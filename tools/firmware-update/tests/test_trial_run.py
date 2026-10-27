"""Exercise the explicit trial client through an in-memory device boundary."""

import struct
import unittest
from unittest.mock import patch

from test_update import HidFixture

from trial import TrialPackage
from trial_run import TrialClient, TrialOp, TrialUsbLink


def package() -> TrialPackage:
    image = bytearray(1024)
    struct.pack_into("<IIII", image, 0, 0x20006FC0, *([0x08020201] * 3))
    return TrialPackage.create(bytes(image), "right")


def response(request: bytes, *, result: int = 0, session: int | None = None) -> bytes:
    reply = bytearray(request)
    reply[5:8] = bytes((result, 0, 3 if request[3] == 0 else 0))
    if session is not None:
        struct.pack_into("<I", reply, 8, session)
    return bytes(reply)


class Clock:
    def __init__(self) -> None:
        self.value = 0.0

    def now(self) -> float:
        return self.value

    def sleep(self, seconds: float) -> None:
        self.value += max(seconds, 0.001)


class Device:
    def __init__(self) -> None:
        self.operations: list[int] = []
        self.data = bytearray()
        self.state = 0
        self.pending: bytes | None = None
        self.action = 0
        self.flags = 4
        self.phase = 3
        self.boot_crc = 0x12345678
        self.fail_seal = False
        self.unavailable = 0
        self.changed_identity = False
        self.reconnections = 0

    def exchange(self, request: bytes) -> bytes:
        op = request[4]
        self.operations.append(op)
        if self.unavailable:
            self.unavailable -= 1
            return response(request, result=7)
        if op == TrialOp.STATUS:
            assert self.pending is not None
            return self.pending
        reply = bytearray(response(request))
        if op == TrialOp.INFO:
            struct.pack_into("<IIII", reply, 16, 92000, 0xABCDEF01, 2, self.boot_crc)
        elif op == TrialOp.BEGIN:
            self.state = 1
        elif op == TrialOp.DATA:
            offset = struct.unpack_from("<I", request, 12)[0]
            assert offset == len(self.data)
            self.data.extend(request[16 : 16 + request[5]])
            struct.pack_into("<I", reply, 12, len(self.data))
        elif op == TrialOp.SEAL:
            self.state = 2
            if self.fail_seal:
                reply[5] = 6
        elif op in (TrialOp.RUN, TrialOp.PROBE):
            if op == TrialOp.RUN:
                assert self.state == 2
            self.action = 1 if op == TrialOp.RUN else 2
            self.state = 0
            if self.changed_identity:
                self.boot_crc ^= 1
        elif op == TrialOp.RESULT:
            image = package()
            struct.pack_into(
                "<IIII",
                reply,
                16,
                self.action | self.phase << 8,
                self.flags,
                len(self.data),
                image.manifest["crc32"] if self.data else 0,
            )
        reply[6] = self.state
        self.pending = bytes(reply)
        busy = bytearray(reply)
        busy[5] = 1
        return bytes(busy)

    def reconnect(self) -> None:
        self.reconnections += 1

    def close(self) -> None:
        pass


