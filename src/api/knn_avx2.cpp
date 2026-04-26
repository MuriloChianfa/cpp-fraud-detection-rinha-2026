#include "api/knn_avx2.h"

#include <cmath>
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
    uint32_t threshold;
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

[[gnu::always_inline]] inline uint32_t hsum256_epi32(__m256i v) noexcept {
    __m128i lo  = _mm256_castsi256_si128(v);
    __m128i hi  = _mm256_extracti128_si256(v, 1);
    __m128i s4  = _mm_add_epi32(lo, hi);
    __m128i sh  = _mm_shuffle_epi32(s4, _MM_SHUFFLE(1, 0, 3, 2));
    __m128i s2  = _mm_add_epi32(s4, sh);
    __m128i sh2 = _mm_shuffle_epi32(s2, _MM_SHUFFLE(2, 3, 0, 1));
    __m128i s1  = _mm_add_epi32(s2, sh2);
    return static_cast<uint32_t>(_mm_cvtsi128_si32(s1));
}

[[gnu::always_inline]] inline uint32_t compute_bail(uint32_t q_norm,
                                                    uint32_t threshold) noexcept {
    if (threshold == kMaxDist) return kMaxDist;
    double s = std::sqrt(static_cast<double>(threshold));
    uint32_t up = static_cast<uint32_t>(s);
    if (static_cast<double>(up) < s) ++up;
    uint64_t b = static_cast<uint64_t>(q_norm) + up + 1u;
    return b > kMaxDist ? kMaxDist : static_cast<uint32_t>(b);
}

#if defined(RINHA_KNN_ASM)
[[gnu::always_inline]] inline uint32_t dist_one(__m256i qv,
                                                const int16_t* p) noexcept {
    uint32_t out;
    asm volatile (
        "vmovdqa     (%[ptr]),  %%ymm1\n\t"
        "vpsubw      %[q],      %%ymm1,  %%ymm1\n\t"
        "vpmaddwd    %%ymm1,    %%ymm1,  %%ymm1\n\t"
        "vextracti128 $1,       %%ymm1,  %%xmm2\n\t"
        "vpaddd      %%xmm2,    %%xmm1,  %%xmm1\n\t"
        "vpshufd     $0x4e,     %%xmm1,  %%xmm2\n\t"
        "vpaddd      %%xmm2,    %%xmm1,  %%xmm1\n\t"
        "vpshufd     $0xb1,     %%xmm1,  %%xmm2\n\t"
        "vpaddd      %%xmm2,    %%xmm1,  %%xmm1\n\t"
        "vmovd       %%xmm1,    %k[out]\n\t"
        : [out] "=r"(out)
        : [q] "x"(qv), [ptr] "r"(p)
        : "ymm1", "xmm2", "memory"
    );
    return out;
}
#else
[[gnu::always_inline]] inline uint32_t dist_one(__m256i qv,
                                                const int16_t* p) noexcept {
    __m256i r = _mm256_load_si256(reinterpret_cast<const __m256i*>(p));
    __m256i d = _mm256_sub_epi16(qv, r);
    __m256i s = _mm256_madd_epi16(d, d);
    return hsum256_epi32(s);
}
#endif

}

