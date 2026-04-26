#pragma once

#include <array>
#include <cstdint>

#include "api/json_parser.h"

namespace rinha {

// 14 real dims + 2 zero-padded lanes to fill one __m256i aligned for _mm256_load_si256.
struct alignas(32) Query {
    std::array<int16_t, 16> v;
};

void vectorize(const Payload& p, Query& q) noexcept;

}  // namespace rinha
