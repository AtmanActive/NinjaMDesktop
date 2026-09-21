/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Appearance: light/dark theme and zoom.
//
// Linux: SWELL draws every control itself using a color table that can be loaded from a
// ".colortheme" file (the same mechanism REAPER's Linux themes use). We keep one file per
// mode in the settings folder, so users can tweak the colors, and pick one based on the
// "Theme" setting or, in "Follow system" mode, the freedesktop appearance portal.
//
// Zoom (Linux) uses SWELL's UI scale, which sizes dialogs, fonts, menus and scrollbars
// when windows are created, so a new zoom level applies after a restart.
//
// Windows/macOS: not implemented yet; the menus are not shown there.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "host.h"
#include "WDL/wdlstring.h"
#include "WDL/wdlcstring.h"

#if !defined(_WIN32) && !defined(__APPLE__)
#define NJD_HAVE_THEMES
#include <gio/gio.h>
#include "WDL/swell/swell-internal.h" // g_swell_ctheme, g_swell_ui_scale
#endif

#define THEME_SEC "ninjamdesktop"
#define THEME_POLL_TIMER 0x4e4a
#define THEME_POLL_MS 3000

static int s_mode = THEME_SYSTEM;
static int s_zoom = 0; // percent, 0 = automatic (follow the display's scale factor)
static int s_applied = -1; // 0 light, 1 dark
static HWND s_hwnd;

// the settings file ReaNINJAM will use (same rule as InitializeInstance() in winclient.cpp)
static const char *theme_ini_path()
{
  static WDL_FastString s_path;
  if (!s_path.GetLength())
  {
    char buf[4096];
    GetModuleFileName(NULL, buf, sizeof(buf));
    WDL_remove_filepart(buf);
    s_path.Set(buf);
    s_path.Append("/reaninjam.ini");
    FILE *fp = fopen(s_path.Get(), "r+");
    if (fp) fclose(fp);
    else
    {
      s_path.Set(Host_GetConfigDir());
      s_path.Append("/reaninjam.ini");
    }
  }
  return s_path.Get();
}

#ifdef NJD_HAVE_THEMES

void swell_load_color_theme(const char *fn); // swell-gdi-generic.cpp

static const char s_dark_theme[] =
  "; NinjaMDesktop dark theme (SWELL color theme format: key #rrggbb).\n"
  "; Edit to taste, then re-select File > Theme > Dark to reload.\n"
  "_3dface #2d3035\n_3dshadow #1c1e21\n_3dhilight #484c53\n_3ddkshadow #141517\n"
  "button_bg #3a3e45\nbutton_text #e2e2e2\nbutton_text_disabled #7c8087\nbutton_shadow #1c1e21\nbutton_hilight #555a62\n"
  "checkbox_text #e2e2e2\ncheckbox_text_disabled #7c8087\ncheckbox_fg #e2e2e2\ncheckbox_inter #7c8087\ncheckbox_bg #1d1f23\n"
  "scrollbar #121315\nscrollbar_fg #5c6169\nscrollbar_bg #25272b\n"
  "edit_cursor #4aa3ff\nedit_bg #1d1f23\nedit_bg_disabled #2a2d31\nedit_text #e6e6e6\nedit_text_disabled #7c8087\n"
  "edit_bg_sel #2f5f9f\nedit_text_sel #ffffff\nedit_hilight #484c53\nedit_shadow #141517\n"
  "info_bk #3b3a2f\ninfo_text #eeeeee\n"
  "menu_bg #2a2d32\nmenu_shadow #141517\nmenu_hilight #484c53\nmenu_text #e2e2e2\nmenu_text_disabled #6c7078\n"
  "menu_bg_sel #3a6db3\nmenu_text_sel #ffffff\nmenu_scroll #1d1f23\nmenu_scroll_arrow #9aa0a8\n"
  "menu_submenu_arrow #c8c8c8\nmenu_submenu_arrow_sel #ffffff\n"
  "menubar_bg #24272b\nmenubar_bg_inactive #24272b\nmenubar_text #e2e2e2\nmenubar_text_inactive #a0a4aa\n"
  "menubar_text_disabled #6c7078\nmenubar_bg_sel #3a6db3\nmenubar_text_sel #ffffff\n"
  "trackbar_track #1d1f23\ntrackbar_mark #7c8087\ntrackbar_knob #c8c8c8\nprogress #3a8ee6\n"
  "label_text #d8d8d8\nlabel_text_disabled #7c8087\n"
  "combo_text #e2e2e2\ncombo_text_disabled #7c8087\ncombo_bg #3a3e45\ncombo_bg2 #1d1f23\ncombo_shadow #141517\n"
  "combo_hilight #555a62\ncombo_arrow #b4b8be\ncombo_arrow_press #ffffff\n"
  "listview_bg #1d1f23\nlistview_bg_sel #3a6db3\nlistview_text #e2e2e2\nlistview_text_sel #ffffff\n"
  "listview_bg_sel_inactive #3a3e45\nlistview_text_sel_inactive #e2e2e2\nlistview_grid #2d3035\nlistview_hdr_arrow #b4b8be\n"
  "listview_shadow #141517\nlistview_hilight #484c53\nlistview_hdr_shadow #141517\nlistview_hdr_hilight #484c53\n"
  "listview_hdr_bg #2d3035\nlistview_hdr_text #e2e2e2\n"
  "treeview_text #e2e2e2\ntreeview_bg #1d1f23\ntreeview_bg_sel #3a6db3\ntreeview_text_sel #ffffff\n"
  "treeview_bg_sel_inactive #3a3e45\ntreeview_text_sel_inactive #e2e2e2\ntreeview_arrow #b4b8be\n"
  "treeview_shadow #141517\ntreeview_hilight #484c53\n"
  "tab_shadow #141517\ntab_hilight #484c53\ntab_text #e2e2e2\n"
  "focusrect #4aa3ff\ngroup_text #e2e2e2\ngroup_shadow #141517\ngroup_hilight #484c53\nfocus_hilight #3a6db3\n";

