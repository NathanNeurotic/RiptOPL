#pragma once
typedef struct _io_driver {
    int (*open)(const char *, int, ...);
    int (*close)(int);
    int (*read)(int, void *, int);
    int mode;
} _io_driver;
inline int GetRomNameWithIODriver(char *name, _io_driver *) { name[4] = 'A'; return 0; }
