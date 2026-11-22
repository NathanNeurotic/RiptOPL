# Grid, device activation, and HIRES art repair

## Intent

Fix the three reported GUI problems without removing the accepted ambient animation or per-game backgrounds, and without changing game-launch behavior.

## Goals

- With the Grid device sidebar enabled, Up from the first row selects the same column in the last row; Down from the last row selects the same column in the first row. A short last row resolves to its last cover. Themes without the sidebar retain their existing device-page navigation at the Grid edges.
- Grid keeps a separate device-list focus because its covers use all four directions. Inside that list, Up/Down moves a highlight without changing the active page. Cross activates the highlighted device only when it is inactive and returns to the covers; on the already-active page it simply returns to the covers. Right/Circle also leaves the list without requiring Cross. No device-list action calls `execCross` or launches a game.
- Coverflow uses a left-side vertical device list like Grid instead of a bottom horizontal bar with a separate focus mode. Up/Down changes the selected device immediately; Left/Right remains with the covers. No Cross press or extra navigation step is needed to activate the device page, and no device action launches a game.
- In 1080i and other HIRES modes, changing art must not release CPU pixels or reuse VRAM still referenced by the outgoing DMA queue. If a complete new frame cannot fit, the last complete frame remains on screen instead of displaying mixed or corrupt bands.
- Reduce HD background upload/VRAM pressure and improve JPG downscaling quality. Keep the accepted ambient effects and page transitions.

## Non-goals

- No physical-console claim; emulator comparison is separate from hardware acceptance.
- No native-resolution art upgrade in this change, no game-launch remapping, and no changes to review-only PRs #485/#486.
- No merge of #487 or #488 without separate explicit authorization.

## Root cause and evidence

gsKit HIRES alternates two draw queues and replays the outgoing queue across multiple passes. Its inline texture uploads contain DMA references to EE pixel buffers. `cacheClearItem` currently unloads and frees those buffers immediately, and gsKit can reuse the old texture's VRAM for a new image while the outgoing queue still needs it. The supplied recording shows horizontal bands containing different UI/art states during navigation, matching this ownership violation. Enlarging source images alone would not repair it.

JPG backgrounds are independently capped at 720x512 and reduced with nearest-pixel sampling, which causes avoidable aliasing before the 1080i upscale.

## Approaches considered

1. **Chosen: preserve animated game art and repair ownership.** Add two-generation texture/VRAM protection with backpressure, deferred pixel release, smaller HD background textures, and smooth resampling. This retains the requested appearance at the cost of a focused renderer change and a project-local gsKit texture-manager adaptation.
2. **Static HIRES background.** Disable only per-game background changes in HIRES while retaining theme art and ambient effects. Smaller, but removes behavior the user explicitly chose to keep.
3. **Flicker filtering alone.** Rejected: it can reduce interlace strobing but cannot correct DMA/texture lifetime or VRAM reuse.

## Design

### Navigation

Extend the Grid row-step helper with an explicit wrap choice and test both directions, full and short final rows, single-row grids, empty lists, and invalid indices. `menuGridRow` enables wrapping only when `gridDevices` is active.

Keep a Grid device-highlight pointer separate from `selected_item`, which remains the active page. Opening the sidebar initializes the highlight to the active page. Up/Down moves only the highlight through visible pages. Cross activates an inactive highlighted page through the same publication path as previous/next device navigation, then closes the sidebar; on an already-active page it only closes the sidebar. Right/Circle closes without activation. Device-list input always returns before the main-page `execCross` handler.

Replace Coverflow's bottom horizontal device bar and `gCoverflowDeviceBarFocused` mode with the same left-side vertical presentation used by Grid, adapted to the existing device-bar theme elements where practical. While the Coverflow page is active, Up/Down calls the existing previous/next device-page actions directly and Left/Right continues navigating covers. Remove the enter/exit-focus branches rather than adding another activation mapping. Device changes still must not invoke `execCross`.

### HIRES ownership and backpressure

Adapt gsKit's texture manager locally, preserving its upstream copyright and public behavior for ordinary modes. Track frame use and protect blocks used by the current and outgoing HIRES queues. Explicit unloads reserve their old VRAM until that outgoing queue retires. A rebound texture receives a different block rather than overwriting the old one.

If no safe VRAM block is available, allocation reports starvation instead of evicting a protected block or looping. `rmEndFrame` then waits for the HIRES boundary, resets only the unfinished new queue, and does not flip. The previous complete queue continues displaying. On the next frame, expired blocks become reclaimable and drawing retries. This favors a short pause over corruption.

Replace paired unload/free calls with a renderer-owned texture-release operation. In HIRES it queues CPU pixel/CLUT release until the corresponding queue is retired; ordinary modes retain immediate release. Mode shutdown flushes deferred releases after the HIRES DMA thread is drained. No art-worker GS/VRAM calls are introduced.

### Background memory and scaling

For HIRES modes whose framebuffer is already 16-bit, post-process per-game backgrounds on the art worker into CT16S, halving their pixel memory and transfer footprint without changing the display's output precision. Bound backgrounds to the existing 720x512 budget for both PNG and JPG in those modes. Resample with smooth area/bilinear filtering rather than nearest-pixel selection. Do not perform GS calls on the worker and do not alter ordinary 24-bit video-mode art.

## Verification

- Add failing-first host tests for Grid wrapping and real menu-input/render harnesses proving Grid Cross activates only the device page, Coverflow Up/Down directly changes devices with its vertical list visible, and neither path launches a game.
- Compile the production texture-manager adaptation against a small VRAM model: outgoing pixels/VRAM survive a replacement, starvation leaves the old queue displayed, and the next frame can complete. Include a failing control using the old behavior.
- Exercise smooth resampling and CT16S background preparation with known image data; verify non-HIRES behavior is unchanged.
- Run focused host/script suites, then the full repository suite, coupling gate, standard LLVM build, and GCC/RetroAchievements build sequentially.
- Compare 1080i navigation with real per-game backgrounds in PCSX2, including rapid Grid page changes and theme changes. Record any remaining interlace-only shimmer separately from corruption; do not describe emulator results as physical-console acceptance.
- Recheck PR #487 CI after push and confirm the generated-site companion #488 remains clean. Do not merge either without explicit approval.
