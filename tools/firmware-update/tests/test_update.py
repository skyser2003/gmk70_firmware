import hashlib
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import Mock, patch

from update import Manifest, Op, Package, Reply, Updater, UsbLink


def package(profile: str = "debug") -> Package:
    image = struct.pack("<II", 0x20006FF8, 0x08002201) + bytes(1016)
    boot = bytes(64).ljust(0x2000, b"\xff")
    manifest: Manifest = {
        "format": 1,
        "board": "zuoya/gmk70",
        "mcu": "WB32FQ95-256K",
        "profile": profile,
        "app_address": 0x08002000,
        "app_size": len(image),
        "app_crc": zlib.crc32(image),
        "app_sha256": hashlib.sha256(image).hexdigest(),
        "boot_crc": zlib.crc32(boot),
        "boot_sha256": hashlib.sha256(boot).hexdigest(),
        "install_sha256": "unused by application-only transport",
    }
    return Package(image, manifest)


class FakeLink:
    def __init__(self) -> None:
        self.operations: list[tuple[int, int]] = []
        self.sessions = [0, 0]
        self.states = [0, 0]
        self.data = [bytearray(), bytearray()]
        self.crcs = [0, 0]
        self.sizes = [1024, 1024]
        self.fail_data = False
        self.bad_boot = False
        self.bad_role = False
        self.bad_session = False
        self.drop_data_ack = False
        self.pending: bytes | None = None
        self.locked = False

    def exchange(self, packet: bytes) -> bytes:
        target, op = packet[3], packet[4]
        session, offset = struct.unpack_from("<II", packet, 8)
        self.operations.append((target, op))
        if self.locked and op not in (Op.INFO, Op.STATUS):
            return bytes((0x7F, 0x70, 1, target, op, 8)) + bytes(26)
        if op == Op.STATUS:
            assert self.pending is not None
            return self.pending
        result = 0
        if op == Op.BEGIN:
            self.sessions[target] = session
            self.states[target] = 1
            self.sizes[target], self.crcs[target] = struct.unpack_from(
                "<II", packet, 16
            )
            self.data[target] = bytearray()
        elif op == Op.DATA:
            chunk = packet[16 : 16 + packet[5]]
            if offset == len(self.data[target]):
                self.data[target].extend(chunk)
            else:
                assert self.data[target][offset : offset + len(chunk)] == chunk
            if self.fail_data:
                result = 5
        elif op == Op.SEAL:
            assert zlib.crc32(self.data[target]) == self.crcs[target]
            self.states[target] = 2
        elif op == Op.INSTALL:
            self.states[target] = 3
        elif op == Op.ABORT:
            self.states[target] = 0
        role = (3 if target == 0 else 0) ^ int(self.bad_role)
        returned_session = self.sessions[target] ^ int(self.bad_session)
        response = bytes(
            (0x7F, 0x70, 1, target, op, result, self.states[target], role)
        ) + struct.pack(
            "<IIIIII",
            returned_session,
            len(self.data[target]),
            self.sizes[target],
            self.crcs[target],
            1,
            package().manifest["boot_crc"] ^ int(self.bad_boot),
        )
        if op == Op.INSTALL:
            self.states[target] = 0
            self.sessions[target] = 0
        if op == Op.DATA and self.drop_data_ack:
            self.drop_data_ack = False
            raise OSError("simulated dropped ACK")
        self.pending = response
        busy = bytearray(response)
        busy[5] = 1
        return bytes(busy)

    def reconnect(self) -> None:
        pass

    def close(self) -> None:
        pass


class UsbDiscoveryTests(unittest.TestCase):
    def test_stock_and_legacy_firmware_names(self) -> None:
        for manufacturer in ("HS", "LEO"):
            with self.subTest(manufacturer=manufacturer):
                backend = Mock()
                backend.device.return_value.set_nonblocking.return_value = 0
                backend.enumerate.return_value = [
                    {
                        "usage_page": 0xFF60,
                        "usage": 0x61,
                        "manufacturer_string": manufacturer,
                        "path": b"keyboard-raw-hid",
                    },
                    {
                        "usage_page": 1,
                        "usage": 6,
                        "manufacturer_string": manufacturer,
                        "path": b"keyboard-typing",
                    },
                ]
                with patch.dict("sys.modules", {"hid": backend}):
                    link = UsbLink()
                    backend.enumerate.assert_called_once_with(0x342D, 0xE491)
                    backend.device.return_value.open_path.assert_called_once_with(
                        b"keyboard-raw-hid"
                    )
                    link.close()

    def test_unknown_or_ambiguous_devices_are_not_opened(self) -> None:
        for manufacturers in (("other",), ("HS", "LEO")):
            with self.subTest(manufacturers=manufacturers):
                backend = Mock()
                backend.enumerate.return_value = [
                    {
                        "usage_page": 0xFF60,
                        "usage": 0x61,
                        "manufacturer_string": manufacturer,
                        "path": manufacturer.encode(),
                    }
                    for manufacturer in manufacturers
                ]
                with patch.dict("sys.modules", {"hid": backend}):
                    with self.assertRaisesRegex(RuntimeError, "exactly one"):
                        UsbLink()
                    backend.device.return_value.open_path.assert_not_called()


