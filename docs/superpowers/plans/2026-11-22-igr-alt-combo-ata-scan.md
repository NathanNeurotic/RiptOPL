# IGR alternative combination and ATA/exFAT scan implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Repair the existing alternative IGR combination and the internal ATA/exFAT library-scan failure with focused, test-backed changes in one stacked follow-up PR.

**Architecture:** Preserve the existing IGR setting and BDM/ATA architecture. Reproduce each failure in its existing host harness, identify the exact faulty path, and make only the minimal correction required by that evidence.

**Tech Stack:** C/C++ for the EE and menu paths, Python source-extraction host harnesses, ps2build LLVM and GCC, GitHub CodeRabbit and CI.

**Spec:** `docs/superpowers/specs/2026-11-22-igr-alt-combo-ata-scan-design.md`

## Global Constraints

- IGR alternative combination defaults Off. Off preserves Start+Select IGR and L3+R3 power off; On leaves Start+Select to the game, routes L3+R3 to IGR, and disables the separate pad power-off combination. The physical power button and in-game-menu Power Off remain available.
- Do not change the IGR setting's meaning, add a duplicate setting, or change MMCE/BDM driver architecture.
- Diagnose ATA path construction, mount/identity, folder creation, and error propagation before changing production behavior. Preserve last-good lists on transient read failures.
- #487, #491, and #488 merge only after their CodeRabbit findings are handled and CI is green. This follow-up merges only after those branches land and its own findings and CI are clean.
- Host tests and emulator output are not physical-console acceptance.

## Review Focus

- Alternative On: L3+R3 must reset through the normal IGR worker rather than freezing or powering off.
- Alternative Off: Start+Select resets and L3+R3 powers off exactly as before.
- Alternative On with the in-game menu: Start+Select remains available to the game; the menu and physical power button retain their escape paths.
- ATA with valid CD/DVD folders: scan must publish games rather than reporting `mass0:` error 1; MMCE behavior must not change.
- ATA transient read failure: preserve the last-good list and report the actual error without a stale or misleading errno.

---

### Task 1: Repair the alternative IGR combination

**Files:**
- Modify: `ee_core/src/padhook.c`
- Modify: `.github/scripts/test_igr_polling.py`
- Modify if propagation is implicated: `src/app/host.cpp`

**Interfaces:**
- Consumes: `config->IgrAltCombo`, `Pad_Data.combo_type`, `Power_Button.press`, and the existing IGR worker in `ee_core/src/padhook.c`.
- Produces: unchanged constants `IGR_COMBO_START_SELECT` and `IGR_COMBO_R3_L3`, with reset routed to `oplIGRShutdown(0)` and power off to `oplIGRShutdown(1)` according to the setting.

- [ ] **Step 1: Add alternative-combo controls to `test_igr_polling.py`**

Exercise the real `IGR_CheckInputs` and production worker with both combinations in both setting states. Assert:
- Off: Start+Select selects reset; L3+R3 selects power off.
- On: Start+Select produces no reset request; L3+R3 selects reset and never selects power off.
- Physical-button and in-game-menu paths remain intact.

Run the focused test. If it passes, extend the control through the in-game-menu and worker-reset stages until the reported freeze is reproduced; a passing input-detection stage proves only that mapping was not the root cause.

- [ ] **Step 2: Run the focused control and identify the first failing stage**

Run:

```bash
wsl -e bash -lc 'cd /mnt/c/Users/natha/Github/RiptOPL.worktrees/igr-ata-followup && export TMPDIR="$PWD/.tmp/host-wsl" GIT_CEILING_DIRECTORIES="$PWD/.tmp" && python3 .github/scripts/test_igr_polling.py'
```

Expected: a named stage fails and identifies whether detection, worker reset, in-game-menu handling, or reset-caller reinstallation causes the freeze. If every stage passes, record that the host model does not reproduce the console freeze and use the spec's bounded-diagnostic fallback rather than guessing.

- [ ] **Step 3: Apply the smallest correction to the identified path**

Change only the implicated production path. Keep `config->IgrAltCombo` and both combo constants unchanged, and do not alter the physical-button or in-game-menu escape paths.

- [ ] **Step 4: Rerun the control and existing IGR suites**

Run the command from Step 2 plus `.github/scripts/test_igr_power_button.py`, `.github/scripts/test_igr_menu_no_room.py`, and `tests/host/run.sh`.

