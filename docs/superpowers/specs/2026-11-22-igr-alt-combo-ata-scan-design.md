# IGR alternative combination and internal ATA/exFAT scan repair

## Intent

Repair two regressions in one small follow-up PR after the current delivery branches land. Do not add another settings system or replace working storage drivers.

## Evidence and current behavior

- Issue #416 already has an `IGR on L3 + R3` setting, default Off. With it Off, Start+Select performs IGR and L3+R3 powers off. With it On, Start+Select must be left to the game, L3+R3 must perform IGR instead, and the separate pad power-off shortcut must be disabled. The physical power button and the in-game menu's Power Off action remain available.
- The follow-up report says the new combination freezes on build 4303. The task is to diagnose and repair that existing path while preserving both On and Off contracts and normal in-game-menu behavior.
- The supplied internal exFAT ATA screenshots show HDD spinning up and an enabled HDD page, but `mass0:` cannot open CD or DVD and reports error 1. MMCE works independently. The same disk works in wOPL, so treat this as a RiptOPL path/scan regression, not a bad disk.

## Goals

- Reproduce the alternative-combo failure with host tests, then fix its routing without changing the default setting or either On/Off meaning.
- Reproduce the ATA library-scan failure with focused host tests. Identify whether path construction, mount/identity, folder creation, or error propagation causes it before changing production behavior.
- Keep changes limited to the faulty paths and their existing tests. Run the full host suite and sequential LLVM and GCC/RA builds.
- Deliver through a stacked PR based on #487's current head. Retarget it to `rebuild/main` after #487 merges. Do not merge this follow-up until its own CodeRabbit findings are handled and CI is green.

## Non-goals and anti-goals

- No duplicate IGR setting, no changed default, and no removal of the physical power button or in-game-menu Power Off action.
- No ATA/BDM driver rewrite, no speculative filesystem changes, and no changes to MMCE behavior.
- No merging #487, #491, or #488 before their reviews and CI are complete, and no merging this follow-up before those branches land.
- No claim of physical-console acceptance from host tests, emulator output, or successful builds.

## Approaches considered

1. **Chosen: targeted repair.** Use the existing IGR alternative-combo and ATA scan paths, write failing regressions first, and make only evidence-backed corrections. This preserves the current architecture and minimizes risk.
2. **Diagnostics-first.** Add bounded ATA scan diagnostics and wait for another console run before changing behavior. Keep this as a fallback if host reproduction cannot establish the root cause; do not use it to justify a broad rewrite.

## Implementation boundaries

- For IGR, verify pad detection, `Pad_Data.combo_type`, reset versus power-off handling, in-game-menu entry, configuration propagation, and the report's freeze path. Cover both combinations in both setting states and the physical-button/menu escape paths.
- For ATA, trace `sbReadList`, `scanForISO`, the BDM prefix, CD/DVD creation, and the saved error value. Preserve last-good lists on transient read failures and leave MMCE and other BDM backends unchanged.
- Use the existing host harnesses where practical. Do not add new allocation systems, rendering systems, or unrelated refactors.

## Remaining builder decisions

- Exact root-cause lines and the smallest corrections after the failing controls are observed.
- Whether an existing diagnostic configuration is sufficient for any follow-up console evidence needed to confirm the IGR freeze and ATA scan on hardware.
