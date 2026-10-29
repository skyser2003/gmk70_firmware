"""Studio wire vectors and saved-layout transactions using an in-memory peer."""

import json
import stat
import tempfile
import unittest
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
        }
        name, rule = descriptions[identifier]
        result = Protobuf.uint(1, identifier) + Protobuf.blob(2, name.encode())
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
                identifiers = (
                    (11, 22, 33, 44, 55) if self.duplicate_none else (11, 22, 33, 44)
                )
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
