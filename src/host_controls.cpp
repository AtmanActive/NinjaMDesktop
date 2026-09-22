/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// ReaNINJAM's dialogs use window classes that REAPER registers:
//   "REAPERhfader"   horizontal fader (trackbar messages, WM_HSCROLL notifications)
//   "REAPERhorzvu"   horizontal stereo peak meter  (WM_USER+1011, lParam = double[2] in dB)
//   "REAPERvertvu"   vertical stereo peak meter    (same)
//   "HotTrackButton" themed icon button used for mute/solo (BM_SETIMAGE with IMAGE_ICON|0x8000)
// plus "RichEditChild", which the VST wrapper maps to a SWELL edit field on Linux/macOS.
// This file implements all of them with plain Win32/SWELL drawing.

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#else
#include "WDL/swell/swell.h"
#endif
#include <math.h>
#include <string.h>
#include <stdlib.h>

#include "WDL/wdltypes.h"
#include "host.h"

#ifndef DLGC_WANTARROWS
#define DLGC_WANTARROWS 0x0001
#endif
#ifndef ES_READONLY // SWELL defines these in swell-dlggen.h
#define ES_READONLY 0x0800L
#define ES_MULTILINE 4
#endif
#ifndef GWLP_USERDATA
#define GWLP_USERDATA GWL_USERDATA
#endif
#ifndef TBM_GETRANGEMIN
#define TBM_GETRANGEMIN (WM_USER+1)
#define TBM_GETRANGEMAX (WM_USER+2)
#define TBM_SETRANGEMIN (WM_USER+7)
#define TBM_SETRANGEMAX (WM_USER+8)
#endif

#define WM_REAPER_VU_SETPEAKS (WM_USER+1011)
#define WM_REAPER_SETACCESSNAME (WM_USER+9999)
#define WM_REAPER_SETTOOLTIP (WM_USER+0x300)

// ---------------------------------------------------------------------------
// drawing helpers
// ---------------------------------------------------------------------------
static void fill_rect(HDC dc, int l, int t, int r, int b, COLORREF col)
{
  if (r <= l || b <= t) return;
  RECT rc = { l, t, r, b };
  HBRUSH br = CreateSolidBrush(col);
  FillRect(dc, &rc, br);
  DeleteObject(br);
}

static void frame_rect(HDC dc, const RECT *r, COLORREF col)
{
  fill_rect(dc, r->left, r->top, r->right, r->top+1, col);
  fill_rect(dc, r->left, r->bottom-1, r->right, r->bottom, col);
  fill_rect(dc, r->left, r->top, r->left+1, r->bottom, col);
  fill_rect(dc, r->right-1, r->top, r->right, r->bottom, col);
}

static COLORREF mix_color(COLORREF a, COLORREF b, int amt256) // amt256=0 -> a, 256 -> b
{
  const int r = GetRValue(a) + ((GetRValue(b) - GetRValue(a)) * amt256) / 256;
  const int g = GetGValue(a) + ((GetGValue(b) - GetGValue(a)) * amt256) / 256;
  const int bl = GetBValue(a) + ((GetBValue(b) - GetBValue(a)) * amt256) / 256;
  return RGB(r, g, bl);
}

template<class T> static T *get_state(HWND hwnd)
{
  T *s = (T *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  if (!s)
  {
    s = new T;
    SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)s);
  }
  return s;
}

template<class T> static void free_state(HWND hwnd)
{
  T *s = (T *)GetWindowLongPtr(hwnd, GWLP_USERDATA);
  SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
  delete s;
}

static bool fine_adjust_key() { return (GetAsyncKeyState(VK_CONTROL) & 0x8000) || (GetAsyncKeyState(VK_SHIFT) & 0x8000); }

// ---------------------------------------------------------------------------
// REAPERhfader
// ---------------------------------------------------------------------------
struct FaderState
{
  int minv = 0, maxv = 1000; // default range matches Host_DB2Slider()
  int pos = 0;
  int tic = -1;              // -1: 0 dB of the default volume scale
  bool range_set = false;
  bool dragging = false;
  int last_x = 0;
  double drag_pos = 0.0;
};

static int fader_default_pos(const FaderState *s)
{
  if (s->tic >= 0) return s->tic;
  if (!s->range_set) return (int)(Host_DB2Slider(0.0) + 0.5);
  return (s->minv + s->maxv) / 2;
}

static int fader_thumb_w(const RECT *r)
{
  int tw = (r->bottom - r->top) * 5 / 8;
  if (tw < 7) tw = 7;
  if (tw > 16) tw = 16;
  return tw;
}

