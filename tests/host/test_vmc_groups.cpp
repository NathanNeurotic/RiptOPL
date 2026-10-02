// vmc_groups: the embedded title-id -> memory-card-group dictionary. Includes the real source so the
// private table is visible.
#include <map>
#include <set>
#include <string>
#include <string.h>
#include "../../src/core/vmc_groups.cpp"
#include "check.h"

int main()
{
    CHECK(NUM_MEMORY_CARD_GROUPS == sizeof(allMemoryCardGroups) / sizeof(allMemoryCardGroups[0]));
    CHECK(NUM_MEMORY_CARD_GROUPS > 0);

    // Table sanity plus the lookup contract: the FIRST group (table order) containing a title wins.
    // KNOWN DATA ISSUE (reported, not asserted): some titles are listed in more than one group, which
    // makes the later group unreachable for them, and a few ids are not the usual 11-char shape.
    std::set<std::string> seenGroups;
    std::map<std::string, std::string> firstGroup;
    size_t duplicates = 0, oddShape = 0;
    for (size_t i = 0; i < NUM_MEMORY_CARD_GROUPS; i++) {
        const MemoryCardGroup &g = allMemoryCardGroups[i];
        CHECK(g.groupId && g.groupId[0]);
        CHECK(seenGroups.insert(g.groupId).second); // group ids are unique
        CHECK(g.titleCount > 0 && g.titleIds);
        for (size_t j = 0; j < g.titleCount; j++) {
            const char *t = g.titleIds[j];
            CHECK(t && t[0] && strlen(t) < 32);
            if (strlen(t) != 11 || t[4] != '_' || t[8] != '.') oddShape++;
            if (!firstGroup.emplace(t, g.groupId).second) duplicates++;
        }
    }
    for (auto &kv : firstGroup)
        CHECK_STR(getGroupIdForTitleId(kv.first.c_str()), kv.second.c_str());
    printf("  info: %zu groups, %zu distinct titles, %zu duplicate entries, %zu odd-shaped ids\n",
           (size_t)NUM_MEMORY_CARD_GROUPS, firstGroup.size(), duplicates, oddShape);

    // Unknown ids come back as the very same pointer (callers compare by identity); NULL stays NULL.
    const char unknown[] = "XXXX_000.00";
    CHECK(getGroupIdForTitleId(unknown) == unknown);
    CHECK(getGroupIdForTitleId("") != NULL);
    CHECK(getGroupIdForTitleId(NULL) == NULL);
    CHECK_STR(getGroupIdForTitleId("SCES_532.86"), "XEBP_100.01"); // spot check from the first group
    // lookup is case-sensitive exact match (strcmp); a lower-case id is not a hit
    CHECK(getGroupIdForTitleId("sces_532.86") != NULL && strcmp(getGroupIdForTitleId("sces_532.86"), "XEBP_100.01") != 0);
    TEST_DONE("vmc_groups");
}
