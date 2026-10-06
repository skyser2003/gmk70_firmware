import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

from read_eeprom import EepromReader, QueryOp, decode_v15, main, write_backup
from update import Op


class FakeEepromLink:
    def __init__(self) -> None:
        self.data = [
            bytearray((i * 13 + target) % 256 for i in range(4096)) for target in (0, 1)
        ]
        self.operations: list[bytes] = []
        self.pending: bytes | None = None
        self.drop_read_ack = False
        self.stale_once = False
        self.old_command_pending = False
        self.change_once = False
        self.corrupt_read = False
        self.error = 0
        self.bad_role = False
        self.bad_count = False
        self.bad_padding = False
        self.bad_target = False
        self.info_counts = [0, 0]
        self.protocol = 1
        self.layout = 15
        self.size = 4096

    def exchange(self, packet: bytes) -> bytes:
        self.operations.append(packet)
        target, op = packet[3], packet[4]
        assert len(packet) == 32 and op in (Op.STATUS, QueryOp.INFO, QueryOp.READ)
        assert packet[16:] == bytes(16)  # All requests are payload-free getters.
        if op == Op.STATUS:
            assert self.pending is not None
            return self.pending
        token, offset = struct.unpack_from("<II", packet, 8)
        reply = bytearray(packet)
        reply[5] = self.error
        reply[6] = (packet[5] if op == QueryOp.READ else 0) ^ int(self.bad_count)
        reply[7] = (0 if target else 3) ^ int(self.bad_role)
        reply[3] = target ^ int(self.bad_target)
        if op == QueryOp.INFO:
            self.info_counts[target] += 1
            if self.change_once and self.info_counts[target] == 2:
                self.data[target][774] ^= 1
                self.change_once = False
            struct.pack_into(
                "<IIII",
                reply,
                16,
                self.size,
                zlib.crc32(self.data[target]),
                self.protocol,
                self.layout,
            )
        else:
            reply[16 : 16 + packet[5]] = self.data[target][offset : offset + packet[5]]
            if self.corrupt_read:
                reply[16] ^= 1
            if self.bad_padding:
                reply[31] = 1
        self.pending = bytes(reply)
        if self.old_command_pending:
            self.old_command_pending = False
            old_reply = bytearray(reply)
            struct.pack_into("<I", old_reply, 8, token ^ 1)
            old_reply[4] = QueryOp.READ
            self.pending = bytes(old_reply)
        if op == QueryOp.READ and self.drop_read_ack:
            self.drop_read_ack = False
            raise OSError("Lost read acknowledgement")
        if op == QueryOp.READ and self.stale_once:
            self.stale_once = False
            stale = bytearray(reply)
            struct.pack_into("<II", stale, 8, token ^ 1, offset + 16)
            return bytes(stale)
        busy = bytearray(self.pending)
        busy[5] = 1
        return bytes(busy)

    def reconnect(self) -> None:
        raise AssertionError("EEPROM reader must not reconnect or reset devices")

    def close(self) -> None:
        pass


