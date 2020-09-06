#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdarg>
#include <cstdio>

int vscprintf(const char *format, va_list ap)
{
	va_list ap_copy;
	va_copy(ap_copy, ap);
	int retval = vsnprintf(NULL, 0, format, ap_copy);
	va_end(ap_copy);
	return retval;
}

int vasprintf(char **strp, const char *format, va_list ap)
{
	int len = vscprintf(format, ap);
	if(len == -1) {
		return -1;
	}
	char *str = (char*)malloc((size_t) len + 1);
	if(!str) {
		return -1;
	}
	int retval = vsnprintf(str, len + 1, format, ap);
	if(retval == -1) {
		free(str);
		return -1;
	}
	*strp = str;
	return retval;
}

int asprintf(char **strp, const char *format, ...)
{
	va_list ap;
	va_start(ap, format);
	int retval = vasprintf(strp, format, ap);
	va_end(ap);
	return retval;
}

#endif
