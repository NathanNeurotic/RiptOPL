// grid: the Grid view's shape limits, page scrolling, Up/Down and L1/R1 steps (include/grid.h).
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
    CHECK(columns == GRID_MAX_COLUMNS && rows * columns <= GRID_MAX_CELLS && rows == 2);
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
    CHECK(gridRowStep(10, 1, 4, -1) == -1); // top row: Up goes to the previous device menu
    CHECK(gridRowStep(10, 5, 4, -1) == 1);
    CHECK(gridRowStep(10, 1, 4, 1) == 5);
    CHECK(gridRowStep(10, 7, 4, 1) == 9);  // row 2 holds 8 and 9 only
    CHECK(gridRowStep(10, 9, 4, 1) == -1); // last row: Down goes to the next device menu
    CHECK(gridRowStep(10, 8, 4, -1) == 4);
    CHECK(gridRowStep(0, 0, 4, 1) == -1);   // empty page
    CHECK(gridRowStep(10, 12, 4, 1) == -1); // stale index

    TEST_DONE("grid");
}