class HidFixture:
    """HID boundary fixture: replies become available when a request is written."""

    def __init__(self, responses: list[list[bytes]]) -> None:
        self.responses = responses
        self.incoming: list[bytes] = []
        self.read_timeouts: list[int] = []
        self.nonblocking = False
        self.nonblocking_changes: list[bool] = []

    def enumerate(self, vendor: int, product: int) -> list[dict[str, object]]:
        return [
            {
                "usage_page": 0xFF60,
                "usage": 0x61,
                "manufacturer_string": "HS",
                "path": b"fixture",
            }
        ]

    def device(self) -> "HidFixture":
        return self

    def open_path(self, path: bytes) -> None:
        pass

    def close(self) -> None:
        pass

    def write(self, data: bytes) -> int:
        self.incoming.extend(self.responses.pop(0))
        return len(data)

    def set_nonblocking(self, enabled: int) -> int:
        self.nonblocking = bool(enabled)
        self.nonblocking_changes.append(self.nonblocking)
        return 0

    def read(self, size: int, timeout: int) -> list[int]:
        # cython-hidapi uses blocking hid_read for timeout == 0 unless the
        # device was explicitly switched to nonblocking mode.
        if timeout == 0 and not self.nonblocking:
            raise AssertionError("HID read would block indefinitely")
        self.read_timeouts.append(timeout)
        return list(self.incoming.pop(0)) if self.incoming else []


def data_reply(session: int, received: int, target: int = 0) -> bytes:
    return bytes(
        (0x7F, 0x70, 1, target, Op.DATA, 0, 1, 0 if target else 3)
    ) + struct.pack("<IIIIII", session, received, 1024, 0, 1, 0)


