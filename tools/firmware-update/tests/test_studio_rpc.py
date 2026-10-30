"""Studio wire vectors and saved-layout transactions using an in-memory peer."""

import json
import stat
import subprocess
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

from studio_rpc import (
    ExpectedLayout,
    JsonSchema,
    JsonValue,
    MatrixPosition,
    PhysicalKey,
    Protobuf,
    ProtocolError,
    RpcBinding,
    RpcKeymap,
    RpcLayer,
    StudioApplyError,
    StudioClient,
    StudioFraming,
    StudioPlan,
    SymbolicBinding,
    SymbolicLayer,
)


class MemoryStudio:
    """A real framed, stateful peer; no hardware or serial backend is imported."""

    ids = (3, 7, 1, 9)
    names = ("Runtime Windows", "Runtime Fn", "Runtime Mac", "Runtime Mac Fn")

    def __init__(self) -> None:
        self.framing = StudioFraming()
        self.incoming = bytearray()
        self.requests: list[bytes] = []
        self.original = RpcKeymap(
            tuple(
                RpcLayer(identifier, name, (RpcBinding(11, 0x70004, 0),) * 72)
                for identifier, name in zip(self.ids, self.names, strict=True)
            ),
            12,
            32,
        )
        self.current = self.original
        self.saved = self.original
        self.dirty = False
        self.fail_binding_number: int | None = None
        self.binding_writes = 0
        self.saves = 0
        self.discards = 0
        self.duplicate_none = False
        self.invalid_battery_metadata = False
        self.wrong_geometry = False
        self.notifications = False
        self.fail_all_bindings = False
        self.fail_save_once = False
        self.corrupt_reload_once = False
        self.packed_behavior_ids = False
        self.include_radio = False
        self.include_flip = False
        self.duplicate_radio = False
        self.radio_name = "GMK70 Bluetooth / Dongle"
        self.invalid_radio_metadata = False

    @staticmethod
    def geometry() -> ExpectedLayout:
        positions = tuple(
            MatrixPosition(row, column) for row in range(8) for column in range(9)
        )
        keys = tuple(
            PhysicalKey(100, 100, index * 100, 0, 0, 0, 0) for index in range(72)
        )
        return ExpectedLayout(positions, keys)

    @staticmethod
    def plan() -> StudioPlan:
        bindings = [SymbolicBinding("none", 0, 0)] * 72
        bindings[0] = SymbolicBinding("mo", 1, 0)
        bindings[1] = SymbolicBinding("gmk_battery", 1, 0)
        bindings[2] = SymbolicBinding("kp", 0x02070004, 0)
        return StudioPlan(
            MemoryStudio.geometry().positions,
            tuple(
                SymbolicLayer(f"Source {index}", tuple(bindings)) for index in range(4)
            ),
            "0" * 64,
        )

    def read(self, size: int) -> bytes:
        count = min(size, 7, len(self.incoming))
        result = bytes(self.incoming[:count])
        del self.incoming[:count]
        return result

    def write(self, data: bytes) -> int:
        count = min(5, len(data))
        for request in self.framing.feed(data[:count]):
            self.requests.append(request)
            response = self.respond(request)
            if self.notifications:
                self.incoming.extend(
                    StudioFraming.encode(bytes.fromhex("12042a020801"))
                )
            self.incoming.extend(StudioFraming.encode(response))
        return count

    @staticmethod
    def binding_bytes(binding: RpcBinding) -> bytes:
        return (
            Protobuf.uint(1, binding.behavior_id * 2)
            + Protobuf.uint(2, binding.param1)
            + Protobuf.uint(3, binding.param2)
        )

    def keymap_bytes(self) -> bytes:
        result = bytearray()
        for layer in self.current.layers:
            layer_bytes = Protobuf.uint(1, layer.layer_id) + Protobuf.blob(
                2, layer.name.encode()
            )
            for binding in layer.bindings:
                layer_bytes += Protobuf.blob(3, self.binding_bytes(binding))
            result.extend(Protobuf.blob(1, layer_bytes))
        result.extend(Protobuf.uint(2, self.current.available_layers))
        result.extend(Protobuf.uint(3, self.current.max_layer_name_length))
        return bytes(result)

    def behavior(self, identifier: int) -> bytes:
        descriptions = {
            11: (
                "Key Press",
                Protobuf.blob(5, Protobuf.uint(1, 0xA4) + Protobuf.uint(2, 0xFFF)),
            ),
            22: ("None", b""),
            33: ("Momentary Layer", Protobuf.blob(6, b"")),
            44: ("GMK70 Battery / Charging", Protobuf.uint(3, 0) + b""),
            55: ("None", b""),
            66: (self.radio_name, b""),
            77: (self.radio_name, b""),
            88: ("GMK70 Function / Media Flip", b""),
        }
        name, rule = descriptions[identifier]
        result = Protobuf.uint(1, identifier) + Protobuf.blob(2, name.encode())
        if identifier in (66, 77):
            rules = b"".join(
                Protobuf.blob(1, Protobuf.uint(3, parameter))
                for parameter in range(6 if self.invalid_radio_metadata else 7)
            )
            return result + Protobuf.blob(3, rules)
        if identifier == 44:
            rules = Protobuf.blob(1, Protobuf.uint(3, 0))
            if not self.invalid_battery_metadata:
                rules += Protobuf.blob(1, Protobuf.uint(3, 1))
            return result + Protobuf.blob(3, rules)
        if rule:
            result += Protobuf.blob(3, Protobuf.blob(1, rule))
        return result

    def physical_bytes(self) -> bytes:
        layout = Protobuf.blob(1, b"GMK70")
        for index, key in enumerate(self.geometry().keys):
            values = (
                key.width,
                key.height,
                key.x,
                key.y,
                key.rotation,
                key.rotation_x,
                key.rotation_y,
            )
            if self.wrong_geometry and index == 5:
                values = (100, 100, 0, 0, 0, 0, 0)
            payload = b"".join(
                Protobuf.uint(field, value * 2) for field, value in enumerate(values, 1)
            )
            layout += Protobuf.blob(2, payload)
        return Protobuf.uint(1, 0) + Protobuf.blob(2, layout)

    def respond(self, request: bytes) -> bytes:
        outer = Protobuf.parse(request)
        request_id = outer.uint(1)
        if outer.has(4):
            behavior_request = Protobuf.parse(outer.blob(4))
            if behavior_request.has(1):
                identifiers: tuple[int, ...] = (
                    (11, 22, 33, 44, 55) if self.duplicate_none else (11, 22, 33, 44)
                )
                if self.include_radio:
                    identifiers += (66, 77) if self.duplicate_radio else (66,)
                if self.include_flip:
                    identifiers += (88,)
                # Upstream nanopb emits unpacked IDs; protobuf also permits packed IDs.
                payload = (
                    Protobuf.blob(
                        1,
                        b"".join(
                            Protobuf.varint(identifier) for identifier in identifiers
                        ),
                    )
                    if self.packed_behavior_ids
                    else b"".join(
                        Protobuf.uint(1, identifier) for identifier in identifiers
                    )
                )
                subsystem = Protobuf.blob(4, Protobuf.blob(1, payload))
            else:
                identifier = Protobuf.parse(behavior_request.blob(2)).uint(1)
                subsystem = Protobuf.blob(
                    4, Protobuf.blob(2, self.behavior(identifier))
                )
        else:
            operation = Protobuf.parse(outer.blob(5))
            if operation.has(1):
                result = Protobuf.blob(1, self.keymap_bytes())
            elif operation.has(2):
                self.binding_writes += 1
                if (
                    self.fail_all_bindings
                    or self.binding_writes == self.fail_binding_number
                ):
                    self.fail_binding_number = None
                    result = Protobuf.uint(2, 3)
                else:
                    update = Protobuf.parse(operation.blob(2))
                    layer_id, position = update.uint(1), update.uint(2)
                    binding = RpcBinding.decode(update.blob(3))
                    layers = list(self.current.layers)
                    index = self.ids.index(layer_id)
                    bindings = list(layers[index].bindings)
                    bindings[position] = binding
                    layers[index] = RpcLayer(
                        layer_id, layers[index].name, tuple(bindings)
                    )
                    self.current = RpcKeymap(tuple(layers), 12, 32)
                    self.dirty = True
                    result = Protobuf.uint(2, 0)
            elif operation.has(3):
                result = Protobuf.uint(3, int(self.dirty))
            elif operation.has(4):
                if self.fail_save_once:
                    self.fail_save_once = False
                    return Protobuf.blob(
                        1,
                        Protobuf.uint(1, request_id)
                        + Protobuf.blob(5, Protobuf.blob(4, Protobuf.uint(2, 3))),
                    )
                self.saved, self.dirty = self.current, False
                self.saves += 1
                result = Protobuf.blob(4, Protobuf.uint(1, 1))
            elif operation.has(5):
                self.current, self.dirty = self.saved, False
                if self.corrupt_reload_once:
                    self.corrupt_reload_once = False
                    layers = list(self.current.layers)
                    first = layers[0]
                    layers[0] = RpcLayer(
                        first.layer_id,
                        first.name,
                        (RpcBinding(11, 0x70005, 0), *first.bindings[1:]),
                    )
                    self.current = RpcKeymap(tuple(layers), 12, 32)
                self.discards += 1
                result = Protobuf.uint(5, 1)
            elif operation.has(6):
                result = Protobuf.blob(6, self.physical_bytes())
            else:
                raise AssertionError("Unexpected Studio request operation")
            subsystem = Protobuf.blob(5, result)
        return Protobuf.blob(1, Protobuf.uint(1, request_id) + subsystem)


