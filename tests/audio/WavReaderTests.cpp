// The WAV parser: the chunk walk, the format classification, and the conversion of whatever a
// file happens to hold into the 16-bit PCM the engine plays.
//
// Every byte this unit reads comes from a file the user chose, and it is the only parser in the
// application that runs on attacker-shaped input in the ordinary course of use -- a chunk size
// taken on trust is a read past the end of a buffer the caller owns. Its second job is routing:
// decodeFile hands the same bytes to Media Foundation for every status except Malformed, so the
// difference between "not RIFF/WAVE", "compressed" and "broken" decides whether a file the
// machine could play perfectly well plays at all, or goes silent with a misleading log line.
//
// Every image below is assembled byte by byte in memory. The fmt chunk in particular is laid
// out field by field rather than by copying a WAVEFORMATEXTENSIBLE, so what the parser reads is
// checked against the on-disk layout instead of against the packing mmreg.h applies to that
// struct. Nothing here opens a file or needs an audio device.

#include "TestSupport.h"

#include "audio/WavReader.h"

#include <mmreg.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace {

using peek::audio::PcmClip;
using peek::audio::WavStatus;
using peek::audio::describeWavStatus;
using peek::audio::parseWav;

using Bytes = std::vector<std::uint8_t>;

void append(Bytes& out, Bytes const& more) {
    out.insert(out.end(), more.begin(), more.end());
}

void appendId(Bytes& out, std::string_view id) {
    for (char const c : id) {
        out.push_back(static_cast<std::uint8_t>(c));
    }
}

void appendU16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFu));
}

void appendU32(Bytes& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

// A GUID reaches the disk as its three integer fields little-endian followed by Data4 in
// order, so the bytes of a SubFormat are not the order its text spelling reads in.
void appendGuid(Bytes& out, GUID const& guid) {
    appendU32(out, static_cast<std::uint32_t>(guid.Data1));
    appendU16(out, guid.Data2);
    appendU16(out, guid.Data3);
    for (unsigned char const part : guid.Data4) {
        out.push_back(static_cast<std::uint8_t>(part));
    }
}

// The 8-byte chunk header, the body, and the pad byte RIFF requires after an odd body.
Bytes chunk(std::string_view id, Bytes const& body) {
    Bytes out;
    appendId(out, id);
    appendU32(out, static_cast<std::uint32_t>(body.size()));
    append(out, body);
    if ((body.size() & 1u) != 0u) {
        out.push_back(std::uint8_t{0});
    }
    return out;
}

// A chunk whose declared size is deliberately not its body's, for the truncated downloads and
// corrupt tag chunks the walk has to survive. No pad byte: the caller is spelling out exactly
// what lands on disk.
Bytes chunkSized(std::string_view id, std::uint32_t declaredSize, Bytes const& body) {
    Bytes out;
    appendId(out, id);
    appendU32(out, declaredSize);
    append(out, body);
    return out;
}

// `declaredSize` overrides the RIFF size field, which real writers get wrong in both
// directions: a streaming writer leaves zero behind, a truncated download leaves it too large.
Bytes riffImage(std::string_view riffId, std::string_view formType,
                std::vector<Bytes> const& parts,
                std::optional<std::uint32_t> declaredSize = std::nullopt) {
    Bytes payload;
    appendId(payload, formType);
    for (Bytes const& part : parts) {
        append(payload, part);
    }

    Bytes image;
    appendId(image, riffId);
    appendU32(image, declaredSize.value_or(static_cast<std::uint32_t>(payload.size())));
    append(image, payload);
    return image;
}

Bytes riff(std::vector<Bytes> const& parts) {
    return riffImage("RIFF", "WAVE", parts);
}

// The fmt chunk, field by field. An absent optional is a field the writer did not emit at all,
// which is the difference between a 16-byte PCMWAVEFORMAT, an 18-byte WAVEFORMATEX and a
// 40-byte WAVEFORMATEXTENSIBLE.
struct Fmt {
    std::uint16_t tag = WAVE_FORMAT_PCM;
    std::uint16_t channels = 1;
    std::uint32_t rate = 44100;
    std::uint16_t bits = 16;
    // Written verbatim when set, so an image can lie about them the way broken writers do.
    std::optional<std::uint16_t> blockAlign{};
    std::optional<std::uint32_t> byteRate{};
    std::optional<std::uint16_t> cbSize{};
    // Present means the 22-byte extension follows, whatever cbSize claims about it.
    std::uint16_t validBits = 0;
    std::uint32_t channelMask = 0;
    std::optional<GUID> subFormat{};
    // Vendor bytes past the end of WAVEFORMATEXTENSIBLE.
    std::size_t trailing = 0;
};

Bytes fmtBody(Fmt const& fmt) {
    std::uint16_t const frame = static_cast<std::uint16_t>(fmt.channels * (fmt.bits / 8u));

    Bytes body;
    appendU16(body, fmt.tag);
    appendU16(body, fmt.channels);
    appendU32(body, fmt.rate);
    appendU32(body, fmt.byteRate.value_or(fmt.rate * frame));
    appendU16(body, fmt.blockAlign.value_or(frame));
    appendU16(body, fmt.bits);
    if (fmt.cbSize) {
        appendU16(body, *fmt.cbSize);
    }
    if (fmt.subFormat) {
        appendU16(body, fmt.validBits);
        appendU32(body, fmt.channelMask);
        appendGuid(body, *fmt.subFormat);
    }
    body.resize(body.size() + fmt.trailing, std::uint8_t{0});
    return body;
}

Bytes pcm16Bytes(std::vector<std::int16_t> const& samples) {
    Bytes out;
    for (std::int16_t const sample : samples) {
        appendU16(out, static_cast<std::uint16_t>(sample));
    }
    return out;
}

Bytes u32Bytes(std::vector<std::uint32_t> const& values) {
    Bytes out;
    for (std::uint32_t const value : values) {
        appendU32(out, value);
    }
    return out;
}

Bytes float32Bytes(std::vector<float> const& samples) {
    Bytes out;
    for (float const sample : samples) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &sample, sizeof(bits));
        appendU32(out, bits);
    }
    return out;
}

