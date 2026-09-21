/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Portions (reaninjamAccelProc, customControlCreator) from ReaNINJAM's vstframe.cpp,
    Copyright (C) Cockos Incorporated.
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Application entry point: plays the role REAPER plays for ReaNINJAM
// (message loop, keyboard accelerators, lifetime of the ReaNINJAM window).

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#endif
// on Windows this only provides the SWELLAPP_* constants used by SWELLAppMain() below
#include "WDL/swell/swell.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#include "winclient.h"
#include "resource.h"
#include "host.h"
#include "host_resource.h"

// winclient.cpp
extern HWND g_hwnd;
void InitializeInstance();
void QuitInstance();

#if !defined(_WIN32) && !defined(__APPLE__)
static bool g_quit;
static bool g_restart; // relaunch after a clean shutdown (zoom changes)
#endif

static WNDPROC s_orig_mainproc;
#define ICON_RETRY_TIMER 0x4e49

// File > Zoom choices (percent; 0 = automatic)
static const int s_zoom_levels[] = { 0, 75, 90, 100, 110, 125, 150, 175, 200 };
static const int NUM_ZOOM_LEVELS = (int)(sizeof(s_zoom_levels)/sizeof(s_zoom_levels[0]));

// ---------------------------------------------------------------------------
// Keyboard shortcuts: ReaNINJAM registers this with REAPER's "accelerator" hook.
// Copied verbatim from jmde/fx/reaninjam/vstframe.cpp.
// Returns 1 to eat the message, -1 to pass it to the window, 0 if not ours.
// ---------------------------------------------------------------------------
static int reaninjamAccelProc(MSG *msg, accelerator_register_t *ctx)
{
  extern HWND g_hwnd;
  if (g_hwnd && (
        msg->message == WM_KEYDOWN || msg->message == WM_KEYUP ||
        msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP ||
        msg->message == WM_CHAR) &&
      msg->hwnd && (g_hwnd==msg->hwnd || IsChild(g_hwnd,msg->hwnd)))
  {

    if (msg->message != WM_CHAR)
    {
      const int flags = ((GetAsyncKeyState(VK_MENU)&0x8000) ? FALT : 0) |
                        ((GetAsyncKeyState(VK_SHIFT)&0x8000) ? FSHIFT : 0) |
                        ((GetAsyncKeyState(VK_CONTROL)&0x8000) ? FCONTROL : 0);
      const bool isDown = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
      if (IS_MSG_VIRTKEY(msg) && msg->wParam >= VK_F1 && msg->wParam <= VK_F10)
      {
        int idx = msg->wParam - VK_F1;
        switch (flags)
        {
          case 0:
            if (isDown)
              SendMessage(g_hwnd,WM_COMMAND,ID_LOCAL_CHANNEL_1+idx,0);
          return 1;
          case FSHIFT:
            if (isDown)
              SendMessage(g_hwnd,WM_COMMAND,ID_REMOTE_USER_1+idx,0);
          return 1;
          case FCONTROL|FSHIFT:
            if (isDown)
              SendMessage(g_hwnd,WM_COMMAND,ID_REMOTE_USER_CHANNEL_1+idx,0);
          return 1;
        }
      }
      else switch (flags)
      {
      case FALT:
        switch (msg->wParam)
        {
#ifdef __APPLE__
          case 'Y':
            if (isDown) SendMessage(g_hwnd,WM_COMMAND,IDC_SYNC,0);
          return 1;
#endif
          case 'S':
          case 'M':
            if (isDown)
              SendMessage(g_hwnd,WM_COMMAND,msg->wParam == 'S' ? IDC_SOLO : IDC_MUTE,0);
          return 1;
          case 'T':
            if (isDown) SetFocus(GetDlgItem(g_hwnd,IDC_CHATENT));
          return 1;
        }
      break;
      case FCONTROL|FSHIFT:
        switch (msg->wParam)
        {
          case 'D':
            if (isDown)
              SendMessage(g_hwnd,WM_COMMAND,IDC_REMOVE,0);
          return 1;
          case 'N':
            if (isDown)
              SendMessage(g_hwnd,WM_COMMAND,IDC_ADDCH,0);
          return 1;
          case 'M':
            if (isDown) SendMessage(g_hwnd,WM_COMMAND,IDC_MASTERMUTE,0);
          return 1;
        }
      break;
      case FCONTROL:
        switch (msg->wParam)
        {
          case 'M':
            if (isDown) SendMessage(g_hwnd,WM_COMMAND,IDC_METROMUTE,0);
          return 1;
          case 'O':
            if (isDown) SendMessage(g_hwnd,WM_COMMAND,ID_FILE_CONNECT,0);
          return 1;
          case 'D':
            if (isDown) SendMessage(g_hwnd,WM_COMMAND,ID_FILE_DISCONNECT,0);
          return 1;
#ifdef __APPLE__
          case ',':
#else
          case 'P':
#endif
            if (isDown) SendMessage(g_hwnd,WM_COMMAND,ID_OPTIONS_PREFERENCES,0);
          return 1;
        }
      break;
      }
    }

    HWND list = GetDlgItem(g_hwnd,IDC_CHATDISP);
    HWND e = GetDlgItem(g_hwnd,IDC_CHATENT);
    if (e)
    {
      if (msg->hwnd == e || IsChild(e,msg->hwnd))
      {
#ifdef _WIN32
        if (msg->message == WM_CHAR && msg->wParam == VK_RETURN)
#else
        if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN)
#endif
        {
          SendMessage(g_hwnd,WM_COMMAND,IDC_CHATOK,0);
          return 1;
        }
      }
      else if (list && (msg->hwnd == list || IsChild(list,msg->hwnd)))
      {
#ifndef __APPLE__
        // this appears to be unsafe on macOS (could get it to throw exceptions bleh)
        if (msg->message != WM_CHAR) switch (msg->wParam)
        {
          case VK_CONTROL:
          case VK_SHIFT:
          case VK_UP:
          case VK_DOWN:
          case VK_LEFT:
          case VK_RIGHT:
          case VK_NEXT:
          case VK_PRIOR:
          case VK_TAB:
            // allow ctrl/shift/nav keys to go to control, everything else causes focus change to edit
          return -1;
        }
        if (!(GetAsyncKeyState(VK_CONTROL)&0x8000))
        {
          SetFocus(e);
#ifdef _WIN32
          msg->hwnd = e;
#else
          if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN)
          {
            SendMessage(g_hwnd,WM_COMMAND,IDC_CHATOK,0);
          }
          else
          {
            // linux at least needs this (otherwise it selects existing text and then overwrites it)
            SendMessage(e,EM_SETSEL,-1,0);
            SendMessage(e,msg->message,msg->wParam,msg->lParam);
          }
          return 1;
#endif
        }
#endif
      }
    }
    return -1;
  }
  return 0;
}

