# Fresh-start defaults

These defaults describe this C++/ps2build repository when no saved master
configuration is loaded. Updating the ELF does not reset saved settings. A new
installation folder can still discover settings on another device.

| Setting | Fresh default | Where to change it |
| --- | --- | --- |
| Menu aspect ratio | 4:3; Widescreen off | Settings → Interface |
| Menu video mode | Auto | Settings → Interface |
| Theme | Built-in `<OPL>` | Settings → Interface → Theme |
| Theme View | Default (List for `<OPL>`); Coverflow, PSBBN Flow and Grid are optional views | Settings → Interface → Theme View, or hold L3 |
| Cover and disc artwork | On | Settings → Interface → Artwork Settings |
| Per-game background art | Off in Korium and RetroAchievements (as the original's Korium); On in IncebtIon | Settings → Interface → Artwork Settings |
| Notifications | On | Settings → Interface |
| PS2 logo | On | Settings → Game Launching |
| Sound effects, boot sound, background music | Off; stored volume 90 | Settings → Audio Settings |
| Controller Vibration in Menus | Off | Settings → Controller Settings |
| Remember last played | Off | Settings → General & System |
| Automatic VMC creation | Off | Settings → Game Launching |
| All Games List | Off | Settings → Game Sources |
| Show Hidden Games | Off | Settings → Game Sources |
| Sort Games | A to Z (an out-of-range saved value also reads as A to Z) | Settings → Interface |
| Button Glyphs | Theme (the theme's own glyphs, or its `glyph_set` key) | Settings → Interface |
| GameID barcode | PS1 half On, PS2 half Off (shown as *On (POPSTARTER only)*) | Settings → Interface |

Background music still needs a playable configured/default track. An enabled
setting alone does not supply music. Storage discovery and saved configuration
can affect the active presentation after initial defaults are applied.

To use the widescreen/carousel presentation, select **Widescreen: On** and
**Theme View: Coverflow** under **Settings → Interface** (theme `<OPL>`), apply, and save.
Coverflow supports both 4:3 and 16:9; selecting it does not toggle widescreen.
The 720p and 1080i video modes are always drawn as 16:9, whatever the Widescreen setting.
The carousel starts with three covers, Large center scale (45), 200 ms animation and
side-cover dimming off. L1/R1 page moves animate only when selection changes,
including wraps; flat list themes keep their existing page behavior.

For the PSBBN Definitive Project Game Collection look, choose **Theme View: PSBBN Flow** on the same
page, apply and save. It uses the colour picker's palette and its own fixed perspective layout and
25-frame glide; the Coverflow count, scale, animation and dimming settings do not apply to it. See
[PSBBN Flow](THEME_ENGINE.md#psbbn-flow) for artwork and authoring details.

Existing `settings_riptopl.cfg` values override fresh defaults. The legacy
`conf_riptopl.cfg` can also be migrated. Inspect the active configuration location
before diagnosing a different appearance as a failed update; preserve settings
and saves instead of deleting them to force a reset.

Menu video settings do not force the same video mode in games. **Triangle + Cross**
at boot selects the explicit 480p menu-recovery override; **Triangle + Circle**
selects Auto/interlaced recovery. Use the override suitable for the connected display.

Implementation references: [settings table](../src/core/settings.cpp),
[configuration loading](../src/app/settingsio.cpp),
[initial theme](../src/gfx/themes.cpp), [theme catalog](../include/themecatalog.h),
and [carousel tuning](../src/gfx/coverflow.cpp).

These match the original Open-PS2-Loader's fresh-start defaults (4:3, audio and
menu rumble off, center scale 45). Saved settings are untouched, so an existing
installation keeps its own choices.
