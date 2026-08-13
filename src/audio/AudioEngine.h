#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

#include "core/Win.h"

namespace peek::audio {

// Decoded 16-bit PCM ready to hand to a source voice.
struct PcmClip {
    std::vector<std::uint8_t> samples;
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;

    bool valid() const noexcept { return !samples.empty() && sampleRate != 0 && channels != 0; }
    double durationSeconds() const noexcept;
};

// Decodes a sound file into PCM.
//
// WAV is parsed directly because the overwhelmingly common case should not need Media
// Foundation spun up; anything else (mp3, m4a, flac, ogg where a codec is installed) goes
// through IMFSourceReader. Returns an invalid clip on failure and logs why -- a user who
// points the setting at a broken file gets silence and a log line, not a crash.
PcmClip decodeFile(std::filesystem::path const& file);

// Same, for a WAV embedded in the executable as an RCDATA resource.
PcmClip decodeResource(int resourceId);

// XAudio2 playback.
//
// Voices are pooled per source format: creating an IXAudio2SourceVoice is comparatively
// expensive and notification sounds are short and repetitive, so a voice that finished
// playing is kept for the next clip with the same format. The engine tolerates the
// default audio endpoint disappearing -- OnCriticalError tears the engine down and the
// next play() rebuilds it, which is what happens when a USB headset is unplugged.
class AudioEngine {
public:
    static AudioEngine& instance();

    // Lazily initialised on first play(); returns false if there is no audio device at
    // all, in which case play() is a no-op rather than an error.
    bool ensureStarted();
    void shutdown();

    // `volume` is linear 0..1 and is applied on top of the master volume.
    void play(PcmClip const& clip, float volume);

    void setMasterVolume(float volume);
    float masterVolume() const noexcept;

    // Cuts anything currently sounding; used when the user changes a sound while
    // previewing it.
    void stopAll();

private:
    AudioEngine();
    ~AudioEngine();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace peek::audio