// host-level shortcuts first (Ctrl+Q), then ReaNINJAM's own
static int HostAccelProc(MSG *msg)
{
  if (g_hwnd && msg->message == WM_KEYDOWN && (msg->wParam == 'Q' || msg->wParam == 'q') &&
      (GetAsyncKeyState(VK_CONTROL) & 0x8000) && !(GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
      !(GetAsyncKeyState(VK_MENU) & 0x8000) &&
      msg->hwnd && (msg->hwnd == g_hwnd || IsChild(g_hwnd, msg->hwnd)))
  {
    PostMessage(g_hwnd, WM_COMMAND, IDM_NJD_QUIT, 0);
    return 1;
  }
  return reaninjamAccelProc(msg, NULL);
}

// ---------------------------------------------------------------------------
// Main window: adjustments for running outside REAPER
// ---------------------------------------------------------------------------
static void ShowAbout(HWND parent)
{
  char buf[2048];
  snprintf(buf, sizeof(buf),
    NJD_APP_NAME " " NJD_VERSION "\n\n"
    "A standalone desktop build of ReaNINJAM, the NINJAM client from Cockos Incorporated.\n\n"
    "NINJAM and ReaNINJAM: Copyright (C) 2005-2025 Cockos Incorporated (www.ninjam.com)\n"
    "Audio I/O: miniaudio by David Reid\n"
    "Ogg Vorbis: Xiph.Org Foundation\n\n"
    "This program is free software, licensed under the GNU General Public License v2 or later. "
    "It comes with ABSOLUTELY NO WARRANTY.\n\n"
    "Settings: %s", g_ini_file.Get());
  MessageBox(parent, buf, "About " NJD_APP_NAME, MB_OK);
}

static LRESULT WINAPI MainWndSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case ID_OPTIONS_AUDIOCONFIGURATION:
          // ReaNINJAM sets a session directory on connect and clears it on disconnect
          if (g_client && g_client->GetWorkDir()[0])
          {
            // reopening the device mid-session would disturb NJClient's interval timing
            if (MessageBox(hwnd, "Changing the audio configuration will disconnect you from the server.\n\nContinue?",
                           "Audio Configuration", MB_YESNO) != IDYES)
              return 0;
            SendMessage(hwnd, WM_COMMAND, ID_FILE_DISCONNECT, 0);
          }
          AudioConfig_Show(hwnd);
        return 0;
        case IDM_NJD_QUIT:
          SendMessage(hwnd, WM_CLOSE, 0, 0);
        return 0;
        case IDM_NJD_ABOUT:
          ShowAbout(hwnd);
        return 0;
        case IDM_NJD_THEME_SYSTEM:
        case IDM_NJD_THEME_LIGHT:
        case IDM_NJD_THEME_DARK:
          Theme_SetMode(LOWORD(wParam) - IDM_NJD_THEME_SYSTEM);
        return 0;
        default:
          if (LOWORD(wParam) >= IDM_NJD_ZOOM_FIRST && LOWORD(wParam) < IDM_NJD_ZOOM_FIRST + NUM_ZOOM_LEVELS)
          {
            if (Zoom_SetPercent(s_zoom_levels[LOWORD(wParam) - IDM_NJD_ZOOM_FIRST]))
            {
              const bool connected = g_client && g_client->GetWorkDir()[0];
              if (MessageBox(hwnd, connected ?
                    "The new zoom level applies after restarting " NJD_APP_NAME ".\n\n"
                    "Restart now? This will disconnect you from the server." :
                    "The new zoom level applies after restarting " NJD_APP_NAME ".\n\nRestart now?",
                    "Zoom", MB_YESNO) == IDYES)
              {
#if !defined(_WIN32) && !defined(__APPLE__)
                g_restart = true;
#endif
                SendMessage(hwnd, WM_CLOSE, 0, 0);
              }
            }
            return 0;
          }
        break;
      }
    break;
    case WM_TIMER:
      if (wParam == ICON_RETRY_TIMER)
      {
        if (Host_SetWindowIcon(hwnd)) KillTimer(hwnd, ICON_RETRY_TIMER);
        return 0;
      }
      if (Theme_OnTimer(wParam)) return 0;
    break;
    case WM_INITMENUPOPUP:
      {
        // ReaNINJAM greys "Audio configuration" while connected (a leftover from the old
        // standalone client); keep it available and offer to disconnect instead.
        const LRESULT ret = CallWindowProc(s_orig_mainproc, hwnd, msg, wParam, lParam);
        if (wParam)
        {
          HMENU m = (HMENU)wParam;
          EnableMenuItem(m, ID_OPTIONS_AUDIOCONFIGURATION, MF_BYCOMMAND|MF_ENABLED);
          for (int x = THEME_SYSTEM; x <= THEME_DARK; x++)
            CheckMenuItem(m, IDM_NJD_THEME_SYSTEM + x, MF_BYCOMMAND|(Theme_GetMode() == x ? MF_CHECKED : MF_UNCHECKED));
          for (int x = 0; x < NUM_ZOOM_LEVELS; x++)
            CheckMenuItem(m, IDM_NJD_ZOOM_FIRST + x, MF_BYCOMMAND|(Zoom_GetPercent() == s_zoom_levels[x] ? MF_CHECKED : MF_UNCHECKED));
        }
        return ret;
      }
