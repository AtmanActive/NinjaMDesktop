/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Windows light/dark theme (used by host_theme.cpp).
//
// Win32 has no public switch for dark controls. Like other Win32 programs with a dark mode
// (Notepad++, for example), this combines:
//  - WM_CTLCOLOR* replies for dialog backgrounds, labels, edit fields and lists. ReaNINJAM's
//    dialogs send these to REAPER's main window, which is how REAPER themes them;
//  - the dark visual styles Windows uses for its own UI ("DarkMode_Explorer", "DarkMode_CFD")
//    for buttons, scrollbars, combo boxes and edit borders;
//  - uxtheme's unnamed exports (Windows 10 1809 and later) for popup menus;
//  - DWMWA_USE_IMMERSIVE_DARK_MODE for title bars;
//  - custom painting where Windows ignores colors: check box labels, the menu bar, the
//    colors of rich edit (chat) and list view controls.
//
// Dialogs are found with a CBT hook on the UI thread. Only dialogs whose dialog procedure is in
// this program are themed, so message boxes and system dialogs keep the standard look.

#include <windows.h>
#include <commctrl.h>
#include <richedit.h>
#include <uxtheme.h>
#include <vssym32.h>
#include <dwmapi.h>
#include <string.h>

#include "host.h"

#ifndef BST_HOT
#define BST_HOT 0x0200
#endif

#define HEXRGB(x) RGB(((x) >> 16) & 0xff, ((x) >> 8) & 0xff, (x) & 0xff)

// the colors of the Linux dark theme (dark.colortheme in host_theme.cpp)
static const COLORREF C_FACE = HEXRGB(0x2d3035);
static const COLORREF C_SHADOW = HEXRGB(0x1c1e21);
static const COLORREF C_HILIGHT = HEXRGB(0x484c53);
static const COLORREF C_DKSHADOW = HEXRGB(0x141517);
static const COLORREF C_LIGHT = HEXRGB(0x3a3e45);
static const COLORREF C_TEXT = HEXRGB(0xe2e2e2);
static const COLORREF C_GRAYTEXT = HEXRGB(0x7c8087);
static const COLORREF C_WINDOW = HEXRGB(0x1d1f23);
static const COLORREF C_SCROLLBAR = HEXRGB(0x25272b);
static const COLORREF C_SELECTION = HEXRGB(0x3a6db3);
static const COLORREF C_SELTEXT = HEXRGB(0xffffff);
static const COLORREF C_MENUBAR = HEXRGB(0x24272b);
static const COLORREF C_MENUBAR_DISABLED = HEXRGB(0x6c7078);

static bool s_dark;
static HBRUSH s_br_face, s_br_window, s_br_menubar, s_br_menuhot, s_br_menusel;
static HHOOK s_hook;
static HTHEME s_menu_theme;

// uxtheme exports without names; signatures as used by Windows' own dark mode code
enum { APPMODE_DEFAULT = 0, APPMODE_ALLOWDARK, APPMODE_FORCEDARK, APPMODE_FORCELIGHT };
static int (WINAPI *s_SetPreferredAppMode)(int mode);                // #135, Windows 10 1903+
static bool (WINAPI *s_AllowDarkModeForApp)(bool allow);             // #135, Windows 10 1809
static bool (WINAPI *s_AllowDarkModeForWindow)(HWND hwnd, bool allow); // #133
static void (WINAPI *s_FlushMenuThemes)();                           // #136
static void (WINAPI *s_RefreshImmersiveColorPolicyState)();          // #104

#define SUBCLASS_ID 0x4e4a

// ---------------------------------------------------------------------------
// menu bar painting: undocumented messages the themed menu bar sends to its window
// ---------------------------------------------------------------------------
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092

struct UAHMENU { HMENU hmenu; HDC hdc; DWORD dwFlags; };
union UAHMENUITEMMETRICS
{
  struct { DWORD cx, cy; } rgsizeBar[2];
  struct { DWORD cx, cy; } rgsizePopup[4];
};
struct UAHMENUPOPUPMETRICS { DWORD rgcx[4]; DWORD fUpdateMaxWidths : 2; };
struct UAHMENUITEM { int iPosition; UAHMENUITEMMETRICS umim; UAHMENUPOPUPMETRICS umpm; };
struct UAHDRAWMENUITEM { DRAWITEMSTRUCT dis; UAHMENU um; UAHMENUITEM umi; };

