# Favorites storage, staged saves and recovery

Favorites use the shared OPL configuration directory's `favourites.bin`. The
file format remains OFAV version 3, with read support for versions 1 and 2 and
complete uOPL/wOPL imports. The three ps2build editions use the same policy.

Adding or removing a favorite first reads the stored list. A genuinely
missing pair of files (`ENOENT`) or a complete supported list permits an edit. A valid
header-only OFAV file is an editable empty list. A zero-byte file, truncated
record, invalid text terminator, unsupported version, oversized list, trailing
data, failed allocation, failed seek, read error or close error refuses the
edit. A salvageable prefix may still be displayed; it is never silently saved
over the original unless a complete native recovery copy can be restored safely.
Unknown versions, oversized stores and IO/allocation failures keep edits refused
even when a recovery copy can be displayed. Existing failure feedback reports the refused edit and
does not confirm it or change the source star.

Ordinary reads load a bounded image instead of issuing a device read for each
record field. Positive short reads and writes accumulate, and clean EOF and
close are required. OFAV v1/v2/v3 and complete foreign imports retain their
existing decoding rules. Oversized paths are refused rather than truncated.

## Save and recovery rules

1. A complete, readable `favourites.bin` wins, including a PC-edited list or a
   valid empty OFAV list. A stale `favourites.bin.new` does not override it.
2. If the primary is missing or structurally incomplete, a complete native OFAV
   `.new` can supply the list. Foreign imports are accepted only from the primary.
   An unreadable or unknown-version primary never authorizes replacement.
3. Before saving another edit, restore a recovery-only list to the primary and
   verify its bytes. Failure leaves `.new` intact and refuses the edit. Authority
   is re-read for each save; it is not cached across config paths or PC edits.
4. Serialize the requested list, write `.new`, close it and read it back exactly.
   Only then rewrite the primary. If the primary write, close or verification
   fails, retain `.new`. Remove it only after the primary is verified; a failed
   cleanup is harmless because a complete primary has precedence.

A full device that cannot stage a second copy **refuses the edit and preserves
the old list**. There is no direct-overwrite ENOSPC fallback. The original
fallback's failure was reproduced and reported in
[original issue #943](https://github.com/NathanNeurotic/Open-PS2-Loader/issues/943).
Free some space before retrying. Memory-card filesystems do not need rename for
this protocol; the primary rewrite still uses `O_TRUNC` with a verified recovery
copy present.

This provides logical recovery under the tested read/write/close/allocation
failures. Readback is not a physical-media flush or proof of power-loss durability.
A size-correct, syntactically complete primary has precedence; if it contains
unexpected edits after a device error, preserve both files for manual recovery.
Do not delete an unreadable file simply to make the next add succeed. First
copy both files elsewhere and recover entries or restore a known-good copy.
Device and interruption acceptance remains tracked in
[migration issue #20](https://github.com/NathanNeurotic/RiptOPL/issues/20) and
[original issue #943](https://github.com/NathanNeurotic/Open-PS2-Loader/issues/943).

The source fault tests run the actual reader, writer and editing methods under
ASan/UBSan, including restart-style snapshots at truncate/write/close/cleanup
boundaries and allocation failures. They establish host behavior; physical
memory-card, BDM, MMCE, HDD and network filesystem acceptance remains separate.

## Recent games (`recents.bin`)

`recents.bin` sits beside `favourites.bin` and uses the same OFAV record format. It holds the last
10 games launched, newest first, one row per game. A launch rewrites it only while the Favourites
page or All Games is on, or Sort Games is **Last played**. That rewrite sits between the launch
press and the game, so it is one write in place: no `.new` copy and no read-back, unlike
`favourites.bin`. A `recents.bin` the device cannot read is never replaced; one that reads but is
cut short or not understood is replaced by the next launch, as the list is rebuilt by playing.
See [Game library](GAME-LIBRARY.md) for what reads it. NOT TESTED on hardware.