static void fader_set(HWND hwnd, FaderState *s, int pos, int code)
{
  if (pos < s->minv) pos = s->minv;
  if (pos > s->maxv) pos = s->maxv;
  if (pos == s->pos && code == SB_THUMBTRACK) return;
  s->pos = pos;
  InvalidateRect(hwnd, NULL, FALSE);
  SendMessage(GetParent(hwnd), WM_HSCROLL, MAKEWPARAM(code, pos), (LPARAM)hwnd);
}

static void fader_paint(HWND hwnd, FaderState *s)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  if (!dc) return;
  RECT r;
  GetClientRect(hwnd, &r);
  const COLORREF face = Host_GetSysColor(COLOR_3DFACE);
  const COLORREF shadow = Host_GetSysColor(COLOR_3DSHADOW);
  const COLORREF hilite = Host_GetSysColor(COLOR_3DHILIGHT);
  const COLORREF dark = Host_GetSysColor(COLOR_3DDKSHADOW);
  fill_rect(dc, r.left, r.top, r.right, r.bottom, face);

  const int tw = fader_thumb_w(&r);
  const int usable = wdl_max(r.right - r.left - tw, 1);
  const int range = wdl_max(s->maxv - s->minv, 1);
  const int cy = (r.top + r.bottom) / 2;

  // groove
  fill_rect(dc, r.left + tw/2, cy - 1, r.right - tw/2, cy, shadow);
  fill_rect(dc, r.left + tw/2, cy, r.right - tw/2, cy + 1, hilite);

  // default position tick
  {
    const int tp = fader_default_pos(s);
    if (tp >= s->minv && tp <= s->maxv)
    {
      const int tx = r.left + tw/2 + (int)((double)(tp - s->minv) * usable / range + 0.5);
      fill_rect(dc, tx, r.top + 1, tx + 1, cy - 2, shadow);
      fill_rect(dc, tx, cy + 3, tx + 1, r.bottom - 1, shadow);
    }
  }

  // thumb
  const int tx = r.left + (int)((double)(s->pos - s->minv) * usable / range + 0.5);
  RECT th = { tx, r.top, tx + tw, r.bottom };
  fill_rect(dc, th.left, th.top, th.right, th.bottom, mix_color(face, hilite, 128));
  frame_rect(dc, &th, GetFocus() == hwnd ? RGB(40, 120, 220) : dark);
  fill_rect(dc, tx + tw/2, th.top + 2, tx + tw/2 + 1, th.bottom - 2, dark);

  EndPaint(hwnd, &ps);
}

static LRESULT WINAPI FaderProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_DESTROY:
      free_state<FaderState>(hwnd);
    break;
    case TBM_SETRANGE:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        s->minv = (short)LOWORD(lParam);
        s->maxv = (short)HIWORD(lParam);
        if (s->maxv <= s->minv) s->maxv = s->minv + 1;
        s->range_set = true;
        if (wParam) InvalidateRect(hwnd, NULL, FALSE);
      }
    return 0;
    case TBM_SETRANGEMIN: get_state<FaderState>(hwnd)->minv = (int)lParam; get_state<FaderState>(hwnd)->range_set = true; return 0;
    case TBM_SETRANGEMAX: get_state<FaderState>(hwnd)->maxv = (int)lParam; get_state<FaderState>(hwnd)->range_set = true; return 0;
    case TBM_GETRANGEMIN: return get_state<FaderState>(hwnd)->minv;
    case TBM_GETRANGEMAX: return get_state<FaderState>(hwnd)->maxv;
    case TBM_SETTIC:
      get_state<FaderState>(hwnd)->tic = (int)lParam;
      InvalidateRect(hwnd, NULL, FALSE);
    return 0;
    case TBM_SETPOS:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        int p = (int)lParam;
        if (p < s->minv) p = s->minv;
        if (p > s->maxv) p = s->maxv;
        s->pos = p;
        InvalidateRect(hwnd, NULL, FALSE);
      }
    return 0;
    case TBM_GETPOS:
    return get_state<FaderState>(hwnd)->pos;
    case WM_REAPER_SETACCESSNAME:
      if (wParam == 1 && lParam) SetWindowText(hwnd, (const char *)lParam);
    return 0;
#ifdef WM_GETDLGCODE
    case WM_GETDLGCODE:
    return DLGC_WANTARROWS;
