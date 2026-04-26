#include "api/responses.h"

#include <array>

#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

namespace rinha {

namespace {

std::array<drogon::HttpResponsePtr, 6> g_fraud_responses{};
drogon::HttpResponsePtr               g_ready_response{};

drogon::HttpResponsePtr build_json(std::string_view body) {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k200OK);
    resp->setContentTypeCode(drogon::CT_APPLICATION_JSON);
    resp->setBody(std::string(body));
    resp->setExpiredTime(-1);  // reuse pre-rendered wire bytes for every request
    return resp;
}

}  // namespace

void init_responses() {
    for (size_t i = 0; i < kBodies.size(); ++i) {
        g_fraud_responses[i] = build_json(kBodies[i]);
    }

    auto ready = drogon::HttpResponse::newHttpResponse();
    ready->setStatusCode(drogon::k200OK);
    ready->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    ready->setBody("OK");
    ready->setExpiredTime(-1);
    g_ready_response = std::move(ready);
}

const drogon::HttpResponsePtr& fraudResp(uint8_t fraud_count) noexcept {
    if (fraud_count > 5) fraud_count = 5;
    return g_fraud_responses[fraud_count];
}

const drogon::HttpResponsePtr& readyResp() noexcept {
    return g_ready_response;
}

}  // namespace rinha
