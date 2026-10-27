"""Exercise the explicit trial client through an in-memory device boundary."""

import io
import json
import struct
import unittest
from collections.abc import Callable
from unittest.mock import patch

from test_update import HidFixture

from trial import TrialPackage
from trial_run import TrialClient, TrialOp, TrialUsbLink, main


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
        self.capabilities = (7, 0, 0x20000040, 0x20006FC0)
        self.caps_supported = True
        self.trace_words = (0xE0042004, 0, 0x10000, 0x08003000, 1)

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
        elif op == 0x13:
            if self.caps_supported:
                struct.pack_into("<IIII", reply, 16, *self.capabilities)
            else:
                reply[5] = 2
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
        elif op in (TrialOp.RUN, TrialOp.PROBE, 0x14, 0x16):
            if op == TrialOp.RUN:
                assert self.state == 2
            self.action = 1 if op == TrialOp.RUN else 2
            self.state = 0
            if self.changed_identity:
                self.boot_crc ^= 1
        elif op == 0x15:
            page = struct.unpack_from("<I", request, 12)[0]
            words = (
                self.trace_words[:4] if page == 0 else (self.trace_words[4], 0, 0, 0)
            )
            struct.pack_into("<IIII", reply, 16, *words)
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


class HealthDevice(Device):
    def __init__(self, clock: Clock) -> None:
        super().__init__()
        self.clock = clock
        self.capabilities = (71, 0, 0x20000040, 0x20006FC0)
        self.health_words: Callable[[], tuple[int, int, int, int]] = lambda: (
            int(clock.value * 1000) & 0xFFFFFFFF,
            int(clock.value * 100) & 0xFFFFFFFF,
            1,
            0x31484D47,
        )

    def exchange(self, request: bytes) -> bytes:
        if request[4] == TrialOp.TRACE:
            assert struct.unpack_from("<I", request, 12)[0] == 1
            reply = bytearray(response(request))
            struct.pack_into("<IIII", reply, 16, *self.health_words())
            return bytes(reply)
        return super().exchange(request)


