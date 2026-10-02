#ifndef __APPSOURCE_H
#define __APPSOURCE_H

// The APPS game source (src/sources/appsupport.cpp). A GameSource that also owns the module
// state the file-scope statics used to hold: the discovered app list (legacy conf_apps.cfg
// entries plus device-scanned title.cfg entries), the legacy config set, the startup-hash art
// lookup table, the per-view visibility filter and the scratch buffers used while resolving an
// app's startup path.
//
// One instance, built lazily by placement new in appInstance() -- the GameSource base has a
// non-trivial constructor, so there is deliberately no namespace-scope object (EE rule: no
// dynamic initialization before main; same exception as every phase-5 source, see
// docs/CPP-CONVERSION-PLAN.md 17h). The appsupport.h entry points (appGetObject/appGetArtMode)
// stay free forwarders to the instance.
//
// State members and helpers keep their historical appXxx names instead of trailing-underscore
// style: .github/scripts/test_app_launch_args.py scrapes appsupport.cpp and compiles the launch
// body as a free C function against host stubs, so that body must keep addressing its state as
// `appsList` and every helper it calls as a free function of the same name.

#include "include/gamesource.h"
#include "include/appsupport.h" // app_info_t, APP_* sizes

// A discovery record while the list is being built: update() collects nodes and then copies
// them into the flat appsList in reverse order.
struct app_info_linked
{
    struct app_info_linked *next;
    app_info_t app;
};

class AppSource: public GameSource
{
public:
    AppSource();

    void init(item_list_t *itemList) override;
    int needsUpdate(item_list_t *itemList) override;
    int update(item_list_t *itemList) override;
    int count(item_list_t *itemList) override;
    char *name(item_list_t *itemList, int id) override;
    int nameLength(item_list_t *itemList, int id) override;
    char *startup(item_list_t *itemList, int id) override;
    void deleteGame(item_list_t *itemList, int id) override;
    void rename(item_list_t *itemList, int id, char *newName) override;
    void launch(item_list_t *itemList, int id, config_set_t *configSet) override;
    config_set_t *config(item_list_t *itemList, int id) override;
    int image(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm) override;
    int textId(item_list_t *itemList) override;
    int iconId(item_list_t *itemList) override;
    int rowView(item_list_t *itemList, int id) override;
    int sourceId(item_list_t *itemList, int id) override;
    void cleanUp(item_list_t *itemList, int exception) override;
    void shutdown(item_list_t *itemList) override;

    // Used by the free callbacks/forwarders in appsupport.cpp (appScanCallback, appGetArtMode).
    void appSetResolvedStartup(app_info_t *app);
    app_info_t *appLookupByStartup(const char *startup);

private:
    int appVisibleToMaster(item_list_t *itemList, int id);
    int appRebuildVisibleList(void);
    struct config_value_t *appGetConfigValue(int id);
    const char *appBuildStartupPath(const char *path, const char *boot);
    const char *appResolveLegacyMassStartup(const char *startup);
    void appFreeArtLookup(void);
    int appBuildArtLookup(void);
    int addAppsLegacyList(struct app_info_linked **appsLinkedList);
    void appDedupList(void);
    void appFreeList(void);
#ifdef __OPLDIAG
    void appDumpDiscovery(void);
#endif

    int appForceUpdate = 1;
    int appItemCount = 0;
    char appStartupPath[256] = {};
    char appResolvedStartupPath[256] = {};
    int *appArtLookupTable = NULL;
    int appArtLookupTableSize = 0;

    config_set_t *configApps = NULL;
    app_info_t *appsList = NULL;
    int *appVisibleIds = NULL;
    int appVisibleCount = 0;
    int appViewOnlyUpdate = 0;
    int appDroppedCount = 0;
};

#endif