class TrialClientTests(unittest.TestCase):
    def client(self, device: Device, clock: Clock | None = None) -> TrialClient:
        timer = clock or Clock()
        return TrialClient(device, now=timer.now, sleep=timer.sleep)

    def test_probe_retries_unavailable_and_verifies_watchdog_return(self) -> None:
        device, clock = Device(), Clock()
        device.unavailable = 3
        report = self.client(device, clock).probe(1)
        self.assertTrue(report["success"])
        self.assertGreaterEqual(report["retries"], 3)
        self.assertGreaterEqual(clock.value, 9)
        self.assertEqual(device.operations.count(TrialOp.PROBE), 1)

    def test_run_uploads_exact_bytes_before_run(self) -> None:
        device = Device()
        report = self.client(device).run(package(), expect_reset="watchdog")
        self.assertEqual(bytes(device.data), package().image)
        self.assertLess(
            device.operations.index(TrialOp.SEAL), device.operations.index(TrialOp.RUN)
        )
        self.assertEqual(report["package_sha256"], package().manifest["sha256"])

    def test_failed_seal_never_sends_run(self) -> None:
        device = Device()
        device.fail_seal = True
        with self.assertRaises(RuntimeError):
            self.client(device).run(package(), expect_reset="watchdog")
        self.assertNotIn(TrialOp.RUN, device.operations)

    def test_terminal_wrong_reset_or_action_is_not_success(self) -> None:
        for flags, phase in ((0, 3), (8, 3), (12, 3), (20, 3), (4, 4)):
            device = Device()
            device.flags, device.phase = flags, phase
            with (
                self.subTest(flags=flags, phase=phase),
                self.assertRaises(RuntimeError),
            ):
                self.client(device).probe(1)

    def test_software_return_is_explicitly_bound(self) -> None:
        device = Device()
        device.flags = 40
        self.assertTrue(
            self.client(device).run(package(), expect_reset="software")["success"]
        )

    def test_changed_resident_identity_fails_recovery(self) -> None:
        device = Device()
        device.changed_identity = True
        with self.assertRaisesRegex(RuntimeError, "identity"):
            self.client(device).probe(1)

    def test_left_recovery_reopens_usb(self) -> None:
        device = Device()
        self.client(device).probe(0)
        self.assertGreater(device.reconnections, 0)

    def test_persistent_unavailability_cannot_claim_a_probe(self) -> None:
        device = Device()
        device.unavailable = 10000
        client = self.client(device)
        with self.assertRaises(TimeoutError):
            client.probe(1)
        self.assertNotIn(TrialOp.PROBE, device.operations)
        self.assertFalse(client.last_report["success"])

    def test_result_must_match_the_acknowledged_action(self) -> None:
        device = Device()
        exchange = device.exchange

        def mismatched_result(request: bytes) -> bytes:
            if request[4] == TrialOp.RESULT:
                device.action = 1
            return exchange(request)

        device.exchange = mismatched_result  # type: ignore[method-assign]
        with self.assertRaisesRegex(RuntimeError, "RETURNED"):
            self.client(device).probe(1)

    def test_real_usb_boundary_filters_stale_busy_and_cached_completions(self) -> None:
        request = TrialClient.packet(1, TrialOp.PROBE, 42)
        wrong_namespace = bytearray(response(request, result=1))
        wrong_namespace[1] = 0x70
        backend = HidFixture(
            [
                [
                    bytes(wrong_namespace),
                    response(request, result=1, session=41),
                    response(request, result=1),
                ],
                [response(request, session=41), response(request)],
            ]
        )
        with patch.dict("sys.modules", {"hid": backend}):
            link = TrialUsbLink()
            reply = self.client_with_usb(link).command(1, TrialOp.PROBE, 42)
            self.assertEqual(reply.session, 42)
            self.assertFalse(backend.incoming)
            link.close()

    @staticmethod
    def client_with_usb(link: TrialUsbLink) -> TrialClient:
        clock = Clock()
        return TrialClient(link, now=clock.now, sleep=clock.sleep)

    def test_stale_busy_namespace_operation_and_session_are_ignored(self) -> None:
        timer = Clock()
        wanted = TrialClient.packet(1, TrialOp.PROBE, 42)
        stale_namespace = bytearray(response(wanted, result=1))
        stale_namespace[1] = 0x70
        stale_operation = bytearray(response(wanted, result=1))
        stale_operation[4] = TrialOp.RUN
        replies = [
            bytes(stale_namespace),
            bytes(stale_operation),
            response(wanted, result=1, session=41),
            response(wanted),
        ]
        device = Device()
        device.exchange = lambda request: replies.pop(0)  # type: ignore[method-assign]
        reply = TrialClient(device, now=timer.now, sleep=timer.sleep).command(
            1, TrialOp.PROBE, 42
        )
        self.assertEqual(reply.session, 42)
        self.assertFalse(replies)


if __name__ == "__main__":
    unittest.main()
