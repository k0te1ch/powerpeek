#pragma once

#include <filesystem>

#include "audio/AudioEngine.h"

namespace peek::audio {

// Decodes anything Media Foundation has a codec for -- mp3, m4a/AAC, wma, flac on 1703+,
// and the compressed WAV variants this project's own parser rejects -- into 16-bit PCM.
//
// Blocking and comparatively expensive: it starts Media Foundation, reads the whole file
// through IMFSourceReader and shuts it down again, which is tens of milliseconds for a
// short clip and unbounded for a file the user mispicked. Call it from a worker thread,
// never from a window procedure. Returns false and logs the reason on failure.
bool decodeWithMediaFoundation(std::filesystem::path const& file, PcmClip& out);

}  // namespace peek::audio
