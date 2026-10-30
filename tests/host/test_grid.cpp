// grid: the Grid view's shape limits, page scrolling, Up/Down and L1/R1 steps, layouts, slide (include/grid.h).
#include "check.h"
#include "../../include/grid.h"

int main()
{
    // ---- shape: defaults for nonsense, caps, and never more than GRID_MAX_CELLS covers -------------
    int rows = 0, columns = 0;
    gridClamp(&rows, &columns);
    CHECK(rows == GRID_DEFAULT_ROWS && columns == GRID_DEFAULT_COLUMNS);
    rows = 9, columns = 9;
    gridClamp(&rows, &columns);
    CHECK(columns == GRID_MAX_COLUMNS && rows * columns <= GRID_MAX_CELLS && rows == 3);
    rows = 4, columns = 4;
    gridClamp(&rows, &columns);
    CHECK(rows == 4 && columns == 4);

    // ---- scrolling: whole pages; the page holding the selection, a short last page from the top ---
    CHECK(gridTopRow(30, 0, 3, 4) == 0);  // first page: rows 0..2
    CHECK(gridTopRow(30, 11, 3, 4) == 0); // last cover of the first page
    CHECK(gridTopRow(30, 12, 3, 4) == 3); // first cover of the second page
    CHECK(gridTopRow(30, 29, 3, 4) == 6); // last page holds rows 6..7 only
    CHECK(gridTopRow(30, 9, 2, 4) == 2);  // 4 x 2: covers 8..15 are page two
    CHECK(gridTopRow(5, 4, 3, 4) == 0);   // a list shorter than a page never scrolls
    CHECK(gridTopRow(1, 0, 3, 4) == 0);
    CHECK(gridTopRow(0, 0, 3, 4) == 0);   // empty list
    CHECK(gridTopRow(10, 12, 2, 4) == 2); // stale index: the last cover's page (rows 2..3)
    // Moving inside a page never scrolls it.
    for (int i = 8; i < 16; i++)
        CHECK(gridTopRow(40, i, 2, 4) == 2);

    // ---- L1/R1: a page back or on, keeping the place on the page; -1 at either end (the caller wraps)
    CHECK(gridPageStep(30, 5, 2, 4, 1) == 13);
    CHECK(gridPageStep(30, 13, 2, 4, -1) == 5);
    CHECK(gridPageStep(30, 5, 2, 4, -1) == -1); // first page: L1 wraps to the end
    CHECK(gridPageStep(30, 22, 2, 4, 1) == 29); // the last page is short: its last cover
    CHECK(gridPageStep(30, 25, 2, 4, 1) == -1); // already on the last page: R1 wraps to the start
    CHECK(gridPageStep(8, 3, 2, 4, 1) == -1);   // one page only
    CHECK(gridPageStep(0, 0, 2, 4, 1) == -1);
    CHECK(gridPageStep(30, 40, 2, 4, -1) == -1); // stale index
    CHECK(GRID_SELECTED_SCALE > 100);

    // ---- Up/Down: keep the column, a short last row takes its last cover, -1 leaves the grid ------
    CHECK(gridRowStep(10, 1, 4, -1) == -1); // top row: the cursor stays (or, with no sidebar, the previous device)
    CHECK(gridRowStep(10, 5, 4, -1) == 1);
    CHECK(gridRowStep(10, 1, 4, 1) == 5);
    CHECK(gridRowStep(10, 7, 4, 1) == 9);  // row 2 holds 8 and 9 only
    CHECK(gridRowStep(10, 9, 4, 1) == -1); // last row: Down goes to the next device menu
    CHECK(gridRowStep(10, 8, 4, -1) == 4);
    CHECK(gridRowStep(0, 0, 4, 1) == -1);   // empty page
    CHECK(gridRowStep(10, 12, 4, 1) == -1); // stale index

    // ---- Grid Layout: RIPPS2's presets, a wider twin on 16:9, every one within the limits ------------
    grid_layout_t l;
    CHECK(!gridLayoutPreset(0, 0, &l)); // the theme's own
    CHECK(!gridLayoutPreset(GRID_LAYOUTS + 1, 1, &l));
    CHECK(gridLayoutPreset(1, 0, &l) && l.columns == 4 && l.rows == 2 && l.width == 107 && l.height == 150);
    CHECK(gridLayoutPreset(1, 1, &l) && l.columns == 6 && l.rows == 2);
    for (int p = 1; p <= GRID_LAYOUTS; p++)
        for (int wide = 0; wide < 2; wide++) {
            CHECK(gridLayoutPreset(p, wide, &l));
            int r = l.rows, c = l.columns;
            gridClamp(&r, &c);
            CHECK(r == l.rows && c == l.columns); // a preset is never cut down
            // fits 640 wide beside the 30-pixel device bar: covers draw at 3/4 width on 16:9
            int drawn = wide ? l.width * 3 / 4 : l.width;
            CHECK(c * drawn + (c - 1) * l.spacing <= 640 - 2 * 32);
        }

    // ---- the device sidebar opens from the first column --------------------------------------------
    CHECK(gridAtRowStart(10, 0, 4) && gridAtRowStart(10, 8, 4));
    CHECK(!gridAtRowStart(10, 1, 4) && !gridAtRowStart(10, 7, 4));
    CHECK(gridAtRowStart(0, 0, 4)); // an empty page

    // ---- the page slide eases out and ends in place -----------------------------------------------
    CHECK(gridSlideEase(-1.0f) == 0.0f && gridSlideEase(0.0f) == 0.0f && gridSlideEase(1.0f) == 1.0f && gridSlideEase(2.0f) == 1.0f);
    CHECK(gridSlideEase(0.5f) > 0.5f); // fast first, slow into place
    for (int i = 1; i <= 10; i++)
        CHECK(gridSlideEase(i / 10.0f) >= gridSlideEase((i - 1) / 10.0f));

    TEST_DONE("grid");
}
