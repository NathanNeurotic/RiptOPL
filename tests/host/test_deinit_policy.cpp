// HOST-TEST: sdk-headers
// Deinit device cleanup stays keyed to selection mode; transition intent is explicit.
#include "check.h"
#include <tamtypes.h>
#include "include/iosupport.h"

int main()
{
    opl_deinit_policy_t disc = oplDeinitPolicy(IO_MODE_SELECTED_ALL, 0);
    CHECK(disc.terminalCleanup == 1);
    CHECK(disc.launchTransition == 1);

    opl_deinit_policy_t app = oplDeinitPolicy(IO_MODE_SELECTED_ALL_SPARE, 0);
    CHECK(app.terminalCleanup == 0);
    CHECK(app.launchTransition == 1);

    opl_deinit_policy_t game = oplDeinitPolicy(BDM_MODE, 0);
    CHECK(game.terminalCleanup == 0);
    CHECK(game.launchTransition == 1);

    opl_deinit_policy_t exit = oplDeinitPolicy(IO_MODE_SELECTED_ALL, 1);
    CHECK(exit.terminalCleanup == 1);
    CHECK(exit.launchTransition == 0);

    opl_deinit_policy_t poweroff = oplDeinitPolicy(IO_MODE_SELECTED_NONE, 1);
    CHECK(poweroff.terminalCleanup == 1);
    CHECK(poweroff.launchTransition == 0);

    TEST_DONE("deinit policy");
}
