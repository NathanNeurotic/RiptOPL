# Favorites storage and saves

Favorites use the shared OPL configuration directory's `favourites.bin`. The
file format remains OFAV version 3, with read support for versions 1 and 2 and
complete uOPL/wOPL imports. The three ps2build editions use the same policy.

Adding or removing a favorite first reads the stored list. A genuinely
missing pair of files (`ENOENT`) or a complete supported list permits an edit. A valid
header-only OFAV file is an editable empty list. A zero-byte file counts as no file. A truncated
record, invalid text terminator, unsupported version, oversized list, trailing
data, failed allocation, failed seek, read error or close error refuses the
edit. A salvageable prefix may still be displayed; it is never silently saved
over the original.
Unknown versions, oversized stores and IO/allocation failures keep edits refused
even when a recovery copy can be displayed. Existing failure feedback reports the refused edit and
does not confirm it or change the source star.

Ordinary reads load a bounded image instead of issuing a device read for each
record field. Positive short reads and writes accumulate, and clean EOF and
close are required. OFAV v1/v2/v3 and complete foreign imports retain their
existing decoding rules. Oversized paths are refused rather than truncated.

## Save rules

1. A save is one open and one write of `favourites.bin`, in place. There is no
   `.new` copy and no read-back; write and close results are checked and a
   failure is reported.
2. A complete, readable `favourites.bin` wins, including a PC-edited list or a
   valid empty OFAV list.
3. A `favourites.bin.new` left by an older build is still read when the primary
   is missing, empty or structurally incomplete. It is never written or removed.
   An unreadable or unknown-version primary never authorizes replacement.

Earlier builds wrote a `.new` copy, read it back, rewrote the primary, read it
back and deleted the copy on every save. That was three extra reads, a second
write, and a file created and deleted each time; on an exFAT drive the new file
also searched the drive's free-space map from its start on the first save since
power-on. The cost of the plain save: a power loss or a full device in the
middle of a save can leave `favourites.bin` short. It then shows the entries
that still read and refuses edits until the file is deleted or restored
([original issue #943](https://github.com/NathanNeurotic/Open-PS2-Loader/issues/943)
is that case on a full device).

The source fault tests run the actual reader, writer and editing methods under
ASan/UBSan, with failed opens, writes, closes and allocations. They establish host behavior; physical
memory-card, BDM, MMCE, HDD and network filesystem acceptance remains separate.

## Recent games (`recents.bin`)

`recents.bin` sits beside `favourites.bin` and uses the same OFAV record format. It holds the last
10 games launched, newest first, one row per game. A launch rewrites it only while the Favourites
page or All Games is on, or Sort Games is **Last played**. It is saved the same way, one write in place. A `recents.bin` the device cannot read is never
replaced; one that reads but is cut short or not understood is replaced by the next launch, as the
list is rebuilt by playing.
See [Game library](GAME-LIBRARY.md) for what reads it. NOT TESTED on hardware.