[[gnu::target("avx2,fma,bmi2,popcnt")]]
[[gnu::hot]]
[[gnu::flatten]]
uint8_t knn5_avx2_count(const Query& q) noexcept {
    Top5 top;
    top5_init(top);

    uint32_t q_norm_sq = 0;
    {
        const int16_t* qp = q.v.data();
        for (int i = 0; i < DIM; ++i) {
            int32_t v = static_cast<int32_t>(qp[i]);
            q_norm_sq += static_cast<uint32_t>(v * v);
        }
    }
    uint32_t q_norm;
    {
        double s = std::sqrt(static_cast<double>(q_norm_sq));
        uint32_t up = static_cast<uint32_t>(s);
        if (static_cast<double>(up) < s) ++up;
        q_norm = up;
    }

    const int16_t*  __restrict__ refs0   = refs();
    const uint8_t*  __restrict__ labels0 = labels();
    const uint16_t* __restrict__ norms0  = norms();
    const int16_t*  refs_p   = static_cast<const int16_t*>(__builtin_assume_aligned(refs0, 64));
    const uint16_t* norms_p  = static_cast<const uint16_t*>(__builtin_assume_aligned(norms0, 64));
    constexpr size_t N   = rinha::N;
    constexpr size_t STR = rinha::STRIDE;

    const __m256i qv = _mm256_load_si256(reinterpret_cast<const __m256i*>(q.v.data()));

    uint32_t bail_norm = kMaxDist;

    constexpr size_t kPrefetchAhead = 32;

    size_t i = 0;
    for (; i + 4 <= N; i += 4) {
        if (i + kPrefetchAhead < N) {
            _mm_prefetch(reinterpret_cast<const char*>(refs_p + (i + kPrefetchAhead) * STR),
                         _MM_HINT_T0);
            _mm_prefetch(reinterpret_cast<const char*>(refs_p + (i + kPrefetchAhead + 2) * STR),
                         _MM_HINT_T0);
        }

        uint32_t rn0 = norms_p[i + 0];
        if (rn0 >= bail_norm) [[unlikely]] {
            goto done;
        }

        {
            int32_t  d0 = static_cast<int32_t>(rn0) - static_cast<int32_t>(q_norm);
            uint32_t lb0 = (d0 > 0) ? static_cast<uint32_t>(d0) * static_cast<uint32_t>(d0) : 0u;
            if (lb0 < top.threshold) {
                uint32_t dist = dist_one(qv, refs_p + (i + 0) * STR);
                if (dist < top.threshold) [[unlikely]] {
                    top5_insert(top, dist, static_cast<uint32_t>(i + 0));
                    bail_norm = compute_bail(q_norm, top.threshold);
                }
            }
        }
        {
            uint32_t rn1 = norms_p[i + 1];
            if (rn1 >= bail_norm) [[unlikely]] goto done;
            int32_t  d1 = static_cast<int32_t>(rn1) - static_cast<int32_t>(q_norm);
            uint32_t lb1 = (d1 > 0) ? static_cast<uint32_t>(d1) * static_cast<uint32_t>(d1) : 0u;
            if (lb1 < top.threshold) {
                uint32_t dist = dist_one(qv, refs_p + (i + 1) * STR);
                if (dist < top.threshold) [[unlikely]] {
                    top5_insert(top, dist, static_cast<uint32_t>(i + 1));
                    bail_norm = compute_bail(q_norm, top.threshold);
                }
            }
        }
        {
            uint32_t rn2 = norms_p[i + 2];
            if (rn2 >= bail_norm) [[unlikely]] goto done;
            int32_t  d2 = static_cast<int32_t>(rn2) - static_cast<int32_t>(q_norm);
            uint32_t lb2 = (d2 > 0) ? static_cast<uint32_t>(d2) * static_cast<uint32_t>(d2) : 0u;
            if (lb2 < top.threshold) {
                uint32_t dist = dist_one(qv, refs_p + (i + 2) * STR);
                if (dist < top.threshold) [[unlikely]] {
                    top5_insert(top, dist, static_cast<uint32_t>(i + 2));
                    bail_norm = compute_bail(q_norm, top.threshold);
                }
            }
        }
        {
            uint32_t rn3 = norms_p[i + 3];
            if (rn3 >= bail_norm) [[unlikely]] goto done;
            int32_t  d3 = static_cast<int32_t>(rn3) - static_cast<int32_t>(q_norm);
            uint32_t lb3 = (d3 > 0) ? static_cast<uint32_t>(d3) * static_cast<uint32_t>(d3) : 0u;
            if (lb3 < top.threshold) {
                uint32_t dist = dist_one(qv, refs_p + (i + 3) * STR);
                if (dist < top.threshold) [[unlikely]] {
                    top5_insert(top, dist, static_cast<uint32_t>(i + 3));
                    bail_norm = compute_bail(q_norm, top.threshold);
                }
            }
        }
    }

    for (; i < N; ++i) {
        uint32_t rn = norms_p[i];
        if (rn >= bail_norm) break;
        int32_t  delta = static_cast<int32_t>(rn) - static_cast<int32_t>(q_norm);
        uint32_t lb = (delta > 0) ? static_cast<uint32_t>(delta) * static_cast<uint32_t>(delta) : 0u;
        if (lb >= top.threshold) continue;

        uint32_t dist = dist_one(qv, refs_p + i * STR);
        if (dist < top.threshold) {
            top5_insert(top, dist, static_cast<uint32_t>(i));
            bail_norm = compute_bail(q_norm, top.threshold);
        }
    }

done:
    uint8_t fraud = 0;
    fraud += labels0[top.idxs[0]];
    fraud += labels0[top.idxs[1]];
    fraud += labels0[top.idxs[2]];
    fraud += labels0[top.idxs[3]];
    fraud += labels0[top.idxs[4]];
    return fraud;
}

}
