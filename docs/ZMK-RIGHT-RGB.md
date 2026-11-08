# GMK70 split RGB

The universal application enables native RGB underglow and the WB32 LED driver
on both halves. LEFT renders one 77-pixel strip: 36 local and 41 remote pixels.
It sends bounded transaction-11 chunks; RIGHT displays complete CRC-checked
frames outside the UART lock. A local-only LEFT build sends no peer frames.

Use `make build-zmk-wired-studio-updater` for both halves. The smaller
`build-zmk-wired-peripheral` target remains available for compatibility.
Both halves need lighting-capable firmware. Brightness limits and native controls
are described in [Mac layers and lighting](ZMK-MAC-LIGHTING.md).
