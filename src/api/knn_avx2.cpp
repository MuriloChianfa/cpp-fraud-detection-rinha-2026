#include "api/knn_avx2.h"

#include <cstdint>
#include <immintrin.h>
#include <limits>

#include "common/refs_data.h"

namespace rinha {

namespace {

constexpr uint32_t kMaxDist = std::numeric_limits<uint32_t>::max();

struct Top5 {
    uint32_t dists[5];
    uint32_t idxs[5];
    uint32_t threshold;  // == dists[4]
};

[[gnu::always_inline]] inline void top5_init(Top5& t) noexcept {
    for (int i = 0; i < 5; ++i) {
        t.dists[i] = kMaxDist;
        t.idxs[i]  = 0;
    }
    t.threshold = kMaxDist;
}

[[gnu::always_inline]] inline void top5_insert(Top5& t, uint32_t d, uint32_t i) noexcept {
    int pos = 4;
    while (pos > 0 && t.dists[pos - 1] > d) {
        t.dists[pos] = t.dists[pos - 1];
        t.idxs[pos]  = t.idxs[pos - 1];
        --pos;
    }
    t.dists[pos] = d;
    t.idxs[pos]  = i;
    t.threshold  = t.dists[4];
}

// AVX2 has no single-instruction horizontal sum; collapse 8->4->2->1 with SSE shuffles.
[[gnu::always_inline]] inline uint32_t hsum256_epi32(__m256i v) noexcept {
    __m128i lo = _mm256_castsi256_si128(v);
    __m128i hi = _mm256_extracti128_si256(v, 1);
    __m128i s4 = _mm_add_epi32(lo, hi);
    __m128i sh = _mm_shuffle_epi32(s4, _MM_SHUFFLE(1, 0, 3, 2));
    __m128i s2 = _mm_add_epi32(s4, sh);
    __m128i sh2 = _mm_shuffle_epi32(s2, _MM_SHUFFLE(2, 3, 0, 1));
    __m128i s1 = _mm_add_epi32(s2, sh2);
    return static_cast<uint32_t>(_mm_cvtsi128_si32(s1));
}

}  // namespace

[[gnu::target("avx2,fma,bmi2,popcnt")]]
[[gnu::hot]]
uint8_t knn5_avx2_count(const Query& q) noexcept {
    Top5 top;
    top5_init(top);

    const int16_t* refs   = rinha::refs();
    const uint8_t* labels = rinha::labels();
    constexpr size_t N    = rinha::N;
    constexpr size_t STR  = rinha::STRIDE;

    const __m256i qv = _mm256_load_si256(reinterpret_cast<const __m256i*>(q.v.data()));

    constexpr size_t kPrefetchAhead = 16;  // 16 refs * 32 B = 512 B = 8 cache lines
    size_t i = 0;

    for (; i + 2 <= N; i += 2) {
        const int16_t* p0 = refs + (i + 0) * STR;
        const int16_t* p1 = refs + (i + 1) * STR;

        if (i + kPrefetchAhead < N) {
            _mm_prefetch(reinterpret_cast<const char*>(refs + (i + kPrefetchAhead) * STR),
                         _MM_HINT_T0);
        }

        __m256i r0 = _mm256_load_si256(reinterpret_cast<const __m256i*>(p0));
        __m256i r1 = _mm256_load_si256(reinterpret_cast<const __m256i*>(p1));

        __m256i d0 = _mm256_sub_epi16(qv, r0);
        __m256i d1 = _mm256_sub_epi16(qv, r1);

        __m256i s0 = _mm256_madd_epi16(d0, d0);
        __m256i s1 = _mm256_madd_epi16(d1, d1);

        uint32_t dist0 = hsum256_epi32(s0);
        uint32_t dist1 = hsum256_epi32(s1);

        if (dist0 < top.threshold) top5_insert(top, dist0, static_cast<uint32_t>(i + 0));
        if (dist1 < top.threshold) top5_insert(top, dist1, static_cast<uint32_t>(i + 1));
    }

    // Tail: only when N is odd (N == 100000, so never runs in practice).
    for (; i < N; ++i) {
        const int16_t* p = refs + i * STR;
        __m256i r  = _mm256_load_si256(reinterpret_cast<const __m256i*>(p));
        __m256i d  = _mm256_sub_epi16(qv, r);
        __m256i s  = _mm256_madd_epi16(d, d);
        uint32_t dist = hsum256_epi32(s);
        if (dist < top.threshold) top5_insert(top, dist, static_cast<uint32_t>(i));
    }

    uint8_t fraud = 0;
    fraud += labels[top.idxs[0]];
    fraud += labels[top.idxs[1]];
    fraud += labels[top.idxs[2]];
    fraud += labels[top.idxs[3]];
    fraud += labels[top.idxs[4]];
    return fraud;
}

}  // namespace rinha
