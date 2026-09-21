/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Window icon on Linux. SWELL only looks for "Resources/main.png" next to the executable,
// which doesn't work for an installed binary, so the icon is compiled in (generated from
// packaging/linux/ninjamdesktop-256.png) and set on the window via GDK (_NET_WM_ICON).
// Windows gets its icon from host.rc; macOS from the bundle's .icns.

#include "host.h"

#if !defined(_WIN32) && !defined(__APPLE__)

#include "WDL/swell/swell-internal.h" // HWND__::m_oswindow
#include <gdk-pixbuf/gdk-pixbuf.h>
#include "app_icon_png.h"             // generated: njd_app_icon_png[]

static GList *icon_list()
{
  static GList *s_list;
  static bool s_tried;
  if (!s_tried)
  {
    s_tried = true;
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    if (gdk_pixbuf_loader_write(loader, njd_app_icon_png, sizeof(njd_app_icon_png), NULL) &&
        gdk_pixbuf_loader_close(loader, NULL))
    {
      GdkPixbuf *pb = gdk_pixbuf_loader_get_pixbuf(loader);
      if (pb)
      {
        // GDK stops at the first icon that would push the property past the X server's
        // maximum request size (256x256 alone exceeds it on XWayland), so stay at <=128
        static const int sizes[] = { 16, 24, 32, 48, 64, 128 };
        for (size_t i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++)
        {
          GdkPixbuf *s = gdk_pixbuf_scale_simple(pb, sizes[i], sizes[i], GDK_INTERP_HYPER);
          if (s) s_list = g_list_append(s_list, s);
        }
      }
    }
    else gdk_pixbuf_loader_close(loader, NULL);
    g_object_unref(loader);
  }
  return s_list;
}

bool Host_SetWindowIcon(HWND hwnd)
{
  GList *list = icon_list();
  if (!list || !hwnd || !hwnd->m_oswindow) return false;
  gdk_window_set_icon_list(hwnd->m_oswindow, list);
  return true;
}

#else

bool Host_SetWindowIcon(HWND hwnd) { return true; }

#endif