#ifdef _WIN32
    case WM_ENDSESSION:
#endif
    case WM_DESTROY:
      {
        // stop the audio thread before ReaNINJAM deletes its NJClient
        Audio_Stop();
        const LRESULT ret = CallWindowProc(s_orig_mainproc, hwnd, msg, wParam, lParam);
#ifdef _WIN32
        if (msg == WM_DESTROY) PostQuitMessage(0);
#elif defined(__APPLE__)
        SWELL_PostQuitMessage(0);
#else
        g_quit = true;
#endif
        return ret;
      }
  }
  return CallWindowProc(s_orig_mainproc, hwnd, msg, wParam, lParam);
}

static void AdaptMainWindow(HWND hwnd)
{
  SetWindowText(hwnd, NJD_APP_NAME);

#ifdef _WIN32
  {
    HICON icon = LoadIcon(g_hInst, MAKEINTRESOURCE(IDI_NJD_APPICON));
    if (icon)
    {
      SendMessage(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
      SendMessage(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    }
  }
#endif

  // "Sync..." only controls REAPER's transport/tempo, which does not exist here
  ShowWindow(GetDlgItem(hwnd, IDC_SYNC), SW_HIDE);

  HMENU menu = GetMenu(hwnd);
  if (menu)
  {
    // top-level menus: File, Channels, Sync
    const int cnt = GetMenuItemCount(menu);
    if (cnt >= 3) DeleteMenu(menu, 2, MF_BYPOSITION);

    HMENU file = GetSubMenu(menu, 0);
    if (file)
    {
      // after "Preferences..."
      MENUITEMINFO mii = { sizeof(mii), MIIM_TYPE | MIIM_ID, MFT_STRING, };
      mii.wID = ID_OPTIONS_AUDIOCONFIGURATION;
      mii.dwTypeData = (char *)"&Audio configuration...";
      InsertMenuItem(file, GetMenuItemCount(file), TRUE, &mii);

      if (Theme_Supported())
      {
        HMENU theme = CreatePopupMenu();
        static const char *names[] = { "Follow &system", "&Light", "&Dark" };
        for (int x = THEME_SYSTEM; x <= THEME_DARK; x++)
        {
          MENUITEMINFO ti = { sizeof(ti), MIIM_TYPE | MIIM_ID, MFT_STRING, };
          ti.wID = IDM_NJD_THEME_SYSTEM + x;
          ti.dwTypeData = (char *)names[x];
          InsertMenuItem(theme, x, TRUE, &ti);
        }
        MENUITEMINFO sub = { sizeof(sub), MIIM_TYPE | MIIM_SUBMENU, MFT_STRING, };
        sub.hSubMenu = theme;
        sub.dwTypeData = (char *)"&Theme";
        InsertMenuItem(file, GetMenuItemCount(file), TRUE, &sub);

        HMENU zoom = CreatePopupMenu();
        for (int x = 0; x < NUM_ZOOM_LEVELS; x++)
        {
          char buf[64];
          if (!s_zoom_levels[x]) snprintf(buf, sizeof(buf), "&Automatic (display scale)");
          else snprintf(buf, sizeof(buf), "%d%%", s_zoom_levels[x]);
          MENUITEMINFO zi = { sizeof(zi), MIIM_TYPE | MIIM_ID, MFT_STRING, };
          zi.wID = IDM_NJD_ZOOM_FIRST + x;
          zi.dwTypeData = buf;
          InsertMenuItem(zoom, x, TRUE, &zi);
        }
        sub.hSubMenu = zoom;
        sub.dwTypeData = (char *)"&Zoom";
        InsertMenuItem(file, GetMenuItemCount(file), TRUE, &sub);
      }

      MENUITEMINFO sep = { sizeof(sep), MIIM_TYPE, MFT_SEPARATOR, };
      InsertMenuItem(file, GetMenuItemCount(file), TRUE, &sep);

      mii.wID = IDM_NJD_ABOUT;
      mii.dwTypeData = (char *)"A&bout " NJD_APP_NAME "...";
      InsertMenuItem(file, GetMenuItemCount(file), TRUE, &mii);

#ifndef __APPLE__
      mii.wID = IDM_NJD_QUIT;
      mii.dwTypeData = (char *)"&Quit\tCtrl+Q";
      InsertMenuItem(file, GetMenuItemCount(file), TRUE, &mii);
#endif
    }
#ifdef __APPLE__
    // the first menu becomes the application menu (inside REAPER, ReaNINJAM inserts REAPER's here)
    extern HMENU SWELL_app_stocksysmenu; // application menu from MainMenu.xib
    if (SWELL_app_stocksysmenu)
    {
      HMENU appmenu = SWELL_DuplicateMenu(SWELL_app_stocksysmenu);
      MENUITEMINFO mi = { sizeof(mi), MIIM_STATE|MIIM_SUBMENU|MIIM_TYPE, MFT_STRING, 0, 0, appmenu, NULL, NULL, 0, (char *)NJD_APP_NAME };
      InsertMenuItem(menu, 0, TRUE, &mi);
    }
#endif
#ifdef _WIN32
    DrawMenuBar(hwnd);
#endif
  }

  s_orig_mainproc = (WNDPROC)SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)MainWndSubclassProc);
  Theme_AttachWindow(hwnd);

  // the native window may not exist until the first message loop pass
  if (!Host_SetWindowIcon(hwnd)) SetTimer(hwnd, ICON_RETRY_TIMER, 100, NULL);
}