Bytes float64Bytes(std::vector<double> const& samples) {
    Bytes out;
    for (double const sample : samples) {
        std::uint64_t bits = 0;
        std::memcpy(&bits, &sample, sizeof(bits));
        appendU32(out, static_cast<std::uint32_t>(bits & 0xFFFFFFFFu));
        appendU32(out, static_cast<std::uint32_t>(bits >> 32));
    }
    return out;
}

// An mp3 carrying an ID3v2 header, renamed to .wav: the shape of file the NotRiffWave status
// exists for.
Bytes id3Image() {
    Bytes image{0x49, 0x44, 0x33, 0x03};
    image.resize(64u, std::uint8_t{0});
    return image;
}

// Four 16-bit mono frames of arbitrary data: enough for a file whose point is its chunk list
// rather than its samples.
Bytes eightBytes() {
    return Bytes{0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
}

// The plainest file the parser accepts: PCM, 16-bit, mono, 44100 Hz, four frames.
std::vector<Bytes> plainChunks() {
    return {chunk("fmt ", fmtBody(Fmt{})), chunk("data", eightBytes())};
}

WavStatus parseImage(Bytes const& image, PcmClip& out) {
    return parseWav(std::span<std::uint8_t const>(image), out);
}

// Every classification case is one image with a different fmt chunk, so the fmt is the only
// thing that has to be read to know why the answer came out the way it did.
WavStatus statusOfFmt(Fmt const& fmt) {
    PcmClip clip;
    return parseImage(riff({chunk("fmt ", fmtBody(fmt)), chunk("data", eightBytes())}), clip);
}

PcmClip decode(Fmt const& fmt, Bytes const& data) {
    PcmClip clip;
    REQUIRE(parseImage(riff({chunk("fmt ", fmtBody(fmt)), chunk("data", data)}), clip) ==
            WavStatus::Ok);
    return clip;
}

// The clip carries bytes; every expectation about conversion is about sample values.
std::vector<std::int16_t> samplesOf(PcmClip const& clip) {
    std::vector<std::int16_t> values(clip.samples.size() / sizeof(std::int16_t));
    if (!values.empty()) {
        std::memcpy(values.data(), clip.samples.data(), values.size() * sizeof(std::int16_t));
    }
    return values;
}

// mmreg.h names the PCM and float subtypes but none of the compressed ones. This is the
// waveformatex family GUID with the ADPCM format tag sitting in Data1.
constexpr GUID kAdpcmSubtype{0x00000002,
                             0x0000,
                             0x0010,
                             {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71}};

}  // namespace

TEST_CASE("wavReader: a buffer shorter than a riff header is malformed") {
    // The length check runs before the ids are read. The other order would make every stub or
    // zero-byte file the user points the sound setting at an out-of-bounds read rather than a
    // rejection.
    PcmClip clip;

    SUBCASE("nothing at all") {
        CHECK(parseImage(Bytes{}, clip) == WavStatus::Malformed);
    }

    SUBCASE("the riff id alone") {
        CHECK(parseImage(Bytes{0x52, 0x49, 0x46, 0x46}, clip) == WavStatus::Malformed);
    }

    SUBCASE("one byte short of a header") {
        Bytes image;
        appendId(image, "RIFF");
        appendU32(image, 0u);
        appendId(image, "WAV");
        CHECK(image.size() == 11u);
        CHECK(parseImage(image, clip) == WavStatus::Malformed);
    }
}

TEST_CASE("wavReader: an image that is not riff is not riff wave") {
    // decodeFile only offers the bytes to Media Foundation for statuses other than Malformed,
    // so a renamed mp3 reported as broken would be logged as a bad WAV and play nothing at all.
    PcmClip clip;
    CHECK(parseImage(id3Image(), clip) == WavStatus::NotRiffWave);
}

TEST_CASE("wavReader: riff with a form type other than wave is not riff wave") {
    // The form type is the only thing separating an AVI or a RIFF MIDI file from audio, so a
    // perfectly well-formed chunk list behind the wrong form type still has to be refused --
    // otherwise video payload reaches the source voice.
    PcmClip clip;
    CHECK(parseImage(riffImage("RIFF", "AVI ", plainChunks()), clip) == WavStatus::NotRiffWave);
}

TEST_CASE("wavReader: rf64 and bw64 images are rejected before the chunk walk") {
    // RF64 and BW64 carry their real (>4 GB) sizes in a ds64 chunk, so the 32-bit sizes in the
    // chunk headers are placeholders. Parsing one as RIFF would either truncate the audio or
    // hand the engine a length that disagrees with the file; both have to reach Media
    // Foundation instead.
    //
    // Seeded rather than default-constructed, because the two checks at the end are about the
    // parser leaving the caller's clip alone. Against a default clip they would pass for a
    // parser that cleared it, and indeed for one that did nothing at all.
    PcmClip clip;
    clip.samples = Bytes{0x11, 0x22};
    clip.sampleRate = 8000;

    SUBCASE("rf64") {
        CHECK(parseImage(riffImage("RF64", "WAVE", plainChunks()), clip) ==
              WavStatus::NotRiffWave);
    }

    SUBCASE("bw64") {
        CHECK(parseImage(riffImage("BW64", "WAVE", plainChunks()), clip) ==
              WavStatus::NotRiffWave);
    }

    CHECK(clip.samples == Bytes{0x11, 0x22});
    CHECK(clip.sampleRate == 8000u);
}

TEST_CASE("wavReader: a fmt chunk without a data chunk is malformed") {
    // A header-only WAV has no samples at all, and accepting it would leave the converter
    // reading from a null data pointer.
    PcmClip clip;
    CHECK(parseImage(riff({chunk("fmt ", fmtBody(Fmt{}))}), clip) == WavStatus::Malformed);
}

TEST_CASE("wavReader: a data chunk without a fmt chunk is malformed") {
    // Without a fmt chunk there is no sample rate, channel count or bit depth; guessing them
    // would play white noise at an arbitrary pitch.
    PcmClip clip;
    CHECK(parseImage(riff({chunk("data", eightBytes())}), clip) == WavStatus::Malformed);
}

