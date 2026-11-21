/*
  DIAG.TXT, ported from RIPPS2 build 115 (akilluminati47/RIPPS2, ripps2status.c). See include/diaglog.h; the log
  itself is platform/diaglog.cpp.
*/

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>

#include "include/opl.h"
#include "include/config.h"
#include "include/pad.h"
#include "include/diaglog.h"
#include "include/mmcesupport.h"

#define DIAG_SIZE (16 * 1024) // as large as the ring, so a copy holds all of it

int diagSave(const char *dir, char *path, int pathSize)
{
    static char copy[DIAG_SIZE];
    char head[400];
    u32 notReady = 0, empty = 0;
    int len, fd, ok, wrapped = 0;

    size_t dirLen;

    if (dir == NULL)
        dir = configGetHomePath(); // the settings folder; configGetDir() is only its device
    dirLen = strlen(dir);
    snprintf(path, pathSize, "%s%sDIAG.TXT", dir, dirLen == 0 || dir[dirLen - 1] == '/' || dir[dirLen - 1] == ':' ? "" : "/");
    len = diagCopy(copy, sizeof(copy), &wrapped);
    padGetFaultCounters(&notReady, &empty);
    snprintf(head, sizeof(head),
             "RiptOPL %s diagnostics, %d s after boot\r\n"
             "longest run of missed pad reads: %u polls; of empty reads: %u polls\r\n"
             "MMCE: Game ID %s\r\n%s\r\n\r\n",
             OPL_VERSION, (int)(clock() / CLOCKS_PER_SEC), (unsigned)notReady, (unsigned)empty, mmceGameIdModeLabel(gMMCEEnableGameID),
             wrapped ? "(the oldest lines have rolled out of the log)" : "");
    if ((fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666)) < 0)
        return 0;
    ok = write(fd, head, strlen(head)) == (int)strlen(head) && write(fd, copy, len) == len;
    close(fd);
    return ok;
}

void diagSaveBeforeLaunch(void)
{
    char path[300];
    const char *dir = configGetDir();

    if (!gEnableDebug || dir == NULL || !strncmp(dir, "mc", 2) || !strncmp(dir, "mmce", 4))
        return;
    diagSave(NULL, path, sizeof(path));
}