static void StartAudio()
{
  char err[1024];
  if (!Audio_Start(err, sizeof(err)))
  {
    WDL_FastString msg(err);
    msg.Append("\n\nUse File > Audio configuration... to choose a different audio device.");
    MessageBox(g_hwnd, msg.Get(), NJD_APP_NAME, MB_OK);
  }
}

// ---------------------------------------------------------------------------
// Application lifecycle (SWELL calls this on Linux/macOS; WinMain below on Windows)
// ---------------------------------------------------------------------------
INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
{
  switch (msg)
  {
    case SWELLAPP_ONLOAD:
      Host_InitReaperAPI();
      Theme_Init(); // before any window exists, so the first paint uses the right colors
      Controls_Register();
    break;
    case SWELLAPP_LOADED:
      InitializeInstance(); // ReaNINJAM: reads reaninjam.ini, creates NJClient and the main window
      if (!g_hwnd)
      {
        MessageBox(NULL, "Could not create the main window.", NJD_APP_NAME, MB_OK);
#ifdef _WIN32
        PostQuitMessage(1);
#elif defined(__APPLE__)
        SWELL_PostQuitMessage(0);
#else
        g_quit = true;
#endif
        break;
      }
      AdaptMainWindow(g_hwnd);
      StartAudio();
    break;
    case SWELLAPP_DESTROY:
      Audio_Stop();
      if (g_hwnd) DestroyWindow(g_hwnd);
      QuitInstance();
    break;
    case SWELLAPP_ONCOMMAND:
      // commands from the macOS application menu
      if (g_hwnd && parm1) SendMessage(g_hwnd, WM_COMMAND, parm1, 0);
    break;
    case SWELLAPP_PROCESSMESSAGE:
      if (parm1) return HostAccelProc((MSG *)parm1) > 0 ? 1 : 0;
    break;
  }
  return 0;
}

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  g_hInst = hInstance;

  SWELLAppMain(SWELLAPP_ONLOAD, 0, 0);
  SWELLAppMain(SWELLAPP_LOADED, 0, 0);

  for (;;)
  {
    MSG msg = { 0, };
    const int ret = GetMessage(&msg, NULL, 0, 0);
    if (!ret) break;
    if (ret < 0)
    {
      Sleep(10);
      continue;
    }
    if (!msg.hwnd)
    {
      DispatchMessage(&msg);
      continue;
    }

    if (HostAccelProc(&msg) > 0) continue;

    if (g_hwnd && IsDialogMessage(g_hwnd, &msg)) continue;

    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  SWELLAppMain(SWELLAPP_DESTROY, 0, 0);
  return 0;
}

