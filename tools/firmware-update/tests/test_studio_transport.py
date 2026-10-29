"""Feature framing uses the same updater correlation as legacy HID."""

import hashlib
import unittest
import zlib
from unittest.mock import patch

from test_update import FakeLink, HidFixture, data_reply, package

from update import ZMK_BOOT_CRC, ZMK_BOOT_SHA256, Op, Package, Updater, UsbLink


class FeatureFixture(HidFixture):
    def __init__(self, responses: list[list[bytes]]) -> None:
        super().__init__(responses)
        self.usage = 0x62
        self.frames: list[bytes] = []

    def enumerate(self, vendor: int, product: int) -> list[dict[str, object]]:
        devices = super().enumerate(vendor, product)
        devices[0]["usage"] = self.usage
        return devices

    def send_feature_report(self, data: bytes) -> int:
        if len(data) != 33 or data[0] != 4:
            raise AssertionError("wrong Feature frame")
        self.frames.append(data)
        self.incoming.extend(self.responses.pop(0))
        return 33

    def get_feature_report(self, report: int, size: int) -> list[int]:
        if report != 4 or size != 33:
            raise AssertionError("wrong Feature query")
        return list(self.incoming.pop(0)) if self.incoming else [4] + [0] * 32


class StudioTransportTests(unittest.TestCase):
    def test_ambiguous_legacy_and_feature_are_not_opened(self) -> None:
        backend = FeatureFixture([])
        feature = backend.enumerate(0, 0)[0]
        with (
            patch.object(
                backend, "enumerate", return_value=[feature, feature | {"usage": 0x61}]
            ),
            patch.dict("sys.modules", {"hid": backend}),
            self.assertRaisesRegex(RuntimeError, "exactly one"),
        ):
            UsbLink()

    def test_trial_feature_retains_nonce_correlation(self) -> None:
        from test_trial_run import response

        from trial_run import TrialClient, TrialOp, TrialUsbLink

        request = TrialClient.packet(1, TrialOp.PROBE, 42)
        wanted = response(request)
        backend = FeatureFixture(
            [[b"\x04" + response(request, session=41), b"\x04" + wanted]]
        )
        with patch.dict("sys.modules", {"hid": backend}):
            link = TrialUsbLink()
            self.assertEqual(link.exchange(request), wanted)
            self.assertEqual(link.observation_errors, 1)

    def test_studio_size_and_resident_bounds(self) -> None:
        for size in (0x1A000, 0x1A004):
            pkg = package("zmk-studio")
            image = pkg.image.ljust(size, b"\0")
            pkg.manifest["app_size"] = size
            pkg.manifest["app_crc"] = zlib.crc32(image)
            pkg.manifest["app_sha256"] = hashlib.sha256(image).hexdigest()
            pkg.manifest["boot_crc"] = ZMK_BOOT_CRC
            pkg.manifest["boot_sha256"] = ZMK_BOOT_SHA256
            value = Package(image, pkg.manifest)
            if size == 0x1A000:
                value.validate_application()
                pkg.manifest["boot_crc"] ^= 1
                with self.assertRaisesRegex(ValueError, "resident"):
                    value.validate_application()
            else:
                with self.assertRaisesRegex(ValueError, "reservation"):
                    value.validate_application()

    def test_feature_filters_empty_wrong_id_short_and_stale(self) -> None:
        expected = data_reply(7, 32)
        backend = FeatureFixture(
            [
                [
                    b"\x04" + bytes(32),
                    b"\x03" + expected,
                    b"\x04short",
                    b"\x04" + data_reply(6, 32),
                    b"\x04" + expected,
                ]
            ]
        )
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            self.assertEqual(
                link.exchange(Updater.packet(0, Op.DATA, 7, 16, bytes(16))), expected
            )
            self.assertEqual(backend.nonblocking_changes, [])
            self.assertEqual(len(backend.frames), 1)

    def test_legacy_to_feature_reconnect(self) -> None:
        expected = data_reply(7, 32)
        backend = FeatureFixture([[expected], [b"\x04" + expected]])
        backend.usage = 0x61
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            self.assertEqual(
                link.exchange(Updater.packet(0, Op.DATA, 7, 16, bytes(16))), expected
            )
            backend.usage = 0x62
            link.reconnect()
            self.assertEqual(
                link.exchange(Updater.packet(0, Op.DATA, 7, 16, bytes(16))), expected
            )
            self.assertEqual(len(backend.frames), 1)

    def test_studio_right_and_both_install_rejected_before_device(self) -> None:
        pkg = package("zmk-studio")
        for target in (1,):
            link = FakeLink()
            with self.assertRaisesRegex(ValueError, "LEFT|left"):
                Updater(link).install(target, pkg)
            self.assertEqual(link.operations, [])
        link = FakeLink()
        with self.assertRaises(ValueError):
            Updater(link).install_both(pkg)
        self.assertEqual(link.operations, [])
