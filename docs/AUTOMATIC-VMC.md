# Automatic VMC creation

In **Settings → Game Launching**, enable **Create VMC on First Launch** to create
and assign an 8 MB card when a PS2 game's slot 1 has no card assigned. The option
defaults off and is saved as `auto_create_vmc`. It applies to menu launches,
including PS2 Favorites, on all three ps2build editions. It does not change
command-line Auto Loading or create cards before the GUI is active.

The card lives in the game's source VMC directory. Its name follows the same
group/title-ID rule as the VMC screen, with `_0` for slot 1, so known multidisc
groups share a card. An existing formatted supported-size card under that name is reused.
Neutrino also receives an existing formatted odd-size card as is, following its own card
handling; native OPL requires a supported size. This option never resizes or
reformats an existing file. Assigned cards and disabled
slots remain unchanged. Slot 2 is untouched.

Apps and PS1 rows are skipped. HTTP has no writable VMC backend, and internal
APA HDD games under Neutrino have no supported VMC path, so those are skipped
too. Favorites resolve the owning source before applying this policy.
UDPFS Files uses Neutrino regardless of the saved loader preference, UDPBD and UDPFS IMG use
Neutrino unless the game's own Loader Core is `<OPL>`, and SMB uses the native core. The same effective-loader rules govern VMC checks and warnings.
The VMC configuration dialogs resolve the effective launch core for the selected
title, ensuring games launching through Neutrino suppress native mcemu single-extent
fragmentation warnings on card creation.

Creation shows progress and accepts the configured Cancel button. Cancellation
keeps the menu open and does not assign the card. The formatter removes its
cancelled partial image before reporting completion. A card that completed
before cancellation remains intact but is not assigned by that cancelled launch.
Working jobs have no automatic two-minute deadline: slow devices may legitimately
take longer. After Cancel, the menu waits up to 600 more status polls for cleanup.
If cleanup cannot be confirmed, it stays in the menu without assigning the card.
A blocked device RPC can still delay a poll; this is not a hard wall-clock timeout.
The formatter releases its abort semaphore before cluster seek/write, so the
abort/status request path can remain responsive while device IO is stalled.
Subsequent automatic launches refuse while the formatter still reports a job,
so a preallocated but unfinished image cannot be mistaken for a reusable card.

Unreadable images and images with an unsupported native size are preserved and
are not automatically assigned by that path. Failed creation submission or a reported formatting failure displays
an error and allows the game to launch without a newly assigned VMC. A settings
write failure is reported separately; the successfully created card is retained
and its assignment remains available to the current launch.
After successful creation, the source is probed again for a formatted 8 MB image
before assignment. Favorites re-resolve their source after modal progress;
changed rows, source instances, device state or source modes refuse continuation.

The menu owns its launch configuration across progress screens, then checks
that the selected row still has the same identity before handoff. A source
refresh cannot free that configuration or silently redirect the launch to a
different row. These safeguards and formatter scheduling/failure behavior have
host regression coverage. Physical-console creation, cancellation, device
removal and filesystem behavior still require acceptance testing.

## Native BDM cards: one reserved extent

When the effective core is native OPL, a new card on BDM storage (USB, i.LINK, MX4SIO, ATA, from
the VMC dialog, automatic creation or Favorites) is created only if the bundled FAT driver reserves
the whole card in one extent first. genvmc asks the driver for its versioned capability before
opening anything, creates the file exclusively (a file that appears meanwhile is kept), reserves,
formats, then verifies one synced extent inside the native 32-bit sector range. An old driver, a
card with no free run large enough, or a failed verify or close is reported as a failure, and only
this job's new file is removed. Neutrino cards and other sources allocate as before. Existing cards
are never relocated. Any failure after the formatter opened a card it owns removes the partial file.
Host image tests cover FAT32 and exFAT; console acceptance is still required.

## Rebuilding a card the native core cannot map