#endif
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      InvalidateRect(hwnd, NULL, FALSE);
    return 0;
    case WM_LBUTTONDOWN:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        SetFocus(hwnd);
        SetCapture(hwnd);
        s->dragging = true;
        s->last_x = GET_X_LPARAM(lParam);
        s->drag_pos = s->pos;
      }
    return 0;
    case WM_MOUSEMOVE:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        if (s->dragging && GetCapture() == hwnd)
        {
          RECT r;
          GetClientRect(hwnd, &r);
          const int usable = wdl_max(r.right - r.left - fader_thumb_w(&r), 1);
          const int x = GET_X_LPARAM(lParam);
          double scale = (double)(s->maxv - s->minv) / usable;
          if (fine_adjust_key()) scale *= 0.1;
          s->drag_pos += (x - s->last_x) * scale;
          if (s->drag_pos < s->minv) s->drag_pos = s->minv;
          if (s->drag_pos > s->maxv) s->drag_pos = s->maxv;
          s->last_x = x;
          fader_set(hwnd, s, (int)floor(s->drag_pos + 0.5), SB_THUMBTRACK);
        }
      }
    return 0;
    case WM_LBUTTONUP:
    case WM_CAPTURECHANGED:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        if (s->dragging)
        {
          s->dragging = false;
          if (msg == WM_LBUTTONUP && GetCapture() == hwnd) ReleaseCapture();
          fader_set(hwnd, s, s->pos, SB_ENDSCROLL);
        }
      }
    return 0;
    case WM_LBUTTONDBLCLK:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        fader_set(hwnd, s, fader_default_pos(s), SB_ENDSCROLL);
      }
    return 0;
    case WM_MOUSEWHEEL:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        const int delta = (short)HIWORD(wParam);
        int step = wdl_max((s->maxv - s->minv) / 50, 1);
        if (fine_adjust_key()) step = wdl_max(step / 10, 1);
        fader_set(hwnd, s, s->pos + (delta > 0 ? step : delta < 0 ? -step : 0), SB_ENDSCROLL);
      }
    return 0;
    case WM_KEYDOWN:
      {
        FaderState *s = get_state<FaderState>(hwnd);
        int step = wdl_max((s->maxv - s->minv) / 100, 1);
        if (fine_adjust_key()) step = wdl_max(step / 10, 1);
        switch (wParam)
        {
          case VK_LEFT: case VK_DOWN: fader_set(hwnd, s, s->pos - step, SB_ENDSCROLL); return 0;
          case VK_RIGHT: case VK_UP: fader_set(hwnd, s, s->pos + step, SB_ENDSCROLL); return 0;
          case VK_NEXT: fader_set(hwnd, s, s->pos - step*10, SB_ENDSCROLL); return 0;
          case VK_PRIOR: fader_set(hwnd, s, s->pos + step*10, SB_ENDSCROLL); return 0;
          case VK_HOME: fader_set(hwnd, s, s->minv, SB_ENDSCROLL); return 0;
          case VK_END: fader_set(hwnd, s, s->maxv, SB_ENDSCROLL); return 0;
        }
      }
    break;
    case WM_ERASEBKGND:
    return 1;
    case WM_PAINT:
      fader_paint(hwnd, get_state<FaderState>(hwnd));
    return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// REAPERhorzvu / REAPERvertvu
// ---------------------------------------------------------------------------
struct MeterState
{
  double db[2] = { -150.0, -150.0 };
};

static const double METER_MIN_DB = -60.0, METER_MAX_DB = 6.0;

static double meter_frac(double db)
{
  const double f = (db - METER_MIN_DB) / (METER_MAX_DB - METER_MIN_DB);
  return f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
}

static void meter_paint(HWND hwnd, MeterState *s, bool vertical)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  if (!dc) return;
  RECT r;
  GetClientRect(hwnd, &r);
  fill_rect(dc, r.left, r.top, r.right, r.bottom, RGB(16, 16, 16));

  const COLORREF unlit = RGB(40, 40, 40);
  const struct { double db; COLORREF col; } zones[3] = {
    { -12.0, RGB(40, 200, 70) }, { -3.0, RGB(230, 200, 40) }, { METER_MAX_DB, RGB(235, 60, 45) }
  };

  const int len = vertical ? (r.bottom - r.top - 2) : (r.right - r.left - 2);
  const int across = vertical ? (r.right - r.left - 2) : (r.bottom - r.top - 2);
  if (len > 0 && across > 1)
  {
    for (int ch = 0; ch < 2; ch++)
    {
      const int a0 = 1 + (across * ch) / 2 + (ch ? 1 : 0);
      const int a1 = 1 + (across * (ch + 1)) / 2;
      const int lit = (int)(meter_frac(s->db[ch]) * len + 0.5);
      int start = 0;
      for (int z = 0; z < 3; z++)
      {
        const int zend = (int)(meter_frac(zones[z].db) * len + 0.5);
        const int lit_end = wdl_min(zend, lit);
        for (int pass = 0; pass < 2; pass++)
        {
          const int p0 = pass ? wdl_max(start, lit) : start;
          const int p1 = pass ? zend : lit_end;
          if (p1 <= p0) continue;
          const COLORREF col = pass ? mix_color(unlit, zones[z].col, 40) : zones[z].col;
          if (vertical) fill_rect(dc, r.left + a0, r.bottom - 1 - p1, r.left + a1, r.bottom - 1 - p0, col);
          else fill_rect(dc, r.left + 1 + p0, r.top + a0, r.left + 1 + p1, r.top + a1, col);
        }
        start = zend;
      }
    }
    // 0 dB mark
    const int z0 = (int)(meter_frac(0.0) * len + 0.5);
    if (vertical) fill_rect(dc, r.left, r.bottom - 1 - z0, r.right, r.bottom - z0, RGB(120, 120, 120));
    else fill_rect(dc, r.left + 1 + z0, r.top, r.left + 2 + z0, r.bottom, RGB(120, 120, 120));
  }
  EndPaint(hwnd, &ps);
}

