#pragma once

#include <cstddef>
#include <cstdint>

namespace rinha {

inline constexpr int    DIM    = 14;
inline constexpr int    STRIDE = 16;
inline constexpr float  S      = 8192.0f;
inline constexpr int    S_I    = 8192;
inline constexpr size_t N      = 100000;

extern "C" const int16_t  rinha_refs_bin_start[];
extern "C" const int16_t  rinha_refs_bin_end[];
extern "C" const uint8_t  rinha_labels_bin_start[];
extern "C" const uint8_t  rinha_labels_bin_end[];
extern "C" const uint16_t rinha_norms_bin_start[];
extern "C" const uint16_t rinha_norms_bin_end[];

extern const int16_t*  g_refs_ptr;
extern const uint8_t*  g_labels_ptr;
extern const uint16_t* g_norms_ptr;

[[gnu::always_inline]] inline const int16_t* refs() noexcept {
    return g_refs_ptr;
}

[[gnu::always_inline]] inline const uint8_t* labels() noexcept {
    return g_labels_ptr;
}

[[gnu::always_inline]] inline const uint16_t* norms() noexcept {
    return g_norms_ptr;
}

void init_refs_residency() noexcept;

}