TEST_CASE("wavReader: a fmt chunk shorter than sixteen bytes is not a fmt chunk") {
    // A 14-byte fmt is a PCMWAVEFORMAT with its wBitsPerSample field missing. Copying it would
    // leave the bit depth at whatever the struct was initialised with and classify the file
    // from that, so the walk skips it as if the id were unknown.
    Bytes shortFmt = fmtBody(Fmt{});
    shortFmt.resize(14u);

    PcmClip clip;
    CHECK(parseImage(riff({chunk("fmt ", shortFmt), chunk("data", eightBytes())}), clip) ==
          WavStatus::Malformed);
}

TEST_CASE("wavReader: unknown chunks before fmt are skipped") {
    // Almost every WAV a DAW or a tag editor writes carries LIST and fact chunks ahead of the
    // fmt chunk; reading fmt from a fixed offset 12 would make all of those files silent.
    Bytes const info{'I', 'N', 'F', 'O'};
    Bytes const image = riff({chunk("LIST", info), chunk("fact", Bytes(4u, std::uint8_t{0})),
                              chunk("fmt ", fmtBody(Fmt{})), chunk("data", eightBytes())});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.sampleRate == 44100u);
    CHECK(clip.channels == 1);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: an odd sized chunk is followed by a pad byte") {
    // The pad byte is not counted in the chunk size. Miss it and every chunk header after the
    // first odd chunk is read one byte early, so the ids are garbage, fmt is never found, and a
    // perfectly good file is reported as broken.
    Bytes const odd = chunk("LIST", Bytes(5u, std::uint8_t{0x11}));
    CHECK(odd.size() == 14u);

    PcmClip clip;

    SUBCASE("before fmt") {
        Bytes const image =
            riff({odd, chunk("fmt ", fmtBody(Fmt{})), chunk("data", eightBytes())});
        CHECK(parseImage(image, clip) == WavStatus::Ok);
        CHECK(clip.samples == eightBytes());
    }

    SUBCASE("between fmt and data") {
        Bytes const image =
            riff({chunk("fmt ", fmtBody(Fmt{})), odd, chunk("data", eightBytes())});
        CHECK(parseImage(image, clip) == WavStatus::Ok);
        CHECK(clip.samples == eightBytes());
    }
}

TEST_CASE("wavReader: an odd sized data chunk excludes its pad byte") {
    // An 8-bit mono file with an odd sample count is ordinary; playing its pad byte would append
    // one sample of arbitrary level to the end of every such sound. The trailing chunk is what
    // makes the pad byte a byte in the middle of the file rather than the last one.
    Bytes const image =
        riff({chunk("fmt ", fmtBody(Fmt{.rate = 8000, .bits = 8})),
              chunk("data", Bytes{0x00, 0x80, 0xFF}), chunk("LIST", Bytes{'I', 'N', 'F', 'O'})});

    PcmClip clip;
    REQUIRE(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples.size() == 6u);

    std::vector<std::int16_t> const expected{-32767, 0, 32511};
    CHECK(samplesOf(clip) == expected);
}

TEST_CASE("wavReader: a data chunk before fmt is still found") {
    // Chunk order is not fixed by the RIFF spec, so a writer that emits data first must still
    // play.
    Bytes const image = riff({chunk("data", eightBytes()), chunk("fmt ", fmtBody(Fmt{}))});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a zero sized chunk does not stall the walk") {
    // A zero-length fact or cue chunk is legal. The cursor still has to advance by the eight
    // bytes of the header, or the walk spins forever and hangs the thread decoding the sound.
    Bytes const image = riff({chunk("fact", Bytes{}), chunk("fmt ", fmtBody(Fmt{})),
                              chunk("data", eightBytes())});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a tail too short for a chunk header is ignored") {
    // Files padded out to a sector or block boundary are common; treating four stray bytes as a
    // chunk header would read a garbage size and could reject the file.
    Bytes const tail{0xDE, 0xAD, 0xBE, 0xEF};
    Bytes const image =
        riff({chunk("fmt ", fmtBody(Fmt{})), chunk("data", eightBytes()), tail});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a chunk after data whose size overruns does not spoil the parse") {
    // A download cut short after the audio, or a tag chunk with a corrupt size, must still play
    // the samples that did arrive rather than losing the whole file.
    Bytes const image = riff({chunk("fmt ", fmtBody(Fmt{})), chunk("data", eightBytes()),
                              chunkSized("LIST", 0xFFFF0000u, Bytes{})});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a data chunk whose declared size runs past the buffer is clamped") {
    // The truncated-download case: without the clamp the converter would read the declared 1000
    // bytes out of a buffer 990 bytes shorter and hand XAudio2 a pointer into unrelated memory.
    Bytes const present = pcm16Bytes({1, 2, 3, 4, 5});
    Bytes const image =
        riff({chunk("fmt ", fmtBody(Fmt{})), chunkSized("data", 1000u, present)});

    PcmClip clip;
    REQUIRE(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples.size() == 10u);
    CHECK(clip.samples == present);
}

TEST_CASE("wavReader: a data chunk header with no body is malformed") {
    // An empty clip would otherwise sail through to the engine and come out as silence with no
    // log line saying why: only a Malformed status is logged as a broken WAV.
    Bytes const image = riff({chunk("fmt ", fmtBody(Fmt{})), chunkSized("data", 4u, Bytes{})});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Malformed);
}

TEST_CASE("wavReader: a data chunk shorter than one frame is malformed") {
    // Nothing usable can be converted out of less than one frame, and catching it here is what
    // turns a truncated file into a logged warning instead of a silent no-op.
    Fmt const stereo{.channels = 2};
    PcmClip clip;

    SUBCASE("two bytes of a four byte frame") {
        Bytes const image =
            riff({chunk("fmt ", fmtBody(stereo)), chunk("data", pcm16Bytes({1}))});
        CHECK(parseImage(image, clip) == WavStatus::Malformed);
    }

    SUBCASE("zero length data") {
        Bytes const image = riff({chunk("fmt ", fmtBody(stereo)), chunk("data", Bytes{})});
        CHECK(parseImage(image, clip) == WavStatus::Malformed);
    }
}

