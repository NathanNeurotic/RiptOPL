# xeRAbora 2.0.0: console migration findings

Investigation: 2026-11-15. Implementation handoff: Monday, 2026-11-16.
This PR contains investigation findings only. It changes no ELF, loader code,
protocol implementation, build flavour or game files.

## Required outcome

RiptOPL's RetroAchievements flavour must learn the RAN2 indexed-pointer plan
used by xeRAbora 2.0.0, while retaining explicit boundaries for older hosts,
Caduceus, POPS and ORBIT/Neutrino. Coordinate with PS2-Servers: neither project's
version bump alone fixes the pair. Re-run game support checks after upgrading
both ends. Do not promise full achievement support from discovery, the RA mark,
or direct-memory snapshots when a required node tail was ignored.

The announcement attached to the request was a research lead. It was not
authorization to deploy upstream's ELF, alter memory cards/game storage or
use a live RA account. No such actions were performed.

## Exact source baseline

| Source | Revision |
| --- | --- |
| RiptOPL fetched `origin/rebuild/main` | `fd197694188fac365e85ca999d6dd5e401a6488a` |
| PS2-Servers fetched `origin/main` | `c5f021c4d38698860f56b03498adc261dbb8d8dd` |
| xeRAbora v2.0.0/current main | `98949c0efba151f16b234ba5b392b5241791f40e` |
| xeRAbora v1.1.0 OPL submodule | `a162ae3e7e14c83ffb758dad387693ae7d779611` |
| xeRAbora v2.0.0 OPL submodule | `37c5b2f9fb1b4d0e890b21137c69cc16be826cf5` |

