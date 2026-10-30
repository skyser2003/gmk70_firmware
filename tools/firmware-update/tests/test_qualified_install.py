"""Frozen qualification/install lifecycle with real packet codecs and fake USB/time."""

import hashlib
import json
import os
import stat
import struct
import tempfile
import unittest
import zlib
from collections.abc import Callable, Iterator
from contextlib import contextmanager
from pathlib import Path
from unittest.mock import patch

import qualified_install
import read_health
import update
from qualified_install import QualifiedInstall
from studio_rpc import JsonSchema, JsonValue
from update import Link, Manifest, Op

RESIDENT = bytes(range(256)) * 2
BOOT = RESIDENT.ljust(0x2000, b"\xff")
BEFORE = ((1024, 111), (1024, 222))


@contextmanager
def resident_fixture() -> Iterator[None]:
    """Only substitute resident identity inputs; package/hash checks remain real."""
    crc, sha = zlib.crc32(BOOT), hashlib.sha256(BOOT).hexdigest()
    with (
        patch.object(update, "ZMK_BOOT_CRC", crc),
        patch.object(update, "ZMK_BOOT_SHA256", sha),
        patch.object(read_health, "ZMK_BOOT_CRC", crc),
        patch.object(qualified_install, "ZMK_BOOT_CRC", crc),
    ):
        yield


class Clock:
    def __init__(self) -> None:
        self.now = 0.0

    def monotonic(self) -> float:
        return self.now

    def epoch(self) -> float:
        return 1000.0 + self.now

    def sleep(self, delay: float) -> None:
        self.now += delay


