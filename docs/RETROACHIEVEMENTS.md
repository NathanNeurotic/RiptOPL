# RetroAchievements

RiptOPL can report a running game's memory to a PC client that runs
[rcheevos](https://github.com/RetroAchievements/rcheevos) on the console's behalf, so achievements
unlock on real PlayStation 2 hardware.

The console does two things: it works out **which** game an image or physical disc is (a hash), and once the game is
running it streams the handful of memory addresses that game's achievements depend on. Everything
else — talking to the RetroAchievements servers, deciding what unlocked, your login — happens on the
PC.

Based on the RetroAchievements implementation by **[hacan359 (yoba)](https://github.com/hacan359/Open-PS2-Loader/pull/1)**.
Use the upstream **[xeRAbora PC client](https://github.com/hacan359/xerabora)** with RiptOPL's RA build. Download the required **[v1.0.1 release](https://github.com/hacan359/xerabora/releases/tag/v1.0.1)**.

RiptOPL's real-hardware stabilization was contributed by **[oMrRexD](https://github.com/oMrRexD)** through
PRs **#702–#705**, covering the ee_core stack squeeze, MMCE DEV9 dependency, cold-launch network
bring-up / unusable-IP fallback, and the misplaced RA settings rows.

## Mode 6 (IGR disabled)

When per-game Compatibility Mode 6 disables IGR, the RA build keeps the in-game snapshot tick through a dedicated VBLANK handler, but only when telemetry successfully initialized. It installs no IGR worker or pad-open hooks. Achievements can still be recorded; controller IGR and the client's **RESET CONSOLE** are unavailable. Mode 6 does not help a game that rejects a VBLANK handler itself. This follows xeRAbora alpha.17. **NOT TESTED on hardware** in RiptOPL.

---

## Status

**Implemented, and telemetry has run end to end on real PS2 hardware** in [oMrRexD](https://github.com/oMrRexD)'s session for
PRs #702–#705: an SCPH-50001 with an expansion-bay network adapter, a static IP and [xeRAbora](https://github.com/hacan359/xerabora)
v0.1.0-alpha.10. Launched from MMCE, Need for Speed: Underground 2 streamed 25,200 snapshots with none
lost and unlocked an achievement with the overlay on screen; the same build also tracked the game from
USB. That is one console, one game and a static address. DHCP, the other launch paths and the edge
cases below remain development-grade and should still be validated individually before being treated
as finished.

| Part | State |
| --- | --- |
| RetroAchievements editions (`ra`, `ra_incebtion`) | done |
| In-game telemetry (`raudp`, ee_core snapshots) | **hardware-tested** from MMCE and USB, static IP (#702–#704) |
| Watch-list loading from `<device>/RA/<serial>.wl` | done |
| Image hashing (ISO9660 walk + MD5) | done; images on a share and on a USB stick hashed to matching values on hardware ([hacan359](https://github.com/hacan359), 2026-09-10) |
| Menu actions: check game support, test PC link | both used on hardware: the support check's hashing by [hacan359](https://github.com/hacan359), the PC link test in #704 |
| List badges and the cover mark | written, **not yet confirmed on hardware** |
| Unlock overlay | **hardware-tested**: the gold overlay showed over the running game on an unlock (#704) |
| Settings: RA telemetry, RA badges | done; on the **Network** page since #705, where switching telemetry off and on was checked on hardware |
| Disc in the tray | implemented in the RA build; **not hardware-tested** |

---

## Using it

### First tracked game

1. Start **RIPTOPL-RA.ELF** from the RetroAchievements kit (`rolling-ra`). The Korium and
   IncebtIon kits have no achievement telemetry and need no companion PC.
2. Run **xeRAbora v1.0.1** (alpha.17 or later) on the companion PC and sign in to your RetroAchievements
   account in its Settings page. Keep it running.
3. In **Settings → Network**, leave **RA Mode** on **Xerabora**, turn **RetroAchievements
   telemetry** on and save. Telemetry is Off on a fresh start. Give the PS2 working network
   settings on the same LAN as the PC, and set **RA companion IP** to the PC's IPv4 address to
   target it directly (0.0.0.0 finds it by broadcast).
4. For a first test use a PS2 image on USB or MMCE, with **OPL** chosen as the game's core. The
   RA kit's Neutrino has its own limits (see [Neutrino in the RA edition](#neutrino-in-the-ra-edition));
   the other launch paths and their state are under [Which launch paths can work](#which-launch-paths-can-work).
5. On the game's menu run **Test RetroAchievements PC link**, then **Check RetroAchievements
   support**. The first proves the console reaches xeRAbora; the second fetches the watch list.
   Let xeRAbora through the PC firewall, including inbound UDP 18194.
6. Launch the game after a successful check, and confirm xeRAbora's LIVE view receives snapshots.
   The list badge alone does not prove in-game telemetry.

Run the support check once for each image you want tracked. A game without a watch list starts
an ordinary, untracked game. An unknown hash can mean a different region or dump, or a game with
no set; it does not mean the storage source is unsupported. For Caduceus, select that mode and
complete its pairing instead (see [RA Mode and the companion PC](#ra-mode-and-the-companion-pc));
run one achievement service per PC.

### Settings and menu actions

The settings, on the **Network** settings page:

| Setting | Default | Effect |
| --- | --- | --- |
| RetroAchievements telemetry | **Off** | The master switch. While it is off an RA build launches a game for exactly what the standard build costs — no in-game network stack, no `raudp`, no snapshots. |
| RetroAchievements badges | On | Shows the RA mark in the game list and over the cover. Cosmetic, and it follows the master switch: badges stay dark while telemetry is off. |
| RA mark in game list | Prefix | Where the list mark goes: **Prefix** (`RA Name`) or **Suffix** (`Name [RA]`). Both sort by the clean game title and keep the file name for Favorites, rename and artwork. |

Two actions on a game's own menu:

* **check game support** — hashes the image, asks the PC client about it, and writes
  `<device>/RA/<serial>.wl` if the game is tracked.
* **test PC link** — confirms the console can find the client at all, without touching a game.

A game the console has a watch list for shows the badge and the cover mark after the next list
refresh. Switch telemetry on and launch it.

---

## Physical discs

Use an RA build and the upstream xeRAbora PC client. Insert a PS2 game disc, then open
RiptOPL's main menu:

1. Choose **RA: check disc support**. RiptOPL hashes the disc's boot executable and asks the PC
   client for its watch list. The check runs in the background and reports its result on screen.
2. Enable **RetroAchievements telemetry** in **Network** settings.
3. Choose **RA: launch disc**. This action requires a watch list for the disc currently in the tray.

Disc watch lists and hash logs use `RA/` inside the active settings folder: for example,
`mc0:/OPL/RA/SLUS_201.74.wl` and `mc0:/OPL/RA/hashes.txt`. The settings device must remain available
through the support check and launch preparation. RiptOPL also keeps the returned list in
memory for the current session, so a failed file save does not discard that result.
If the menu is using UDPBD or UDPFS, switch that network protocol off before asking the PC client
for support; it cannot take over their network stack. A previously saved watch list can still be used.

**Launch PS2 Disc** keeps the existing normal disc boot path. Use it when you want to launch
without achievements, including when the PC has no watch list for that game.

The RA path retains the console's physical-disc drivers and PS2LOGO authentication, then adds the
OPL telemetry core. It accepts PS2 CD/DVD boot executables in the disc root, with names up to
15 characters. Nested boot paths, PS1 discs and video DVDs are not supported by this RA action.
Physical memory cards are used; image-specific VMC, cheats, GSM, pad emulation, language overrides
and storage-emulation patches are not applied. The thread/pad fixes for Eutechnyx titles,
Shaun Palmer's Pro Snowboarder and PAL .hack selectors remain enabled because disc launches retain
OPL's hooks. The power-off controller combo is unavailable on this
path. Disc boot, telemetry across game IOP resets and the IGR return path still need PS2 validation.

---

## Which launch paths can work

Telemetry lives inside OPL's own loader core, which only exists for launches OPL performs itself.

| Launch path | Supported |
| --- | --- |
| BDM — USB, iLink, MX4SIO, ATA/exFAT | yes |
| ETH / SMB | wired, but **known bad upstream** — see below |
| HDD (APA) | yes; the support check reads HDLoader installs read-only (see below) |
| MMCE | yes |
| Physical PS2 disc, **RA: launch disc** | implemented; **not hardware-tested** |
| HTTP | local watch-list loading is wired into OPL-core launch; combined HTTP/RA operation is not hardware-validated, and it shares the ETH/SMB risk below |
| Neutrino core (`$CoreLoader`) | RA kit's ORBIT runtime only (below); **not hardware-tested** |
| UDPFS | RA kit's ORBIT runtime (below), with the support check run from the menu over the UDPFS link; **not hardware-tested** |
| UDPBD / UDPFS (Files or IMG) on `<OPL>` | **no** telemetry: OPL's RA module needs the SMB network stack, which cannot share the adapter with the UDP driver |
| PS1 / USB VCD (POPStarter) | yes, through its own module (below); **NOT TESTED on hardware** |
| PS1 / Ember, MMCE, APA HDD, SMB and non-USB BDM | **no** |

Neutrino (and so UDPFS) and PS1 hand the console over to an external ELF and never load OPL's core, so
ee_core has nothing to take a snapshot from. The RA kit's Neutrino carries its own agent (below), and a USB
VCD carries its own reader into POPStarter (see [PS1 through USB POPStarter](#ps1-through-usb-popstarter)).

### Neutrino in the RA edition

The RetroAchievements kit's `neutrino/` is **[ORBIT v1.0.2](https://github.com/danielnuld/ps2-launcher/releases/tag/v1.0.2)**'s
RA-Neutrino by **Daniel / [danielnuld](https://github.com/danielnuld)**, based on [Neutrino](https://github.com/ps2max32/neutrino) by **[Maximus32](https://github.com/rickgaiser)**, with an achievement agent
(`raagent.irx`) that speaks **[hacan359](https://github.com/hacan359) / [xeRAbora](https://github.com/hacan359/xerabora)**'s protocol. Its AFL-3.0 licence, source commit and
archive SHA-256 ship beside it (`neutrino/ORBIT-RA.json`). Standard kits keep ordinary upstream Neutrino,
and an ELF-only update does not install this runtime: copy the RA kit's whole `neutrino/` folder.
`neutrino/RA-EDITION-NEUTRINO.txt` labels it. ORBIT is built on **Neutrino v1.8.0**, so every Neutrino
launch in the RA edition lacks later upstream changes, Neutrino cheats included. RiptOPL replaces only its
i.LINK pair, `IEEE1394_bd_mini.irx` and `iLinkman.irx`, with the same builds as the standard kit.

For a game with a saved watch list, RiptOPL writes `ript-ra.wl` and `ript-ra.toml` next to the cheats file
(`config/` under the effective `-cwd`, or beside `neutrino.elf` without one) and adds `-cfg=ript-ra -ra=...`;
the generated config keeps the user's `-cfg` and the cheats config through `depends`. That folder must be
writable, and a raw `hdd0:` path is refused. Both RA modes work: Caduceus's engine receives the same wire
protocol. The agent sends to the RA companion IP, else the verified peer. Network games (UDPFS, UDPBD) need
a static console IP; other devices raise the menu network first, unless a UDP transport holds the adapter.

The agent sends each snapshot in one packet, so a set whose **direct values exceed 1,216 bytes** cannot be
tracked. It reads the direct entries only and ignores a pointer-chain tail, as xeRAbora's protocol defines
for an agent without chain support: chain-based achievements do not unlock on Neutrino launches while the
rest of the set does. A game without a watch list launches quietly; any other reason the handoff cannot be
made (no ORBIT runtime, no companion address, a failed write, or the two arguments pushing the launch past
Neutrino's 256-byte budget) launches the game without achievements and says so. **Console unlock cards and
remote reset are not provided by this runtime**; unlocks appear on the companion. **NOT TESTED on hardware.**

### Do not expect a game with achievements to run from a network share

A game that streams its own disc over the NIC is competing with telemetry for that NIC. This build no
longer *reads* from the network in play on such a launch — `raudp` stops walking the SMAP receive ring
and stops polling the stack, because the game's disc stream arrives through both — but **[hacan359](https://github.com/hacan359)
reports on hardware (2026-09-05) that this is not enough: a game with an achievement set still stops
loading about a minute in from a share, so the send path disturbs the stream on its own.** [xeRAbora](https://github.com/hacan359/xerabora)'s
own documentation says the same, and names the USB stick and the original disc as the only tested ways
to play.

Since then, on SMB and HTTP launches `raudp` no longer opens or polls a socket at all. It learns the
companion's MAC from the stack's ARP table (asking once by raw ARP if it is missing, through the
gateway when the companion is routed), the in-game SMAP driver sets aside RA datagrams it receives for
the game anyway, and a snapshot waits while the game has a received frame queued. **This is NOT TESTED
on hardware** and does not make a share a supported way to play yet.

That defect is upstream's and ours alike; we have not reproduced or ruled it out on our own hardware.
The hardware testing so far (#702–#705) launched from MMCE and USB, never from a share. Until a share
is tested, **keep the images you play with achievements on a local device.** The same reasoning
applies to HTTP, which streams down the same path.

**HDD (APA) / HDLoader games** have a **read-only support check**. It reads the installed game's
1024-byte HDLoader descriptor, validates its logical-to-ATA sector map (refusing invalid or overlapping
extents, or a descriptor whose boot executable does not match the selected entry), and hashes the boot
executable through the same ISO9660 walker as an image. It never rewrites or converts a partition. The
list is saved under the HDD settings home as `RA/<startup>.wl`; a list placed by hand still loads. A
synthetic split-partition test matches the image hash; **NOT TESTED on hardware**, including its speed.

### An image an earlier run left open on the share can still be hashed

Checking a game on a share used to fail outright once a console had played that game and been
switched off: OPL logs off from nothing on power-down, the SMB session dies with the console, and a
cheap server never reaps the handle. From then on the server refused every NT open of that file with
a sharing violation, so "check game support" could only report that it could not open the image —
while the game still *booted* off it, because the in-play open uses the older command, which the same
server grants in compatibility mode.

RiptOPL now carries its own `smbman` (`modules/network/smbman-ra`, first in this flavour and now in every
edition) that tries NT create first and falls back to the legacy open **only** when the server says the file is held, and **only for a
read** — reaching past another handle to write is not something worth asking for. If both are
refused, the hash log names the image as held instead of saying nothing. [hacan359](https://github.com/hacan359) confirmed this on
hardware on 2026-09-10: two images held open by earlier runs hashed anyway, and the hashes matched
the ones taken from a USB stick.

Note this fixes *hashing*, not play: a game with an achievement set still stops about a minute in
when streamed from a share, per the section above.

## PS1 through USB POPStarter

Follows the original RiptOPL, which adapted hacan359/Open-PS2-Loader `531aad45` (xeRAbora alpha.16).
Put a stock `POPS/POPSTARTER.ELF` and the `.VCD` games in the USB device's root `POPS/` folder.

- **Support check.** **RA: check game support** on a USB VCD row (or its favourite) hashes the
  executable `SYSTEM.CNF` names, with the `PSX.EXE` fallback, as RetroAchievements does for PS1. The
  watch list is keyed by a 15-character `P` key from the normalized VCD path, since many PS1 images
  share a boot name, and saved with an `RA/<key>.md5` guard holding the image's hash. Other PS1 rows
  say that PS1 achievements need a USB VCD.
- **Launch.** With a list and a matching guard, telemetry on and the network up (the same link, DHCP
  and Caduceus session checks as a PS2 launch), the loader writes `POPS/MODULE_9.IRX`: `rapops` with the
  network settings, the watch list and the six modules it starts (DEV9, SMSUTILS, SMSTCPIP, SMAP,
  `raudp`, `rapull`) packed into its configuration block. POPStarter loads it after its IOP reset;
  `rapull` reads PS1 RAM through SIF from **25 seconds after launch**, and `raudp` sends the snapshots
  to the companion. Unlocks show on the companion only.
- **Fails open.** No list, an old or mismatched guard, telemetry off, no network, low memory, an
  unwritable slot or modules that do not fit all launch the game untracked. A partly written module
  is deleted; one that cannot be deleted blocks the launch rather than boot a broken POPStarter.
- **Your `MODULE_9.IRX` is never touched.** Only a file carrying `rapops`' marker is RiptOPL's. A
  tracked launch with your module in the slot stops with a message; move it to track the game. The RA
  build removes its own leftover before every BDM VCD launch. The standard build, and SMB and MMCE VCD
  launches, ask before deleting one, then offer **Continue anyway**; declining cancels the launch.

- **Menu, badges and browser.** A POPStarter row's menu has the RA entries (Ember rows do not). A
  tracked USB VCD, and its favourite, carries the RA badge; the badge looks only on the game's own
  device, as POPStarter's reader does, never on the share. **View achievements** opens the set by the
  VCD's executable hash and returns to the PS1 menu.

Host tests cover the hashing, module ownership, the handoff and the reader; **none of this
is tested on a console**, and POPS compatibility is not established by a build.

## Networking

Telemetry is UDP, sent by an IOP module that builds Ethernet frames by hand rather than going through
the network stack — the stack's mailbox is shared with the game's own traffic, and 60 sends a second
pushed the game out of it.

Because of that, **the console needs a valid IP even when the game was not launched over the
network.** OPL's own network settings supply it: whatever address, netmask and gateway are configured
there are handed to the in-game Ethernet driver on every RA launch. If those are wrong or unset, the
game runs normally but nothing reaches the PC.

A launch with a watch list and telemetry on brings the menu's network up first when it is not up
already, so it takes a moment longer. That is not optional: the in-game DEV9 driver does not power
the network adapter on, it expects the menu to have done it, and without it no packet leaves the
console. It is also how the console learns its address when it uses DHCP. If the network cannot
come up -- no cable, no adapter, no usable IP address or DHCP lease -- the game launches without
achievements and the menu shows a notice. With the adapter powered and no link, the in-game
network driver would otherwise wait for a cable for ever and the game would never start.

### RA Mode and the companion PC

**Settings → Network → RA Mode** picks the PC service: **Xerabora** (the default; configurations
without `ra_mode` keep it) or **Caduceus**. It does not switch telemetry on by itself; **RA Telemetry**
still does. **RA companion IP** names the PC that runs the service; **0.0.0.0** keeps the fallback.

| | Xerabora | Caduceus |
| --- | --- | --- |
| Finding the PC | broadcast on UDP 18194, or unicast to the companion IP when one is set | the companion IP, else the SMB server; never a broadcast |
| Before a tracked launch | the menu's link check | a session check on the bridge, UDP 18197 (`CADQ2`/`CADR2`); offline or signed out launches untracked |
| In the game | `raudp` asks the PC again, pinned to the companion IP when one is set | `raudp` re-probes the bridge on that host only, then talks to the engine on UDP 18194 |

Replies are accepted only from the host asked, and only as `RAO1 OK`. UDP **18195** is the console's
telemetry and notice port and **18196** its menu reply port; allow them through the PC's firewall.
RiptOPL never stores a RetroAchievements password or key. Caduceus here covers the session check,
watch lists, telemetry, in-game notices and the achievement browser below. **Caduceus end-to-end
console tracking is NOT TESTED on hardware.**

In the game an unlock shows as a gold pulse (Xerabora always, Caduceus by default). **Settings →
Network → Caduceus experimental cards** (Off by default, saved as `ra_experimental_cards=1`; any other
value reads as Off) instead draws a small card with the achievement's title and points in the upper
right. The row is available only with RA Mode Caduceus and telemetry on, and a switch away keeps the
saved choice. With cards Off a launch reserves nothing extra and installs no GS tracker. With cards on,
a tracked launch reserves about 30 KiB more in module storage, kept below 1 MB when the storage starts
there; without room, or for a display the renderer cannot draw on, the pulse is used instead. The
renderer is the original's (adapted from [Rian6's caduceus-opl](https://github.com/Rian6/caduceus-opl/tree/2a98a9d7361f8e33f421244bc88c318eab3bdf2f),
AFL-3.0): it checks the GIF and display state before it draws, which does not prove a game's graphics
are safe. **Cards may flicker or freeze a game; if one does, turn the setting off. NOT TESTED on
hardware.** POPStarter PS1 unlocks stay on the companion.

The PC can also send **RAR1**, which leaves the game for the menu exactly as the IGR combo does.
Under Compatibility Mode 6, which has no IGR, the request is ignored.

### Achievement browser

The main menu's **Caduceus achievements** or **RetroAchievements library (xeRAbora)** entry (named
after RA Mode) lists the account's games with progress; pick one to see its achievements, points,
descriptions and unlock dates. **View achievements** in a game's menu hashes that ISO and opens its
set directly; HDLoader and PS1 rows use the library instead. **L1/R1** pages, **Square** cycles All /
Earned / Locked / Hardcore, **Select** refreshes and Back returns. Pages load on the I/O thread, and a
launch or a network settings change waits until the page has loaded.

- **Caduceus** needs **ART/CADUCEUS.KEY** (the companion's 64-character pairing key, not an RA
  password) on the SMB share, or with **UDPFS Files** in the folder PS2-Servers shares (`ART/` or
  `art/`), where optional `ART/<icon>_RA.png` badges also live. Requests go to the RA companion IP,
  else the SMB server, on UDP **18198**; never to a broadcast. UDPFS IMG and UDPBD have no folder for
  the key. The LAN protocol is unencrypted: keep the share to trusted devices.
- **Xerabora** reads the xeRAbora PC's own interface on TCP **18280**: turn on **OPEN TO THE NETWORK**
  in xeRAbora and allow it through the firewall. The PC is the RA companion IP, a verified peer, or
  whoever answers discovery with `RAO1 OK`. A game card first asks retroachievements.org for the
  image's game ID over HTTPS (ORBIT's lookup), so even a game never played shows its whole list.
  Without internet, or with the console clock unset (the certificate check needs it, and the browser
  then says so), the set is found by its title in the account library instead: a game never played
  then shows its title and achievement count with a note to play it once. A failed connection is
  tried once per session; **Select** tries again. TCP needs the menu's own network stack, so UDPFS
  and UDPBD cannot open it.
  A set opened from a game's **View achievements** is also saved as `<device>/RA/<serial>.ach` beside
  the watch list; with **In-Game Menu** on, the game's next launch shows it on the menu's Achievements
  page with what is left to hunt ([IGR.md](IGR.md)). Open it again to bring the list up to date.
- **The HTTPS lookup** uses [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) 3.6.7 (RA edition only,
  Apache-2.0, `LICENSE-MBEDTLS.txt` in the package) with TLS 1.3/1.2 and full certificate checks
  against five embedded roots (`third_party/ca-roots`). The PS2 has no hardware random number
  generator: the TLS keys are seeded from timer jitter.

**The browser is NOT TESTED on hardware.**

The PS2 console core follows **xeRAbora v0.1.0-alpha.15** (client
[`d36ee3a5`](https://github.com/hacan359/xerabora/commit/d36ee3a54c8769825c99756f908367b2cc2622f3),
which pins console [`f4d559a3`](https://github.com/hacan359/Open-PS2-Loader/commit/f4d559a38f510bf644ac0f3f23431f15f6c9dc3a)),
and the Mode 6 snapshot handling follows alpha.17. The console fix in alpha.18 (console
[`c6e14e6b`](https://github.com/hacan359/Open-PS2-Loader/commit/c6e14e6b943be74210981e3a7bd51a25702a8df0):
a watch-list chunk whose data began with a whitespace byte lost that byte, and the list was refused)
was already in RiptOPL: the `RAC1` header ends at the length and its one space is stepped over by hand.
Updating the external client alone does not update the telemetry code in your ELF.

- **Pointer chains:** xeRAbora compiles pointer chains into nodes (parent, static offset, read size) sent as a tail after the watch addresses. `ee_core` walks the nodes once per frame and appends the dynamic `(address, value)` pair per node. A base that resolves to 0 (a null pointer or an out-of-range parent) sends address 0 instead of reading through it.
- **Larger achievement sets:** up to 4,096 direct watches and 128 pointer-chain nodes, subject to the snapshot byte limit. The block in module storage is sized to the actual set, so ee_core's limited memory carries no tables.
- **Bounded telemetry cadence:** up to three snapshot packets per video frame on average. Sets needing four to six parts are sampled every second frame, seven to nine parts every third frame; smaller sets every frame.
- **Read-cost diagnostics:** the PC receives the COP0 ticks spent reading a snapshot and the ticks per video frame.
- **Watch-list validation and read safety:** only 1-, 2- and 4-byte entries with an exact declared byte sum are accepted. Addresses outside `0x00080000..0x02000000` (some sets, like X-Men Origins, carry `0x00000000`) are never read from the VBlank handler, where they took a TLB miss and hung the game; they are sent as 0 so the snapshot keeps its shape.
- **Set identification:** while the PC replies WAIT, the console pauses 500 ms between up to 20 rounds. A newer client's refusal reason is shown rather than a generic unknown-image message.
- **PC-requested return:** the client's return command takes the existing IGR path, so it is unavailable in Mode 6.
- **Snapshot delivery:** telemetry polls every 4 ms, sends each new snapshot once, retries busy or torn copies, repeats an unchanged snapshot after ~1 second, and marks the staged sequence sent only after every part is accepted.

> [!IMPORTANT]
> Use **[xeRAbora v1.0.1](https://github.com/hacan359/xerabora/releases/tag/v1.0.1)** with this build; alpha.17 or later works. The console wire protocol is unchanged since alpha.10. Since alpha.17 the client has only gained a setting that keeps unlock notices off the TV (it then sends nothing) and reads optional link counters from the snapshot header. That is from a source review of xeRAbora `v0.1.0-alpha.17...v1.0.1` and its console fork `fa2d2e42...a9f97397`; **v1.0.1 is NOT TESTED on hardware with this build.** Host tests and successful builds do not establish console timing or game compatibility; the alpha.10 hardware report above is historical evidence, not validation of this update.

---

## Files on the card

```
<device root>/RA/<serial>.wl        e.g. mass0:/RA/SLUS_210.65.wl
```

One watch list per tracked game, generated by the PC client from that game's achievement set. `RA/` follows the source’s support-file prefix, like `CHT/`. HTTP loads an existing watch list from the local settings home because the server is read-only; do not assume automatic image hashing/support detection works for a remote HTTP ISO. RiptOPL creates it along with the other library folders.

Image launches with a missing watch list proceed normally without telemetry or its extra modules.
The explicit **RA: launch disc** action instead asks you to check support first; **Launch PS2 Disc**
remains available for a normal disc boot.

---

## Building

```sh
python tools/build_edition.py ra             # Korium theme
python tools/build_edition.py ra_incebtion   # IncebtIon theme
```

Each edition build regenerates its assets and objects under `build/editions/<edition>-gcc`, so
switching between RA and a standard edition needs no clean step. See
[release channels and packaging](../ROLLING_RELEASE.md) for packaging.

The `rolling-ra` channel publishes the kit as **`RIPTOPL-RetroAchievements-<version>.zip`**, built to the same
shape as the standard kit: `POPS/`, `EMBER/`, `neutrino/` (here the
[ORBIT Neutrino RA runtime v1.0.2](https://github.com/danielnuld/ps2-launcher/releases/tag/v1.0.2), see
[Neutrino in the RA edition](#neutrino-in-the-ra-edition)) and the PC-tool shortcuts, with
`APPS/APP_RIPTOPL-RA/RIPTOPL-RA.ELF`, matching `ART/RIPTOPL-RA.ELF_*.png` artwork, and
`APP_RIPTOPL-RA.psu`. The PSU contains only the direct files of `APP_RIPTOPL-RA`; import it with a
PS2 save manager for a memory-card installation, then copy the other folders separately.

The same channel carries the IncebtIon-theme kit (`RIPTOPL-RetroAchievements-IncebtIon-<version>.zip`, with
`RIPTOPL-RA-IncebtIon.ELF` and `APP_RIPTOPL-RA-IncebtIon.psu`; inside it the app is still
`APP_RIPTOPL-RA`), and `RIPTOPL-RA-VARIANTS-<version>.zip` with the `pademu0` loader (pad emulation compiled
out); `RIPTOPL-RA-VARIANTS-IncebtIon-<version>.zip` is the same loader with the IncebtIon theme. The kit also carries **`xeRAbora.url`** (pointing directly to the paired
[v1.0.1 release](https://github.com/hacan359/xerabora/releases/tag/v1.0.1)),
because the loader does nothing without the PC client.

The standard editions are unaffected: every RA source file and call site is behind
`#ifdef RETROACHIEVEMENTS`, the standard kits reject RA resources, and every build manifest
records the RA flag.

### Two IOP modules RiptOPL builds instead of taking from the SDK

Both are ps2sdk sources vendored with a marked change, the way `modules/network/ps2ips` already is,
built by `ps2.yaml` (`usbd_mini_fork`, `smbman_ra`). Both began in the RA edition; every edition now
embeds both. [Notes for ps2sdk](PS2SDK-NOTES.md) lists the ps2sdk side of both, and of every other
SDK module RiptOPL replaces.

| Module | Why | Diff from ps2sdk |
|---|---|---|
| `modules/network/smbman-ra` (every edition) | the held-image fix above; also a write now counts only the bytes the server's WRITE_ANDX Count says it stored, where ps2sdk's smbman takes any success status as the whole write. That matters on a server that answers a full disk, or a partial write, with success and a short or zero Count, which MS-CIFS allows; Samba, Windows (per MS-CIFS) and PS2-Servers answer a full disk with an error status, which both copies already fail. A write request also carries at most 65,534 bytes, so its 16-bit ByteCount no longer wraps to 0 | `smb.c` (+57/−8: the three fixes), `smb.h` (+4) and `auth.c` (+6/−1: an unmarked password-buffer bound); every other source file byte-identical to ps2sdk master, checked 2026-10-27 by the original's #1064; only the build file is ours |
| `modules/usb/usbd-ra` | see below | **nothing** — byte-identical to ps2sdk `iop/usb/usbd` at `314d87e7` (2024-04-01); only the Makefile is ours |

**The usbd swap is not a RetroAchievements feature.** ps2sdk rewrote its USB host driver on
2024-09-04 (`b1f7ff96`: 28 files, +4446/−3336), and since then a CD-era game run off a USB stick
loses its controller *and* its sound at the game's first IOP reboot — Dynasty Warriors 2 on real
hardware, reported by [hacan359](https://github.com/hacan359) on 2026-09-10, reproducing **with official OPL as much as with this
fork**. Swapping only that one module fixes it. `usbd_mini.irx` is not a separate source tree:
`iop/usb/usbd_mini/Makefile` points `IOP_SRC_DIR` at `iop/usb/usbd/src` and adds `-DMINI_DRIVER`, so
the rewrite lands squarely on the module every RiptOPL edition embeds straight from the SDK.

It is therefore **not** gated: `USBD_MINI_IRX` sits outside the `RETROACHIEVEMENTS` conditional and
every edition gets the pre-rewrite driver. It is listed here only because this is where the module
came from; it is not a RetroAchievements feature and does not depend on the flag.

Hardware-checked by **Zack on 2026-09-10** before that promotion landed: no noticeable USB
regressions on the promoted build. That is the check the promotion was waiting on — it is a
no-regression result, not a confirmation that the Dynasty Warriors 2 case is cured here; that
remains [hacan359](https://github.com/hacan359)'s finding on his own hardware. `backup/pre-usbd-forkwide-58fe5529` is the tree
immediately before the swap, kept as a restore point.

## Before this is called finished

Most of the list below is still open. [oMrRexD](https://github.com/oMrRexD)'s hardware session for #702–#705 covered the nearest
thing to **3**, launched from MMCE rather than USB (25,200 snapshots with none lost after a cold boot
and a straight launch; the same build also tracked the game from USB), and **13**, the unlock overlay
over a running game; a launch with telemetry switched off sent nothing, which is part of **2**.
Everything else still needs a PlayStation 2. Emulator testing gets you as far as: the menu boots, the
RA entries render and navigate, the flag-off build is unchanged, and a game with no `.wl` does not
crash. That is the ceiling — GS raster timing and real SMAP behaviour are not testable there.

Hand testers a **run-pinned nightly.link build**, never a bare artifact link.

| # | Test | Expected |
| --- | --- | --- |
| 1 | Default build (`RETROACHIEVEMENTS=0`) | no regression on any existing path |
| 2 | RA build, telemetry Off | behaves as the default build |
| 3 | USB launch, valid `.wl`, telemetry On | ~60 snapshots/s for ≥5 min, skip/fail counters flat |
| 4 | Same, watching frame rate | no perceptible impact |
| 5 | SMB launch with telemetry | the game's own SMB stream unaffected |
| 6 | Menu check, protocol = SMB | title + counts, `.wl` written |
| 7 | Menu check, protocol = Off | RA raises the stack, same result |
| 8 | Menu check, protocol = UDPBD / UDPFS | clean refusal + hash written to `RA/hashes.txt`, no wedge |
| 9 | Neutrino-core game | clean refusal, no hash attempt |
| 10 | PS1/VCD entry | clean refusal |
| 11 | HDD/APA entry | the read-only check maps the HDL extents; a watch list still loads |
| 12 | Badge + cover mark | appear after refresh, correct game, correct device |
| 13 | Unlock overlay | gold pulse on `RAU1`, game keeps running |
| 14 | IGR combo during an RA launch | still resets |
| 15 | MX4SIO launch with telemetry | no navigation or art regression (that device has history) |
| 16 | Check a physical PS2 CD/DVD, then RA launch with telemetry On | correct hash/watch list, normal authentication, game and telemetry run |
| 17 | Swap the disc after checking support | launch reads the current disc identity and requires its own watch list |
| 18 | Normal Launch PS2 Disc in both builds | existing disc boot remains usable without RA |
| 19 | RA disc game performs an IOP reset; then try IGR | disc access and telemetry recover; IGR returns without waiting on the absent OPL CDVDMAN RPC |

---

## Notes for anyone changing this code

* **Telemetry requires an OPL-core launch.** BDM, ETH/SMB, HDD/APA and MMCE have this path; HTTP also loads an existing watch list from its local settings prefix. Neutrino and the PS1 cores run external ELFs, so OPL's telemetry core does not run there. Treat HTTP/RA interoperability as unvalidated until tested.

* **The badge cache belongs to the I/O thread.** `raBadgeRefresh` frees and reallocates it. Nothing
  on the render path may read it — that is why a row carries `raBadged` as a plain int, resolved
  when the row is built. Re-introducing a `raBadgeHas`-style call from a draw routine re-introduces
  a use-after-free that only shows up as a rare hang.
* **`MSG_DONTWAIT` is `0x08` on the menu path**, from ps2sdk's `tcpip.h`. The `0x40` in SMSTCPIP
  belongs to the in-game stack that serves `raudp`. They are different stacks; do not unify them.
* **The texture table is positional.** `RA_MARK` in `enum INTERNAL_TEXTURE` and its row in
  `internalDefault[]` are guarded by the same `#ifdef` for that reason. Guard one without the other
  and the array silently goes one short.
* **Build from a clean object tree** (`build/editions/<edition>-gcc`) after any change to
  `EECoreConfig_t`, `OPL_MODULE_ID` or the submenu structs. A stale incremental build can pass
  locally while CI's clean build fails.
* **`ee_core`'s stack is whatever ram84 leaves above `.bss`.** ram84 is 77312 bytes
  (`ee_core/linkfile`), and the link now fails when less than 3 KB of stack is left, so the next
  thing that grows `.bss` breaks the build rather than a console. RA's two buffers, `ra_snap_buf` and
  `ra_watch` (about 8 KB), used to live in `.bss`: they left the RA build 1920 bytes of stack, which
  overflowed at the first IOP reset. Since #702 they sit in a work area the launcher reserves at the
  end of module storage (`raWorkArea` in `EECoreConfig_t`), and only for a launch that has a watch
  list with telemetry on. When #702 landed (in the original C loader), the stack left was 13312 bytes
  in the default build, 3348 with in-game screenshots and RTL enabled (today's `extras` configuration),
  and 9984 / 10112 in the RA builds with and without PADEMU. The RA release is built without `extras`,
  like the standard loader, so the RA kit is the shipping loader plus achievements, not a variant.
  Before #702, RA and `extras` together overflowed ram84 by roughly 6.5 KB; that pairing has not been
  re-measured since.
* **PC integration uses [xeRAbora](https://github.com/hacan359/xerabora)**, maintained by [hacan359](https://github.com/hacan359).
* **Physical discs use an EESYNC-only reset image.** ROM CDVDMAN/CDVDFSV remain in place;
  standalone DEV9 and SMSUTILS load before the telemetry network stack. Do not call OPL CDVDMAN's
  shutdown RPC in this mode. Hash checks share the existing I/O worker; a launch refuses while a check
  is pending, before probing or tearing down devices.

---

## Credits

* **[hacan359 (yoba)](https://github.com/hacan359)** — the RetroAchievements design, the console implementation this port follows,
  and the PC client. Physical-disc integration follows the
  [upstream disc implementation](https://github.com/hacan359/Open-PS2-Loader/tree/cb713e686acb2fb63de20aa44b3fd4e8c59ca680).
* **[oMrRexD](https://github.com/oMrRexD)** — the real-hardware RA stabilization work merged through
  PRs #702–#705: ee_core stack recovery, MMCE DEV9, launch-time network readiness/fallback, and the
  RA settings-page correction, plus the hardware investigation that exposed those failures.
* `src/md5.c`, `include/md5.h` — L. Peter Deutsch, zlib licence, vendored unchanged.
