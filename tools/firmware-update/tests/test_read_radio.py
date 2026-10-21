import importlib
import json
import struct
import tempfile
import unittest
from pathlib import Path
from types import ModuleType
from unittest.mock import Mock, patch


class RadioLink:
    def __init__(self) -> None:
        self.words = [1234, 1, 49, 2, 31, 2] + [0] * 24
        self.words[18] = 500
        self.words[25] = 1
        self.requests: list[bytes] = []
        self.error = 0
        self.wrong_token = False
        self.closed = False

    def close(self) -> None:
        self.closed = True

    def exchange(self, packet: bytes) -> bytes:
        assert len(packet) == 32 and packet[:4] == b"\x22RD\x01"
        assert packet[5] == 0 and packet[8:] == bytes(24)
        self.requests.append(packet)
        page = packet[4]
        reply = bytearray(packet)
        reply[5] = self.error
        if self.wrong_token:
            reply[6] ^= 1
        struct.pack_into("<6I", reply, 8, *self.words[page * 6 : page * 6 + 6])
        return bytes(reply)


class ReadRadioTests(unittest.TestCase):
    def reader_module(self) -> ModuleType:
        try:
            return importlib.import_module("read_radio")
        except ModuleNotFoundError:
            self.fail("The read-only USB radio reader is missing")

    def test_snapshot_uses_only_five_getters_and_decodes_bt_counts(self) -> None:
        module = self.reader_module()
        link = RadioLink()
        snapshot = module.RadioReader(link).snapshot()
        self.assertEqual(snapshot["radio_firmware_version"], 49)
        self.assertTrue(snapshot["radio_firmware_version_received"])
        self.assertEqual(snapshot["bluetooth"]["raw_31"], 1)
        self.assertEqual([p[4] for p in link.requests], list(range(5)))
        self.assertEqual(len({p[6:8] for p in link.requests}), 1)

    def test_missing_version_is_not_firmware_version_zero(self) -> None:
        module = self.reader_module()
        link = RadioLink()
        link.words[1] = link.words[2] = 0
        snapshot = module.RadioReader(link).snapshot()
        self.assertIsNone(snapshot["radio_firmware_version"])
        link.words[1] = 1
        self.assertEqual(
            module.RadioReader(link).snapshot()["radio_firmware_version"], 0
        )

    def test_rejects_usb_gate_failure_and_uncorrelated_reply(self) -> None:
        module = self.reader_module()
        link = RadioLink()
        link.error = 1
        with self.assertRaisesRegex(RuntimeError, "wired|USB"):
            module.RadioReader(link).snapshot()
        link.error = 0
        link.wrong_token = True
        with self.assertRaisesRegex(RuntimeError, "matching|correlat"):
            module.RadioReader(link).snapshot()

    def test_baseline_detects_new_packets_and_reset(self) -> None:
        module = self.reader_module()
        link = RadioLink()
        before = module.RadioReader(link).snapshot()
        link.words[0] += 2000
        link.words[25] += 1
        after = module.RadioReader(link).snapshot()
        delta = module.compare(before, after)
        self.assertEqual(delta["bluetooth"]["raw_31"], 1)
        self.assertFalse(delta["reset_or_wrap_suspected"])
        link.words[0] = 1
        reset = module.compare(after, module.RadioReader(link).snapshot())
        self.assertTrue(reset["reset_or_wrap_suspected"])
        self.assertIsNone(reset["bluetooth"])

    def test_cli_saves_snapshot_and_refuses_overwrite_before_connecting(self) -> None:
        module = self.reader_module()
        link = RadioLink()
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "radio.json"
            with (
                patch.object(module, "UsbRadioLink", return_value=link),
                patch("builtins.print"),
            ):
                self.assertEqual(module.main(["--output", str(output)]), 0)
            document = json.loads(output.read_text())
            self.assertEqual(document["bluetooth"]["raw_31"], 1)
            self.assertTrue(link.closed)
            with (
                patch.object(module, "UsbRadioLink") as connect,
                self.assertRaises(SystemExit),
            ):
                module.main(["--output", str(output)])
            connect.assert_not_called()

    def test_corrupt_baseline_is_rejected_before_connecting(self) -> None:
        module = self.reader_module()
        with tempfile.TemporaryDirectory() as temporary:
            baseline = Path(temporary) / "bad.json"
            baseline.write_text('{"format": "gmk70-radio-diagnostics", "version": 1}')
            with (
                patch.object(module, "UsbRadioLink") as connect,
                self.assertRaises(SystemExit),
            ):
                module.main(
                    [
                        "--baseline",
                        str(baseline),
                        "--output",
                        str(Path(temporary) / "after.json"),
                    ]
                )
            connect.assert_not_called()

    def test_usb_transport_filters_stale_replies_and_allows_only_getters(self) -> None:
        module = self.reader_module()
        hid = Mock()
        hid.enumerate.return_value = [
            {
                "usage_page": 0xFF60,
                "usage": 0x61,
                "manufacturer_string": "HS",
                "path": b"left",
            }
        ]
        device = hid.device.return_value
        device.write.return_value = 33
        request = module.PREFIX + bytes((0, 0, 12, 34)) + bytes(24)
        stale = bytearray(request)
        stale[6] ^= 1
        device.read.side_effect = [bytes(stale), request]
        with patch.dict("sys.modules", {"hid": hid}):
            link = module.UsbRadioLink()
            self.assertEqual(link.exchange(request), request)
            device.write.assert_called_once_with(b"\0" + request)
            device.open_path.assert_called_once_with(b"left")
            for forbidden in (
                bytes((0x0B,)) + bytes(31),
                module.PREFIX + bytes((5,)) + bytes(27),
                request[:-1] + b"\x01",
            ):
                with self.assertRaisesRegex(ValueError, "Only payload-free"):
                    link.exchange(forbidden)
            self.assertEqual(device.write.call_count, 1)
            link.close()
            device.close.assert_called_once()

    def test_usb_transport_reports_short_write_unhandled_and_timeout(self) -> None:
        module = self.reader_module()
        hid = Mock()
        hid.enumerate.return_value = [
            {
                "usage_page": 0xFF60,
                "usage": 0x61,
                "manufacturer_string": "HS",
                "path": b"left",
            }
        ]
        device = hid.device.return_value
        request = module.PREFIX + bytes(28)
        with patch.dict("sys.modules", {"hid": hid}):
            link = module.UsbRadioLink()
            device.write.return_value = 32
            with self.assertRaisesRegex(OSError, "Incomplete"):
                link.exchange(request)
            device.write.return_value = 33
            device.read.return_value = b"\xff" + request[1:]
            with self.assertRaisesRegex(RuntimeError, "Install the optional"):
                link.exchange(request)
            device.read.return_value = []
            with self.assertRaisesRegex(RuntimeError, "timed out"):
                link.exchange(request)
