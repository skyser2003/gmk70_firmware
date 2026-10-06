"""Read fixed diagnostic getters; no keymap, reset or flash operations."""

import sys
from typing import Protocol

import hid


class Connection(Protocol):
    def write(self, buff: bytes) -> int: ...
    def read(self, max_length: int, timeout_ms: int = 0) -> list[int]: ...


class DiagnosticPage:
    @staticmethod
    def request(page: int) -> bytes:
        if page not in range(8):
            raise ValueError("Only diagnostic getter pages 0 through 7 are permitted")
        return bytes((0, 2, 0x80, page)) + bytes(29)

    @classmethod
    def validate(cls, page: int, response: bytes) -> bytes:
        cls.request(page)
        if len(response) != 32 or response[:6] != bytes((2, 0x80, page, 0x47, 0x42, 1)):
            raise ValueError(
                "No matching diagnostic response. The normal/older firmware does "
                "not support this getter, or the response is incomplete."
            )
        return response[6:]


class DiagnosticReader:
    def __init__(self, connection: Connection) -> None:
        self._connection = connection

    def read_page(self, page: int) -> bytes:
        packet = DiagnosticPage.request(page)
        if self._connection.write(packet) != len(packet):
            raise RuntimeError("Incomplete getter request")
        response = bytes(self._connection.read(32, 1500))
        DiagnosticPage.validate(page, response)
        return response

    @staticmethod
    def snapshot(data: bytes) -> str:
        if len(data) != 19:
            raise ValueError("Invalid snapshot length")
        flag_names = (
            "master",
            "left",
            "charging",
            "full",
            "rgb_recording",
            "battery_request",
            "charging_request",
            "indicators_enabled",
            "inquiry_block",
            "white_test",
            "rgb_enabled",
            "rgb_suspended",
        )
        flags = int.from_bytes(data[8:10])
        pin_names = (
            "A7_USB",
            "A15_full",
            "C15_BT",
            "C14_2G4",
            "A5_LED",
            "A8_LED",
            "B9_half",
        )
        fields = (
            ("board_device", 10),
            ("radio_device", 11),
            ("radio_state", 12),
            ("battery_percent", 13),
            ("peer_battery_percent", 14),
            ("rgb_mode", 15),
            ("brightness", 16),
            ("led_flags", 17),
        )
        lines = [
            f"  active_layers=0x{int.from_bytes(data[:4]):08X}",
            f"  default_layers=0x{int.from_bytes(data[4:8]):08X}",
            *(
                f"  {name}={bool(flags & (1 << i))}"
                for i, name in enumerate(flag_names)
            ),
            *(f"  {name}={data[offset]}" for name, offset in fields),
            *(
                f"  pin_{name}={bool(data[18] & (1 << i))}"
                for i, name in enumerate(pin_names)
            ),
        ]
        return "\n".join(lines)

    def report(self) -> str:
        pages = tuple(self.read_page(page) for page in (0, 1, 2))
        raw_lines = [f"page{page}={data.hex()}" for page, data in enumerate(pages)]
        current, press, counts = tuple(page[6:] for page in pages)
        frame_lines: list[str] = []
        if counts[24] & 1:
            frame_page = self.read_page(3)
            frame = frame_page[6:]
            raw_lines.append(f"page3={frame_page.hex()}")
            frame_lines = [
                "LED 33 buffer immediately before transfer (not an electrical measurement):",
                f"  frames={int.from_bytes(frame[:4])}",
                f"  overlay_frames={int.from_bytes(frame[4:8])}",
                f"  last_overlay_uptime_ms={int.from_bytes(frame[8:12])}",
                f"  last_overlay_kind={frame[12]} (1=level, 2=charging)",
                f"  last_overlay_before_rgb={tuple(frame[13:16])}",
                f"  last_overlay_after_rgb={tuple(frame[16:19])}",
                f"  current_rgb={tuple(frame[19:22])}",
            ]
        if counts[24] & 2:
            phase_names = ("normal charge", "all red", "LED 5 white", "LED 33 white")
            for phase, name in enumerate(phase_names):
                page_number = phase + 4
                output_page = self.read_page(page_number)
                raw_lines.append(f"page{page_number}={output_page.hex()}")
                output = output_page[6:]
                frame_lines.extend(
                    (
                        f"Encoded SPI probe phase {phase} ({name}; not an electrical measurement):",
                        f"  transfers={int.from_bytes(output[:4])}",
                        f"  starts_while_busy={int.from_bytes(output[4:8])}",
                        f"  last_sample_uptime_ms={int.from_bytes(output[8:12])}",
                        f"  packet_bytes={int.from_bytes(output[12:14])}",
                        f"  encoded_led5_rgb={tuple(output[14:17])}",
                        f"  encoded_led33_rgb={tuple(output[17:20])}",
                        f"  sampled_symbols_valid={bool(output[20])}",
                        f"  left_pixels_matching_led0={output[21]}",
                        f"  previous_tx_remaining={int.from_bytes(output[22:24])}",
                        f"  previous_rx_remaining={int.from_bytes(output[24:26])}",
                    )
                )
        stages = (
            "entered",
            "consumed_by_RGB_recording",
            "consumed_by_device_handler",
            "battery_handler_completed",
            "charging_handler_completed",
        )
        stage = stages[press[2]] if press[2] < len(stages) else f"invalid({press[2]})"
        counter_names = (
            "battery_device_presses",
            "battery_device_releases",
            "indicator_calls",
            "battery_draw_calls",
            "charging_draw_calls",
            "last_battery_draw_uptime_ms",
        )
        return "\n".join(
            (
                "GMK70 battery diagnostic protocol 1 (read only)",
                "LED transmission: synchronous"
                if counts[24] & 4
                else "LED transmission: asynchronous/unspecified",
                f"Current uptime_ms={int.from_bytes(current[:4])}",
                self.snapshot(current[4:23]),
                (
                    f"Last tracked press: keycode=0x{int.from_bytes(press[:2]):04X}, "
                    f"stage={stage}, uptime_ms={int.from_bytes(press[3:7])}"
                ),
                self.snapshot(press[7:26]),
                "Counters since power-on (draw calls are not proof of visible LEDs):",
                *(
                    f"  {name}={int.from_bytes(counts[4 * i : 4 * i + 4])}"
                    for i, name in enumerate(counter_names)
                ),
                *frame_lines,
                *raw_lines,
            )
        )

    @classmethod
    def run(cls) -> str:
        interfaces = [
            info
            for info in hid.enumerate(0x342D, 0xE491)
            if info["usage_page"] == 0xFF60
            and info["usage"] == 0x61
            and info["manufacturer_string"] in ("HS", "LEO")
        ]
        if len(interfaces) != 1:
            raise RuntimeError(
                "Connect exactly one GMK70 half directly by USB; a wireless receiver "
                "is not accepted by this reader."
            )
        connection = hid.device()
        connection.open_path(interfaces[0]["path"])
        try:
            return cls(connection).report()
        finally:
            connection.close()


if __name__ == "__main__":
    try:
        print(DiagnosticReader.run())
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
