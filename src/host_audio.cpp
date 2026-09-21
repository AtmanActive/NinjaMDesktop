/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// Audio I/O. Inside REAPER, ReaNINJAM's audio callback (audiostream_onsamples) is
// driven by the VST processReplacing call. Here a miniaudio duplex device drives it:
// WASAPI/DirectSound on Windows, Core Audio on macOS, PulseAudio/ALSA/JACK on Linux.

#ifdef _WIN32
#include <windows.h>
#else
#include "WDL/swell/swell.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "miniaudio.h"

#include "winclient.h"
#include "host.h"
#include "host_audio.h"

// winclient.cpp
void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, bool isPlaying, bool isSeek, double curpos);

#define NJD_BLOCK 512 // ReaNINJAM gets called with at most this many frames at a time

static struct
{
  ma_context ctx;
  bool ctx_ok;
  ma_device dev;
  bool dev_ok;
  int dev_in_nch, dev_out_nch;
  float *storage;
  float *in_ptrs[MAX_INPUTS];
  float *out_ptrs[MAX_OUTPUTS];
  WDL_FastString status;
} s_audio;

// ---------------------------------------------------------------------------
// settings / enumeration helpers (shared with the configuration dialog)
// ---------------------------------------------------------------------------
int Audio_GetBackends(ma_backend *list, int maxn)
{
  ma_backend all[MA_BACKEND_COUNT];
  size_t cnt = 0;
  if (ma_get_enabled_backends(all, MA_BACKEND_COUNT, &cnt) != MA_SUCCESS) return 0;
  int n = 0;
  for (size_t i = 0; i < cnt && n < maxn; i++)
    if (all[i] != ma_backend_null && all[i] != ma_backend_custom) list[n++] = all[i];
  return n;
}

bool Audio_BackendFromName(const char *name, ma_backend *out)
{
  if (!name || !*name) return false;
  ma_backend list[MA_BACKEND_COUNT];
  const int n = Audio_GetBackends(list, MA_BACKEND_COUNT);
  for (int i = 0; i < n; i++)
    if (!stricmp(ma_get_backend_name(list[i]), name)) { *out = list[i]; return true; }
  return false;
}

void Audio_InitContextConfig(ma_context_config *cc)
{
  *cc = ma_context_config_init();
  cc->pulse.pApplicationName = NJD_APP_NAME;
  cc->jack.pClientName = NJD_APP_NAME;
}

void Audio_ReadSettings(AudioSettings *st)
{
  const char *ini = g_ini_file.Get();
  GetPrivateProfileString(NJD_AUDIO_SEC, "backend", "", st->backend, sizeof(st->backend), ini);
  GetPrivateProfileString(NJD_AUDIO_SEC, "indev", "", st->indev, sizeof(st->indev), ini);
  GetPrivateProfileString(NJD_AUDIO_SEC, "outdev", "", st->outdev, sizeof(st->outdev), ini);
  st->srate = GetPrivateProfileInt(NJD_AUDIO_SEC, "srate", 0, ini);
  st->bsize = GetPrivateProfileInt(NJD_AUDIO_SEC, "bsize", 256, ini);
  if (st->srate < 0 || st->srate > 384000) st->srate = 0;
  if (st->bsize < 16 || st->bsize > 8192) st->bsize = 256;
}

void Audio_WriteSettings(const AudioSettings *st)
{
  const char *ini = g_ini_file.Get();
  char buf[64];
  WritePrivateProfileString(NJD_AUDIO_SEC, "backend", st->backend, ini);
  WritePrivateProfileString(NJD_AUDIO_SEC, "indev", st->indev, ini);
  WritePrivateProfileString(NJD_AUDIO_SEC, "outdev", st->outdev, ini);
  snprintf(buf, sizeof(buf), "%d", st->srate);
  WritePrivateProfileString(NJD_AUDIO_SEC, "srate", buf, ini);
  snprintf(buf, sizeof(buf), "%d", st->bsize);
  WritePrivateProfileString(NJD_AUDIO_SEC, "bsize", buf, ini);
}

