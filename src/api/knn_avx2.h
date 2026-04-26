#pragma once

#include <cstdint>

#include "api/vectorize.h"

namespace rinha {

uint8_t knn5_avx2_count(const Query& q) noexcept;

}
