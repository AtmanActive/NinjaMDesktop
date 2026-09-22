/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Remembers accepted server license agreements, as ReaNINJAM does inside REAPER.
//
// The open-source ReaNINJAM shows the agreement on every connect (licensecallback() in
// license.cpp). We wrap that callback: once the user accepts a server's license, we store a
// SHA-1 of its text under the server's address, and skip the dialog while the text is
// unchanged. If the server changes its license, the dialog appears again.

#include <string.h>
#include <ctype.h>

#include "host.h" // windows.h or swell.h, which winclient.h expects first
#include "winclient.h"
#include "WDL/sha.h"

#define LICENSE_SEC "ninjamdesktop_licenses"

// ini key for a server address: lowercase, without characters the ini format can't hold
static void license_key(const char *host, char *buf, int bufsz)
{
  int n = 0;
  for (; *host && n < bufsz - 1; host++)
  {
    const unsigned char c = (unsigned char)*host;
    buf[n++] = (c <= ' ' || c == '=' || c == '[' || c == ']' || c == ';') ? '_' : (char)tolower(c);
  }
  buf[n] = 0;
}

static void license_hash(const char *text, char *hex /* 41 bytes */)
{
  WDL_SHA1 sha;
  sha.add(text, (int)strlen(text));
  unsigned char res[WDL_SHA1SIZE];
  sha.result(res);
  static const char digits[] = "0123456789abcdef";
  for (int i = 0; i < WDL_SHA1SIZE; i++)
  {
    hex[i*2] = digits[res[i] >> 4];
    hex[i*2+1] = digits[res[i] & 15];
  }
  hex[WDL_SHA1SIZE*2] = 0;
}

// called by NJClient::Run() on ReaNINJAM's network thread, like licensecallback()
static int host_licensecallback(void *userData, const char *licensetext)
{
  if (!licensetext || !*licensetext) return 1;

  char key[512], hash[WDL_SHA1SIZE*2 + 1], stored[64];
  license_key(g_client && g_client->GetHostName() ? g_client->GetHostName() : "", key, sizeof(key));
  license_hash(licensetext, hash);

  if (key[0])
  {
    GetPrivateProfileString(LICENSE_SEC, key, "", stored, sizeof(stored), g_ini_file.Get());
    if (!strcmp(stored, hash)) return 1;
  }

  const int ret = licensecallback(userData, licensetext); // ReaNINJAM's dialog
  if (ret > 0 && key[0]) WritePrivateProfileString(LICENSE_SEC, key, hash, g_ini_file.Get());
  return ret;
}

void License_Install()
{
  if (g_client) g_client->LicenseAgreementCallback = host_licensecallback;
}
