# Exit To BOOT ELF choices

## Intent

Let users exit RiptOPL directly to `mc?:/BOOT/BOOT2.ELF` or `mc?:/BOOT/BOOT.ELF`, without depending on an OSDSYS/FMCB update variation.

## Scope

- Add two choices to Settings > General & System > Exit To.
- Preserve every existing choice, saved value, and the OSDSYS-with-update default.
- Only the main menu's Exit uses the new choices; restart and IGR behavior stay unchanged.
- Do not add a general file picker, a new launcher, or changes to other device types.

## Card selection and launch

For the selected filename, try `mc0:/BOOT/` first, then `mc1:/BOOT/` if the file is absent or its load fails. Use the existing `sysLoadELFApp()` handoff with an IOP reset, after the same application teardown used by other exit modes. Pass the selected ELF path as `argv[0]` and add no extra arguments.

## Approaches

- Chosen: extend the existing Exit To enum, settings list, and `sysExecExit()` using the existing ELF loader. This reuses configuration persistence and launch safeguards.
- Rejected: invoke OSDSYS with special arguments. That would still depend on the user's OSDSYS/FMCB setup, which these options are meant to avoid.
- Rejected: separate mc0/mc1 menu choices or a picker on every exit. The selected behavior is mc0 first with mc1 fallback, without another prompt.

## Verification

Add host coverage for both filenames, mc0-first selection, mc1 fallback, unchanged legacy choices, and saved-value validation. Run the related host suites and the standard PS2 build. Emulator or physical-console launch confirmation is separate from host/build evidence.