#else // SWELL

#ifdef __APPLE__
extern "C" {
#endif
const char **g_argv;
int g_argc;
#ifdef __APPLE__
};
#endif

#ifndef __APPLE__
int main(int argc, const char **argv)
{
  g_argc = argc;
  g_argv = argv;
  // SWELL's GDK port is X11-only (it runs through XWayland on Wayland desktops). It asks GDK
  // for the x11 backend, but an inherited GDK_BACKEND=wayland would override that request.
  setenv("GDK_BACKEND", "x11", 1);
  SWELL_initargs(&argc, (char ***)&argv);
  SWELL_Internal_PostMessage_Init();
  SWELL_ExtendedAPI("APPNAME", (void *)NJD_APP_NAME);
  SWELLAppMain(SWELLAPP_ONLOAD, 0, 0);
  SWELLAppMain(SWELLAPP_LOADED, 0, 0);
  while (!g_quit)
  {
    SWELL_RunMessageLoop();
    Sleep(10);
  }
  SWELLAppMain(SWELLAPP_DESTROY, 0, 0);
  if (g_restart)
  {
    // ReaNINJAM saved its window size in pixels at the old zoom; keep the same apparent size
    const int zoom = Zoom_GetPercent();
    if (zoom > 0)
    {
      const double ratio = (zoom * 256.0 / 100.0) / SWELL_GetScaling256();
      static const char *keys[] = { "wnd_w", "wnd_h" };
      for (int i = 0; i < 2; i++)
      {
        const int v = GetPrivateProfileInt("ninjam", keys[i], 0, g_ini_file.Get());
        if (v <= 0) continue;
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", (int)(v * ratio + 0.5));
        WritePrivateProfileString("ninjam", keys[i], buf, g_ini_file.Get());
      }
    }
    execv("/proc/self/exe", (char * const *)g_argv); // settings were saved on close
  }
  return 0;
}
#endif

// ---------------------------------------------------------------------------
// SWELL dialog and menu resources, generated from the .rc files at build time
// ---------------------------------------------------------------------------
#define SET_IDD_EMPTY_SCROLL_STYLE SWELL_DLG_FLAGS_AUTOGEN|SWELL_DLG_WS_CHILD
#define SET_IDD_EMPTY_STYLE SWELL_DLG_FLAGS_AUTOGEN|SWELL_DLG_WS_CHILD

#include "WDL/swell/swell-dlggen.h"
#include "res.rc_mac_dlg"
#include "host.rc_mac_dlg"
#undef BEGIN
#undef END
#include "WDL/swell/swell-menugen.h"
#include "res.rc_mac_menu"
#include "host.rc_mac_menu"

#endif
