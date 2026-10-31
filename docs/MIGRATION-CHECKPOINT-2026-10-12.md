# RiptOPL SDK/C++ migration checkpoint

## HDD mini row resolution continuation (2026-10-20)

#74 merged as `15c440a5` after exact `72118cb5` head passed 46 checks/statuses,
the expected PR publication skip, completed CodeRabbit coverage and manual
source review. No actionable inline comments or unresolved review threads;
[techwritescode](https://github.com/techwritescode) reviewer request verified. This branch is based on that merge.

Adapt original #1013 / `5aa7e0df` through C++ `HddSource::launch`: resolve an
unnamed autolaunch row's data LBA through the existing read-only HDL listing
before either core. Copy the row and preserve the launcher startup already
used to load CFG. Zero, missing or unreadable matches refuse the handoff and
retain the existing mini-session cleanup. Menu and named rows add no scan.
The cold mini path adds one existing directory/header walk, no fixed wait,
disk write, worker, cache or new public interface.

The actual C++ selection/Neutrino copy fixture fails against the old empty
partition path and passes matching-row metadata, startup preservation,
empty/partial-error lists, refusal/cleanup and menu no-scan cases. Existing
autolaunch settings and parental-lock fixtures and the coupling gate pass.
Native target/matrix CI remains pending; no console result claimed.

Newly identified separate gap: original `1e321048` passes the real media sector
count in the common CDVDMAN settings. Migration still reads that bound from
the ISO PVD and lacks the common field. Port all producers/consumers and HTTP
layout assertions together; do not fold an ABI change into this row resolver.
The earlier #1011 damaged-tail repair, #1012 sound and #1014 PFS VMC follow-ups
remain open, together with SDK MMCE EOF status and console issue #63.

## Reset integration and Neutrino memory-card writes (2026-10-20)

#72 is merged as `d50a9c24`; #73 as `2d097b91`. The latter exact head
`374e2d72` had 48 successful checks/statuses and the expected publication skip.
Manual review verified installer idempotency, Mode 6 exclusion, failure cleanup
and missing-patch-only scanning. No unresolved review threads or objections;
[techwritescode](https://github.com/techwritescode) was requested. CodeRabbit was rate-limited and did not review
#73. The repository has no required-review rule; the manual review and tests
established readiness, correcting older comments that treated bot quota as a gate.

Original main advanced to `2082cac3` with #1011 Neutrino memory-card TOML writes,
#1012 menu one-shot sound replacement, #1013 HDD mini autolaunch resolution and
#1014 PFS VMC allocation/committed inode mapping. These need separate adaptation.

This change ports only #1011's two write flags into C++ `sysneutrino.cpp`:
`O_CREAT | O_TRUNC` with mode `0666`, including short-write restoration.
No new parser, allocation, worker, retry or launch delay. Existing matching-IP,
missing-file and read-only behavior remains. The actual C++ function fixture
fails on the previous source and passes MC/FAT shrink/grow, paths, no-op,
descriptor-zero and failure/restore cases. Existing bsdfs, argument-editor,
argv-budget and DVR fixtures and the unchanged coupling check pass locally.
Exact-head target/host CI remains to run; no console acceptance is claimed.

Automatic repair of old damaged TOML tails remains pending. Keep that separate
from this prevention fix and verify preservation of all user-authored settings.
Other remaining work includes the new original HDD/PFS/sound fixes, IGR worker
request-latch parity, SDK MMCE EOF package status, and console issue #63.

## Native VMC layout and retry safety (2026-10-20)

Reconciled remote integration at `796eb835`: #57/#59/#60/#62/#67 and the
native BDM alignment fix #70 are merged; there was no remaining open PR queue
at this audit. Issue #61 is closed and the published releases use the persistent
changelog/download format. The main compiler, host and release workflows passed.
Do not reuse the older pending-review queue descriptions below as current state.

Original reference advanced to `a23a6d77`. Adapt `fc0aba7b`'s native VMC
extent validation to C++ `BdmLauncher` and `HddSource`: require written LBA and
fragment outputs, a single extent covering the whole card, matching start,
and a last sector within the native core's 32-bit address space. Mapping
failures and PFS block-map capacity errors have separate appended messages.
Rejected cards remain inactive; no card is created, resized, deleted or moved.

The embedded module patch markers are overwritten by their first slot write.
Retain bounded offsets for BDM/PFS so retries can clear an old card after
mapping failure or removal. Every offset must leave room for the complete
slot payload. This port does not enable other storage families or change
Neutrino VMC arguments, the SDK driver, or the launch core selection.

Actual production launch blocks are exercised in the C++ host fixture, including
both slots, manual continue/cancel, autolaunch, missing/partial mapping output,
fragmentation, mismatched/short extent, address boundaries, invalid cards,
PFS error classification and repeated valid-to-invalid/absent transitions.
All 85 Python host checks, HTTP tests and seven C++ suites pass. Final focused
ASan/UBSan and language-placement checks also pass; local leak detection is
disabled due to the existing sandbox ptrace limitation, while CI retains normal
sanitizer behavior. The unchanged coupling-baseline check passes. Local native
ps2build LLVM Korium links and packs (working tree based on `796eb835`, before
this commit; dirty builds are not published): ELF SHA-256
`a239663df106391c9df4fcda1221c1f5de6d2074d2f7a8bd01ac6882398def8c`.
Exact-head GCC/LLVM/matrix/package CI and review remain gates before merge.

Remaining priorities: installed-ps2build FAT walker evidence and any resulting
maintainer report; current original IGR VBLANK/IOP-reset lifecycle changes
(the original alarm design has since changed again); MMCE module/EOF fixes;
new title defaults; native iLink/Neutrino/logo and APA POPStarter console cases.
Source, host tests, native builds and physical console acceptance remain separate.
No new SDK deficiency or hardware success is claimed.

## Native BDM read alignment continuation (2026-10-19)

Port only `DeviceReadSectors` from original commit
`35af507b6a78a10c6b567c70e067742cf59097cf` (upstream OPL #1783).
Aligned requests retain the direct multi-sector call. Unaligned requests use
a static 64-byte-aligned 2 KiB buffer under the existing semaphore, copying
only complete sectors and returning the existing read error on a failed call.
No heap allocation, mode mapping, quickboot, timing or public API changes.

The actual-function regression fails against the previous source on its first
unaligned driver call. The port passes offsets 0-3, multi-sector data and guards,
direct-call counts, zero/partial failures on each bounce read, detached devices,
zero-sector requests and semaphore balance under ASan/UBSan. Local LSan cannot
run under the sandbox's ptrace; local runs disable leak detection only. CI keeps
the normal sanitizer settings. Compiler, matrix, package and publication results
must be recorded against their exact commits separately.

This is a reproduced alignment contract gap, not proof of FifthFox's iLink
failure cause. #63 remains open for matched console tests; native iLink module
initialization/binding/raw reads, MMCE Neutrino modes, APA POPStarter and each
loader's logo path remain separate questions. See the hardware report for the
comparison procedure. PRs #62/#60/#59/#57 retain their independent scopes.

## Current continuation: DVR external-core launch contract

Base: merged #56 at `b56e0b19`; original comparison remains `eab6c37c`.
Adapt the safely host-testable launch subset of #947/#955 through the C++ host
UI interface: exact DVR backend mapping, companion-file preflight, no automatic
quickboot/logo/raw-filesystem override, and bounded argv composition. DVR also
rejects active user backend overrides before teardown, avoiding a last-token
replacement of its filesystem contract. Existing argument-scope behavior stays
intact. Actual-method preflight tests cover refusal paths and descriptor cleanup;
the actual argv builder verifies the DVR filesystem tuple.

This does not enable discovery, load drivers, mount partitions or expose DVR
game rows. The later GameSource/platform port must preserve read-only discovery
and mount ownership, use a matching DVR-capable companion and retain a hardware
acceptance gate. Installed SDK `dvr`/`dvrfile` packages exist; absence of a custom
PCCF package is not a confirmed SDK deficiency. See docs/DESR-DVR.md.

Validation for this checkpoint: all 78 Python host tests, the HTTP tests and
seven C++ host suites passed. Native ps2build LLVM RetroAchievements linked,
stripped and packed successfully after language regeneration; its ELF SHA-256
is `73de2ea70739a2d8effec968c596cab3e41ccce750b2818467fbe596a4f7d789`.
This is local build evidence, not console acceptance. GitHub checks and review
must complete before merge. Original preflight at `eab6c37c` was exercised with
the same file mocks: `-qb` returned -1 with no opens; active `-bsdfs=bd`
returned 0 after four companion opens. The new preflight refuses both.

PR #57 CI follow-up: GCC and LLVM compilation succeeded, but both jobs failed
the coupling gate because the DVR missing-file messages called `_l` directly
from the platform layer. Added `UiServices::missingFileId`, implemented by the
application UI and mirrored in host fixtures, so translation stays above the
platform layer. The coupling baseline is unchanged and its check now passes;
DVR, filesystem-preflight and argv tests pass with the interface correction.
CI must rerun on the follow-up commit; the native SHA above predates this fix.

The UI-service revision `828507c5` also passed native LLVM RA build (ELF
SHA-256 `ae694e359044bcaa4cecd2d6b6650f38a59e7554d7a1bb9e8ead8df1e3528292`).
A full host rerun exposed an existing release-note fixture timing race: Git
commits with equal timestamps can omit the branch fix from the ten-entry
history. The fixture now assigns distinct ordered dates; release-note
production behavior is unchanged. Repeated fixture runs and full-suite rerun
are required for this follow-up.
## Current continuation: original #975 POPS module absence guard

Branch `codex/pops-module-absence-975`, based independently on merged #56
`b56e0b19`. Original advanced to `b35ff38c` (merged #973 and #975). Adapt #975
through the C++ all-USB-volume POPS check: ENOENT remains ordinary absence;
only EINTR invokes directory confirmation. Found modules (case-insensitive),
real IO errors, unreadable directories, enumeration errors and close errors
remain guarded. No files are deleted. All 77 Python host tests, HTTP tests, seven C++
host suites and the unchanged architecture gate pass. Native LLVM RA passes
(ELF SHA-256 `64ba8b7f60ef7a0e41b29f29acc99fe2ac292bfc2beaf8ab537897a3f0af2cc5`).
The expanded fixture fails on the prior C++ implementation at the ambiguous
missing-module case and passes with this guard.

Expanded actual-function POPS fixture passes 132 config/rollback/error checks
plus source wiring checks. This emulates ambiguous errno behavior and does not
prove the installed SDK leaks that error. No new SDK deficiency is confirmed.
The new #973 IGR alarm/input-starvation port remains a separate task; audit its
callback generations and teardown against the current migrated hooks first.
## Current continuation: safe IGR installation from original #973

Independent branch `codex/igr-install-failure-973`, merged #56 base `b56e0b19`.
Source reference `b35ff38c`. Port only installation safety: initialize the entire
EE thread descriptor, never start an invalid thread, discard a thread whose
start fails, retry registration without creating duplicate workers, and enable
VBLANK only for a successfully registered handler with a running worker.

Expanded actual-function lifetime tests pass in standard and RA modes with
creation, startup and registration failure injection. All 77 Python host tests, HTTP tests and seven C++ suites pass. Native LLVM
Korium and RA both pass; ELF SHA-256 values are
`aa695ab01b0b93df4c9a0b8eb51204f4da8cabf26fe8ec5ab6cd29ebc5d2fdfb`
and `c8df6515875c22d398821cabdb45854bfb2867159d4dc8a80fbe6d5c05336e0b`.
The unchanged architecture coupling gate passes. No ABI, sampling or shutdown-context change;
standard builds still use VBLANK. The remainder of the #973 alarm port needs its
own callback-generation/reset/RA-context review. No SDK deficiency identified.
## Current continuation: EE syscall-hook GP/register context

Branch `codex/ee-hook-gp-context`, merged #58 base `1c4822fc`. Adapt original
`b35ff38c`'s three syscall-hook C-entry contexts while preserving migration
assembler compatibility and unrelated hooks. Returning hooks restore caller
context; the DMA trampoline preserves C success/failure and ExecPS2 retains
its deliberate non-returning register scrub.

The instruction-level regression fails on the old DMA C-entry GP and passes
with the change. It models delay slots, ABI-width register clobbers, stack
bounds, bypasses, forwarding and return values; it is not R5900 execution.
All 79 Python host tests, HTTP tests and seven C++ suites pass. Local
working-tree LLVM Korium/RA assembly and linking pass, with ELF SHA-256
`a44316bc0e0f34d1ec21d5554fa4e1fac82f3faf5f883474d6aa260835f83440`
and `216cc08776c4d69caea50430d596c58bbe735bc86b134307682ec2235a47bb8b`.
The unchanged architecture coupling gate passes. PR-head CI remains required.
No SDK deficiency or console result claimed. Alarm callback generation,
registration/cancellation, allocation recovery and sampling remain separate.
## Current priority: first hardware launch report

Track https://github.com/NathanNeurotic/RiptOPL/issues/63 and
`docs/HARDWARE-FIRST-REPORT-2026-10-18.md`. Nathan identifies the test build as
likely latest rolling Korium; exact binary and original comparison remain unknown.
Native OPL/iLink launch, Neutrino compatibility and non-USB POPStarter remain
reported, not confirmed fixed. Faster UI is not proof of a timing cause.

Confirmed independent defect: the iLink BDMA signature expects 23452 bytes for
usbhdfsd while our shipped file is 21609 bytes. Correct the constant and validate
all six real shipped sizes through both production validators. Old code fails,
corrected code passes under ASan/UBSan. Wrong-family/marker/missing/truncated
pairs remain refused. Retain the previously accepted 23452-byte iLink pair
only for iLink with its matching 48500-byte USBD, to avoid invalidating existing
card installations when fixing shipped-file recognition. Existing POPS config/rollback/wiring checks pass.
This is not a native PS2/iLink fix or a hardware-confirmed POPStarter fix.
No SDK deficiency identified. Full target builds remain a PR-CI gate.

## PR #58 review follow-up

Address CodeRabbit review 5562101248: horizontal tab navigation now takes
menuSemaId across selected-row/invalidation updates. The input path does not
already hold that lock; folder reset does not reacquire it, and IO enqueue uses
the queue's short-lived end semaphore rather than its processing semaphore.
The prefetch fixture now executes its recorded callback, exercises the waiter
callback, and checks lock ownership, hidden-tab skipping and generation changes.

Python syntax validation passes. Local C++ reruns encountered WSL service
E_UNEXPECTED before test execution; do not claim those runs passed. Linux CI
must validate this follow-up before merge. Prior revision's full local host and
native results remain historical evidence only.

## Current continuation: browse-config generation ownership from original #959

Independent branch `codex/config-prefetch-generation`, based on merged #56
`b56e0b19`; pending DVR PR #57 remains separate. Original comparison is still
`eab6c37c`. Adapt #959's remaining single browse-worker latch and latest-target
loop through C++ GameSource and scoped semaphore locks. Reads run outside the
menu lock. Publish requires the same generation, owning list and selected row;
row/tab invalidation and launch ownership changes invalidate older reads.
Foreground settings remain direct rather than being sent back to the IO queue.

The actual-method ASan/UBSan fixture covers repeated requests, row/tab changes,
A-to-B-to-A changes, explicit invalidation, stale cursor reads, queue refusal,
read failure/retry and dialog-waiter isolation. The old implementation fails
this fixture; the adapted worker passes. Native LLVM RA passed (ELF SHA-256
`96e0c63ebe59d896b850ba53ad70bbe2c244ccc31057e30802fd62ea8e1346d7`).
The first full host run exposed the VMC extraction fixture missing the new
generation state; its fixture declaration is updated. Final validation passes all 78 Python
host tests, HTTP tests and seven C++ host suites. The coupling gate passes
without changing its baseline.
This does not establish device latency, menu shutdown ownership, full #959
assembly parity or console acceptance. No SDK deficiency has been identified.

## Current continuation: low-memory config/file IO from original #971

Source comparison: original `eab6c37c`, destination `82a11663`. The local release
review edits were superseded by merged #47 and preserved in recovery commit
`3de7c3de`; they were not applied over newer migration work. Destination #48-#55
already contain fades, config allocation safety, SIO2MAN/parental/logo-art ports,
VMC budget/reuse, CJK fallback, type labels and About/QA layout.

The next missing safety boundary is upstream #971: allocation failures must not
truncate an existing config, buffered close must preserve the first real errno,
and failed font/file reads must close descriptors. Adapt its four file-IO methods
to the C++ source layout and skip unused MC/PFS verification snapshots, retaining
ConfigSet, MC single-open behavior and existing failure recovery. The adapted
fault-injection harness compiles the actual C++ bodies and fails on the old
implementation. This is not a claim about the cause of a reported hardware error.

The refreshed parity ledger records newer upstream work and remaining DESR/CF,
RA/client/ORBIT/HTTPS, SaveIcon, docs/defaults and hardware acceptance tasks.

Release publication is enabled for integration pushes to `rebuild/main`:
`rolling-korium`, `rolling-ra`, and `rolling-incebtion`, all using ps2build/GCC.
PRs remain previews. Manual dispatch with `publish=false` remains verification-only;
`publish=true` publishes. Publication verifies all three kits and matching SDK
suites, remote asset digests, current integration head and final tag targets.
Passing builds and published development releases do not establish console acceptance.

## Current continuation: foreground settings and Favorites bus policy

PR #45 merged at `cc46de23`. Its current-head CodeRabbit review completed
without actionable findings, all candidate gates passed, and all 35 downloaded
candidate packages independently verified. Final integration evidence is tracked
separately in issue #20; all 35 final packages now independently verify against
the merged SHA. Final compiler, host, configuration, kit and link-sync jobs pass.

Adapt another bounded subset of merged original #959 (`2b838e09`): settings
configuration uses the existing foreground C++ GameSource load instead of the
browse settle gate and shared IO worker. Flush pending pad rumble before storage
work. Favorites resolve the selected row's source mode for MMCE image-load
cancellation and APP/MMCE browse idle thresholds, while keeping Favorites row
IDs and cache ownership. Actual-method sanitizer tests cover immediate uncached
settings, cached returns, failed-read retry, duplicate/stale result disposal,
invalid selection and device policies, with storage reads outside menuSemaId.

This does not complete #959. Async config generation/deduplication, graceful
handling of a genuinely failed/null config in settings consumers, and assembly
register/GP preservation remain explicit work. No console-freeze cure is claimed.

## Historical continuation: EE hook installation versus IOP buffer lifetime

Base: merged navigation #43 at `f9d33e93`; final compiler/host/32 configurations
and three kits pass, with all 35 final packages independently verified.
Pages #44 is deployed at `32405d06`; Wiki defaults update is published at
`de2a3c1`. Final evidence is recorded in issue #20 and the respective PRs.

Original #959 at `7af8845b` (subsequently merged at `2b838e09`) exposes a second lifetime distinction: an IOP
reset ends the pad DMA producer, but does not erase the installed EE PadOpen
patch. Frozen input frames likewise do not mean that patch disappeared.
The destination now retains the install latch across both events, while still
invalidating the DMA pointer on reset. Executing pad-open callbacks marks the
patch installed. Existing ExecPS2 scanning remains the loaded-ELF boundary.
The actual patcher flushes data then instruction caches only after patch writes.
Its shared masked scanner now rejects empty/short ranges before unsigned
subtraction and includes the final valid window. The original helper fails the
last-window assertion and produces an ASan heap-buffer-overflow for a short
allocation; the real patcher now handles a pattern ending at the scan boundary.

Standard/RA actual-method regressions cover frozen frames, retained/missing
install flags across successful/failed resets, callback reacquisition and the
real patcher's writes/cache-flush order. Old methods fail behavioral assertions.
This branch retains interrupt-driven input handling; the upstream polling-worker
KSU change does not apply directly. #959's register-preservation and Favorites
configuration changes remain separate audits; no complete #959 parity is claimed.
No SDK deficiency or physical-console/RE4 causal acceptance is claimed.

## Historical continuation: page-key animation and verified defaults

Base: IGR lifetime PR #42 merged at `9dc339bdc62021c54dfa295c6783af8673cb5ecb`.
Its candidate compiler/host/32 configurations/three kits pass and all 35
candidate packages verify. CodeRabbit completed review with no actionable
comments; [techwritescode](https://github.com/techwritescode) reviewer request was verified, no human approval claimed.
Merged-source CI/package evidence belongs in issue #20.

Adapt merged original #952 (`0cf08cc1`) through C++ menu navigation: page keys
animate Coverflow only on a changed selection, including both wraps. Flat-theme
paging and existing page targets remain intact. The actual C++ methods run under
ASan/UBSan, including empty/single-item and short-page cases, and in migration CI.
The regression fails against pre-adaptation methods.

Adapt original #953 documentation against actual C++ settings rather than copy
its defaults: fresh theme is `<OPL>`, while this branch retains widescreen/audio
on and carousel center scale 30 (original: 4:3/audio off/scale 45). Correct README
and theme guidance; add [DEFAULTS.md](DEFAULTS.md). These three runtime-default
differences remain explicit unfinished reconciliation; no runtime default changes.
Original production refreshed to `0f2b5e69`. Full RA, UI/font, diagnostic,
distribution, public documentation and console acceptance work remains open.

## Historical continuation: IGR pad lifetime across IOP reset

Base: merged runtime-budget PR #41 at `c0c99efd`. Its final compiler/host,
32-configuration and three-kit CI pass; all 35 final packages verify.

The destination retains interrupt-driven IGR input handling. This adapts the
buffer-lifetime subset of original PR #951 (`feb4181afdab1a21ee9dbf291cd95bf24782df1a`)
without importing its polling worker. Reset interception disables pad-open
callbacks and invalidates the old DMA buffer before hook rediscovery and IOP
reset, including failed-reset paths. Pad reopen resets liveness counters and
publishes its pointer/layout with the previous interrupt state preserved.
Repeated installation preserves pending reset and physical-button state.

Actual-function host regressions exercise standard/RA input handling, all three
libpad layouts, reused old buffer contents, failed-reset paths and interrupt
state. Local 64 Python suites, HTTP and seven C++ sanitizer suites pass; mutation
checks reject retained buffers, unconditional interrupt enabling and pending
combo loss. Native LLVM RA compile/link/pack passed on source `44ab268d`.
Exact PR CI and package evidence belong in issue #20 and the PR.
This is a source-backed safety candidate, not a confirmed fix for reported RE4
freezes; targeted console acceptance remains required. Upstream #951 merged during validation at production `d0f4e64b`; its
merge does not establish the cause of the reported console freeze. No ps2build deficiency
has been identified in this change.

## Historical continuation: larger RA runtime budgets

Base: merged transfer PR #40 at `0c9f198776b774b9859cee3ececccbbcf331651d`.
Its final GCC/LLVM, hosts, all 32 configurations and three complete kits pass;
all 35 downloaded final integration packages independently verify.

Branch `codex/ra-runtime-budgets` coordinates 4,096 entries, 128 nodes, nine
snapshot parts and a 20 KB menu transfer limit through the existing C++ services.
LaunchServices reserves actual entry/node capacity after the IRX data; the
EE-core config carries that capacity and setup refuses unsafe reservations.
No large arrays are added to EE-core BSS. Direct-entry widths and byte sums are
validated in network/file parsers and again at setup. Unaligned values use byte
reads. Larger snapshots run every second/third frame, preserving an average
three-part-per-frame budget and per-frame overlay updates. See
[RA-RUNTIME-BUDGETS.md](RA-RUNTIME-BUDGETS.md).

The actual-method host tests cover maximum entries/nodes, IOP ninth-part tail,
module-storage bounds, parser failures and pacing. Unadapted EE methods fail the
new pacing regression when paired with the larger constants. Native/CI/package
results belong in issue #20; this document records the implementation snapshot.

Original production refreshed to `c5800e4ff026018c6bd10cca64fc2720a0e9eaad`.
New QA credits and preserved RE4/VMC/IGR diagnostic evidence remain to adapt,
alongside merged freeze diagnostics/About changes. Full RA commands/browser/
ORBIT/HTTPS, peer verification, overlay changes, cache recovery, CJK/type labels,
distribution and console acceptance remain open. Tag [techwritescode](https://github.com/techwritescode) with
versions/reproduction/logs whenever a confirmed ps2build deficiency is found.

## Historical continuation: RA watch-list transfer integrity

Base: merged VMC format PR #38 at `3acd135c406f324b60df8a422c112388e6b87d5f`.
Final integration GCC/LLVM, host, all 32 configurations and three complete kits
pass. All 35 downloaded final integration packages independently verify.
Pages #39 deployed at `5f22b6af`; Wiki `687acb3d` publishes scoped VMC guidance.

Branch `codex/ra-watch-transfer` adapts the confirmed fixed-stride chunk checks
from original `f0943360` through C++ Ranet. The advertised chunk count and every
payload length must cover the list exactly at 896-byte strides. Invalid parsed
watch lists are neither confirmed nor persisted. The binary payload begins
after exactly one protocol separator; whitespace bytes must not be consumed
by the header scanner. The original scanner's defect is reproduced and reported
in [original #946](https://github.com/NathanNeurotic/Open-PS2-Loader/issues/946).
The existing 16 KB limit remains until the broader runtime budget port is done.

The actual Ranet method/real class declaration run under ASan/UBSan with padded
datagrams, gaps, malicious counts, truncated packets, stale indices, binary
whitespace/NUL, supported sizes, parser refusal and NIC reservation. The old
method confirms a gap-containing transfer and fails the new regression. All
62 Python host suites, HTTP and seven C++ sanitizer suites pass. Native/CI/package
checks remain pending for this branch.

Original production refreshed to `6a3f12f2d22dbbf95bf2a1cf18f914ad6ed66145`:
#942 freeze diagnostics and #944 About/QA layout are now merged and require
individual adaptation. Original open #733 and DO NOT MERGE #687 remain tracked.
Full RA runtime/browser/client/ORBIT/HTTPS, peer verification, watch-list cache
replacement/error recovery, CJK/type labels, diagnostics/distribution and
console acceptance remain open. This receive-boundary fix is not full RA parity.

## Historical continuation: validate VMC format before automatic assignment

Base: merged Favorites PR #36 at `9a868e82aef3a4846e8659b1aa3977ea6dfb61b1`.
Its final GCC/LLVM, host, all 32 configurations and three kits pass, with all
35 downloaded integration packages verified. Pages #37 deployed at `44b6291c`;
Wiki `bd82806e` publishes scoped Favorites storage/recovery guidance.

Branch `codex/vmc-format-probe` addresses the retry described in original #941.
Automatic existing-file and post-creation checks request a read-only format
probe through GameSource. The actual superblock must be fully read and match
PS2 type, raw page/cluster geometry and file length; seek/read/close errors
refuse assignment. Geometry multiplication is widened before using untrusted
fields. Valid formatted odd-size cards remain eligible for Neutrino. Existing
invalid images are preserved and never implicitly reformatted. Open failures
retain the creation path's completed-directory proof of absence.

The real sysCheckVMC regression fails against the old size-only implementation
on an erased full-size image. Current tests cover erased headers, malformed
geometry, positive short reads, EOF/errors, seek and close failures, supported
and odd sizes, safe absence checks and explicit-only deletion. All 61 Python
suites, HTTP and seven C++ sanitizer suites pass; the final expanded focused
test also passes under ASan/UBSan. Native/CI/package checks remain pending for
this branch; update issue #20 with their final exact revisions.

This validates the header written after formatter metadata, not every filesystem
entry or media durability. Manual selection remains separate. Original production
is unchanged at `fb00066fe7c806a415cd019177bcf0227068bf52`; original #941 is still
open. Full RA/HTTPS/browser/ORBIT, CJK/type labels, optional diagnostics and
distribution bundles, and console acceptance remain in issue #20.

## Historical continuation: Favorites staging and restore-before-reuse

Base: merged VMC PR #34 at `01affe4de6c88dfcfda68a31349941e4b644cae2`.
Final compiler/host checks, all 32 configurations and three complete kits pass;
all 35 downloaded packages independently verify. Pages #35 deployed at `330f0f54`;
Wiki `0e02e12e` publishes scoped VMC sections and repeatable maintenance tooling.

Continue `codex/favorites-staged-recovery`. Commit `80cb0015` adds bounded image
reads, strict native/foreign parsing, native `.new` recovery, complete-primary
precedence, restore-before-restaging and exact readback before rewriting the
primary. ENOSPC staging failure refuses the edit without a direct overwrite.
Existing C++ edit/feedback/source architecture remains. See
[FAVORITES-STORAGE.md](FAVORITES-STORAGE.md). All 61 Python suites, HTTP and seven
C++ sanitizer suites pass; focused allocation/restart snapshots pass too.
Native/CI/package validation is pending for this branch.

Production remains `fb00066fe7c806a415cd019177bcf0227068bf52`. Original #943 is the
confirmed full-device fallback defect; this port deliberately excludes that
fallback. Real-device flush/power-loss behavior is not established by host IO
or readback. Shared failed-format image reuse (#941), CJK/list labels, full RA
and optional diagnostics remain separate work in issue #20.

Earlier continuation sections below are historical and superseded above.

## Historical continuation: VMC safety follow-ups from original #939/#940

Base: merged CFG-title PR #32 at `cd78770555500d8020789bc8e436fcd17c2fa36e`.
Its final compiler/host checks, all 32 configurations and three complete kits
pass, with all 35 downloaded packages independently verified. Pages PR #33 is
deployed at `bf73a48e`; Wiki `5e745be4` publishes the scoped Unicode guide.

Continue `codex/vmc-safety-followups`. Production refreshed to
`fb00066fe7c806a415cd019177bcf0227068bf52`, including original #938/#939/#940.
Commit `78f79dfe` snapshots cancellation under the formatter's abort lock, then
releases it before cluster seek/write. Its real-source regression fails on the
old lock scope and exercises abort/status calls during the IO window; failed
seek/write and existing owned-request/worker lifecycle tests continue to pass.

Automatic VMC follows actual UDPFS/UDPBD/SMB loader selection, allows existing
Neutrino odd-size cards without replacement, keeps slow working jobs running,
bounds post-cancel cleanup waiting and re-probes newly created cards before
assignment. Modal progress owns identity/configuration and re-resolves Favorites
before touching source state again. See [AUTOMATIC-VMC.md](AUTOMATIC-VMC.md).
All 61 Python suites, HTTP and seven C++ sanitizer suites pass; native/CI/package
validation is pending for this branch.

New shared defect reported as [original #941](https://github.com/NathanNeurotic/Open-PS2-Loader/issues/941):
a failed formatter can leave a full-size unfinished image that a later automatic
launch assigns based on size alone. Source inspection and the actual C++ policy
host harness reproduce this retry; physical-console evidence is still absent.
This batch does not resolve failed-output provenance/structural validation.
Favorites staged recovery from #938/#940 remains the next separate storage port.

Earlier continuation sections below are historical and superseded above.

## Historical continuation: CFG-title list parity and refreshed upstream audit

Base: merged PR #31 at `d74bb85bef54e83cca3727f4f75f6b47af65d658`.
Its final integration compiler/host checks, all 32 configurations and three kits
passed; downloaded packages were independently verified. Native LLVM RA passed
with ps2build/sdk-core v2026.10.13. Favorites durability remains open.

Continue `codex/cfg-title-parity-932`. Production source was refreshed to
`dea980ef4549de41dd1ee5e1261238a22da2080b`, including merged #932 and Pages #937.
The optional CFG title setting is adapted to C++ GameSource, table-driven
settings, list sorting, theme/header rendering and RA badge preservation.
Cache IO runs on the worker; HTTP refresh precedes Favorites resolution.
See [CFG-TITLES.md](CFG-TITLES.md). All 61 Python suites, HTTP and seven C++
sanitizer suites pass. Native LLVM RA compile/link/pack passes at `fc7dfb74`,
packed ELF SHA256 `9d11c08fd380e2db1dfc2b158cdf19c8a650526a3c0009bc947e92a29a4777f0`.
Exact-head CI/package validation remains pending in PR #32.

The audit confirmed two independent UI gaps: #917 optional CJK font fallback
and the `game_type_labels` setting. Neither is supplied by this title port.
Original PR #733 (cover art flow) remains open; #687 is explicitly marked
DO NOT MERGE. Track them without treating their unmerged code as production.
Full RA/browser/client/ORBIT, durable Favorites replacement and console testing
remain open in [issue #20](https://github.com/NathanNeurotic/RiptOPL/issues/20).
Pages #937 and Wiki title guidance require a scoped import preserving destination
migration notices; current publications remain Pages `b686bebd`, Wiki `feb11605`.

Earlier continuation sections below are historical and superseded above.

## Historical continuation: Favorites IO safeguards and native SDK sockets

Base is merged PRs #29/#30 at `39eda4b8ae12d8486503c16ffab8a3b5f1df6724`.
Fresh integration GCC/LLVM, host gates, all 32 selected configurations and three
complete kits pass, with all downloaded configuration/kit artifacts verified
against that exact SHA. Automatic VMC and newer Ember missing-core behavior are
merged. Pages `b686bebd` and Wiki `feb11605` are unchanged by this source batch.
The final evidence and remaining queue are in
[issue #20](https://github.com/NathanNeurotic/RiptOPL/issues/20).

Continue `codex/favorites-read-write-failures`. Commit `6516f0c9` refuses edits
after failed/incomplete Favorites reads, distinguishes validated empty/missing
stores, accumulates short transfers and propagates close failure. All 60 Python
host suites, HTTP and seven C++ sanitizer suites pass. The same edit test fails
against both the pre-fix C++ and original C methods. See
[FAVORITES-STORAGE.md](FAVORITES-STORAGE.md); durable replacement and interruption
recovery remain open in both repositories' linked issues.

Local ps2build/sdk-core updated to `v2026.10.13`, sdk-world remains
`v2026.10.12.2`, LLVM `v0.3.3`. The RA graph now uses native cglue sockets and
refuses stale core metadata; the historical fallback source/notices remain.
The newer audio driver and audsrv-compat were already selected. See
[NATIVE-SDK-SOCKETS.md](NATIVE-SDK-SOCKETS.md) for source and package evidence.
Final native/CI/artifact verification is pending at this source checkpoint.

Earlier continuation sections below are historical and superseded above.

## Historical continuation: automatic VMC and Ember missing-core behavior

Base is merged PR #27 at `2959b7a5077b6a16a0dca981813cdd1fdf1c793d`.
Its fresh GCC/LLVM, host, 32-configuration and three-kit integration checks and
downloaded artifacts pass. Favorites Pages PR #28 deployed at `b686bebd`; Wiki
`feb11605` updates only the affected guidance with repeatable scoped generation.
Earlier instructions below to start the already merged branch are historical.

Continue `codex/vmc-ember-parity`. Production refreshed to
`ebb62e1d` (including newer RA #927/#929, still separate work). Adapt automatic
VMC creation through GameSource, table-driven settings and the existing launch
menu; see [AUTOMATIC-VMC.md](AUTOMATIC-VMC.md). Preserve the non-destructive
sysCheckVMC guards and supported backend/core exclusions.

The formatter now owns its asynchronous request, reserves busy state before
worker scheduling, rejects overlapping creates, and recovers failed thread
creation/start. Abort is nonblocking/idempotent and completion publishes idle
after partial-file cleanup. A real-source lifecycle regression fails against
the pre-port formatter. The modal launch owns its config, rejects stale cache
ownership and revalidates row identity after yielding.

Ember scans retain valid image folders without a readable core; five launch
backends use the existing host missing-file service to name the expected ELF
or BIOS path. Ambiguous absent-directory errors retain production's missing-core
fallback, while definite device errors remain scan failures. No whole-source
legacy files or alternate SDK/release flavours are introduced.

All 59 Python host suites, HTTP and seven C++ sanitizer suites pass; the focused
policy/launch tests also pass after the final full-length app-identity guard.
Final native/CI/artifact verification is pending at this source checkpoint.

## Current continuation: older feature parity and released SDK follow-up

Base is merged POPS PR #25, `4157d5a23eb51d377635188b811b74883e927e41`.
Its fresh compiler, host, 32-configuration and three-kit integration runs and
downloaded artifacts pass. Pages PR #26 deployed at `fdda817f`; Wiki `6c1ffb6d`
updates only POPS sections, preserving unrelated imports and credits.

Continue `codex/legacy-parity-885-898`. Adapted production bodies are #885 quoted
config decoding (`f71cb5c6`), #887 top-level Ember image filtering (`ac5085c9`),
#889 VMC write/semaphore recovery and Neutrino warning policy (`3e836bd3`), and
#898 explicit SMB session checking (`2297297b`). The current production snapshot
is `a0ca55c54cb32b801423a4fdc800968ff4f62c2d`; full RA/browser parity is separate.

The C++ config sanitizer suite tests real file save/read cycles, maximum escaped
values and unknown escapes. Config and VMC regressions fail on the actual
pre-port source. VMC creation also stops before writing on a failed seek and
releases the semaphore. The stronger existing non-destructive VMC guards stay.
The production automatic-VMC-creation launch hook is absent here; this batch
does not claim that broader feature is ported.

Checked SMB Refresh uses `EthSupport`, host library services and the existing
serialized reconnect path. Healthy ART/BGM handles stay open. Queue rejection
is retryable; protocol changes cancel queued/in-flight probes. A settings
reconnect upgrades a queued or in-flight probe instead of being lost behind it;
the initial adaptation failed that scenario and the corrected regression passes.
New checks execute the actual class methods, menu dispatch and Ember scan.
The seven C++ sanitizer suites are now explicitly included in CI.

Runtime checkpoint `47f61dce` is pushed. Local ps2build/sdk-world are updated to
`v2026.10.12.2`, LLVM to `v0.3.3`; sdk-core remains `v2026.10.12.1`.
The released project-header fix passes a native header-only-change probe on
GCC and LLVM: both rebuild the object with changed bytes and project `-I` paths,
without the old `-sys-header-deps` workaround. The wrapper now requires the fixed
ps2build release. Both compiler package sets contain native `ppctty` (AFL-2.0);
UART overlays use that package and the bundled driver source is removed while
retaining license/provenance notices. Retain the RA socket compatibility archive:
released cglue still lacks those APIs, as the maintainer confirms.
The EE-core stack guard and narrow LLVM IGS `-Oz` policy remain unchanged.
All 57 Python host suites, HTTP and seven C++ sanitizer suites pass after the
final changes. Native LLVM Korium `debug_uart` compiles, links and packs with
the SDK driver (ELF SHA256 `62bf8defbff72e07c1681a959b0f1667bd5e401144223f366fc2ec69ec9cea8f`).
This local candidate records a dirty SDK-cleanup checkout; clean final CI,
configuration/kit verification and console UART acceptance remain pending.

## Latest continuation: POPS UDPFS #904/#909

Base integration is merged PR #24, `7bcb77b3ccf5bcf2af29c34527d864a6417ffbc7`.
Its fresh GCC/LLVM, host, format, 32 optional-configuration builds and three
standard kits pass. All 32 configuration candidates and three downloaded kits
were independently verified. Historical continuation instructions below are superseded.

Continue `codex/pops-udpfs-parity`. Production was fetched at
`90566eb863f9ed3771cdbc9702a5611b55f8ddd9`; #904 POPS and #909 rollback are
adapted through `UdpfsSource`, `VcdSupport` and host UI services. UDPFS Files
merges VCD/Ember rows; partial PS1 scans and failed ISO scans stay retryable.
ISO-only scans avoid unsupported UL probing. Output allocation failure keeps
the last-good library. Launch validates USB configs, remote VCD readability,
POPSTARTER lookup, equipped driver identity and clean-launch eligibility before
teardown. Both page and Favorites use the same virtual launch method.

The editor stages both DAT files on every identified USB volume, backs up old
copies, rolls back failed replacement, and preserves recovery files if restore
fails. Copy/write helpers propagate destination close errors. These checks do
not establish power-loss atomicity across multiple files or USB volumes.

The official BDM-UDPFS Assault Alpha archive has SHA256
`532bcf85b5beff90df9ff51da02b690b0bdee82ed3372e40b4f2c36de5eaf5ce`.
All ten upstream driver/config/notice members match byte-for-byte; provenance
and full notices ship in `POPS/UDPFS-NOTICES/`. USB supplies non-VCD dependencies;
no dummy local VCD is required. This external POPSTARTER driver is separate
from the sole ps2build SDK and the Neutrino PS2 network loader.

All 55 Python host checks, HTTP and seven C++ sanitizer suites pass, including
121 config/rollback/module checks and actual C++ launch/scan/IO failure harnesses.
The native LLVM Korium compile/link/pack passes. New PR compiler/configuration/
edition CI and downloaded artifact verification must finish before merging.
Physical-console POPS UDPFS acceptance, RA runtime parity
(including newer production #923), Favorites durable
storage, DECI2/distribution bundles and publication remain in issue #20.

The follow-up UL audit reproduced a heap-buffer-overflow with a 65-byte `ul.cfg`:
the old loop allocated one 64-byte record's output but wrote a second row for
the trailing byte. The real-layout ASan/UBSan regression fails against the
pre-fix scanner and passes after enforcing record alignment, bounded output
counts and complete reads. Valid partial reads accumulate; failed size/read/
close/allocation checks retain the previous list/count/size. Valid UL, ISO and
folder rows still publish together. Corrupt UL content now prevents replacement
of the cached library, rather than publishing partially trusted entries.

The first configuration matrix passed 31/32; the remaining job timed out reading
Vorbis release metadata from `git.techwritescode.dev` after 60 seconds, before
compilation. `tools/install_ports.py` now retries transient network/HTTP failures
up to three times, reports the URL, and still fails immediately for permanent
HTTP errors. Package selection, staged replacement and archive checks remain.
Fault injection covers interrupted response reads, transient HTTP errors,
permanent missing releases and retry exhaustion. Reverify the final pushed head.

## Latest continuation: optional ps2build configurations

Integration `daae402eb7e3618c0de948a2537a3ae4b4a4d960` contains merged PR #23
Favorites parity. Fresh GCC/LLVM, host, format and all three full-kit jobs pass;
downloaded final integration kits pass independent publisher verification at
that exact SHA. Both primary local RiptOPL checkouts were synchronized and clean.
No releases were published. The earlier sections below retain historical evidence;
their instructions to continue the already merged branches are superseded here.

Continue `codex/ps2build-configurations`. It adapts the recovered transition
definitions through `tools/build_edition.py --configuration`, retaining the
three editions and one SDK. See [BUILD-CONFIGURATIONS.md](BUILD-CONFIGURATIONS.md)
for flags, paths and limits. All 14 configurations preserve edition/RA ABI in
host checks; the new workflow builds 16 selected edition/configuration pairs on
both compilers (32 candidates). Nonstandard objects/stages are isolated, and
packaging/publication reject nonstandard manifests. Older standard manifests
remain verifiable. Do not restore the recovered helper's duplicate RA setup,
obsolete asset paths, or alternate release workflow.

The native LLVM Korium `iopcore_debug_uart` compile/link/pack passes, as do all
52 Python host suites, HTTP and seven C++ sanitizer suites. Verify the new PR's
exact-head 32-build matrix and standard edition packages before merging.
PPC TTY is still absent from SDK package discovery; its six recovered source/
license files were compared to official ps2sdk `ac92a9f657d2e531dd8f060250b07f2a5ac6dea5`
and match after line-ending normalization. License/provenance remain in the
source and applicable candidate artifacts. UART output requires separate console
acceptance. The source contains no verified compile-time no-IGR switch; that
audit, DECI2 and debug/language/font distribution bundles remain in issue #20.

## Latest continuation: merged network port and Favorites #903/#904

Verified integration is `d54dfc57d6af99995e72568af7a7ae8323b7dec8` (PR #22).
Fresh [GCC/LLVM](https://github.com/NathanNeurotic/RiptOPL/actions/runs/38331420380),
[host gates](https://github.com/NathanNeurotic/RiptOPL/actions/runs/38331420445),
format and [three complete kits](https://github.com/NathanNeurotic/RiptOPL/actions/runs/38331420407)
pass. Downloaded integration artifacts independently pass
`python tools/publish_editions.py build/ci-integration-d54dfc57 --check` at that
exact SHA. PR candidates were verified separately, without relabelling them.
No GitHub releases were published; console acceptance remains open.

Continue `codex/favorites-parity-903`: production #903 (`a73fb2e4`) and #904
(`906cdafe`) Favorites changes are adapted to the C++ classes. Indexed removal
returns persistence failure before clearing the source star; source and Favorites
actions show saving/success/failure feedback and only confirm/reload successful
writes. A new config opens Favorites on All; existing valid shelves remain.
Centered titles retain their anchor and scrolling room with leading stars;
ordinary lists retain aligned leading columns. Language labels are appended.
Regression coverage executes the actual C++ removal, R3 actions/guards, config
decoding, wrap-safe toast timing and layout geometry. The host entry point is
`.github/scripts/test_favorites_parity.py`; the full suite is
`sh tools/run_tests.sh -k`. Refresh the branch's exact compiler/edition CI before
merging. Local host/native logs are ignored under `build/`.

The inherited Favorites writer still opens with `O_TRUNC`, writes once and
ignores `close` errors. This port fixes feedback/star ordering, not atomic disk
replacement. Partial writes, interrupted saves and device-specific recovery need
a separate implementation and fault-injection/hardware checks. POPS UDPFS
launch/config/driver work and full RA/ORBIT parity remain in issue #20.

SDK feedback was sent to [@techwritescode on PR #22](https://github.com/NathanNeurotic/RiptOPL/pull/22#issuecomment-6135955069):
LLVM project-header dependency tracking, the confirmed missing POSIX socket
entrypoints in both installed cglue archives, and unavailable PPC TTY package
discovery. Local asset regeneration is an application preparation issue, not an
SDK defect. Retain the current RA-only compatibility code until native SDK
coverage preserves libc descriptor ownership and passes runtime validation.

The recovery section below records the earlier checkpoint; its open-branch
directions are superseded by this section. Recovery ZIPs and permanent Git refs
remain intact.

## Local recovery and production #922 (prior checkpoint)

Integration is `6988499b84e6049c7f544e4a8eff2769f58563a3`: PR #21 merged the
useful changes from #12–#16, and those originals are closed as superseded.
Its GCC/LLVM, host and three-edition CI passed. Independently downloaded kits
passed publisher verification against that exact integration SHA. Site
`72e3dd6575c0ff602cf087e1c7b812428984c2b8` and wiki
`45e0ee19293b1f86a4bbd599671700b838d86a89` are pushed and validated.

Continue branch `codex/network-parity-922` for the next reviewed batch. It
adapts production `0a1a593ade393d2c1d28bafb09ea8f3c5cb63706`: DEV9 return/failure
gates in all three UDP load paths, selected UDP NIC reservation for the existing
C++ RA menu methods, appended ministack export slot 7, local IOP devctl and a
one-shot warning queued by the IO scan and displayed by the GUI frame hook.
The newer achievements browser/Caduceus methods are absent and remain part of
the broader RA port. Legacy Makefile dependencies and RA-only preview filtering
were not imported; ps2build header tracking and all-three-edition CI remain.

Local ps2build/SDK update commands report `v2026.10.12.1` current, optional
LLVM `v0.3.2` current (clang 24.0.0git). Regenerating assets with
`python tools/assets.py all` fixed stale language IDs in the older local build
outputs. The actual LLVM compile/link/pack then passed. Console compatibility
and LLVM precision/performance are still unverified.

Local cleanup preserved changes before stashing them. Recovery ZIPs and JSON
hash inventories are under `REA/local-recovery/2026-10-12/`, outside the source
clone. All archived file bytes were read back and verified before stashing.
The old repository also retains permanent recovery refs:

- `refs/recovery/2026-10-12-public-transition` →
  `6aabfb2bac319d4d45fd9a2ec610bacf2d66fa99` (112 files, including old variant helpers).
- `refs/recovery/2026-10-12-primary` →
  `d76f7e5bc164cb7f166815308490713d5ea18962` (old primary artwork and scratch scripts).

Three entirely clean closed-PR worktrees (docs, Ember Watch and site) were
removed; their Git branches/history remain. Three fully merged local branches
in the active clone were deleted. The transition, Neutrino and unrelated Copilot
worktrees were retained, including ignored build outputs and the nested legacy
wiki checkout. Recover unique `tools/build_flavour.py`, variant tests and PPC
TTY provenance from the ZIP or the transition stash's third parent before
implementing configuration coverage. Never restore obsolete workflows wholesale.

The historical sections below retain prior evidence; their open-PR and dirty
worktree descriptions are superseded by this continuation record. Issue #20
remains authoritative for unfinished parity, publication and hardware work.

## Current merged checkpoint and continuation queue

The authoritative remaining-work tracker is
[issue #20](https://github.com/NathanNeurotic/RiptOPL/issues/20). Start there and
refresh the integration head before continuing. PR #19 merged to `rebuild/main`
as `0dae12553672d8c988fe5b038d383e375d287eff`, after PR #18. Its exact PR head
`c53341a02f74102d6abcf68f50910ac87f46e98f` passed GCC/LLVM, host, format and all
three complete edition package jobs. Downloaded packages independently passed
publisher verification against GitHub's PR merge revision
`b6af86efd7a2e8dc0518048edd4d12e4175e3781`; the final merge has the same tree.
Fresh integration CI also passed at `0dae1255`. These are separate source
revisions; do not relabel the PR candidate as a final integration artifact.

Korium, RetroAchievements and IncebtIon now have complete ps2build-only release
candidate workflows. No GitHub releases were published during this work;
publication remains an explicit dispatch from the tested integration head.
The RA edition builds and packages, but newer RA/ORBIT behavior and console
acceptance are still outstanding. The socket compatibility source is a small
RA-only fallback inside ps2build, not a second SDK.

PR #17 merged the full production site history as
`acdb961d8c36a2da33f3a7c8593db378d2feb248`; GitHub Pages reports a successful
deployment. All 33 HTML pages and 1,669 local links/resources pass validation;
the generated search index is current. The separate wiki import preserves the
production history and destination initialization at
`50a4053a267295977d3c146f82450b5f19a1364a`, with 53 content pages plus sidebar
and footer and 651 verified internal links/anchors. The imported guides retain
their production evidence and explicitly qualify migration/hardware coverage.

The follow-up branch `codex/merge-migration-queue` consolidates useful work from
the older PRs: #13 Ember saves (`e5ff4ae0`), #14 Neutrino editing/preflight
(`ffc9867a`), #15 docs/evidence (`44661659`), #12 splash artwork (`4efe9d46`),
and #16 Ember Watch (`8f3f02bf`). It retains the newer warning parser, VMC strings,
edition texture ordering and release contract. Ember Watch builds all three
candidates with `publish=false`; it does not invoke removed Korium automation.
All combined host tests, HTTP tests and seven sanitizer suites pass locally.
The consolidated PR's checks and actual merge state determine its final status.

Production advanced to `9d910fc05796b7bcfb4f00544a55baf73d800a33`, adding #922
NIC reservation/DEV9/IP-clash hardening after the original ledger audit. That
port remains open in issue #20 along with Favorites/POPS UDPFS, full RA/ORBIT,
additional configurations, language packages, publication and console testing.
Other dirty worktrees are preserved; the public-transition worktree contains
useful uncommitted variant work and must be inspected before duplicating it.

The following sections retain the earlier build and artifact evidence for
provenance; their older PR/setup status is superseded by this checkpoint.

This is a continuation record, not a declaration of full public parity or console
acceptance. Read this and `UPSTREAM-PARITY-2026-10-12.md` before repeating setup or
starting another port. RiptOPL was made public during this session; GitHub's API
now reports `visibility: public`. Earlier notes call it the private migration repo.

## Repository and branch

- Migration: `NathanNeurotic/RiptOPL`, local checkout
  `C:\Users\natha\Github\REA\RiptOPL`, branch `codex/sdk-llvm-parity`.
- Production reference: `NathanNeurotic/Open-PS2-Loader`, default `rebuild/main`.
  Do not confuse it with the migration repository or change that checkout as part
  of this work.
- Continue [PR #19](https://github.com/NathanNeurotic/RiptOPL/pull/19), stacked on
  [PR #18](https://github.com/NathanNeurotic/RiptOPL/pull/18), base
  `rebuild/main` after PR #18 merged as `7a760cf9c1d45cbca9c6cc9a7df86dced221ad41`.
  Refresh remote refs and actual reviews before updating or retargeting it.
  `techwritescode` is the requested reviewer; no submitted reviews were present.
- Other local migration checkouts/worktrees contain unrelated uncommitted work.
  Preserve them, especially `.codex\worktrees\public-transition\RiptOPL`.

## Committed work

| Commit | Scope |
| --- | --- |
| `c7495a366fa4faa4eceaad6240adc9eabe8e0d7d` | Native Windows assets, compiler-qualified ports installation, warning reporting and GCC/LLVM CI setup. |
| `988d4ac87911183394d711cb26bd78497f5a72ae` | LLVM project-header dependencies through `tools/build.py`. |
| `fb50211e1eb9eae2d6c507cf55707078ecd5ecc8` | Public UDPFS serialization, shared transport, reconnect/ACK/ARP fixes, with production driver regressions. |
| `f2a37033a05f2459517e431c6a40f3656ec74a4e` | VMC preservation, BDM allocation retry, C++ cache/menu allocation handling, queue/GUI lifetime, reset packet/failure handling, and the RA Mode 6 snapshot subset. Eight new regression files; CI and parity ledger expanded. |

The checkpoint commit itself only adds this document. The locally built source
commit is `f2a37033a05f2459517e431c6a40f3656ec74a4e`; do not relabel its artifacts
as a later commit's build. Future source changes need new builds and versioning.

## Reproduce the build and tests

Installed locally: ps2build/sdk-core/sdk-world `v2026.10.12.1`, optional
LLVM toolchain `v0.3.2`, GCC toolchain `v2026.09.27`. SDK root:
`C:\Users\natha\AppData\Local\ps2build`. The global ps2build MCP is registered;
the CLI was used for these native Windows builds. The default SDK update does
not install LLVM automatically.

```sh
ps2build update
ps2build update llvm-toolchain
python tools/install_ports.py --toolchain all
python tools/assets.py all
python tools/build.py --toolchain gcc --out-dir build/gcc
python tools/build.py --toolchain llvm --out-dir build/llvm
```

Use `python3` on hosts that require it. Use `tools/build.py` for LLVM: direct
SDK builds currently omit project headers from dependency tracking. GCC and LLVM
outputs/ports stay separate. Generate `tools/assets.py version` after committing
source, then build both sequentially; building both concurrently causes heavy
resource contention on this host.

Host tests on Linux/WSL: `sh tools/run_tests.sh -k`. The local WSL virtualenv
`build/site-test-env/bin` has PyYAML, Beautiful Soup and markdown-it-py; put it
first on PATH so the site test runs. No PS2 compiler is needed for host tests.
Native Windows assets require Git for Windows, Python/PyYAML and the SDK.

## Validation and local artifacts

- All **46** `.github/scripts/test_*.py` files, HTTP parser tests and **7** existing
  ASan/UBSan C++ suites passed without skips. New tests compile the adapted
  production C++/C routines, including standard and RA reset/handler variants.
- Saved pre-port sources reproduce image-constructor crashes, invalid BDM
  registration and the queue-head lifetime regression. The old VMC implementation
  fails the preservation/bounds scenarios. New code passes. Reset/Mode 6 source
  and lifecycle checks also detect the missing pre-port implementation.
- Both native Windows compilers compiled all EE sources, linked and packed the
  main flavour with version `v1.2.0-Beta-3512-f2a3703`. Unchanged IOP objects were
  validated in the earlier full/UDPFS builds. This is not a fresh clean rebuild
  of every IOP module at the final commit.
- Final GCC warning gate: **55** against baseline **62**, zero regressions; the
  baseline was not changed. Coupling gate, clang-format 12, workflow syntax,
  Python syntax and whitespace checks passed.
- Both dependency databases contain the version header and canonical UDPFS
  sources; touching the version header schedules compilation in both compilers.
- LLVM's Windows linker crashed once in its winpthreads thread pool with
  `0xC0000008`. An unchanged retry and the final versioned link passed. This is
  an intermittent SDK tool failure, not a confirmed fix. Logs are retained.

Local candidate: `dist/sdk-f2a3703.zip` (12 files, CRC checked). It contains two
packed alternatives, a JSON manifest, current/prior build logs, generated Ninja
graphs and the LLVM failure/retry logs. `dist/` and `build/` are intentionally
ignored; these local files are not a published release.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `RiptOPL-main-gcc-f2a3703.elf` | 1,650,496 | `1f7c8a8c51155cbbf49c2574bbac76257fa522f8195c1d19c6d4d98013e2a9cc` |
| `RiptOPL-main-llvm-f2a3703.elf` | 1,696,032 | `311a82f9739a6e278eaa7830f43016793179817f157d6b7d9d9d37a978b84d22` |

Before the visibility change, Actions jobs never started because of account
billing/spending limits. New public-repository CI must be checked at its actual
head; old failed runs are not compiler failures. Do not infer a green run from
this checkpoint or from the repository visibility alone.

## Continue from here

1. Read issue #20, refresh `rebuild/main`, and inspect the consolidated queue
   PR's exact head, reviews, CI and merge state. PR #19 is already merged.
2. Audit and adapt public Favorites/POPS UDPFS (#903/#904 plus #909 rollback) as
   a coherent source/launch/config/package change. Preserve `GameSource` and host
   services; do not replace converted backends with legacy C files.
3. Reuse the committed queue consolidation before duplicating PRs #12–#16.
   The full site and wiki imports are published; PR #17 is merged.
4. Validate the three ps2build release editions and standard/RA package isolation, then port
   the remaining RA runtime/browser/client/ORBIT behavior. The current
   `tools/build_edition.py` now overlays the base `ps2.yaml` for Korium, RA and
   IncebtIon. Clean edition CI builds and complete packages now pass;
   runtime/hardware parity remains a separate gate. `IGR_RequestReset`
   is absent from the older private runtime.
5. The release workflow now uses only ps2build and builds three complete kits.
   Publication is explicit manual dispatch; validate staged packages, provenance
   and all exact-head CI jobs before publishing. Old normalizer/promote/Korium
   branch-sync publishers are removed to prevent conflicting release contracts.
   See `ROLLING_RELEASE.md`. README badges and full Pages/wiki imports are done;
   keep their generated content and migration-status claims synchronized.
6. Work through earlier feature/config differences and console acceptance in
   the parity ledger. LLVM ignores `-fsingle-precision-constant`; precision,
   performance, audio, storage/network behavior and game/IGR compatibility need
   hardware evidence.

Keep committing verified batches and update this continuation record when a
material state changes. Preserve the distinction between source tests, compiled
ELFs, packaged artifacts, GitHub CI and real-console acceptance.