TEST_CASE("wavReader: an overrunning data chunk before fmt loses the rest of the file") {
    // A file whose data size lies and whose fmt sits behind it cannot be trusted: decoding the
    // trailing bytes at a guessed rate would play the fmt header itself as audio.
    Bytes const image = riff({chunkSized("data", 10000u, Bytes{}), chunk("fmt ", fmtBody(Fmt{})),
                              chunk("data", eightBytes())});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Malformed);
}

TEST_CASE("wavReader: a fmt chunk that overruns the buffer is not accepted") {
    // Copying a declared-40-byte fmt out of a 16-byte remainder would read past the caller's
    // buffer into whatever follows it and classify the file from that. The data chunk in front
    // of it is there so the refusal can only be about the fmt.
    Bytes const image =
        riff({chunk("data", eightBytes()), chunkSized("fmt ", 40u, fmtBody(Fmt{}))});

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Malformed);
}

TEST_CASE("wavReader: a riff size larger than the buffer is ignored") {
    // Streaming writers and truncated downloads leave an oversized RIFF size behind; trusting it
    // would walk the chunk list off the end of the buffer.
    Bytes const image = riffImage("RIFF", "WAVE", plainChunks(), 0xFFFFFFF0u);

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a zero riff size is ignored") {
    // A WAV written to a pipe carries a zero size, because the length is not known when the
    // header goes out. Taking it literally would find no chunks and report the file broken.
    Bytes const image = riffImage("RIFF", "WAVE", plainChunks(), 0u);

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a riff size smaller than the buffer ends the walk early") {
    // Bytes past the declared end of the RIFF form are not part of it. A tag editor's trailing
    // blob shaped like a data chunk would otherwise win, because the walk overwrites the data
    // pointer on every match, and the user would hear the tag instead of the sound.
    Bytes image = riff(plainChunks());
    append(image, chunk("data", Bytes(8u, std::uint8_t{0xAA})));

    PcmClip clip;
    REQUIRE(parseImage(image, clip) == WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a riff size that excludes every chunk is malformed") {
    // A RIFF size that stops inside the header means the writer produced nothing usable, and the
    // parser has to say so rather than fall back to scanning the whole buffer.
    Bytes const image = riffImage("RIFF", "WAVE", plainChunks(), 4u);

    PcmClip clip;
    CHECK(parseImage(image, clip) == WavStatus::Malformed);
}

TEST_CASE("wavReader: zero channels or a zero sample rate is malformed") {
    // Zero channels makes the frame size zero, and the converter takes the byte count modulo
    // that -- an instant crash. A zero rate survives as far as the source voice, where setting
    // the sample rate fails and the clip's duration divides by it.
    SUBCASE("no channels") {
        CHECK(statusOfFmt(Fmt{.channels = 0}) == WavStatus::Malformed);
    }

    SUBCASE("no sample rate") {
        CHECK(statusOfFmt(Fmt{.channels = 2, .rate = 0}) == WavStatus::Malformed);
    }
}

TEST_CASE("wavReader: a compressed codec is unsupported rather than malformed") {
    // The tag is refused before the bit depth is looked at, which is why a 4-bit ADPCM file and
    // a 0-bit MP3-in-WAV are UnsupportedCodec and not Malformed. The distinction is the whole
    // routing decision: Media Foundation decodes all of these perfectly well, and a Malformed
    // status would log them as broken and play nothing.
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_ADPCM, .bits = 4, .cbSize = std::uint16_t{0}}) ==
          WavStatus::UnsupportedCodec);
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_ALAW, .bits = 8, .cbSize = std::uint16_t{0}}) ==
          WavStatus::UnsupportedCodec);
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_MULAW, .bits = 8, .cbSize = std::uint16_t{0}}) ==
          WavStatus::UnsupportedCodec);
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_MPEGLAYER3, .bits = 0, .cbSize = std::uint16_t{0}}) ==
          WavStatus::UnsupportedCodec);
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_GSM610, .bits = 0, .cbSize = std::uint16_t{0}}) ==
          WavStatus::UnsupportedCodec);

    // A legal bit depth does not rescue an unknown tag either.
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_UNKNOWN, .bits = 16, .cbSize = std::uint16_t{0}}) ==
          WavStatus::UnsupportedCodec);
}

TEST_CASE("wavReader: an unsupported pcm bit depth is malformed") {
    // The converter's fallback branch reads four bytes whatever the depth says, so a 12-bit or
    // 20-bit container would be read with the wrong stride and the clip would be noise; a
    // zero-bit depth would divide by zero working out how many samples there are.
    CHECK(statusOfFmt(Fmt{.bits = 0}) == WavStatus::Malformed);
    CHECK(statusOfFmt(Fmt{.bits = 4}) == WavStatus::Malformed);
    CHECK(statusOfFmt(Fmt{.bits = 12}) == WavStatus::Malformed);
    CHECK(statusOfFmt(Fmt{.bits = 20}) == WavStatus::Malformed);

    // 64 bits is accepted for float and for nothing else.
    CHECK(statusOfFmt(Fmt{.bits = 64}) == WavStatus::Malformed);
}

TEST_CASE("wavReader: float samples must be thirty two or sixty four bits") {
    // The float branch reads a 32-bit float or a 64-bit double; a 16-bit float chunk would be
    // read four bytes at a time out of two-byte samples and run off the end of the data chunk.
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 8}) == WavStatus::Malformed);
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 16}) == WavStatus::Malformed);
    CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 24}) == WavStatus::Malformed);
}

