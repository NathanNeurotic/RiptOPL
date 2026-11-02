#ifndef __GUINOTIFY_H
#define __GUINOTIFY_H

#include <time.h>

// The top-right popups: partition / config / theme / language / network notices (one shared 5 s timer) and the
// RetroAchievements two-line notice (8 s, set from the I/O thread). See src/guinotify.cpp.
class Notifications
{
public:
    // Arm the theme / language popups when the setting names a file worth announcing (no-op with notifications off).
    void check(bool theme, bool lang);
    // Forget the theme/language popups and the timer (the config/partition/DHCP flags clear themselves on expiry).
    void reset();
    void showPartition() { showPart_ = true; }
    // One frame: draw whatever is pending and expire it after 5 s.
    void show();
    // GUI-thread action feedback, drawn even when courtesy notifications are disabled.
    void setToast(const char *text);
    void drawToast();
#ifdef RETROACHIEVEMENTS
    void setRA(const char *line1, const char *line2);
    void drawRA();
#endif

private:
    // Draw one notice whose box starts at y; returns the y for the next one (yadd lower, plus any wrapped lines).
    static int render(const char *string, int y, int yadd);

    bool showPart_ = false;
    bool showTheme_ = false;
    bool showLang_ = false;
    // START tick + how long to hold, NOT an absolute deadline. clock() is a 32-bit microsecond counter that wraps every
    // ~71.6 minutes; `clock() >= start + duration` silently becomes false-for-71-minutes whenever the sum crosses the
    // wrap. The (clock() - start) ELAPSED form is correct across one wrap by ordinary unsigned arithmetic.
    clock_t popupStart_ = 0;
    bool popupArmed_ = false;
    // Room for the longest translated notice: a toast carries a game name (a HIDDEN_NAME_MAX buffer) in its message,
    // and 96 bytes cut the English CF notice mid-sentence. test_menu_text_fit.py checks every language against these.
    char toast_[384] = {0};
    clock_t toastStart_ = 0;
#ifdef RETROACHIEVEMENTS
    char raNotice_[2][256] = {{0}, {0}};
    int raNoticeLines_ = 0;
    clock_t raNoticeTimer_ = 0;
    int renderRA(int y, int yadd);
#endif
};

extern Notifications gNotifications;

#endif
