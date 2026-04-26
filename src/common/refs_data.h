#pragma once

#include <cstddef>
#include <cstdint>

namespace rinha {

inline constexpr int    DIM    = 14;
inline constexpr int    STRIDE = 16;   // padded to 16 lanes for one __m256i per ref
inline constexpr float  S      = 8192.0f;
inline constexpr int    S_I    = 8192;
inline constexpr size_t N      = 100000;

extern "C" const int16_t rinha_refs_bin_start[];
extern "C" const int16_t rinha_refs_bin_end[];
extern "C" const uint8_t rinha_labels_bin_start[];
extern "C" const uint8_t rinha_labels_bin_end[];

[[gnu::always_inline]] inline const int16_t* refs() noexcept {
    return rinha_refs_bin_start;
}

[[gnu::always_inline]] inline const uint8_t* labels() noexcept {
    return rinha_labels_bin_start;
}

}  // namespace rinha
