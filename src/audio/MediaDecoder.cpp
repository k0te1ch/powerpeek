#include "audio/MediaDecoder.h"

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <vector>

#include "core/Logger.h"

namespace peek::audio {
namespace {

// A user who points the setting at a film gets one truncated notification sound rather
// than a gigabyte of decoded PCM.
constexpr std::size_t kMaxDecodedBytes = 64u * 1024u * 1024u;

// The MF_SOURCE_READER_* selectors are an unscoped signed enum; the reader takes DWORD.
constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kFirstAudioStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);

struct ComScope {
    HRESULT const hr;

    ComScope() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        // RPC_E_CHANGED_MODE means somebody else already made this an STA thread and our
        // call did not take a reference -- uninitialising then would unbalance theirs.
        if (SUCCEEDED(hr)) {
            CoUninitialize();
        }
    }

    ComScope(ComScope const&) = delete;
    ComScope& operator=(ComScope const&) = delete;
};

// Started and shut down around a single decode rather than once per process. That pairs
// the two calls on one thread and guarantees no source reader can outlive the thread that
// started Media Foundation, which is the documented way to hang on exit.
struct MediaFoundationScope {
    HRESULT const hr;

    MediaFoundationScope() : hr(MFStartup(MF_VERSION, MFSTARTUP_LITE)) {}
    ~MediaFoundationScope() {
        if (SUCCEEDED(hr)) {
            MFShutdown();
        }
    }

    MediaFoundationScope(MediaFoundationScope const&) = delete;
    MediaFoundationScope& operator=(MediaFoundationScope const&) = delete;
};

// Over-specifying the wanted type is the most common cause of MF_E_INVALIDMEDIATYPE here,
// so the major type and subtype are asked for alone first and the bit depth is only
// pinned as a fallback.
bool selectPcmOutput(IMFSourceReader* reader, bool pinBitDepth) {
    com_ptr<IMFMediaType> wanted;
    if (FAILED(MFCreateMediaType(wanted.put()))) {
        return false;
    }
    if (FAILED(wanted->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio)) ||
        FAILED(wanted->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM))) {
        return false;
    }
    if (pinBitDepth && FAILED(wanted->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16))) {
        return false;
    }
    return SUCCEEDED(reader->SetCurrentMediaType(kFirstAudioStream, nullptr, wanted.get()));
}

bool readOutputFormat(IMFSourceReader* reader, std::uint32_t& sampleRate,
                      std::uint16_t& channels) {
    com_ptr<IMFMediaType> actual;
    HRESULT hr = reader->GetCurrentMediaType(kFirstAudioStream, actual.put());
    if (FAILED(hr)) {
        log::warning(L"Media Foundation would not report the decoded audio type: {}",
                     describeHresult(hr));
        return false;
    }

    WAVEFORMATEX* format = nullptr;
    UINT32 formatBytes = 0;
    hr = MFCreateWaveFormatExFromMFMediaType(actual.get(), &format, &formatBytes,
                                             MFWaveFormatExConvertFlag_Normal);
    if (FAILED(hr) || format == nullptr) {
        log::warning(L"Media Foundation returned an audio type with no WAVEFORMATEX: {}",
                     describeHresult(hr));
        return false;
    }
    sampleRate = format->nSamplesPerSec;
    channels = format->nChannels;
    std::uint16_t const bits = format->wBitsPerSample;
    CoTaskMemFree(format);  // MFCreateWaveFormatExFromMFMediaType allocates with CoTaskMemAlloc

    if (sampleRate == 0 || channels == 0 || bits != 16) {
        log::warning(L"Media Foundation decoded to an unusable format: {} Hz, {} channels, {} bits",
                     sampleRate, channels, bits);
        return false;
    }
    return true;
}

bool readAllSamples(IMFSourceReader* reader, std::vector<std::uint8_t>& samples) {
    for (;;) {
        DWORD flags = 0;
        com_ptr<IMFSample> sample;
        HRESULT const hr =
            reader->ReadSample(kFirstAudioStream, 0, nullptr, &flags, nullptr, sample.put());
        if (FAILED(hr)) {
            log::warning(L"Decoding stopped on a read error: {}", describeHresult(hr));
            return false;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            break;
        }
        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            log::warning(L"Decoding stopped: the stream changed format mid-file");
            return false;
        }
        if (!sample) {
            // S_OK with no sample and no end-of-stream flag is a gap, not the end.
            continue;
        }

        com_ptr<IMFMediaBuffer> buffer;
        HRESULT bufferHr = sample->ConvertToContiguousBuffer(buffer.put());
        BYTE* data = nullptr;
        DWORD bytes = 0;
        if (SUCCEEDED(bufferHr)) {
            bufferHr = buffer->Lock(&data, nullptr, &bytes);
        }
        if (FAILED(bufferHr)) {
            log::warning(L"Decoding stopped: a decoded block could not be read ({})",
                         describeHresult(bufferHr));
            return false;
        }
        samples.insert(samples.end(), data, data + bytes);
        buffer->Unlock();

        if (samples.size() > kMaxDecodedBytes) {
            log::warning(L"Sound is longer than {} MB decoded; keeping only the start",
                         kMaxDecodedBytes / (1024u * 1024u));
            break;
        }
    }
    return !samples.empty();
}

}  // namespace

bool decodeWithMediaFoundation(std::filesystem::path const& file, PcmClip& out) {
    ComScope const com;
    if (FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) {
        log::error(L"COM is unavailable on this thread: {}", describeHresult(com.hr));
        return false;
    }
    MediaFoundationScope const mediaFoundation;
    if (FAILED(mediaFoundation.hr)) {
        log::error(L"Media Foundation failed to start: {}", describeHresult(mediaFoundation.hr));
        return false;
    }

    com_ptr<IMFAttributes> attributes;
    if (FAILED(MFCreateAttributes(attributes.put(), 1)) ||
        FAILED(attributes->SetUINT32(MF_LOW_LATENCY, TRUE))) {
        log::error(L"Could not configure the Media Foundation source reader");
        return false;
    }

    com_ptr<IMFSourceReader> reader;
    HRESULT hr = MFCreateSourceReaderFromURL(file.c_str(), attributes.get(), reader.put());
    if (FAILED(hr)) {
        log::warning(L"No installed codec can open {}: {}", file.wstring(), describeHresult(hr));
        return false;
    }

    // Without deselecting everything first we would also decode the cover-art stream that
    // an m4a or an ID3-tagged mp3 carries.
    hr = reader->SetStreamSelection(kAllStreams, FALSE);
    if (SUCCEEDED(hr)) {
        hr = reader->SetStreamSelection(kFirstAudioStream, TRUE);
    }
    if (FAILED(hr)) {
        log::warning(L"{} has no audio stream: {}", file.wstring(), describeHresult(hr));
        return false;
    }

    if (!selectPcmOutput(reader.get(), false) && !selectPcmOutput(reader.get(), true)) {
        log::warning(L"Media Foundation cannot decode {} to PCM on this machine",
                     file.wstring());
        return false;
    }

    PcmClip decoded;
    decoded.bitsPerSample = 16;
    if (!readOutputFormat(reader.get(), decoded.sampleRate, decoded.channels)) {
        return false;
    }
    decoded.samples.reserve(1u << 20);
    if (!readAllSamples(reader.get(), decoded.samples)) {
        return false;
    }
    if (!decoded.valid()) {
        log::warning(L"{} decoded to nothing", file.wstring());
        return false;
    }

    out = std::move(decoded);
    return true;
}

}  // namespace peek::audio
