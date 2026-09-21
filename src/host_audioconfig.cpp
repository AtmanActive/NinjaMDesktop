/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// "Audio Configuration" dialog (the part of REAPER's preferences ReaNINJAM relies on).

#ifdef _WIN32
#include <windows.h>
#else
#include "WDL/swell/swell.h"
#endif
#include <stdio.h>
#include <string.h>

#include "WDL/wdlcstring.h"
#include "WDL/wdlstring.h"
#include "host.h"
#include "host_audio.h"
#include "host_resource.h"

static const int s_srates[] = { 0, 44100, 48000, 88200, 96000 };
static const int s_bsizes[] = { 64, 128, 256, 512, 1024, 2048 };

static void combo_add(HWND combo, const char *text)
{
  SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)text);
}

static void combo_select_string(HWND combo, const char *text, int fallback)
{
  const int n = (int)SendMessage(combo, CB_GETCOUNT, 0, 0);
  for (int i = 0; i < n; i++)
  {
    char buf[1024];
    buf[0] = 0;
    SendMessage(combo, CB_GETLBTEXT, i, (LPARAM)buf);
    if (!strcmp(buf, text))
    {
      SendMessage(combo, CB_SETCURSEL, i, 0);
      return;
    }
  }
  SendMessage(combo, CB_SETCURSEL, fallback, 0);
}

static void combo_get_selected(HWND combo, char *buf, int bufsz)
{
  buf[0] = 0;
  const int sel = (int)SendMessage(combo, CB_GETCURSEL, 0, 0);
  if (sel < 0) return;
  char tmp[1024];
  tmp[0] = 0;
  SendMessage(combo, CB_GETLBTEXT, sel, (LPARAM)tmp);
  lstrcpyn_safe(buf, tmp, bufsz);
}

#define DEFAULT_DEVICE_STR "Default device"
#define NO_INPUT_STR "No input (listen only)"
#define AUTO_BACKEND_STR "Automatic"

// fill the device combos for the selected backend; keeps current choices when possible
static void populate_devices(HWND hwndDlg, const char *want_in, const char *want_out)
{
  HWND hin = GetDlgItem(hwndDlg, IDC_NJD_INDEV), hout = GetDlgItem(hwndDlg, IDC_NJD_OUTDEV);
  SendMessage(hin, CB_RESETCONTENT, 0, 0);
  SendMessage(hout, CB_RESETCONTENT, 0, 0);
  combo_add(hin, DEFAULT_DEVICE_STR);
  combo_add(hin, NO_INPUT_STR);
  combo_add(hout, DEFAULT_DEVICE_STR);

  char bname[64];
  combo_get_selected(GetDlgItem(hwndDlg, IDC_NJD_BACKEND), bname, sizeof(bname));
  ma_backend backend;
  const bool have_backend = Audio_BackendFromName(bname, &backend);

  ma_context ctx;
  ma_context_config cc;
  Audio_InitContextConfig(&cc);
  WDL_FastString note;
  if (ma_context_init(have_backend ? &backend : NULL, have_backend ? 1 : 0, &cc, &ctx) == MA_SUCCESS)
  {
    ma_device_info *pinfo = NULL, *cinfo = NULL;
    ma_uint32 pcnt = 0, ccnt = 0;
    if (ma_context_get_devices(&ctx, &pinfo, &pcnt, &cinfo, &ccnt) == MA_SUCCESS)
    {
      for (ma_uint32 i = 0; i < ccnt; i++) combo_add(hin, cinfo[i].name);
      for (ma_uint32 i = 0; i < pcnt; i++) combo_add(hout, pinfo[i].name);
    }
    ma_context_uninit(&ctx);
  }
  else
  {
    note.SetFormatted(256, "%s is not available on this system.", bname);
  }

  if (!strcmp(want_in, AUDIO_NO_INPUT)) SendMessage(hin, CB_SETCURSEL, 1, 0);
  else combo_select_string(hin, want_in[0] ? want_in : DEFAULT_DEVICE_STR, 0);
  combo_select_string(hout, want_out[0] ? want_out : DEFAULT_DEVICE_STR, 0);

  if (note.GetLength()) SetDlgItemText(hwndDlg, IDC_NJD_STATUS, note.Get());
}