A card that already holds saves but is too fragmented for the native core (on BDM: more than one extent; on the HDD: a PFS map longer than native mcemu holds) is skipped at launch. The per-game VMC dialog shows such a card as **Too fragmented for the native core** and offers a **Rebuild** button. It appears only for a valid 8/16/32/64 MB card on a BDM (FAT32/exFAT) or HDD source whose game uses the native core, and only when the layout check gives a definite answer; Neutrino maps cards its own way, and other stores cannot say how a card lies.

Rebuild asks for confirmation, then:

1. Refuses to start if `VMC/<name>.bak` or `VMC/<name>.new` already exists (absence is proven by reading the folder). Either may hold saves, so neither is ever overwritten; move it away on a PC and try again.
2. genvmc creates `VMC/<name>.new` exclusively, with the same reservation a new card gets: on BDM the whole card as one extent through the versioned driver API (no room means the job stops at once); on the HDD every zone in one PIOCALLOC request, and here a refusal also stops the job.
3. genvmc copies every byte of the card into the copy, then reads both back and compares them byte for byte. On BDM the driver then confirms one extent within the 32-bit sector range.
4. The loader runs the launch's own layout check on the copy (BDM: one extent covering the card; HDD: the PFS map fits).
5. Only then two renames swap them: the card becomes `VMC/<name>.bak`, the copy becomes `VMC/<name>.bin`. On the HDD the PFS cache is committed after the renames.

The card itself is only ever opened read-only. On any failure or cancellation before the swap, only the `.new` file this rebuild created is removed, and the card is left as it was. Not enough unbroken free space is reported as such, with nothing changed. If the second rename fails, the first is undone. If a rename cannot be undone, both `.bak` (the old card) and `.new` (the verified copy) are kept and the dialog says so; rename one of them back to `.bin` on a PC. On the HDD, if PFS cannot commit the swap, the dialog says the change was not confirmed instead of reporting success. A copy is removed only while the card is confirmed under its own name. The loader never deletes the card or the backup: delete `VMC/<name>.bak` on a PC once the rebuilt card's saves check out. If power is lost between the two renames, `VMC/<name>.bak` holds the card.

Host tests run genvmc's real request, worker, copy and verify code and the loader's real start, layout check and swap over an in-memory filesystem with a fault at every step: reservation refusal, card read error, short copy write, copy reading back different, failed close, driver layout failure, a copy whose layout fails or cannot be checked, each rename failing or reporting failure after it happened, a `.bak` or `.new` already present or appearing during the job, an unreadable folder, and an abort during the copy and during the verify, on both BDM and PFS. A second test writes a card interleaved with other files on disposable FAT32 and exFAT images (the vendored FatFs, fragment walker and allocation guard from modules/bdm/bdmfs_fatfs), confirms the launch check calls it fragmented, gets "not enough unbroken free space" with nothing changed while no 8 MiB run is free, then rebuilds it to one extent with identical bytes and the old card kept as `.bak` across a remount. None of this establishes real-console behaviour; the rebuild has not been run on hardware.

## Failed-format retry protection

Size alone is not proof of a formatted card. A formatter failure after erasing
the complete image can leave an 8 MB file without its final metadata. Automatic
assignment now uses a read-only format probe on existing files and again after
creation: a complete PS2 header, matching file length/geometry and successful
seek/read/close are required. Short positive reads accumulate; missing bytes,
erased headers, unsupported geometry and IO failures refuse assignment. The
formatter writes this header last, after IFC/FAT/root metadata. The probe does
not depend on the previous worker's error status, so the check also runs after
restart and for another title sharing the same image.

Invalid images remain untouched; the option neither truncates nor deletes them.
Back up an image before manually recovering or explicitly deleting it. Valid
formatted odd-size images remain eligible for Neutrino; native OPL still requires
a supported size. Manual selection and the existing creation/deletion interface
retain their separate behavior. See
[original issue #941](https://github.com/NathanNeurotic/Open-PS2-Loader/issues/941)
and [migration issue #20](https://github.com/NathanNeurotic/RiptOPL/issues/20).
This is header/geometry validation, not a filesystem integrity scan or a media
flush/power-loss guarantee. A valid header cannot establish every FAT/directory
entry or existing save's health. Physical-device acceptance remains open.
