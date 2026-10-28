# Manual joined-left workflow decision

On 2026-10-28 the accepted wired milestone changed from automatic restart to an
explicit updater transfer and separate readback/health verification, with
joined-LEFT USB cycling as a tested fallback when restart verification fails.
Product authorization remains permanent ZMK and left-USB updater
support, with a strong preference to avoid right-only recovery where possible.
No firmware behavior or resident change was made by this decision.

Four distinct left installations failed automatic restart and became healthy
only after physical reconnection. They stay failed. The automatic root cause is
unresolved. The missing worker startup and B1 USB power initialization were fixed
with source-backed tests; a 50-ms disconnect experiment failed and was reverted.
No cache/clock/watchdog/resident speculation was promoted to a discovered cause.
The separate identical-image control was prepared offline and deferred without
hardware execution.

Before right replacement, the reviewed manual chain requires genuine right
Vial PROBE/automatic recovery, actual shared reset evidence from lost terminal
record and restarted counters after the left A→B cycle, and actual right native
Q INFO/staging no-feed health within 3 seconds plus genuine Vial 60-second return.
Ordinary cold Vial may have flags 0/feed 0; those are not recovery feeds.

Q cannot establish first permanent APP entry/feeding or guarantee recovery from
an unreachable right image. There is no old-image rollback slot or universal
automatic fallback. That remaining risk was explicitly reviewed within the user's
existing instruction; no right-only procedure or guarantee was introduced.
Separate actual identities/residents/USB/relay and at least 60-second native
health establish installation. The first right B and distinct right B→A passed
after confirmed joined-left cycles. Final left B→A was independently verified
as A/A without final cycle confirmation; the strict final manual stage stayed
unexecuted. Physical action at that final restart is unknown, so automatic
restart is not claimed. Native saved-configuration backups stay private; no
post-ZMK settings-readback claim is supported.

## Review/readiness record

Before hardware, the original full implementation and host contracts passed
review; subsequent worker lifecycle and B1-only initialization repairs passed
scoped reviews. Actual A/B/Q builds and independent image checks passed at
source `0554dffec19bfb6fb50f3afd87c3c5afb9f9820a`. The combined B1 readiness
index SHA256 was `7e7ae2eb01da9b50063e563f4c88f7f2a5e7372ecd4a0031b38bda82fa6df61f`;
actual build-facts SHA256 was `e9368ded326803886b2c60f06368a8c328ff57c88a861c36f6c0a195eb420ea6`.
The separate manual procedure and residual first-right-install risk passed
review before execution (review SHA256
`211d032cf6c052efac94b01e33659517ad42197332f477c32a357d74851a8a22`). All writes stayed application-only, through LEFT USB;
resident/configuration writes, driver changes and right-only recovery were not
part of this migration. Complete historical reviews remain private, while the
source/package pins and sanitized outcomes here preserve the durable rationale.

Archive this concise decision with sanitized final evidence so it survives
cleanup of ignored SDD execution records. Keep full immutable reports/reviews,
original backups and package hashes privately; do not publish HID paths/serials
or configuration payloads.

## Preserved implementation rulings

The six literal `Ruling:` entries below are retained in ledger order. The cost
notes are retrospective explanations of what would be at risk if each judgment
were wrong; they do not assert a new hardware failure.

### 1

```text
Ruling: User's prior explicit continuous execution/opt-out persists; implement in sole existing checkout/task branch without design approval or worktree question. Preserve resident, EEPROM, pinned deps; separate hardware operations. Wired USB is first permanent target; do not claim radio/RGB/encoder/Studio.
```

Cost if wrong: If prior authorization was misunderstood, execution in the shared checkout could exceed intent or interfere with concurrent edits; residents/configuration and operation separation therefore remain hard boundaries.

### 2

```text
Ruling: permanent watchdog init must setLSI/clocks/clearDBGfreeze, configurelongperiod/reload/start thenhousekeepingfeed; stageinit/feed no-op. Agent confirmed nativevendorpreop restorationAPB2PRE/APB2ENR/RNGCLK/PCLK/FHSI/IRQ.
```