static void read_dialog(HWND hwndDlg, AudioSettings *st)
{
  memset(st, 0, sizeof(*st));
  combo_get_selected(GetDlgItem(hwndDlg, IDC_NJD_BACKEND), st->backend, sizeof(st->backend));
  if (!strcmp(st->backend, AUTO_BACKEND_STR)) st->backend[0] = 0;

  combo_get_selected(GetDlgItem(hwndDlg, IDC_NJD_INDEV), st->indev, sizeof(st->indev));
  if (!strcmp(st->indev, DEFAULT_DEVICE_STR)) st->indev[0] = 0;
  else if (!strcmp(st->indev, NO_INPUT_STR)) lstrcpyn_safe(st->indev, AUDIO_NO_INPUT, sizeof(st->indev));

  combo_get_selected(GetDlgItem(hwndDlg, IDC_NJD_OUTDEV), st->outdev, sizeof(st->outdev));
  if (!strcmp(st->outdev, DEFAULT_DEVICE_STR)) st->outdev[0] = 0;

  int sel = (int)SendDlgItemMessage(hwndDlg, IDC_NJD_SRATE, CB_GETCURSEL, 0, 0);
  st->srate = (sel >= 0 && sel < (int)(sizeof(s_srates)/sizeof(s_srates[0]))) ? s_srates[sel] : 0;
  sel = (int)SendDlgItemMessage(hwndDlg, IDC_NJD_BSIZE, CB_GETCURSEL, 0, 0);
  st->bsize = (sel >= 0 && sel < (int)(sizeof(s_bsizes)/sizeof(s_bsizes[0]))) ? s_bsizes[sel] : 256;
}

// save settings and restart the device; returns true on success
static bool apply(HWND hwndDlg)
{
  AudioSettings st;
  read_dialog(hwndDlg, &st);
  Audio_WriteSettings(&st);

  char err[1024];
  const bool ok = Audio_Start(err, sizeof(err));
  SetDlgItemText(hwndDlg, IDC_NJD_STATUS, ok ? Audio_GetStatusText() : err);
  if (!ok) MessageBox(hwndDlg, err, "Audio Configuration", MB_OK);
  return ok;
}

static WDL_DLGRET AudioConfigProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        AudioSettings st;
        Audio_ReadSettings(&st);

        HWND hb = GetDlgItem(hwndDlg, IDC_NJD_BACKEND);
        combo_add(hb, AUTO_BACKEND_STR);
        ma_backend list[MA_BACKEND_COUNT];
        const int n = Audio_GetBackends(list, MA_BACKEND_COUNT);
        for (int i = 0; i < n; i++) combo_add(hb, ma_get_backend_name(list[i]));
        combo_select_string(hb, st.backend[0] ? st.backend : AUTO_BACKEND_STR, 0);

        HWND hs = GetDlgItem(hwndDlg, IDC_NJD_SRATE);
        int sel = 0;
        for (size_t i = 0; i < sizeof(s_srates)/sizeof(s_srates[0]); i++)
        {
          char buf[64];
          if (!s_srates[i]) lstrcpyn_safe(buf, "Device default", sizeof(buf));
          else snprintf(buf, sizeof(buf), "%d Hz", s_srates[i]);
          combo_add(hs, buf);
          if (s_srates[i] == st.srate) sel = (int)i;
        }
        SendMessage(hs, CB_SETCURSEL, sel, 0);

        HWND hz = GetDlgItem(hwndDlg, IDC_NJD_BSIZE);
        sel = 2;
        for (size_t i = 0; i < sizeof(s_bsizes)/sizeof(s_bsizes[0]); i++)
        {
          char buf[64];
          snprintf(buf, sizeof(buf), "%d frames", s_bsizes[i]);
          combo_add(hz, buf);
          if (s_bsizes[i] == st.bsize) sel = (int)i;
        }
        SendMessage(hz, CB_SETCURSEL, sel, 0);

        SetDlgItemText(hwndDlg, IDC_NJD_STATUS, Audio_GetStatusText());
        populate_devices(hwndDlg, st.indev, st.outdev);
      }
    return 1;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_NJD_BACKEND:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            AudioSettings st;
            read_dialog(hwndDlg, &st);
            populate_devices(hwndDlg, st.indev, st.outdev);
          }
        break;
        case IDC_NJD_APPLY:
          apply(hwndDlg);
        break;
        case IDOK:
          if (apply(hwndDlg)) EndDialog(hwndDlg, 1);
        break;
        case IDCANCEL:
          EndDialog(hwndDlg, 0);
        break;
      }
    return 0;

    case WM_CLOSE:
      EndDialog(hwndDlg, 0);
    return 0;
  }
  return 0;
}

void AudioConfig_Show(HWND parent)
{
  DialogBox(g_hInst, MAKEINTRESOURCE(IDD_NJD_AUDIOCFG), parent, AudioConfigProc);
}
