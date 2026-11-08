# Hardware validation

The binaries are build candidates. Compilation, host tests, and ELF inspection do not validate the physical radio, matrix wiring, charging signals, or low-power timing. Build and test commands do not flash a device.

Record PCB revision, stock keyboard version, receiver version, host OS, and the candidate SHA-256 before a device test. Preserve the known-good original image. The external radio and receiver retain their existing firmware; this build only targets the WB32 keyboard MCU. Similarity to other keyboards is not proof that firmware is interchangeable across revisions.

## Acceptance tests

| Area | Required result |
| --- | --- |
| USB enumeration | VID 342D, PID E491; reconnect works |
| Matrix | Every key on both halves, including both B keys and both spaces |
| Encoder | Volume down/up and mute; both Windows and Mac layers |
| Layers | Fn, Mac/Windows switching, media/function flip |
| Rollover | Modifiers and simultaneous keys; releasing in different orders leaves no stuck keys |
| VIA | Load provided JSON; change keys on all four layers; survive unplug/replug |
| RGB | On/off, brightness, effects, recording channels, indicators on both halves |
| Bluetooth | Pair all three slots with Fn+Q/W/E held for 3 seconds; reconnect and switch slots |
| 2.4 GHz | Pair with Fn+R held for 3 seconds; type through the existing receiver |
| Mode switch | USB/BT/2.4 GHz transitions while keys are held; no stale reports |
| Radio raw HID | VIA through receiver if supported by its firmware; not assumed over Bluetooth |
| Wired idle | More than 5 minutes idle, then first key from either half works |
| Wireless idle | 1 minute idle, then wake separately from each half; no lost/stuck keys |
| Long idle | Radio's 30-minute sleep and host suspend/resume |
| Battery/charging | Plausible level, low-battery indication, charging transitions |
| Recovery | Existing WB32 DFU recovery remains accessible |

The original port reports a right-half idle problem. This fork fixes specific source defects in that path, including USB slave timeout, split pin shutdown before RPC completion, missing split-RX wake event, wrong sleep column pins on the right, an extra five zero-initialized row pins, and a pending-interrupt bitmask typo. Wake behavior on both halves still requires physical testing; it is not established by these changes.
