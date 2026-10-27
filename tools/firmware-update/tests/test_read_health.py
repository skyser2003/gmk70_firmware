import struct
import unittest
from unittest.mock import patch

from test_update import HidFixture

from read_health import Health, HealthReader, validate_progress
from update import ZMK_BOOT_CRC, Op, UsbLink


def response(
    target: int = 1, nonce: int = 7, uptime: int = 1000, feeds: int = 100
) -> bytes:
    return bytes((0x7F, 0x72, 1, target, 0x20, 0, 0, 0 if target else 3)) + struct.pack(
        "<6I", nonce, 0x4B4D5A47, uptime, feeds, 7, 0
    )


class HealthTests(unittest.TestCase):
    def test_late_reply_cannot_satisfy_short_qualification_deadline(self) -> None:
        now = [0.0]

        class Link:
            def exchange(self, packet: bytes) -> bytes:
                now[0] += 2.0
                return response()

        with (
            patch("read_health.time.monotonic", side_effect=lambda: now[0]),
            self.assertRaises(TimeoutError),
        ):
            HealthReader(Link()).sample(1, 7, timeout=0.5)

    def test_read_only_minute_evidence_separates_retries_and_never_reconnects(
        self,
    ) -> None:
        class Clock:
            now = 0.0

            def sleep(self, delay: float) -> None:
                self.now += delay

        clock = Clock()

        class Link:
            failed_transport = False
            failed_relay = False

            def __init__(self) -> None:
                self.packets: list[bytes] = []

            def exchange(self, packet: bytes) -> bytes:
                self.packets.append(packet)
                target = packet[3]
                if packet[1] == 0x70:
                    self_outer.assertEqual(packet[4], Op.INFO)
                    return bytes(
                        (0x7F, 0x70, 1, target, Op.INFO, 0, 0, 0 if target else 3)
                    ) + struct.pack("<6I", 0, 0, 1024, 123, 1, ZMK_BOOT_CRC)
                if not self.failed_transport:
                    self.failed_transport = True
                    raise OSError("dropped USB reply")
                if not self.failed_relay:
                    self.failed_relay = True
                    reply = bytearray(packet)
                    reply[5] = 7
                    reply[7] = 3
                    return bytes(reply)
                nonce = struct.unpack_from("<I", packet, 8)[0]
                return response(
                    target,
                    nonce,
                    int((clock.now + 10) * 1000),
                    int((clock.now + 10) * 100),
                )

        self_outer = self
        link = Link()
        reader = HealthReader(link)
        with (
            patch("read_health.time.monotonic", side_effect=lambda: clock.now),
            patch("read_health.time.sleep", side_effect=clock.sleep),
        ):
            evidence = reader.observe()
        self.assertGreaterEqual(clock.now, 60)
        self.assertEqual(evidence["transport_retries"], 1)
        self.assertEqual(evidence["relay_retries"], 1)
        self.assertEqual(evidence["reconnects"], 0)
        self.assertTrue(
            all(
                p[1] == 0x72 or (p[1] == 0x70 and p[4] == Op.INFO) for p in link.packets
            )
        )
        for duration in (0, 59):
            with self.assertRaises(ValueError):
                reader.observe(duration=duration)

    def test_cached_peer_failure_retries_fresh_nonce_but_busy_does_not(self) -> None:
        class Link:
            def __init__(self) -> None:
                self.packets: list[bytes] = []

            def exchange(self, packet: bytes) -> bytes:
                self.packets.append(packet)
                nonce = struct.unpack_from("<I", packet, 8)[0]
                if nonce == 7:
                    reply = bytearray(packet)
                    reply[5] = 7
                    reply[7] = 3
                    return bytes(reply)
                return response(nonce=nonce)

        link = Link()
        reader = HealthReader(link)
        health = reader.sample(1, 7, timeout=0.1)
        self.assertNotEqual(health.nonce, 7)
        self.assertEqual(reader.relay_retries, 1)
        self.assertEqual(len(link.packets), 2)

    def test_successful_minute_starts_only_after_young_uptime_exceeds_gap(self) -> None:
        class Clock:
            now = 0.0

            def sleep(self, delay: float) -> None:
                self.now += delay

        clock = Clock()

        class Link:
            def exchange(self, packet: bytes) -> bytes:
                if packet[1] == 0x70:
                    return bytes((0x7F, 0x70, 1, 0, Op.INFO, 0, 0, 3)) + struct.pack(
                        "<6I", 0, 0, 1024, 123, 1, ZMK_BOOT_CRC
                    )
                nonce = struct.unpack_from("<I", packet, 8)[0]
                return response(
                    0,
                    nonce,
                    int((clock.now + 0.1) * 1000),
                    int((clock.now + 0.1) * 100),
                )

        with (
            patch("read_health.time.monotonic", side_effect=lambda: clock.now),
            patch("read_health.time.sleep", side_effect=clock.sleep),
        ):
            evidence = HealthReader(Link()).observe((0,))
        self.assertGreaterEqual(clock.now, 62)
        self.assertEqual(evidence["max_gap_seconds"], 2.0)
        # Once uptime exceeds the maximum sample gap, every fresh reboot must
        # move uptime backwards, even if it occurs just after the prior sample.
        old = Health.decode(response(0, 7, uptime=2100, feeds=100), 0, 7)
        reset = Health.decode(response(0, 8, uptime=1900, feeds=110), 0, 8)
        with self.assertRaisesRegex(RuntimeError, "reboot"):
            validate_progress(old, reset, 2.0, 2.0)
        with self.assertRaisesRegex(RuntimeError, "gap"):
            validate_progress(old, reset, 2.1, 2.0)

    def test_qualification_requires_no_watchdog_and_zero_feeds(self) -> None:
        packet = bytearray(response(feeds=0))
        struct.pack_into("<I", packet, 24, 10)
        self.assertEqual(Health.decode(bytes(packet), 1, 7, staging=True).flags, 10)
        with self.assertRaises(RuntimeError):
            Health.decode(bytes(packet), 1, 7)
        struct.pack_into("<I", packet, 20, 1)
        with self.assertRaises(RuntimeError):
            Health.decode(bytes(packet), 1, 7, staging=True)

    def test_decode_and_matrix_zero_is_unavailable(self) -> None:
        health = Health.decode(response(), 1, 7)
        self.assertEqual(health.uptime_ms, 1000)
        self.assertIsNone(health.matrix_events)

    def test_malformed_or_wrong_identity_and_mode_are_rejected(self) -> None:
        for offset, value in (
            (0, 0),
            (1, 0x70),
            (2, 2),
            (3, 0),
            (4, 1),
            (5, 7),
            (6, 1),
            (7, 3),
            (8, 8),
            (12, 0),
            (24, 15),
            (24, 6),
            (24, 5),
        ):
            packet = bytearray(response())
            packet[offset] = value
            with (
                self.subTest(offset=offset, value=value),
                self.assertRaises(RuntimeError),
            ):
                Health.decode(bytes(packet), 1, 7)
        for wrong_length in (response()[:-1], response() + b"\0"):
            with self.assertRaises(RuntimeError):
                Health.decode(wrong_length, 1, 7)

    def test_busy_repeats_identical_nonce_and_never_polls_status(self) -> None:
        busy = (
            bytes((0x7F, 0x72, 1, 1, 0x20, 1, 0, 3)) + struct.pack("<I", 7) + bytes(20)
        )

        class Link:
            def __init__(self) -> None:
                self.packets: list[bytes] = []

            def exchange(self, packet: bytes) -> bytes:
                self.packets.append(packet)
                return busy if len(self.packets) < 3 else response()

        link = Link()
        health = HealthReader(link).sample(1, 7)
        self.assertEqual(health.feed_calls, 100)
        self.assertEqual(len(link.packets), 3)
        self.assertTrue(all(p == link.packets[0] for p in link.packets))
        self.assertEqual(link.packets[0][:5], bytes((0x7F, 0x72, 1, 1, 0x20)))

    def test_modulo32_progress_and_reboot_stopped_or_gap_detection(self) -> None:
        previous = Health.decode(response(uptime=0xFFFFFF00, feeds=0xFFFFFFFE), 1, 7)
        current = Health.decode(response(nonce=8, uptime=744, feeds=15), 1, 8)
        validate_progress(previous, current, 1.0, 5.0)
        before = Health.decode(response(uptime=10000, feeds=100), 1, 7)
        for now, elapsed in (
            (response(uptime=100, feeds=1), 1.0),
            (response(uptime=11000, feeds=100), 1.0),
            (response(uptime=10000, feeds=110), 1.0),
            (response(uptime=20000, feeds=110), 10.0),
        ):
            with self.assertRaises(RuntimeError):
                validate_progress(before, Health.decode(now, 1, 7), elapsed, 5.0)

    def test_usb_link_keeps_health_namespace_and_nonce(self) -> None:
        backend = HidFixture([[response()]])
        with patch.dict("sys.modules", {"hid": backend}):
            link = UsbLink()
            self.assertEqual(HealthReader(link).sample(1, 7).nonce, 7)
            link.close()
