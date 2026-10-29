# GMK70 warm-restart analysis — 2026-10-28

## Finding and scope

**The failure is not localized and no fix is established.** The strongest new
offline result is a quantitative constraint on a specific watchdog hypothesis:
the preserved resident needs at least **0.709506 seconds at nominal 8 MHz** for
the two full CRC passes of the installed 61,696-byte Studio image, before page
comparison, programming or other overhead. If a running IWDG crosses software
reset while its prescaler returns to the documented default, its nominal
512-millisecond deadline cannot accommodate that path. The required mixed
retention behavior is **unproven**, and part of the reference manual weighs
against it. This is a falsifiable candidate, not a discovered watchdog defect.

This analysis used systematic debugging: compare the observed failing path with
successful paths, inspect actual linked instructions and separate facts from
inference. It was conducted offline against baseline `d27ab64` on
`codex/zmk-trial-launcher`. The only output written was this document. No build,
test suite, dependency setup, device-backend import, keyboard query, firmware
installation, reset, power cycle or commit was performed. This analysis did not
change resident, application, settings, radio or other maintained sources;
concurrent work in the shared checkout was left alone.

Current operation remains the qualified Studio/permanent configuration in
[ZMK Studio](ZMK-STUDIO.md), [permanent wired ZMK](ZMK-PERMANENT-WIRED.md) and the
[Studio validation record](validation/2026-10-28-zmk-studio/README.md).

## Recorded observations and their limits

| Recorded fact | What it establishes | What it does not establish |
| --- | --- | --- |
| Each of the two Studio uploads completed SEAL, then one INSTALL returned result 0, committed state 3, expected size and CRC. | The running sender accepted and wrote the installation manifest through its actual flash/readback path. This is stronger than the earlier verifier-timeout reports. | Entry into `sys_reboot`, execution of AIRCR, completion of resident copying or entry into the new application. |
| The validation record reports no returning USB device after 80 seconds; preserved timeout USB captures are empty. | The expected host interface did not return in that observation. | MCU PC, clock state, watchdog behavior, absence of every possible unknown/problem USB node, or whether application code was running without USB. |
| Each joined-left power cycle was followed by the expected new LEFT identity and healthy operation. | The requested image was installed by that later readback. | Whether installation completed before the power cycle or was completed/retried by the resident during it. |
| Final LEFT: 61,696 bytes, CRC32 `1010245074`, SHA256 `1aa746786927b03d3a19e2a13aeda33f50ef4a72b3b62c336c5256907df88d51`. RIGHT remains 36,576 bytes, CRC32 `1296309606`. Both resident CRCs are `1770733249`. | The documented final firmware identities, with unchanged residents and no Studio RIGHT flash. | An automatic-restart success. |
| Final both-half health advanced for 61 seconds with zero transport/relay retries or reconnects; Studio edits survived the distinct-image update and were restored. | Healthy post-cycle runtime and actual settings persistence. | A diagnosis of the preceding warm outage. |
| Earlier staging watchdog, return, hang and actual ZMK trials returned to Vial automatically. | Those bounded launch/return paths worked on the tested hardware. | Resident installation with a live commit: the successful trial-return path normally has an erased commit and skips full-image installation CRC/copy work. |

The installed-image SHA256 was checked against the frozen local binary.
Sanitized current facts are in
[result.json](validation/2026-10-28-zmk-studio/result.json). Private first/persistence
install logs, timeout captures and frozen inputs remain under
`artifacts/recovery/2026-10-28-studio/operator-and-evidence/`; this report does not
republish sessions, USB paths, configuration contents or RPC payloads.

The permanent investigation already tried a 50-ms native D+ disconnect interval
and still failed automatic restart. The missing B1 pull-up power initialization
was subsequently repaired from board-source evidence, but that experiment also
failed automatic restart. Both outcomes are recorded in the
[permanent implementation plan](superpowers/plans/2026-10-27-zmk-permanent.md).
Neither change is evidence that the next unobserved boot reached its initializer.
The old static-thread lifecycle defect is repaired and post-cycle health proves
the current worker runs; it must not be presented again as a new diagnosis.

