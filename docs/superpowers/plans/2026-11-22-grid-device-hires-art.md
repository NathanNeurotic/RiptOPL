# Implementation plan: Grid, vertical Coverflow devices, and HIRES art

Design: `docs/superpowers/specs/2026-11-22-grid-device-hires-art-design.md` (approved with the Grid highlight/X amendment).

## Task 1. Navigation regression controls

- Extend `tests/host/test_grid.cpp` with first/last-row wrapping, short final rows, one-row lists, and the legacy no-wrap choice. Run it before implementation and retain the failure as the control.
- Add a host-compiled menu-input harness that extracts the production functions. Use visible linked pages with callbacks that count activations and game launches. Prove:
  - Grid Up/Down changes only its highlight.
  - Grid Cross activates an inactive highlight once, closes the list, and never calls `execCross`.
  - Grid Right/Circle on an active page leaves without activation; reopening starts from the active page.
  - Coverflow Up/Down directly calls previous/next device navigation and needs no focus flag or Cross.
- Add rendering coverage proving Coverflow uses the vertical device renderer while non-Coverflow PSBBN Flow keeps its existing bar.

Files: `include/grid.h`, `src/ui/menusys.cpp`, `src/gfx/grid.cpp`, `src/gfx/themeelems.cpp`, `src/gfx/themes.cpp`, `include/themes.h`, `tests/host/test_grid.cpp`, and a focused `.github/scripts/` harness.

## Task 2. Implement navigation

- Add the explicit wrap option to `gridRowStep`; use it only for sidebar-enabled Grid pages.
- Add a separate Grid highlight pointer, visible-page highlight movement, and a shared device-page activation helper used by existing previous/next navigation. Consume all sidebar input before game actions.
- Generalize Grid's vertical device drawing to accept active/highlighted pages. Use it for Coverflow, remove the bottom-bar focus state/input, and map Coverflow Up/Down directly to device navigation.

Run the navigation controls and existing Grid/Coverflow suites after this step.

## Task 3. HIRES regression controls

- Add `.github/scripts/test_hires_texture_ownership.py` (and register it with the repository's script runner) to compile the production texture manager and frame-end logic against a bounded fake VRAM/DMA model.
- Cover: direct upload with immediate source-pixel release, outgoing VRAM protection, explicit unload/rebind using a distinct block, starvation retaining the previous queue, recovery on the following frame, ordinary-mode immediate behavior, and shutdown release.
- Run against the current code to demonstrate premature reuse; do not weaken the control to make it pass.

## Task 4. Implement HIRES ownership

- Add a project-local adaptation of gsKit's texture manager with upstream attribution, current/previous-frame protection, explicit-unload reservation, and an allocation-starvation result rather than an unbounded eviction loop.
- Add renderer-owned texture release for CPU pixels/CLUTs. Select `ETM_DIRECT` for HIRES so uploads leave no inline DMA reference to source pixels, then free pixels/CLUTs immediately without deferred-release metadata. Convert existing paired unload/free call sites without changing art-worker rules.
- On starvation, reset only the unfinished HIRES queue and retain the displayed queue. If retries still cannot fit, complete one degraded frame without new uploads so expired blocks become reclaimable instead of retrying forever. Keep non-HIRES execution unchanged.
- Add the new source to `ps2.yaml` and verify archive link resolution in both toolchains.

Files: new `src/gfx/texmanager.cpp`, `src/gfx/renderman.cpp`, `include/renderer.h`, `include/renderman.h`, texture-owning call sites, `ps2.yaml`, and the ownership harness.

## Task 5. HD background preparation

- Post-process successful per-game backgrounds on the art worker only when the active HIRES framebuffer is 16-bit.
- Bound PNG and JPG backgrounds to 720x512 in those modes, use smooth area/bilinear resampling, and convert CT24 and CT32 backgrounds to CT16S before publication. Preserve indexed T4/T8 backgrounds in their native formats. Leave 24-bit video-mode art and all GS/VRAM operations out of the worker.
- Add known-image resampling/format tests for RGBA and indexed backgrounds and rerun art cache/queue suites.

## Task 6. Verification and delivery

- Run focused tests, all host suites, relevant `.github/scripts/` tests, and `tools/analyze_coupling.py --check` with the safe temporary/Git-ceiling settings.
- Run the standard LLVM build and GCC/RetroAchievements build sequentially; inspect resulting worktree files and retain build logs in the recovery directory.
- Exercise rapid Grid navigation, Coverflow direct device switching, and real per-game art at 1080i in PCSX2. Compare against the supplied recording and distinguish remaining interlace shimmer from corruption. Do not claim hardware acceptance.
- Review the diff, remove temporary outputs not needed for delivery, and keep code and tests on #487 (`docs/psbbn-release-refresh`) while this documentation branch contains only README.md and docs/** changes. Deliver source documentation on #491 (`docs/psbbn-release-guides`) and the generated site on #488. Merge #487 before #491 because the guides reference its behavior, then #488. Watch all relevant checks and do not merge without explicit authorization.
