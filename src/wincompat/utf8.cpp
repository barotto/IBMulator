/*
  (c) Mircea Neacsu 2014-2019. Licensed under MIT License.
*/
#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace std;
namespace utf8 {

/*!
  Conversion from wide character to UTF-8

  \param  s   input string
  \param  nch number of character to convert or 0 if string is null-terminated
  \return UTF-8 character string
*/
std::string narrow (const wchar_t* s, size_t nch)
{
  int nsz;
  if (!s || !(nsz = WideCharToMultiByte (CP_UTF8, 0, s, (nch?(int)nch:-1), 0, 0, 0, 0)))
    return string ();

  string out (nsz, 0);
  WideCharToMultiByte (CP_UTF8, 0, s, -1, &out[0], nsz, 0, 0);
  if (!nch)
    out.resize (nsz - 1); //output is null-terminated
  return out;
}

/*!
  Conversion from wide character to UTF-8

  \param  s input string
  \return UTF-8 character string
*/
std::string narrow (const std::wstring& s)
{
  int nsz = WideCharToMultiByte (CP_UTF8, 0, s.c_str(), -1, 0, 0, 0, 0);
  if (!nsz)
    return string ();

  string out (nsz, 0);
  WideCharToMultiByte (CP_UTF8, 0, s.c_str (), -1, &out[0], nsz, 0, 0);
  out.resize (nsz - 1); //output is null-terminated
  return out;
}

/*!
  Conversion from UTF-8 to wide character

  \param  s input string
  \param nch number of characters to convert or 0 if string is null-terminated
  \return wide character string
*/
std::wstring widen (const char* s, size_t nch)
{
  int wsz;
  if (!s || !(wsz = MultiByteToWideChar (CP_UTF8, 0, s, (nch?(int)nch:-1), 0, 0)))
    return wstring ();

  wstring out (wsz, 0);
  MultiByteToWideChar (CP_UTF8, 0, s, -1, &out[0], wsz);
  if (!nch)
    out.resize (wsz - 1); //output is null-terminated
  return out;
}

/*!
  Conversion from UTF-8 to wide character

  \param  s input string
  \return wide character string
*/
std::wstring widen (const std::string& s)
{
  int wsz = MultiByteToWideChar (CP_UTF8, 0, s.c_str(), -1, 0, 0);
  if (!wsz)
    return wstring ();

  wstring out (wsz, 0);
  MultiByteToWideChar (CP_UTF8, 0, s.c_str (), -1, &out[0], wsz);
  out.resize (wsz - 1); //output is null-terminated
  return out;
}

/*!
  Gets the current working directory
  \return UTF-8 encoded name of working directory
*/
std::string getcwd ()
{
  wchar_t tmp[_MAX_PATH];
  if (_wgetcwd (tmp, _countof (tmp)))
    return narrow (tmp);
  else
    return string ();
}

/*!
  Retrieves the value of an environment variable
  \param  var name of environment variable
  \return value of environment variable or an empty string if there is no such
          environment variable
*/
char* getenv (const std::string& var)
{
  static string nstr;
  size_t nsz;
  wstring wvar = widen (var);
  _wgetenv_s (&nsz, 0, 0, wvar.c_str ());
  if (!nsz)
    return nullptr;

  wstring wval (nsz, L'\0');
  _wgetenv_s (&nsz, &wval[0], nsz, wvar.c_str ());
  wval.resize (nsz - 1);
  
  nstr = narrow (wval);
  return nstr.data();
}

/*!
  Converts wide byte command arguments to an array of pointers
  to UTF-8 strings.

  \param  argc Pointer to an integer that contains number of parameters
  \return array of pointers to each command line parameter or NULL if an error
  occurred.

  The space allocated for strings and array of pointers should be freed
  by calling free_utf8argv()
*/
char** get_argv (int* argc)
{
  char** uargv = nullptr;
  wchar_t** wargv = CommandLineToArgvW (GetCommandLineW (), argc);
  if (wargv)
  {
    uargv = new char* [*argc];
    for (int i = 0; i < *argc; i++)
    {
      int nc = WideCharToMultiByte (CP_UTF8, 0, wargv[i], -1, 0, 0, 0, 0);
      uargv[i] = new char[nc + 1];
      WideCharToMultiByte (CP_UTF8, 0, wargv[i], -1, uargv[i], nc, 0, 0);
    }
    LocalFree (wargv);
  }
  return uargv;
}

/*!
  Frees the memory allocated by get_argv(int *argc)

  \param  argc  number of arguments
  \param  argv  array of pointers to arguments
*/
void free_argv (int argc, char** argv)
{
  for (int i = 0; i < argc; i++)
    delete argv[i];
  delete argv;
}


} //end namespace

#endif