## Actual reset and resident paths

The permanent sender records committed state, waits at least 300 ms in
`firmware/zmk/update/receiver.c:165`, then calls
`gmk_zmk_update_reset()` in `update/service.c:39`. That calls
`sys_reboot(SYS_REBOOT_COLD)`. The frozen final Studio ELF contains:

| Symbol / address | Verified instruction behavior |
| --- | --- |
| `sys_reboot`, `0x0800be0e` | Raise BASEPRI, disable SysTick through `sys_clock_disable`, call `sys_arch_reboot`. |
| `sys_clock_disable`, `0x0800c5dc` | Clear bit 0 of SysTick CTRL at `0xe000e010`; no oscillator or USB operation. |
| Weak `sys_arch_reboot`, `0x08006734` | DSB; AIRCR write `0x05fa0004 \| (oldAIRCR & 0x700)`; DSB; spin. |
| Receiver vector at `0x08002004` | Thumb reset entry `0x08006709`; MSP `0x20005b58`. |

The pinned Zephyr `arch/arm/core/cortex_m/scb.c:38` explicitly ignores the reboot
type. **COLD and WARM emit the same hardware request in this port.** Changing the
enum cannot expand its reset domain. QMK's pinned
`platforms/chibios/bootloaders/wb32_dfu.c` uses the same CMSIS
`NVIC_SystemReset()` for ordinary MCU reset. No bootloader-magic write or deliberate
USB detach is part of the native updater reset path.

The resident source is
`firmware/qmk-common/keyboards/zuoya/gmk70/update/{boot.c,image.c,wb32_flash.c}`.
Its actual ELF confirms this order:

1. `gmk_boot_reset` (`0x080000a4`) masks IRQs and checks the bootloader magic.
2. `gmk_install_pending` (`0x080002ac`) decodes the commit manifest. A valid commit
   requires complete staging-vector/CRC validation before any application write.
3. Compare every application page with staging and program differing pages.
4. Validate the complete installed image CRC, then clear the commit page.
5. Check application vectors, set VTOR to `0x08002000`, execute DSB/ISB and load
   the application's MSP/reset vector through `jump_vector` (`0x08000040`).

There is **no full QMK/ChibiOS clock initialization, watchdog feed or trace** in
that path. The direct NVIC reset in `boot.c` belongs to `fault_recovery`
(`0x08000070`), which sets bootloader magic; ordinary successful installation
branches directly to the application without an additional reset. This distinction
matters: resident flash/calibration side effects are the receiver's starting
state even when AIRCR itself correctly reset the system beforehand.

The inspected resident ELF reconstructs the existing 2,120-byte binary exactly.
Its binary SHA256 is
`94427de2fad50bb91337ded4341df02cab9070c550042a9832b3e12366791bf8`.
Padding with FF to the resident's 8 KiB produces the installed contract:
CRC32 `1770733249`, SHA256
`be33f246624cbb2e7a02c6617dc5c3284ee4783e2e3ae3860b62a7dc6182462a`.
The frozen Studio package contains the same resident binary. No resident rebuild
or modification is needed to explain or test the software model.

## Reset domains: documented expectations versus unknown silicon state

The cached primary reference is Westberry **EN_RM2905025_WB32FQ95xx_V01**, also
linked from [the execution contract](ZMK-TRIAL-EXECUTION.md). Its inspected text
and PDF are preserved privately at
`.work/zmk-permanent-continuation/wb32-reference.{txt,pdf}`.

