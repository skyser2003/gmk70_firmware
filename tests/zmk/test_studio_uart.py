"""Real pinned CDC callback must preserve valid framed Studio requests."""

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class StudioUartTests(unittest.TestCase):
    def test_valid_requests_and_cdc_burst_are_not_dropped(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            generated = Path(tmp)
            proto = ROOT / "dependencies/modules/msgs/zmk-studio-messages/proto/zmk"
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "grpc_tools.protoc",
                    f"-I{proto}",
                    f"--python_out={generated}",
                    *map(str, proto.glob("*.proto")),
                ],
                check=True,
                capture_output=True,
            )
            sys.path.insert(0, tmp)
            try:
                import studio_pb2

                requests = [
                    studio_pb2.Request(request_id=1),
                    studio_pb2.Request(request_id=0xFFFFFFFF),
                    studio_pb2.Request(
                        request_id=(0x2B | (0x2C << 7) | (0x2D << 14) | (1 << 21))
                    ),
                ]
                for request in requests:
                    request.keymap.set_layer_props.layer_id = 1
                    request.keymap.set_layer_props.name = "ABCDEFGHIJKLMNOPQRS"
                binding = studio_pb2.Request(request_id=0xFFFFFFFF)
                binding.keymap.set_layer_binding.layer_id = 1
                binding.keymap.set_layer_binding.key_position = 71
                binding.keymap.set_layer_binding.binding.behavior_id = 0x7FFFFFFF
                binding.keymap.set_layer_binding.binding.param1 = 0xFFFFFFFF
                binding.keymap.set_layer_binding.binding.param2 = (
                    0x2B | (0x2C << 7) | (0x2D << 14) | (1 << 21)
                )
                requests.append(binding)
                short = studio_pb2.Request(request_id=1)
                short.keymap.set_layer_props.layer_id = 1
                short.keymap.set_layer_props.name = "ABCD"
                requests.append(short)
                frames = [
                    b"\xab"
                    + b"".join(
                        (b"\xac" if b in (0xAB, 0xAC, 0xAD) else b"") + bytes([b])
                        for b in r.SerializeToString()
                    )
                    + b"\xad"
                    for r in requests
                ]
            finally:
                sys.path.remove(tmp)
            self.assertEqual(len(frames[0]), 31)
            conf = (
                ROOT / "firmware/zmk/boards/zuoya/gmk70_wired/studio.conf"
            ).read_text()
            value = re.search(r"^CONFIG_ZMK_STUDIO_RPC_RX_BUF_SIZE=(\d+)$", conf, re.M)
            # Pinned upstream default; test must also reproduce the omitted setting.
            size = int(value[1]) if value else 30
            executable = generated / "uart-test"
            subprocess.run(
                [
                    "cc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-Wno-unused-parameter",
                    "-Wno-unused-function",
                    f"-DTEST_RX_CAPACITY={size}",
                    "-Itests/zmk/studio_uart_stubs",
                    "tests/zmk/studio_uart_test.c",
                    "dependencies/zmk/app/src/studio/msg_framing.c",
                    "-o",
                    str(executable),
                ],
                cwd=ROOT,
                check=True,
                capture_output=True,
            )
            burst = frames[0] * 16 + frames[-1]
            self.assertEqual(len(burst), 512)
            for frame in [*frames, burst]:
                with self.subTest(frame_size=len(frame)):
                    result = subprocess.run(
                        [str(executable)], input=frame, capture_output=True
                    )
                    self.assertEqual(result.returncode, 0, result.stderr.decode())