static LRESULT meter_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, bool vertical)
{
  switch (msg)
  {
    case WM_DESTROY:
      free_state<MeterState>(hwnd);
    break;
    case WM_REAPER_VU_SETPEAKS:
      if (lParam)
      {
        MeterState *s = get_state<MeterState>(hwnd);
        const double *p = (const double *)lParam;
        if (p[0] != s->db[0] || p[1] != s->db[1])
        {
          s->db[0] = p[0];
          s->db[1] = p[1];
          InvalidateRect(hwnd, NULL, FALSE);
        }
      }
    return 0;
    case WM_ERASEBKGND:
    return 1;
    case WM_PAINT:
      meter_paint(hwnd, get_state<MeterState>(hwnd), vertical);
    return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

static LRESULT WINAPI HorzMeterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return meter_proc(hwnd, msg, wParam, lParam, false); }
static LRESULT WINAPI VertMeterProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) { return meter_proc(hwnd, msg, wParam, lParam, true); }

// ---------------------------------------------------------------------------
// HotTrackButton (mute/solo)
// ---------------------------------------------------------------------------
struct ButtonState
{
  int icon = NJD_ICON_NONE;
  bool pressed = false, inside = false;
};

static void button_click(HWND hwnd)
{
  SendMessage(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(GetWindowLong(hwnd, GWL_ID), BN_CLICKED), (LPARAM)hwnd);
}

static void button_paint(HWND hwnd, ButtonState *s)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(hwnd, &ps);
  if (!dc) return;
  RECT r;
  GetClientRect(hwnd, &r);
  const COLORREF face = Host_GetSysColor(COLOR_3DFACE);
  fill_rect(dc, r.left, r.top, r.right, r.bottom, face);

  int icon = s->icon;
  if (icon == NJD_ICON_NONE)
  {
    char buf[64];
    GetWindowText(hwnd, buf, sizeof(buf));
    icon = strstr(buf, "solo") ? NJD_ICON_SOLO_OFF : NJD_ICON_MUTE_OFF;
  }
  const bool is_solo = icon == NJD_ICON_SOLO_OFF || icon == NJD_ICON_SOLO_ON;
  const bool on = icon == NJD_ICON_MUTE_ON || icon == NJD_ICON_SOLO_ON;

  COLORREF bg, fg;
  if (on)
  {
    bg = is_solo ? RGB(235, 200, 50) : RGB(215, 60, 50);
    fg = is_solo ? RGB(0, 0, 0) : RGB(255, 255, 255);
  }
  else
  {
    bg = mix_color(face, Host_GetSysColor(COLOR_3DHILIGHT), 96);
    fg = Host_GetSysColor(COLOR_BTNTEXT);
  }
  if (s->pressed && s->inside) bg = mix_color(bg, RGB(0, 0, 0), 48);

  RECT box = r;
  fill_rect(dc, box.left, box.top, box.right, box.bottom, bg);
  frame_rect(dc, &box, GetFocus() == hwnd ? RGB(40, 120, 220) : Host_GetSysColor(COLOR_3DDKSHADOW));

  static HFONT s_font;
  static int s_font_h;
  const int fh = wdl_max((r.bottom - r.top) - 2, 8);
  if (!s_font || s_font_h != fh)
  {
    if (s_font) DeleteObject(s_font);
    LOGFONT lf = { fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                   CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Arial" };
    s_font = CreateFontIndirect(&lf);
    s_font_h = fh;
  }
  HGDIOBJ old = SelectObject(dc, s_font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, fg);
  DrawText(dc, is_solo ? "S" : "M", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old);

  EndPaint(hwnd, &ps);
}

