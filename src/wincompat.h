#ifndef IBMULATOR_WINCOMPAT_H
#define IBMULATOR_WINCOMPAT_H

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int mkstemp(char *tmpl);
int mkostemp(char *tmpl, int flags);
char * realpath(const char *name, char *resolved);
int asprintf(char **strp, const char *format, ...);

#endif

#endif
