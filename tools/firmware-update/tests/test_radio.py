"""Radio controls use in-memory protocol/HID fixtures; never real hardware."""

import io
import struct
import unittest
from contextlib import redirect_stdout
from typing import TypedDict
from unittest.mock import patch

from radio import (
    RadioAction,
    RadioReader,
    RadioRequest,
    RadioSnapshot,
    RadioUsbLink,
    main,
)


def response(*, nonce: int = 7, result: int = 0, flags: int = 7) -> bytes:
    return (
        bytes((0x7F, 0x72, 1, 0, 0x22, result, flags, 3))
        + struct.pack("<I", nonce)
        + b"RADI"
        + bytes((2, 1, 2, 8))
        + struct.pack("<III", 123, 45, 67)
    )


class FixtureLink:
    def __init__(self, reply: bytes | OSError | None = None) -> None:
        self.reply = response() if reply is None else reply
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
    def __init__(self, replies: list[bytes]) -> None:
        self.replies = replies
        self.requests: list[bytes] = []
        self.closed = False

    def enumerate(self, vendor: int, product: int) -> list[HidCandidate]:
        if (vendor, product) != (0x342D, 0xE491):
            raise AssertionError("Unexpected HID identity")
        return [
            {
                "usage_page": 0xFF60,
                "usage": 0x62,
                "manufacturer_string": "HS",
                "path": b"radio-fixture",
            }
        ]

    def device(self) -> "FeatureHid":
        return self

    def open_path(self, path: bytes) -> None:
        if path != b"radio-fixture":
            raise AssertionError("Unexpected fixture path")

    def close(self) -> None:
        self.closed = True

    def send_feature_report(self, packet: bytes) -> int:
        self.requests.append(packet)
        return len(packet)

    def get_feature_report(self, report_id: int, size: int) -> list[int]:
        if (report_id, size) != (4, 33):
            raise AssertionError("Unexpected Feature framing")
        return list(b"\x04" + self.replies.pop(0))