TEST_CASE("wavReader: fmt chunks of sixteen eighteen and forty bytes all parse") {
    // All three layouts come out of real tools: the bare PCMWAVEFORMAT from older recorders, the
    // 40-byte extensible form from everything modern. Rejecting either end would silence a whole
    // family of files.
    PcmClip clip;

    SUBCASE("pcmwaveformat") {
        CHECK(fmtBody(Fmt{}).size() == 16u);
        CHECK(parseImage(riff(plainChunks()), clip) == WavStatus::Ok);
    }

    SUBCASE("waveformatex") {
        Fmt const fmt{.cbSize = std::uint16_t{0}};
        CHECK(fmtBody(fmt).size() == 18u);
        CHECK(parseImage(riff({chunk("fmt ", fmtBody(fmt)), chunk("data", eightBytes())}),
                         clip) == WavStatus::Ok);
    }

    SUBCASE("extensible") {
        Fmt const fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                      .cbSize = std::uint16_t{22},
                      .validBits = 16,
                      .subFormat = KSDATAFORMAT_SUBTYPE_PCM};
        CHECK(fmtBody(fmt).size() == 40u);
        CHECK(parseImage(riff({chunk("fmt ", fmtBody(fmt)), chunk("data", eightBytes())}),
                         clip) == WavStatus::Ok);
    }

    CHECK(clip.sampleRate == 44100u);
    CHECK(clip.channels == 1);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: a fmt chunk too short to hold cbsize never reads one from the file") {
    // A 17-byte fmt is the one chunk length where the copy really does bring a byte of cbSize in
    // off the disk: the body is a PCMWAVEFORMAT with a single stray byte after it, and that byte
    // lands in the low half of the field. Believed, it says the 22 bytes of an extensible
    // extension arrived, and the SubFormat is then read as the zeroes this parser cleared the
    // struct to -- which match neither subtype, so a header cut off mid-field is reported as an
    // exotic codec, handed to Media Foundation and logged as a compressed WAVE rather than as
    // the truncation it is.
    Bytes body = fmtBody(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE});
    REQUIRE(body.size() == 16u);
    body.push_back(std::uint8_t{22});

    PcmClip clip;
    CHECK(parseImage(riff({chunk("fmt ", body), chunk("data", eightBytes())}), clip) ==
          WavStatus::Malformed);
}

TEST_CASE("wavReader: a fmt chunk longer than the extensible struct is truncated") {
    // A fmt chunk declaring 60 bytes is copied into a 40-byte struct, so the copy has to be
    // capped at the struct: without that, a file the user picked overflows the stack.
    Fmt const fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                  .cbSize = std::uint16_t{42},
                  .validBits = 16,
                  .subFormat = KSDATAFORMAT_SUBTYPE_PCM,
                  .trailing = 20};
    REQUIRE(fmtBody(fmt).size() == 60u);

    PcmClip clip;
    REQUIRE(parseImage(riff({chunk("fmt ", fmtBody(fmt)), chunk("data", eightBytes())}), clip) ==
            WavStatus::Ok);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: an extensible fmt chunk with the pcm subformat decodes as pcm") {
    // Everything a modern DAW writes above 16 bits or above two channels is extensible, and the
    // SubFormat has to be read from 24 bytes into the fmt body -- which only holds if
    // WAVEFORMATEXTENSIBLE is byte-packed the way mmreg.h declares it.
    PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                                    .channels = 2,
                                    .rate = 48000,
                                    .cbSize = std::uint16_t{22},
                                    .validBits = 16,
                                    .channelMask = 3,
                                    .subFormat = KSDATAFORMAT_SUBTYPE_PCM},
                                eightBytes());

    CHECK(clip.channels == 2);
    CHECK(clip.sampleRate == 48000u);
    CHECK(clip.bitsPerSample == 16);
    CHECK(clip.samples == eightBytes());
}

TEST_CASE("wavReader: an extensible fmt chunk with the float subformat decodes as float") {
    // An extensible float file misclassified as PCM would reinterpret the bit pattern of 1.0f
    // as an integer sample and play full-scale noise.
    PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                                    .bits = 32,
                                    .cbSize = std::uint16_t{22},
                                    .validBits = 32,
                                    .subFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT},
                                float32Bytes({1.0f, -1.0f, 0.25f}));

    std::vector<std::int16_t> const expected{32767, -32767, 8192};
    CHECK(samplesOf(clip) == expected);
}

TEST_CASE("wavReader: an extensible fmt chunk with a foreign subformat is unsupported") {
    // Same routing consequence as the plain compressed tags: an extensible ADPCM file has to
    // reach Media Foundation, and Malformed would make it permanently silent with a misleading
    // log line.
    SUBCASE("adpcm subformat") {
        CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                              .bits = 4,
                              .cbSize = std::uint16_t{22},
                              .subFormat = kAdpcmSubtype}) == WavStatus::UnsupportedCodec);
    }

    SUBCASE("waveformatex subformat") {
        CHECK(statusOfFmt(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                              .cbSize = std::uint16_t{22},
                              .validBits = 16,
                              .subFormat = KSDATAFORMAT_SUBTYPE_WAVEFORMATEX}) ==
              WavStatus::UnsupportedCodec);
    }
}

TEST_CASE("wavReader: an extensible fmt chunk with a short cbsize is malformed") {
    // cbSize is the writer's own statement that the 22 extension bytes carrying the SubFormat
    // are there. Below 22 there is no GUID to trust, and classifying on it would be classifying
    // on whatever the struct was initialised with.
    auto const withCbSize = [](int cbSize) {
        return statusOfFmt(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                               .cbSize = static_cast<std::uint16_t>(cbSize),
                               .validBits = 16,
                               .subFormat = KSDATAFORMAT_SUBTYPE_PCM});
    };

    CHECK(withCbSize(0) == WavStatus::Malformed);
    CHECK(withCbSize(21) == WavStatus::Malformed);
    CHECK(withCbSize(22) == WavStatus::Ok);
}

