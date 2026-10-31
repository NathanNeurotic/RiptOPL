# RiptOPL documentation

Current comparison: [Full ecosystem fork-gap refresh - October 29, 2026](FORK-GAP-FULL-REFRESH-2026-10-29.md) (301 provenance records; 152 stable families). Source/branch/release evidence and console acceptance are recorded separately.

Start with [installation and first boot](https://nathanneurotic.github.io/RiptOPL/install.html)
and [your first game](https://nathanneurotic.github.io/RiptOPL/getting-started.html).
The [website](https://nathanneurotic.github.io/RiptOPL/) provides searchable task guides.
Use the references below for implementation details, exact limits and contributor workflows.

- [October 29 ecosystem fork-gap reconciliation](FORK-GAP-ECOSYSTEM-AUDIT-2026-10-29.md) — current refs/releases, merged and pending work, and every stable record.

## Installation and troubleshooting

| Task | Reference |
| --- | --- |
| Choose an edition, configuration or package | [Release reference](../ROLLING_RELEASE.md) · [Build configurations](BUILD-CONFIGURATIONS.md) |
| Test a PR artifact or collect freeze diagnostics | [Test builds](TEST-BUILDS.md) |
| Understand defaults and saved settings | [Fresh-start defaults](DEFAULTS.md) |
| Diagnose setup or a failed launch | [Setup questions](SETUP-QUESTIONS.md) · [Troubleshooting](https://nathanneurotic.github.io/RiptOPL/troubleshooting.html) |
| Separate package, core, transport and launch-stage failures | [Launch diagnostics](LAUNCH-DIAGNOSTICS.md) |
| Recover a failed VMC creation | [VMC creation and recovery](VMC.md) · [Automatic VMC](AUTOMATIC-VMC.md) |
| Copy, move or delete files, back up saves as PSU | [File Manager](FILE_MANAGER.md) |
| Diagnose an APA disk without formatting it | [APA safety](APA-SAFETY.md) · [Recovery tool](../tools/pc/apa-recovery/README.md) |

## Sources and game cores

| Topic | Reference |
| --- | --- |
| Game image formats | [ISO, ZSO, CHD, CSO and server decoding](GAME-FORMATS.md) · [Compressed images](COMPRESSED-IMAGES.md) |
| USB, MX4SIO and iLink | [Device setup](https://nathanneurotic.github.io/RiptOPL/usb.html) · [MX4SIO launch notes](MX4SIO.md) |
| MMCE | [MMCE setup](https://nathanneurotic.github.io/RiptOPL/mmce.html) |
| Internal HDD | [APA/exFAT guide](https://nathanneurotic.github.io/RiptOPL/hdd.html) · [exFAT and APA-Jail](EXFAT.md) |
| SMB / UDPFS / UDPBD | [SMB](https://nathanneurotic.github.io/RiptOPL/smb.html) · [Network boot](https://nathanneurotic.github.io/RiptOPL/network-boot.html) |
| HTTP | [HTTP profile and limits](HTTP.md) · [Host conformance tools](../tools/pc/http/README.md) |
| Neutrino | [Installation, core selection, arguments, cheats and VMC](NEUTRINO.md) |
| PS1 | [POPSTARTER and Ember](VCD.md), including POPS UDPFS and device/core restrictions |
| RetroAchievements | [RA setup and runtime limits](RETROACHIEVEMENTS.md); requires its separate kit |
| Experimental hardware | [PC CARD CF](PC-CARD-CF.md) · [PSX DESR DVR](DESR-DVR.md) |

## Settings and customization

- [Controls](CONTROLS.md), [in-game reset](IGR.md), [GSM video](GSM.md) and [PADEMU](PADEMU.md).
- [Interface, folders and audio](INTERFACE.md), [theme authoring](THEME_ENGINE.md), [languages](LANGUAGES.md), [Unicode game titles](UNICODE-GAME-TITLES.md) and [CFG titles](CFG-TITLES.md).
- [Game library](GAME-LIBRARY.md): All Games, Recent, Sort Games, Random Game, Hide Game, Delete and JPG art.
- [Favorites storage](FAVORITES-STORAGE.md), [per-game parental locks](PARENTAL-LOCK.md) and [autolaunch/global settings](AUTOLAUNCH-GLOBAL-SETTINGS.md).
- [SIO2MAN compatibility](SIO2MAN-COMPATIBILITY.md) for contributors investigating VMC/pad hooks.
- [Notes for ps2sdk](PS2SDK-NOTES.md): the ps2sdk defects behind every IOP module RiptOPL builds or patches, with patches for ps2sdk.

## Migration and history

[ps2build migration](PS2BUILD-MIGRATION.md), the [C++ conversion plan](CPP-CONVERSION-PLAN.md) and the
[parity ledger](UPSTREAM-PARITY-2026-10-12.md) record how RiptOPL tracks Open-PS2-Loader.
[Fork-gap status: 2026-10-28](FORK-GAP-STATUS-2026-10-28.md) is the current summary: parity with the
legacy edition, fixes landed in both, the RIPPS2 audit and port plan, and the open hardware checks.
[Learning across PS2 loader projects](FORK-COLLABORATION.md) says what evidence a candidate from
another fork needs before it is ported, and [Maintaining documentation](MAINTAINING-DOCS.md) says
where each page of the website, wiki and these references is edited and how the wiki is rebuilt.

Integration plans, dated audits and community-report handoffs are engineering history. Their old
DONE/pending states, branches and package names describe that baseline; they are not current
installation instructions. Historical records: [gap ledger](DOCUMENTATION-GAPS.md),
[community reports](COMMUNITY_REPORTS_HANDOFF_2026-10-02.md), [contributor roadmap](TECH_CONTRIBUTOR_ROADMAP_2026-10-03.md),
[Ember plan](EMBER-INTEGRATION-PLAN.md), [HTTP plan](HTTP-INTEGRATION-PLAN.md),
[RA plan](RETROACHIEVEMENTS-INTEGRATION-PLAN.md), [VMC audit](VMC-AUDIT-2026-10-08.md) and [legacy changelog](OLD_DETAILED_CHANGELOG).
