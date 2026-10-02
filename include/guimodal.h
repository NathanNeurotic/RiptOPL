#ifndef __GUIMODAL_H
#define __GUIMODAL_H

struct UIItem;
typedef struct ConfigSet config_set_t;

// A full-screen modal: one frame per pass of start frame / pad poll / key handling / draw / end frame until a key finishes it.
// The message box, the Reboot IOP prompt, the remove-settings prompt and the cheat selector are all this loop with different
// keys and a different draw.
class ModalScreen
{
public:
    // Runs the modal; the return value is whatever finish() decides.
    int run();

protected:
    ~ModalScreen() = default;
    // Called once before the first frame.
    virtual void begin();
    // Handle this frame's pad state; true ends the loop.
    virtual bool onKeys() = 0;
    virtual void draw() = 0;
    // Called once after the loop: play the closing sound, act on the choice, return the result.
    virtual int finish() = 0;
};

// Word-wrapped text with a Back button (and optionally Accept). Returns 1 for Accept, 0 for Back.
class MessageBoxScreen: public ModalScreen
{
public:
    MessageBoxScreen(const char *text, int addAccept, struct UIItem *ui);

private:
    bool onKeys() override;
    void draw() override;
    int finish() override;

    // Greedily word-wrap `text` to innerW, writing '\n'-separated lines into out. Returns the line count.
    static int wrapText(const char *text, int innerW, char *out, int outSize);

    int addAccept_;
    struct UIItem *ui_;
    int terminate_ = 0;
    int lines_ = 0;
    // Wrapped once up front, not per frame: the result depends only on the text and the screen width.
    char wrapped_[512];
};

// "REBOOT IOP?": 1 = yes, 0 = no, -1 = cancel.
class RebootIopPrompt: public ModalScreen
{
private:
    bool onKeys() override;
    void draw() override;
    int finish() override;

    int terminate_ = 0;
};

// Per-game / global / all settings removal prompt; returns 1 when something was removed.
class RemoveSettingsPrompt: public ModalScreen
{
public:
    RemoveSettingsPrompt(config_set_t *configSet, config_set_t *configGame)
        : configSet_(configSet)
        , configGame_(configGame)
    {
    }

private:
    bool onKeys() override;
    void draw() override;
    int finish() override;

    config_set_t *configSet_;
    config_set_t *configGame_;
    int terminate_ = 0;
};

// The cheat selector: toggle codes, Disable All, Start to run.
class CheatScreen: public ModalScreen
{
public:
    CheatScreen();

private:
    bool onKeys() override;
    void draw() override;
    int finish() override;

    int offset_ = 0;
    int cheatCount_ = 0;
    int selected_ = 0;
    static constexpr int VISIBLE = 10; // Maximum number of cheats visible on screen
};

#endif
