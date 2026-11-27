# ript-core-24.b notes (lens defects)

## Done (all files read)
- src/sources/appsupport.cpp 1-1444
- include/bdmboot.h 1-41
- src/sources/bdmboot.cpp 1-496
- src/sources/bdmdevices.cpp 1-582
- src/sources/bdmlaunch.cpp 1-999

## Kept (checked)
- bdmdevices.cpp:286-309 fast reappearance: old list shown for a different volume in the same slot; skips publish gates (flash for disabled transport) | chain checked: bdmdevices 437-449, supportbase 707-714, bdmsupport 1725-1734, 1814-1818; slot reuse INFERRED
- appsupport.cpp:944-965 rename: legacy removeKey(cur->key) reads freed node (config.cpp 447-474) and drops entry if newName empty or has '=' (config.cpp 221-227, '=' on keyboard diaeditors.cpp:149); scanned branch writes title.cfg even if configRead failed
- appsupport.cpp:696-721,374-381,1323-1342 legacy argv1 uninitialised, donated argv1 never used
- appsupport.cpp:1181 mmceReset before validation returns

## Dropped
- strncpy 949 (val is NUL-terminated 256), configGetStrCopy 1088 (clears on absent), appItemCount vs nodes (scanApps counts ret==0), init leak (only after cleanUp), art drain timeout (image() copies value at once), bdmReadDeviceIdentity (memsets driver), filename[32] (GAME_STARTUP_MAX 12), last_played double save (setStr only marks on change), bdmboot (budgeted design), bdmWaitForDevice u32 (wraps correctly)
