"""Read retained split/sleep diagnostics; never reset, flash, or change settings."""

import sys
from typing import ClassVar

from read_battery import DiagnosticReader


class SplitPage:
    @staticmethod
    def request(page: int, sequence: int = 0) -> bytes:
        if page not in range(4):
            raise ValueError("Only split getter pages 0 through 3 are permitted")
        if (page == 3 and not 1 <= sequence <= 0xFFFFFFFF) or (
            page != 3 and sequence != 0
        ):
            raise ValueError("Only event page 3 accepts a nonzero event sequence")
        return bytes((0, 2, 0x81, page)) + sequence.to_bytes(4) + bytes(25)

    @classmethod
    def validate(cls, page: int, sequence: int, response: bytes) -> bytes:
        cls.request(page, sequence)
        if (
            len(response) != 32
            or response[:5] != bytes((2, 0x81, page, 71, 83))
            or response[5] not in (1, 2)
        ):
            raise ValueError(
                "No matching split diagnostic response. Flash the new via_diagnostic "
                "build to both halves, use wired USB, and close VIA before reading."
            )
        if page == 3 and int.from_bytes(response[6:10]) != sequence:
            raise ValueError("Mismatched event sequence in diagnostic response")
        return response[6:]


class SplitReader(DiagnosticReader):
    """Uses the same constrained USB selection and lifetime as the battery reader."""

    _states = ("NORMAL", "PRESLEEP", "STOP", "WAKEUP")
    _wake = ("NONE", "MATRIX", "UART", "CABLE", "USB", "ONEKEY", "ENCODER", "SWITCH")
    _reasons: ClassVar[dict[int, str]] = {
        1: "radio disconnected timeout",
        2: "radio idle timeout",
        3: "low battery",
        4: "host suspend",
        5: "USB special sleep",
    }
    _commands: ClassVar[dict[int, str]] = {
        0x55: "mode",
        0x56: "battery",
        0xA4: "battery press",
        0xA5: "battery release",
        0xF0: "charging press",
        0xF1: "charging release",
        0xAA: "sleep",
        0xBB: "LED off",
        0xCC: "LED on",
        0xDD: "reset",
    }

    def read_split(self, page: int, sequence: int = 0) -> bytes:
        packet = SplitPage.request(page, sequence)
        if self._connection.write(packet) != len(packet):
            raise RuntimeError("Incomplete split getter request")
        response = bytes(self._connection.read(32, 1500))
        SplitPage.validate(page, sequence, response)
        return response

    @classmethod
    def status(cls, value: int, version: int = 1) -> str:
        spi = f"SPI_active={bool(value & 32)} " if version == 1 else ""
        return (
            f"state={cls._states[value & 3]} manual={bool(value & 4)} "
            f"A5_LED={bool(value & 8)} A8_LED={bool(value & 16)} "
            f"{spi}low_battery_sleep={bool(value & 64)} "
            f"master={bool(value & 128)}"
        )

    @classmethod
    def wake(cls, value: int) -> str:
        return cls._wake[value] if value < len(cls._wake) else f"invalid({value})"

    @classmethod
    def command(cls, value: int) -> str:
        return f"0x{value:02X}({cls._commands.get(value, 'other')})"

    @staticmethod
    def age(value: int) -> str:
        return (
            f"{value * 16}ms" if value < 255 else ">=4080ms (or loop not yet observed)"
        )

    @classmethod
    def event(cls, data: bytes, version: int = 1) -> str:
        sequence = int.from_bytes(data[:4])
        if not data[12]:
            return f"event #{sequence}: overwritten/unavailable while reading"
        time = int.from_bytes(data[4:8])
        event, a, b, c = data[8:12]
        match event:
            case 1:
                detail = f"power state {cls._states[a & 3]} -> {cls._states[b & 3]}, pending_manual={bool(c)}"
            case 2:
                detail = f"manual sleep={bool(a)}, state={cls._states[b & 3]}"
            case 3:
                detail = f"radio host resume={bool(a)}, state={cls._states[b & 3]}, pending_manual={bool(c)}"
            case 4 | 5:
                detail = f"MCU STOP {'entry attempt' if event == 4 else 'path return'} state={cls._states[a & 3]} manual={bool(b)} wake={cls.wake(c)}"
            case 6:
                detail = f"device {a} -> {b}, pairing_reset={bool(c)}"
            case 7:
                detail = (
                    f"sleep requested: {cls._reasons.get(a, 'unknown')}, context={b}"
                )
            case 8:
                detail = f"RPC {cls.command(a)} success={bool(b)}"
            case 9 | 10:
                detail = (
                    f"control {'sent' if event == 9 else 'received'} {cls.command(a)}"
                )
            case 11:
                detail = f"peer {cls.status(a, version)} loop_age={cls.age(b)}"
                if version == 1:
                    detail += f" SPI_age={cls.age(c)}"
            case 12:
                detail = f"local {cls.status(a, version)} device={b} radio={c}"
            case 13:
                detail = f"QMK split connected={bool(a)} {cls.status(b, version)}"
            case 14:
                detail = (
                    f"SPI returned after {cls.age(a)}"
                    if version == 1
                    else "reserved legacy SPI event (not measured by protocol 2)"
                )
            case 15:
                detail = f"main loop returned after {cls.age(a)} {cls.status(b, version)} wake={cls.wake(c)}"
            case 16:
                detail = f"sleep accepted: manual={bool(a)} idle_seconds={'at least ' if b == 255 else ''}{b} device={c}"
            case 17:
                detail = f"MCU STOP entry cancelled state={cls._states[a & 3]} manual={bool(b)} wake={cls.wake(c)}"
            case _:
                detail = f"unknown event {event}: {a}, {b}, {c}"
        return f"#{sequence} at {time}ms: {detail}"

    def report(self) -> str:
        pages = [self.read_split(page) for page in range(3)]
        version = pages[0][5]
        if any(page[5] != version for page in pages):
            raise ValueError("Split diagnostic protocol changed while reading")
        current, counts, peer = (page[6:] for page in pages)
        total = int.from_bytes(current[4:8])
        capacity = current[8]
        if capacity != 128:
            raise ValueError(f"Unexpected event capacity for split protocol {version}")
        lines = [
            f"GMK70 split diagnostic protocol {version} (read only, RAM history)",
            "Times are local uptime, not wall time; age samples saturate at 4080 ms.",
            "No keycodes or typed text are recorded by this logger.",
            f"Current uptime={int.from_bytes(current[:4])}ms left={bool(current[14])} split_connected={bool(current[15])}",
            self.status(current[9], version),
            f"device={current[10]} radio_state={current[11]} wake={self.wake(current[12])} pins_A7_A15_C15_C14_B9=0x{current[13]:02X}",
            f"main_loop_age={int.from_bytes(current[16:20])}ms",
            f"RPC_calls={int.from_bytes(counts[:4])} failures={int.from_bytes(counts[4:8])} last_success={int.from_bytes(counts[8:12])}ms",
            f"last_RPC={self.command(counts[24])} RPC_in_progress={bool(counts[25])}",
        ]
        if version == 1:
            lines.extend(
                (
                    "Peer samples use the legacy 250 ms battery RPC.",
                    f"SPI_active_age={int.from_bytes(current[20:24])}ms",
                    f"SPI_started={int.from_bytes(counts[12:16])} completed={int.from_bytes(counts[16:20])} max_duration={int.from_bytes(counts[20:24])}ms",
                )
            )
        else:
            lines.extend(
                (
                    "Peer samples use the existing two-second mode RPC (0x55).",
                    "SPI telemetry unavailable: baseline asynchronous LED output is not instrumented.",
                )
            )
        if peer[8]:
            peer_status = (
                f"  {self.status(peer[5], version)} loop_age={self.age(peer[6])}"
            )
            if version == 1:
                peer_status += f" SPI_age={self.age(peer[7])}"
            lines.extend(
                (
                    f"Last peer sample received at local {int.from_bytes(peer[:4])}ms:",
                    peer_status,
                )
            )
        else:
            lines.append(
                "No diagnostic peer sample received. Both halves need this build."
            )
        lines.append(
            f"Event history: {total} total, last {min(total, capacity)} retained."
        )
        raw = [f"split_page{i}={page.hex()}" for i, page in enumerate(pages)]
        for sequence in range(max(1, total - capacity + 1), total + 1):
            page = self.read_split(3, sequence)
            if page[5] != version:
                raise ValueError("Split diagnostic protocol changed while reading")
            lines.append(self.event(page[6:], version))
            raw.append(f"split_event{sequence}={page.hex()}")
        return "\n".join((*lines, *raw))


if __name__ == "__main__":
    try:
        print(SplitReader.run())
    except (OSError, RuntimeError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