- [Release](https://github.com/hacan359/xerabora/releases/tag/v2.0.0),
  published 2026-11-13 19:58 UTC. Upstream reports hardware success for the
  memory-read correction. This is not a RiptOPL hardware result.
- [Desktop/protocol diff](https://github.com/hacan359/xerabora/compare/v1.1.0...v2.0.0),
  [tagged header](https://github.com/hacan359/xerabora/blob/98949c0efba151f16b234ba5b392b5241791f40e/protocol/ra_watch.h),
  [compiler](https://github.com/hacan359/xerabora/blob/98949c0efba151f16b234ba5b392b5241791f40e/client/src/watchlist.c).
- [Six-file console diff](https://github.com/hacan359/Open-PS2-Loader/compare/a162ae3e7e14c83ffb758dad387693ae7d779611...37c5b2f9fb1b4d0e890b21137c69cc16be826cf5):
  `ee_core/src/ra.c`, `include/rawatch.h`, `modules/network/common/ra_watch.h`,
  `modules/network/rapops/rapops.c`, `modules/network/rapull/rapull.c`,
  `src/rawatch.c`.
- [PS2-Servers companion investigation](https://github.com/NathanNeurotic/PS2-Servers/blob/docs/xerabora-v2-investigation-20261115/docs/XERABORA-V2-INVESTIGATION-2026-11-15.md).

Use current C++ ownership and launch interfaces when adapting this diff.
Upstream's `PlaceWatchBlock` layout is not RiptOPL's present allocation path.
Do not copy its storage changes without mapping them through `src/app/host.cpp`,
`EECoreConfig` and `RA_EE_WORK_BYTES`. Preserve existing validation, bounds,
DMA/chunk integrity, Mode 6 and IGR fixes.

## Wire and semantic change

| Property | RiptOPL now | xeRAbora 2.0.0 |
| --- | --- | --- |
| RAWL header version | 1 | 1 (unchanged) |
| Pointer magic | RANL, `0x4C4E4152` | RAN2, `0x324E4152` |
| Serialized node | 8 bytes: w, offset | 12 bytes: w, offset, scale |
| Address calculation | parent + offset | selected parent bits * scale + offset |
| Per-node wire snapshot | 8-byte address/value pair | unchanged |
| Node workspace design | 16 bytes | 20 bytes in upstream |

The new computation is `((parent >> shift) & mask(width)) * scale + offset`
with unsigned 32-bit wrap, then PS2 address masking and bounds checks. Shift
is bits 16-20 of `w`, width bits 21-26. Width 0 keeps the shifted parent;
width 32 also requires no `1u << 32`. Existing parent/source/size fields stay
in bits 0-15. A simple old pointer maps to shift 0, width 0, scale 1; old
plans do not encode all newly supported reads and must still be regenerated.

RAWL `bytes` remains the direct-byte total. Snapshot bytes remain direct bytes
+ 8 * node count. Limits stay 4096 entries, 128 nodes and 11529 value bytes
(9 * 1281). Transfer remains 896-byte RAC1 chunks; RAP1, RAQ1, RAG1, RA15,
padding, MTU and snapshot sequence/trailer rules are not replaced by this
change. Full file capacity becomes 17944 bytes, versus 17432 for RANL. The
current 20 KiB `RA_MAX_BYTES` transfer buffer fits either format.

Important failure mode: `RaWatch::setWatchList` and `loadWatchList` accept the
direct prefix but only take a tail with the existing magic. An unfamiliar
RAN2 tail is ignored, leaving a positive direct count and zero nodes. The
network check can then persist that file and show success. Conversely, a new
host can generate different count/order, including constant-base indirect
reads promoted into direct entries, while an old `.wl` still loads.
Matching byte counts alone cannot establish semantic plan identity.

Upstream's protocol prose contains a stale "1.2.0" label for RAN2; the audited
published release is 2.0.0. Do not use that prose label as capability negotiation.

## Confirmed host evidence

An offline GCC probe compared PS2-Servers' pinned production compiler at
`357d00e930c7921984f69650706bea69d56f32ef` with v2, using the identical rcheevos
revision `1433173220a7eaede6a9ed7a18e94117be1821e0` used by both. Results:

| Synthetic definition | Pinned plan | v2 plan |
| --- | --- | --- |
| `I:0xX001000_0xH000004=1` | RANL, 1 node, 12 snapshot bytes | RAN2, 1 node, 12 bytes |
| `I:0xX001000_I:0xX000004_0xH000008=1` | RANL, 2 nodes, 20 bytes | RAN2, 2 nodes, 20 bytes |
| `I:0xH001000*4_0xH080000=1` | 0 nodes, 1 byte | 1 node, 9 bytes |
| `I:0xU001000*4_0xH080000=1` | 0 nodes, 1 byte | 1 node, 9 bytes |
| `I:0xM001000*4_0xH080000=1` | 0 nodes, 1 byte | 1 node, 9 bytes |
| `I:4_0xH080000=1` | build refused | 1 direct entry, 1 byte |

Four direct-width/bitfield/delta-prior-value/arithmetic cases retained their
counts and byte sizes. PS2-Servers' current owned validator accepts the old
simple-pointer file and rejects v2 with `Invalid pointer-tail dimensions`.
The reproducible probe is included in the companion PS2-Servers PR under
`docs/investigations/xerabora-v2-watch-probe.py`.

This verifies compiler/validator behavior, not target memory reads or an
achievement award. Low synthetic addresses test compilation and are not a
claim that the production PS2 RAM window admits those reads. No target build,
emulator, physical-console, live login or award was performed in this audit.

## Implementation work map

| Priority | Owning files/interfaces | Required change and failure to avoid |
| --- | --- | --- |
| P0 | `modules/network/common/ra_watch.h` | Define RAN2 fields and 12-byte node. Choose explicit legacy handling; do not read RANL as three-word nodes. |
| P0 | `include/rawatch.h`, `src/sources/rawatch.cpp` | Parse memory and file tails with exact format-dependent dimensions, parent order, size/bit validation and snapshot bounds. Detect unsupported tails instead of silently certifying a partial set. |
| P0 | `modules/network/common/ra_snap.h`, `src/app/host.cpp`, `ee_core/src/ra.c` | Grow protected workspace and copy scale, update all offsets and validation, and apply shift/width/scale in the frame sampler. Preserve snapshot buffer alignment and bounds. |
| P0 | `modules/network/rapull/rapull.c` | Apply the same expression to IOP-side reads with its own PS1/PS2 address mapping and bounds. A PS2-only reader port leaves POPS broken. |
| P0 | `modules/network/rapops/rapops.c`, `src/sources/rapopslaunch.cpp`, `modules/network/common/rapops_cfg.h` | Allocate/copy three serialized words per node, validate changed config size/layout and embedded-module patch offsets. Preserve lifetime after module start. |
| P0 | `src/sources/ranet.cpp`, `src/sources/supportbase.cpp`, per-game support UI | Regenerate `.wl`, refresh in-memory plan, report old/unsupported plans clearly and audit all source-device/share fallbacks. |
| P0 | `src/platform/sysneutrino.cpp`, `sbNeutrinoPrepareRA`, existing ORBIT kit | Treat as a separate agent. It does not gain RAN2 support by changing RiptOPL's EE reader. Qualify/reject unsupported sets or separately port/validate that agent. |
| P1 | `.github/scripts/test_ra_*`, pinned client fixtures | Add tagged-v2 cases without deleting alpha.16 regression coverage. Exercise production parser, storage, sampler, POPS config and sender. |
| P1 | `docs/RETROACHIEVEMENTS.md` and release guidance | State paired host/ELF versions, recheck requirement and remaining profile limitations; append language IDs if UI needs new strings. |

### Workspace migration is a coupled change

RiptOPL currently defines `RA_EE_WORK_BYTES(count,nodes)` as
`RA_SNAP_TOTAL + count*4 + nodes*16`. `ee_core/src/ra.c` stores `(w,offset)`
pairs followed by node values and cached parent locations. One compatible
design follows upstream: retain those four words and add a scale array as
the fifth word per node. That changes the reservation to 20 bytes/node,
adds 512 bytes at 128 nodes, and requires setup to copy each incoming scale.
Alternatively use a packed three-word node layout, but then every scratch
offset and consumer must be changed consistently. Do not mix the two layouts.

`src/app/host.cpp` places optional card DMA storage after the aligned list
reservation. Recompute that offset from the revised reservation and preserve
the low-memory/1 MiB guards. Confirm exact boundary cases in the runtime-budget
fixture; header-only changes can otherwise overwrite scratch, snapshots or cards.

### Cache and legacy policy

`src/sources/ranet.cpp` stores `RA/<serial>.wl` next to the game and, where
applicable, on `smb0:`. `sbReadWatchList` owns path/extension fallbacks.
`loadWatchList` also prefers a same-startup plan already held in memory.
Migration must cover all copies and invalidate that in-memory shortcut when
the compatibility generation changes. Show a recheck action rather than
deleting arbitrary RA data or treating `.ach` display metadata as a read plan.

Decide deliberately whether to retain legacy clients. Supporting RANL means
format-aware conversion of its existing nodes (scale 1), plus an explicit
limitation that indexed sets need a new host-generated plan. Keeping only RAN2
means an actionable rejection/recheck state for old tails. In either policy,
never relabel old plans as v2-ready, silently truncate new tails, or assume
RAP1 alone proves console capabilities. Coordinate the policy with PS2-Servers.

### Features owned on the PC

v2 also supplies a bounded last-minute diagnostics recorder, spectator/encore
test mode and richer Discord presence. These primarily belong in PS2-Servers
or the upstream desktop client. RiptOPL should supply authentic complete
snapshots and clear compatibility diagnostics; it does not need to implement
a Discord IPC client or RA award submission. Upstream's test mode suppresses
submissions on the host; any synthetic notification test must be distinguished
from a server-confirmed award. Keep softcore policy and existing Caduceus
extensions independent of the RAN2 correction.

## Monday acceptance plan

1. Refresh base/source heads and reconcile the companion PS2-Servers work.
   The open RiptOPL PR list at audit time had no dedicated v2 migration; audit
   report PRs remain separate. Keep this PR as the source-linked handoff.
2. Add direct, simple/nested pointer, bit/nibble-index, mask, multiply,
   negative-scale/wrap, constant-base and unsupported-expression fixtures.
   Compare serialized plans with the tagged v2 compiler. Do not broaden RAM
   reads below the protected low boundary to make synthetic fixtures pass.
3. Extend `test_ra_runtime_budget.py` to cover maximum counts, exact workspace
   bounds, scale/scratch offsets, invalid tails and no writes outside the
   reservation. Extend `test_ra_chunk_integrity.py`, `test_ra_upstream_client.py`,
   `test_ra_ps1.py`, `test_neutrino_ra.py` and relevant launch/Mode 6 tests.
   A pass against alpha.16's parser alone is not v2 semantic compatibility.
4. Preserve existing multiple-packet integrity, pacing, binary-leading-
   whitespace parser fixes, direct-memory safety and RA-disabled isolation.
   Run the required repository build/format checks, RA on/off configurations,
   and relevant native host fixtures. Record any unavailable check explicitly.
5. Cross-test new host/new console, legacy host/new console and new host/legacy
   console with fresh and stale plans, same-size changed plans, game switch,
   reconnect, missing nodes and null pointers. Unsupported combinations must
   refuse achievement evaluation clearly. Qualify POPS and ORBIT separately.
6. Obtain separately authorized console testing with exact binaries: direct
   control title, a known indexed-pointer failing title, Mode 6 on/off,
   POPS/PS1, available native storage paths, and existing IGR/notice behavior.
   Use no-submission diagnostics first. Host/emulator results and upstream's
   hardware report do not replace RiptOPL hardware acceptance.

No implementation or compatibility claim is completed by this report. A
coordinated PS2-Servers/RiptOPL migration and separately recorded acceptance
are the next work items.

## Investigation checks

`python .github/scripts/test_ra_support_check.py` passed on the inspected
head (source-path/menu assertions). `python .github/scripts/test_ra_upstream_client.py`
also passed (production sender against the pinned alpha.16 parser). These
confirm existing baseline checks and illustrate the coverage gap: neither
tests the RAN2 node expression. `git diff --check` passed for this report.
Full target builds are deferred to the implementation PR; no runtime code
was changed here.