static const char s_light_theme[] =
  "; NinjaMDesktop light theme: empty means SWELL's built-in colors.\n"
  "; Add overrides (key #rrggbb, see dark.colortheme for the keys), then re-select File > Theme > Light.\n";

// load <config dir>/<name>.colortheme, creating it from the built-in contents first if needed
static void load_theme_file(const char *name, const char *contents)
{
  WDL_FastString fn(Host_GetConfigDir());
  fn.AppendFormatted(256, "/%s.colortheme", name);
  FILE *fp = fopen(fn.Get(), "r");
  if (fp) fclose(fp);
  else if ((fp = fopen(fn.Get(), "w")) != NULL)
  {
    fputs(contents, fp);
    fclose(fp);
  }
  // keys missing from the file fall back to SWELL's defaults
  swell_load_color_theme(fn.Get());

  // the file's sizes are at 100%; scale them like SWELL does for its own ui_scale setting
  if (g_swell_ui_scale != 256)
  {
    const double sc = g_swell_ui_scale * (1.0 / 256.0);
    g_swell_ctheme.default_font_size--;
#define __scale(x,c) g_swell_ctheme.x = (int) (g_swell_ctheme.x * sc + 0.5);
    SWELL_GENERIC_THEMESIZEDEFS(__scale,__scale)
#undef __scale
    g_swell_ctheme.default_font_size++;
  }
}

// freedesktop appearance portal: 0 = no preference, 1 = dark, 2 = light, -1 = unavailable
static int portal_color_scheme()
{
  static GDBusConnection *s_bus;
  static bool s_bus_failed;
  if (!s_bus && !s_bus_failed)
  {
    s_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, NULL);
    if (!s_bus) s_bus_failed = true;
  }
  if (!s_bus) return -1;

  GVariant *res = g_dbus_connection_call_sync(s_bus, "org.freedesktop.portal.Desktop",
      "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "ReadOne",
      g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
      NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
  if (!res) // older portals only have the deprecated Read(), which wraps the value in an extra variant
    res = g_dbus_connection_call_sync(s_bus, "org.freedesktop.portal.Desktop",
      "/org/freedesktop/portal/desktop", "org.freedesktop.portal.Settings", "Read",
      g_variant_new("(ss)", "org.freedesktop.appearance", "color-scheme"),
      NULL, G_DBUS_CALL_FLAGS_NONE, 500, NULL, NULL);
  if (!res) return -1;

  int ret = -1;
  GVariant *v = NULL;
  g_variant_get(res, "(v)", &v);
  while (v && g_variant_is_of_type(v, G_VARIANT_TYPE_VARIANT))
  {
    GVariant *inner = g_variant_get_variant(v);
    g_variant_unref(v);
    v = inner;
  }
  if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_UINT32)) ret = (int)g_variant_get_uint32(v);
  if (v) g_variant_unref(v);
  g_variant_unref(res);
  return ret;
}

