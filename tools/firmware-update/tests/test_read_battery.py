"""Read-only battery protocol tests; all exchanges use local fixtures."""

import io
import struct
import unittest
from contextlib import redirect_stdout
from typing import TypedDict
from unittest.mock import patch

from read_battery import BatteryReader, BatterySnapshot, main
from update import UsbLink


def response(
    *,
    nonce: int = 7,
    percentage: int = 50,
    flags: int = 3,
    age_ms: int = 100,
) -> bytes:
    return (
        bytes((0x7F, 0x72, 1, 0, 0x21, 0, 0, 3))
        + struct.pack("<I", nonce)
        + b"BATT"
        + bytes((percentage, flags, 0, 0))
        + struct.pack("<III", age_ms, 123, 121)
    )


class FixtureLink:
    def __init__(self, reply: bytes | OSError) -> None:
        self.reply = reply
        self.packets: list[bytes] = []
        self.closed = False

    def exchange(self, packet: bytes) -> bytes:
        self.packets.append(packet)
        if isinstance(self.reply, OSError):
            raise self.reply
        return self.reply

    def close(self) -> None:
        self.closed = True


class HidCandidate(TypedDict):
    usage_page: int
    usage: int
    manufacturer_string: str
    path: bytes


class FeatureHid:
    """Only the Feature HID boundary is replaced; UsbLink remains real."""

    def __init__(self) -> None:
        self.requests: list[bytes] = []
        self.responses = [response(nonce=6), response()]

    def enumerate(self, vendor: int, product: int) -> list[HidCandidate]:
        if (vendor, product) != (0x342D, 0xE491):
            raise AssertionError("Wrong device identity")
        return [
            {
                "usage_page": 0xFF60,
                "usage": 0x62,
                "manufacturer_string": "HS",
                "path": b"battery-feature-fixture",
            }
        ]

    def device(self) -> "FeatureHid":
        return self

    def close(self) -> None:
        pass

    def open_path(self, path: bytes) -> None:
        if path != b"battery-feature-fixture":
            raise AssertionError("Wrong fixture path")

    def send_feature_report(self, packet: bytes) -> int:
        self.requests.append(packet)
        return len(packet)

    def get_feature_report(self, report_id: int, size: int) -> list[int]:
        if (report_id, size) != (4, 33):
            raise AssertionError("Wrong Feature HID framing")
        return list(b"\x04" + self.responses.pop(0))


class BatteryDecodeTests(unittest.TestCase):
    def test_fresh_endpoints_and_aggregate_counters(self) -> None:
        for percentage in (0, 100):
            with self.subTest(percentage=percentage):
                snapshot = BatterySnapshot.decode(
                    response(percentage=percentage, flags=7, age_ms=10000), 7
                )
                self.assertEqual(snapshot.percentage, percentage)
                self.assertTrue(snapshot.valid)
                self.assertTrue(snapshot.cable)
                self.assertTrue(snapshot.full)
                self.assertEqual(snapshot.age_ms, 10000)
                self.assertEqual(snapshot.queries, 123)
                self.assertEqual(snapshot.replies, 121)

    def test_unknown_percentage_preserves_independent_gpio_state_and_age(self) -> None:
        for age_ms in (0, 10001, 0xFFFFFFFF):
            with self.subTest(age_ms=age_ms):
                snapshot = BatterySnapshot.decode(
                    response(percentage=255, flags=6, age_ms=age_ms), 7
                )
                self.assertIsNone(snapshot.percentage)
                self.assertFalse(snapshot.valid)
                self.assertTrue(snapshot.cable)
                self.assertTrue(snapshot.full)
                self.assertEqual(snapshot.age_ms, age_ms)

    def test_header_magic_nonce_reserved_bytes_and_result_are_strict(self) -> None:
        for offset, value in (
            (0, 0),
            (1, 0x70),
            (2, 2),
            (3, 1),
            (4, 0x20),
            (5, 1),
            (5, 7),
            (6, 1),
            (7, 0),
            (7, 1),
            (8, 8),
            (12, 0),
            (18, 1),
            (19, 1),
        ):
            changed = bytearray(response())
            changed[offset] = value
            with (
                self.subTest(offset=offset, value=value),
                self.assertRaises(RuntimeError),
            ):
                BatterySnapshot.decode(bytes(changed), 7)
        for packet in (b"", response()[:-1], response() + b"\0"):
            with self.subTest(size=len(packet)), self.assertRaises(RuntimeError):
                BatterySnapshot.decode(packet, 7)

    def test_percentage_flags_and_freshness_cannot_contradict(self) -> None:
        for percentage, flags, age_ms in (
            (101, 1, 0),
            (254, 1, 0),
            (255, 1, 0),
            (50, 0, 0),
            (101, 0, 0),
            (50, 8, 0),
            (50, 255, 0),
            (50, 5, 0),
            (255, 4, 0),
            (50, 1, 10001),
            (50, 1, 0xFFFFFFFF),
        ):
            with (
                self.subTest(percentage=percentage, flags=flags, age_ms=age_ms),
                self.assertRaises(RuntimeError),
            ):
                BatterySnapshot.decode(
                    response(percentage=percentage, flags=flags, age_ms=age_ms), 7
                )


