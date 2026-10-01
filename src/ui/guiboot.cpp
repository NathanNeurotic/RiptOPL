// Boot-splash status line. Split out of gui.cpp.
//
// Boot-step localizer: a deferred IO-thread boot step can wedge with no timeout on real hardware and
// freeze the splash, while the MAIN thread races ahead setting "Ready." -- the frozen screen would then
// show a useless "Ready." instead of the stuck step. An IO-thread step publishes its label via
// setSticky(); line() PREFERS it over the main-thread text, so whichever ordering wins the STUCK STEP is
// what stays on screen. Cross-thread state is a single aligned POINTER to a static _l() string -- no
// shared buffer, so no data race. Cleared by set(NULL).

#include "include/guiboot.h"

#include <stdio.h>

void BootStatus::set(const char *status)
{
    if (status == NULL) {
        text_[0] = '\0';
        active_ = false;
        sticky_ = NULL; // release the localizer latch so a fresh boot can claim the line again
        return;
    }
    snprintf(text_, sizeof(text_), "%s", status);
    active_ = true;
}

void BootStatus::setSticky(const char *label)
{
    if (label == NULL)
        return;
    sticky_ = label;
}

// Dynamic diagnostic labels cannot use setSticky(), which deliberately stores the caller's pointer.
// Keep two owned buffers and always format into the one that is not published. The EE is single-core:
// if the renderer is pre-empted after loading one pointer, the IO thread only writes the other buffer,
// then atomically publishes that pointer.
void BootStatus::setStickyCopy(const char *label)
{
    if (label == NULL)
        return;

    int target = (sticky_ == copy_[0]) ? 1 : 0;
    snprintf(copy_[target], sizeof(copy_[target]), "%s", label);
    sticky_ = copy_[target];
}

const char *BootStatus::line() const
{
    const char *bootLine = sticky_;
    if (bootLine == NULL && active_ && text_[0] != '\0')
        bootLine = text_;
    return bootLine;
}
