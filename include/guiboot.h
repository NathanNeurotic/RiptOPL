#ifndef __GUIBOOT_H
#define __GUIBOOT_H

#include <stddef.h>

// Boot-splash status line state (see src/guiboot.cpp).
class BootStatus
{
public:
    // Main-thread status line; NULL clears it and releases the sticky latch.
    void set(const char *status);
    // IO-thread step label: only the POINTER is stored, so `label` must outlive the splash.
    void setSticky(const char *label);
    // Dynamic label: formatted into the buffer that is not currently published, then published.
    void setStickyCopy(const char *label);
    // The line to draw: the sticky label wins over the main-thread status. NULL when nothing to show.
    const char *line() const;

private:
    char text_[64] = {0};
    bool active_ = false;
    // Single aligned pointer: atomic load/store on the EE, so no lock is needed across threads.
    const char *volatile sticky_ = NULL;
    char copy_[2][64] = {{0}, {0}};
};

#endif