| Domain | Reference-manual evidence | Consequence for this investigation |
| --- | --- | --- |
| System reset | §7.1.1 p71 identifies SYSRESETREQ as software/system reset; its general rule resets registers except RCC reset flags and the battery domain. | Native AIRCR requests a platform system reset. It is not merely a scheduler restart. |
| Reset clocks | §7.2.9 p75 says MAINCLK is MHSI 8 MHz after reset. §11.4.2–5 pp116–117 gives MHSI and FHSI enable/ready defaults of 1; HSE and PLL enable/ready defaults are 0. | The documented resident execution clock is 8 MHz. Assuming a reset starts with FHSI disabled contradicts these defaults. |
| POR-only exceptions | PWR status/general-purpose/configuration registers (§5.4.5–8 pp63–64), DBGMCU_CR (§27.4.2 p387), FMC_ADDR/FMC_DATA1 (§20.4.2–3 p263) have explicit power-reset-only language. | The general rule is not a complete per-register retention specification. These exceptions do not establish that all analog, cache or USB state survives. |
| Watchdog | §18 pp255–257 gives PR default 0 (/4), RLR default 4095 and nominal 512 ms at 32 kHz. §7.2.12 p76 forces LSI on while IWDG is enabled. Conversely p56 says a started IWDG cannot stop **except by system reset**. | A surviving enable latch combined with reset-default PR/RLR is possible only as an unverified silicon/retention condition; it is not established by the manual. |
| Flash accelerator | CACHE_CR default `0x03000000` (§2.6 pp35–36); no POR-only qualification there. FMC self-calibrates at power-on (§20.2 p261), but instruction-tag coherence after programming is not specified in the inspected sections. | A stale-cache explanation is unproven; unchanged CACHE_CR after reset is also the documented enabled default. A data CRC cannot by itself prove instruction-fetch coherence. |
| GPIO / external USB | GPIO reset configuration includes PA12 pull-up and PA11 pull-down (§8.3.1 p91). B1 controls the board's external D+ pull-up supply in maintained board sources. | Host-visible detach is an electrical/host boundary; a software peripheral reset is not proof of a measured D+ low interval. The prior failed 50-ms experiment must still be respected. |

Successful qualified trial recovery services a *possibly* inherited watchdog,
without issuing a new start key. Prior unserviced runtime watchdog recovery
briefly returned and then lost USB; revised servicing was followed by stable
recovery. This is supporting circumstantial evidence for investigating watchdog
retention. It is not a controlled measurement of which IWDG state survived:
health/feed counters count software calls, not a hardware running-status bit.
The documents correctly retain that uncertainty.

## Clock and flash-state comparison with working vendor startup

Pinned vendor-authored WB32FQ95xx `hal_lld.c` is the reference clock sequence;
it is not a disassembly of proprietary stock V15. The QMK and Vial dependency
revisions are independently pinned in `dependencies/sources.lock.json`.

| Boundary | Vendor ChibiOS path | Maintained native path | Assessment |
| --- | --- | --- | --- |
| Starting assumptions | `SetSysClock` explicitly says to call only after reset. | Supports cold resident entry and a Vial-prepared staging launch. Explicitly enables/waits MHSI, selects it, then disables PLL. | A concrete additional native sequence, not evidence it fails. |
| Peripheral bus clocks | Establish APB1 source, later AHB/APB2 divisors; enable GPIO bus before HSE pins. | Establish AHB/APB1/APB2 first, then enable GPIOD/bus matrix. | The earlier APB/GPIO ordering defect is already corrected. |
| HSE | PD0/PD1 analog, CFGMSK `0xfffc`, HSECR1 2, HSECR0 1. A bounded poll ultimately enters an infinite failure loop. | Same pin/control values; unbounded HSE-ready loop. | No missing cold-HSE configuration found. Both paths can stop permanently if HSE never becomes ready. |
| Flash at 96 MHz | CACHE_CR `0x03000013`. | Same value before selecting PLL. | No native-versus-vendor missing cache-disable/invalidate sequence found. |
| PLL | HSE /2 ×16 =96 MHz, wait PLLSR==3, select PLL. | Same values and final source. | No wrong divider/multiplier found. |
| USB | Core reset, FIFO/PHY setup and PLL/2 USB clock; QMK USB startup disconnects/stops for 50 ms. | Equivalent core/PHY/clock/pin setup in `wb32_usb_clock_enable`; ordinary attach has no defined initial disconnect interval. B1 is initialized before USB. | The timing difference is real, but the isolated 50-ms attempt already failed. It cannot be promoted to a fix. |