// ---------------------------------------------------------------------------
// the audio callback
// ---------------------------------------------------------------------------
static void data_callback(ma_device *dev, void *pOutput, const void *pInput, ma_uint32 frames)
{
  const float *in = (const float *)pInput;
  float *out = (float *)pOutput;
  const int din = in ? s_audio.dev_in_nch : 0;
  const int dout = s_audio.dev_out_nch;
  const int srate = (int)dev->sampleRate;

  // ReaNINJAM lets the user change its channel counts at any time (Preferences)
  int nin = g_config_num_inputs, nout = g_config_num_outputs;
  if (nin < 1) nin = 1; else if (nin > MAX_INPUTS) nin = MAX_INPUTS;
  if (nout < 1) nout = 1; else if (nout > MAX_OUTPUTS) nout = MAX_OUTPUTS;

  while (frames > 0)
  {
    const int n = frames > NJD_BLOCK ? NJD_BLOCK : (int)frames;

    for (int ch = 0; ch < nin; ch++)
    {
      float *b = s_audio.in_ptrs[ch];
      if (ch < din)
      {
        const float *src = in + ch;
        for (int i = 0; i < n; i++) b[i] = src[i * din];
      }
      else memset(b, 0, n * sizeof(float));
    }

    // isPlaying=false: there is no project timeline, so session-mode channels stay idle
    audiostream_onsamples(s_audio.in_ptrs, nin, s_audio.out_ptrs, nout, n, srate, false, false, -1.0);

    if (out)
    {
      for (int i = 0; i < n; i++)
      {
        float *o = out + i * dout;
        for (int ch = 0; ch < dout; ch++) o[ch] = ch < nout ? s_audio.out_ptrs[ch][i] : 0.0f;
      }
      out += n * dout;
    }
    if (in) in += n * din;
    frames -= n;
  }
}

// ---------------------------------------------------------------------------
// start / stop
// ---------------------------------------------------------------------------
static bool find_device(ma_device_info *list, ma_uint32 cnt, const char *name, ma_device_id *id)
{
  for (ma_uint32 i = 0; i < cnt; i++)
    if (!strcmp(list[i].name, name)) { *id = list[i].id; return true; }
  return false;
}