TEST_CASE("wavReader: cbsize is believed only as far as the chunk actually reaches") {
    // cbSize is the writer's claim about the extension that follows; the chunk size is what
    // actually arrived, and the parser believes the smaller of the two. Both halves of that
    // clamp are load-bearing, and they go wrong in opposite directions.
    //
    // Too little arrived, and the file is a download cut off after 18 bytes: it still carries
    // the extensible tag and a cbSize of 22, and taking the claim at its word classifies the
    // file from a SubFormat that was never read out of it -- the GUID is whatever this parser
    // cleared the struct to. Those zeroes match neither the PCM nor the float subtype, so a file
    // that is simply cut in half is reported as an exotic codec, which sends the bytes on to
    // Media Foundation and logs a compressed-WAVE line about a truncation.
    //
    // All of it arrived and only the claim is wrong, and the file is an ordinary 40-byte
    // extensible header from a writer that left rubbish in the field. Rewriting the clamp as a
    // refusal of any mismatch, or as a zeroing of cbSize, would silence that whole class of file
    // while leaving the truncation subcases green.
    auto const fmtOf = [](int cbSize, std::size_t extensionBytes) {
        return Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                   .cbSize = static_cast<std::uint16_t>(cbSize),
                   .trailing = extensionBytes};
    };

    SUBCASE("eighteen bytes claiming the twenty two an extensible file needs") {
        Fmt const fmt = fmtOf(22, 0);
        REQUIRE(fmtBody(fmt).size() == 18u);
        CHECK(statusOfFmt(fmt) == WavStatus::Malformed);
    }

    SUBCASE("twenty bytes of the twenty two") {
        // Two bytes of extension is wValidBitsPerSample and nothing else; the GUID starts six
        // bytes further along. Part of an extension is no more classifiable than none of it.
        Fmt const fmt = fmtOf(22, 2);
        REQUIRE(fmtBody(fmt).size() == 20u);
        CHECK(statusOfFmt(fmt) == WavStatus::Malformed);
    }

    SUBCASE("the whole extension behind an impossible claim") {
        // The samples are checked rather than just the status, so the file has to come out as
        // the PCM its SubFormat says it is and not merely avoid being refused.
        Fmt const fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                      .cbSize = std::uint16_t{0xFFFF},
                      .validBits = 16,
                      .subFormat = KSDATAFORMAT_SUBTYPE_PCM};
        REQUIRE(fmtBody(fmt).size() == 40u);

        std::vector<std::int16_t> const expected{1, -1, 32767, -32767};
        CHECK(samplesOf(decode(fmt, pcm16Bytes(expected))) == expected);
    }
}

TEST_CASE("wavReader: sixteen bit pcm is copied verbatim") {
    // The common case costs one copy and is bit-exact. A round trip through the converter would
    // clip the most negative sample by one least significant bit on every full-scale trough and
    // add a rounding pass to every notification sound.
    SUBCASE("mono") {
        std::vector<std::int16_t> const expected{
            0, 0x1234, std::numeric_limits<std::int16_t>::min(),
            std::numeric_limits<std::int16_t>::max()};
        Bytes const data = pcm16Bytes(expected);
        PcmClip const clip = decode(Fmt{}, data);

        CHECK(clip.sampleRate == 44100u);
        CHECK(clip.channels == 1);
        CHECK(clip.bitsPerSample == 16);
        CHECK(clip.samples == data);
        CHECK(samplesOf(clip) == expected);
    }

    SUBCASE("stereo") {
        std::vector<std::int16_t> const expected{-1, 1, -2, 2, -3, 3, -4, 4};
        Bytes const data = pcm16Bytes(expected);
        PcmClip const clip = decode(Fmt{.channels = 2, .rate = 48000}, data);

        CHECK(clip.channels == 2);
        CHECK(clip.samples == data);
        CHECK(samplesOf(clip) == expected);
    }
}

TEST_CASE("wavReader: a truncated final frame is dropped") {
    // XAudio2 wants a whole number of frames, and for the 24-bit case the converter would read
    // three bytes out of a two-byte remainder.
    SUBCASE("sixteen bit stereo") {
        Bytes const data = pcm16Bytes({1, 2, 3, 4, 5});
        PcmClip const clip = decode(Fmt{.channels = 2}, data);

        CHECK(clip.samples.size() == 8u);
        std::vector<std::int16_t> const expected{1, 2, 3, 4};
        CHECK(samplesOf(clip) == expected);
    }

    SUBCASE("twenty four bit stereo") {
        Bytes const data{0x00, 0x00, 0x20, 0x00, 0x00, 0xE0, 0x00, 0x00};
        PcmClip const clip = decode(Fmt{.channels = 2, .bits = 24}, data);

        CHECK(clip.samples.size() == 4u);
        std::vector<std::int16_t> const expected{8192, -8192};
        CHECK(samplesOf(clip) == expected);
    }
}

TEST_CASE("wavReader: eight bit pcm is unsigned around a midpoint of 128") {
    // 8-bit WAV PCM is the one unsigned depth. Read as signed, every sample above the midpoint
    // flips polarity and the file becomes a loud click.
    PcmClip const clip =
        decode(Fmt{.rate = 8000, .bits = 8}, Bytes{0x00, 0x80, 0xFF, 0x60, 0xE0});

    CHECK(clip.bitsPerSample == 16);
    CHECK(clip.samples.size() == 10u);

    // 0xFF is 127/128 of full scale, not full scale: the range is asymmetric around 128.
    std::vector<std::int16_t> const expected{-32767, 0, 32511, -8192, 24575};
    CHECK(samplesOf(clip) == expected);
}

TEST_CASE("wavReader: twenty four bit pcm is little endian and sign extended") {
    // Two independent mistakes with the same symptom. Reading the triple big-endian turns
    // near-silence (00 00 FF) into a large positive sample, and dropping the sign extension
    // turns every negative half-cycle positive -- audible as full-wave-rectified distortion.
    Bytes const data{0x00, 0x00, 0x00, 0xFF, 0xFF, 0x7F, 0x00, 0x00,
                     0x80, 0x00, 0x00, 0x20, 0x00, 0x00, 0xFF};
    PcmClip const clip = decode(Fmt{.bits = 24}, data);

    CHECK(clip.samples.size() == 10u);
    std::vector<std::int16_t> const expected{0, 32767, -32767, 8192, -256};
    CHECK(samplesOf(clip) == expected);
}

