// Host stub for the PS2SDK header tar.cpp includes; Linux already has lseek64/open/read.
#pragma once
#ifndef _LARGEFILE64_SOURCE
#define _LARGEFILE64_SOURCE
#endif
#include <sys/types.h>
#include <unistd.h>
