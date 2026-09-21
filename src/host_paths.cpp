/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Per-user file locations.

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <unistd.h>
#include <pwd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "host.h"
#include "WDL/wdlstring.h"

bool Host_CreateDirectories(const char *path)
{
  WDL_FastString p(path);
  char *buf = (char *)p.Get();
  const int len = p.GetLength();
  for (int i = 1; i <= len; i++)
  {
    if (i == len || buf[i] == '/' || buf[i] == '\\')
    {
      const char save = buf[i];
      buf[i] = 0;
#ifdef _WIN32
      if (!(i == 2 && buf[1] == ':')) CreateDirectory(buf, NULL);
#else
      mkdir(buf, 0755);
#endif
      buf[i] = save;
    }
  }
#ifdef _WIN32
  const DWORD attr = GetFileAttributes(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else
  struct stat st;
  return !stat(path, &st) && S_ISDIR(st.st_mode);
#endif
}

#ifndef _WIN32
static const char *home_dir()
{
  const char *h = getenv("HOME");
  if (h && *h) return h;
  struct passwd *pw = getpwuid(getuid());
  return pw && pw->pw_dir ? pw->pw_dir : "/tmp";
}
#endif

const char *Host_GetConfigDir()
{
  static WDL_FastString s_dir;
  if (!s_dir.GetLength())
  {
#ifdef _WIN32
    char buf[MAX_PATH] = "";
    if (SHGetFolderPath(NULL, CSIDL_APPDATA | CSIDL_FLAG_CREATE, NULL, 0, buf) != S_OK || !buf[0])
      GetTempPath(sizeof(buf), buf);
    s_dir.Set(buf);
    s_dir.Append("\\" NJD_APP_NAME);
#elif defined(__APPLE__)
    s_dir.Set(home_dir());
    s_dir.Append("/Library/Application Support/" NJD_APP_NAME);
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && xdg[0] == '/') s_dir.Set(xdg);
    else
    {
      s_dir.Set(home_dir());
      s_dir.Append("/.config");
    }
    s_dir.Append("/" NJD_APP_NAME);
#endif
    Host_CreateDirectories(s_dir.Get());
  }
  return s_dir.Get();
}

const char *Host_GetDocumentsDir()
{
  static WDL_FastString s_dir;
  if (!s_dir.GetLength())
  {
#ifdef _WIN32
    char buf[MAX_PATH] = "";
    if (SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, 0, buf) == S_OK && buf[0]) s_dir.Set(buf);
    else s_dir.Set(Host_GetConfigDir());
#else
    s_dir.Set(home_dir());
#ifndef __APPLE__
    // honour XDG_DOCUMENTS_DIR if it is exported, otherwise use ~/Documents when it exists
    const char *xdg = getenv("XDG_DOCUMENTS_DIR");
    if (xdg && xdg[0] == '/')
    {
      s_dir.Set(xdg);
      return s_dir.Get();
    }
#endif
    WDL_FastString docs(s_dir.Get());
    docs.Append("/Documents");
    struct stat st;
    if (!stat(docs.Get(), &st) && S_ISDIR(st.st_mode)) s_dir.Set(docs.Get());
#endif
  }
  return s_dir.Get();
}
