#include <cstddef>
#include <cstdint>
#include <span>

#include "audio/WavReader.h"

// Any byte string must come back as a status, never as a crash or an out-of-bounds read.
extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const* data, std::size_t size) {
    peek::audio::PcmClip clip;
    (void)peek::audio::parseWav(std::span<std::uint8_t const>(data, size), clip);
    return 0;
}