Actual final Studio `soc_early_init_hook` is 224 bytes at `0x08004df0`, SHA256
`f3cb8f75409f73b23fa65899240a879f8196fa206d909e8b438b6bc094fc4cd1`;
`wb32_usb_clock_enable` is 128 bytes at `0x08004ed0`, SHA256
`af1afb24f62e344d1c42fa89e29590317263d8881ae42bc047462bd56444d3e7`.
Those match the previously audited A/Q functions. MHSI, HSE and PLL polling
instructions are at `0x08004e00`, `0x08004e5a` and `0x08004e82`, respectively.
All precede PRE_KERNEL watchdog initialization. A finite readiness failure in
the later watchdog initializer deliberately permits read-only startup; it must
be distinguished from a fault/stall before reaching it.

Resident vendor PRE_OP was also decoded as **Thumb instructions**, rather than
interpreting the instruction-word array as data or ARM code. It toggles FHSI
off/on with fixed 1,000-iteration delays, enables APB2 source, enables RNG/bus
gates, reads calibration data and can adjust FHSI trim at `0x40010434`. The
resident adapter restores the prior FHSI enable and locks analog access, but does
not restore those APB2/RNG gates or the calibration trim. The native flash
adapter additionally restores APB2/PCLK/RNG gates. Neither PRE_OP nor the resident
contains an explicit instruction-cache invalidation.

These are concrete inherited-state differences. They are not newly found wrong
register writes: they are the preserved vendor sequence, both normal firmware
and Studio successfully use flash programming, and a cold boot with a pending
commit also executes the resident sequence. There is no basis here to edit the
resident, vendor instruction words, trim, option bytes or cache-reset controls.

## New quantitative constraints

### The small Studio update still performs full CRC work

The first Studio binary is 61,696 bytes, CRC32 `551392334`, SHA256
`8e24ae7248a4a936404ded6902f9d14182aa7e4365baf1e3d0dcc3ca6a7f0666`.
The final binary has the same length and differs in **352 bytes across 17 of 241
256-byte pages**. There is no partial-page padding ambiguity because this image
length is page-aligned. Assuming actual flash matched the frozen first binary,
the resident would skip 224 matching pages during the second install. The
commit-page clear remains an additional flash operation.

This weakens a simple explanation based on programming every application page
or the nominal 32.768-second runtime watchdog period. It does not prove the
resident ever reached the comparison loop, and it does not reduce either full
CRC pass. Even a completely identical-image installation would still perform
both full-image CRCs and commit-page clearing.

### Exact CRC instruction count and a conditional short deadline

The actual resident `gmk_crc32` occupies `0x08000114..0x08000143`.
Its loop executes, for each byte:

- Two outer-loop instructions (`cmp`, `bne`).
- Three byte/setup instructions (`ldrb`, `eors`, `movs`).
- Eight iterations of five instructions (`sbfx`, `ands`, `subs`, `eor`, `bne`).
- One back branch.

For positive size N this is **46N+8 executed instructions per CRC**, including
entry/exit. The following uses an intentionally optimistic one core cycle per
listed instruction and nominal reset MHSI 8 MHz. Loads, taken branches, bus/flash
arbitration and programming can only make this model slower; these are not
measured silicon timings.

| Payload | Instructions for two full CRCs | Optimistic CRC-only time at 8 MHz |
| --- | ---: | ---: |
| Permanent A, 36,576 bytes | 3,365,008 | 0.420626 s |
| Studio, 61,696 bytes | 5,676,048 | **0.709506 s** |
| Historical right trial updater, 95,908 bytes | 8,823,552 | 1.102944 s |

Under a **hypothetical** surviving IWDG with PR reset to 0, RLR reset to 4095,
and nominal LSI 32 kHz, the Studio install cannot complete before the nominal
0.512-second deadline even with zero page replacements. A reset partway through
copying might make incremental page progress, but both CRCs must still occur in
one successful pass before the commit can be cleared. Repeated resets could
therefore prevent application entry indefinitely. Removing power could stop
that watchdog condition and allow completion later.

This matches several observations and yields a strong test predicate. It still
depends on unknowns: retention of the running latch, reset behavior of PR/RLR
and clock gates, actual oscillator rates and whether reset entry occurred. A
retained PR6/RLR4095 deadline is approximately 32.768 seconds, for which this
CRC count alone provides no evidence of an overrun. If system reset stops IWDG
as p56 says, the short-deadline mechanism is false. Successful trial returns
are compatible with either case because their no-commit resident path is much
shorter and recovery servicing happens before normal CRT/clock setup.

