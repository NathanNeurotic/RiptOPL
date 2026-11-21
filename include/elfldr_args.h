#ifndef ELFLDR_ARGS_H
#define ELFLDR_ARGS_H

#include <string.h>

#define ELFLDR_OPTION_MARKER "-rlopt=0"

/* The child receives the load path, target argv, loader options and a marker. */
static inline int elfldrArgsFit(const char *filename, int argc, char *const argv[],
                                const char *loaderOption, const char *fallbackOption)
{
    const int extra = (loaderOption != NULL) + (fallbackOption != NULL);
    if (filename == NULL || argv == NULL || argc < 1 || argc > 15 - 2 - extra)
        return 0;

    size_t bytes = strlen(filename) + 1 + sizeof(ELFLDR_OPTION_MARKER);
    if (loaderOption != NULL)
        bytes += strlen(loaderOption) + 1;
    if (fallbackOption != NULL)
        bytes += strlen(fallbackOption) + 1;
    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL)
            return 0;
        bytes += strlen(argv[i]) + 1;
    }
    return bytes <= 256;
}

#endif
