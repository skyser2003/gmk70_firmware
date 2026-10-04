import hashlib
import json
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

from update import Manifest, Op, Package, Reply, Updater


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

    def exchange(self, packet: bytes) -> bytes:
        target, op = packet[3], packet[4]
        session, offset = struct.unpack_from("<II", packet, 8)
        self.operations.append((target, op))
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

    def test_release_cannot_remove_left_debug_control(self) -> None:
        link = FakeLink()
        with self.assertRaises(ValueError):
            Updater(link).install_both(package("release"))
        self.assertEqual(link.operations, [])


if __name__ == "__main__":
    unittest.main()