TEST_CASE("wavReader: thirty two bit pcm is scaled from the full int range") {
    // The scaling runs through a double, which is what puts the largest positive sample exactly
    // on full scale. Dividing by the wrong power of two would halve or double the level of every
    // 32-bit sound the user picks.
    PcmClip const clip = decode(
        Fmt{.bits = 32},
        u32Bytes({0x00000000u, 0x7FFFFFFFu, 0x80000000u, 0x20000000u, 0xE0000000u}));

    CHECK(clip.samples.size() == 10u);
    std::vector<std::int16_t> const expected{0, 32767, -32767, 8192, -8192};
    CHECK(samplesOf(clip) == expected);
}

TEST_CASE("wavReader: thirty two bit float is quantised to sixteen bit") {
    // Float is what every modern editor exports by default. Quantising against 32768 instead of
    // 32767 would put a bias on every sample, and losing the interleave would swap the stereo
    // image.
    SUBCASE("mono") {
        PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .rate = 48000, .bits = 32},
                                    float32Bytes({0.0f, 1.0f, -1.0f, 0.25f, -0.25f}));

        CHECK(clip.bitsPerSample == 16);
        CHECK(clip.samples.size() == 10u);
        std::vector<std::int16_t> const expected{0, 32767, -32767, 8192, -8192};
        CHECK(samplesOf(clip) == expected);
    }

    SUBCASE("stereo interleaving") {
        PcmClip const clip =
            decode(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .channels = 2, .rate = 48000, .bits = 32},
                   float32Bytes({1.0f, -1.0f, 0.25f, -0.25f}));

        CHECK(clip.channels == 2);
        std::vector<std::int16_t> const expected{32767, -32767, 8192, -8192};
        CHECK(samplesOf(clip) == expected);
    }
}

TEST_CASE("wavReader: float samples outside minus one to plus one clamp rather than wrap") {
    // The loudest possible failure. Anything off a mastering chain peaks above 0 dBFS, and
    // without the clamp the cast to a 16-bit sample wraps to the opposite polarity -- full-scale
    // square-wave noise where a quiet notification blip was meant to be.
    SUBCASE("thirty two bit") {
        float const huge = std::numeric_limits<float>::infinity();
        PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 32},
                                    float32Bytes({1.5f, -2.75f, 1e30f, -1e30f, huge, -huge}));

        std::vector<std::int16_t> const expected{32767, -32767, 32767, -32767, 32767, -32767};
        CHECK(samplesOf(clip) == expected);
    }

    SUBCASE("sixty four bit") {
        // The last four are past FLT_MAX, where narrowing to float is undefined rather than
        // merely lossy, so the clamp has to happen while the sample is still a double. Nothing
        // in this build can see that go wrong -- both orders land on the same rail on MSVC --
        // which is why they are extra inputs here rather than a case of their own: they cost a
        // line, and a sanitizing build would catch what the assertion cannot.
        double const huge = std::numeric_limits<double>::infinity();
        PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 64},
                                    float64Bytes({2.5, -2.5, 1e300, -1e300, huge, -huge}));

        std::vector<std::int16_t> const expected{32767, -32767, 32767, -32767, 32767, -32767};
        CHECK(samplesOf(clip) == expected);
    }
}

TEST_CASE("wavReader: a sample that is not a number decodes to silence") {
    // std::clamp does not stop a NaN: both of its comparisons are false against one, so it
    // passes through to std::lrintf, which raises the invalid-operation exception and returns
    // an unspecified value. That value then is the sample. A file the user chose in the sounds
    // page is arbitrary input, so this has to be decided rather than left to the library.
    double const quietNaN = std::numeric_limits<double>::quiet_NaN();

    SUBCASE("thirty two bit") {
        PcmClip const clip =
            decode(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 32},
                   float32Bytes({0.5f, std::numeric_limits<float>::quiet_NaN(), -0.5f}));

        std::vector<std::int16_t> const expected{16384, 0, -16384};
        CHECK(samplesOf(clip) == expected);
    }

    SUBCASE("sixty four bit") {
        PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 64},
                                    float64Bytes({quietNaN, 0.5}));

        std::vector<std::int16_t> const expected{0, 16384};
        CHECK(samplesOf(clip) == expected);
    }
}

TEST_CASE("wavReader: sixty four bit float is decoded") {
    // 64-bit float WAVs come out of scientific and mastering tools; reading them four bytes at a
    // time would decode the low half of each double as a sample of its own and produce noise at
    // twice the length.
    PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .bits = 64},
                                float64Bytes({0.0, 1.0, -1.0, 0.25}));

    CHECK(clip.samples.size() == 8u);
    std::vector<std::int16_t> const expected{0, 32767, -32767, 8192};
    CHECK(samplesOf(clip) == expected);
}

TEST_CASE("wavReader: the clip always reports sixteen bit samples") {
    // The engine builds the source voice straight from these three fields and derives the block
    // alignment from the bit depth. Leaving a source depth of 24 there would make XAudio2 read
    // the converted buffer with a six-byte stride and play the clip at the wrong pitch.
    auto const expectConverted = [](Fmt const& fmt, std::size_t dataBytes) {
        PcmClip const clip = decode(fmt, Bytes(dataBytes, std::uint8_t{0}));
        CHECK(clip.sampleRate == 22050u);
        CHECK(clip.channels == 2);
        CHECK(clip.bitsPerSample == 16);
        CHECK(clip.samples.size() == 8u);
    };

    SUBCASE("eight bit pcm") {
        expectConverted(Fmt{.channels = 2, .rate = 22050, .bits = 8}, 4u);
    }

    SUBCASE("twenty four bit pcm") {
        expectConverted(Fmt{.channels = 2, .rate = 22050, .bits = 24}, 12u);
    }

    SUBCASE("thirty two bit pcm") {
        expectConverted(Fmt{.channels = 2, .rate = 22050, .bits = 32}, 16u);
    }

    SUBCASE("thirty two bit float") {
        expectConverted(
            Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .channels = 2, .rate = 22050, .bits = 32}, 16u);
    }

    SUBCASE("sixty four bit float") {
        expectConverted(
            Fmt{.tag = WAVE_FORMAT_IEEE_FLOAT, .channels = 2, .rate = 22050, .bits = 64}, 32u);
    }
}

