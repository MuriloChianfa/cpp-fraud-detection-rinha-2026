#include "api/json_parser.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>

#include <simdjson.h>

namespace rinha {

namespace {

struct DateTime {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
};

[[gnu::always_inline]] inline bool slice_iso(std::string_view s, DateTime& out) noexcept {
    if (s.size() < 19) return false;
    auto d = [&](size_t i)->int { return static_cast<int>(s[i] - '0'); };
    out.year   = static_cast<uint16_t>(d(0)*1000 + d(1)*100 + d(2)*10 + d(3));
    out.month  = static_cast<uint8_t>(d(5)*10 + d(6));
    out.day    = static_cast<uint8_t>(d(8)*10 + d(9));
    out.hour   = static_cast<uint8_t>(d(11)*10 + d(12));
    out.minute = static_cast<uint8_t>(d(14)*10 + d(15));
    out.second = static_cast<uint8_t>(d(17)*10 + d(18));
    return true;
}

// Howard Hinnant's days_from_civil.
[[gnu::always_inline]] inline int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

// 0 = Monday .. 6 = Sunday.
[[gnu::always_inline]] inline uint8_t day_of_week(int y, unsigned m, unsigned d) noexcept {
    const int64_t days = days_from_civil(y, m, d);  // 1970-01-01 was a Thursday
    int64_t mod = (days + 3) % 7;                   // shift so Monday == 0
    if (mod < 0) mod += 7;
    return static_cast<uint8_t>(mod);
}

[[gnu::always_inline]] inline uint32_t minutes_between(const DateTime& a,
                                                       const DateTime& b) noexcept {
    const int64_t da = days_from_civil(a.year, a.month, a.day);
    const int64_t db = days_from_civil(b.year, b.month, b.day);
    const int64_t ma = da * 1440 + int64_t(a.hour) * 60 + a.minute;
    const int64_t mb = db * 1440 + int64_t(b.hour) * 60 + b.minute;
    int64_t diff = mb - ma;
    if (diff < 0) diff = 0;
    if (diff > 0xffffffffLL) diff = 0xffffffffLL;
    return static_cast<uint32_t>(diff);
}

[[gnu::always_inline]] inline uint32_t parse_mcc_digits(std::string_view sv) noexcept {
    uint32_t v = 0;
    for (char c : sv) {
        if (c < '0' || c > '9') break;
        v = v * 10 + static_cast<uint32_t>(c - '0');
    }
    return v;
}

constexpr size_t kScratchSize = 16 * 1024;
constexpr size_t kPadding     = simdjson::SIMDJSON_PADDING;

struct ThreadCtx {
    alignas(64) std::array<char, kScratchSize + kPadding> scratch{};
    simdjson::ondemand::parser parser{kScratchSize};
};

[[gnu::always_inline]] inline ThreadCtx& tctx() noexcept {
    thread_local ThreadCtx ctx;
    return ctx;
}

}  // namespace

bool parsePayload(std::string_view body, Payload& out) noexcept {
    if (body.size() == 0 || body.size() >= kScratchSize) return false;

    auto& ctx = tctx();
    std::memcpy(ctx.scratch.data(), body.data(), body.size());
    // simdjson requires the padding region to be readable.
    std::memset(ctx.scratch.data() + body.size(), 0, kPadding);

    simdjson::ondemand::document doc;
    if (ctx.parser
            .iterate(ctx.scratch.data(), body.size(), ctx.scratch.size())
            .get(doc) != simdjson::SUCCESS) {
        return false;
    }

    DateTime req_at{};
    DateTime last_at{};
    bool     have_last_at = false;

    {
        auto tx_res = doc.find_field_unordered("transaction");
        if (tx_res.error() != simdjson::SUCCESS) return false;
        simdjson::ondemand::object tx = tx_res.value_unsafe();

        double amount = 0.0;
        if (tx["amount"].get(amount) != simdjson::SUCCESS) return false;
        out.amount = static_cast<float>(amount);

        int64_t inst = 0;
        if (tx["installments"].get(inst) != simdjson::SUCCESS) return false;
        if (inst < 0)        inst = 0;
        else if (inst > 255) inst = 255;
        out.installments = static_cast<uint8_t>(inst);

        std::string_view req_sv;
        if (tx["requested_at"].get(req_sv) != simdjson::SUCCESS) return false;
        if (!slice_iso(req_sv, req_at)) return false;
        out.hour        = req_at.hour;
        out.day_of_week = day_of_week(req_at.year, req_at.month, req_at.day);
    }

    // string_views point into the scratch buffer and stay valid for the whole call.
    constexpr size_t kMaxKnown = 32;
    std::array<std::string_view, kMaxKnown> known{};
    size_t n_known = 0;
    {
        auto cust_res = doc.find_field_unordered("customer");
        if (cust_res.error() != simdjson::SUCCESS) return false;
        simdjson::ondemand::object cust = cust_res.value_unsafe();

        double avg = 0.0;
        if (cust["avg_amount"].get(avg) != simdjson::SUCCESS) return false;
        out.customer_avg_amount = static_cast<float>(avg);

        int64_t cnt = 0;
        if (cust["tx_count_24h"].get(cnt) != simdjson::SUCCESS) return false;
        if (cnt < 0)        cnt = 0;
        out.tx_count_24h = static_cast<uint32_t>(cnt);

        auto km_field = cust["known_merchants"];
        if (km_field.error() == simdjson::SUCCESS) {
            simdjson::ondemand::array arr = km_field.value_unsafe();
            for (auto elem : arr) {
                std::string_view sv;
                if (elem.get_string().get(sv) != simdjson::SUCCESS) continue;
                if (n_known < kMaxKnown) known[n_known++] = sv;
            }
        }
    }

    std::string_view merchant_id;
    {
        auto m_res = doc.find_field_unordered("merchant");
        if (m_res.error() != simdjson::SUCCESS) return false;
        simdjson::ondemand::object m = m_res.value_unsafe();

        if (m["id"].get_string().get(merchant_id) != simdjson::SUCCESS)
            merchant_id = std::string_view{};

        std::string_view mcc_sv;
        if (m["mcc"].get_string().get(mcc_sv) == simdjson::SUCCESS) {
            out.mcc = parse_mcc_digits(mcc_sv);
        } else {
            out.mcc = 0;
        }

        double mavg = 0.0;
        if (m["avg_amount"].get(mavg) != simdjson::SUCCESS) return false;
        out.merchant_avg_amount = static_cast<float>(mavg);
    }

    {
        bool unknown = true;
        if (!merchant_id.empty()) {
            for (size_t i = 0; i < n_known; ++i) {
                if (known[i].size() == merchant_id.size() &&
                    std::memcmp(known[i].data(), merchant_id.data(), merchant_id.size()) == 0) {
                    unknown = false;
                    break;
                }
            }
        }
        out.is_unknown_merchant = unknown;
    }

    {
        auto t_res = doc.find_field_unordered("terminal");
        if (t_res.error() != simdjson::SUCCESS) return false;
        simdjson::ondemand::object t = t_res.value_unsafe();

        bool b = false;
        if (t["is_online"].get(b) != simdjson::SUCCESS) return false;
        out.is_online = b;

        if (t["card_present"].get(b) != simdjson::SUCCESS) return false;
        out.card_present = b;

        double km = 0.0;
        if (t["km_from_home"].get(km) != simdjson::SUCCESS) return false;
        out.km_from_home = static_cast<float>(km);
    }

    {
        auto lt_field = doc.find_field_unordered("last_transaction");
        if (lt_field.error() == simdjson::SUCCESS) {
            simdjson::ondemand::value lt = lt_field.value_unsafe();
            if (lt.is_null().value_unsafe()) {
                out.has_last_tx        = false;
                out.minutes_since_last = 0;
                out.km_from_current    = 0.0f;
            } else {
                std::string_view ts_sv;
                if (lt["timestamp"].get_string().get(ts_sv) != simdjson::SUCCESS) return false;
                if (!slice_iso(ts_sv, last_at)) return false;
                have_last_at = true;

                double km = 0.0;
                if (lt["km_from_current"].get(km) != simdjson::SUCCESS) return false;
                out.km_from_current = static_cast<float>(km);
                out.has_last_tx     = true;
            }
        } else {
            out.has_last_tx        = false;
            out.minutes_since_last = 0;
            out.km_from_current    = 0.0f;
        }
    }

    if (out.has_last_tx && have_last_at) {
        out.minutes_since_last = minutes_between(last_at, req_at);
    }

    return true;
}

}  // namespace rinha