class RadioTests(unittest.TestCase):
    def test_reader_methods_send_only_the_requested_action(self) -> None:
        link = FixtureLink()
        reader = RadioReader(link)
        reader.status(7)
        reader.select(6, 7)
        reader.pair(2, 7)
        reader.marker(7)
        self.assertEqual(
            [packet[5:7] for packet in link.packets],
            [b"\0\0", b"\1\6", b"\2\2", b"\3\0"],
        )

    def test_request_bytes_are_exact_and_actions_have_no_payload(self) -> None:
        for action, channel in (
            (RadioAction.STATUS, 0),
            (RadioAction.SELECT, 6),
            (RadioAction.PAIR, 2),
            (RadioAction.MARKER, 0),
        ):
            packet = RadioRequest(action, channel, 7).encode()
            self.assertEqual(
                packet,
                bytes((0x7F, 0x72, 1, 0, 0x22, action, channel, 0, 7, 0, 0, 0))
                + bytes(20),
            )
            self.assertEqual(
                RadioRequest.decode(packet), RadioRequest(action, channel, 7)
            )

    def test_invalid_requests_fail_before_transport(self) -> None:
        link = FixtureLink()
        reader = RadioReader(link)
        for nonce in (-1, 0, 2**32, True):
            with self.subTest(nonce=nonce), self.assertRaises(ValueError):
                reader.status(nonce)
        for channel in (-1, 7, True):
            with self.subTest(channel=channel), self.assertRaises(ValueError):
                reader.select(channel, 7)
        with self.assertRaises(ValueError):
            reader.pair(0, 7)
        self.assertEqual(link.packets, [])
        good = RadioRequest(RadioAction.STATUS, 0, 7).encode()
        for offset in (*range(5), 5, 6, 7, *range(12, 32)):
            malformed = bytearray(good)
            malformed[offset] = 255
            with self.subTest(offset=offset), self.assertRaises(ValueError):
                RadioRequest.decode(bytes(malformed))
        for packet in (b"", good[:-1], good + b"\0"):
            with self.assertRaises(ValueError):
                RadioRequest.decode(packet)

    def test_strict_reply_fields_and_counter_endianness(self) -> None:
        snapshot = RadioSnapshot.decode(response(), 7)
        self.assertEqual(
            (snapshot.selected, snapshot.active, snapshot.connection, snapshot.queued),
            (2, 1, 2, 8),
        )
        self.assertEqual(
            (snapshot.errors, snapshot.selections, snapshot.reports), (123, 45, 67)
        )
        self.assertTrue(
            snapshot.waiting and snapshot.available and snapshot.qualification
        )
        for offset, value in (
            (0, 0),
            (1, 0x70),
            (2, 2),
            (3, 1),
            (4, 0x20),
            (5, 3),
            (6, 8),
            (7, 0),
            (8, 8),
            (12, 0),
            (16, 7),
            (17, 7),
            (18, 5),
            (19, 9),
        ):
            malformed = bytearray(response())
            malformed[offset] = value
            with self.subTest(offset=offset), self.assertRaises(RuntimeError):
                RadioSnapshot.decode(bytes(malformed), 7)
        for packet in (b"", response()[:-1], response() + b"\0"):
            with self.assertRaises(RuntimeError):
                RadioSnapshot.decode(packet, 7)
        for nonce in (0, -1, 2**32, True):
            with self.assertRaises(ValueError):
                RadioSnapshot.decode(response(), nonce)

    def test_busy_unavailable_transport_and_nonce_errors_never_retry_actions(
        self,
    ) -> None:
        for reply in (
            response(result=1),
            response(result=2),
            response(nonce=8),
            OSError("fixture transport"),
        ):
            for action in (
                RadioAction.STATUS,
                RadioAction.SELECT,
                RadioAction.PAIR,
                RadioAction.MARKER,
            ):
                link = FixtureLink(reply)
                reader = RadioReader(link)
                with (
                    self.subTest(action=action, reply=reply),
                    self.assertRaises((RuntimeError, OSError)),
                ):
                    if action == RadioAction.STATUS:
                        reader.status(7)
                    elif action == RadioAction.SELECT:
                        reader.select(2, 7)
                    elif action == RadioAction.PAIR:
                        reader.pair(2, 7)
                    else:
                        reader.marker(7)
                self.assertEqual(len(link.packets), 1)

    def test_real_feature_framing_filters_stale_nonce_and_delivers_unavailable(
        self,
    ) -> None:
        backend = FeatureHid([response(nonce=6), response(result=2)])
        with patch.dict("sys.modules", {"hid": backend}):
            link = RadioUsbLink()
            try:
                with self.assertRaisesRegex(RuntimeError, "unavailable"):
                    RadioReader(link).select(2, 7)
            finally:
                link.close()
        self.assertEqual(
            backend.requests,
            [b"\x04\x7f\x72\x01\x00\x22\x01\x02\x00\x07\0\0\0" + bytes(20)],
        )
        self.assertTrue(backend.closed)

    def test_feature_link_rejects_nonradio_requests_and_late_completion(self) -> None:
        backend = FeatureHid([response()])
        with patch.dict("sys.modules", {"hid": backend}):
            link = RadioUsbLink()
            try:
                with self.assertRaises(ValueError):
                    link.exchange(bytes(32))
                self.assertEqual(backend.requests, [])
                with (
                    patch("radio.time.monotonic", side_effect=[0.0, 0.0, 3.0]),
                    self.assertRaises(OSError),
                ):
                    RadioReader(link).select(2, 7)
            finally:
                link.close()
        self.assertEqual(len(backend.requests), 1)

    def test_cli_polling_is_status_only_bounded_and_closes_link(self) -> None:
        link = FixtureLink()
        output = io.StringIO()
        with (
            patch("radio.RadioUsbLink", return_value=link),
            patch("radio.secrets.randbelow", return_value=6),
            patch("radio.time.sleep") as sleep,
            patch("sys.argv", ["radio.py", "status", "--samples", "2"]),
            redirect_stdout(output),
        ):
            main()
        self.assertEqual(len(link.packets), 2)
        self.assertEqual([packet[5] for packet in link.packets], [0, 0])
        self.assertEqual(sleep.call_count, 1)
        self.assertTrue(link.closed)
        self.assertIn('"read_only": true', output.getvalue())
        self.assertIn('"target": "left"', output.getvalue())

    def test_cli_rejects_invalid_action_arguments_before_opening_device(self) -> None:
        for arguments in (
            ("select",),
            ("pair", "--channel", "0"),
            ("status", "--channel", "1"),
            ("marker", "--samples", "2"),
            ("status", "--samples", "121"),
            ("status", "--interval", "nan"),
        ):
            with (
                self.subTest(arguments=arguments),
                patch("radio.RadioUsbLink") as constructor,
                patch("sys.argv", ["radio.py", *arguments]),
                patch("sys.stderr", new_callable=io.StringIO),
                self.assertRaises(SystemExit),
            ):
                main()
            constructor.assert_not_called()


if __name__ == "__main__":
    unittest.main()
