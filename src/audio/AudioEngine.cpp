#include "audio/AudioEngine.h"

#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include "audio/MediaDecoder.h"
#include "audio/WavReader.h"
#include "core/Logger.h"

namespace peek::audio {
namespace {

// Eight overlapping notification blips is already more than a user can tell apart;
// beyond that, dropping is better than stuttering.
constexpr std::size_t kMaxConcurrentVoices = 8;

// A sound file larger than this is not a notification sound.
constexpr std::uintmax_t kMaxSoundFileBytes = 32u * 1024u * 1024u;

// How long the engine waits before trying the audio endpoint again after a failure, so a
// machine with no sound card does not pay for a device enumeration on every event.
constexpr std::uint64_t kEndpointRetryMs = 30'000;

// Slack added to a clip's own length before the reaper looks at it, and the interval it
// re-checks at while anything is still sounding.
constexpr double kReapSlackSeconds = 0.15;
constexpr double kReapPollSeconds = 0.25;

// Nothing depends on the reaper being punctual, so it is given a wide coalescing window.
constexpr DWORD kReapWindowMs = 100;

// xaudio2.h defines XAUDIO2_USE_DEFAULT_PROCESSOR only when NTDDI_VERSION >= 19H1 and this
// project pins NTDDI to Windows 10 RTM, so the value is spelled out. The macro that is
// visible, XAUDIO2_DEFAULT_PROCESSOR, is Processor1 -- it pins the audio thread to core 0.
constexpr XAUDIO2_PROCESSOR kDefaultProcessor = 0;

std::uint32_t formatKey(PcmClip const& clip) noexcept {
    return (static_cast<std::uint32_t>(clip.channels) << 16) | clip.bitsPerSample;
}

WAVEFORMATEX waveFormatFor(PcmClip const& clip) noexcept {
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = clip.channels;
    format.nSamplesPerSec = clip.sampleRate;
    format.wBitsPerSample = clip.bitsPerSample;
    format.nBlockAlign = static_cast<WORD>(clip.channels * (clip.bitsPerSample / 8u));
    format.nAvgBytesPerSec = clip.sampleRate * format.nBlockAlign;
    format.cbSize = 0;
    return format;
}

bool readWholeFile(std::filesystem::path const& file, std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    std::uintmax_t const size = std::filesystem::file_size(file, ec);
    if (ec) {
        log::warning(L"Sound file {} cannot be read: {}", file.wstring(),
                     widen(ec.message()));
        return false;
    }
    if (size == 0 || size > kMaxSoundFileBytes) {
        log::warning(L"Sound file {} has an implausible size ({} bytes)", file.wstring(), size);
        return false;
    }

    std::ifstream stream(file, std::ios::binary);
    if (!stream) {
        log::warning(L"Sound file {} could not be opened", file.wstring());
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (stream.gcount() != static_cast<std::streamsize>(size)) {
        log::warning(L"Sound file {} ended early while reading", file.wstring());
        return false;
    }
    return true;
}

}  // namespace

double PcmClip::durationSeconds() const noexcept {
    if (!valid()) {
        return 0.0;
    }
    std::size_t const frameBytes = static_cast<std::size_t>(channels) * (bitsPerSample / 8u);
    if (frameBytes == 0) {
        return 0.0;
    }
    return static_cast<double>(samples.size() / frameBytes) / sampleRate;
}

PcmClip decodeFile(std::filesystem::path const& file) {
    std::vector<std::uint8_t> bytes;
    if (!readWholeFile(file, bytes)) {
        return {};
    }

    PcmClip clip;
    WavStatus const status = parseWav(bytes, clip);
    if (status == WavStatus::Ok) {
        return clip;
    }
    if (status == WavStatus::Malformed) {
        log::warning(L"Sound file {} is a broken WAV ({}); no sound will play",
                     file.wstring(), describeWavStatus(status));
        return {};
    }

    log::debug(L"{} is {}; handing it to Media Foundation", file.wstring(),
               describeWavStatus(status));
    if (!decodeWithMediaFoundation(file, clip)) {
        return {};
    }
    return clip;
}

PcmClip decodeResource(int resourceId) {
    HMODULE const module = GetModuleHandleW(nullptr);
    HRSRC const found =
        FindResourceW(module, MAKEINTRESOURCEW(static_cast<WORD>(resourceId)), RT_RCDATA);
    if (found == nullptr) {
        log::error(L"Built-in sound {} is missing from the executable: {}", resourceId,
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        return {};
    }

    // Resource blobs are part of the mapped image: they need no unlock and no free, and
    // they stay valid for as long as the module is loaded.
    HGLOBAL const loaded = LoadResource(module, found);
    DWORD const size = SizeofResource(module, found);
    void const* const data = (loaded != nullptr) ? LockResource(loaded) : nullptr;
    if (data == nullptr || size == 0) {
        log::error(L"Built-in sound {} could not be locked: {}", resourceId,
                   describeHresult(HRESULT_FROM_WIN32(GetLastError())));
        return {};
    }

    PcmClip clip;
    WavStatus const status =
        parseWav({static_cast<std::uint8_t const*>(data), size}, clip);
    if (status != WavStatus::Ok) {
        log::error(L"Built-in sound {} did not parse: {}", resourceId,
                   describeWavStatus(status));
        return {};
    }
    return clip;
}

// Every member is guarded by `mutex` unless it is atomic. The mutex is never held across
// anything that waits on the XAudio2 audio thread except DestroyVoice during teardown,
// which is bounded by one 10 ms quantum.
struct AudioEngine::Impl final : IXAudio2EngineCallback {
    struct Playing {
        IXAudio2SourceVoice* voice = nullptr;
        std::uint32_t key = 0;
        // XAudio2 reads pAudioData asynchronously, long after play() has returned, while
        // the PcmClip the caller passed by reference may be gone by then.
        std::vector<std::uint8_t> samples;
    };

    Impl() {
        m_reaper = CreateThreadpoolTimer(&Impl::onReapTimer, this, nullptr);
        if (m_reaper == nullptr) {
            log::warning(L"No threadpool timer for audio: finished voices will only be "
                         L"recycled on the next sound");
        }
        m_playing.reserve(kMaxConcurrentVoices);
    }

    virtual ~Impl() {
        cancelReaper();
        if (m_reaper != nullptr) {
            CloseThreadpoolTimer(m_reaper);
        }
    }

    Impl(Impl const&) = delete;
    Impl& operator=(Impl const&) = delete;

    std::mutex mutex;

    bool start() {
        if (m_deviceLost.exchange(false)) {
            HRESULT const error = m_criticalError.exchange(S_OK);
            log::warning(L"The audio endpoint went away ({}); rebuilding the engine",
                         describeHresult(error));
            teardown();
            // A headset being unplugged must not cost the user the retry window that a
            // machine without a sound card earns.
            m_nextStartTick = 0;
        }
        // The mastering voice is only ever null when the engine is.
        if (m_engine) {
            return true;
        }

        std::uint64_t const now = GetTickCount64();
        if (now < m_nextStartTick) {
            return false;
        }

        // XAUDIO2_STOP_ENGINE_WHEN_IDLE lets the audio thread and the audio session go
        // away between notifications, which is what a tray application should do; the
        // price is that endpoint loss surfaces as a failing Start() rather than as
        // OnCriticalError, so play() treats both the same way.
        HRESULT hr =
            XAudio2Create(m_engine.put(), XAUDIO2_STOP_ENGINE_WHEN_IDLE, kDefaultProcessor);
        if (FAILED(hr)) {
            m_nextStartTick = now + kEndpointRetryMs;
            log::error(L"XAudio2 is unavailable: {}", describeHresult(hr));
            return false;
        }
        m_engine->RegisterForCallbacks(this);

        // Passing the defaults and a null device id is what keeps XAudio2's virtual audio
        // client following the default endpoint; pinning a rate here is what breaks the
        // follow when the new endpoint is a 44.1 kHz-only headset.
        hr = m_engine->CreateMasteringVoice(&m_master, XAUDIO2_DEFAULT_CHANNELS,
                                            XAUDIO2_DEFAULT_SAMPLERATE, 0, nullptr, nullptr,
                                            AudioCategory_SoundEffects);
        if (FAILED(hr)) {
            m_nextStartTick = now + kEndpointRetryMs;
            log::warning(L"No usable audio output device: {}", describeHresult(hr));
            m_engine->UnregisterForCallbacks(this);
            m_engine = nullptr;
            return false;
        }
        return true;
    }

    void teardown() {
        if (m_engine) {
            m_engine->StopEngine();
        }
        for (Playing& playing : m_playing) {
            destroyVoice(playing.voice);
        }
        m_playing.clear();
        for (auto& [key, voices] : m_idle) {
            for (IXAudio2SourceVoice* voice : voices) {
                destroyVoice(voice);
            }
        }
        m_idle.clear();
        if (m_master != nullptr) {
            m_master->DestroyVoice();
            m_master = nullptr;
        }
        if (m_engine) {
            m_engine->UnregisterForCallbacks(this);
            m_engine = nullptr;
        }
    }

    // Returns finished voices to the pool. A voice is finished once its queue has drained;
    // stopping it is also what allows the engine to reach its idle state.
    void reap() {
        std::erase_if(m_playing, [this](Playing& playing) {
            XAUDIO2_VOICE_STATE state{};
            playing.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued != 0) {
                return false;
            }
            playing.voice->Stop(0);
            playing.voice->FlushSourceBuffers();
            m_idle[playing.key].push_back(playing.voice);
            return true;
        });
    }

    void stopEverything() {
        for (Playing& playing : m_playing) {
            playing.voice->Stop(0);
            playing.voice->FlushSourceBuffers();
            m_idle[playing.key].push_back(playing.voice);
        }
        m_playing.clear();
    }

    bool atVoiceLimit() const { return m_playing.size() >= kMaxConcurrentVoices; }

    IXAudio2SourceVoice* acquireVoice(PcmClip const& clip) {
        std::uint32_t const key = formatKey(clip);
        if (auto it = m_idle.find(key); it != m_idle.end() && !it->second.empty()) {
            IXAudio2SourceVoice* const voice = it->second.back();
            it->second.pop_back();
            // Channel count and bit depth are fixed at creation, but the source rate can
            // be retuned as long as nothing is queued -- and reap() flushed this voice.
            if (SUCCEEDED(voice->SetSourceSampleRate(clip.sampleRate))) {
                return voice;
            }
            destroyVoice(voice);
        }

        WAVEFORMATEX const format = waveFormatFor(clip);
        IXAudio2SourceVoice* voice = nullptr;
        HRESULT const hr = m_engine->CreateSourceVoice(&voice, &format);
        if (FAILED(hr)) {
            log::warning(L"Could not create a source voice for {} Hz / {} channels: {}",
                         clip.sampleRate, clip.channels, describeHresult(hr));
            return nullptr;
        }
        return voice;
    }

    void submit(IXAudio2SourceVoice* voice, PcmClip const& clip, float volume) {
        Playing playing{voice, formatKey(clip), clip.samples};

        XAUDIO2_BUFFER buffer{};
        // Without XAUDIO2_END_OF_STREAM the voice never reports an empty queue and the
        // pool leaks a voice per sound.
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.AudioBytes = static_cast<UINT32>(playing.samples.size());
        buffer.pAudioData = playing.samples.data();

        voice->SetVolume(volume);
        HRESULT hr = voice->SubmitSourceBuffer(&buffer);
        if (SUCCEEDED(hr)) {
            hr = voice->Start(0);
        }
        if (FAILED(hr)) {
            destroyVoice(voice);
            m_criticalError.store(hr, std::memory_order_relaxed);
            m_deviceLost.store(true, std::memory_order_release);
            log::warning(L"Playback failed ({}); the engine will be rebuilt for the next "
                         L"sound", describeHresult(hr));
            return;
        }

        m_playing.push_back(std::move(playing));
        armReaper(clip.durationSeconds() + kReapSlackSeconds);
    }

    // While set, the reaper does not re-arm itself, which is what makes cancelReaper()
    // final rather than a race against the callback.
    void setStopping(bool stopping) { m_stopping = stopping; }

    // Must be called with `mutex` released: the callback it waits for takes the mutex.
    void cancelReaper() {
        if (m_reaper == nullptr) {
            return;
        }
        SetThreadpoolTimer(m_reaper, nullptr, 0, 0);
        WaitForThreadpoolTimerCallbacks(m_reaper, TRUE);
    }

    float masterVolume() const noexcept { return m_masterVolume.load(std::memory_order_relaxed); }
    void setMasterVolume(float volume) noexcept {
        m_masterVolume.store(std::clamp(volume, 0.0f, 1.0f), std::memory_order_relaxed);
    }

private:
    static void CALLBACK onReapTimer(PTP_CALLBACK_INSTANCE, void* context, PTP_TIMER) {
        auto* const self = static_cast<Impl*>(context);
        std::lock_guard const lock(self->mutex);
        self->reap();
        if (!self->m_stopping && !self->m_playing.empty()) {
            self->armReaper(kReapPollSeconds);
        }
    }

    void armReaper(double seconds) {
        if (m_reaper == nullptr || m_stopping) {
            return;
        }
        LARGE_INTEGER due{};
        // A negative FILETIME is relative to now, in 100 ns units.
        due.QuadPart = -static_cast<LONGLONG>(seconds * 10'000'000.0);
        FILETIME when{due.LowPart, static_cast<DWORD>(due.HighPart)};
        SetThreadpoolTimer(m_reaper, &when, 0, kReapWindowMs);
    }

    static void destroyVoice(IXAudio2SourceVoice* voice) {
        voice->Stop(0);
        voice->FlushSourceBuffers();
        // Blocks until the audio thread is idle, which is why nothing calls this from an
        // XAudio2 callback.
        voice->DestroyVoice();
    }

    void STDMETHODCALLTYPE OnProcessingPassStart() noexcept override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() noexcept override {}

    // Audio thread. Calling into XAudio2 here deadlocks, and taking the mutex would
    // deadlock against a UI thread that is already inside DestroyVoice, so this records
    // the failure and lets the next play() rebuild.
    void STDMETHODCALLTYPE OnCriticalError(HRESULT error) noexcept override {
        m_criticalError.store(error, std::memory_order_relaxed);
        m_deviceLost.store(true, std::memory_order_release);
    }

    com_ptr<IXAudio2> m_engine;
    IXAudio2MasteringVoice* m_master = nullptr;
    std::unordered_map<std::uint32_t, std::vector<IXAudio2SourceVoice*>> m_idle;
    std::vector<Playing> m_playing;
    PTP_TIMER m_reaper = nullptr;
    bool m_stopping = false;
    std::uint64_t m_nextStartTick = 0;
    std::atomic<bool> m_deviceLost{false};
    std::atomic<HRESULT> m_criticalError{S_OK};
    std::atomic<float> m_masterVolume{1.0f};
};

AudioEngine& AudioEngine::instance() {
    static AudioEngine engine;
    return engine;
}

AudioEngine::AudioEngine() : m_impl(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() {
    shutdown();
}

bool AudioEngine::ensureStarted() {
    std::lock_guard const lock(m_impl->mutex);
    return m_impl->start();
}

void AudioEngine::shutdown() {
    {
        std::lock_guard const lock(m_impl->mutex);
        m_impl->setStopping(true);
    }
    m_impl->cancelReaper();

    std::lock_guard const lock(m_impl->mutex);
    m_impl->setStopping(false);
    m_impl->teardown();
}

// Cheap enough for a window procedure: it decodes nothing, allocates one copy of an
// already-decoded clip, and only touches XAudio2 calls that do not wait on the audio
// thread. The one exception is the first call, which builds the engine (~5 ms) -- call
// ensureStarted() at startup to move that off the first notification.
void AudioEngine::play(PcmClip const& clip, float volume) {
    if (!clip.valid()) {
        return;
    }

    std::lock_guard const lock(m_impl->mutex);
    if (!m_impl->start()) {
        return;
    }
    m_impl->reap();
    if (m_impl->atVoiceLimit()) {
        log::debug(L"Dropping a notification sound: {} already playing", kMaxConcurrentVoices);
        return;
    }

    IXAudio2SourceVoice* const voice = m_impl->acquireVoice(clip);
    if (voice == nullptr) {
        return;
    }
    m_impl->submit(voice, clip, std::clamp(volume, 0.0f, 1.0f) * m_impl->masterVolume());
}

void AudioEngine::setMasterVolume(float volume) {
    m_impl->setMasterVolume(volume);
}

float AudioEngine::masterVolume() const noexcept {
    return m_impl->masterVolume();
}

void AudioEngine::stopAll() {
    std::lock_guard const lock(m_impl->mutex);
    m_impl->stopEverything();
}

}  // namespace peek::audio
