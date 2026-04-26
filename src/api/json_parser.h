#pragma once

#include <cstdint>
#include <string_view>

namespace rinha {

struct Payload {
    float    amount;
    uint8_t  installments;
    uint8_t  hour;                  // 0..23 UTC
    uint8_t  day_of_week;           // 0=Mon .. 6=Sun
    float    customer_avg_amount;
    uint32_t tx_count_24h;
    uint32_t mcc;
    float    merchant_avg_amount;
    bool     is_online;
    bool     card_present;
    float    km_from_home;
    bool     is_unknown_merchant;   // merchant.id NOT in known_merchants
    bool     has_last_tx;           // false when last_transaction == null
    uint32_t minutes_since_last;    // 0 when !has_last_tx
    float    km_from_current;       // 0.0 when !has_last_tx
};

bool parsePayload(std::string_view body, Payload& out) noexcept;

}  // namespace rinha
