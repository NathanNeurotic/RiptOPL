# Saved disk theme stalls on restart — investigation (2026-11-03)

**Status: cause unconfirmed; diagnostic-only PR.** The hardware report is from
HollywoodScotty in the PS2 Scene discussion, using a configuration associated with
Berion. A disk theme can be selected successfully through the running RiptOPL
Interface settings menu, but after reboot the console remains on the splash near
"Loading device drivers...". Removing the saved `theme=` value allows the
built-in inceBtion look to boot. The reporter says the behaviour occurs with
several themes, not one broken asset pack.

These details are the report, **not** a verified local reproduction. The exact
edition/build, theme files, settings-home device, device source settings, model,
and whether the GUI is frozen or only the boot worker is stalled remain unknown.

## Why this needs a startup-specific test

`_loadConfig()` resolves the saved name to the catalog, then `applyConfig()`
chooses the theme and starts device support. A theme on storage that appears
later can be discovered in `ThemeCatalog::addElements()` and loaded on the IO
thread while the splash is drawing. Manually selecting a theme after boot runs
in a different timing context. The displayed splash label is the **last rendered
progress label**, not a reliable backtrace or proof that the device driver itself
is defective.

No theme is discarded, no saved preference is overwritten, and no IOP reset,
timeout, retry loop, or boot ordering change is proposed by this investigation.

## Safely reproduce and collect evidence

1. **Back up** the current `settings_riptopl.cfg` and the selected `THM/thm_*`
   folder. Do not delete either or reformat a device to diagnose this problem.
2. Record the PS2 model, edition (especially inceBtion versus Korium), exact
   version/commit and boot device. Record whether the theme and settings file
   live on MC, USB, MX4SIO/MMCE, i.LINK, APA, exFAT HDD, or SMB. Note whether a
   memory card is available at `mc0:` or `mc1:`.
3. On a working boot using the built-in theme, select a disk theme, save the
   settings, confirm it works **before** the restart, and then restart without
   changing devices. Record the exact last screen and whether animation/pad
   response continues. Test one theme at a time.
4. Repeat using the **matching diagnostic configuration** from this PR (do not
   accidentally test an unrelated release ELF). The diagnostic build already
   has a boot watchdog and exception catcher. This PR records the saved theme
   name and checkpoints for scanning, theme configuration, font loading, UI
   elements, texture preparation, and final GUI publication in its in-RAM log.
   Normal non-diagnostic editions get none of these extra breadcrumbs.
5. Leave the frozen diagnostic build running long enough for its existing boot
   watchdog to fire. A successful report write changes the screen to **green**.
   Check `mc0:/RIPTOPL-FREEZE.TXT` or `mc1:/RIPTOPL-FREEZE.TXT`. An exception
   can instead produce `RIPTOPL-CRASH.TXT`. **Orange** means the report write
   failed; record that colour rather than claiming the report exists. If it
   stays on the original screen, record that too.
6. Share the report, the matching diagnostic ELF's build identity and map
   file if available, the affected `conf_theme.cfg`, and a **sanitized**
   `settings_riptopl.cfg`. Remove network passwords, account tokens, IPs or
   other private fields before posting. A screenshot/video of the frozen frame
   is useful. The watchdog's EE thread/semaphore states and last PCs can
   distinguish a lock wait from an exception or a slow/blocking device call.

### Recovery without data loss

Holding **START** during boot activates the existing safe-boot path, which skips
loading saved configuration. **Do not save settings from this safe-boot session:**
the rest of the saved settings were not loaded and a save could replace them with defaults.
For persistent recovery, edit **only** the `theme=` line in a backed-up copy
of the original active `settings_riptopl.cfg` to `theme=<OPL>`, or remove that key,
then replace the active file while retaining an untouched backup.
Do not delete game libraries, VMC saves, the theme folder, or the entire
settings file. A successful fallback is a useful isolation result, not a cause.

## Interpreting the report

- The last `boot theme: saved ...` line proves the saved name was parsed, but
  does not prove its on-disk folder was discovered yet.
- `theme scan: begin` without `theme scan: end` isolates a directory traversal. The additional `theme fs:` checkpoints now distinguish `checkFile` probing, the initial `opendir`, early `readdir` calls, and `closedir`. Missing `opendir returned` after `opendir begin` suggests the directory-open RPC stalled; missing `readdir finished` suggests directory enumeration, a nested `theme dirent: probing candidate` call, or their respective drivers. The last trace line is a clue, not a guaranteed stack frame.
- Different results with the **same theme copied to MC versus USB/ATA/MMCE** isolate storage/driver timing from the theme's own configuration. Keep settings and enabled sources unchanged when making this comparison.
- A disk theme that loads manually after startup but stalls on reboot is consistent with a late device mount and theme discovery through the IO worker. That is not evidence that missing PNGs caused the hang. The existing font-rendering lock has already been hardened against a known, similar boot-stage freeze; check thread and semaphore states before reworking locks.
- `theme scan: applying saved theme` and `theme load:` checkpoints narrow the
  work to pre-read drain, config, fonts, layout/elements, textures, or GUI swap.
- `theme load: waiting for GUI publish` without `published` warrants checking
  **both** the renderer lock and old-theme teardown. It does not by itself
  prove a deadlock; the watchdog's thread/PC data is needed.
- A complete `theme load: published` followed by a frozen intro shifts the
  investigation to later menu initialization or the boot-sound/fade path.

**Merge policy:** Do not merge a speculative production fix just because a
theme-free boot succeeds. First identify the blocking path in a hardware report;
then add the smallest causal fix and a failing-before/passing-after regression.