static bool draw_menubar(HWND hwnd, const UAHMENU *um)
{
  MENUBARINFO mbi = { sizeof(mbi), };
  if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) return false;
  RECT wr, r = mbi.rcBar;
  GetWindowRect(hwnd, &wr);
  OffsetRect(&r, -wr.left, -wr.top);
  FillRect(um->hdc, &r, s_br_menubar);
  return true;
}

static bool draw_menubar_item(HWND hwnd, const UAHDRAWMENUITEM *di)
{
  wchar_t text[256] = { 0, };
  MENUITEMINFOW mii = { sizeof(mii), MIIM_STRING, };
  mii.dwTypeData = text;
  mii.cch = (UINT)(sizeof(text) / sizeof(text[0]) - 1);
  if (!GetMenuItemInfoW(di->um.hmenu, di->umi.iPosition, TRUE, &mii)) return false;

  const UINT st = di->dis.itemState;
  HBRUSH bg = s_br_menubar;
  COLORREF fg = C_TEXT;
  if (st & ODS_HOTLIGHT) bg = s_br_menuhot;
  if (st & ODS_SELECTED) { bg = s_br_menusel; fg = C_SELTEXT; }
  if (st & (ODS_GRAYED | ODS_DISABLED)) fg = C_MENUBAR_DISABLED;
  DWORD fmt = DT_CENTER | DT_SINGLELINE | DT_VCENTER;
  if (st & ODS_NOACCEL) fmt |= DT_HIDEPREFIX;

  RECT r = di->dis.rcItem;
  FillRect(di->um.hdc, &r, bg);
  if (!s_menu_theme) s_menu_theme = OpenThemeData(hwnd, L"Menu");
  if (s_menu_theme)
  {
    DTTOPTS o = { sizeof(o), DTT_TEXTCOLOR, };
    o.crText = fg;
    DrawThemeTextEx(s_menu_theme, di->um.hdc, MENU_BARITEM, MBI_NORMAL, text, -1, fmt, &r, &o);
  }
  else
  {
    SetBkMode(di->um.hdc, TRANSPARENT);
    SetTextColor(di->um.hdc, fg);
    DrawTextW(di->um.hdc, text, -1, &r, fmt);
  }
  return true;
}

// the default non-client painting leaves a light line between the menu bar and the client area
static void draw_menubar_bottom_line(HWND hwnd)
{
  MENUBARINFO mbi = { sizeof(mbi), };
  if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi)) return;
  RECT cr, wr;
  GetClientRect(hwnd, &cr);
  MapWindowPoints(hwnd, NULL, (POINT *)&cr, 2);
  GetWindowRect(hwnd, &wr);
  OffsetRect(&cr, -wr.left, -wr.top);
  RECT line = cr;
  line.bottom = line.top;
  line.top--;
  HDC dc = GetWindowDC(hwnd);
  if (dc)
  {
    FillRect(dc, &line, s_br_menubar);
    ReleaseDC(hwnd, dc);
  }
}

// ---------------------------------------------------------------------------
// controls
// ---------------------------------------------------------------------------
static void set_theme(HWND h, const wchar_t *dark_name)
{
  if (s_AllowDarkModeForWindow) s_AllowDarkModeForWindow(h, s_dark);
  SetWindowTheme(h, s_dark ? dark_name : NULL, NULL); // NULL: back to the default theme
}

static void set_titlebar(HWND h)
{
  if (s_AllowDarkModeForWindow) s_AllowDarkModeForWindow(h, s_dark);
  BOOL v = s_dark;
  // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 since Windows 10 2004, 19 before that
  if (FAILED(DwmSetWindowAttribute(h, 20, &v, sizeof(v)))) DwmSetWindowAttribute(h, 19, &v, sizeof(v));
}

static bool class_is(const char *cls, const char *name) { return !lstrcmpiA(cls, name); }

