# NULD (formerly ORBIT) Neutrino runtime for the RA edition

Pinned binary distribution: [NULD v1.2.14](https://github.com/danielnuld/nuld/releases/tag/v1.2.14) (ORBIT was renamed NULD at v1.2;
the runtime keeps the ORBIT name here and in the kit's file names).
Source: [danielnuld/nuld at 53abd1d3](https://github.com/danielnuld/nuld/tree/dd27a63006030ce07b31327b5cdeaedc09865d2b/third_party/neutrino-igr).

v1.2.14 (reviewed against v1.2.13): NULD adds Portuguese language support,
optional PS2-logo handling, and launcher cover features. RiptOPL extracts
only neutrino/, not the separate launcher. Published SHA-256 digests show
raagent.irx and udpfs_fhi.irx are unchanged from v1.2.13, but neutrino.elf
and ee_core.elf changed. RAN2 chains and the xeRAbora v2.1 RAH1 hash trailer
remain unsupported by NULD itself: RiptOPL validates RAH1 and gives NULD
a direct-entries-only watch list. Hardware launch behavior is unverified.

v1.2.13 (reviewed against v1.2.12): inside `neutrino/` only `modules/udpfs_fhi.irx` changed. `neutrino.elf`,
`modules/ee_core.elf` and `modules/raagent.irx` are byte-identical, so the `-ra=` handoff, the 1,216-byte
snapshot limit and direct-entry-only reading are unchanged. The new `udpfs_fhi.irx` (NULD commit 8b31753,
`patch_neutrino.py` step 6) finds the UDPFS server again after a lost link, for up to about a minute, and asks a
failed block read again over the new link; before, every later read of the running game failed. It restarts the
link at sequence 0, which PS2-Servers' UDPFS server treats as the same console coming back (its open handles are
kept). NULD's notes say this is not tested on a console yet.

Daniel / ORBIT supplies the modified Neutrino loader, EE core and achievement agent
(`raagent.irx`). Neutrino is by Maximus32; the achievement protocol and original agent
work are by hacan359 / xeRAbora. The Neutrino fork is AFL-3.0: its LICENSE and the source
reference travel with the distributed runtime. The GPL-3.0 ORBIT launcher is neither
bundled nor linked.

`tools/package_edition.py` (`stage_orbit`) checks the whole ZIP against `release.json`'s
SHA-256 and every required entry before writing, then extracts only `neutrino/`. One file
is replaced afterwards, exactly as in the standard kit: `modules/IEEE1394_bd_mini.irx`
becomes RiptOPL's i.LINK driver (original #1018: the stock driver loses its login after an
IOP reset). Standard kits keep the official upstream Neutrino; only the RA kit may contain
`raagent.irx`, `ORBIT-RA.*` and `RA-EDITION-NEUTRINO.txt`
(`.github/scripts/package_release_app.py` enforces both directions).

`sbNeutrinoPrepareRA` (`src/sources/supportbase.cpp`) hands a game over only when
`sysNeutrinoRARuntime` (`src/platform/sysneutrino.cpp`) finds `neutrino/ORBIT-RA.txt`
starting with `RiptOPL ORBIT RA handoff v1` and `raagent.irx` present, so a stock Neutrino is never sent the unknown `-ra=` option. v1.2.13 sends each
snapshot in one packet: sets whose direct values exceed 1,216 bytes stay untracked. It
reads the direct entries only, so a pointer-chain tail is ignored (xeRAbora PROTOCOL.md)
and chain-based achievements do not unlock. It speaks the xeRAbora wire protocol, which
is also what Caduceus's engine receives, so both RA modes use it. v1.2.14 can pause the game for
about 2.5 s under an unlock box when the client sends `RAU1 <id> <points>`; RiptOPL hands over no titles
for it and does not claim console unlock cards. ORBIT is Neutrino v1.8.0 based: Neutrino cheats are not available through it.

Do not replace this pin automatically when a new release appears: review the new
release's handoff, snapshot limit and chain support first, then update `release.json`,
`RA_ORBIT_SNAPSHOT_MAX` (`src/sources/supportbase.cpp`) and the tests together.
`.github/workflows/orbit-neutrino-watch.yml` checks ORBIT's latest release on Neutrino Watch's
schedule and opens the "ORBIT Neutrino RA runtime update" issue when it differs from this pin
(a newer tag, or the same tag with a replaced archive). It never edits the pin.

**NOT TESTED on hardware** in RiptOPL.