class FixtureLink:
    def __init__(self, clock: Clock, target: int = 0) -> None:
        self.clock = clock
        self.target = target
        self.packets: list[bytes] = []
        self.opened = self.closed = self.reconnects = 0
        self.ids = list(BEFORE)
        self.uptime_offsets = [10000, 12000]
        self.state = self.session = self.size = self.crc = 0
        self.data = bytearray()
        self.pending = bytes(32)
        self.fail_begin = self.fail_data = False
        self.restart_at_seal: int | None = None
        self.lost_install_ack = False
        self.bad_ack = ""
        self.bad_boot = False
        self.bad_right_boot = False
        self.health_flags = 22
        self.data_delay = 0.01
        self.installed = False
        self.on_seal: Callable[[], None] | None = None
        self.install_marker: Path | None = None
        self.fail_install_status = False
        self.sealed_info_fault = ""

    def open(self) -> Link:
        self.opened += 1
        return self

    def reconnect(self) -> None:
        self.reconnects += 1
        raise AssertionError("Qualification must never reconnect")

    def close(self) -> None:
        self.closed += 1
        if self.installed:
            self.ids[self.target] = (self.size, self.crc)
            self.state = 0
            self.installed = False

    def exchange(self, packet: bytes) -> bytes:
        self.packets.append(packet)
        target, operation = packet[3], packet[4]
        role = 3 if target == 0 else 0
        boot_crc = update.ZMK_BOOT_CRC ^ int(
            self.bad_boot or (target == 1 and self.bad_right_boot)
        )
        if packet[1] == 0x72:
            nonce = int.from_bytes(packet[8:12], "little")
            uptime = int(self.clock.now * 1000) + self.uptime_offsets[target]
            return bytes((0x7F, 0x72, 1, target, 0x20, 0, 0, role)) + struct.pack(
                "<6I",
                nonce,
                0x4B4D5A47,
                uptime & 0xFFFFFFFF,
                0,
                self.health_flags,
                0,
            )
        if operation == Op.INFO:
            size, crc = self.ids[target]
            answer = bytearray(
                bytes(
                    (
                        0x7F,
                        0x70,
                        1,
                        target,
                        Op.INFO,
                        0,
                        self.state if target == self.target else 0,
                        role,
                    )
                )
                + struct.pack(
                    "<6I",
                    self.session if target == self.target else 0,
                    len(self.data) if target == self.target else 0,
                    size,
                    crc,
                    1,
                    boot_crc,
                )
            )
            if target == self.target and self.state == 2 and self.sealed_info_fault:
                offsets = {
                    "state": 6,
                    "role": 7,
                    "session": 8,
                    "received": 12,
                    "size": 16,
                    "crc": 20,
                    "abi": 24,
                    "resident": 28,
                }
                answer[offsets[self.sealed_info_fault]] ^= 1
            return bytes(answer)
        if operation == Op.STATUS:
            if self.fail_install_status:
                raise OSError("INSTALL status unavailable")
            return self.pending
        if target != self.target:
            raise AssertionError("Only the frozen selected target may be written")
        session, offset = struct.unpack_from("<II", packet, 8)
        result = 0
        if operation == Op.BEGIN:
            self.session = session
            self.size, self.crc = struct.unpack_from("<II", packet, 16)
            self.state = 1
            result = 5 if self.fail_begin else 0
        elif operation == Op.DATA:
            if offset != len(self.data):
                raise AssertionError("Unexpected image offset")
            self.data.extend(packet[16 : 16 + packet[5]])
            result = 5 if self.fail_data else 0
            self.clock.now += self.data_delay
        elif operation == Op.SEAL:
            if zlib.crc32(self.data) != self.crc:
                raise AssertionError("Wrong staged image bytes")
            self.state = 2
            if self.restart_at_seal is not None:
                self.uptime_offsets[self.restart_at_seal] -= 10000
            if self.on_seal is not None:
                self.on_seal()
        elif operation == Op.INSTALL:
            if self.install_marker is not None and (
                not self.install_marker.is_file()
                or not self.install_marker.with_suffix(".json.sha256").is_file()
            ):
                raise AssertionError("INSTALL reached USB before its exclusive marker")
            self.state = 3
            self.installed = True
        elif operation == Op.ABORT:
            self.state = 0
        else:
            raise AssertionError("Unexpected device operation")
        answer = bytearray(
            bytes((0x7F, 0x70, 1, target, operation, result, self.state, role))
            + struct.pack("<6I", session, len(self.data), self.size, self.crc, 1, 0)
        )
        if operation == Op.INSTALL:
            offsets = {
                "operation": 4,
                "state": 6,
                "session": 8,
                "size": 16,
                "crc": 20,
                "role": 7,
                "abi": 24,
                "reserved": 28,
            }
            if self.bad_ack:
                answer[offsets[self.bad_ack]] ^= 1
        self.pending = bytes(answer)
        if operation == Op.INSTALL and self.lost_install_ack:
            self.lost_install_ack = False
            raise OSError("Lost INSTALL acknowledgement")
        return self.pending

    def writes(self) -> list[int]:
        return [
            packet[4]
            for packet in self.packets
            if packet[1] == 0x70 and packet[4] not in (Op.INFO, Op.STATUS)
        ]


