/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

// Shared declarations for the host layer that replaces REAPER around ReaNINJAM.

#ifndef NJD_HOST_H
#define NJD_HOST_H

#ifdef _WIN32
#include <windows.h>
#else
#include "WDL/swell/swell.h"
#endif

#define NJD_APP_NAME "NinjaMDesktop"
#define NJD_AUDIO_SEC "ninjamdesktop_audio"

extern HINSTANCE g_hInst;

// host_paths.cpp
const char *Host_GetConfigDir();     // per-user settings directory (created on demand)
const char *Host_GetDocumentsDir();  // default parent of NINJAMsessions
bool Host_CreateDirectories(const char *path);

// host_reaper_api.cpp: fills in the function pointers ReaNINJAM normally imports from REAPER
void Host_InitReaperAPI();
// maps a pointer returned by our GetIconThemePointer() to one of the NJD_ICON_* values (0 if unknown)
enum { NJD_ICON_NONE=0, NJD_ICON_MUTE_OFF, NJD_ICON_MUTE_ON, NJD_ICON_SOLO_OFF, NJD_ICON_SOLO_ON, NJD_ICON_COUNT };
int Host_IconFromThemePointer(const void *ptr);
double Host_DB2Slider(double db);
double Host_Slider2DB(double pos);

// host_controls.cpp: REAPER's custom controls (faders, meters, mute/solo buttons)
void Controls_Register();

// host_icon.cpp: sets the application icon on a top-level window (Linux); false if the window
// has no native window yet
bool Host_SetWindowIcon(HWND hwnd);

// host_audio.cpp
bool Audio_Start(char *errbuf, int errbufsz); // reads settings from the ini file
void Audio_Stop();
bool Audio_IsRunning();
const char *Audio_GetStatusText();

// host_audioconfig.cpp
void AudioConfig_Show(HWND parent);

// host_theme.cpp (Windows parts in host_theme_win.cpp)
enum { THEME_SYSTEM = 0, THEME_LIGHT = 1, THEME_DARK = 2 };
bool Theme_Supported();
void Theme_Init();                  // before any window is created
void Theme_AttachWindow(HWND hwnd); // main window: repainted on changes, polls the system setting
bool Theme_OnTimer(WPARAM id);      // true if the timer was ours
void Theme_OnSystemChange();        // the system's settings changed (WM_SETTINGCHANGE)
int Theme_GetMode();
void Theme_SetMode(int mode);
COLORREF Host_GetSysColor(int idx); // GetSysColor() in the current theme's colors
bool Zoom_Supported();
int Zoom_GetPercent();             // 0 = automatic
bool Zoom_SetPercent(int pct);     // saves the setting (applies at next start); true if it changed

// host_license.cpp: remembers accepted server license agreements (after InitializeInstance())
void License_Install();

#endif
