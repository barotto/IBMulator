/*
  (c) Mircea Neacsu 2014-2019. Licensed under MIT License.
*/
#ifndef IBMULATOR_UTF8_H
#define IBMULATOR_UTF8_H

#ifdef _WIN32

#include <string>

namespace utf8 {

std::string narrow(const wchar_t* s, size_t nch);
std::string narrow(const std::wstring& s);
std::wstring widen(const char* s, size_t nch);
std::wstring widen(const std::string& s);
std::string getcwd ();
char* getenv(const std::string& var);
char** get_argv(int* argc);
void free_argv(int argc, char** argv);

}

#endif

#endif