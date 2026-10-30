# ORBIT Neutrino runtime for the RA edition

Pinned binary distribution: [ORBIT v1.0.2](https://github.com/danielnuld/ps2-launcher/releases/tag/v1.0.2).
Source: [danielnuld/ps2-launcher at 7a88a6a5](https://github.com/danielnuld/ps2-launcher/tree/7a88a6a5fdc935001d977497460716762edef404/third_party/neutrino-igr).

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
starting with `RiptOPL ORBIT RA handoff v1` and `raagent.irx` present, so a stock Neutrino is never sent the unknown `-ra=` option. v1.0.2 sends each
snapshot in one packet: sets whose direct values exceed 1,216 bytes stay untracked. It
reads the direct entries only, so a pointer-chain tail is ignored (xeRAbora PROTOCOL.md)
and chain-based achievements do not unlock. It speaks the xeRAbora wire protocol, which
is also what Caduceus's engine receives, so both RA modes use it. Console unlock cards are
not claimed. ORBIT is Neutrino v1.8.0 based: Neutrino cheats are not available through it.

Do not replace this pin automatically when a new release appears: review the new
release's handoff, snapshot limit and chain support first, then update `release.json`,
`RA_ORBIT_SNAPSHOT_MAX` (`src/sources/supportbase.cpp`) and the tests together.
`.github/workflows/orbit-neutrino-watch.yml` checks ORBIT's latest release on Neutrino Watch's
schedule and opens the "ORBIT Neutrino RA runtime update" issue when it differs from this pin
(a newer tag, or the same tag with a replaced archive). It never edits the pin.

**NOT TESTED on hardware** in RiptOPL.
