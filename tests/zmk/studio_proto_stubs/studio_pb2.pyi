"""Narrow handwritten types for the real, temporary generated Studio messages.

Only fields exercised by test_studio_uart are declared here. Runtime imports
still load protoc output generated from the pinned upstream schemas.
"""

class BehaviorBinding:
    behavior_id: int
    param1: int
    param2: int

class SetLayerBindingRequest:
    layer_id: int
    key_position: int
    binding: BehaviorBinding

class SetLayerPropsRequest:
    layer_id: int
    name: str

class KeymapRequest:
    set_layer_binding: SetLayerBindingRequest
    set_layer_props: SetLayerPropsRequest

class Request:
    request_id: int
    keymap: KeymapRequest
    def __init__(self, *, request_id: int = ...) -> None: ...
    def SerializeToString(self) -> bytes: ...
