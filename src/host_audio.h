/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// miniaudio-level helpers shared by host_audio.cpp and host_audioconfig.cpp

#ifndef NJD_HOST_AUDIO_H
#define NJD_HOST_AUDIO_H

#include "miniaudio.h"

#define AUDIO_NO_INPUT "(none)"

struct AudioSettings
{
  char backend[64];  // miniaudio backend name, "" = automatic
  char indev[512];   // "" = default, AUDIO_NO_INPUT = playback only
  char outdev[512];  // "" = default
  int srate;         // 0 = device default
  int bsize;         // frames per period
};

int Audio_GetBackends(ma_backend *list, int maxn);
bool Audio_BackendFromName(const char *name, ma_backend *out);
void Audio_InitContextConfig(ma_context_config *cc);
void Audio_ReadSettings(AudioSettings *st);
void Audio_WriteSettings(const AudioSettings *st);

#endif