class EepromTests(unittest.TestCase):
    def setUp(self) -> None:
        sleeper = patch("read_eeprom.time.sleep")
        sleeper.start()
        self.addCleanup(sleeper.stop)

    def test_full_both_half_snapshots_include_last_macro_byte(self) -> None:
        link = FakeEepromLink()
        reader = EepromReader(link)
        for target in (1, 0):
            snapshot = reader.snapshot(target)
            self.assertEqual(snapshot.data, bytes(link.data[target]))
            self.assertEqual(snapshot.metadata.crc32, zlib.crc32(snapshot.data))
            self.assertEqual(len(snapshot.data), 4096)
            document = snapshot.document()
            self.assertEqual(document["target"], "right" if target else "left")
            self.assertIn("decoded", document)
        reads = [packet for packet in link.operations if packet[4] == QueryOp.READ]
        self.assertEqual(len(reads), 512)
        self.assertEqual(struct.unpack_from("<I", reads[-1], 12)[0], 4080)

    def test_dropped_stale_and_previous_process_responses(self) -> None:
        link = FakeEepromLink()
        link.drop_read_ack = link.stale_once = link.old_command_pending = True
        snapshot = EepromReader(link).snapshot(1)
        self.assertEqual(snapshot.data, bytes(link.data[1]))
        reads = [packet for packet in link.operations if packet[4] == QueryOp.READ]
        self.assertGreater(len(reads), 256)
        self.assertEqual(reads[0], reads[1])  # Retry uses the same correlation token.

    def test_changed_settings_retry_entire_snapshot(self) -> None:
        link = FakeEepromLink()
        link.change_once = True
        snapshot = EepromReader(link).snapshot(1)
        self.assertEqual(link.info_counts[1], 4)
        self.assertEqual(snapshot.data, bytes(link.data[1]))

    def test_corrupt_reads_fail_after_bounded_attempts(self) -> None:
        link = FakeEepromLink()
        link.corrupt_read = True
        with self.assertRaisesRegex(RuntimeError, "CRC"):
            EepromReader(link).snapshot(1)
        self.assertEqual(link.info_counts[1], 6)

    def test_legacy_unavailable_and_update_in_progress_errors(self) -> None:
        for code, message in (
            (2, "unsupported"),
            (3, "update is active"),
            (7, "unavailable"),
        ):
            with self.subTest(code=code):
                link = FakeEepromLink()
                link.error = code
                with self.assertRaisesRegex(RuntimeError, message):
                    EepromReader(link).inspect(1)
                self.assertLessEqual(len(link.operations), 2)

    def test_wrong_target_role_length_padding_and_protocol(self) -> None:
        for field in ("bad_target", "bad_role", "bad_count", "bad_padding"):
            with self.subTest(field=field):
                link = FakeEepromLink()
                setattr(link, field, True)
                with self.assertRaises(RuntimeError):
                    EepromReader(link).command(1, QueryOp.READ, 4095, 1)
        for size, protocol in ((0, 1), (65537, 1), (4096, 2)):
            with self.subTest(size=size, protocol=protocol):
                link = FakeEepromLink()
                link.size, link.protocol = size, protocol
                with self.assertRaisesRegex(RuntimeError, "Unsupported"):
                    EepromReader(link).inspect(1)

    def test_missing_and_wrong_header_responses(self) -> None:
        link = FakeEepromLink()
        for reply in (b"", bytes(32), bytes((0x7F, 0x70, 2, 1)) + bytes(28)):
            with (
                self.subTest(reply=reply),
                patch.object(link, "exchange", return_value=reply),
                self.assertRaisesRegex(RuntimeError, "matching EEPROM"),
            ):
                EepromReader(link).inspect(1)

    def test_lost_connection_has_bounded_timeout(self) -> None:
        link = FakeEepromLink()
        with (
            patch.object(link, "exchange", side_effect=OSError),
            patch("read_eeprom.time.monotonic", side_effect=(0, 0, 9)),
            self.assertRaises(TimeoutError),
        ):
            EepromReader(link).inspect(1)

    def test_packet_bounds_and_unique_tokens(self) -> None:
        for target, op, token, offset, count in (
            (2, QueryOp.INFO, 1, 0, 0),
            (1, QueryOp.INFO, 0, 0, 0),
            (1, QueryOp.INFO, 1, 1, 0),
            (1, QueryOp.INFO, 1, 0, 1),
            (1, QueryOp.READ, 1, 0, 0),
            (1, QueryOp.READ, 1, 0, 17),
            (1, QueryOp.READ, 1, -1, 1),
            (1, QueryOp.READ, 1, 0x100000000, 1),
        ):
            with (
                self.subTest(
                    target=target, op=op, token=token, offset=offset, count=count
                ),
                self.assertRaises(ValueError),
            ):
                EepromReader.packet(target, op, token, offset, count)
        link = FakeEepromLink()
        reader = EepromReader(link)
        for _ in range(2):
            reader.command(1, QueryOp.READ, 0, 1)
        tokens = [
            struct.unpack_from("<I", packet, 8)[0]
            for packet in link.operations
            if packet[4] == QueryOp.READ
        ]
        self.assertNotEqual(tokens[0], tokens[1])
        with self.assertRaises(ValueError):
            reader.snapshot(1, attempts=0)

    def test_future_layout_saved_raw_without_invented_decode(self) -> None:
        link = FakeEepromLink()
        link.layout = 99
        snapshot = EepromReader(link).snapshot(1)
        self.assertNotIn("decoded", snapshot.document())
        self.assertEqual(snapshot.document()["logical_eeprom_hex"], snapshot.data.hex())

    def test_backup_never_overwrites_and_contains_verified_bytes(self) -> None:
        link = FakeEepromLink()
        snapshots = [EepromReader(link).snapshot(target) for target in (1, 0)]
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "eeprom.json"
            write_backup(path, snapshots)
            original = path.read_bytes()
            document = json.loads(original)
            self.assertEqual(document["format"], "gmk70-logical-eeprom-backup")
            self.assertEqual(
                document["halves"]["right"]["logical_eeprom_hex"],
                bytes(link.data[1]).hex(),
            )
            with self.assertRaises(FileExistsError):
                write_backup(path, snapshots)
            self.assertEqual(path.read_bytes(), original)
            with (
                patch("sys.argv", ["read_eeprom.py", "--output", str(path)]),
                patch("read_eeprom.UsbLink") as usb,
            ):
                with self.assertRaises(SystemExit):
                    main()
                usb.assert_not_called()

    def test_v15_decode_preserves_storage_endianness_and_macro_buffer(self) -> None:
        data = bytearray(4096)
        data[:2] = bytes.fromhex("e7fe")
        data[4:6] = bytes.fromhex("0014")
        data[8:12] = bytes.fromhex("1d00ff50")
        data[15:19] = bytes.fromhex("0001010e")
        data[19] = 20
        data[28:34] = bytes.fromhex("3500ff50ccff")
        data[34:37] = bytes.fromhex("260819")
        struct.pack_into(">H", data, 38, 0x7E05)
        struct.pack_into(">H", data, 756, 0xFFFF)
        struct.pack_into(">H", data, 758, 0x00EA)
        data[774:778] = b"abc\0"
        data[-1] = 0xAB
        decoded = json.loads(json.dumps(decode_v15(bytes(data))))
        self.assertEqual(decoded["core_marker"], "0xFEE7")
        self.assertEqual(decoded["keymap_config"], "0x1400")
        self.assertEqual(decoded["matrix_keycodes"][0][0][0], "0x7E05")
        self.assertEqual(decoded["matrix_keycodes"][3][9][8], "0xFFFF")
        self.assertEqual(decoded["encoders"][0][0][0], "0x00EA")
        self.assertEqual(decoded["macros"]["buffer_hex"][:8], "61626300")
        self.assertTrue(decoded["macros"]["buffer_hex"].endswith("ab"))
        self.assertEqual(decoded["macros"]["capacity"], 3322)
        self.assertTrue(decoded["board"]["gui_locked"] and decoded["board"]["flipped"])
        self.assertEqual(decoded["rgblight"]["mode"], 14)
        self.assertEqual(decoded["rgb_matrix"]["mode"], 13)
        self.assertEqual(decoded["radio"]["flags_and_sleep_selector"], 14)
        self.assertEqual(decoded["radio"]["sleep_selector"], 7)
        self.assertEqual(decoded["radio"]["idle_sleep_timeout_ms"], 300000)
        self.assertFalse(decoded["radio"]["initialized_flag"])
        with self.assertRaises(ValueError):
            decode_v15(b"short")


if __name__ == "__main__":
    unittest.main()