static bool system_prefers_dark()
{
  const int scheme = portal_color_scheme();
  if (scheme == 1) return true;
  if (scheme == 2) return false;
  const char *gtk = getenv("GTK_THEME");
  return gtk && (strstr(gtk, ":dark") || strstr(gtk, "-dark") || strstr(gtk, "-Dark"));
}

static void invalidate_tree(HWND h)
{
  InvalidateRect(h, NULL, TRUE);
  for (HWND c = GetWindow(h, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT)) invalidate_tree(c);
}

static void apply(bool force)
{
  const bool dark = s_mode == THEME_DARK || (s_mode == THEME_SYSTEM && system_prefers_dark());
  if (!force && s_applied == (int)dark) return;
  s_applied = dark;
  if (dark) load_theme_file("dark", s_dark_theme);
  else load_theme_file("light", s_light_theme);
  if (s_hwnd)
  {
    invalidate_tree(s_hwnd);
    DrawMenuBar(s_hwnd);
  }
}

static void init_zoom()
{
  if (s_zoom > 0) g_swell_ui_scale = (s_zoom * 256 + 50) / 100;
  // with an explicit zoom this only tells GDK not to scale on top; otherwise it
  // picks up the display's (integer) scale factor
  swell_scaling_init(s_zoom > 0);
}

static int host_GetWindowDPIScaling(HWND hwnd) { return SWELL_GetScaling256(); }

bool Theme_Supported() { return true; }

#else

static void apply(bool force) { }
static void init_zoom() { }
bool Theme_Supported() { return false; }

#endif

void Theme_Init()
{
  s_mode = GetPrivateProfileInt(THEME_SEC, "theme", THEME_SYSTEM, theme_ini_path());
  if (s_mode < THEME_SYSTEM || s_mode > THEME_DARK) s_mode = THEME_SYSTEM;
  s_zoom = GetPrivateProfileInt(THEME_SEC, "zoom", 0, theme_ini_path());
  if (s_zoom != 0 && (s_zoom < 50 || s_zoom > 300)) s_zoom = 0;

  init_zoom();
#ifdef NJD_HAVE_THEMES
  // lets ReaNINJAM store its panel sizes independent of the zoom level, as it does in REAPER
  extern int (*GetWindowDPIScaling)(HWND hwnd);
  GetWindowDPIScaling = host_GetWindowDPIScaling;
#endif
  apply(true);
}

void Theme_AttachWindow(HWND hwnd)
{
  s_hwnd = hwnd;
#ifdef NJD_HAVE_THEMES
  SetTimer(hwnd, THEME_POLL_TIMER, THEME_POLL_MS, NULL);
#endif
}

bool Theme_OnTimer(WPARAM id)
{
  if (id != THEME_POLL_TIMER) return false;
  if (s_mode == THEME_SYSTEM) apply(false);
  return true;
}

int Theme_GetMode() { return s_mode; }

void Theme_SetMode(int mode)
{
  if (mode < THEME_SYSTEM || mode > THEME_DARK) return;
  s_mode = mode;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", mode);
  WritePrivateProfileString(THEME_SEC, "theme", buf, theme_ini_path());
  apply(true); // also reloads the .colortheme file, so edits take effect
}

int Zoom_GetPercent() { return s_zoom; }

bool Zoom_SetPercent(int pct)
{
  if (pct != 0 && (pct < 50 || pct > 300)) return false;
  const bool changed = pct != s_zoom;
  s_zoom = pct;
  char buf[32];
  snprintf(buf, sizeof(buf), "%d", pct);
  WritePrivateProfileString(THEME_SEC, "zoom", buf, theme_ini_path());
  return changed;
}
