/*
    NinjaMDesktop - standalone host for Cockos ReaNINJAM
    Copyright (C) 2026 NinjaMDesktop contributors
    Licensed under the GNU General Public License v2 or later (see LICENSE).
*/

// ReaNINJAM builds NJClient with REANINJAM defined, which makes it ask the host
// (normally REAPER) for its Vorbis encoder/decoder. Provide WDL's implementation,
// built against libvorbis/libogg, using the same interface names as njclient.cpp.

#ifdef _WIN32
#include <windows.h>
#else
#include "WDL/swell/swell.h"
#endif

// quality-based VBR constructor: VorbisEncoder(srate, nch, qv, serno)
#define VORBISENC_WANT_QVAL
#define VorbisEncoderInterface I_NJEncoder
#define VorbisDecoderInterface I_NJDecoder
#include "WDL/vorbisencdec.h"
#undef VorbisEncoderInterface
#undef VorbisDecoderInterface

void *Host_CreateVorbisEncoder(int srate, int nch, int serno, float qv, int cbr, int minbr, int maxbr)
{
  // NJClient always requests VBR (cbr/minbr/maxbr = -1) with a quality derived from the
  // channel's bitrate; WDL's full-config constructor does vorbis_encode_init_vbr() in that case.
  // Errors are reported via isError(), as in NJClient's non-REANINJAM build.
  return static_cast<I_NJEncoder *>(new VorbisEncoder(srate, nch, qv, serno));
}

void *Host_CreateVorbisDecoder()
{
  return static_cast<I_NJDecoder *>(new VorbisDecoder);
}
