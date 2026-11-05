# Saved disk theme stalls on restart — investigation and fix (2026-11-03/04)

**Status: a source-level glyph-atlas ownership violation on the boot path is guarded
(2026-11-04); whether it causes the reported freeze is still open.** The hardware report is from
HollywoodScotty in the PS2 Scene discussion, using a configuration associated with
Berion. A disk theme can be selected successfully through the running RiptOPL
Interface settings menu, but after reboot the console remains on the splash near
"Loading device drivers...". Removing the saved `theme=` value allows the
built-in inceBtion look to boot. The reporter says the behaviour occurs with
several themes, not one broken asset pack.

These details are the report, **not** a verified local reproduction. The exact
edition/build, theme files, settings-home device, device source settings, model,
and whether the GUI is frozen or only the boot worker is stalled remain unknown.
The diagnostic breadcrumbs below stay in the `__OPLDIAG` builds so a report can
still confirm or refute the fix on a console.

## Suspected source-level cause and guard

The boot path for a saved disk theme is: the splash draws on the GUI thread
every frame; the IO worker brings a storage device up, `bdmNeedsUpdate` ->
`thmAddElements(path, "/", 1)` finds the saved theme and runs `thmLoad` on the
IO thread; `thmLoadFonts` calls `fntLoadFile` (and `fntSetWeight`) for each
theme font. Every font load ends in `FontSystem::updateAspectRatioLocked`, which
flushes **every** font's glyph cache and atlases -- including the font the splash
is drawing its status line with.

A glyph atlas is a GS texture. Its flush (`cacheFlush` -> `Atlas::destroy` ->
`rmUnloadTexture` + `free`) does two things that are GUI-frame work everywhere
else in the tree:

- `rmUnloadTexture` -> `gsKit_TexManager_free` unlinks and frees nodes of
  gsKit's **one global, unlocked** VRAM block list (`gsTexManager.c`). The GUI
  thread walks and splits that same list on every textured draw
  (`gsKit_TexManager_bind`, inside `rmDrawQuad`/`rmDrawPixmap`). The GUI thread
  runs at priority 31 and the IO worker at 32, so a VSync wakeup preempts the
  worker at any instruction inside that list surgery. A walk over a half-unlinked
  or freed node can leave `_blockAlloc` spinning with the GUI lock held, which
  matches the frozen splash; the worker then blocks on the GUI lock, so the
  "Loading ..." label never advances.
- `free(surface_.Mem)` releases pixels that the frame in flight still references
  through its inline upload tags (and, under HIRES, that the DMA thread replays
  until the next flip).

The font lock added for the earlier, random form of this freeze (the long note at
`src/gfx/fntsys.cpp` `FontSystem::lock`) covers the CPU side only: the atlas
pointers and glyph cache. It does not serialise against gsKit's list or the GS.
`texcache.cpp` ("GUI thread: gsKit contact is correct here"), `themes.cpp`
`thmGlyphSetLoad` ("VRAM copies to drop ... only ever done on the GUI thread")
and `thmLoad`'s publish tail (GUI lock, `rmDiscardFrame()`, then `thmFree`, from
#302) all state the rule the font loaders were breaking. Selecting the same theme
in Settings works because that load runs on the GUI thread between frames.

Ruled out from the source: a GUI/theme lock-order inversion (every
`thmSwitchSema` taker on the GUI thread runs outside the GUI lock); the heap
(`malloc`/`free` are semaphore-locked in ps2sdk's libcglue); IO-thread stack;
`gsKit_hires_sync` (a plain VSync semaphore wait).

**Fix (`src/gfx/fntsys.cpp`):** a `FontFrameScope` -- GUI lock, `rmDiscardFrame()`,
released at scope exit -- wraps the locked section of the four entry points that
can flush atlases: `loadFile`, `loadDefault`, `setWeight`, `updateAspectRatio`.
Lock order is the one `renderString` already establishes (frame lock, then font
lock). The slow file read stays outside both locks, so the splash keeps drawing
while a font is read from a slow device. `release()` is deliberately not wrapped:
`thmFree` calls it already holding the GUI lock after its own `rmDiscardFrame`.
Before `guiInit` the GUI lock is a no-op and `rmDiscardFrame` has nothing to
retire, so `lngInit`/`thmInit` are unaffected.

**Regression test:** `.github/scripts/test_font_flush_frame_lock.py` compiles
the production loaders against a stub GUI/renderer that models the contract; its
control build (the same code with only the scope removed) trips on "glyph atlas
left the texture manager outside the GUI lock", the fixed build passes.

**What this does not prove:** the fix is reasoned from the source and a host
model of the ownership contract. It has not been run on the reporter's console or
theme. If a saved-theme boot still stalls on hardware with this build, the
watchdog report described below is the next piece of evidence; a last PC inside
`gsKit_TexManager_bind`/`_blockAlloc` or `FontSystem::cacheFlush` would confirm
this mechanism, anything else points past it.

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