class UsbExchangeTests(unittest.TestCase):
    def test_connection_drain_restores_blocking_mode(self) -> None:
        backend = HidFixture([])
        backend.incoming.append(data_reply(6, 16))
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            self.assertFalse(backend.incoming)
            self.assertEqual(backend.nonblocking_changes, [True, False])
            self.assertFalse(backend.nonblocking)
            link.close()

    def test_connection_drain_restores_mode_after_read_failure(self) -> None:
        backend = HidFixture([])
        with (
            patch.dict("sys.modules", {"hid": backend}),
            patch.object(backend, "read", side_effect=OSError("drain failed")),
            self.assertRaisesRegex(OSError, "drain failed"),
        ):
            UsbLink()
        self.assertEqual(backend.nonblocking_changes, [True, False])
        self.assertFalse(backend.nonblocking)

    def test_data_reply_ignores_stale_target_session_and_offset(self) -> None:
        expected = data_reply(7, 32)
        backend = HidFixture(
            [
                [
                    bytes(32),
                    data_reply(7, 32, 1),
                    data_reply(6, 32),
                    data_reply(7, 16),
                    expected,
                ]
            ]
        )
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            actual = link.exchange(Updater.packet(0, Op.DATA, 7, 16, bytes(16)))
            self.assertEqual(actual, expected)
            link.close()

    def test_exchange_has_no_timed_stale_drain(self) -> None:
        first, second = data_reply(7, 16), data_reply(7, 32)
        backend = HidFixture([[first], [second]])
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            self.assertEqual(
                link.exchange(Updater.packet(0, Op.DATA, 7, 0, bytes(16))), first
            )
            self.assertEqual(
                link.exchange(Updater.packet(0, Op.DATA, 7, 16, bytes(16))), second
            )
            self.assertNotIn(1, backend.read_timeouts)
            link.close()

    def test_status_correlates_with_the_pending_data_request(self) -> None:
        expected = data_reply(7, 32)
        busy = bytearray(expected)
        busy[5] = 1
        backend = HidFixture(
            [[bytes(busy)], [data_reply(6, 32), data_reply(7, 16), expected]]
        )
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            self.assertEqual(
                link.exchange(Updater.packet(0, Op.DATA, 7, 16, bytes(16))), busy
            )
            self.assertEqual(link.exchange(Updater.packet(0, Op.STATUS)), expected)
            link.close()

    def test_status_without_pending_request_accepts_completed_operation(self) -> None:
        for result in (0, 3):
            with self.subTest(result=result):
                completed = bytearray(data_reply(7, 16))
                completed[5] = result
                backend = HidFixture([[bytes(completed)]])
                with patch.dict("sys.modules", {"hid": backend}):
                    link = UsbLink()
                    self.assertEqual(
                        link.exchange(Updater.packet(0, Op.STATUS)), completed
                    )
                    link.close()

    def test_stale_errors_cannot_abort_a_matching_data_request(self) -> None:
        for polling in (False, True):
            with self.subTest(polling=polling):
                expected = data_reply(7, 32)
                stale_session = bytearray(data_reply(6, 16))
                stale_session[5] = 3
                stale_offset = bytearray(data_reply(7, 0))
                stale_offset[5] = 4
                replies = [bytes(stale_session), bytes(stale_offset), expected]
                busy = bytearray(expected)
                busy[5] = 1
                backend = HidFixture([[bytes(busy)], replies] if polling else [replies])
                with patch.dict("sys.modules", {"hid": backend}):
                    link = UsbLink()
                    reply = Updater(link).command(0, Op.DATA, 7, 16, bytes(16))
                    self.assertEqual(reply.result, 0)
                    self.assertEqual(reply.received, 32)
                    self.assertFalse(backend.incoming)
                    link.close()

    def test_matching_errors_are_reported_even_when_receiver_is_idle(self) -> None:
        for result in (3, 4, 5, 8):
            for polling in (False, True):
                with self.subTest(result=result, polling=polling):
                    error = bytearray(data_reply(7, 16))
                    error[5:7] = bytes((result, 0))
                    busy = bytearray(error)
                    busy[5] = 1
                    backend = HidFixture(
                        [[bytes(busy)], [bytes(error)]] if polling else [[bytes(error)]]
                    )
                    with patch.dict("sys.modules", {"hid": backend}):
                        link = UsbLink()
                        with self.assertRaisesRegex(
                            RuntimeError, "failed|Vial is locked"
                        ):
                            Updater(link).command(0, Op.DATA, 7, 16, bytes(16))
                        self.assertFalse(backend.incoming)
                        link.close()

    def test_status_gate_error_retries_original_request_without_read_timeout(
        self,
    ) -> None:
        expected = data_reply(7, 32)
        busy = bytearray(expected)
        busy[5] = 1
        unavailable = bytearray(Updater.packet(0, Op.STATUS))
        unavailable[5] = 7
        backend = HidFixture([[bytes(busy)], [bytes(unavailable)], [expected]])
        with (
            patch.dict("sys.modules", {"hid": backend}),
            patch("update.time.sleep") as sleep,
        ):
            link = UsbLink()
            reply = Updater(link).command(0, Op.DATA, 7, 16, bytes(16))
            self.assertEqual(reply.received, 32)
            sleep.assert_called_once_with(0.05)
            self.assertFalse(backend.responses)
            link.close()


