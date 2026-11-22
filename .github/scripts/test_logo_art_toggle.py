#!/usr/bin/env python3
"""Source-wiring regression checks for the per-game _LGO artwork toggle."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def source(path):
    return (ROOT / path).read_text(encoding="utf-8")


class LogoArtToggle(unittest.TestCase):
    def test_default_enabled_and_persistence(self):
        opl = source("src/app/opl.cpp")
        config = source("include/config.h")
        settings = source("src/core/settings.cpp")
        self.assertIn('CONFIG_OPL_ENABLE_LOGOART             "enable_logoart"', config)
        self.assertIn("int gEnableLogoArt;", opl)
        self.assertIn("intSetting(CONFIG_OPL_ENABLE_LOGOART, &gEnableLogoArt, 1),", settings)

    def test_artwork_ui_reads_and_writes_toggle(self):
        dialogs = source("src/ui/dialogs.cpp")
        gui = source("src/ui/guisettings.cpp")
        self.assertIn("UICFG_ENABLE_LOGOART, 1, 1, _STR_HINT_LOGOART", dialogs)
        self.assertIn("_STR_LOGOART", dialogs)
        theme = gui.split("static int guiSettingsShowTheme(void)", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("diaSetInt(ui, UICFG_ENABLE_LOGOART, gEnableLogoArt)", theme)
        self.assertIn("diaGetInt(ui, UICFG_ENABLE_LOGOART, &gEnableLogoArt)", theme)

    def test_artwork_controls_are_on_the_theme_page(self):
        dialogs = source("src/ui/dialogs.cpp")
        theme = dialogs.split("struct UIItem diaThemeConfig[]", 1)[1].split("{UI_TERMINATOR}", 1)[0]
        interface = dialogs.split("struct UIItem diaUIConfig[]", 1)[1].split("{UI_TERMINATOR}", 1)[0]
        for row in ("UICFG_COVERART", "UICFG_ENABLE_DISCART", "UICFG_ENABLE_LOGOART", "UICFG_ENABLE_BGART",
                    "UICFG_ENABLE_SAVE_ICONS", "UICFG_ENABLE_ART_TAR", "UICFG_ART_DELAY"):
            self.assertIn(row, theme)
            self.assertNotIn(row, interface)
        self.assertNotIn("diaArtworkConfig", dialogs)

    def test_logo_and_disc_art_guarded_at_cache_and_draw_sites(self):
        themeelems = source("src/gfx/themeelems.cpp")
        coverflow = source("src/gfx/coverflow.cpp")
        self.assertIn('!gEnableDiscArt && strcmp(cache->suffix(), "ICO") == 0', themeelems)
        self.assertIn('!gEnableLogoArt && strcmp(cache->suffix(), "LGO") == 0', themeelems)
        self.assertIn("if (isHiddenArtworkCache(cache))", themeelems)
        self.assertIn("if (isHiddenArtworkCache(gameImage->cache))", themeelems)
        self.assertIn("if (!cimg || isHiddenArtworkCache(cimg->cache))", coverflow)
        self.assertGreaterEqual(themeelems.count("!isHiddenArtworkCache(itemsList->decoratorImage->cache)"), 3)
        self.assertNotIn("isDiscArtCache(", themeelems)

    def test_language_ids_are_appended(self):
        labels = source("lng/tmpl/_base.yml")
        self.assertLess(labels.index("- label: AUTO_VMC_STILL_RUNNING"), labels.index("- label: LOGOART"))
        self.assertLess(labels.index("- label: LOGOART"), labels.index("- label: HINT_LOGOART"))


if __name__ == "__main__":
    unittest.main()
