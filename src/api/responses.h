#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include <drogon/HttpResponse.h>

namespace rinha {

inline constexpr std::array<std::string_view, 6> kBodies = {
    std::string_view{R"({"approved":true,"fraud_score":0.0})"},
    std::string_view{R"({"approved":true,"fraud_score":0.2})"},
    std::string_view{R"({"approved":true,"fraud_score":0.4})"},
    std::string_view{R"({"approved":false,"fraud_score":0.6})"},
    std::string_view{R"({"approved":false,"fraud_score":0.8})"},
    std::string_view{R"({"approved":false,"fraud_score":1.0})"},
};

void init_responses();

const drogon::HttpResponsePtr& fraudResp(uint8_t fraud_count) noexcept;
const drogon::HttpResponsePtr& readyResp() noexcept;

}  // namespace rinha
