# ript-core-24 notes (lens structure)

## Done (all files read in full)
- src/sources/appsupport.cpp 1-1444
- include/bdmboot.h 1-41
- src/sources/bdmboot.cpp 1-496
- src/sources/bdmdevices.cpp 1-582
- src/sources/bdmlaunch.cpp 1-999

## Candidates (to verify with grep)
### appsupport.cpp
- 178-181 | appIsPopstarterElf title param unused ((void)title) | check callers/tests
- 677-689,736-748 | if/else both malloc and link identically; duplicated in two functions | confirm
- 90-101 vs 138-154 | appGetELFName vs appBasename near-dup | callers
- 1418-1443 | cleanUp and shutdown identical bodies | base class / other sources
- 1090-1116 | launch re-implements mass:/mass?: resolution = appResolveLegacyMassStartup 438-464
- 289-299,510-584 | art lookup hash table + linear fallback after hash miss | callers of appLookupByStartup
- 273-287,1212-1241 | Ember Phase 0 probe under __OPLDIAG | real Ember path exists in bdmlaunch.cpp launchCue -> probe superseded?
- 499-503,533-534,481 | impossible-state checks | minor group
- 1335-1342 vs 1357-1364 | config() media/format/size block twice
- 29-43 | appTitleIsPs1Elf hand-rolled [PS1] matcher | other copies?
- 361,375 | appDroppedCount only read under __OPLDIAG
### bdmboot.h / bdmboot.cpp
- bdmboot.h whole + bdmboot.cpp 41,472-496 | stateless class BdmBoot, one global, free forwarders, public static shims slotIdentity/elfOnSlot | who uses gBdmBoot / bdmboot.h
- bdmboot.cpp 84-112 | bdmPickBootElfSlot callback+ctx with single caller/impl | tests referencing it?
- bdmboot.cpp 158-161,180-189,216-225 | identical wait-for-identity loop three times
- bdmboot.cpp 49-54,70-73,120-123 | tail normalisation snprintf three times
- bdmboot.cpp 281 | comment names resolveBootDirToMass | exists?
- bdmboot.cpp 1-36, bdmdevices.cpp 1-36, bdmlaunch.cpp 1-39 | include block copied from bdmsupport.cpp, mostly unused, comments describe code elsewhere
- bdmboot.cpp 345-356,420-429,449-457 | history narrative comments (rebuild-135 etc)
### bdmdevices.cpp
- 458-483 bdmWaitForDevice, 485-497 bdmDeviceIsPresent | callers? dup of Dopen probe
- 341 vs 347 redundant type write; 353-357 same condition twice; 359-362 unreachable else branches (driver non-empty guaranteed by 326)
- 222,231 commented-out LOG
- 233-259 __DEBUG cold-diag block; BdmDiag* machinery | where defined, still needed?
- 551-566 switch duplicates the loaded-flag mapping of bdmEnsureTransportLoaded 506-531
- 135-153 bdmEnumerateDevices | callers
### bdmlaunch.cpp
- 271,347,444 | fragBudgetOk isUdp param always 0 -> _STR_NEUTRINO_TOO_FRAGMENTED_NET dead?
- 360-364,501-505,965-971 | autolaunch teardown x3
- 453-456,898-901 | last_played save x2 (other sources too?)
- 480-485,957-960 | mmce settle gate x2
- 103-108,197-202 | MX4SIO quiesce x2
- 975-998 | six sysLaunchLoaderElf calls differing by mode string/flags; 983,986 `|= 0` no-ops (TODO marker)
- 46-47,120-123,255-258 | BdmLauncher/NeutrinoLauncher single-instance stateless classes + forwarders | headers
- 128 | launchVcd configSet unused
- 652-654 | stale comment vs bdmdevices.cpp:139-142
- 911-939 | DMA setup: dup in hddsupport?
- 576-604 bdmCheckNativeVmcLayout | callers