TEST_CASE("wavReader: block align channel mask and valid bits are ignored") {
    SUBCASE("a lying block align and byte rate") {
        // Broken writers routinely emit a wrong nBlockAlign. Deriving the frame size from it
        // rather than from the depth and the channel count would desynchronise the whole clip.
        PcmClip const clip =
            decode(Fmt{.blockAlign = std::uint16_t{999}, .byteRate = std::uint32_t{7}},
                   eightBytes());
        CHECK(clip.samples == eightBytes());
    }

    SUBCASE("valid bits smaller than the container") {
        // A 24-in-32 file has to be read at its container width; reading it at 24 would take
        // every frame three bytes short and let the channels drift apart.
        PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                                        .bits = 32,
                                        .cbSize = std::uint16_t{22},
                                        .validBits = 24,
                                        .subFormat = KSDATAFORMAT_SUBTYPE_PCM},
                                    u32Bytes({0x20000000u}));

        CHECK(clip.samples.size() == 2u);
        std::vector<std::int16_t> const expected{8192};
        CHECK(samplesOf(clip) == expected);
    }
}

TEST_CASE("wavReader: a six channel extensible file keeps all six channels") {
    // 5.1 material is the realistic reason for a file to be extensible at all. A frame-size or
    // interleave mistake reorders the channels, which on a surround endpoint puts the sound in
    // the wrong speaker.
    Bytes data{0x00, 0x00, 0x00, 0xFF, 0xFF, 0x7F, 0x00, 0x00, 0x80,
               0x00, 0x00, 0x20, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00};
    data.resize(36u, std::uint8_t{0});

    PcmClip const clip = decode(Fmt{.tag = WAVE_FORMAT_EXTENSIBLE,
                                    .channels = 6,
                                    .rate = 48000,
                                    .bits = 24,
                                    .cbSize = std::uint16_t{22},
                                    .validBits = 24,
                                    .channelMask = 0x3F,
                                    .subFormat = KSDATAFORMAT_SUBTYPE_PCM},
                                data);

    CHECK(clip.channels == 6);
    CHECK(clip.bitsPerSample == 16);

    // Six 24-bit samples is an 18-byte frame, so 36 bytes is two frames and not one byte more.
    // Getting that count back is half the check: a reader that took the frame size from the
    // block align field, or that rounded the tail up, would land on a different number here.
    std::vector<std::int16_t> const values = samplesOf(clip);
    REQUIRE(values.size() == 12u);

    std::vector<std::int16_t> const firstFrame(values.begin(), values.begin() + 6);
    std::vector<std::int16_t> const expected{0, 32767, -32767, 8192, -256, 0};
    CHECK(firstFrame == expected);

    // The other half: the second frame is the zero padding, so anything but silence there
    // means the first frame was read at the wrong stride and bled into it.
    std::vector<std::int16_t> const secondFrame(values.begin() + 6, values.end());
    CHECK(secondFrame == std::vector<std::int16_t>(6, std::int16_t{0}));
}

TEST_CASE("wavReader: a failed parse leaves the out parameter untouched") {
    // The header promises it. A converter that wrote the rate and the channel count before
    // failing would leave the caller holding a clip whose samples belong to the previous sound
    // and whose format fields belong to the broken one -- the recipe for playing an old buffer
    // at a new stride.
    Bytes const seed{0xAB, 0xCD};
    PcmClip clip;
    clip.samples = seed;
    clip.sampleRate = 12345;
    clip.channels = 7;
    clip.bitsPerSample = 99;

    SUBCASE("not riff wave") {
        CHECK(parseImage(id3Image(), clip) == WavStatus::NotRiffWave);
    }

    SUBCASE("malformed") {
        CHECK(parseImage(riff({chunk("fmt ", fmtBody(Fmt{}))}), clip) == WavStatus::Malformed);
    }

    SUBCASE("unsupported codec") {
        Bytes const image = riff({chunk("fmt ", fmtBody(Fmt{.tag = WAVE_FORMAT_ADPCM,
                                                           .bits = 4,
                                                           .cbSize = std::uint16_t{0}})),
                                  chunk("data", eightBytes())});
        CHECK(parseImage(image, clip) == WavStatus::UnsupportedCodec);
    }

    CHECK(clip.samples == seed);
    CHECK(clip.sampleRate == 12345u);
    CHECK(clip.channels == 7);
    CHECK(clip.bitsPerSample == 99);
}

TEST_CASE("wavReader: a successful parse replaces the whole out parameter") {
    // Previewing one sound after another reuses the same clip; appending rather than replacing
    // would grow the buffer on every preview and play the previous sound's tail after the new
    // one.
    PcmClip clip;
    clip.samples = Bytes(64u, std::uint8_t{0xEE});
    clip.sampleRate = 12345;
    clip.channels = 7;
    clip.bitsPerSample = 99;

    REQUIRE(parseImage(riff(plainChunks()), clip) == WavStatus::Ok);
    CHECK(clip.samples.size() == 8u);
    CHECK(clip.samples == eightBytes());
    CHECK(clip.sampleRate == 44100u);
    CHECK(clip.channels == 1);
    CHECK(clip.bitsPerSample == 16);
}

TEST_CASE("wavReader: describeWavStatus names every status") {
    // These strings are the whole explanation the user gets when a sound goes silent: they are
    // what the engine writes to the log when a file will not decode.
    CHECK(describeWavStatus(WavStatus::Ok) == L"ok");
    CHECK(describeWavStatus(WavStatus::NotRiffWave) == L"not a RIFF/WAVE file");
    CHECK(describeWavStatus(WavStatus::Malformed) ==
          L"malformed or incomplete RIFF/WAVE chunk list");
    CHECK(describeWavStatus(WavStatus::UnsupportedCodec) ==
          L"compressed WAVE (not linear PCM or IEEE float)");

    // A status added later without a case of its own falls through to this, and the log stops
    // saying why the sound went quiet.
    CHECK(describeWavStatus(static_cast<WavStatus>(99)) == L"unknown");
}