class TrialClientTests(unittest.TestCase):
    def client(self, device: Device, clock: Clock | None = None) -> TrialClient:
        timer = clock or Clock()
        return TrialClient(device, now=timer.now, sleep=timer.sleep)

    def test_health_qualifies_sixty_seconds_despite_short_relay_retries(self) -> None:
        clock = Clock()
        device = HealthDevice(clock)
        exchange = device.exchange
        last_retry = [0.0]

        def relay(request: bytes) -> bytes:
            if clock.value > 10 and clock.value - last_retry[0] > 1:
                last_retry[0] = clock.value
                return response(request, result=7)
            return exchange(request)

        device.exchange = relay  # type: ignore[method-assign]
        report = self.client(device, clock).probe(1)
        self.assertGreaterEqual(report["stable_seconds"], 60)
        self.assertGreater(report["relay_retries"], 30)
        self.assertEqual(report["health"]["flags"], 1)

    def test_health_rejects_reboot_or_stuck_watchdog_feeds(self) -> None:
        for broken in ("reboot", "feeds"):
            with self.subTest(broken=broken):
                clock = Clock()
                device = HealthDevice(clock)
                original = device.health_words

                def words(
                    original: Callable[[], tuple[int, int, int, int]] = original,
                    broken: str = broken,
                    clock: Clock = clock,
                ) -> tuple[int, int, int, int]:
                    uptime, feeds, flags, magic = original()
                    if broken == "reboot" and clock.value > 15:
                        uptime = 100
                    if broken == "feeds":
                        feeds = 10
                    return uptime, feeds, flags, magic

                device.health_words = words
                with self.assertRaisesRegex(RuntimeError, "uptime|feed"):
                    self.client(device, clock).probe(1)

    def test_health_gap_and_hidden_retries_restart_window(self) -> None:
        for failure in ("gap", "usb", "correlation"):
            with self.subTest(failure=failure):
                clock = Clock()
                device = HealthDevice(clock)
                exchange = device.exchange
                interrupted: list[float] = []

                def interrupt(
                    request: bytes,
                    clock: Clock = clock,
                    interrupted: list[float] = interrupted,
                    failure: str = failure,
                    exchange: Callable[[bytes], bytes] = exchange,
                ) -> bytes:
                    if clock.value > 15 and not interrupted:
                        interrupted.append(clock.value)
                        if failure == "gap":
                            clock.sleep(2)
                        elif failure == "usb":
                            raise OSError("hidden retry")
                        else:
                            bad = bytearray(response(request))
                            bad[1] ^= 1
                            return bytes(bad)
                    return exchange(request)

                device.exchange = interrupt  # type: ignore[method-assign]
                report = self.client(device, clock).probe(1)
                self.assertGreaterEqual(report["stable_seconds"], 60)
                self.assertGreaterEqual(clock.value - interrupted[0], 60)

    def test_health_repeated_two_second_gaps_never_qualify(self) -> None:
        clock = Clock()
        device = HealthDevice(clock)
        original = device.health_words

        def slow_words() -> tuple[int, int, int, int]:
            clock.sleep(2)
            return original()

        device.health_words = slow_words
        client = self.client(device, clock)
        with self.assertRaises(TimeoutError):
            client.probe(1)
        self.assertLess(float(str(client.last_report["stable_seconds"])), 60)
        self.assertLess(clock.value, 120)

    def test_health_initial_uptime_must_exceed_two_seconds(self) -> None:
        clock = Clock()
        device = HealthDevice(clock)
        device.health_words = lambda: (
            max(0, int((clock.value - 9) * 1000)),
            int(clock.value * 100),
            1,
            0x31484D47,
        )
        report = self.client(device, clock).probe(1)
        self.assertGreater(report["elapsed_seconds"], 71)

    def test_health_accepts_modular_uptime_and_feed_wrap(self) -> None:
        clock = Clock()
        device = HealthDevice(clock)
        device.health_words = lambda: (
            (0xFFFFC000 + int(clock.value * 1000)) & 0xFFFFFFFF,
            (0xFFFFF000 + int(clock.value * 100)) & 0xFFFFFFFF,
            1,
            0x31484D47,
        )
        self.assertGreaterEqual(
            self.client(device, clock).probe(1)["stable_seconds"], 60
        )

    def test_health_rejects_bad_magic_or_flags(self) -> None:
        for flags, magic in ((1, 0), (4, 0x31484D47), (0, 0x31484D47), (3, 0x31484D47)):
            with self.subTest(flags=flags, magic=magic):
                clock = Clock()
                device = HealthDevice(clock)

                def invalid_words(
                    flags: int = flags, magic: int = magic
                ) -> tuple[int, int, int, int]:
                    return 9000, 100, flags, magic

                device.health_words = invalid_words
                with self.assertRaises(RuntimeError):
                    self.client(device, clock).probe(1)

    def test_fault_test_requires_capability_and_captured_undefined_instruction(
        self,
    ) -> None:
        device = Device()
        device.flags = 40
        device.capabilities = (57, 12000, 0x20000040, 0x20006FC0)
        report = self.client(device).fault(0)
        self.assertTrue(report["success"])
        self.assertEqual(report["trace"]["address"], 0xE0042004)
        self.assertEqual(report["trace"]["pc"], 0x08003000)
        self.assertEqual(device.operations.count(0x16), 1)
        self.assertLess(device.operations.index(0x13), device.operations.index(0x16))

    def test_fault_test_cannot_succeed_without_fault_evidence(self) -> None:
        device = Device()
        device.flags = 40
        device.capabilities = (57, 12000, 0x20000040, 0x20006FC0)
        device.trace_words = (0, 0, 0, 0, 0)
        with self.assertRaisesRegex(RuntimeError, "captured undefined"):
            self.client(device).fault(0)

    def test_failed_watchdog_still_captures_trace(self) -> None:
        device = Device()
        device.flags = 40
        device.capabilities = (57, 12000, 0x20000040, 0x20006FC0)
        client = self.client(device)
        with self.assertRaisesRegex(RuntimeError, "Unexpected trial reset"):
            client.probe(0)
        self.assertEqual(
            client.last_report["trace"],
            {
                "address": 0xE0042004,
                "value": 0,
                "cfsr": 0x10000,
                "pc": 0x08003000,
                "flags": 1,
            },
        )

    def test_probe_retries_unavailable_and_verifies_watchdog_return(self) -> None:
        device, clock = Device(), Clock()
        device.unavailable = 3
        report = self.client(device, clock).probe(1)
        self.assertTrue(report["success"])
        self.assertGreaterEqual(report["retries"], 3)
        self.assertGreaterEqual(clock.value, 9)
        self.assertEqual(device.operations.count(TrialOp.PROBE), 1)

    def test_recovery_requires_sustained_read_only_verification(self) -> None:
        device, clock = Device(), Clock()
        report = self.client(device, clock).probe(0)
        self.assertGreaterEqual(report["elapsed_seconds"], 39)
        self.assertGreaterEqual(report["stable_seconds"], 30)
        after_probe = device.operations[device.operations.index(TrialOp.PROBE) + 1 :]
        self.assertGreater(after_probe.count(TrialOp.RESULT), 2)
        self.assertLessEqual(
            set(after_probe), {TrialOp.STATUS, TrialOp.INFO, TrialOp.RESULT}
        )

    def test_disappearance_after_first_verified_return_is_not_success(self) -> None:
        device, clock = Device(), Clock()
        exchange = device.exchange

        def disappear(request: bytes) -> bytes:
            if clock.value >= 10:
                raise OSError("Watchdog recovery disappeared")
            return exchange(request)

        device.exchange = disappear  # type: ignore[method-assign]
        client = self.client(device, clock)
        with self.assertRaises(TimeoutError):
            client.probe(0)
        self.assertFalse(client.last_report["success"])
        self.assertIn("result", client.last_report)
        self.assertLess(clock.value, 100)
        self.assertEqual(device.operations.count(TrialOp.PROBE), 1)

    def test_transport_retry_restarts_the_stable_window(self) -> None:
        device, clock = Device(), Clock()
        exchange = device.exchange
        interrupted_at: list[float] = []

        def interrupt_once(request: bytes) -> bytes:
            if clock.value >= 15 and not interrupted_at:
                interrupted_at.append(clock.value)
                raise OSError("Temporary USB loss")
            return exchange(request)

        device.exchange = interrupt_once  # type: ignore[method-assign]
        report = self.client(device, clock).probe(0)
        self.assertTrue(report["success"])
        self.assertTrue(interrupted_at)
        self.assertGreaterEqual(clock.value - interrupted_at[0], 30)
        self.assertGreaterEqual(report["stable_seconds"], 30)

    def test_run_uploads_exact_bytes_before_run(self) -> None:
        device = Device()
        report = self.client(device).run(package(), expect_reset="watchdog")
        self.assertEqual(bytes(device.data), package().image)
        self.assertLess(
            device.operations.index(TrialOp.SEAL), device.operations.index(TrialOp.RUN)
        )
        self.assertLess(device.operations.index(0x13), device.operations.index(2))
        self.assertEqual(report["package_sha256"], package().manifest["sha256"])

    def test_probe_reads_and_records_capabilities_before_reset(self) -> None:
        device = Device()
        device.capabilities = (25, 12000, 0x20000040, 0x20006FC0)
        report = self.client(device).probe(0)
        self.assertLess(device.operations.index(0x13), device.operations.index(0x11))
        self.assertEqual(
            report["capabilities"],
            {
                "bits": 25,
                "fallback_ms": 12000,
                "ram_start": 0x20000040,
                "ram_end": 0x20006FC0,
            },
        )

    def test_runtime_backend_refuses_run_before_uploading(self) -> None:
        device = Device()
        device.capabilities = (25, 12000, 0x20000040, 0x20006FC0)
        client = self.client(device)
        with self.assertRaisesRegex(RuntimeError, "RUN.*capability"):
            client.run(package(), expect_reset="watchdog")
        self.assertEqual(bytes(device.data), b"")
        self.assertFalse(set(device.operations) & {2, 3, 4, 0x10})
        self.assertEqual(
            client.last_report["capabilities"],
            {
                "bits": 25,
                "fallback_ms": 12000,
                "ram_start": 0x20000040,
                "ram_end": 0x20006FC0,
            },
        )

    def test_probe_requires_probe_capability(self) -> None:
        device = Device()
        device.capabilities = (6, 0, 0x20000040, 0x20006FC0)
        with self.assertRaisesRegex(RuntimeError, "PROBE.*capability"):
            self.client(device).probe(1)
        self.assertNotIn(0x11, device.operations)

    def test_backend_without_caps_fails_before_any_mutation(self) -> None:
        for action in ("probe", "run", "fallback"):
            with self.subTest(action=action):
                device = Device()
                device.caps_supported = False
                client = self.client(device)
                with self.assertRaisesRegex(RuntimeError, "CAPS.*required"):
                    if action == "run":
                        client.run(package(), expect_reset="watchdog")
                    elif action == "fallback":
                        client.fallback(1)
                    else:
                        client.probe(1)
                self.assertFalse(set(device.operations) & {2, 3, 4, 0x10, 0x11, 0x14})

    def test_capability_query_rejects_stale_nonce_word(self) -> None:
        device = Device()
        requests: list[bytes] = []

        def exchange(request: bytes) -> bytes:
            requests.append(request)
            reply = bytearray(response(request))
            if len(requests) == 1:
                reply[12] ^= 1
            struct.pack_into("<IIII", reply, 16, 25, 12000, 0x20000040, 0x20006FC0)
            return bytes(reply)

        device.exchange = exchange  # type: ignore[method-assign]
        client = self.client(device)
        self.assertEqual(client.capabilities(1).bits, 25)
        self.assertEqual(len(requests), 2)
        self.assertNotEqual(requests[0][8:16], bytes(8))

    def test_capability_queries_use_fresh_nonce_words(self) -> None:
        device = Device()
        requests: list[bytes] = []
        exchange = device.exchange

        def record(request: bytes) -> bytes:
            if request[4] == 0x13:
                requests.append(request)
            return exchange(request)

        device.exchange = record  # type: ignore[method-assign]
        with patch("trial_run.nonce", side_effect=(1, 2, 3, 4)):
            client = self.client(device)
            client.capabilities(0)
            client.capabilities(0)
        self.assertEqual(
            [request[8:16] for request in requests],
            [
                b"\x01\0\0\0\x02\0\0\0",
                b"\x03\0\0\0\x04\0\0\0",
            ],
        )

    def test_fallback_requires_its_capability_before_reset(self) -> None:
        device = Device()
        with self.assertRaisesRegex(RuntimeError, "FALLBACK_TEST.*capability"):
            self.client(device).fallback(0)
        self.assertNotIn(0x14, device.operations)

    def test_fallback_requires_software_reset_and_probe_result_action(self) -> None:
        for flags in (8, 40):
            with self.subTest(flags=flags):
                device = Device()
                device.capabilities = (25, 12000, 0x20000040, 0x20006FC0)
                device.flags = flags
                report = self.client(device).fallback(0)
                self.assertTrue(report["success"])
                self.assertEqual(report["action"], "fallback")
                self.assertEqual(report["expected_reset"], "software")
                self.assertEqual(report["result"]["action"], 2)
                self.assertIn(0x14, device.operations)
                self.assertFalse(set(device.operations) & {2, 3, 4, 0x10, 0x11})

    def test_fallback_rejects_hardware_or_ambiguous_reset_flags(self) -> None:
        for flags in (0, 4, 12, 36, 44):
            with self.subTest(flags=flags):
                device = Device()
                device.capabilities = (25, 12000, 0x20000040, 0x20006FC0)
                device.flags = flags
                with self.assertRaisesRegex(RuntimeError, "reset cause"):
                    self.client(device).fallback(1)

    def test_fallback_waits_for_twelve_second_reset_and_usb_recovery(self) -> None:
        device, clock = Device(), Clock()
        device.capabilities = (25, 12000, 0x20000040, 0x20006FC0)
        device.flags = 8
        reconnect_times: list[float] = []

        def reconnect() -> None:
            reconnect_times.append(clock.value)
            if clock.value < 12.3:
                raise OSError("The software fallback has not reset yet")

        device.reconnect = reconnect  # type: ignore[method-assign]
        report = self.client(device, clock).fallback(0)
        self.assertTrue(report["success"])
        self.assertGreaterEqual(report["elapsed_seconds"], 12.3)
        self.assertGreaterEqual(reconnect_times[0], 9)
        self.assertLess(report["elapsed_seconds"], 54)

    def test_fallback_cli_dispatches_explicit_software_proof(self) -> None:
        device, clock = Device(), Clock()
        device.capabilities = (25, 12000, 0x20000040, 0x20006FC0)
        device.flags = 8
        output = io.StringIO()
        with (
            patch("trial_run.TrialUsbLink", return_value=device),
            patch("trial_run.TrialClient", return_value=self.client(device, clock)),
            patch("sys.argv", ["trial_run.py", "fallback", "--target", "left"]),
            patch("sys.stdout", output),
        ):
            self.assertEqual(main(), 0)
        report = json.loads(output.getvalue())
        self.assertEqual(report["action"], "fallback")
        self.assertEqual(report["result"]["reset_flags"], 8)

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
            self.assertEqual(link.observation_errors, 3)
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
