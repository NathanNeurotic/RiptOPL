// Popups drawn over the top-right of the screen. Split out of gui.cpp.

#include "include/opl.h"
#include "include/gui.h"
#include "include/guiint.h"
#include "include/guinotify.h"
#include "include/renderman.h"
#include "include/fntsys.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/menusys.h"
#include "include/config.h"
#include "include/sound.h"

#include <string.h>
#include <stdio.h>
#include <stdint.h>

Notifications gNotifications;

void Notifications::check(bool checkTheme, bool checkLang)
{
    if (gEnableNotifications) {
        if (checkTheme) {
            // Only DISK themes have a path to announce; the built-ins (<OPL>, <Coverflow>) return NULL
            // from thmGetFilePath. The old `!= 0` test was correct only while theme IDs ran 0..nThemes;
            // this tree's themes.c adds a built-in at nThemes+1, so a saved <Coverflow> passed the test
            // and fed NULL to the "%s" in _STR_THM_NOTIFICATION on EVERY boot. Coverflow is our default.
            if (thmGetFilePath(thmGetGuiValue()) != NULL)
                showTheme_ = true;
        }

        if (checkLang) {
            if (lngGetGuiValue() != 0)
                showLang_ = true;
        }
    }
}

void Notifications::reset()
{
    showTheme_ = false;
    showLang_ = false;
    popupArmed_ = false;
}

void Notifications::render(const char *string, int y)
{
    int x;

    x = gGuiScreenWidth - rmUnScaleX(fntCalcDimensions(gTheme->fonts[0], string)) - 10;

    rmDrawRect(x - 10, y, gGuiScreenWidth - x, MENU_ITEM_HEIGHT + 10, gColDarker);
    fntRenderString(gTheme->fonts[0], x - 5, y + 5, ALIGN_NONE, 0, 0, string, gTheme->textColor);
}

void Notifications::setToast(const char *text)
{
    snprintf(toast_, sizeof(toast_), "%s", text ? text : "");
    toastStart_ = clock();
}

void Notifications::drawToast()
{
    if (toast_[0] == '\0')
        return;
    // The EE clock wraps at 32 bits. Cast before subtracting to avoid signed overflow.
    if ((uint32_t)clock() - (uint32_t)toastStart_ >= (uint32_t)3000 * (CLOCKS_PER_SEC / 1000)) {
        toast_[0] = '\0';
        return;
    }
    render(toast_, 10);
}

void Notifications::show()
{
    char notification[128];
    char *col_pos;
    int y = 10;
    int yadd = 35;

    if (showPart_ || showTheme_ || showLang_ || showCfgPopup || showNetDhcpPopup) {
        if (!popupArmed_) {
            popupStart_ = clock();
            popupArmed_ = true;
            sfxPlay(SFX_MESSAGE);
        }

        if (showPart_) {
            col_pos = strchr(gOPLPart, ':');
            col_pos++;

            snprintf(notification, sizeof(notification), _l(_STR_PARTITION_NOTIFICATION), col_pos);
            render(notification, y);
            y += yadd;
        }

        if (showCfgPopup) {
            snprintf(notification, sizeof(notification), _l(_STR_CFG_NOTIFICATION), configGetLoadDir());
            if ((col_pos = strchr(notification, ':')) != NULL)
                *(col_pos + 1) = '\0';

            render(notification, y);
            y += yadd;
        }

        if (showTheme_) {
            snprintf(notification, sizeof(notification), _l(_STR_THM_NOTIFICATION), thmGetFilePath(thmGetGuiValue()));
            if ((col_pos = strchr(notification, ':')) != NULL)
                *(col_pos + 1) = '\0';

            render(notification, y);
            y += yadd;
        }

        if (showLang_) {
            snprintf(notification, sizeof(notification), _l(_STR_LNG_NOTIFICATION), lngGetFilePath(lngGetGuiValue()));
            if ((col_pos = strchr(notification, ':')) != NULL)
                *(col_pos + 1) = '\0';

            render(notification, y);
            y += yadd;
        }

        // One-time network notice set at config load: UDP transports always use the saved static PS2
        // IP fields. If DHCP is the preserved SMB/HTTP preference, remind the user which address UDP uses.
        if (showNetDhcpPopup) {
            render(_l(_STR_UDPBD_NEEDS_STATIC_IP), y);
            y += yadd;
        }

        if ((clock() - popupStart_) >= (clock_t)5000 * (CLOCKS_PER_SEC / 1000)) {
            reset();
            showPart_ = false;
            showCfgPopup = 0;
            showNetDhcpPopup = 0;
        }
    }
}

#ifdef RETROACHIEVEMENTS
/* RA notices: two-line feedback from the check/test actions, which run on the
   I/O thread and so cannot open a dialog. They set the text; the render loop
   draws it. Deliberately NOT gated on gEnableNotifications -- they are the
   answer to an action the user just took, not a courtesy popup. */
void Notifications::setRA(const char *line1, const char *line2)
{
    raNoticeLines_ = 0; /* renderer stops drawing before the new text lands */
    snprintf(raNotice_[0], sizeof(raNotice_[0]), "%s", line1 ? line1 : "");
    snprintf(raNotice_[1], sizeof(raNotice_[1]), "%s", line2 ? line2 : "");
    raNoticeTimer_ = 0;
    raNoticeLines_ = (line2 != NULL && line2[0] != '\0') ? 2 : 1;
}

int Notifications::renderRA(int y, int yadd)
{
    int i;

    if (raNoticeLines_ <= 0)
        return y;

    if (!raNoticeTimer_) {
        raNoticeTimer_ = clock() + 8000 * (CLOCKS_PER_SEC / 1000);
        sfxPlay(SFX_MESSAGE);
    }

    for (i = 0; i < raNoticeLines_; i++) {
        render(raNotice_[i], y);
        y += yadd;
    }

    if (clock() >= raNoticeTimer_) {
        raNoticeLines_ = 0;
        raNoticeTimer_ = 0;
    }

    return y;
}

void Notifications::drawRA()
{
    renderRA(10, 35);
}
#endif


// The C-style entry points other files use.
void guiCheckNotifications(int checkTheme, int checkLang)
{
    gNotifications.check(checkTheme != 0, checkLang != 0);
}

void guiResetNotifications(void)
{
    gNotifications.reset();
}

#ifdef RETROACHIEVEMENTS
void guiShowRANotice(const char *line1, const char *line2)
{
    gNotifications.setRA(line1, line2);
}
#endif
