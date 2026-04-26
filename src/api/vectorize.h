#pragma once

#include <array>
#include <cstdint>

#include "api/json_parser.h"

namespace rinha {

struct alignas(32) Query {
    std::array<int16_t, 16> v;
};

void vectorize(const Payload& p, Query& q) noexcept;

}
