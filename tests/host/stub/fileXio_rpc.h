#pragma once
typedef struct { unsigned int mode; unsigned long long size; } iox_stat_t;
int fileXioGetStat(const char *path, iox_stat_t *stat);
