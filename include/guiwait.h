#ifndef __GUIWAIT_H
#define __GUIWAIT_H

struct UIItem;

// Hands a request to the I/O thread and keeps the GUI drawing until it clears `*flag`. The flag is a file-static int owned by the
// caller and `data` a file-static handler, so a late write by a still-blocked I/O thread after a timeout is harmless.
class DeferredIoWait
{
public:
    // Wait behind a text screen showing `message`; timeoutMs > 0 bounds the wait (sets gLastDeferredTimedOut on expiry).
    // The text is copied here: it is often an _l() string, and the I/O thread can switch the language (freeing that
    // string) while the wait is drawn.
    DeferredIoWait(int *flag, const char *message, int type, void *data, int timeoutMs)
        : flag_(flag)
        , hasMessage_(message != nullptr)
        , ui_(nullptr)
        , type_(type)
        , data_(data)
        , timeoutMs_(timeoutMs)
    {
        copyMessage(message);
    }
    // Wait behind a dialog (or the current screen when `ui` is NULL); unbounded.
    DeferredIoWait(int *flag, struct UIItem *ui, int type, void *data)
        : flag_(flag)
        , hasMessage_(false)
        , ui_(ui)
        , type_(type)
        , data_(data)
        , timeoutMs_(0)
    {
        message_[0] = '\0';
    }
    void run();

private:
    void copyMessage(const char *message);
    void runWithMessage();
    void runWithDialog();

    int *flag_;
    bool hasMessage_;
    char message_[512]; // as much as guiDrawMsgText wraps
    struct UIItem *ui_;
    int type_;
    void *data_;
    int timeoutMs_;
};

#endif
