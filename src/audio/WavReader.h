#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include "audio/AudioEngine.h"

namespace peek::audio {

enum class WavStatus {
    Ok,
    // Not a RIFF/WAVE image at all: an mp3 that someone renamed, or an RF64/BW64 file.
    // The caller should hand the same bytes to a real decoder.
    NotRiffWave,
    // RIFF/WAVE, but the chunk list does not yield a usable fmt + data pair.
    Malformed,
    // Well-formed WAVE carrying something other than linear PCM or IEEE float -- ADPCM,
    // A-law, mu-law, MP3-in-WAV. Media Foundation decodes those; we do not.
    UnsupportedCodec,
};

std::wstring_view describeWavStatus(WavStatus status);

// Walks the chunk list of an in-memory RIFF/WAVE image and converts its samples to the
// 16-bit PCM the engine plays. `out` is left untouched unless the result is Ok.
//
// Pure computation over a caller-owned buffer: safe to call from any thread.
WavStatus parseWav(std::span<std::uint8_t const> image, PcmClip& out);

}  // namespace peek::audio