class StudioRpcTests(unittest.TestCase):
    def test_flip_sparse_import_changes_one_binding_and_preserves_287(self) -> None:
        peer = self.freshly_edited_peer()
        peer.include_flip = True
        before = peer.current
        plan = MemoryStudio.plan()
        first = plan.layers[0]
        plan = replace(
            plan,
            layers=(
                replace(
                    first,
                    bindings=(SymbolicBinding("gmk_flip", 0, 0), *first.bindings[1:]),
                ),
                *plan.layers[1:],
            ),
        )
        with tempfile.TemporaryDirectory() as folder:
            result = StudioClient(peer).apply(
                plan,
                peer.geometry(),
                Path(folder) / "before.json",
                only_behavior="gmk_flip",
            )
        self.assertTrue(result.saved_and_reloaded)
        self.assertEqual(peer.saved.layers[0].bindings[0], RpcBinding(88, 0, 0))
        self.assertEqual(
            peer.saved.layers[0].bindings[1:], before.layers[0].bindings[1:]
        )
        self.assertEqual(peer.saved.layers[1:], before.layers[1:])
        self.assertEqual(
            tuple((x.layer_id, x.name) for x in peer.saved.layers),
            tuple((x.layer_id, x.name) for x in before.layers),
        )

    def test_flip_schema_rejects_nonzero_or_boolean_parameters(self) -> None:
        for first, second in ((1, 0), (0, 1), (True, 0), (0, True), (-1, 0)):
            with (
                self.subTest(first=first, second=second),
                self.assertRaises(ValueError),
            ):
                SymbolicBinding("gmk_flip", first, second)

    @classmethod
    def eight_radio_plan(cls) -> StudioPlan:
        plan = cls.radio_plan()
        last = plan.layers[3]
        return replace(
            plan,
            layers=(
                *plan.layers[:3],
                replace(
                    last,
                    bindings=(*last.bindings[:71], SymbolicBinding("gmk_radio", 1, 0)),
                ),
            ),
        )

    @staticmethod
    def freshly_edited_peer() -> MemoryStudio:
        peer = MemoryStudio()
        peer.include_radio = True
        peer.notifications = True
        peer.ids = (9, 3, 7, 1)
        reordered = tuple(peer.current.layers[index] for index in (3, 0, 1, 2))
        peer.current = replace(
            peer.current,
            layers=tuple(
                replace(
                    layer,
                    name=f"Fresh Studio edit {index}",
                    bindings=(
                        *layer.bindings[:2],
                        RpcBinding(11, 0x70005 + index, 0),
                        *layer.bindings[3:],
                    ),
                )
                for index, layer in enumerate(reordered)
            ),
        )
        peer.saved = peer.current
        return peer

    @staticmethod
    def radio_plan() -> StudioPlan:
        plan = MemoryStudio.plan()
        first = plan.layers[0]
        bindings = list(first.bindings)
        for index, parameter in enumerate((0, 1, 2, 3, 4, 5, 6), 4):
            bindings[index] = SymbolicBinding("gmk_radio", parameter, 0)
        return replace(
            plan,
            layers=(replace(first, bindings=tuple(bindings)), *plan.layers[1:]),
        )

    @staticmethod
    def plan_document() -> dict[str, JsonValue]:
        plan = MemoryStudio.plan()
        return {
            "format": "gmk70-zmk-studio-symbolic-v1",
            "ready_for_behavior_id_resolution": True,
            "source_sha256": plan.source_sha256,
            "unsupported_policy": "unassigned",
            "positions": [
                [position.row, position.column] for position in plan.positions
            ],
            "layers": [
                {
                    "name": layer.name,
                    "bindings": [
                        {
                            "behavior": binding.behavior,
                            "param1": binding.param1,
                            "param2": binding.param2,
                        }
                        for binding in layer.bindings
                    ],
                }
                for layer in plan.layers
            ],
            "issues": [
                {
                    "category": "wireless",
                    "location": "layers[0].bindings[3]",
                    "disposition": "unassigned",
                }
            ],
        }

    def test_plan_and_exact_build_geometry_are_loaded_without_a_transport(self) -> None:
        expected = MemoryStudio.geometry()
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            plan, keyboard, overlay = (
                root / "plan.json",
                root / "keyboard.json",
                root / "layout.overlay",
            )
            plan.write_text(json.dumps(self.plan_document()), encoding="utf-8")
            keyboard.write_text(
                json.dumps(
                    {
                        "layouts": {
                            "LAYOUT": {
                                "layout": [
                                    {"matrix": [position.row, position.column]}
                                    for position in expected.positions
                                ]
                            }
                        }
                    }
                ),
                encoding="utf-8",
            )
            overlay.write_text(
                "\n".join(
                    f"<&key_physical_attrs {key.width} {key.height} {key.x} {key.y} 0 0 0>"
                    for key in expected.keys
                ),
                encoding="utf-8",
            )
            self.assertEqual(StudioPlan.load(plan), MemoryStudio.plan())
            self.assertEqual(ExpectedLayout.from_files(keyboard, overlay), expected)

    def test_plan_rejects_unready_duplicate_positions_and_tampered_dispositions(
        self,
    ) -> None:
        for case in ("unready", "duplicate", "tampered", "unresolved"):
            document = self.plan_document()
            if case == "unready":
                document["ready_for_behavior_id_resolution"] = False
            elif case == "duplicate":
                positions = JsonSchema.sequence(document["positions"])
                positions[1] = positions[0]
            else:
                issues = JsonSchema.sequence(document["issues"])
                issue = JsonSchema.mapping(issues[0])
                if case == "tampered":
                    issue["location"] = "layers[0].bindings[0]"
                else:
                    issue["disposition"] = "unresolved"
            with tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "plan.json"
                path.write_text(json.dumps(document), encoding="utf-8")
                with self.subTest(case=case), self.assertRaises(ValueError):
                    StudioPlan.load(path)

    def test_framing_matches_pinned_escape_bytes_across_chunks(self) -> None:
        payload = bytes.fromhex("01abacadae")
        self.assertEqual(
            StudioFraming.encode(payload), bytes.fromhex("ab01acabacacacadaead")
        )
        decoder = StudioFraming()
        self.assertEqual(decoder.feed(bytes.fromhex("0000ab01ac")), ())
        self.assertEqual(decoder.feed(bytes.fromhex("abacacacadaead")), (payload,))
        with self.assertRaises(ProtocolError):
            StudioFraming(max_frame=2).feed(bytes.fromhex("ab010203ad"))

    def test_radio_plan_schema_rejects_invalid_parameters(self) -> None:
        invalid: tuple[tuple[JsonValue, JsonValue], ...] = (
            (-1, 0),
            (7, 0),
            (True, 0),
            (1, True),
            (1, 1),
        )
        for first, second in invalid:
            document = self.plan_document()
            layer = JsonSchema.mapping(JsonSchema.sequence(document["layers"])[0])
            JsonSchema.sequence(layer["bindings"])[4] = {
                "behavior": "gmk_radio",
                "param1": first,
                "param2": second,
            }
            with tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "plan.json"
                path.write_text(json.dumps(document), encoding="utf-8")
                with (
                    self.subTest(first=first, second=second),
                    self.assertRaises((ValueError, TypeError)),
                ):
                    StudioPlan.load(path)

    def test_radio_programmatic_bindings_reject_bool_range_and_second_parameter(
        self,
    ) -> None:
        for first, second in ((-1, 0), (7, 0), (True, 0), (1, True), (1, 1)):
            with (
                self.subTest(first=first, second=second),
                self.assertRaises(ValueError),
            ):
                SymbolicBinding("gmk_radio", first, second)

    def test_radio_plan_schema_accepts_every_profile(self) -> None:
        for parameter in range(7):
            document = self.plan_document()
            layer = JsonSchema.mapping(JsonSchema.sequence(document["layers"])[0])
            JsonSchema.sequence(layer["bindings"])[4] = {
                "behavior": "gmk_radio",
                "param1": parameter,
            }
            with tempfile.TemporaryDirectory() as folder:
                path = Path(folder) / "plan.json"
                path.write_text(json.dumps(document), encoding="utf-8")
                loaded = StudioPlan.load(path)
            self.assertEqual(
                loaded.layers[0].bindings[4], SymbolicBinding("gmk_radio", parameter, 0)
            )

    def test_radio_apply_resolves_all_profiles_and_reloads_saved_bindings(self) -> None:
        peer = MemoryStudio()
        peer.include_radio = True
        peer.notifications = True
        with tempfile.TemporaryDirectory() as folder:
            backup = Path(folder) / "before.json"
            result = StudioClient(peer).apply(
                self.radio_plan(), peer.geometry(), backup
            )
            self.assertEqual(result.bindings_written, 288)
            self.assertEqual(
                peer.current.layers[0].bindings[4:11],
                tuple(RpcBinding(66, value, 0) for value in (0, 1, 2, 3, 4, 5, 6)),
            )
            self.assertEqual(peer.current, peer.saved)
            self.assertEqual((peer.saves, peer.discards), (1, 1))
            self.assertEqual(stat.S_IMODE(backup.stat().st_mode), 0o600)

    def test_selective_radio_apply_changes_eight_bindings_and_preserves_fresh_edits(
        self,
    ) -> None:
        peer = self.freshly_edited_peer()
        before = peer.current
        plan = self.eight_radio_plan()
        with tempfile.TemporaryDirectory() as folder:
            backup = Path(folder) / "before.json"
            result = StudioClient(peer).apply(
                plan, peer.geometry(), backup, only_behavior="gmk_radio"
            )
            self.assertEqual(result.bindings_written, 288)
            self.assertTrue(result.saved_and_reloaded)
            changed = 0
            for old, actual, source in zip(
                before.layers, peer.current.layers, plan.layers, strict=True
            ):
                self.assertEqual(
                    (actual.layer_id, actual.name), (old.layer_id, old.name)
                )
                for previous, current, symbolic in zip(
                    old.bindings, actual.bindings, source.bindings, strict=True
                ):
                    if symbolic.behavior == "gmk_radio":
                        self.assertEqual(current, RpcBinding(66, symbolic.param1, 0))
                        changed += int(previous != current)
                    else:
                        self.assertEqual(current, previous)
            self.assertEqual(changed, 8)
            self.assertEqual(peer.current, peer.saved)
            self.assertEqual((peer.saves, peer.discards), (1, 1))
            self.assertEqual(stat.S_IMODE(backup.stat().st_mode), 0o600)
            saved: JsonValue = json.loads(backup.read_text())
            self.assertEqual(JsonSchema.mapping(saved)["keymap"], before.document())

    def test_selective_apply_rejects_unknown_empty_and_absent_filters_before_backup(
        self,
    ) -> None:
        cases = (
            ("", self.eight_radio_plan()),
            ("unknown", self.eight_radio_plan()),
            ("GMK70 Bluetooth / Dongle", self.eight_radio_plan()),
            ("gmk_radio ", self.eight_radio_plan()),
            ("rgb_ug", self.eight_radio_plan()),
            ("gmk_radio", MemoryStudio.plan()),
        )
        for behavior, plan in cases:
            peer = self.freshly_edited_peer()
            before = peer.current
            with (
                tempfile.TemporaryDirectory() as folder,
                self.subTest(behavior=behavior),
            ):
                backup = Path(folder) / "before.json"
                with self.assertRaises(ValueError):
                    StudioClient(peer).apply(
                        plan, peer.geometry(), backup, only_behavior=behavior
                    )
                self.assertFalse(backup.exists())
            self.assertEqual((peer.binding_writes, peer.saves), (0, 0))
            self.assertEqual(peer.current, before)

    def test_selective_apply_partial_failure_restores_all_fresh_bindings(self) -> None:
        peer = self.freshly_edited_peer()
        before = peer.current
        peer.fail_binding_number = 8
        with tempfile.TemporaryDirectory() as folder:
            backup = Path(folder) / "before.json"
            with self.assertRaises(StudioApplyError) as raised:
                StudioClient(peer).apply(
                    self.eight_radio_plan(),
                    peer.geometry(),
                    backup,
                    only_behavior="gmk_radio",
                )
            self.assertTrue(raised.exception.rollback_succeeded)
            saved: JsonValue = json.loads(backup.read_text())
            self.assertEqual(JsonSchema.mapping(saved)["keymap"], before.document())
        self.assertEqual(peer.current, before)
        self.assertEqual(peer.saved, before)
        self.assertNotEqual(before, peer.original)
        self.assertFalse(peer.dirty)

    def test_cli_apply_offers_exact_symbolic_filter_without_opening_transport(
        self,
    ) -> None:
        script = Path(__file__).resolve().parents[1] / "studio_rpc.py"
        command = [sys.executable, str(script), "--port", "unused-fixture", "apply"]
        help_result = subprocess.run(
            [*command, "--help"], capture_output=True, text=True, check=False
        )
        self.assertEqual(help_result.returncode, 0)
        self.assertIn("--only-behavior", help_result.stdout)
        self.assertIn("gmk_radio", help_result.stdout)
        for name in ("plan", "keyboard", "geometry", "backup", "result"):
            command.extend((f"--{name}", "unused-fixture.json"))
        rejected = subprocess.run(
            [*command, "--only-behavior", "GMK70 Bluetooth / Dongle"],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(rejected.returncode, 2)
        self.assertIn("invalid choice", rejected.stderr)

    def test_radio_apply_requires_exact_unique_runtime_name_and_metadata(self) -> None:
        for case in ("missing", "name", "duplicate", "metadata"):
            peer = MemoryStudio()
            peer.include_radio = case != "missing"
            peer.radio_name = (
                "GMK70 Bluetooth / Dongle extra"
                if case == "name"
                else "GMK70 Bluetooth / Dongle"
            )
            peer.duplicate_radio = case == "duplicate"
            peer.invalid_radio_metadata = case == "metadata"
            with (
                tempfile.TemporaryDirectory() as folder,
                self.subTest(case=case),
                self.assertRaises(ValueError),
            ):
                StudioClient(peer).apply(
                    self.radio_plan(), peer.geometry(), Path(folder) / "before.json"
                )
            self.assertEqual(peer.binding_writes, 0)
            self.assertEqual(peer.saves, 0)

    def test_protobuf_vectors_uint_zigzag_and_malformed_bounds(self) -> None:
        self.assertEqual(Protobuf.uint(1, 300), bytes.fromhex("08ac02"))
        self.assertEqual(Protobuf.blob(5, b"\x08\x01"), bytes.fromhex("2a020801"))
        binding = RpcBinding.decode(bytes.fromhex("08011080801c1800"))
        self.assertEqual(binding, RpcBinding(-1, 0x70000, 0))
        for malformed in (
            b"\x00",
            b"\x08\x80",
            b"\x0a\x03\x01",
            b"\x0f",
            b"\x08" + b"\xff" * 10,
        ):
            with self.subTest(malformed=malformed), self.assertRaises(ProtocolError):
                Protobuf.parse(malformed)

    def test_real_request_ids_partial_io_and_notifications_decode_keymap(self) -> None:
        peer = MemoryStudio()
        peer.notifications = True
        client = StudioClient(peer)
        self.assertEqual(client.read_keymap(), peer.original)
        self.assertEqual(peer.requests[0], bytes.fromhex("08012a020801"))
        self.assertEqual(client.get_available_behaviors(), (11, 22, 33, 44))
        self.assertEqual(peer.requests[1], bytes.fromhex("080222020801"))
        details = client.get_behavior_details(44)
        self.assertEqual(details.display_name, "GMK70 Battery / Charging")
        self.assertTrue(details.allows(RpcBinding(44, 1, 0), frozenset(peer.ids)))
        self.assertFalse(details.allows(RpcBinding(44, 2, 0), frozenset(peer.ids)))

    def test_apply_resolves_runtime_ids_preserves_names_and_reloads_saved_layout(
        self,
    ) -> None:
        peer = MemoryStudio()
        with tempfile.TemporaryDirectory() as folder:
            backup = Path(folder) / "before.json"
            result = StudioClient(peer).apply(
                MemoryStudio.plan(), peer.geometry(), backup
            )
            self.assertEqual(result.bindings_written, 288)
            self.assertEqual(peer.current.layers[0].bindings[0], RpcBinding(33, 7, 0))
            self.assertEqual(peer.current.layers[0].bindings[1], RpcBinding(44, 1, 0))
            self.assertEqual(
                tuple(layer.name for layer in peer.current.layers), peer.names
            )
            self.assertEqual(
                tuple(layer.layer_id for layer in peer.current.layers), peer.ids
            )
            self.assertEqual(peer.current, peer.saved)
            self.assertEqual((peer.saves, peer.discards), (1, 1))
            self.assertEqual(stat.S_IMODE(backup.stat().st_mode), 0o600)
            saved: JsonValue = json.loads(backup.read_text())
            keymap = JsonSchema.mapping(JsonSchema.mapping(saved)["keymap"])
            layers = JsonSchema.sequence(keymap["layers"])
            bindings = JsonSchema.sequence(JsonSchema.mapping(layers[0])["bindings"])
            self.assertEqual(JsonSchema.mapping(bindings[0])["behavior_id"], 11)

    def test_apply_rejects_dirty_duplicate_names_bad_metadata_and_geometry_before_writes(
        self,
    ) -> None:
        for attribute in (
            "dirty",
            "duplicate_none",
            "invalid_battery_metadata",
            "wrong_geometry",
        ):
            peer = MemoryStudio()
            setattr(peer, attribute, True)
            with (
                tempfile.TemporaryDirectory() as folder,
                self.subTest(attribute=attribute),
                self.assertRaises((ValueError, ProtocolError)),
            ):
                StudioClient(peer).apply(
                    MemoryStudio.plan(),
                    peer.geometry(),
                    Path(folder) / "before.json",
                )
            self.assertEqual(peer.binding_writes, 0)
            self.assertEqual(peer.saves, 0)

    def test_mid_apply_failure_restores_fresh_map_and_saves_it(self) -> None:
        peer = MemoryStudio()
        peer.fail_binding_number = 5
        with tempfile.TemporaryDirectory() as folder:
            with self.assertRaises(StudioApplyError) as raised:
                StudioClient(peer).apply(
                    MemoryStudio.plan(), peer.geometry(), Path(folder) / "before.json"
                )
            self.assertTrue(raised.exception.rollback_succeeded)
        self.assertEqual(peer.current, peer.original)
        self.assertEqual(peer.saved, peer.original)
        self.assertFalse(peer.dirty)

    def test_save_failure_and_reload_corruption_both_restore_fresh_map(self) -> None:
        for fail_save in (False, True):
            peer = MemoryStudio()
            peer.fail_save_once = fail_save
            peer.corrupt_reload_once = not fail_save
            with tempfile.TemporaryDirectory() as folder:
                with self.assertRaises(StudioApplyError) as raised:
                    StudioClient(peer).apply(
                        MemoryStudio.plan(),
                        peer.geometry(),
                        Path(folder) / "before.json",
                    )
                self.assertTrue(raised.exception.rollback_succeeded)
            self.assertEqual(peer.current, peer.original)
            self.assertEqual(peer.saved, peer.original)

    def test_failed_recovery_is_reported_and_backup_remains_private(self) -> None:
        peer = MemoryStudio()
        peer.fail_all_bindings = True
        with tempfile.TemporaryDirectory() as folder:
            backup = Path(folder) / "before.json"
            with self.assertRaises(StudioApplyError) as raised:
                StudioClient(peer).apply(MemoryStudio.plan(), peer.geometry(), backup)
            self.assertFalse(raised.exception.rollback_succeeded)
            self.assertTrue(backup.exists())
            self.assertEqual(stat.S_IMODE(backup.stat().st_mode), 0o600)

    def test_preexisting_backup_blocks_all_device_requests(self) -> None:
        peer = MemoryStudio()
        with tempfile.TemporaryDirectory() as folder:
            backup = Path(folder) / "before.json"
            backup.write_text("must remain untouched", encoding="utf-8")
            with self.assertRaises(FileExistsError):
                StudioClient(peer).apply(MemoryStudio.plan(), peer.geometry(), backup)
            self.assertEqual(backup.read_text(), "must remain untouched")
        self.assertEqual(peer.requests, [])

    def test_programmatic_parameter_overflow_is_rejected_before_mutation(self) -> None:
        peer = MemoryStudio()
        plan = MemoryStudio.plan()
        layers = list(plan.layers)
        first = layers[0]
        bindings = list(first.bindings)
        bindings[0] = SymbolicBinding("kp", 0x100070004, 0)
        layers[0] = SymbolicLayer(first.name, tuple(bindings))
        invalid = StudioPlan(plan.positions, tuple(layers), plan.source_sha256)
        with (
            tempfile.TemporaryDirectory() as folder,
            self.assertRaises(ValueError),
        ):
            StudioClient(peer).apply(
                invalid, peer.geometry(), Path(folder) / "before.json"
            )
        self.assertEqual(peer.binding_writes, 0)

    def test_packed_behavior_ids_are_accepted_and_missing_response_times_out(
        self,
    ) -> None:
        peer = MemoryStudio()
        peer.packed_behavior_ids = True
        self.assertEqual(StudioClient(peer).get_available_behaviors(), (11, 22, 33, 44))

        class EmptyTransport:
            def read(self, size: int) -> bytes:
                return b""

            def write(self, data: bytes) -> int:
                return len(data)

        with self.assertRaises(TimeoutError):
            StudioClient(EmptyTransport(), timeout=0.001).read_keymap()

    def test_conflicting_response_oneof_is_rejected(self) -> None:
        peer = MemoryStudio()

        class ConflictingTransport:
            def read(self, size: int) -> bytes:
                return peer.read(size)

            def write(self, data: bytes) -> int:
                count = peer.write(data)
                if peer.incoming and peer.incoming[-1] == 0xAD:
                    peer.incoming[-1:] = b"\x12\x00\xad"
                return count

        with self.assertRaises(ProtocolError):
            StudioClient(ConflictingTransport()).read_keymap()


if __name__ == "__main__":
    unittest.main()
