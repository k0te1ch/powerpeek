#include "audio/WavReader.h"

#include <mmreg.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace peek::audio {
namespace {

struct WaveView {
    WAVEFORMATEXTENSIBLE format{};
    bool isFloat = false;
    std::uint8_t const* data = nullptr;
    std::size_t bytes = 0;
};

std::uint32_t readU32(std::uint8_t const* p) noexcept {
    std::uint32_t value = 0;
    std::memcpy(&value, p, sizeof(value));
    return value;
}

bool idIs(std::uint8_t const* p, char const* id) noexcept {
    return std::memcmp(p, id, 4) == 0;
}

// Chunk order is not guaranteed and LIST/fact/cue/id3 chunks may precede `fmt `, so the
// whole list is walked instead of reading from fixed offsets.
WavStatus locateChunks(std::span<std::uint8_t const> image, WaveView& view) {
    if (image.size() < 12) {
        return WavStatus::Malformed;
    }
    std::uint8_t const* const base = image.data();
    if (idIs(base, "RF64") || idIs(base, "BW64")) {
        // The >4 GB RIFF replacements carry their real sizes in a ds64 chunk. Media
        // Foundation understands them; this parser deliberately does not.
        return WavStatus::NotRiffWave;
    }
    if (!idIs(base, "RIFF") || !idIs(base + 8, "WAVE")) {
        return WavStatus::NotRiffWave;
    }

    // Trust whichever of the declared RIFF size and the real buffer size is smaller, so a
    // truncated download cannot walk us off the end.
    std::uint32_t const declared = readU32(base + 4);
    std::size_t const end = (declared != 0 && static_cast<std::size_t>(declared) + 8 < image.size())
                                ? static_cast<std::size_t>(declared) + 8
                                : image.size();

    bool haveFormat = false;
    bool haveData = false;
    std::size_t offset = 12;
    while (offset + 8 <= end) {
        std::uint8_t const* const id = base + offset;
        std::uint32_t const size = readU32(base + offset + 4);
        std::size_t const body = offset + 8;
        if (body + size > end) {
            if (idIs(id, "data") && haveFormat) {
                view.data = base + body;
                view.bytes = end - body;
                haveData = true;
            }
            break;
        }
        if (idIs(id, "fmt ") && size >= 16) {
            std::size_t const copy = (std::min)(static_cast<std::size_t>(size),
                                                sizeof(WAVEFORMATEXTENSIBLE));
            view.format = WAVEFORMATEXTENSIBLE{};
            std::memcpy(&view.format, base + body, copy);
            if (size == 16) {
                // A 16-byte fmt chunk is a PCMWAVEFORMAT, which has no cbSize field at
                // all -- whatever landed there came from the next chunk's header.
                view.format.Format.cbSize = 0;
            }
            haveFormat = true;
        } else if (idIs(id, "data")) {
            view.data = base + body;
            view.bytes = size;
            haveData = true;
        }
        // Odd-sized chunks are followed by a pad byte that is not counted in the size.
        // Skipping it desynchronises the walk on the first odd LIST chunk.
        offset = body + size + (size & 1u);
    }

    return (haveFormat && haveData) ? WavStatus::Ok : WavStatus::Malformed;
}

WavStatus classifyFormat(WaveView& view) {
    WAVEFORMATEX const& format = view.format.Format;
    if (format.nChannels == 0 || format.nSamplesPerSec == 0) {
        return WavStatus::Malformed;
    }

    if (format.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (format.cbSize < 22) {
            return WavStatus::Malformed;
        }
        if (IsEqualGUID(view.format.SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) {
            view.isFloat = false;
        } else if (IsEqualGUID(view.format.SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            view.isFloat = true;
        } else {
            return WavStatus::UnsupportedCodec;
        }
    } else if (format.wFormatTag == WAVE_FORMAT_PCM) {
        view.isFloat = false;
    } else if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        view.isFloat = true;
    } else {
        return WavStatus::UnsupportedCodec;
    }

    if (view.isFloat) {
        if (format.wBitsPerSample != 32 && format.wBitsPerSample != 64) {
            return WavStatus::Malformed;
        }
    } else if (format.wBitsPerSample != 8 && format.wBitsPerSample != 16 &&
               format.wBitsPerSample != 24 && format.wBitsPerSample != 32) {
        return WavStatus::Malformed;
    }
    return WavStatus::Ok;
}

float decodeSample(std::uint8_t const* p, std::uint16_t bits, bool isFloat) noexcept {
    if (isFloat) {
        if (bits == 32) {
            float value = 0.0f;
            std::memcpy(&value, p, sizeof(value));
            return value;
        }
        double value = 0.0;
        std::memcpy(&value, p, sizeof(value));
        return static_cast<float>(value);
    }
    switch (bits) {
    case 8:
        // 8-bit WAV PCM is unsigned with a midpoint of 128; reading it as signed produces
        // a loud DC-offset click.
        return (static_cast<float>(p[0]) - 128.0f) / 128.0f;
    case 16: {
        std::int16_t value = 0;
        std::memcpy(&value, p, sizeof(value));
        return static_cast<float>(value) / 32768.0f;
    }
    case 24: {
        std::uint32_t const raw = static_cast<std::uint32_t>(p[0]) |
                                  (static_cast<std::uint32_t>(p[1]) << 8) |
                                  (static_cast<std::uint32_t>(p[2]) << 16);
        std::int32_t const value = static_cast<std::int32_t>((raw ^ 0x800000u) - 0x800000u);
        return static_cast<float>(value) / 8388608.0f;
    }
    default: {
        std::int32_t value = 0;
        std::memcpy(&value, p, sizeof(value));
        return static_cast<float>(static_cast<double>(value) / 2147483648.0);
    }
    }
}

std::int16_t quantise(float sample) noexcept {
    float const clamped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::lrintf(clamped * 32767.0f));
}

void convertToPcm16(WaveView const& view, PcmClip& out) {
    WAVEFORMATEX const& format = view.format.Format;
    std::uint16_t const bits = format.wBitsPerSample;
    std::size_t const sourceBytesPerSample = bits / 8u;
    std::size_t const frameBytes = sourceBytesPerSample * format.nChannels;
    // A truncated final frame would be read as a partial sample; drop it.
    std::size_t const usable = view.bytes - (view.bytes % frameBytes);

    out.sampleRate = format.nSamplesPerSec;
    out.channels = format.nChannels;
    out.bitsPerSample = 16;

    if (bits == 16 && !view.isFloat) {
        out.samples.assign(view.data, view.data + usable);
        return;
    }

    std::size_t const count = usable / sourceBytesPerSample;
    out.samples.resize(count * sizeof(std::int16_t));
    std::uint8_t const* source = view.data;
    for (std::size_t i = 0; i < count; ++i, source += sourceBytesPerSample) {
        std::int16_t const value = quantise(decodeSample(source, bits, view.isFloat));
        std::memcpy(out.samples.data() + i * sizeof(value), &value, sizeof(value));
    }
}

}  // namespace