class PackageTests(unittest.TestCase):
    def test_package_and_corruption(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            value = package()
            (root / "update.json").write_text(json.dumps(value.manifest))
            (root / "application.bin").write_bytes(value.image)
            (root / "resident.bin").write_bytes(bytes(64))
            self.assertEqual(Package.load(root), value)
            (root / "application.bin").write_bytes(value.image[:-1] + b"\xff")
            with self.assertRaises(ValueError):
                Package.load(root)

    def test_response_target_and_size(self) -> None:
        with self.assertRaises(RuntimeError):
            Reply.decode(bytes(32), 0)
        with self.assertRaises(RuntimeError):
            Reply.decode(bytes((0x7F, 0x70, 1, 0)) + bytes(28), 1)
        with self.assertRaises(ValueError):
            Updater.packet(2, Op.INFO)

    def test_vial_package_and_unsupported_profile(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            value = package("vial")
            (root / "update.json").write_text(json.dumps(value.manifest))
            (root / "application.bin").write_bytes(value.image)
            (root / "resident.bin").write_bytes(bytes(64))
            self.assertEqual(Package.load(root), value)
            value.manifest["profile"] = "unknown"
            (root / "update.json").write_text(json.dumps(value.manifest))
            with self.assertRaises(ValueError):
                Package.load(root)


class UpdaterTests(unittest.TestCase):
    def setUp(self) -> None:
        self.sleeper = patch("update.time.sleep")
        self.sleeper.start()
        self.addCleanup(self.sleeper.stop)

    def test_both_updates_right_before_left_and_verifies(self) -> None:
        link = FakeLink()
        link.drop_data_ack = True
        Updater(link).install_both(package())
        installs = [target for target, op in link.operations if op == Op.INSTALL]
        self.assertEqual(installs, [1, 0])
        self.assertEqual(bytes(link.data[0]), package().image)
        self.assertEqual(bytes(link.data[1]), package().image)

    def test_vial_retains_usb_updater_on_both_halves(self) -> None:
        link = FakeLink()
        Updater(link).install_both(package("vial"))
        self.assertEqual(
            [target for target, op in link.operations if op == Op.INSTALL], [1, 0]
        )

    def test_locked_vial_reports_unlock_without_staging(self) -> None:
        link = FakeLink()
        link.locked = True
        Updater(link).inspect(0, package("vial"))
        Updater(link).inspect(1, package("vial"))
        with self.assertRaisesRegex(RuntimeError, "Escape.*Shift"):
            Updater(link).install_both(package("vial"))
        self.assertEqual(link.data, [bytearray(), bytearray()])
        self.assertFalse(any(op in (Op.DATA, Op.INSTALL) for _, op in link.operations))

    def test_right_restart_must_be_verified_before_left_is_started(self) -> None:
        link = FakeLink()
        updater = Updater(link)
        with (
            patch.object(updater, "verify_restart", side_effect=TimeoutError),
            self.assertRaises(TimeoutError),
        ):
            updater.install_both(package("vial"))
        self.assertNotIn((0, Op.BEGIN), link.operations)
        self.assertEqual(
            [target for target, op in link.operations if op == Op.INSTALL], [1]
        )

    def test_failed_transfer_aborts_without_installing_either_half(self) -> None:
        link = FakeLink()
        link.fail_data = True
        with self.assertRaises(RuntimeError):
            Updater(link).install_both(package())
        self.assertNotIn((0, Op.INSTALL), link.operations)
        self.assertNotIn((1, Op.INSTALL), link.operations)
        self.assertIn((1, Op.ABORT), link.operations)

    def test_wrong_boot_or_role_does_not_start_update(self) -> None:
        for field in ("bad_boot", "bad_role"):
            with self.subTest(field=field):
                link = FakeLink()
                setattr(link, field, True)
                with self.assertRaises(RuntimeError):
                    Updater(link).install_both(package())
                self.assertTrue(
                    all(op in (Op.INFO, Op.STATUS) for _, op in link.operations)
                )

    def test_session_mismatch_is_rejected(self) -> None:
        link = FakeLink()
        link.bad_session = True
        with self.assertRaisesRegex(RuntimeError, "session"):
            Updater(link).command(0, Op.BEGIN, 5, data=struct.pack("<III", 1024, 0, 1))

    def test_first_busy_poll_has_no_fixed_sleep(self) -> None:
        elapsed = [0.0]
        with patch(
            "update.time.sleep",
            side_effect=lambda delay: elapsed.__setitem__(0, elapsed[0] + delay),
        ):
            Updater(FakeLink()).command(
                0, Op.BEGIN, 5, data=struct.pack("<III", 1024, 0, 1)
            )
        self.assertEqual(elapsed[0], 0)

    def test_stale_data_status_cannot_acknowledge_the_next_chunk(self) -> None:
        class StaleStatusLink(FakeLink):
            stale = True

            def exchange(self, packet: bytes) -> bytes:
                response = super().exchange(packet)
                if self.stale and packet[4] == Op.STATUS and response[4] == Op.DATA:
                    self.stale = False
                    delayed = bytearray(response)
                    struct.pack_into("<I", delayed, 12, 0)
                    return bytes(delayed)
                return response

        link = StaleStatusLink()
        updater = Updater(link)
        updater.command(0, Op.BEGIN, 5, data=struct.pack("<III", 1024, 0, 1))
        reply = updater.command(0, Op.DATA, 5, 0, bytes(16))
        self.assertEqual(reply.received, 16)
        self.assertEqual(len(link.data[0]), 16)

    def test_busy_poll_backoff_is_short_and_bounded(self) -> None:
        class DelayedLink(FakeLink):
            polls = 0

            def exchange(self, packet: bytes) -> bytes:
                response = super().exchange(packet)
                if packet[4] == Op.STATUS:
                    self.polls += 1
                    if self.polls <= 3:
                        delayed = bytearray(response)
                        delayed[5] = 1
                        return bytes(delayed)
                return response

        waits: list[float] = []
        with patch("update.time.sleep", side_effect=waits.append):
            Updater(DelayedLink()).command(
                0, Op.BEGIN, 5, data=struct.pack("<III", 1024, 0, 1)
            )
        self.assertLess(sum(waits), 0.01)
        self.assertTrue(all(0 < wait <= 0.004 for wait in waits))

    def test_release_cannot_remove_left_debug_control(self) -> None:
        link = FakeLink()
        with self.assertRaises(ValueError):
            Updater(link).install_both(package("release"))
        self.assertEqual(link.operations, [])


if __name__ == "__main__":
    unittest.main()
