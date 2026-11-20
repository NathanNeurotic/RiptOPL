#ifndef __FNTSYS_H
#define __FNTSYS_H

/// default (built-in) font id
constexpr int FNT_DEFAULT = (0);
/// Value returned on errors
constexpr int FNT_ERROR = (-1);

constexpr int FNTSYS_DEFAULT_SIZE = 17;

// Menu font scaling, 50%-150% in 5% increments. Only the standard 17px
// menu face (including translation and CJK fallbacks) follows this choice;
// explicitly sized theme fonts retain their author's point sizes.
// The config persists the actual percentage, NOT the UI's selection index.
constexpr int FONT_SIZE_MIN_PERCENT = 50;
constexpr int FONT_SIZE_MAX_PERCENT = 150;
constexpr int FONT_SIZE_STEP_PERCENT = 5;
constexpr int FONT_SIZE_DEFAULT = 100;
constexpr int FONT_SIZE_COUNT = (FONT_SIZE_MAX_PERCENT - FONT_SIZE_MIN_PERCENT) / FONT_SIZE_STEP_PERCENT + 1;

/** Convert old config values (Small=0, Default=1, Large=2) to matching
 *  percentages before use. 90% -> 15px, 100% -> 17px, 110% -> 19px. */
static inline int fntValidFontSize(int size)
{
    if (size == 0)
        return 90;
    if (size == 1)
        return FONT_SIZE_DEFAULT;
    if (size == 2)
        return 110;
    return (size >= FONT_SIZE_MIN_PERCENT && size <= FONT_SIZE_MAX_PERCENT &&
            size % FONT_SIZE_STEP_PERCENT == 0) ?
               size :
               FONT_SIZE_DEFAULT;
}

/** Map between the stored percentage and the 21-entry picker, safely. */
static inline int fntFontSizeIndex(int percent)
{
    return (fntValidFontSize(percent) - FONT_SIZE_MIN_PERCENT) / FONT_SIZE_STEP_PERCENT;
}
static inline int fntFontSizePercentFromIndex(int index)
{
    return (index >= 0 && index < FONT_SIZE_COUNT) ?
               FONT_SIZE_MIN_PERCENT + index * FONT_SIZE_STEP_PERCENT :
               FONT_SIZE_DEFAULT;
}

/** Scale the standard menu font; other explicitly sized fonts are unchanged. */
int fntScaleFontSize(int baseSize);

/** Initializes the font subsystem */
void fntInit();

/** Terminates the font subsystem */
void fntEnd();

/** Loads a font from a file path
 * @param path The path to the font file
 * @return font slot id (negative value means error happened) */
int fntLoadFile(char *path, int fontSize);

/** Loads an optional Unicode fallback font
 * @param path The path to the font file
 * @return 0 on success, negative value on error */
int fntLoadFallback(char *path);

/** Draws a theme font at a weight, 100 to 900 (0: the face's own): a variable face as designed, a static one grown */
void fntSetWeight(int id, int weight);

/** Reloads the default font */
int fntLoadDefault(char *path);

/** Releases a font slot */
void fntRelease(int id);

/** Updates to the native display resolution and aspect ratio
 * @note Invalidates the whole glyph cache for all fonts! */
void fntUpdateAspectRatio();

/** Renders a text with specified window dimensions */
int fntRenderString(int id, int x, int y, short aligned, size_t width, size_t height, const char *string, u64 colour);

/** Renders a string limited to `room` units: whole if it fits, else scrolling (scroll=1) or ending in "..." (scroll=0).
    Centred on x for ALIGN_HCENTER, like fntRenderString. Returns the end x. */
int fntRenderStringFit(int id, int x, int y, short aligned, int room, const char *string, u64 colour, int scroll);

/** replaces spaces with newlines so that the text fits into the specified width.
 * @note A destrutive operation - modifies the given string!
 */
void fntFitString(int id, char *string, size_t width);

/** Greedy word wrap of text to innerW (unscaled) into out, '\\n' between lines; an explicit '\\n' stays a break.
 * A word wider than the line breaks between characters, and CJK, Thai and Lao text (no spaces) breaks only where
 * those scripts allow. Never ends inside a character when out fills. Returns the number of lines. */
int fntWrapText(int id, const char *text, int innerW, char *out, int outSize);

/** Calculates the width of the given text string
 * We can't use the height for alignment, as the horizontal center would depends of the contained text itself */
int fntCalcDimensions(int id, const char *str);

#endif
