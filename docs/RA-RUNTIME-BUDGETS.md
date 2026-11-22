# RA watch-list and snapshot budgets

The RetroAchievements edition accepts up to 4,096 direct entries and 512 PS2
pointer-chain nodes. POPStarter supports up to 256 nodes. The menu transfer
ceiling is 24 KiB and can hold a full RAN2 list with its RAH1 hash record.
RAWL remains version 1 and the loader accepts RANL and RAN2 node records.
Every direct entry must read 1, 2 or 4 bytes, and their sum must exactly match
the header's direct-byte count. The loader and EE core both validate the list.

The total sampled bytes, including eight bytes per node, must fit nine snapshot
parts. A packet reserves 191 bytes for the text header, leaving 1,281 value
bytes; the IOP derives its actual chunk size from its header and caps it at that
value. Tests verify the current header fits and that the maximum ninth part
reaches the sender without dropping the tail.

## Protected storage and launch contract

Watch data stays in the C++ `RaWatch` service until launch. `LaunchServices`
calculates the validated work-area size and the launcher reserves it after
the IRX data in protected module storage, aligned to 64 bytes. The EE-core
config carries both the pointer and actual reserved capacity. EE-core copies
the entries and nodes during setup, before loader memory can be overwritten.

The layout reserves the maximum snapshot DMA buffer, one word per direct entry,
and five words per node. The additional nodes use up to 5 KiB in protected
module storage when a set needs them. An untracked launch reserves no RA work
area. The launcher skips telemetry if the cache-aligned reservation would cross
the applicable RAM boundary. EE core also checks alignment and capacity.

## Sampling and pacing

One to three parts are sampled every frame; four to six every second frame;
seven to nine every third frame. This bounds the average to three parts per
frame. The IOP still sends each complete snapshot as a batch, so this is not a
claim that every individual frame or poll sends at most three packets. An
in-flight SIF DMA is skipped without blocking, and the overlay keeps its own
per-frame schedule. Existing start delay, torn-snapshot checks, pending-send
retry and sequence handling remain.

Aligned word/halfword reads use their native widths. Unaligned wider values
use byte reads to avoid address-error exceptions inside VBLANK. Out-of-window
addresses produce zeros while preserving the snapshot's shape. Entry width
and direct-region bounds are checked before reads; node resolution retains its
existing parent/size/address guards.

Host tests execute the real C++ network/file parser, launch capacity method,
EE-core setup/sampling/pacing and IOP sender. They cover full entry/node budgets,
maximum packet coverage, bounded memory writes, malformed lists, DMA busy/torn
states, sequence advancement and frame cadence. These checks do not establish
physical-console latency, network throughput, frame cost or achievement accuracy.
