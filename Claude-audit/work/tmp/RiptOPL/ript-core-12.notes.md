# ript-core-12 notes (lens structure)

## Read (second pass, all 9 files re-read in full): pad.cpp, riptlite.h, riptlite.cpp, settings.h, settings.cpp, sfx.cpp, sound.h, sound.cpp, vmc_groups.h

## Verified -> findings
- sfxGetDropDiag stub always 0, only caller guioverlay.cpp:202-213 (SX field), stale include comments in 5 ui files
- pad.h/sound.h forwarding layer over single static PadManager / SoundEffects; out-of-line members gc'd (riptopl-discarded-firstparty.txt)
- LITE_DEVICE_NAME x8, LITE_HAS_TCPIP, LITE_HAS_NBD only feed TCPIP: unused
- pad.cpp in-file dupes: deadzone switches 317-349, actAlign init 279-284/613-618, rumble() branches 736-752
- initialize() always returns 1; startOne check 1144-1146 dead
- stale comments pad.cpp 34, 47, 71-77, 581 (five vs four), 641, 713; padmanager.h 25-26; settings.cpp 3-5, settings.h 7
- unused includes sfx.cpp 9-10, sound.cpp 8-10,16-17; stale banners sfx.cpp 21-22, sound.cpp 19-20
- settings.cpp 64-67 two adjacent #ifdef RETROACHIEVEMENTS
- screenPosStore/Recall only used in settings.cpp, exported in opl.h:430-431

## Dropped
- SCREEN_POS_MODES 20 matches 20 vmodeNames (guisettings.cpp:2607-2616)
- settingsKeyAt: host test hook (tests/host/test_settings.cpp) - kept
- vmc_groups.h used; sound.h other prototypes used