std::wstring_view describeWavStatus(WavStatus status) {
    switch (status) {
    case WavStatus::Ok:
        return L"ok";
    case WavStatus::NotRiffWave:
        return L"not a RIFF/WAVE file";
    case WavStatus::Malformed:
        return L"malformed or incomplete RIFF/WAVE chunk list";
    case WavStatus::UnsupportedCodec:
        return L"compressed WAVE (not linear PCM or IEEE float)";
    }
    return L"unknown";
}

WavStatus parseWav(std::span<std::uint8_t const> image, PcmClip& out) {
    WaveView view;
    if (WavStatus const status = locateChunks(image, view); status != WavStatus::Ok) {
        return status;
    }
    if (WavStatus const status = classifyFormat(view); status != WavStatus::Ok) {
        return status;
    }

    std::size_t const frameBytes =
        static_cast<std::size_t>(view.format.Format.wBitsPerSample / 8u) *
        view.format.Format.nChannels;
    if (frameBytes == 0 || view.bytes < frameBytes) {
        return WavStatus::Malformed;
    }

    // wValidBitsPerSample may be smaller than the container width in an EXTENSIBLE file;
    // the unused bits are the low ones, so treating the container as the sample width
    // costs at most sub-LSB noise on a notification blip.
    PcmClip decoded;
    convertToPcm16(view, decoded);
    if (!decoded.valid()) {
        return WavStatus::Malformed;
    }
    out = std::move(decoded);
    return WavStatus::Ok;
}

}  // namespace peek::audio