static LRESULT WINAPI ButtonProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_DESTROY:
      free_state<ButtonState>(hwnd);
    break;
    case BM_SETIMAGE:
      if (wParam & 0x8000)
      {
        ButtonState *s = get_state<ButtonState>(hwnd);
        s->icon = Host_IconFromThemePointer((const void *)lParam);
        InvalidateRect(hwnd, NULL, FALSE);
      }
    return 0;
    case WM_REAPER_SETTOOLTIP:
      if (wParam == 0xbeef && lParam) SetWindowText(hwnd, (const char *)lParam);
    return 0;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
      InvalidateRect(hwnd, NULL, FALSE);
    return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      {
        ButtonState *s = get_state<ButtonState>(hwnd);
        SetFocus(hwnd);
        SetCapture(hwnd);
        s->pressed = s->inside = true;
        InvalidateRect(hwnd, NULL, FALSE);
      }
    return 0;
    case WM_MOUSEMOVE:
      {
        ButtonState *s = get_state<ButtonState>(hwnd);
        if (s->pressed)
        {
          RECT r;
          GetClientRect(hwnd, &r);
          POINT p = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
          const bool in = !!PtInRect(&r, p);
          if (in != s->inside)
          {
            s->inside = in;
            InvalidateRect(hwnd, NULL, FALSE);
          }
        }
      }
    return 0;
    case WM_LBUTTONUP:
      {
        ButtonState *s = get_state<ButtonState>(hwnd);
        if (s->pressed)
        {
          const bool click = s->inside;
          s->pressed = s->inside = false;
          if (GetCapture() == hwnd) ReleaseCapture();
          InvalidateRect(hwnd, NULL, FALSE);
          if (click) button_click(hwnd);
        }
      }
    return 0;
    case WM_CAPTURECHANGED:
      {
        ButtonState *s = get_state<ButtonState>(hwnd);
        if (s->pressed)
        {
          s->pressed = s->inside = false;
          InvalidateRect(hwnd, NULL, FALSE);
        }
      }
    return 0;
    case WM_KEYDOWN:
      if (wParam == VK_SPACE)
      {
        button_click(hwnd);
        return 0;
      }
    break;
    case WM_ERASEBKGND:
    return 1;
    case WM_PAINT:
      button_paint(hwnd, get_state<ButtonState>(hwnd));
    return 0;
  }
  return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// registration
// ---------------------------------------------------------------------------
static const struct { const char *name; WNDPROC proc; bool dblclks; } s_classes[] = {
  { "REAPERhfader", FaderProc, true },
  { "REAPERhorzvu", HorzMeterProc, false },
  { "REAPERvertvu", VertMeterProc, false },
  { "HotTrackButton", ButtonProc, false },
};

#ifndef _WIN32
static HWND customControlCreator(HWND parent, const char *cname, int idx, const char *classname, int style, int x, int y, int w, int h)
{
  // from ReaNINJAM's vstframe.cpp
  if (!stricmp(classname, "RichEditChild"))
  {
    if ((style & 0x2800))
    {
      return SWELL_MakeEditField(idx, -x, -y, -w, -h, ES_READONLY|WS_VSCROLL|ES_MULTILINE);
    }
    else
      return SWELL_MakeEditField(idx, -x, -y, -w, -h, 0);
  }

  for (size_t i = 0; i < sizeof(s_classes)/sizeof(s_classes[0]); i++)
  {
    if (strcmp(classname, s_classes[i].name)) continue;

    // resource-less CreateDialog() makes a plain child window using proc as its window procedure
    HWND hw = CreateDialog(g_hInst, 0, parent, (DLGPROC)s_classes[i].proc);
    if (!hw) return 0;
    SetWindowLong(hw, GWL_ID, idx);
    if (cname && *cname) SetWindowText(hw, cname);
    SetWindowPos(hw, HWND_TOP, x, y, w, h, SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(hw, SW_SHOWNA);
    return hw;
  }
  return 0;
}
#endif

void Controls_Register()
{
#ifdef _WIN32
  for (size_t i = 0; i < sizeof(s_classes)/sizeof(s_classes[0]); i++)
  {
    WNDCLASS wc = { 0, };
    wc.style = CS_HREDRAW | CS_VREDRAW | (s_classes[i].dblclks ? CS_DBLCLKS : 0);
    wc.lpfnWndProc = s_classes[i].proc;
    wc.hInstance = g_hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = s_classes[i].name;
    RegisterClass(&wc);
  }
#else
  SWELL_RegisterCustomControlCreator(customControlCreator);
#endif
}
