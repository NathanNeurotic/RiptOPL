#ifndef __CONFIGSET_H
#define __CONFIGSET_H

// One parsed config file: an ordered key/value list plus where it came from and how to write it back.
// C++ only -- C translation units see an opaque `config_set_t` (see config.h). Storage and key rules live
// here; file syntax (legacy / libconfig) stays in config.cpp, which drives a set through this interface.

#include <stddef.h>
#include <string.h>

#include "config.h" // CONFIG_KEY_*_LEN

struct config_value_t
{
    // Including the NULL terminator
    char key[CONFIG_KEY_NAME_LEN];
    char val[CONFIG_KEY_VALUE_LEN];

    struct config_value_t *next;
};

class ConfigSet
{
public:
    // Registry slots are static storage; a zeroed set is "unused" (type 0).
    constexpr ConfigSet() = default;
    ConfigSet(const ConfigSet &) = delete;
    ConfigSet &operator=(const ConfigSet &) = delete;

    // (Re)initialise in place. fileName is copied; false on OOM (set left empty, unnamed).
    bool init(int type, const char *fileName, u32 uid);
    // Drop all entries and the file name. The set itself stays usable (registry slots).
    void release();

    // Heap sets: malloc + placement new (no exceptions/nothrow machinery). NULL on OOM.
    static ConfigSet *create(int type, const char *fileName, u32 uid);
    static void destroy(ConfigSet *set);
    ConfigSet *clone(u32 uid) const;

    int type() const { return type_; }
    u32 uid() const { return uid_; }
    const char *filename() const { return filename_; }
    // openFile()/openFileBuffer() resolve an "mc?" placeholder in place, so they need the real buffer.
    char *filenameForOpen() { return filename_; }
    bool rename(const char *fileName);
    bool modified() const { return modified_ != 0; }
    void markModified() { modified_ = 1; }
    void clearModified() { modified_ = 0; }
    // CFG_FMT_*: latched from the file we READ, honoured on write.
    int format() const { return format_; }
    void setFormat(int fmt) { format_ = fmt; }
    const config_value_t *entries() const { return head_; }
    config_value_t *entries() { return head_; }

    // Keys beginning with '#' are runtime-only and never mark the set modified.
    int setStr(const char *key, const char *value);
    int getStr(const char *key, const char **value) const;
    // Clears `value` when the key is absent.
    int getStrCopy(const char *key, char *value, int length) const;
    int setInt(const char *key, int value);
    int getInt(const char *key, int *value) const;
    int setColor(const char *key, const unsigned char *color);
    int getColor(const char *key, unsigned char *color) const;
    int removeKey(const char *key);
    void merge(const ConfigSet &source);
    void getDiscIDBinary(void *dst) const; // dst has to have 5 bytes space

    // Low level append: no uniqueness check, no validation, never marks modified.
    void append(const char *key, const char *val);
    // Empty the list and mark modified.
    void clear();

    // VMC slot helpers (key names are file format, see CONFIG_ITEM_VMC*).
    void getVMC(char *vmc, int length, int slot) const;
    void setVMC(const char *vmc, int slot);
    void removeVMC(int slot);
    void getVMCDisable(int slot, int *disabled) const;
    void setVMCDisable(int slot, int disabled);
    void removeVMCDisable(int slot);

private:
    config_value_t *find(const char *name) const;
    void freeEntries();

    int type_ = 0;
    config_value_t *head_ = NULL;
    config_value_t *tail_ = NULL;
    char *filename_ = NULL;
    int modified_ = 0;
    int format_ = 0;
    u32 uid_ = 0;
};

#endif