## Ranked hypotheses

Ranking indicates diagnostic priority and fit to current evidence, not a
probability estimate or permission to change firmware.

| Rank / hypothesis | Supporting evidence | Counterevidence / missing fact | Falsifiable discriminator |
| --- | --- | --- | --- |
| 1. Watchdog-dependent reset/installation loop before new application entry, especially mixed enable/config retention. | Pre-start software fallback worked; historical watchdog recovery was unstable until servicing was added; permanent update resets while a watchdog was started; power cycling recovers; the Studio CRC path cannot fit a hypothetical default 512-ms deadline. | Manual p56 says system reset can stop IWDG. No running-latch/config/reset-period measurement exists. Feed-call counters do not settle retention. Nominal 32-second expiry is not supported by measured resident elapsed time. | A non-keyboard silicon fixture or authoritative vendor clarification shows whether enable, PR/RLR and gates survive each reset type. A verified stopped watchdog at resident entry refutes this mechanism. A repeated ~short-period IWDG reset with a live commit supports it. |
| 2. Receiver stalls/faults in early clock/flash state before native watchdog init. | Three unbounded clock waits precede watchdog setup. Staging enters from a prepared Vial state; permanent entry follows actual resident flash operations. POR changes calibration and reset domains together. | Startup/USB machine code matches successful Q and cold-started permanent images; clock values agree with vendor sources. No failing PC/status was captured. A pending cold commit can run the same PRE_OP. | A last checkpoint/PC in MHSI/HSE/PLL wait with a corresponding never-ready status selects a concrete boundary; reaching watchdog/USB startup refutes that specific earlier stall. |
| 3. Firmware runs but USB electrical/PHY/host re-enumeration fails. | No explicit native pre-reset detach; external pull-up and host state differ across a real cycle. Host absence alone cannot prove CPU failure. | Both isolated startup-delay and B1 experiments still failed automatic restart. Native attach resets USB core/flags and cold operation is stable. | Independent advancing firmware liveness plus USB attach/configuration state during the outage, combined with an electrical USB trace, separates CPU health from bus enumeration. Repeating another delay alone provides poor localization. |
| 4. FMC/cache/calibration state causes resident stall or instruction-side failure after overwrite. | Resident executes from flash while replacing application pages; PRE_OP changes calibration; reset versus POR behavior is incompletely specified. Data readback CRC and DSB/ISB do not prove instruction-tag coherence. | Only two 128-bit cache slots are documented; resident loops/copy/CRC heavily access flash. Vendor and native use identical cache settings without a clear. No explicit mandatory omitted invalidate operation was found. | A controlled memory/flash model can expose software ordering assumptions but cannot establish silicon coherence. Independent PC/manifest/data/instruction evidence or a vendor coherence contract is needed. |
| 5. Commit accepted but sender never reaches the reset request, or a later worker fault causes the outage. | ACK precedes the 300-ms deferred reset; no hardware observation covers that boundary. | Healthy pre-update worker operation and repeatable USB disappearance fit actual reboot better than a perpetual healthy sender. A watchdog should eventually act if it remains functional. | Sender reset-request checkpoint plus receiver reset-entry checkpoint; a last PC in the sender selects this class. |

Malformed COLD/WARM selection, wrong installed vectors, missing current B1
initialization, the repaired worker lifecycle, or a simple inherited PRIMASK
are not supported as fresh explanations. The successful staging path also
hands off with PRIMASK set; the audited Zephyr switch-to-main path clears it.

## Safe next work: model and instrument without operating this keyboard

The useful next result is **a boundary observation**, not another combined
clock/cache/watchdog patch. The following can be prepared entirely offline:

