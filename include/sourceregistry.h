#ifndef __SOURCEREGISTRY_H
#define __SOURCEREGISTRY_H

#include "include/opl.h" // opl_io_module_t, MODE_COUNT

// The page registry (src/app/opl.cpp): one opl_io_module_t per IO mode. This is the table the old
// file-scope list_support[] held; opl.cpp reaches it through a constant-initialized pointer that
// keeps the list_support name (test_io_queue_trims.py extracts menuDeferredUpdate() and compiles
// it against a harness declaring its own file-scope list_support[]). One instance, members
// constant-initialized (no dynamic init before main).
class SourceRegistry
{
public:
    opl_io_module_t *module(int mode) { return &modules_[mode]; }
    // First slot of the table, for code that iterates modes by index.
    opl_io_module_t *table() { return modules_; }

    // Art lookup addressed by IO MODE rather than by a device string (body in opl.cpp; the
    // oplGetAppImageByMode entry point in appscan.cpp forwards here).
    int appImageByMode(int mode, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm);

private:
    opl_io_module_t modules_[MODE_COUNT] = {};
};

extern SourceRegistry gSourceRegistry;

#endif