Cost if wrong: An incorrect permanent-watchdog sequence could reset or strand native firmware and require physical recovery; staging must never feed it.

### 3

```text
Ruling: Split originalgiantTask3 into receiverTask3 + board/build/hostTask4; hardwareTask5. Independenttest/reviewgates reduceUSBconcurrency+packagingreviewcomplexity. Task3willfixTask1realARMinclude. Task4endSymbol__rom_region_start+_flash_used matchesentireZephyrbin. Rootspec/planworkingtreeauthoritativeclarifications; no otherproductedits. Needcommitrootdocs beforeTask3.
```

Cost if wrong: Splitting tasks can leave integration assumptions between reviewed components; later actual ARM/build/whole-branch checks must cover those seams.

### 4

```text
Ruling: ReplaceinventedVialrollbackroundtrip withrealZMK A/Bdifferent-imageselfupdatewhileRIGHTVialunchanged. PermanentIWDGmaypersist; generalfrozenVialwarmrollbackaftercoldbootnotqualifiedandmayneedjoinedLEFTpowercycle. Don'tfabricatetrialrecord/updatecommon/resident. Keeprecoveryimages/backups. AfterLEFTA/Bproof, RIGHTstagequalificationusingpeer71relay+samehandlehostobserver readsNS72rightduring3s andverifiesVialauto60shealth, theninstallRIGHTnative. FinalA/Brightupdate+leftselfupdateprovesupdater. EPconfigpreservationviaabsencewrites+backup; don'tclaimpostZMK263getterreadbackwithoutactualVialreturn.
```

Cost if wrong: ZMK-to-ZMK proof does not qualify general Vial warm rollback; recovery may require joined-left power cycling and unreachable firmware may require more involved recovery.

### 5

```text
Ruling: perhalfcompletedhealthmaxgap2s andstartwindowuptime>2s (same strictevidenceprincipleasVialrecovery); avoidsbriefinitialrestartsilentlypassingmonotoniccheckswhenfirstuptimeyoungerthansamplegap. Task4agentinitial5schoice tightenedviaupdatedbrief. Healthremote7cachednonBUSYcannotretryidenticalnonce; boundedfreshnoncesampleafter7, sameBUSYnonce. Noactualhardwareclaim.
```

Cost if wrong: A stricter startup/gap rule can reject healthy timing; an insufficient rule can miss a reset or stalled feed. Actual bounded continuity remains necessary.

### 6

```text
Ruling: whole-branchcode review now before any devicewrites, rather than waitinguntilhardwareTask5done — userprioritizesavoidingrightphysicalrecovery andplanmandatesprehardwaregate. Task5mayprepareignoredscripts/offlinetestsinparallelwithread-onlyreview, butnofirmwarechanges/nohardwareexecutionuntilbroadreviewpasses. Afterdeviceworkreviewonlynewprocedure/evidence/docs, notduplicatewhole-branchreviewwithoutnewproductionchanges. Costifwrong: extra docs/scopedrevieworhardwareprocedureadjustment beforedependentwrites.
```

Cost if wrong: As recorded in the ruling: extra documentation/scoped review or hardware-procedure adjustment before dependent writes; premature hardware could instead require physical recovery.

## Later manual-workflow decision and residual risk

The later explicit product decision accepted updater transfer followed by
separate running-image verification, with joined-left manual recovery rather
than requiring automatic restart. It deferred the unused identical-image
experiment and required genuine PROBE/shared recovery, actual right Q and an
independent residual-risk review before first right replacement. The first-right
APP entry/watchdog/relay risk remained: Q runs from staging, residents have no
old-application rollback slot, and a joined-left cycle cannot recover every
unreachable right image. Cost if wrong: a first-right failure could require
recovery beyond the preferred LEFT-only workflow. No such guarantee was made.
The final independently healthy A/A result does not retroactively prove automatic
restart or relabel prior failures. Future cycling is fallback only after failed
return/verification, never mandatory after verified success.

The final user observation is limited: typing seems functional, while Fn+Left
produced word navigation and expected Home remains unverified/unexpected. No
cause was inferred and no firmware change followed from that observation.
