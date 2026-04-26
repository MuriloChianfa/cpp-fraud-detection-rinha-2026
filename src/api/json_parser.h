#pragma once

#include <cstdint>
#include <string_view>

namespace rinha {

struct Payload {
    float    amount;
    uint8_t  installments;
    uint8_t  hour;
    uint8_t  day_of_week;
    float    customer_avg_amount;
    uint32_t tx_count_24h;
    uint32_t mcc;
    float    merchant_avg_amount;
    bool     is_online;
    bool     card_present;
    float    km_from_home;
    bool     is_unknown_merchant;
    bool     has_last_tx;
    uint32_t minutes_since_last;
    float    km_from_current;
};

bool parsePayload(std::string_view body, Payload& out) noexcept;

}
