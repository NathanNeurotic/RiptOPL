# Exit To BOOT ELF Choices Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Exit To choices for `mc?:/BOOT/BOOT2.ELF` and `mc?:/BOOT/BOOT.ELF`, trying mc0 before mc1.

**Architecture:** Append two values to the existing `gExitMenu` enum and extend `sysExecExit()` to probe those fixed card paths and hand the selected ELF to `sysLoadELFApp()`. Reuse the existing settings dialog and localization template; do not add a picker or another launcher.

**Tech Stack:** C++, PS2SDK, Python host regression harnesses.

**Spec:** `docs/superpowers/specs/2026-11-19-exit-boot-elf-design.md`

## Global Constraints

- Preserve existing stored values and the OSDSYS-with-update default.
- Only the main menu's Exit changes; restart and IGR behavior stay unchanged.
- Try mc0 first, then mc1 when the selected file is absent or cannot be loaded.
- Pass the selected ELF path as `argv[0]`, reset the IOP through `sysLoadELFApp()`, and add no launch arguments.

## Review Focus

- Neither card contains the selected ELF: remain in RiptOPL without falling through to an OSDSYS exit; pin in the host dispatcher test.
- mc0 lacks the file or its load fails: attempt the same filename on mc1; pin both cases.
- Both cards contain the file: launch mc0 only; pin the call sequence.
- Existing OSDSYS choices and restart calls: preserve their ExecOSD/exit behavior; pin in the same test.
- Unknown saved values: still sanitize to `EXIT_MENU_CONSOLE`; exercise the updated validation bound.

---

### Task 1: Add the two fixed-path ELF exit choices

**Files:**
- Create: `.github/scripts/test_exit_boot_options.py`
- Modify: `include/opl.h:230-238`
- Modify: `src/platform/system.cpp:649-669`
- Modify: `src/app/settingsio.cpp:1691-1694`
- Modify: `src/ui/guisettings.cpp:2383-2416`
- Modify: `lng/tmpl/_base.yml:2586-2602`
- Modify: `docs/SETTINGS.md:81`
- Modify: `docs/DEFAULTS.md:25`

**Interfaces:**
- Consumes: `int sysLoadELFApp(const char *filename, const char *partition, int argc, char *argv[], int resetIop, int cleanupHdd)` from `include/system.h`.
- Produces: `EXIT_MENU_BOOT2 = 4` and `EXIT_MENU_BOOT = 5`; `sysExecExit(1)` maps them to `BOOT2.ELF` and `BOOT.ELF` respectively.

- [ ] **Step 1: Write the failing dispatcher and configuration tests**

Create a host harness that extracts the real `sysExecExit()` and supplies bounded mocks for file open/close, application teardown, `ExecOSD()`, `exit()`, and `sysLoadELFApp()`. Assert both filename mappings, mc0-first behavior, mc1 fallback for absence and load failure, `argv[0]`, `resetIop=1`, `cleanupHdd=0`, no OSDSYS fallthrough when neither load succeeds, unchanged legacy choices, and unchanged `sysExecExit(0)`. Exercise the configuration validation block with values 4 and 5 accepted and out-of-range values reset to `EXIT_MENU_CONSOLE`.

- [ ] **Step 2: Run the new test to verify it fails**

Run: `python3 .github/scripts/test_exit_boot_options.py`
Expected: FAIL because the new enum values/BOOT dispatch do not exist.

- [ ] **Step 3: Implement the saved choices and ELF dispatch**

Append `EXIT_MENU_BOOT2` and `EXIT_MENU_BOOT` without renumbering existing values; extend the validation upper bound. In `sysExecExit()`, select only on `toMenuChoice != 0`, probe `mc0:/BOOT/<name>` then `mc1:/BOOT/<name>`, tear down once, and call `sysLoadELFApp(path, "", 1, argv, 1, 0)`. If the first load fails, try the other card; if neither succeeds, return without calling `ExecOSD()` or `exit(0)`.

- [ ] **Step 4: Add the settings labels and documentation**

Append BOOT2 then BOOT to the existing Exit To display order, with labels `mc?:/BOOT/BOOT2.ELF` and `mc?:/BOOT/BOOT.ELF`. Add their English template entries, extend the Exit To hint and `docs/SETTINGS.md`, and leave the default documented as OSDSYS (With Update).

- [ ] **Step 5: Run focused tests and verify they pass**

Run: `python3 .github/scripts/test_exit_boot_options.py && python3 .github/scripts/test_language_switch.py && python3 .github/scripts/test_language_discovery.py`
Expected: all commands exit 0.

- [ ] **Step 6: Run the full host suite and standard build**

Run all `.github/scripts/test_*.py` tests in WSL, keeping any git-dependent release test isolated from inherited `GIT_DIR`/`GIT_WORK_TREE`. Then run `python tools/build.py` and `python tools/analyze_coupling.py --check`.
Expected: no host failures, no coupling regression, and a successful PS2 ELF build.

- [ ] **Step 7: Commit**

```bash
git add .github/scripts/test_exit_boot_options.py include/opl.h src/platform/system.cpp src/app/settingsio.cpp src/ui/guisettings.cpp lng/tmpl/_base.yml docs/SETTINGS.md docs/DEFAULTS.md docs/superpowers/specs/2026-11-19-exit-boot-elf-design.md docs/superpowers/plans/2026-11-19-exit-boot-elf.md
git commit -m "feat(settings): add BOOT ELF exit choices"
```