class QualifiedInstallTests(unittest.TestCase):
    @staticmethod
    def change_proof(case: Path, key: str, value: JsonValue) -> None:
        path = case / "baseline-health.json"
        document: JsonValue = json.loads(path.read_text())
        proof = JsonSchema.mapping(document)
        proof[key] = value
        data = json.dumps(proof).encode()
        path.write_bytes(data)
        (case / "baseline-health.json.sha256").write_text(
            hashlib.sha256(data).hexdigest()
        )

    @staticmethod
    def package(directory: Path, *, universal: bool = False) -> None:
        directory.mkdir()
        image = (
            struct.pack("<II", 0x20006FC0, 0x08002201)
            + bytes(1000)
            + b"GMK70ZMKBOOT\x01\0\0\0"
        )
        manifest: Manifest = {
            "format": 1,
            "board": "zuoya/gmk70",
            "mcu": "WB32FQ95-256K",
            "profile": "zmk-studio-universal" if universal else "zmk-studio",
            "app_address": 0x08002000,
            "app_size": len(image),
            "app_crc": zlib.crc32(image),
            "app_sha256": hashlib.sha256(image).hexdigest(),
            "boot_crc": zlib.crc32(BOOT),
            "boot_sha256": hashlib.sha256(BOOT).hexdigest(),
            "install_sha256": "unused",
        }
        (directory / "application.bin").write_bytes(image)
        (directory / "resident.bin").write_bytes(RESIDENT)
        (directory / "update.json").write_text(json.dumps(manifest))

    @contextmanager
    def fixture(
        self, *, target: int = 0, universal: bool = False
    ) -> Iterator[tuple[Path, FixtureLink, QualifiedInstall, Clock]]:
        with resident_fixture(), tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            package, case = root / "package", root / "case"
            self.package(package, universal=universal)
            QualifiedInstall.create_case(
                case, package, BEFORE[0], BEFORE[1], "a" * 40, target=target
            )
            clock = Clock()
            link = FixtureLink(clock, target)
            with (
                patch("qualified_install.time.time", side_effect=clock.epoch),
                patch("qualified_install.time.monotonic", side_effect=clock.monotonic),
                patch("qualified_install.time.sleep", side_effect=clock.sleep),
            ):
                yield case, link, QualifiedInstall(case, link.open), clock

    def test_universal_case_installs_only_selected_half_and_observes_both(self) -> None:
        for target in (0, 1):
            with (
                self.subTest(target=target),
                self.fixture(target=target, universal=True) as (
                    case,
                    link,
                    operator,
                    _,
                ),
            ):
                operator.baseline()
                operator.install()
                operator.observe()
                self.assertEqual(link.ids[1 - target], BEFORE[1 - target])
                self.assertNotEqual(link.ids[target], BEFORE[target])
                self.assertEqual(
                    [p[3] for p in link.packets if p[4] == Op.INSTALL and p[1] == 0x70],
                    [target],
                )
                self.assertTrue(
                    all(
                        p[3] == target
                        for p in link.packets
                        if p[1] == 0x70
                        and p[4] in (Op.BEGIN, Op.DATA, Op.SEAL, Op.INSTALL, Op.ABORT)
                    )
                )
                marker: JsonValue = json.loads(
                    (case / "INSTALL-requested.json").read_text()
                )
                self.assertEqual(JsonSchema.mapping(marker)["target"], target)

    def test_right_rejects_legacy_studio_and_invalid_target_offline(self) -> None:
        with resident_fixture(), tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            package = root / "package"
            self.package(package)
            for target in (1, 2, -1, True):
                with self.subTest(target=target), self.assertRaises(ValueError):
                    QualifiedInstall.create_case(
                        root / "case",
                        package,
                        BEFORE[0],
                        BEFORE[1],
                        "a" * 40,
                        target=target,
                    )
                self.assertFalse((root / "case").exists())

    def test_right_noop_checks_right_identity_instead_of_left(self) -> None:
        with resident_fixture(), tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            package = root / "package"
            self.package(package, universal=True)
            candidate = update.Package.load(package)
            identity = (len(candidate.image), candidate.manifest["app_crc"])
            with self.assertRaises(ValueError):
                QualifiedInstall.create_case(
                    root / "noop", package, BEFORE[0], identity, "a" * 40, target=1
                )
            QualifiedInstall.create_case(
                root / "right", package, identity, BEFORE[1], "a" * 40, target=1
            )

    def test_right_lost_ack_uses_status_and_never_replays_or_aborts(self) -> None:
        with self.fixture(target=1, universal=True) as (_, link, operator, _):
            operator.baseline()
            link.lost_install_ack = True
            operator.install()
            operator.observe()
            self.assertEqual(link.writes().count(Op.INSTALL), 1)
            self.assertNotIn(Op.ABORT, link.writes())
            self.assertTrue(any(p[3:5] == bytes((1, Op.STATUS)) for p in link.packets))

    def test_right_sealed_context_and_unchanged_left_are_required(self) -> None:
        for fault in (
            "state",
            "role",
            "session",
            "received",
            "size",
            "crc",
            "abi",
            "resident",
            "peer",
        ):
            with (
                self.subTest(fault=fault),
                self.fixture(target=1, universal=True) as (_, link, operator, _),
            ):
                operator.baseline()
                if fault == "peer":
                    link.on_seal = lambda: link.ids.__setitem__(0, (1024, 999))
                else:
                    link.sealed_info_fault = fault
                with self.assertRaises(RuntimeError):
                    operator.install()
                self.assertNotIn(Op.INSTALL, link.writes())
                self.assertEqual(link.writes()[-1], Op.ABORT)

    def test_offline_case_is_exclusive_private_and_freezes_all_inputs(self) -> None:
        with resident_fixture(), tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            package, case = root / "package", root / "case"
            self.package(package)
            QualifiedInstall.create_case(case, package, BEFORE[0], BEFORE[1], "a" * 40)
            self.assertTrue(case.is_dir())
            self.assertEqual(stat.S_IMODE(case.stat().st_mode), 0o700)
            for path in case.rglob("*"):
                if path.is_file():
                    self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o600)
            self.assertEqual(
                (case / "package/application.bin").read_bytes(),
                (package / "application.bin").read_bytes(),
            )
            self.assertTrue((case / "qualified_install.py").is_file())
            with self.assertRaises(FileExistsError):
                QualifiedInstall.create_case(
                    case, package, BEFORE[0], BEFORE[1], "a" * 40
                )

    def test_frozen_input_or_case_metadata_tamper_fails_before_usb(self) -> None:
        with self.fixture() as (case, link, operator, _):
            for name in (
                "qualified_install.py",
                "update.py",
                "read_health.py",
                "studio_rpc.py",
                "package/application.bin",
                "package/resident.bin",
                "package/update.json",
                "freeze.json",
            ):
                path = case / name
                original = path.read_bytes()
                path.write_bytes(original + b"\n")
                with self.subTest(name=name), self.assertRaises(ValueError):
                    operator.baseline()
                path.write_bytes(original)
            self.assertEqual(link.opened, 0)

    def test_invalid_package_commit_and_identity_are_rejected_offline(self) -> None:
        with resident_fixture(), tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            package = root / "package"
            self.package(package)
            with self.assertRaises(ValueError):
                QualifiedInstall.create_case(
                    root / "case", package, BEFORE[0], BEFORE[1], "bad"
                )
            with self.assertRaises(ValueError):
                QualifiedInstall.create_case(
                    root / "case", package, (1023, 1), BEFORE[1], "a" * 40
                )
            (package / "application.bin").write_bytes(bytes(1024))
            with self.assertRaises(ValueError):
                QualifiedInstall.create_case(
                    root / "case", package, BEFORE[0], BEFORE[1], "a" * 40
                )

    def test_baseline_requires_exact_both_images_and_zero_watchdog_policy(self) -> None:
        for bad in ("left", "right", "resident", "policy"):
            with self.fixture() as (case, link, operator, _):
                if bad in ("left", "right"):
                    link.ids[int(bad == "right")] = (1024, 999)
                elif bad == "resident":
                    link.bad_boot = True
                else:
                    link.health_flags = 7
                with (
                    self.subTest(bad=bad),
                    self.assertRaises((RuntimeError, ValueError)),
                ):
                    operator.baseline()
                self.assertFalse((case / "baseline-health.json").exists())
                self.assertEqual(link.writes(), [])

    def test_expired_or_changed_boot_proof_cannot_begin(self) -> None:
        for bad in ("expired", "left", "right"):
            with self.fixture() as (_, link, operator, clock):
                operator.baseline()
                if bad == "expired":
                    clock.now += 301
                else:
                    link.uptime_offsets[int(bad == "right")] -= 10000
                with (
                    self.subTest(bad=bad),
                    self.assertRaises((RuntimeError, ValueError)),
                ):
                    operator.install()
                self.assertEqual(link.writes(), [])

    def test_incomplete_or_retried_baseline_cannot_open_install_link(self) -> None:
        invalid: tuple[tuple[str, JsonValue], ...] = (
            ("verified_window_seconds", 69.9),
            ("transport_retries", 1),
            ("relay_retries", 1),
            ("reconnects", 1),
            ("read_only", False),
        )
        for key, value in invalid:
            with self.fixture() as (case, link, operator, _):
                operator.baseline()
                self.change_proof(case, key, value)
                opened = link.opened
                with self.subTest(key=key), self.assertRaises(ValueError):
                    operator.install()
                self.assertEqual(link.opened, opened)
                self.assertEqual(link.writes(), [])

    def test_staging_expiration_aborts_without_seal_or_install(self) -> None:
        with self.fixture() as (case, link, operator, clock):
            operator.baseline()
            clock.now += 297
            link.data_delay = 0.1
            with self.assertRaises(ValueError):
                operator.install()
            self.assertEqual(link.writes()[-1], Op.ABORT)
            self.assertNotIn(Op.SEAL, link.writes())
            self.assertNotIn(Op.INSTALL, link.writes())
            self.assertFalse((case / "INSTALL-requested.json").exists())

    def test_frozen_package_tamper_at_seal_aborts_before_install(self) -> None:
        with self.fixture() as (case, link, operator, _):
            operator.baseline()

            def tamper() -> None:
                (case / "package/application.bin").write_bytes(bytes(1024))

            link.on_seal = tamper
            with self.assertRaises(ValueError):
                operator.install()
            self.assertEqual(link.writes()[-1], Op.ABORT)
            self.assertNotIn(Op.INSTALL, link.writes())

    def test_right_running_image_and_resident_are_rechecked_after_seal(self) -> None:
        for field in ("application", "resident"):
            with self.fixture() as (case, link, operator, _):
                operator.baseline()

                def changed(field: str = field) -> None:
                    if field == "application":
                        link.ids[1] = (1024, 999)
                    else:
                        link.bad_right_boot = True

                link.on_seal = changed
                with self.subTest(field=field), self.assertRaises(RuntimeError):
                    operator.install()
                self.assertEqual(link.writes()[-1], Op.ABORT)
                self.assertNotIn(Op.INSTALL, link.writes())
                self.assertFalse((case / "INSTALL-requested.json").exists())

    def test_sealed_left_info_requires_original_image_and_exact_update_context(
        self,
    ) -> None:
        for field in (
            "state",
            "role",
            "session",
            "received",
            "size",
            "crc",
            "abi",
            "resident",
        ):
            with self.fixture() as (case, link, operator, _):
                operator.baseline()
                link.sealed_info_fault = field
                with self.subTest(field=field), self.assertRaises(RuntimeError):
                    operator.install()
                self.assertEqual(link.writes()[-1], Op.ABORT)
                self.assertNotIn(Op.INSTALL, link.writes())
                self.assertFalse((case / "INSTALL-requested.json").exists())

    def test_partial_marker_failure_prevents_both_install_and_abort(self) -> None:
        with self.fixture() as (case, link, operator, _):
            operator.baseline()
            (case / "INSTALL-requested.json.sha256").write_bytes(b"collision")
            with self.assertRaises(FileExistsError):
                operator.install()
            self.assertTrue((case / "INSTALL-requested.json").is_file())
            self.assertNotIn(Op.INSTALL, link.writes())
            self.assertNotIn(Op.ABORT, link.writes())

    def test_staging_rechecks_boot_before_install_and_aborts_before_marker(
        self,
    ) -> None:
        for target in (0, 1):
            with self.fixture() as (case, link, operator, _):
                operator.baseline()
                link.restart_at_seal = target
                with (
                    self.subTest(target=target),
                    self.assertRaises((RuntimeError, ValueError)),
                ):
                    operator.install()
                self.assertIn(Op.SEAL, link.writes())
                self.assertEqual(link.writes()[-1], Op.ABORT)
                self.assertNotIn(Op.INSTALL, link.writes())
                self.assertFalse((case / "INSTALL-requested.json").exists())

    def test_marker_persistence_delay_cannot_dispatch_install_with_expired_proof(
        self,
    ) -> None:
        with self.fixture() as (case, link, operator, clock):
            operator.baseline()
            fsync = os.fsync

            def delayed(descriptor: int) -> None:
                fsync(descriptor)
                clock.sleep(151)

            with (
                patch("qualified_install.os.fsync", side_effect=delayed),
                self.assertRaises(ValueError),
            ):
                operator.install()
            self.assertTrue((case / "INSTALL-requested.json").exists())
            self.assertNotIn(Op.INSTALL, link.writes())
            self.assertNotIn(Op.ABORT, link.writes())

    def test_abort_only_after_begin_ack_and_before_install_marker(self) -> None:
        for failure in ("begin", "data"):
            with self.fixture() as (case, link, operator, _):
                operator.baseline()
                link.fail_begin = failure == "begin"
                link.fail_data = failure == "data"
                with self.subTest(failure=failure), self.assertRaises(RuntimeError):
                    operator.install()
                self.assertEqual(Op.ABORT in link.writes(), failure == "data")
                self.assertFalse((case / "INSTALL-requested.json").exists())

    def test_bad_install_ack_never_aborts_or_replays(self) -> None:
        for field in (
            "operation",
            "state",
            "session",
            "size",
            "crc",
            "role",
            "abi",
            "reserved",
        ):
            with self.fixture() as (case, link, operator, _):
                operator.baseline()
                link.bad_ack = field
                with self.subTest(field=field), self.assertRaises(RuntimeError):
                    operator.install()
                self.assertEqual(link.writes().count(Op.INSTALL), 1)
                self.assertNotIn(Op.ABORT, link.writes())
                self.assertTrue((case / "INSTALL-requested.json").is_file())
                with self.assertRaises(FileExistsError):
                    operator.install()
                self.assertEqual(link.writes().count(Op.INSTALL), 1)

    def test_install_ack_zero_reserved_word_is_not_the_info_resident_crc(self) -> None:
        with self.fixture() as (_, link, operator, _):
            operator.baseline()
            rejected: str | None = None
            try:
                operator.install()
            except RuntimeError as error:
                rejected = str(error)
            self.assertEqual(link.pending[28:32], b"\0\0\0\0")
            self.assertIsNone(
                rejected, "A valid INSTALL ACK has a zero reserved final word"
            )

    def test_lost_install_ack_polls_only_status_and_never_replays(self) -> None:
        with self.fixture() as (_, link, operator, _):
            operator.baseline()
            link.lost_install_ack = True
            operator.install()
            install = next(
                index
                for index, packet in enumerate(link.packets)
                if packet[1] == 0x70 and packet[4] == Op.INSTALL
            )
            self.assertEqual(
                [packet[4] for packet in link.packets[install + 1 :]], [Op.STATUS]
            )
            self.assertEqual(link.writes().count(Op.INSTALL), 1)
            self.assertNotIn(Op.ABORT, link.writes())

    def test_normal_install_and_independent_readonly_return_qualification(self) -> None:
        with self.fixture() as (case, link, operator, _):
            operator.baseline()
            link.install_marker = case / "INSTALL-requested.json"
            operator.install()
            operator.observe()
            self.assertTrue((case / "INSTALL-ack.json").is_file())
            self.assertTrue((case / "after-health.json").is_file())
            self.assertEqual(link.writes().count(Op.INSTALL), 1)
            self.assertEqual(link.reconnects, 0)
            self.assertGreaterEqual(link.closed, 3)

    def test_readback_wrong_image_or_resident_never_writes_or_resets(self) -> None:
        for bad in ("left", "right", "resident"):
            with self.fixture() as (case, link, operator, _):
                operator.baseline()
                operator.install()
                if bad in ("left", "right"):
                    link.ids[int(bad == "right")] = (1024, 999)
                else:
                    link.bad_boot = True
                before = link.writes()
                with self.subTest(bad=bad), self.assertRaises(TimeoutError):
                    operator.observe()
                self.assertEqual(link.writes(), before)
                self.assertFalse((case / "after-health.json").exists())
                self.assertEqual(link.reconnects, 0)


if __name__ == "__main__":
    unittest.main()
