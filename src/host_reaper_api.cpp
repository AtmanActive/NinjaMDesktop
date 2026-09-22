/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Replacements for everything ReaNINJAM normally receives from REAPER through its
// VST wrapper (jmde/fx/reaninjam/vstframe.cpp). Functions that only make sense
// inside a DAW (transport, project tempo, loop range) are left NULL; ReaNINJAM
// checks for that before calling them.

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#else
#include "WDL/swell/swell.h"
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>

#define WANT_LOCALIZE_IMPORT_INCLUDE
#include "winclient.h"
#define WDL_WIN32_HIDPI_IMPL
#include "WDL/win32_hidpi.h"

#include "host.h"

// ---------------------------------------------------------------------------
// Globals that vstframe.cpp defines for the rest of ReaNINJAM
// ---------------------------------------------------------------------------
HINSTANCE g_hInst;
DWORD g_main_thread;

class VSTEffectClass;
VSTEffectClass *g_vst_object; // stays NULL: without a VST host, closing the window quits

void *get_parent_project(void) { return NULL; }

// ---------------------------------------------------------------------------
// REAPER API function pointers (same declarations as vstframe.cpp)
// ---------------------------------------------------------------------------
void (*format_timestr_pos)(double tpos, char *buf, int buflen, int modeoverride);
HANDLE * (*GetIconThemePointer)(const char *name);
HWND (*GetMainHwnd)();
void *(*CreateVorbisEncoder)(int srate, int nch, int serno, float qv, int cbr, int minbr, int maxbr);
void *(*CreateVorbisDecoder)();
void (*PluginWantsAlwaysRunFx)(int amt);
int (*GetWindowDPIScaling)(HWND hwnd);
#ifdef _WIN32
LRESULT (*handleCheckboxCustomDraw)(HWND, LPARAM, const unsigned short *list, int listsz, bool isdlg);
#endif
INT_PTR (*autoRepositionWindowOnMessage)(HWND hwnd, int msg, const char *desc_str, int flags);
int (*GetPlayStateEx)(void *proj);
void (*OnPlayButtonForTime)(void *proj, double forTime);
void (*SetEditCurPos2)(void *proj, double time, bool moveview, bool seekplay);
void (*SetCurrentBPM)(void *proj, double bpm, bool wantUndo);
void (*GetSet_LoopTimeRange2)(void* proj, bool isSet, bool isLoop, double* startOut, double* endOut, bool allowautoseek);
int (*GetSetRepeatEx)(void* proj, int val);
double (*GetCursorPositionEx)(void *proj);
void (*Main_OnCommandEx)(int command, int flag, void *proj);

void (*GetProjectPath)(char *buf, int bufsz);
const char *(*get_ini_file)();
BOOL  (WINAPI *InitializeCoolSB)(HWND hwnd);
HRESULT (WINAPI *UninitializeCoolSB)(HWND hwnd);
int   (WINAPI *CoolSB_SetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi, BOOL fRedraw);
BOOL (WINAPI *CoolSB_GetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi);
int (WINAPI *CoolSB_SetScrollPos)(HWND hwnd, int nBar, int nPos, BOOL fRedraw);
int (WINAPI *CoolSB_SetScrollRange)(HWND hwnd, int nBar, int nMinPos, int nMaxPos, BOOL fRedraw);
BOOL (WINAPI *CoolSB_SetMinThumbSize)(HWND hwnd, UINT wBar, UINT size);
int (*plugin_register)(const char *name, void *infostruct);

// declared in winclient.h; REAPER provides these through IMPORT_REAPER_COMMON
double (*DB2SLIDER)(double x);
double (*SLIDER2DB)(double y);
void (*SetWindowAccessibilityString)(HWND h, const char *, int mode);

// ---------------------------------------------------------------------------
// WDL coolscroll (built with its public functions renamed to NJD_*, see CMakeLists.txt)
// ---------------------------------------------------------------------------
extern "C" {
BOOL WINAPI NJD_InitializeCoolSB(HWND hwnd);
HRESULT WINAPI NJD_UninitializeCoolSB(HWND hwnd);
int WINAPI NJD_CoolSB_SetScrollInfo(HWND hwnd, int fnBar, LPSCROLLINFO lpsi, BOOL fRedraw);
BOOL WINAPI NJD_CoolSB_GetScrollInfo(HWND hwnd, int fnBar, LPSCROLLINFO lpsi);
int WINAPI NJD_CoolSB_SetScrollPos(HWND hwnd, int nBar, int nPos, BOOL fRedraw);
int WINAPI NJD_CoolSB_SetScrollRange(HWND hwnd, int nBar, int nMinPos, int nMaxPos, BOOL fRedraw);
BOOL WINAPI NJD_CoolSB_SetMinThumbSize(HWND hwnd, UINT wBar, UINT size);

// hooks coolscroll expects the application to provide
void *NJD_CoolSB_GetIconThemePointer(const char *name) { return NULL; } // NULL: default look
int CoolSB_GetSysColor(HWND hwnd, int val) { return (int)Host_GetSysColor(val); }
};