1. **Reset-domain model with explicit alternatives.** Link the unchanged resident
   image logic to host RAM/flash and a virtual monotonic instruction budget.
   Model watchdog stopped, retained /256, and retained-enable/default-/4 as
   separate assumptions. Exercise erased/live/corrupt commits, identical pages,
   the actual 17-page Studio delta and reset during each page/CRC/commit phase.
   Assert which assumptions permit application entry and commit clearing. The
   model must label its assumptions; passing it cannot verify WB32 retention.
2. **Improve the clock simulation's evidential scope.** The existing native
   `soc_clock_wb32_test.c` makes oscillator-ready status follow enable writes
   immediately. It proves the corrected APB-before-GPIO order, not stabilization
   timing or reset behavior. An isolated future harness can replay documented
   defaults, resident PRE_OP output and Vial-prepared state; inject delayed or
   permanently absent readiness and stop the host harness with a finite budget.
   Do not change production waits, watchdog feeds or analog writes to make that
   harness pass.
3. **Automate frozen-binary checks.** Verify resident load bytes/padded identity,
   actual reset backend, CRC loop count, caller paths, vector reachability and
   page deltas. Extract Thumb code correctly and distinguish VMA from flash
   load addresses. These checks can prevent a later experiment from silently
   changing the preserved resident or testing the wrong image.
4. **Design application-only checkpoints in a host test first.** A future
   diagnostic sender/receiver pair can record commit return, reset request,
   earliest `CONFIG_SOC_RESET_HOOK`, before/after each clock wait, watchdog-init
   result, B1/service/USB init and first worker/feed. Use fixed-address,
   versioned NOLOAD storage with nonce/build identity, sequence, complement/CRC
   and commit-last publication. Preserve the existing `0x20000000..0x2000003f`
   trial record and `0x20006fc0..0x20006fff` exclusion/boot magic. Prove the new
   area avoids both images' data/BSS/stacks and the resident's descending stack;
   do not choose it just because current BSS appears smaller. Test torn writes,
   reset between every store, invalid power-on RAM, image mismatch and repeated
   failures preserving the first useful record. Add no logging, allocation,
   flash writes, extra polls or watchdog feeds in early checkpoints.

That diagnostic design needs an extraction path **before** it is useful. USB-only
getters cannot read a record while USB is absent, and the tested joined-left
power cycle can destroy SRAM evidence. Offline emulation and an independently
controlled spare WB32 fixture can establish record semantics and reset-domain
behavior without touching this keyboard. Draft vendor questions should ask
specifically whether IWDG enable, PR/RLR, bus/source gates and the counter reload
survive SYSRESETREQ and IWDG reset, and what instruction-cache/FMC coherence is
guaranteed after page program. No message has been sent to anyone.

If later hardware localization is explicitly authorized, its required evidence
would include PC/LR/xPSR, VTOR, reset flags, current commit/vectors, selected
clock/status registers, IWDG configuration and independent liveness/USB state
**before a power cycle**. Debugger attach, halting, reset-on-connect and watchdog
freeze themselves alter the experiment and require a separately reviewed
procedure. No probe availability or safe extraction route is established here.
No live command, new install, identical-image replay or unattended reset loop is
part of the present recommendation.

The older detailed designs remain private at
`.work/zmk-permanent-continuation/sdd-archive/2026-10-27-zmk-permanent-20261028T185509Z/`:
`task-5-permanent-boot-diagnosis.md`, `task-5-native-warm-update-diagnosis.md`,
`task-5-early-boot-investigation.md` and `task-5-boot-localization-design.md`.
This report adds the actual Studio delta and quantified resident timing
constraint; it does not relabel those failed attempts or claim the fault fixed.

## Verification of this analysis

Read-only ARM `nm`/`objdump`, ELF program-header extraction and Python
standard-library hashing/page comparison ran after sourcing `scripts/project.sh`.
The frozen final Studio ELF SHA256 is
`5129e856dfae049ee123781497b60fb5d54baeb0c3c05bc54fac7828f1abde44`, matching
its frozen manifest. Source and actual machine code agree on the reset,
resident and clock paths described above. The exact CRC instruction counts,
17 changed pages, 352 changed bytes and padded resident identity were recomputed
from existing files. No broad tests/builds were needed for this documentation
output, and no physical conclusion was inferred from a host simulation.
