import unittest

from read_split import SplitPage, SplitReader


class SplitConnection:
    def __init__(self, total: int = 2, missing: bool = False, version: int = 1) -> None:
        self.requests: list[bytes] = []
        self.total = total
        self.missing = missing
        self.version = version

    def write(self, buff: bytes) -> int:
        self.requests.append(buff)
        return len(buff)

    def read(self, max_length: int, timeout_ms: int = 0) -> list[int]:
        request = self.requests[-1]
        page = request[3]
        payload = bytearray(26)
        if page == 0:
            payload[:4] = (10000).to_bytes(4)
            payload[4:8] = self.total.to_bytes(4)
            payload[8] = 128
            payload[9] = 0x98
        elif page == 2:
            payload[:4] = (9500).to_bytes(4)
            payload[4:9] = bytes((0xD1, 0x38, 32, 32, 1))
        elif page == 3:
            payload[:4] = request[4:8]
            payload[4:8] = (9000).to_bytes(4)
            payload[8:13] = bytes((11, 0x38, 32, 32, not self.missing))
        return list(bytes((2, 0x81, page, 71, 83, self.version)) + payload)


class SplitReaderTests(unittest.TestCase):
    def test_only_fixed_getters_and_event_cursors(self) -> None:
        for page in range(3):
            self.assertEqual(
                SplitPage.request(page), bytes((0, 2, 0x81, page)) + bytes(29)
            )
        self.assertEqual(
            SplitPage.request(3, 0x12345678),
            bytes.fromhex("0002810312345678") + bytes(25),
        )
        for page, seq in (
            (-1, 0),
            (4, 0),
            (0, 1),
            (1, 20),
            (2, -1),
            (3, 0),
            (3, -1),
            (3, 0x100000000),
        ):
            with self.assertRaises(ValueError):
                SplitPage.request(page, seq)

    def test_rejects_wrong_protocol_and_cursor(self) -> None:
        connection = SplitConnection()
        connection.write(SplitPage.request(3, 10))
        response = bytes(connection.read(32))
        SplitPage.validate(3, 10, response)
        for data in (
            b"",
            response[:-1],
            b"\0" + response[1:],
            response[:4] + b"B" + response[5:],
            response[:5] + b"\x03" + response[6:],
        ):
            with self.assertRaises(ValueError):
                SplitPage.validate(3, 10, data)
        with self.assertRaises(ValueError):
            SplitPage.validate(3, 11, response)

    def test_reports_stalled_peer_and_reads_only_getters(self) -> None:
        connection = SplitConnection()
        report = SplitReader(connection).report()
        self.assertIn("SPI_active=True", report)
        self.assertIn("loop_age=512ms SPI_age=512ms", report)
        self.assertIn("#2 at 9000ms: peer", report)
        self.assertEqual(
            connection.requests,
            [SplitPage.request(p) for p in range(3)]
            + [SplitPage.request(3, seq) for seq in (1, 2)],
        )

    def test_bounded_history_and_explicit_overwrite(self) -> None:
        connection = SplitConnection(total=140, missing=True)
        report = SplitReader(connection).report()
        self.assertEqual(len(connection.requests), 131)
        self.assertEqual(connection.requests[3], SplitPage.request(3, 13))
        self.assertEqual(connection.requests[-1], SplitPage.request(3, 140))
        self.assertIn("event #13: overwritten/unavailable", report)

    def test_baseline_protocol_reports_no_spi_measurements(self) -> None:
        connection = SplitConnection(version=2)
        report = SplitReader(connection).report()
        self.assertIn("protocol 2", report)
        self.assertIn("two-second mode RPC (0x55)", report)
        self.assertIn("SPI telemetry unavailable", report)
        self.assertIn("loop_age=512ms", report)
        self.assertIn("#2 at 9000ms: peer", report)
        for field in ("SPI_active=", "SPI_active_age=", "SPI_age=", "SPI_started="):
            self.assertNotIn(field, report)

    def test_rejects_protocol_change_mid_read(self) -> None:
        class ChangingProtocol(SplitConnection):
            def read(self, max_length: int, timeout_ms: int = 0) -> list[int]:
                self.version = 1 if self.requests[-1][3] == 0 else 2
                return super().read(max_length, timeout_ms)

        with self.assertRaisesRegex(ValueError, "protocol changed"):
            SplitReader(ChangingProtocol()).report()

    def test_no_peer_or_events_and_short_write(self) -> None:
        class NoPeer(SplitConnection):
            def read(self, max_length: int, timeout_ms: int = 0) -> list[int]:
                response = super().read(max_length, timeout_ms)
                if response[2] == 2:
                    response[14] = 0
                return response

        connection = NoPeer(total=0)
        self.assertIn("No diagnostic peer sample", SplitReader(connection).report())
        self.assertEqual(len(connection.requests), 3)

        class ShortWrite(SplitConnection):
            def write(self, buff: bytes) -> int:
                return 0

        with self.assertRaises(RuntimeError):
            SplitReader(ShortWrite()).report()


if __name__ == "__main__":
    unittest.main()