class BatteryReaderTests(unittest.TestCase):
    def test_single_read_only_left_query_has_nonce_and_zero_tail(self) -> None:
        link = FixtureLink(response())
        snapshot = BatteryReader(link).sample(7)
        self.assertEqual(snapshot.nonce, 7)
        self.assertEqual(
            link.packets,
            [b"\x7f\x72\x01\x00\x21\0\0\0\x07\0\0\0" + bytes(20)],
        )

    def test_nonce_bounds_fail_before_any_exchange(self) -> None:
        link = FixtureLink(response())
        for nonce in (-1, 0, 2**32):
            with self.subTest(nonce=nonce), self.assertRaises(ValueError):
                BatteryReader(link).sample(nonce)
        self.assertEqual(link.packets, [])
        snapshot = BatteryReader(FixtureLink(response(nonce=0xFFFFFFFF))).sample(
            0xFFFFFFFF
        )
        self.assertEqual(snapshot.nonce, 0xFFFFFFFF)

    def test_transport_errors_busy_and_unknown_state_never_retry(self) -> None:
        pending = bytearray(response())
        pending[5] = 1
        for reply in (OSError("transport fixture"), bytes(pending), response(nonce=8)):
            link = FixtureLink(reply)
            with self.subTest(reply=reply), self.assertRaises((OSError, RuntimeError)):
                BatteryReader(link).sample(7)
            self.assertEqual(len(link.packets), 1)

    def test_feature_hid_filters_stale_nonce_and_sends_one_battery_query(self) -> None:
        backend = FeatureHid()
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            try:
                self.assertEqual(BatteryReader(link).sample(7).percentage, 50)
            finally:
                link.close()
        self.assertEqual(
            backend.requests,
            [b"\x04\x7f\x72\x01\x00\x21\0\0\0\x07\0\0\0" + bytes(20)],
        )

    def test_cli_prints_only_typed_aggregate_evidence_and_closes_link(self) -> None:
        link = FixtureLink(response())
        output = io.StringIO()
        with (
            patch("read_battery.UsbLink", return_value=link),
            patch("read_battery.secrets.randbelow", return_value=6),
            patch("sys.argv", ["read_battery.py"]),
            redirect_stdout(output),
        ):
            main()
        self.assertEqual(
            output.getvalue(),
            '{\n  "read_only": true,\n  "target": "left",\n  "battery": {\n'
            '    "nonce": 7,\n    "percentage": 50,\n    "valid": true,\n'
            '    "cable": true,\n    "full": false,\n    "age_ms": 100,\n'
            '    "queries": 123,\n    "replies": 121\n  }\n}\n',
        )
        self.assertTrue(link.closed)
        self.assertEqual(len(link.packets), 1)


if __name__ == "__main__":
    unittest.main()
