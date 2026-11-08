# Custom behavior acceptance checklist

This checklist covers repository-added behavior and its native regression
coverage. Hardware acceptance remains separate.

| Behavior family | Required integration cases | Evidence / status |
| --- | --- | --- |
| Function/media flip | Tables; press-time flip/OS context; unrelated and overlapping modifiers; duplicate sources; release across mode/native-clear transitions | Shared QMK native report/adapter tests run against both pinned forks. `run_function_flip_native_tests.sh` executes pinned ZMK HID/listener with radio on/off and both modifier-release settings, including nested/threaded clear and balancing-release cases. |
| NKRO controls | Fresh default on; persisted off; explicit/toggle actions; held keys/modifiers; native clear; 6KRO/NKRO/boot output; radio overflow | `test_nkro.py`, `run_nkro_tests.sh`, actual radio/native-report lifecycle and native flip/projection fixtures. ZMK toggles preserve native held state; VIA/Vial toggles release/discard held reports. Genuine connection loss follows the separate discard policy. |
| OS and shortcut adapters | Windows/Mac mapping; literal custom shortcuts; aliases; saved bindings; intentional reboot semantics | Stock keycode/manual-Vial fixtures, `test_stock_v15.py`, `test_os_keymap.py`, and flip mapping tables. ZMK persists OS selection through its native controls settings. |
| Knob rotation | Both directions/order; layers/remap; idle IRQs; short pulse/release; overflow; disable/mode cancellation | Native `encoder_test.c` and wired scanner tests; `test_studio_layout.py` and settings-store append/reload tests. Rotation generates taps; it cannot establish a physical three-second hold. |
| Battery holds | Multiple physical bindings; remap/release; level+charging together; stale/invalid input; RGB-off visibility and release restoration | Shared `battery_holds_test.c`, stock battery/hardware fixtures in both forks; ZMK battery view/status/UART, split lighting and pixels tests. |
| RGB power and split frames | Dark initialization; wake guard; held refresh; successful latch/shutdown; failed DMA; peer replay/rejoin; native settings preserved | `pixels_test.c` executes the real MMIO driver for both roles; `lighting_battery_test.c` uses the real split path. Held frames refresh every 50 ms; final release stops refresh and restores darkness. |
| RGB runtime saves | Native format; debounce; wireless allowlist; wired gates for other writes; busy/failure retry; latest value; no blocking queue | `run_rgb_settings_tests.sh` compiles pinned native RGB separately, invokes its delayed callback, writes through the real backend/store and reloads native bytes. Covers in-flight replacement, execution-time USB loss and startup behavior-ID writes. |
| Radio report lifecycle | Outgoing host releases; disconnect/reconnect; pairing/reject; stale basic/modifier/consumer/flip state; same-channel wake | `test_radio_lifecycle.py`: 60 scenarios across both pinned forks and 6KRO/NKRO, with actual module RX, board gate, native action/report/host caches and transport. ZMK core/kernel/endpoint tests cover reserved releases and native clear. |
| Physical host and pairing ownership | Mode gates including invalid switch input; physical owner; duplicate bindings; synthetic events; remap/reset/reload; queued deadline cancellation | `test_pairing.py` executes native QMK/Vial deferred timers/storage. Native ZMK keymap/kernel fixture executes setter, Discard, Reset and move/add/remove/restore before/during deadlines. Both-low hardware input is rejected regardless of previous logical host. |
| Radio parser and queue | Invalid/raw-tail quarantine; timeout/fault; coherent NKRO pair; ordered host controls; overflow/recovery | Shared module, radio keys, wireless and host-switch tests; ZMK radio core/control/kernel and battery-UART tests. Deterministic framing/order assertions do not establish RF delivery. |
| Awake idle scanning | Both pinouts; first edge/debounce; held keys; arming race; EXTI fallback; peer maintenance; STOP ownership recovery | `run_matrix_idle_tests.sh` executes pinned matrix/debounce/split code for both forks, plus shared low-power/wake fixtures. ZMK wired scanner tests exercise IRQ arming, fallback and first-edge behavior. |
| Updater and settings ownership | Admission/execution gates; Vial native authorization; idle lease; no repeated INSTALL; flash bounds; storage exclusion; no-watchdog invariant | Shared native update/trial/auth fixtures and ZMK service, receiver, backend/store and platform tests; image checks enforce flash/RAM bounds, role contract and both retained exclusions.  |

Native framework feature sets, effect catalogs and restored stock functions that
were never implemented are outside this repository-addition checklist. It does
not promise every upstream feature or change intentional settings policies.

Runtime RGB saves keep their native 60-second debounce and latest pending value.
Ineligible/busy saves retry when allowed, at most once per second. Power loss can
lose unsaved RAM state. A latched backend storage fault retains its existing
fail-closed policy; the adapter does not reformat or remount storage. The native
Reset fixture makes the pinned implementation's pre-existing scratch bitmap
deterministic by enumerating all fixture positions; it does not establish sparse
settings correctness in that upstream function.

Host tests cannot establish RF delivery, electrical power behavior, silicon
interrupt timing or runtime stack watermarks. See
[hardware acceptance](HARDWARE-VALIDATION.md) for physical checks.
