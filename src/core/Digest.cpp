#include "core/Digest.h"

namespace peek {

std::uint64_t digestOf(std::wstring_view text) noexcept {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;

    std::uint64_t digest = kOffsetBasis;
    for (wchar_t const unit : text) {
        auto const value = static_cast<std::uint16_t>(unit);
        digest = (digest ^ (value & 0xff)) * kPrime;
        digest = (digest ^ (value >> 8)) * kPrime;
    }
    return digest;
}

}  // namespace peek