// Themed check boxes and radio buttons draw their label in the theme's (black) text color,
// whatever WM_CTLCOLORSTATIC says, so in dark mode we paint them: the dark theme's glyph plus
// our own label.
static void paint_checkbox(HWND hwnd, HDC target)
{
  RECT rc;
  GetClientRect(hwnd, &rc);
  HDC dc = target;
  HPAINTBUFFER pb = BeginBufferedPaint(target, &rc, BPBF_COMPATIBLEBITMAP, NULL, &dc);
  if (!pb) dc = target;

  FillRect(dc, &rc, s_br_face);

  const LONG style = GetWindowLong(hwnd, GWL_STYLE);
  const LONG type = style & BS_TYPEMASK;
  const bool radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
  const LRESULT check = SendMessage(hwnd, BM_GETCHECK, 0, 0);
  const LRESULT bst = SendMessage(hwnd, BM_GETSTATE, 0, 0);
  const bool enabled = IsWindowEnabled(hwnd) != FALSE;

  // CBS_UNCHECKEDNORMAL..CBS_MIXEDDISABLED and RBS_* share this layout: 4 states per check state
  const int base = !enabled ? 4 : (bst & BST_PUSHED) ? 3 : (bst & BST_HOT) ? 2 : 1;
  const int part = radio ? BP_RADIOBUTTON : BP_CHECKBOX;
  const int state = base + (check == BST_CHECKED ? 4 : (check == BST_INDETERMINATE && !radio) ? 8 : 0);

  HFONT font = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
  HGDIOBJ oldfont = font ? SelectObject(dc, font) : NULL;

  HTHEME th = OpenThemeData(hwnd, L"Button");
  SIZE box = { 13, 13 };
  if (th) GetThemePartSize(th, dc, part, state, NULL, TS_DRAW, &box);
  const bool box_right = (style & BS_LEFTTEXT) != 0;
  RECT rb;
  rb.top = rc.top + (rc.bottom - rc.top - box.cy) / 2;
  rb.bottom = rb.top + box.cy;
  rb.left = box_right ? rc.right - box.cx : rc.left;
  rb.right = rb.left + box.cx;
  if (th) DrawThemeBackground(th, dc, part, state, &rb, NULL);
  else DrawFrameControl(dc, &rb, DFC_BUTTON, (radio ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK) |
                        (check == BST_CHECKED ? DFCS_CHECKED : 0) | (enabled ? 0 : DFCS_INACTIVE));

  char text[512];
  if (GetWindowText(hwnd, text, sizeof(text)) > 0)
  {
    const int gap = MulDiv(box.cx, 3, 13) + 1;
    RECT rt = rc;
    if (box_right) rt.right = rb.left - gap;
    else rt.left = rb.right + gap;

    const LRESULT ui = SendMessage(hwnd, WM_QUERYUISTATE, 0, 0);
    UINT fmt = (style & BS_MULTILINE) ? DT_WORDBREAK : (DT_SINGLELINE | DT_VCENTER);
    if (box_right) fmt |= DT_RIGHT;
    if (ui & UISF_HIDEACCEL) fmt |= DT_HIDEPREFIX;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, enabled ? C_TEXT : C_GRAYTEXT);
    DrawText(dc, text, -1, &rt, fmt);

    if (GetFocus() == hwnd && !(ui & UISF_HIDEFOCUS))
    {
      RECT fr = rt;
      DrawText(dc, text, -1, &fr, (fmt & ~(DT_VCENTER | DT_RIGHT)) | DT_CALCRECT);
      const int w = fr.right - fr.left, h = fr.bottom - fr.top;
      fr.left = box_right ? rt.right - w : rt.left;
      fr.right = fr.left + w;
      if (fmt & DT_VCENTER) fr.top = rt.top + (rt.bottom - rt.top - h) / 2;
      fr.bottom = fr.top + h;
      InflateRect(&fr, 1, 0);
      SetTextColor(dc, C_TEXT);
      SetBkColor(dc, C_FACE);
      DrawFocusRect(dc, &fr);
    }
  }

  if (th) CloseThemeData(th);
  if (oldfont) SelectObject(dc, oldfont);
  if (pb) EndBufferedPaint(pb, TRUE);
}

