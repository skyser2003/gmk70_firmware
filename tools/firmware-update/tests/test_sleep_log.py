import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

import read_sleep_log
from read_eeprom import EepromReader, QueryOp
from read_sleep_log import SleepLogReader, decode_journal, main, write_report
from update import Op


def record(generation: int = 1, committed: bool = True) -> bytes:
    page = bytearray(b"\xff" * 256)
    page[:4] = b"GSL1"
    struct.pack_into("<IIIIIII", page, 4, generation, 1, 60200, 20, 300000, 60001, 0)
    page[28:40] = bytes((0, 7, 0xC2, 1, 2, 0, 0, 1, 3, 1, 2, 12))
    page[40:44] = bytes((0xD1, 0x42, 1, 1))
    struct.pack_into("<II", page, 44, 100, 1)
    page[52:56] = bytes((0, 1, 1, 0))
    struct.pack_into("<IIBBBB", page, 56, 19, 60001, 7, 3, 0, 0)
    struct.pack_into("<IIBBBB", page, 68, 20, 60001, 16, 1, 60, 1)
    struct.pack_into("<I", page, 248, zlib.crc32(page[:248]))
    if committed:
        page[252:] = bytes(4)
    return bytes(page)


class FakeLogLink:
    def __init__(self) -> None:
        self.data = [record() + b"\xff" * (8192 - 256)] * 2
        self.operations: list[bytes] = []
        self.pending = b""
        self.right_error = False
        self.status = 0xC2201303
        self.closed = False

    def exchange(self, packet: bytes) -> bytes:
        self.operations.append(packet)
        target, op = packet[3:5]
        self.assert_getter(op)
        if op == Op.STATUS:
            return self.pending
        reply = bytearray(packet)
        reply[5] = 7 if target and self.right_error else 0
        reply[6] = packet[5] if op == QueryOp.LOG_READ else 0
        reply[7] = 0 if target else 3
        if op == QueryOp.LOG_INFO:
            struct.pack_into(
                "<IIII",
                reply,
                16,
                len(self.data[target]),
                zlib.crc32(self.data[target]),
                1,
                self.status,
            )
        else:
            offset = int.from_bytes(packet[12:16], "little")
            reply[16 : 16 + packet[5]] = self.data[target][offset : offset + packet[5]]
        self.pending = bytes(reply)
        busy = bytearray(reply)
        busy[5] = 1
        return bytes(busy)

    def assert_getter(self, op: int) -> None:
        assert op in (Op.STATUS, QueryOp.LOG_INFO, QueryOp.LOG_READ)

    def reconnect(self) -> None:
        pass

    def close(self) -> None:
        self.closed = True


class SleepLogTests(unittest.TestCase):
    def test_decode_reports_five_minute_configuration_and_forced_sleep(self) -> None:
        decoded = decode_journal(record() + b"\xff" * (8192 - 256))
        records = decoded["records"]
        assert isinstance(records, list)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["configured_timeout_ms"], 300000)
        self.assertEqual(records[0]["accepted_idle_ms"], 60001)
        self.assertEqual(records[0]["sleep_reason"], "critical battery")
        self.assertTrue(records[0]["accepted_manual"])
        self.assertEqual(records[0]["events"][1]["event"], 16)

    def test_torn_corrupt_pages_and_generation_wrap(self) -> None:
        corrupt = bytearray(record(2))
        corrupt[100] ^= 1
        data = (
            record(0xFFFFFFFF)
            + record(1)
            + record(3, committed=False)
            + corrupt
            + b"\xff" * (8192 - 4 * 256)
        )
        decoded = decode_journal(data)
        records = decoded["records"]
        assert isinstance(records, list)
        self.assertEqual([item["generation"] for item in records], [0xFFFFFFFF, 1])
        self.assertEqual(decoded["uncommitted_or_corrupt_pages"], 2)
        with self.assertRaises(ValueError):
            decode_journal(bytes(100))

    def test_reader_uses_only_log_getters_and_status_for_both_halves(self) -> None:
        link = FakeLogLink()
        reader = SleepLogReader(link)
        with patch("read_eeprom.time.sleep"):
            for target in (0, 1):
                snapshot = reader.snapshot(target)
                self.assertEqual(snapshot.data, link.data[target])
        ops = {packet[4] for packet in link.operations}
        self.assertEqual(ops, {1, 10, 11})
        for operation in (QueryOp.INFO, QueryOp.READ):
            with self.assertRaises(ValueError):
                SleepLogReader.packet(0, operation, 1)
        for operation in (QueryOp.LOG_INFO, QueryOp.LOG_READ):
            with self.assertRaises(ValueError):
                EepromReader.packet(0, operation, 1)

    def test_empty_disabled_storage_is_reported_without_read_requests(self) -> None:
        link = FakeLogLink()
        link.data = [b"", b""]
        link.status = 0
        with patch("read_eeprom.time.sleep"):
            snapshot = SleepLogReader(link).snapshot(0)
        self.assertEqual(snapshot.data, b"")
        self.assertNotIn(11, [p[4] for p in link.operations])

    def test_cli_preserves_left_report_when_right_is_unavailable(self) -> None:
        link = FakeLogLink()
        link.right_error = True
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sleep.json"
            with (
                patch.object(read_sleep_log, "UsbLink", return_value=link),
                patch("sys.argv", ["read_sleep_log.py", "--output", str(path)]),
                patch("read_eeprom.time.sleep"),
                self.assertRaises(SystemExit) as result,
            ):
                main()
            self.assertEqual(result.exception.code, 1)
            report = json.loads(path.read_text())
            self.assertEqual(
                report["halves"]["left"]["records"][0]["sleep_reason"],
                "critical battery",
            )
            self.assertIn("right", report["errors"])
            self.assertTrue(link.closed)
            with self.assertRaises(FileExistsError):
                write_report(path, {})


if __name__ == "__main__":
    unittest.main()
