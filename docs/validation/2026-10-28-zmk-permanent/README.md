# Permanent wired ZMK validation — 2026-10-28

Both halves independently verified normal A (36576 bytes/CRC32 `1296309606`),
resident CRC32 `1770733249`, normal USB and 61 seconds of advancing native uptime
and feed counters with zero transport/relay retries or reconnects. Firmware
source is `0554dff`; the reviewed manual plan/procedure was bound to `0e0a699`.

`manual-progress.json` is an allowlisted evidence archive with original-report
SHA256 digests, identities, counters, timings and outcomes. Transfer records stay
unverified; separate reports establish installation. It excludes raw HID paths,
USB serials, nonces and backup/keymap/macro/configuration contents.

| Completed evidence | Result |
| --- | --- |
| Fresh prepare | 60.141s native left health |
| Genuine right Vial PROBE | Automatic return, 60.156s recovery feeds |
| Left A→B and shared recovery | Native left 60.297s; ordinary Vial right 60.375s; genuine terminal record lost and right counters reset |
| Right Q | Actual INFO/staging no-feed health by 0.750s within 3s; genuine Vial return 60.141s |
| First permanent right B | B/B readback and 60.984s both native health after confirmed joined-left cycle |
| Distinct right B→A | B/A readback and 61s both native health after confirmed joined-left cycle |
| Final left B→A | Independent A/A readback, 61s both native health and fresh after-health identity/USB check |

The strict `verify-left-A-final` stage was not executed: the user did not confirm
the final cycle. The physical action during that restart is unknown. Independent
read-only evidence establishes installed A/A without claiming automatic restart
or that the entire guarded manual chain completed. Future cycling is a tested
fallback when restart verification fails, not a mandatory action after success.

Final A/A counters were left uptime 71579→132528ms/feed 64186→118603 and right
uptime 72246→133181ms/feed 59347→109441. Final independent evidence digests:

- Identity inspection: `3fd6ed0f724cf34e922c044f5334ac4dad9a8534229bd956d26a46149de96a7b`.
- Health: `55373b53e3e7d4a4e04536b78943e6138b99fa7f15a91e07680605cd32a19f7a`.
- Fresh after-health identity/USB: `12bd80f18c768c8ee0588356366381af3173e81816d0eca40de7a388ee6484a7` at 18:45:28 UTC.

`automatic-failures.json` retains three failed Q observations and four automatic
restart failures as failures. [Workflow decision and review readiness](workflow-decision.md)
records the rationale and limits. Full immutable reports/reviews and native
configuration backups remain private; no post-ZMK settings readback is claimed.
The user reports typing seems functional and Fn+number produces function keys.
Fn+Left initially appeared to jump by words in this input field. A follow-up
user-confirmed Left / Fn+Left / Fn+Right sequence produced Windows Left / Home /
End states. All 61 navigation states lacked Ctrl; separate Ctrl presses were
observed. The bounded observer used only the current-down high bit of
[GetAsyncKeyState](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getasynckeystate)
for navigation/modifier keys and recorded no text. Its source-report SHA256 is
`bb5b414db6a18b7439d913edbc9f8b30a0ab62cb72ac58e473e088e11a9b419c`.
The tested Fn mappings pass at the OS state boundary. This is not device-specific
raw USB evidence, and the field's visual behavior remains unlocalized. No
firmware change followed. Hardware peer-disconnect release remains untested;
matrix-event zero/unavailable supplies no physical-key proof.
