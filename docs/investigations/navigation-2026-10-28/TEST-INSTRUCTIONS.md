# One-session automatic navigation trace

Give Zack **RIPTOPL-NAVTRACE.ELF**. It is an investigation build, not a replacement
release. It loads his normal configuration and artwork settings.

## Message to forward

> Launch this ELF using the same setup where the navigation problem happens.
> Turn ANALOG off and recreate the scrolling problem **three to five times**.
> After each attempt, let go of all buttons for about five seconds, until the
> banner says **Saved** and the capture count increases. There is no log-save
> button, menu option or test-mode control to operate.
>
> The ELF automatically appends every capture to **NAVTRACE.TXT**. It tries the
> folder you launched the ELF from first; if that is unwritable it tries the
> settings folder, available mass roots, then memory-card roots. The banner shows
> the exact successful path. Send back that text file after the final attempt.
> Keep the same controller, device, theme and settings for the whole test. If an
> attempt behaves differently, still pause and let it save, then repeat normally.
> Do not reset or remove the storage device while it is saving.

## What happens automatically

The first capture uses normal input behavior (A). After a successful capture at
an idle boundary, the next uses the candidate gap guard (B), then A again. The
banner and every text section identify the mode. Zack never changes modes.

The candidate retains only navigation identity for at most 100 ms when a
connected controller's sample becomes unknown. It suppresses navigation events
and freezes the repeat countdown during that interval. A real sampled release
or disconnect clears the state immediately. It retains no confirm/start/action
buttons. This tests the duplicate-edge/interrupted-repeat mechanism; it does
not change the SDK, IOP drivers, repeat cap or artwork fallback behavior.

The RAM trace contains per-port raw SDK state and native return length, packet
mode and button bytes, both modern SDK DMA-buffer frame words, aggregate/raw
versus delivered input, key events, selection, and timed art/CFG/SaveIcon/hidden/
audio/actuator work. A nonzero read length is not labeled a fresh sample.

Each export appends a `BEGIN NAVTRACE` / `END NAVTRACE complete=1` section. Earlier
captures remain in the file. Recording uses a fixed 256 KiB RAM ring and adds no
file writes per input poll. Normal export waits for at least three seconds of
clean idle input and no traced work in progress. A 45-second capture ceiling
allows an automatic export if the user keeps navigating; that export is marked
`forced=1` and does not switch modes. The exporter marks its own write interval
so it can be excluded from the diagnosis.

If every output path fails, the banner reports failure, the RAM capture remains,
and the build retries automatically. This is preferable to clearing the trace
or claiming a successful save on a short write or failed close.

A total console/IOP lock can prevent the exporter itself from executing or
completing. Software cannot guarantee a last-second file write after that kind
of failure; completed earlier capture sections are still useful. The primary
target here includes recoverable pauses, extra steps and interrupted holds.

## Building and verification

```sh
# Prepare translations and pinned lwNBD as in the repository's normal build.
python tools/assets.py fetch
python tools/assets.py lang
python tools/investigation/build_navtrace.py
```

The helper prepares Korium/standard GCC with the existing PADEMU setting, adds
the trial macro and source only to a temporary project definition, and places
the ELF and manifest in `dist/navtrace/`. The normal `ps2.yaml` and configuration
matrix are unchanged. Installed SDK versions and ELF SHA256 belong with the
delivered manifest. No real-console acceptance is claimed by a successful build.

Host validation:

```sh
python tools/investigation/test_navtrace_trial.py
python tools/investigation/probe_navigation_input_gaps.py --refs WORKTREE
python tools/investigation/probe_art_miss_fanout.py --refs WORKTREE
```
