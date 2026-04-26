#include "api/json_parser.h"

#include <cstdint>
#include <cstring>
#include <string_view>

extern "C" void* memmem(const void* haystack, std::size_t haystacklen,
                        const void* needle,   std::size_t needlelen) noexcept;

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

[[gnu::always_inline]] inline int64_t days_from_civil(int y, unsigned m, unsigned d) noexcept {
    y -= (m <= 2);
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

[[gnu::always_inline]] inline uint8_t day_of_week(int y, unsigned m, unsigned d) noexcept {
    const int64_t days = days_from_civil(y, m, d);
    int64_t mod = (days + 3) % 7;
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

[[gnu::always_inline]] inline const char* find_after(const char* buf, size_t len,
                                                     const char* key, size_t klen) noexcept {
    const void* p = ::memmem(buf, len, key, klen);
    if (!p) return nullptr;
    const char* end = buf + len;
    const char* q = static_cast<const char*>(p) + klen;
    while (q < end &&
           (*q == ':' || *q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')) {
        ++q;
    }
    return q;
}

[[gnu::always_inline]] inline const char* find_section(const char* buf, size_t len,
                                                       const char* key, size_t klen) noexcept {
    const void* p = ::memmem(buf, len, key, klen);
    return p ? static_cast<const char*>(p) : nullptr;
}

[[gnu::always_inline]] inline float parse_num(const char* p, const char* end) noexcept {
    bool neg = false;
    if (p < end && *p == '-') { neg = true; ++p; }
    double v = 0.0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10.0 + static_cast<double>(*p - '0');
        ++p;
    }
    if (p < end && *p == '.') {
        ++p;
        double scale = 0.1;
        while (p < end && *p >= '0' && *p <= '9') {
            v += static_cast<double>(*p - '0') * scale;
            scale *= 0.1;
            ++p;
        }
    }
    return static_cast<float>(neg ? -v : v);
}

[[gnu::always_inline]] inline int64_t parse_int(const char* p, const char* end) noexcept {
    bool neg = false;
    if (p < end && *p == '-') { neg = true; ++p; }
    int64_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + static_cast<int64_t>(*p - '0');
        ++p;
    }
    return neg ? -v : v;
}

[[gnu::always_inline]] inline uint32_t parse_digits_u32(const char* p, const char* end) noexcept {
    uint32_t v = 0;
    while (p < end && *p >= '0' && *p <= '9') {
        v = v * 10 + static_cast<uint32_t>(*p - '0');
        ++p;
    }
    return v;
}

[[gnu::always_inline]] inline bool parse_iso(const char* p, const char* end,
                                             DateTime& out) noexcept {
    while (p < end && *p != '"') ++p;
    if (p >= end) return false;
    ++p;
    if (end - p < 19) return false;
    out.year   = static_cast<uint16_t>((p[0]-'0')*1000 + (p[1]-'0')*100 + (p[2]-'0')*10 + (p[3]-'0'));
    out.month  = static_cast<uint8_t>((p[5]-'0')*10 + (p[6]-'0'));
    out.day    = static_cast<uint8_t>((p[8]-'0')*10 + (p[9]-'0'));
    out.hour   = static_cast<uint8_t>((p[11]-'0')*10 + (p[12]-'0'));
    out.minute = static_cast<uint8_t>((p[14]-'0')*10 + (p[15]-'0'));
    out.second = static_cast<uint8_t>((p[17]-'0')*10 + (p[18]-'0'));
    return true;
}

[[gnu::always_inline]] inline bool array_contains_id(const char* p, const char* end,
                                                     const char* needle, size_t nlen) noexcept {
    while (p < end && *p != '[') ++p;
    if (p >= end) return false;
    ++p;
    while (p < end && *p != ']') {
        if (*p == '"') {
            const char* start = ++p;
            while (p < end && *p != '"') ++p;
            if (p >= end) return false;
            if (static_cast<size_t>(p - start) == nlen &&
                std::memcmp(start, needle, nlen) == 0) {
                return true;
            }
            ++p;
        } else {
            ++p;
        }
    }
    return false;
}

}

