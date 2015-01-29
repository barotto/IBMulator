#ifndef IBMULATOR_WINCOMPAT_H
#define IBMULATOR_WINCOMPAT_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int mkstemp(char *tmpl);
char * realpath(const char *name, char *resolved);

#endif

#endif