bool Audio_Start(char *err, int errsz)
{
  Audio_Stop();
  if (err && errsz > 0) err[0] = 0;

  if (!s_audio.storage)
  {
    s_audio.storage = (float *)calloc((MAX_INPUTS + MAX_OUTPUTS) * NJD_BLOCK, sizeof(float));
    for (int i = 0; i < MAX_INPUTS; i++) s_audio.in_ptrs[i] = s_audio.storage + i * NJD_BLOCK;
    for (int i = 0; i < MAX_OUTPUTS; i++) s_audio.out_ptrs[i] = s_audio.storage + (MAX_INPUTS + i) * NJD_BLOCK;
  }

  AudioSettings st;
  Audio_ReadSettings(&st);

  ma_context_config cc;
  Audio_InitContextConfig(&cc);
  ma_backend backend;
  const bool have_backend = Audio_BackendFromName(st.backend, &backend);
  ma_result res = ma_context_init(have_backend ? &backend : NULL, have_backend ? 1 : 0, &cc, &s_audio.ctx);
  if (res != MA_SUCCESS)
  {
    snprintf(err, errsz, "Could not initialize audio system %s: %s", have_backend ? st.backend : "(default)", ma_result_description(res));
    s_audio.status.Set(err);
    return false;
  }
  s_audio.ctx_ok = true;

  const bool want_input = strcmp(st.indev, AUDIO_NO_INPUT) != 0;
  ma_device_id in_id, out_id;
  bool have_in_id = false, have_out_id = false;
  WDL_FastString notes;
  {
    ma_device_info *pinfo = NULL, *cinfo = NULL;
    ma_uint32 pcnt = 0, ccnt = 0;
    if (ma_context_get_devices(&s_audio.ctx, &pinfo, &pcnt, &cinfo, &ccnt) == MA_SUCCESS)
    {
      if (st.outdev[0])
      {
        have_out_id = find_device(pinfo, pcnt, st.outdev, &out_id);
        if (!have_out_id) notes.AppendFormatted(512, "Output \"%s\" not found, using default.\n", st.outdev);
      }
      if (want_input && st.indev[0])
      {
        have_in_id = find_device(cinfo, ccnt, st.indev, &in_id);
        if (!have_in_id) notes.AppendFormatted(512, "Input \"%s\" not found, using default.\n", st.indev);
      }
    }
  }

  ma_device_config dc = ma_device_config_init(want_input ? ma_device_type_duplex : ma_device_type_playback);
  dc.playback.pDeviceID = have_out_id ? &out_id : NULL;
  dc.playback.format = ma_format_f32;
  dc.playback.channels = 0; // device's native channel count
  dc.capture.pDeviceID = have_in_id ? &in_id : NULL;
  dc.capture.format = ma_format_f32;
  dc.capture.channels = 0;
  dc.sampleRate = (ma_uint32)st.srate; // 0 = device default
  dc.periodSizeInFrames = (ma_uint32)st.bsize;
  dc.performanceProfile = ma_performance_profile_low_latency;
  // PulseAudio/PipeWire: miniaudio replaces the device's channel layout with a generic one
  // (AIFF order), which makes PipeWire remix multichannel interfaces by channel position.
  // Unpositioned AUX channels are passed through in order instead, so channel N of the
  // stream is channel N of the device, as ReaNINJAM's "Input N"/"Output N" expect.
  dc.pulse.channelMap = 2; // PA_CHANNEL_MAP_AUX
  dc.pulse.pStreamNameCapture = "NINJAM input";
  dc.pulse.pStreamNamePlayback = "NINJAM output";
  dc.dataCallback = data_callback;

  res = ma_device_init(&s_audio.ctx, &dc, &s_audio.dev);
  if (res != MA_SUCCESS)
  {
    snprintf(err, errsz, "Could not open audio device (%s): %s", ma_get_backend_name(s_audio.ctx.backend), ma_result_description(res));
    s_audio.status.Set(err);
    ma_context_uninit(&s_audio.ctx);
    s_audio.ctx_ok = false;
    return false;
  }
  s_audio.dev_ok = true;
  s_audio.dev_in_nch = want_input ? (int)s_audio.dev.capture.channels : 0;
  s_audio.dev_out_nch = (int)s_audio.dev.playback.channels;

  res = ma_device_start(&s_audio.dev);
  if (res != MA_SUCCESS)
  {
    snprintf(err, errsz, "Could not start audio device (%s): %s", ma_get_backend_name(s_audio.ctx.backend), ma_result_description(res));
    Audio_Stop();
    s_audio.status.Set(err);
    return false;
  }

  const ma_uint32 period = s_audio.dev.playback.internalPeriodSizeInFrames;
  s_audio.status.SetFormatted(2048, "%s, %u Hz, %u-frame buffer (%.1f ms)\nIn: %s (%d ch)\nOut: %s (%d ch)",
      ma_get_backend_name(s_audio.ctx.backend),
      s_audio.dev.sampleRate, period, s_audio.dev.sampleRate ? period * 1000.0 / s_audio.dev.sampleRate : 0.0,
      want_input ? s_audio.dev.capture.name : "none", s_audio.dev_in_nch,
      s_audio.dev.playback.name, s_audio.dev_out_nch);
  if (notes.GetLength())
  {
    s_audio.status.Insert("\n", 0);
    s_audio.status.Insert(notes.Get(), 0);
  }
  return true;
}

void Audio_Stop()
{
  if (s_audio.dev_ok)
  {
    ma_device_uninit(&s_audio.dev);
    s_audio.dev_ok = false;
  }
  if (s_audio.ctx_ok)
  {
    ma_context_uninit(&s_audio.ctx);
    s_audio.ctx_ok = false;
  }
  s_audio.status.Set("Audio stopped.");
}

bool Audio_IsRunning() { return s_audio.dev_ok; }

const char *Audio_GetStatusText() { return s_audio.status.Get(); }