static LRESULT CALLBACK checkbox_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ref)
{
  if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, checkbox_proc, id);
  else if (s_dark) switch (msg)
  {
    case WM_ERASEBKGND:
    return 1;
    case WM_PAINT:
    case WM_PRINTCLIENT:
      if (msg == WM_PRINTCLIENT || wParam) paint_checkbox(hwnd, (HDC)wParam);
      else
      {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (dc) paint_checkbox(hwnd, dc);
        EndPaint(hwnd, &ps);
      }
    return 0;
    case WM_UPDATEUISTATE:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SETTEXT:
    case BM_SETCHECK:
    case BM_SETSTATE:
      {
        // some of these draw directly; repaint in our colors afterwards
        const LRESULT ret = DefSubclassProc(hwnd, msg, wParam, lParam);
        InvalidateRect(hwnd, NULL, FALSE);
        return ret;
      }
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// ReaNINJAM's divider lines use GetSysColor() directly
static LRESULT CALLBACK divider_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ref)
{
  if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, divider_proc, id);
  else if (msg == WM_PAINT && s_dark)
  {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    if (dc)
    {
      RECT r;
      GetClientRect(hwnd, &r);
      r.bottom = r.top + 1;
      HBRUSH br = CreateSolidBrush(C_SHADOW);
      FillRect(dc, &r, br);
      DeleteObject(br);
      OffsetRect(&r, 0, 1);
      br = CreateSolidBrush(C_HILIGHT);
      FillRect(dc, &r, br);
      DeleteObject(br);
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// list view column headers: the dark header theme keeps dark text
static LRESULT CALLBACK listview_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ref)
{
  if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, listview_proc, id);
  else if (msg == WM_NOTIFY && s_dark && lParam)
  {
    NMHDR *nm = (NMHDR *)lParam;
    if (nm->code == NM_CUSTOMDRAW && nm->hwndFrom == ListView_GetHeader(hwnd))
    {
      NMCUSTOMDRAW *cd = (NMCUSTOMDRAW *)lParam;
      if (cd->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
      if (cd->dwDrawStage == CDDS_ITEMPREPAINT)
      {
        SetTextColor(cd->hdc, C_TEXT);
        return CDRF_DODEFAULT;
      }
    }
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static void theme_listview(HWND h)
{
  set_theme(h, L"DarkMode_Explorer");
  HWND hdr = ListView_GetHeader(h);
  if (hdr) set_theme(hdr, L"DarkMode_ItemsView");
  const COLORREF bg = s_dark ? C_WINDOW : GetSysColor(COLOR_WINDOW);
  ListView_SetBkColor(h, bg);
  ListView_SetTextBkColor(h, bg);
  ListView_SetTextColor(h, s_dark ? C_TEXT : GetSysColor(COLOR_WINDOWTEXT));
  SetWindowSubclass(h, listview_proc, SUBCLASS_ID, 0);
}

// chat and license text ("RichEditChild" is ReaNINJAM's name for RichEdit20A)
static void theme_richedit(HWND h)
{
  set_theme(h, L"DarkMode_Explorer"); // scrollbars
  SendMessage(h, EM_SETBKGNDCOLOR, s_dark ? 0 : 1, s_dark ? C_WINDOW : 0);
  CHARFORMATA cf;
  memset(&cf, 0, sizeof(cf));
  cf.cbSize = sizeof(cf);
  cf.dwMask = CFM_COLOR;
  cf.dwEffects = s_dark ? 0 : CFE_AUTOCOLOR;
  cf.crTextColor = C_TEXT;
  SendMessage(h, EM_SETCHARFORMAT, SCF_ALL, (LPARAM)&cf);
  SendMessage(h, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
}

static void theme_control(HWND h)
{
  char cls[64];
  if (!GetClassName(h, cls, sizeof(cls))) return;
  const LONG style = GetWindowLong(h, GWL_STYLE);

  if (class_is(cls, "Button"))
  {
    const LONG type = style & BS_TYPEMASK;
    if (type == BS_OWNERDRAW) return;
    set_theme(h, L"DarkMode_Explorer");
    if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_3STATE || type == BS_AUTO3STATE ||
        type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON)
      SetWindowSubclass(h, checkbox_proc, SUBCLASS_ID, 0);
  }
  else if (class_is(cls, "Edit"))
    set_theme(h, (style & ES_MULTILINE) ? L"DarkMode_Explorer" : L"DarkMode_CFD");
  else if (class_is(cls, "ComboBox"))
  {
    set_theme(h, L"DarkMode_CFD");
    COMBOBOXINFO ci = { sizeof(ci), };
    if (GetComboBoxInfo(h, &ci) && ci.hwndList) set_theme(ci.hwndList, L"DarkMode_Explorer");
  }
  else if (class_is(cls, "ListBox") || class_is(cls, "ScrollBar"))
    set_theme(h, L"DarkMode_Explorer");
  else if (class_is(cls, WC_LISTVIEWA))
    theme_listview(h);
  else if (class_is(cls, "RichEditChild") || !_strnicmp(cls, "RichEdit", 8))
    theme_richedit(h);
  else if (class_is(cls, "ninjamdivider"))
    SetWindowSubclass(h, divider_proc, SUBCLASS_ID, 0);
  else
    return;
  InvalidateRect(h, NULL, TRUE);
}

static BOOL CALLBACK theme_child_cb(HWND h, LPARAM lParam)
{
  theme_control(h);
  return TRUE;
}

// the dialog's own controls and those of dialogs inside it
static void theme_dialog(HWND dlg)
{
  if (!(GetWindowLong(dlg, GWL_STYLE) & WS_CHILD)) set_titlebar(dlg);
  EnumChildWindows(dlg, theme_child_cb, 0);
}

// ---------------------------------------------------------------------------
// dialogs
// ---------------------------------------------------------------------------
static bool proc_is_ours(LONG_PTR proc)
{
  HMODULE mod = NULL;
  return proc && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                    (LPCSTR)proc, &mod) && mod == GetModuleHandle(NULL);
}

// ReaNINJAM's and our dialogs, as opposed to message boxes and dialogs of system DLLs.
// Asking with the "wrong" character set returns a handle instead of the address, so try both.
static bool is_our_dialog(HWND hwnd)
{
  return proc_is_ours(GetWindowLongPtrA(hwnd, DWLP_DLGPROC)) || proc_is_ours(GetWindowLongPtrW(hwnd, DWLP_DLGPROC));
}

// ref: 0 until WM_INITDIALOG, then 1 if the dialog is ours (others are unsubclassed)
static LRESULT CALLBACK dialog_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR id, DWORD_PTR ref)
{
  switch (msg)
  {
    case WM_INITDIALOG:
      {
        const LRESULT ret = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (!is_our_dialog(hwnd)) RemoveWindowSubclass(hwnd, dialog_proc, id);
        else
        {
          SetWindowSubclass(hwnd, dialog_proc, id, 1);
          if (s_dark) theme_dialog(hwnd);
        }
        return ret;
      }
    case WM_NCDESTROY:
      RemoveWindowSubclass(hwnd, dialog_proc, id);
    break;
    case WM_THEMECHANGED:
      if (s_menu_theme && GetMenu(hwnd))
      {
        CloseThemeData(s_menu_theme);
        s_menu_theme = NULL;
      }
    break;
  }
  if (!ref || !s_dark) return DefSubclassProc(hwnd, msg, wParam, lParam);

  switch (msg)
  {
    case WM_CTLCOLORDLG:
    return (LRESULT)s_br_face;
    case WM_CTLCOLORSTATIC: // labels, read-only edit fields
    case WM_CTLCOLORBTN:
      SetTextColor((HDC)wParam, IsWindowEnabled((HWND)lParam) ? C_TEXT : C_GRAYTEXT);
      SetBkColor((HDC)wParam, C_FACE);
    return (LRESULT)s_br_face;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
      SetTextColor((HDC)wParam, C_TEXT);
      SetBkColor((HDC)wParam, C_WINDOW);
    return (LRESULT)s_br_window;
    case WM_UAHDRAWMENU:
      if (lParam && draw_menubar(hwnd, (const UAHMENU *)lParam)) return TRUE;
    break;
    case WM_UAHDRAWMENUITEM:
      if (lParam && draw_menubar_item(hwnd, (const UAHDRAWMENUITEM *)lParam)) return TRUE;
    break;
    case WM_NCPAINT:
    case WM_NCACTIVATE:
      {
        const LRESULT ret = DefSubclassProc(hwnd, msg, wParam, lParam);
        if (GetMenu(hwnd)) draw_menubar_bottom_line(hwnd);
        return ret;
      }
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK cbt_proc(int code, WPARAM wParam, LPARAM lParam)
{
  if (code == HCBT_CREATEWND)
  {
    char cls[16];
    if (GetClassName((HWND)wParam, cls, sizeof(cls)) && !strcmp(cls, "#32770"))
      SetWindowSubclass((HWND)wParam, dialog_proc, SUBCLASS_ID, 0);
  }
  return CallNextHookEx(s_hook, code, wParam, lParam);
}

static BOOL CALLBACK retheme_top_cb(HWND h, LPARAM lParam)
{
  DWORD_PTR ours = 0;
  if (!GetWindowSubclass(h, dialog_proc, SUBCLASS_ID, &ours) || !ours) return TRUE;

  theme_dialog(h);
  if (GetMenu(h)) DrawMenuBar(h);
  if (IsWindowVisible(h) && !(GetWindowLong(h, GWL_STYLE) & WS_CHILD))
  {
    // Windows 10 only repaints the title bar in the new color on activation changes
    const BOOL active = GetActiveWindow() == h;
    SendMessage(h, WM_NCACTIVATE, !active, 0);
    SendMessage(h, WM_NCACTIVATE, active, 0);
  }
  RedrawWindow(h, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
  return TRUE;
}

static void set_app_mode()
{
  // popup menus follow the application's preferred mode
  if (s_SetPreferredAppMode) s_SetPreferredAppMode(s_dark ? APPMODE_FORCEDARK : APPMODE_DEFAULT);
  else if (s_AllowDarkModeForApp) s_AllowDarkModeForApp(s_dark);
  if (s_RefreshImmersiveColorPolicyState) s_RefreshImmersiveColorPolicyState();
  if (s_FlushMenuThemes) s_FlushMenuThemes();
}

static DWORD windows_build()
{
  typedef LONG (WINAPI *RtlGetVersionFunc)(RTL_OSVERSIONINFOW *);
  RtlGetVersionFunc f = (RtlGetVersionFunc)(void *)GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlGetVersion");
  RTL_OSVERSIONINFOW vi;
  memset(&vi, 0, sizeof(vi));
  vi.dwOSVersionInfoSize = sizeof(vi);
  if (!f || f(&vi) != 0 || vi.dwMajorVersion < 10) return 0;
  return vi.dwBuildNumber;
}

template<class T> static void get_ordinal(HMODULE mod, int ord, T *fp)
{
  *fp = (T)(void *)GetProcAddress(mod, MAKEINTRESOURCEA(ord));
}

// ---------------------------------------------------------------------------
// interface for host_theme.cpp
// ---------------------------------------------------------------------------
bool WinTheme_Init()
{
  const DWORD build = windows_build();
  if (build < 17763) return false; // dark mode APIs appeared in Windows 10 1809

  HMODULE ux = GetModuleHandleA("uxtheme.dll");
  if (!ux) ux = LoadLibraryA("uxtheme.dll");
  if (!ux) return false;
  get_ordinal(ux, 133, &s_AllowDarkModeForWindow);
  if (build >= 18362) get_ordinal(ux, 135, &s_SetPreferredAppMode);
  else get_ordinal(ux, 135, &s_AllowDarkModeForApp);
  get_ordinal(ux, 136, &s_FlushMenuThemes);
  get_ordinal(ux, 104, &s_RefreshImmersiveColorPolicyState);
  if (!s_SetPreferredAppMode && !s_AllowDarkModeForApp) return false;

  BufferedPaintInit();
  s_br_face = CreateSolidBrush(C_FACE);
  s_br_window = CreateSolidBrush(C_WINDOW);
  s_br_menubar = CreateSolidBrush(C_MENUBAR);
  s_br_menuhot = CreateSolidBrush(C_LIGHT);
  s_br_menusel = CreateSolidBrush(C_SELECTION);

  s_hook = SetWindowsHookEx(WH_CBT, cbt_proc, NULL, GetCurrentThreadId());
  return s_hook != NULL;
}

bool WinTheme_SystemPrefersDark()
{
  DWORD v = 1, sz = sizeof(v);
  if (RegGetValueA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   "AppsUseLightTheme", RRF_RT_REG_DWORD, NULL, &v, &sz) != ERROR_SUCCESS) return false;
  return v == 0;
}

bool WinTheme_HighContrast()
{
  HIGHCONTRASTA hc = { sizeof(hc), };
  return SystemParametersInfoA(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
}

void WinTheme_Apply(bool dark)
{
  s_dark = dark;
  set_app_mode();
  EnumThreadWindows(GetCurrentThreadId(), retheme_top_cb, 0);
}

// our custom controls (host_controls.cpp) and coolscroll ask for colors through this
COLORREF Host_GetSysColor(int idx)
{
  if (s_dark) switch (idx)
  {
    case COLOR_3DFACE: return C_FACE; // == COLOR_BTNFACE
    case COLOR_3DSHADOW: return C_SHADOW;
    case COLOR_3DHILIGHT: return C_HILIGHT;
    case COLOR_3DDKSHADOW: return C_DKSHADOW;
    case COLOR_3DLIGHT: return C_LIGHT;
    case COLOR_BTNTEXT:
    case COLOR_WINDOWTEXT:
    case COLOR_MENUTEXT:
    case COLOR_CAPTIONTEXT: return C_TEXT;
    case COLOR_GRAYTEXT: return C_GRAYTEXT;
    case COLOR_WINDOW: return C_WINDOW;
    case COLOR_SCROLLBAR: return C_SCROLLBAR;
    case COLOR_HIGHLIGHT: return C_SELECTION;
    case COLOR_HIGHLIGHTTEXT: return C_SELTEXT;
  }
  return GetSysColor(idx);
}