[[gnu::hot]]
bool parsePayload(std::string_view body, Payload& out) noexcept {
    if (body.empty() || body.size() > 16384) return false;

    const char*  buf = body.data();
    const size_t len = body.size();
    const char*  end = buf + len;

    const char* tx_p   = find_section(buf, len, "\"transaction\"",       13);
    const char* cust_p = find_section(buf, len, "\"customer\"",          10);
    const char* mer_p  = find_section(buf, len, "\"merchant\"",          10);
    const char* term_p = find_section(buf, len, "\"terminal\"",          10);
    const char* last_p = find_section(buf, len, "\"last_transaction\"", 18);
    if (!tx_p || !cust_p || !mer_p || !term_p || !last_p) return false;

    struct Item { const char* p; int idx; };
    Item items[5] = {
        {tx_p,   0}, {cust_p, 1}, {mer_p,  2}, {term_p, 3}, {last_p, 4}
    };
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            if (items[i].p > items[j].p) {
                Item t = items[i]; items[i] = items[j]; items[j] = t;
            }
        }
    }
    const char* slice_start[5];
    const char* slice_end[5];
    for (int i = 0; i < 5; ++i) {
        slice_start[items[i].idx] = items[i].p;
        slice_end[items[i].idx]   = (i + 1 < 5) ? items[i + 1].p : end;
    }
    const char* tx_s = slice_start[0]; size_t tx_l = static_cast<size_t>(slice_end[0] - tx_s);
    const char* cu_s = slice_start[1]; size_t cu_l = static_cast<size_t>(slice_end[1] - cu_s);
    const char* me_s = slice_start[2]; size_t me_l = static_cast<size_t>(slice_end[2] - me_s);
    const char* te_s = slice_start[3]; size_t te_l = static_cast<size_t>(slice_end[3] - te_s);
    const char* la_s = slice_start[4]; size_t la_l = static_cast<size_t>(slice_end[4] - la_s);

    DateTime req_at{};
    DateTime last_at{};
    bool     have_last_at = false;

    {
        const char* p = find_after(tx_s, tx_l, "\"amount\"", 8);
        if (!p) return false;
        out.amount = parse_num(p, end);
    }
    {
        const char* p = find_after(tx_s, tx_l, "\"installments\"", 14);
        if (!p) return false;
        int64_t v = parse_int(p, end);
        if (v < 0)        v = 0;
        else if (v > 255) v = 255;
        out.installments = static_cast<uint8_t>(v);
    }
    {
        const char* p = find_after(tx_s, tx_l, "\"requested_at\"", 14);
        if (!p) return false;
        if (!parse_iso(p, end, req_at)) return false;
        out.hour        = req_at.hour;
        out.day_of_week = day_of_week(req_at.year, req_at.month, req_at.day);
    }

    {
        const char* p = find_after(cu_s, cu_l, "\"avg_amount\"", 12);
        if (!p) return false;
        out.customer_avg_amount = parse_num(p, end);
    }
    {
        const char* p = find_after(cu_s, cu_l, "\"tx_count_24h\"", 14);
        if (!p) return false;
        int64_t v = parse_int(p, end);
        if (v < 0) v = 0;
        else if (v > 0xFFFFFFFFLL) v = 0xFFFFFFFFLL;
        out.tx_count_24h = static_cast<uint32_t>(v);
    }

    const char* mid_s = nullptr;
    size_t      mid_l = 0;
    {
        const char* p = find_after(me_s, me_l, "\"id\"", 4);
        if (!p) return false;
        while (p < end && *p != '"') ++p;
        if (p >= end) return false;
        mid_s = ++p;
        while (p < end && *p != '"') ++p;
        if (p >= end) return false;
        mid_l = static_cast<size_t>(p - mid_s);
    }
    {
        const char* p = find_after(me_s, me_l, "\"mcc\"", 5);
        if (!p) {
            out.mcc = 0;
        } else {
            while (p < end && *p != '"') ++p;
            if (p >= end) {
                out.mcc = 0;
            } else {
                ++p;
                out.mcc = parse_digits_u32(p, end);
            }
        }
    }
    {
        const char* p = find_after(me_s, me_l, "\"avg_amount\"", 12);
        if (!p) return false;
        out.merchant_avg_amount = parse_num(p, end);
    }

    {
        bool unknown = true;
        if (mid_s && mid_l > 0) {
            const char* p = find_after(cu_s, cu_l, "\"known_merchants\"", 17);
            if (p) {
                unknown = !array_contains_id(p, end, mid_s, mid_l);
            }
        }
        out.is_unknown_merchant = unknown;
    }

    {
        const char* p = find_after(te_s, te_l, "\"is_online\"", 11);
        if (!p) return false;
        out.is_online = (*p == 't');
    }
    {
        const char* p = find_after(te_s, te_l, "\"card_present\"", 14);
        if (!p) return false;
        out.card_present = (*p == 't');
    }
    {
        const char* p = find_after(te_s, te_l, "\"km_from_home\"", 14);
        if (!p) return false;
        out.km_from_home = parse_num(p, end);
    }

    {
        const char* lp = find_after(la_s, la_l, "\"last_transaction\"", 18);
        if (lp && lp < end && *lp == '{') {
            const char* tp = find_after(la_s, la_l, "\"timestamp\"", 11);
            if (!tp) return false;
            if (!parse_iso(tp, end, last_at)) return false;
            have_last_at = true;

            const char* kp = find_after(la_s, la_l, "\"km_from_current\"", 17);
            if (!kp) return false;
            out.km_from_current = parse_num(kp, end);
            out.has_last_tx     = true;
        } else {
            out.has_last_tx        = false;
            out.minutes_since_last = 0;
            out.km_from_current    = 0.0f;
        }
    }

    if (out.has_last_tx && have_last_at) {
        out.minutes_since_last = minutes_between(last_at, req_at);
    } else {
        out.minutes_since_last = 0;
    }

    return true;
}

}