Expected: the new On/Off controls and all existing IGR tests pass.

- [ ] **Step 5: Commit**

```bash
git add ee_core/src/padhook.c src/app/host.cpp .github/scripts/test_igr_polling.py
git commit -m "Fix alternative IGR combination routing"
```

---

### Task 2: Repair the internal ATA/exFAT library scan

**Files:**
- Modify: `.github/scripts/test_bdm_scan_retry.py`
- Modify as evidence requires: `src/sources/supportbase.cpp`, `src/sources/bdmsupport.cpp`, `src/sources/bdmdevices.cpp`, or `src/platform/util.cpp`
- Modify only if ATA readiness is implicated: `.github/scripts/test_bdm_ata_readiness.py`

**Interfaces:**
- Consumes: `sbReadList`, `scanForISO`, the BDM prefix and `sbGetReadListError`, and the existing ATA readiness path.
- Produces: a valid internal ATA/exFAT scan that preserves the existing `sbReadList` signature and last-good-list behavior.

- [ ] **Step 1: Add the `mass0:` error-1 reproduction to `test_bdm_scan_retry.py`**

Drive the real scan with an internal-ATA prefix and the observed error path. Assert which of these is responsible: missing/uncreated folders, mount/identity, path construction, or saved errno. Include valid CD/DVD folders and verify MMCE is unaffected.

Run the focused test. If the current code passes, extend the reproduction through the real folder-creation and readiness helpers until the faulty stage is isolated; do not treat a passing model as hardware proof.

- [ ] **Step 2: Run the focused control and identify the first failing stage**

Run:

```bash
wsl -e bash -lc 'cd /mnt/c/Users/natha/Github/RiptOPL.worktrees/igr-ata-followup && export TMPDIR="$PWD/.tmp/host-wsl" GIT_CEILING_DIRECTORIES="$PWD/.tmp" && python3 .github/scripts/test_bdm_scan_retry.py'
```

Expected: a named stage reproduces `mass0:` error 1 and identifies the responsible path. If no host stage reproduces it, use bounded diagnostics rather than a driver rewrite.

- [ ] **Step 3: Apply the smallest correction to the identified path**

Correct only the proven path, preserving `sbReadList`, `scanForISO`, and MMCE behavior. A valid scan must publish the library; a transient failure must preserve the last-good list and report the actual saved errno.

- [ ] **Step 4: Rerun the control and BDM suites**

Run the command from Step 2 plus `.github/scripts/test_bdm_ata_readiness.py`, `.github/scripts/test_iso_cache.py`, and `tests/host/run.sh`.

Expected: the ATA reproduction and all existing BDM suites pass.

- [ ] **Step 5: Commit**

```bash
git add src/sources/supportbase.cpp src/sources/bdmsupport.cpp src/sources/bdmdevices.cpp src/platform/util.cpp .github/scripts/test_bdm_scan_retry.py .github/scripts/test_bdm_ata_readiness.py
git commit -m "Fix internal ATA exFAT library scanning"
```

---

### Task 3: Verify and deliver the stacked PR

**Files:**
- Modify only as required by verified review findings.

**Interfaces:**
- Consumes: the two focused fixes from Tasks 1 and 2.
- Produces: a small follow-up PR based on #487's current head, retargeted to `rebuild/main` after #487 merges.

- [ ] **Step 1: Run the complete source suite**

Run `tools/run_tests.sh` with `TMPDIR="$PWD/.tmp/host-wsl"` and `GIT_CEILING_DIRECTORIES="$PWD/.tmp"`.

Expected: all groups pass; report any pre-existing skip separately.

- [ ] **Step 2: Build both target editions sequentially**

Run `tools/build_edition.py riptopl --toolchain llvm`, then `tools/build_edition.py ra --toolchain gcc`, using the established WSL ps2build environment and separate output directories.

Expected: both exit 0 and produce valid staged ELFs. Do not claim physical-console acceptance.

- [ ] **Step 3: Request CodeRabbit and handle each finding**

Request review on the follow-up PR. Reply `implemented and accepted`, `implemented adapted`, or `denied` with technical evidence to every actionable finding, then rerun affected tests and CI.

- [ ] **Step 4: Deliver only after the current branches merge**

Keep this PR stacked on #487 while it is pending. After #487, #491, and #488 merge, retarget this PR to `rebuild/main`, verify its diff contains only the follow-up work, and merge only after its own findings and CI are clean.
