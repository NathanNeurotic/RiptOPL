# UDPFS Assault credits and provenance

The adjacent `usbd.irx.udpfs` / `usbhdfsd.irx.udpfs` pair and IP config examples are from
**Okeanos (Okeanos86)**, [UDPFS Assault 1.0.1](https://github.com/Okeanos86/BDMAssault/releases/tag/1.0.1)
(release of 2026-11-08: the 1.0.0 network read/write driver plus BIN/CUE games and the optional
`PATHMAP.INI` game-folder options): every `mass:` request after POPSTARTER's IOP reset, VMC writes
included, goes to the UDPFS server. They are included unchanged. The original project is
**israpps' BDM Assault**; POPSTARTER is by **krHACKen**. Further upstream credits and license
notices are preserved in this folder.

The driver reads `IPCONFIG.DAT` and `UDPFSCONFIG.DAT` from `mc0:/SYS-CONF/`, then `mc1:/SYS-CONF/`.
At launch RiptOPL copies either file from the server's `POPS/` folder only when no card has it;
existing card files are never overwritten.

`PATHMAP.INI` here is upstream's example (every option off). RiptOPL never copies it; the POPS
UDPFS Network Settings page sets the three options in `mc?:/POPSTARTER/PATHMAP.INI` for you.

See [the POPS UDPFS setup guide](https://github.com/NathanNeurotic/RiptOPL/blob/rebuild/main/docs/VCD.md#pops-udpfs-with-bdm-udpfs-assault).
The upstream README and config guide are preserved verbatim for reference; their relative
links refer to the original archive layout. The full AFL license texts are in this folder.

`PROVENANCE.json` pins the official release archive SHA-256 and each unchanged
driver/config/license member. The two driver files remain loose POPS assets;
they are not an additional SDK or embedded menu driver.