// ---------------------------------------------------------------------------
// Icons for REAPER's "HotTrackButton" mute/solo buttons (drawn by host_controls.cpp)
// ---------------------------------------------------------------------------
static HANDLE s_icon_slots[NJD_ICON_COUNT];

static HANDLE *host_GetIconThemePointer(const char *name)
{
  static const struct { const char *name; int id; } tab[] = {
    { "track_mute_off", NJD_ICON_MUTE_OFF },
    { "track_mute_on", NJD_ICON_MUTE_ON },
    { "track_solo_off", NJD_ICON_SOLO_OFF },
    { "track_solo_on", NJD_ICON_SOLO_ON },
  };
  if (name) for (size_t x = 0; x < sizeof(tab)/sizeof(tab[0]); x++)
    if (!strcmp(name, tab[x].name)) return &s_icon_slots[tab[x].id];
  return NULL;
}

int Host_IconFromThemePointer(const void *ptr)
{
  const HANDLE *p = (const HANDLE *)ptr;
  if (p > s_icon_slots && p < s_icon_slots + NJD_ICON_COUNT) return (int)(p - s_icon_slots);
  return NJD_ICON_NONE;
}

// ---------------------------------------------------------------------------
// Fader scale: NINJAM's classic cube-root curve (ninjam/njmisc.cpp), on a 0..1000 range
// ---------------------------------------------------------------------------
double Host_DB2Slider(double x)
{
  double d = pow(2110.54 * fabs(x), 1.0/3.0);
  if (x < 0.0) d = -d;
  d = (d + 63.0) * 10.0;
  if (d < 0.0) d = 0.0;
  else if (d > 1000.0) d = 1000.0;
  return d;
}

double Host_Slider2DB(double y)
{
  return pow(y * 0.1 - 63.0, 3.0) * (1.0/2110.54);
}

// ---------------------------------------------------------------------------
// Misc REAPER services
// ---------------------------------------------------------------------------
static HWND host_GetMainHwnd()
{
  // ReaNINJAM forwards WM_CTLCOLOR*/WM_DRAWITEM to REAPER's main window for theming.
  // There is no such window here; NULL makes those messages fall back to defaults.
  return NULL;
}

static const char *host_get_ini_file()
{
  // ReaNINJAM keeps its settings in reaninjam.ini next to this file
  // (unless a reaninjam.ini exists next to the executable: "portable" mode).
  static char buf[4096];
  if (!buf[0])
    snprintf(buf, sizeof(buf), "%s%c%s.ini", Host_GetConfigDir(),
#ifdef _WIN32
      '\\',
#else
      '/',
#endif
      "ninjamdesktop");
  return buf;
}

static void host_GetProjectPath(char *buf, int bufsz)
{
  // ReaNINJAM appends "/NINJAMsessions" to this
  lstrcpyn_safe(buf, Host_GetDocumentsDir(), bufsz);
}

// localization: pass strings through unchanged
static const char *host_localizeFunc(const char *str, const char *subctx, int flags) { return str; }
static void host_localizeMenu(const char *rescat, HMENU hMenu, LPCSTR lpMenuName) { }
static DLGPROC host_localizePrepareDialog(const char *rescat, HINSTANCE hInstance, const char *lpTemplate,
                                          DLGPROC dlgProc, LPARAM lParam, void **ptrs, int nptrs) { return NULL; }
static void host_localizeInitializeDialog(HWND hwnd, const char *d) { }

// host_vorbis.cpp
void *Host_CreateVorbisEncoder(int srate, int nch, int serno, float qv, int cbr, int minbr, int maxbr);
void *Host_CreateVorbisDecoder();

void Host_InitReaperAPI()
{
#ifdef _WIN32
  g_main_thread = GetCurrentThreadId();
#endif

  importedLocalizeFunc = host_localizeFunc;
  importedLocalizeMenu = host_localizeMenu;
  importedLocalizePrepareDialog = host_localizePrepareDialog;
  importedLocalizeInitializeDialog = host_localizeInitializeDialog;

  GetMainHwnd = host_GetMainHwnd;
  GetIconThemePointer = host_GetIconThemePointer;
  get_ini_file = host_get_ini_file;
  GetProjectPath = host_GetProjectPath;

  CreateVorbisEncoder = Host_CreateVorbisEncoder;
  CreateVorbisDecoder = Host_CreateVorbisDecoder;

  DB2SLIDER = Host_DB2Slider;
  SLIDER2DB = Host_Slider2DB;

  InitializeCoolSB = NJD_InitializeCoolSB;
  UninitializeCoolSB = NJD_UninitializeCoolSB;
  CoolSB_SetScrollInfo = NJD_CoolSB_SetScrollInfo;
  CoolSB_GetScrollInfo = NJD_CoolSB_GetScrollInfo;
  CoolSB_SetScrollPos = NJD_CoolSB_SetScrollPos;
  CoolSB_SetScrollRange = NJD_CoolSB_SetScrollRange;
  CoolSB_SetMinThumbSize = NJD_CoolSB_SetMinThumbSize;
}
