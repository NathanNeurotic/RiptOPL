#ifndef __BDMINT_H
#define __BDMINT_H

// Internals shared between bdmsupport.cpp and the files split out of it (bdmboot.cpp, ...). Not public API.

#include "bdmsupport.h"

int bdmDetermineDeviceType(const char *driverName);
int bdmEnsureTransportLoaded(int bdmType);
void bdmLoadCoreModules(int forceUsb);
extern s32 bdmLoadModuleLock;
int bdmParseBootStem(const char *bootPath, char *stem, size_t stemSize, int *unit);
int bdmReadDeviceIdentity(const char *path, char *driverName, int driverNameLength, int *deviceIndex);

void bdmBuildGamePrefix(char *target, int targetLength, const char *deviceRoot);
int bdmDeviceIsPresent(int deviceId);
extern item_list_t bdmDeviceList[MAX_BDM_DEVICES];
extern int bdmDeviceListInitialized;
/** The BdmSource instance as the item_list_t each device slot is cloned from (its `source` pointer is shared by
    every slot, so the thunks reach the one BdmSource). */
item_list_t *bdmTemplateList(void);
extern int bdmDeviceModeStarted;
void bdmLoadBlockDeviceModules(void);
int bdmLoadOptionalModule(const char *name, void *module, int moduleSize);
void bdmLogIdentityDiagnosticLive(const char *event, item_list_t *itemList, const config_set_t *configSet);
int bdmTransportEnabled(int bdmDeviceType);
int bdmWaitForDevice(int deviceId, u32 timeoutMs);
extern int hddModLoaded;
extern int iLinkManModLoaded;
extern int iLinkModLoaded;
extern int iUSBModLoaded;
extern int ieee1394ModLoaded;
extern int mx4sioModLoaded;

base_game_info_t *bdmActiveGame(item_list_t *itemList, int id);
void bdmBuildPs1Prefix(char *target, int targetLength, int massSlot);
int bdmDriverIsATA(const char *driverName);
int bdmDriverIsIlink(const char *driverName);
int bdmDriverIsMx4sio(const char *driverName);
int bdmDriverIsUDPBD(const char *driverName);
int bdmDriverIsUSB(const char *driverName);
extern base_game_info_t bdmEmptyGame;
int bdmGetItemView(item_list_t *itemList, int id);
void bdmLaunchCue(item_list_t *itemList, const char *cueName, config_set_t *configSet);
void bdmLaunchVcd(item_list_t *itemList, const char *vcdName, config_set_t *configSet);
void bdmSetLaunchDeviceBinding(struct cdvdman_settings_bdm *settings, const char *driverName, int deviceIndex);

void bdmInitDevicesData();
int bdmUpdateDeviceData(item_list_t *itemList);
extern unsigned int BdmGeneration;

#ifdef __DEBUG
// Debug-only event/IOP snapshot state written by bdmEventHandler and reported from the device-update worker.
extern volatile unsigned int BdmDiagEventPreviousGeneration;
extern volatile unsigned int BdmDiagEventGeneration;
extern volatile unsigned int BdmDiagEventReceivedCount;
extern volatile int BdmDiagEventCause;
extern volatile unsigned int BdmDiagIopCallbackSequence;
extern volatile unsigned int BdmDiagIopMountCallbackCount;
extern volatile unsigned int BdmDiagIopUnmountCallbackCount;
extern volatile unsigned int BdmDiagIopBlockDeviceCount;
extern volatile unsigned int BdmDiagIopUsbRootCount;
extern volatile unsigned int BdmDiagIopUsbRootMask;
extern volatile int BdmDiagEventPending;
extern volatile int BdmDiagIopSnapshotPending;
extern unsigned int BdmDiagSnapshotRequestCount;
extern unsigned int BdmDiagSlotProbeCount[MAX_BDM_DEVICES];
void bdmDiagRequestIopSnapshot(const char *reason, int mode, int openResult);
void bdmDiagFlushPendingIopState(item_list_t *itemList);
#endif

#endif
