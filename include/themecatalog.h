#ifndef __THEMECATALOG_H
#define __THEMECATALOG_H

// The list of themes found on devices plus the built-in "<OPL>" (id 0; its Coverflow and PSBBN Flow looks
// are views, see themeviews.h), the currently selected id and the published name list the settings
// dialog shows. One instance lives in src/themes.cpp; the thm* list/selection functions in themes.h
// forward to it.
//
// Locking: the name list is published to the GUI thread. Rebuilds and removals must hold guiLock (see
// rebuildNames in themes.cpp); the class does not take guiLock by itself. addElements, reinit and setGuiValue take
// the theme switch lock (thmSwitchSema in themes.cpp), so a scan, a removal and a theme load never overlap.

#include "themes.h"

class ThemeCatalog
{
public:
    constexpr ThemeCatalog() = default;
    ThemeCatalog(const ThemeCatalog &) = delete;
    ThemeCatalog &operator=(const ThemeCatalog &) = delete;

    // Scan `path` for thm_* folders and append them; re-applies the saved theme.
    int addElements(char *path, const char *separator, int forceRefresh);
    // Drop every theme living under `path` (a removed device) and keep the active selection coherent.
    void reinit(const char *path);
    void end();

    const char *currentName() const;
    int guiValue() const;
    int setGuiValue(int themeID, int reload);
    // setGuiValue for a caller that already holds the theme switch lock (it is not recursive).
    int setGuiValueLocked(int themeID, int reload);
    int findGuiID(const char *theme) const;
    const char **guiList() const;
    // On-disk folder of a disk theme, NULL for <OPL> and out-of-range ids.
    char *filePath(int themeID);
    // Number of disk themes.
    int count() const { return count_; }

private:
    void rebuildNames();
    int readEntry(int index, const char *path, const char *separator, const char *name, unsigned char d_type);
    static int readEntryCallback(int index, const char *path, const char *separator, const char *name, unsigned char d_type);

    int themeId_ = 0;
    int count_ = 0;
    theme_file_t themes_[THM_MAX_FILES] = {};
    const char **names_ = NULL;
};

#endif
