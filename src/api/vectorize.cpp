#include "api/vectorize.h"

#include <array>
#include <cmath>
#include <cstdint>

#include "common/refs_data.h"

namespace rinha {

namespace {

constexpr float kS  = S;
constexpr int   kSI = S_I;

template <int N, int Den>
constexpr std::array<int16_t, N> make_lut() {
    std::array<int16_t, N> a{};
    for (int i = 0; i < N; ++i) {
        float v = static_cast<float>(i) / static_cast<float>(Den);
        if (v < 0.0f) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        a[i] = static_cast<int16_t>(v * kS + 0.5f);
    }
    return a;
}

constexpr auto kInstallmentsLut = make_lut<13, 12>();   // 0..12 / 12
constexpr auto kHourLut         = make_lut<24, 23>();   // 0..23 / 23
constexpr auto kDowLut          = make_lut<7,  6>();    // 0..6 / 6
constexpr auto kTxCountLut      = make_lut<21, 20>();   // 0..20 / 20

[[gnu::always_inline]] inline int16_t quant(float v) noexcept {
    if (v < 0.0f) v = 0.0f;
    else if (v > 1.0f) v = 1.0f;
    return static_cast<int16_t>(v * kS + 0.5f);
}

[[gnu::always_inline]] inline int16_t mcc_risk_q(uint32_t mcc) noexcept {
    switch (mcc) {
        case 5411: return 1229;  // 0.15
        case 5812: return 2458;  // 0.30
        case 5912: return 1638;  // 0.20
        case 5944: return 3686;  // 0.45
        case 7801: return 6554;  // 0.80
        case 7802: return 6144;  // 0.75
        case 7995: return 6963;  // 0.85
        case 4511: return 2867;  // 0.35
        case 5311: return 2048;  // 0.25
        case 5999: return 4096;  // 0.50
        default:   return 4096;  // 0.50 default per DATASET.md
    }
}

}  // namespace

void vectorize(const Payload& p, Query& q) noexcept {
    auto& v = q.v;

    v[0] = quant(p.amount / 10000.0f);

    {
        unsigned i = p.installments;
        if (i > 12) i = 12;
        v[1] = kInstallmentsLut[i];
    }

    {
        float r;
        if (p.customer_avg_amount > 0.0f) {
            r = (p.amount / p.customer_avg_amount) / 10.0f;
        } else {
            r = 1.0f;
        }
        v[2] = quant(r);
    }

    {
        unsigned i = p.hour;
        if (i > 23) i = 23;
        v[3] = kHourLut[i];
    }

    {
        unsigned i = p.day_of_week;
        if (i > 6) i = 6;
        v[4] = kDowLut[i];
    }

    if (p.has_last_tx) {
        v[5] = quant(static_cast<float>(p.minutes_since_last) / 1440.0f);
        v[6] = quant(p.km_from_current / 1000.0f);
    } else {
        v[5] = static_cast<int16_t>(-kSI);
        v[6] = static_cast<int16_t>(-kSI);
    }

    v[7] = quant(p.km_from_home / 1000.0f);

    {
        unsigned i = p.tx_count_24h;
        if (i > 20) i = 20;
        v[8] = kTxCountLut[i];
    }

    v[9]  = p.is_online           ? static_cast<int16_t>(kSI) : int16_t{0};
    v[10] = p.card_present        ? static_cast<int16_t>(kSI) : int16_t{0};
    v[11] = p.is_unknown_merchant ? static_cast<int16_t>(kSI) : int16_t{0};
    v[12] = mcc_risk_q(p.mcc);
    v[13] = quant(p.merchant_avg_amount / 10000.0f);

    v[14] = 0;  // pad lane
    v[15] = 0;  // pad lane
}

}  // namespace rinha
