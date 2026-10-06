import unittest
from unittest.mock import Mock, patch

import read_battery
from read_battery import DiagnosticPage, DiagnosticReader


class FakeConnection:
    def __init__(self, response: bytes) -> None:
        self.response = response
        self.requests: list[bytes] = []

    def write(self, buff: bytes) -> int:
        self.requests.append(buff)
        return len(buff)

    def read(self, max_length: int, timeout_ms: int = 0) -> list[int]:
        return list(self.response)


class ReaderTests(unittest.TestCase):
    def test_usb_discovery_accepts_current_and_legacy_manufacturers(self) -> None:
        for manufacturer in ("HS", "LEO"):
            with self.subTest(manufacturer=manufacturer):
                backend = Mock()
                backend.enumerate.return_value = [
                    {
                        "usage_page": 0xFF60,
                        "usage": 0x61,
                        "manufacturer_string": manufacturer,
                        "path": b"keyboard",
                    },
                    {
                        "usage_page": 1,
                        "usage": 6,
                        "manufacturer_string": manufacturer,
                        "path": b"keyboard-input",
                    },
                ]
                with (
                    patch.object(read_battery, "hid", backend),
                    patch.object(DiagnosticReader, "report", return_value="report"),
                ):
                    self.assertEqual(DiagnosticReader.run(), "report")
                backend.enumerate.assert_called_once_with(0x342D, 0xE491)
                backend.device.return_value.open_path.assert_called_once_with(
                    b"keyboard"
                )
                backend.device.return_value.close.assert_called_once_with()

    def test_usb_discovery_rejects_unknown_and_multiple_devices(self) -> None:
        def interface(manufacturer: str) -> dict[str, object]:
            return {
                "usage_page": 0xFF60,
                "usage": 0x61,
                "manufacturer_string": manufacturer,
                "path": b"keyboard",
            }

        for interfaces in (
            [],
            [interface("unknown")],
            [interface("HS"), interface("LEO")],
        ):
            backend = Mock()
            backend.enumerate.return_value = interfaces
            with (
                patch.object(read_battery, "hid", backend),
                self.assertRaises(RuntimeError),
            ):
                DiagnosticReader.run()
            backend.device.assert_not_called()

    def test_complete_report_reads_only_three_pages(self) -> None:
        class ReportConnection(FakeConnection):
            def read(self, max_length: int, timeout_ms: int = 0) -> list[int]:
                page = self.requests[-1][3]
                payloads = (
                    "000003e80000000200000001042306060250320d20047f000000",
                    "7e1b03000001f40000000200000001042306060250320d20047f",
                    "00000001000000010000000a0000000500000000000002580000",
                )
                return list(
                    bytes((2, 0x80, page, 0x47, 0x42, 1))
                    + bytes.fromhex(payloads[page])
                )

        connection = ReportConnection(b"")
        report = DiagnosticReader(connection).report()
        self.assertIn("keycode=0x7E1B, stage=battery_handler_completed", report)
        self.assertIn("battery_draw_calls=5", report)
        self.assertIn("last_battery_draw_uptime_ms=600", report)
        self.assertEqual(
            connection.requests, [DiagnosticPage.request(page) for page in (0, 1, 2)]
        )

    def test_only_eight_fixed_getters_are_constructible(self) -> None:
        for page in range(8):
            self.assertEqual(
                DiagnosticPage.request(page), bytes((0, 2, 0x80, page)) + bytes(29)
            )
        for invalid in (-1, 8, 255, 256):
            with self.assertRaises(ValueError):
                DiagnosticPage.request(invalid)

    def test_reads_frame_page_only_when_advertised(self) -> None:
        class FrameConnection(FakeConnection):
            def read(self, max_length: int, timeout_ms: int = 0) -> list[int]:
                page = self.requests[-1][3]
                payload = bytearray(26)
                if page == 2:
                    payload[24] = 1
                elif page == 3:
                    payload[:13] = bytes.fromhex("0000006400000005000001f402")
                    payload[13:22] = bytes((12, 23, 34, 80, 0, 0, 23, 34, 45))
                return list(bytes((2, 0x80, page, 0x47, 0x42, 1)) + payload)

        connection = FrameConnection(b"")
        report = DiagnosticReader(connection).report()
        self.assertIn("overlay_frames=5", report)
        self.assertIn("last_overlay_after_rgb=(80, 0, 0)", report)
        self.assertIn("current_rgb=(23, 34, 45)", report)
        self.assertEqual(
            connection.requests, [DiagnosticPage.request(p) for p in range(4)]
        )

    def test_reads_output_probe_only_when_advertised(self) -> None:
        class OutputConnection(FakeConnection):
            def read(self, max_length: int, timeout_ms: int = 0) -> list[int]:
                page = self.requests[-1][3]
                payload = bytearray(26)
                if page == 2:
                    payload[24] = 6  # Sync probe, independent of page 3 capability.
                elif page >= 4:
                    payload[:14] = bytes.fromhex("0000000a00000000000030000410")
                    payload[14:22] = bytes((0, 0, 0, 80, 80, 80, 1, 35))
                return list(bytes((2, 0x80, page, 0x47, 0x42, 1)) + payload)

        connection = OutputConnection(b"")
        report = DiagnosticReader(connection).report()
        self.assertIn("phase 3 (LED 33 white", report)
        self.assertIn("LED transmission: synchronous", report)
        self.assertIn("packet_bytes=1040", report)
        self.assertIn("encoded_led33_rgb=(80, 80, 80)", report)
        self.assertIn("sampled_symbols_valid=True", report)
        self.assertNotIn("page3=", report)
        self.assertIn("page7=028007", report)
        self.assertEqual(
            connection.requests,
            [DiagnosticPage.request(p) for p in (0, 1, 2, 4, 5, 6, 7)],
        )

    def test_rejects_old_firmware_wrong_page_and_bad_length(self) -> None:
        for response in (
            bytes((255,)) + bytes(31),
            bytes(32),
            bytes((2, 0x80, 1, 0x47, 0x42, 1)) + bytes(26),
            bytes((2, 0x80, 0, 0x47, 0x42, 2)) + bytes(26),
            bytes(31),
        ):
            connection = FakeConnection(response)
            with self.assertRaises(ValueError):
                DiagnosticReader(connection).read_page(0)
            self.assertEqual(connection.requests, [DiagnosticPage.request(0)])

    def test_snapshot_decodes_flags_and_boundaries(self) -> None:
        snapshot = bytes.fromhex("0000000200000001042306060250320d20047f")
        decoded = DiagnosticReader.snapshot(snapshot)
        self.assertIn("active_layers=0x00000002", decoded)
        self.assertIn("battery_request=True", decoded)
        self.assertIn("rgb_enabled=True", decoded)
        self.assertIn("battery_percent=80", decoded)
        self.assertIn("pin_B9_half=True", decoded)
        with self.assertRaises(ValueError):
            DiagnosticReader.snapshot(snapshot[:-1])


if __name__ == "__main__":
    unittest.main()
