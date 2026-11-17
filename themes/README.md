# Example disk themes

Each `thm_<Name>` folder here is a theme in the format described in [docs/THEME_ENGINE.md](../docs/THEME_ENGINE.md).
They are not built into the loader, so they add nothing to its size or memory use. CI packs them as the
`riptopl-theme-ps2classic` artifact, a `THM/` folder ready to copy to the device.

## PS2 Classic (`thm_PS2Classic`)

A light disc-browser look: deep blue backdrop with faint towers, the device name centred at the top, the game
list on the left and the cover in its case on the right. The only picture is `ps2bg.png`, 160x120 with a 64-colour
palette (1.4 KB, about 19 KiB in texture memory); everything else is built-in art and text. The art is original
(`tools/make_ps2classic_bg.py` draws it); no Sony artwork is used.

Install: copy the `thm_PS2Classic` folder into the `THM` folder of the device RiptOPL starts from (for example
`mass0:/THM/thm_PS2Classic`), start RiptOPL, then pick **PS2Classic** under **Settings → Interface → Theme** and apply.

Display modes: the layout is authored for 640x480 and uses `hd_autofit=1`, so 16:9, 720p and 1080i keep the 4:3
proportions. The background is a stretched gradient, so it needs no per-mode variants.

Not verified: any run on PCSX2 or a real PS2. Host check only (`.github/scripts/test_disk_themes.py`).
