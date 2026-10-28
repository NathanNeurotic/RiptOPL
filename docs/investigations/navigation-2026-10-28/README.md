# Navigation interference investigation, October 28, 2026

**Status: mechanisms reproduced on the host; the reporter's cause remains unconfirmed.**

This continues [RiptOPL #133](https://github.com/NathanNeurotic/RiptOPL/issues/133)
and the cut-off investigation supplied by the user. The matching legacy tracker is
[Open-PS2-Loader #1106](https://github.com/NathanNeurotic/Open-PS2-Loader/issues/1106).
The user clarified that the interference could be a double tap or interrupted
hold, rather than a complete hang. That distinction materially changes the diagnosis.

This PR contains investigation scripts and their evidence. It changes no runtime
source, SDK, driver, default setting or release configuration. No physical console
test, hardware bisect or confirmed last-good binary is available.

## What the evidence now supports

1. **A native sample gap can explain a duplicate step and interrupted repeat.**
   The actual pad functions produce another key-on event when a direction returns
   after one failed read, even if it was physically held throughout. They restart
   the longer initial repeat delay. This behavior predates both audited windows.
2. **The art port adds a demonstrated way to extend work across resumed input.**
   Without a usable directory index, a missing cover now starts three file probes
   and a background starts six, versus one before the port. They can continue after
   navigation resumes. This is the strongest reproduced increase in relevant I/O,
   subject to the backend, artwork and index prerequisites below.
3. **The repeat cap has a distinct slow-frame tradeoff.** It prevents an immediate
   extra repeat after one long frame, but repeated long frames make a genuine hold
   wait more polls. It is inside the 48-hour window and already in the 24-hour
   baseline. It does not create a digital-mode-only fault or an infinite freeze.
4. **A missing MMCE/SIO2MAN version match is not established for this SDK.** The
   installed driver supplies the 2.7 table used by the new hook. An actual-function
   hook probe calls its native lock and unlock once and restores the IRQ pointer.
   Hardware scheduling, bus starvation and timeout interactions remain untested.
5. **Successful `padRead()` is not proof of a fresh physical sample.** The installed
   library returns a selected DMA buffer's length, without comparing its frame
   counter with the previous application poll. The current diagnostics cannot
   distinguish stale buffered data from newly sampled input.

The best current causal hypothesis is **new or extended background/device work
plus pre-existing treatment of missing samples as releases**. Neither the occurrence
of that work nor the initiating sample gap has been observed on the reporter's
console. A separate slow-frame/repeat-cap path remains credible for a paused hold.

## Scope and provenance

The inherited cutoff is October 28 at 14:14 PDT / 21:14 UTC. The extended window
starts October 26 at 14:14 PDT / 21:14 UTC. The default branch was rechecked and is
still at the handoff's audited head.

| Reference | Commit | Meaning |
|---|---|---|
| Before extended window | `6edea174e5522b241139eeb4c683c9f2c76f776f` | Historical source reference; not a hardware-verified good build |
| Before strict 24-hour window | `bc439979fde0861aa56e8a63e5408b0a63a52785` | Already includes the repeat cap and broad #77 integration |
| Current audited head | `b4c23bdac8f983a4b4e3456d4036b8645cf95842` | `rebuild/main` when this continuation began |

Commit author dates do not establish when a change reached main. For example,
[the #77 merge, `894dca36`](https://github.com/NathanNeurotic/RiptOPL/commit/894dca36)
integrated earlier work on October 27 at 12:27 PDT. The extended audit therefore
includes its MMCE replacement, SaveIcon and repeat-cap work even where the author
dates differ substantially.

The screenshot/report described in the handoff associates the problem with the
DualShock ANALOG button being off. Exact ELF, build flavour, controller model,
backend, active theme/list, ART directory/index state, rumble setting and the
scope/duration of the interruption remain unknown. The user has no more setup
information to supply. No independent reproduction in the legacy C repository
has been established.

## Native input: how one gap becomes two steps

The probe compiles the real `PadManager::read`, `readOne`, `readLeftJoy`, state
tracking, elapsed clock, repeat timers and key getters from each of the three
source references. Native SDK replies and time are controlled; initialization
and actuator commands are stubbed and asserted unused in these scenarios.
It tests digital `0x41`, analog `0x73` and pressure `0x79` packets with a neutral
stick. PADEMU is not enabled in these continuation probes.

The decisive sequence is:

| Poll | Physical direction in the supplied test | Native read | Aggregate held bit | Menu result |
|---|---|---|---|---|
| A | Down held | Successful | Down | Initial step |
| B | Still held | Successful | Down | Hold countdown |
| C | Still held | Failed / zero length | Zero | No step; initial delay rearmed |
| D | Still held | Successful | Down | New key-on and another step; initial delay reset |

This follows directly from `read()` zeroing `paddata_` before each poll and
`readOne()` repopulating it only when it obtains data. `keyOn()` then compares
that aggregate with the previous poll. A failed poll supplies the zero baseline
that makes recovery appear to be a second press.

The same result appears before the 48-hour window, in the 24-hour baseline and
at the current head, in all three packet modes. The probe also demonstrates:

- A six-poll tap wholly inside failed reads produces zero events.
- Clean short holds produce one initial event.
- A busy-state interval with a failed read produces a recovery event too.
- Once a hold is repeating, a gap changes the next delay from the repeat value
  of 200 to the initial value of 600. At 33 internal elapsed units per poll, the
  next repeat takes 19 successful polls, versus seven for the ordinary repeat
  period. The units are the application's existing `CLOCKS_PER_MILISEC` units;
  this investigation does not recalibrate their physical duration.

This proves an input interpretation mechanism, not a new regression or a
digital-specific cause. Preserving held bits indefinitely would create a different
failure: a released key or disconnected controller could keep navigating. Any
future fix must distinguish unknown input from a verified release, bound its
retention, and test disconnects, real releases and reconnects separately.

Evidence: [navigation_input_gaps.jsonl](navigation_input_gaps.jsonl).
Probe: [probe_navigation_input_gaps.py](../../../tools/investigation/probe_navigation_input_gaps.py).

## Artwork: a reproduced recent increase

Introducing commits:
[initial port `d7d238d4`](https://github.com/NathanNeurotic/RiptOPL/commit/d7d238d4e17577fb9b283c667077a63882535b04),
[JPEG extension addition `adb06979`](https://github.com/NathanNeurotic/RiptOPL/commit/adb0697912208b8a8dfa5c7028733ba54b139055).

The probe compiles actual queue push/pop/release/reset, art discovery, alias,
format selection and JPG file-opening functions from the introducing commit,
its immediate parent and the current head. The PNG loader and device operations
are stubbed. Counts below are **file-probe starts at those boundaries**, not
measured IOP RPC durations or decoder costs.

| Unindexed missing piece | Before port | Initial port | Current head |
|---|---:|---:|---:|
| Cover | 1 | 2 | 3 |
| Background | 1 | 4 | 6 |
| Additional starts after navigation resumes during first probe: cover | 0 | 1 | 2 |
| Additional starts after navigation resumes during first probe: background | 0 | 3 | 5 |

The queue positive control confirms that an SIO2 request remains queued when
navigation is held before `pop()`. The experiment then exposes one released
navigation sample, admits the request, and resumes navigation during its first
file probe. The extension/alias chain does not consult the held-navigation flag
again. The separate pad probe shows how a read gap can supply that released
sample to `cacheTickArt()`; these are controlled experiments, not an observed
cross-thread interleaving on PS2.

Controls at all revisions: an indexed absence starts zero probes; a present PNG
starts one; a transient error stops after the first; an explicit abort after the
first prevents additional probes. Resumed navigation is not itself an explicit
abort within this lookup chain.

**Prerequisites:** the art worker must be active, the piece absent, and the index
unavailable/uncertain. A healthy index removes this miss multiplier. The direct
controller-bus explanation applies to MMCE/MX4SIO. Extra USB/HDD/network work may
affect shared RPC or frame scheduling, but is not itself proof of SIO2 contention.
Existing in-flight I/O exposure predates the art port.

Evidence: [art_miss_fanout.jsonl](art_miss_fanout.jsonl).
Probe: [probe_art_miss_fanout.py](../../../tools/investigation/probe_art_miss_fanout.py).

## Repeat cap: confirmed difference under slow polling

[`1c16f063`](https://github.com/NathanNeurotic/RiptOPL/commit/1c16f063ff6d2184cbb136f80b3c3cdd65ff18a1)
limits one poll's credit to 100 internal elapsed units. The controlled 800-unit
frame immediately following a press produces an unwanted repeat before the
change and no repeat after it. That is the intended improvement.

With a genuinely continuous hold and repeatedly slow frames, the same cap also
limits how quickly the countdown advances:

| Supplied elapsed units per poll | Polls to first repeat before cap | With cap |
|---|---:|---:|
| 33 | 19 | 19 |
| 100 | 6 | 6 |
| 200 | 3 | 6 |
| 800 | 1 | 6 |

This can magnify a hold interruption when a render/device/RPC path is already
slowing polling. It does not cause that initial slowdown, require analog-off,
or block indefinitely. It was already present before the strict 24-hour cutoff.
Removing it would restore the known first-press extra-repeat exposure; a blanket
rollback is not supported by this evidence.

Evidence: `sustained_slow_frames` and `long_frame_after_press` records in
[navigation_input_gaps.jsonl](navigation_input_gaps.jsonl).

## MMCE replacement and SDK contract

The extended window includes
[`f5fa0b20`](https://github.com/NathanNeurotic/RiptOPL/commit/f5fa0b20),
which replaces the packaged menu MMCE driver with the vendored fork. This is
broader than the later
[`0ce2f162`](https://github.com/NathanNeurotic/RiptOPL/commit/0ce2f162)
port-3 filter update. The earlier driver used additional wrapper semaphores;
the current driver uses SIO2MAN's native transfer-init/reset exports and swaps
the IRQ handler while holding that ownership. Fewer local semaphores alone do
not demonstrate that exclusion was lost.

The installed GCC SDK reports sdk-core/world `v2026.10.13.1`, ps2build
`v2026.10.15` and toolchain `v2026.09.27`. Every installed SIO2MAN filename has
SHA256 `10be5da0509f30190622afa9c41a817987f21f69623d99089c472785d47e6a93`.
Its export tables are named **`sio2man` 2.7** and **`sio2man1` 1.2**.
Those names are distinct: the vendored iterator compares all eight bytes.
The 2.7 table supplies nonzero transfer-init/reset exports 23 and 26 at
`0x6cc` and `0x76c`. The disassembly at those entries uses the same stored
semaphore identifier for acquisition and release.

The host probe compiles the complete current hook implementation with controlled
export tables and interrupt internals. With the installed naming/version shape,
it calls native lock/unlock once and restores the original IRQ handler/userdata.
This weakens the proposed standard-SDK version-mismatch explanation.

A negative compatibility control using only a synthetic `sio2man` 1.2 table
returns init success but performs zero native lock/unlock calls: the current
accessors use the major-version-2 slot only. **That is not the installed module
shape and is not evidence of this reporter's problem.** Keep it separate from
the normal build; do not label it an SDK defect.

Host call-order checks cannot settle actual IOP scheduling, fairness, timeout
recovery or coexistence with other SIO2 users. This driver replacement remains
a conditional 48-hour candidate if MMCE activity is demonstrated at the symptom.

Evidence: [mmce_hook_contract.jsonl](mmce_hook_contract.jsonl),
[sdk-binary-contract.txt](sdk-binary-contract.txt).
Probe: [probe_mmce_hook_contract.py](../../../tools/investigation/probe_mmce_hook_contract.py).

## Correct interpretation of pad diagnostics

The installed `libpad.a` SHA256 is
`86812d951c0f91b9084cbf3f6d3f7d25c15f82726db4d4478db7f4d3349128d7`.
Its `padRead()` selects a DMA buffer, copies its reported length and returns
that length. It does not remember which DMA frame the application last consumed.
See the saved relocation-aware disassembly in
[sdk-binary-contract.txt](sdk-binary-contract.txt).

Consequences for the earlier investigation's terminology:

- `readOk` means a nonzero native return, not an independently verified new sample.
- A successful empty read cannot establish that the user supplied no input during
  the interval since the preceding GUI poll. The buffer might be old.
- Not-ready/empty run counters are updated per `readOne()` call across ports,
  rather than representing a timestamped per-port history of physical samples.
- A frame trace must record raw SDK state, read length and DMA frame progression
  alongside elapsed GUI time and emitted key events. The existing counters alone
  cannot assign responsibility to controller hardware, the SDK or the application.

The fault-injection probes do not claim zero-length reads actually occurred on
the reporter's setup. They establish what happens if they occur.

## Remaining 48-hour paths and their prerequisites

These supplement the source references and earlier probes retained in issue #133.
Rows marked source review have not received a new PS2 timing measurement here.

| Path / change | Possible connection | Scope and evidence boundary |
|---|---|---|
| Hidden-list read, `a7294839` | Adds `hidden.txt` device I/O while rebuilding rows | Requires an actual rebuild; not every scroll step. Earlier actual-function probe is recorded in #133. |
| SaveIcon, `1bd61156` | Optional loader can scan physical cards or VMCs, even when the game source is USB/HDD/network | Defaults off. `saveIconSelect()` and `saveIconSelectPs1()` wait 250,000 clock units after selection changes, not after the last held-direction sample. A direction held at a list boundary can leave selection unchanged long enough to queue work. Source review; no measured pad loss. |
| Info-page size request, in #77 | Moving rows on Info now queues `menuRequestInfoSize()` and source config/size work | Requires the Info page. Main-page D-pad handling has no equivalent new size request. Source review. |
| L3 tap/hold, `935a50ad` | A missing held sample can become a release/tap and queue a library-view change | Requires L3 already armed; cannot explain ordinary D-pad-only input. Earlier probe in #133. |
| SFX / rumble defaults, `4631b054` | Cursor sound and actuator commands can add synchronous RPC work if enabled | The change defaults them **off**, not on; saved configs override. Rumble arming remains state-only; the native polling/actuator algorithm is unchanged across these source references. |
| One-shot sound replacement, `38fa15e9` | Different voice selection for confirm/cancel/message/transition | Cursor dispatch remains its existing synchronous RPC path. This edit does not add a new cursor RPC. |
| Grid / All Games / Recent | Extra list traversal, row mapping and potentially larger lists | Requires the feature/view; can contribute CPU or rebuild cost. No digital-only condition or target frame timing established. |
| Text shaping / glyph changes | More per-frame text work; possible frame-time contributor | Earlier sanitizer string probes in #133 passed. Host success does not measure PS2 font/rendering latency. |
| BDM FAT / iLink / SMB driver replacements in #77 | Filesystem/RPC load can affect response time while those transports are active | Backend-dependent. Not native-controller packet decoding; no source proof tying a replacement to this report. |
| IGR, PADEMU launch hooks, Game ID and reset work | Can affect pad ownership during launch/handoff | No new execution from ordinary main-page scrolling established. Keep distinct from menu input diagnosis. |

## Reproduction and saved evidence

Run in a checkout with the pinned historical commits available and a host GCC
or Clang C/C++ compiler on PATH:

```sh
python tools/investigation/probe_navigation_input_gaps.py
python tools/investigation/probe_art_miss_fanout.py
python tools/investigation/probe_mmce_hook_contract.py --sdk-root /path/to/ps2build
```

The SDK argument is optional; without it the third script executes the hook
controls but does not inspect installed binaries. The scripts have no network
or console side effects. They compile into a temporary directory; set `TMPDIR`
or `TEMP`/`TMP` to a writable task directory if the host sandbox restricts its
default temporary location. JSONL on stdout is suitable for comparing runs.

Saved continuation results: 90 native-input records, 18 artwork records and
two MMCE hook controls plus seven installed-IRX records. Host controls passed.
These are host and binary-inspection results, not EE/IOP execution results.

The saved run used Windows x86-64, Python 3.14 and MinGW-w64 GCC/G++ 16.1.0.
The existing `test_pad_repeat_clock.py`, `test_art_queue_order.py` and
`test_mmceman_fork.py` also passed. The repeat-clock suite preserves its old-clock
negative control, and the MMCE suite verifies reverse-patch provenance and
filesystem status/timeout/handle behavior. The latter needs
`GIT_CEILING_DIRECTORIES` set to the temporary root when that root is inside a
checkout: otherwise `git apply` detects the parent repository and can skip the
isolated fixture's paths. This is a host-fixture setup issue, not a driver or
SDK failure. No target rebuild is claimed for this documentation/probe-only PR.

## Next discriminating experiment

The next useful console trace must align events in one RAM timeline, rather than
correlating separate maximum counters or adding synchronous logging to each poll:

1. Per port: raw SDK state, packet mode, native return length, DMA frame/sample
   progression if obtainable through the actual linked SDK, and button bits.
2. GUI: raw elapsed ticks, key-on/repeat events, selected row/view and page.
3. Work: art admission and probe boundaries, navigation flag, actual list rebuild,
   Info-size request, SaveIcon activity and SFX/actuator command boundaries.

Then isolate one condition at a time: missing/unindexed ART versus indexed
absence; SaveIcon off/on; main versus Info page; and a controlled repeat-cap
comparison while keeping the rest of the binary identical. A long frame with
fresh continuous samples supports the cap/frame-time path. A zero-length gap
followed by a recovery key-on supports the synthetic-release path. Repeated DMA
frames with successful reads require investigating sample age rather than treating
`readOk` as freshness. A complete GUI stop requires separate lock/thread evidence.

Do not freeze the input state indefinitely, remove the wrap-safe clock, revert a
whole integration, or declare a driver/SDK/controller responsible from these host
results. A targeted experiment needs to preserve disconnect handling, real
releases, artwork fallback, transient-I/O classification and other transports.
