#include "api/handlers.h"

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <utility>

#include <drogon/drogon.h>
#include <drogon/HttpAppFramework.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/HttpTypes.h>

#include "api/json_parser.h"
#include "api/knn_avx2.h"
#include "api/responses.h"
#include "api/vectorize.h"

namespace rinha {

namespace {

drogon::HttpResponsePtr g_not_found{};

void init_not_found() {
    auto resp = drogon::HttpResponse::newHttpResponse();
    resp->setStatusCode(drogon::k404NotFound);
    resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
    resp->setBody("");
    resp->setExpiredTime(-1);
    g_not_found = std::move(resp);
}

[[gnu::hot, gnu::flatten]]
void hot_route(const drogon::HttpRequestPtr& req,
               drogon::AdviceCallback&& cb) noexcept {
    const std::string& path = req->path();
    const auto method = req->method();

    if (method == drogon::Post &&
        path.size() == 12 &&
        std::memcmp(path.data(), "/fraud-score", 12) == 0) {
        Payload p{};
        if (parsePayload(req->getBody(), p)) {
            Query q;
            vectorize(p, q);
            const uint8_t fc = knn5_avx2_count(q);
            cb(fraudResp(fc));
        } else {
            cb(fraudResp(0));
        }
        return;
    }

    if (method == drogon::Get &&
        path.size() == 6 &&
        std::memcmp(path.data(), "/ready", 6) == 0) {
        cb(readyResp());
        return;
    }

    cb(g_not_found);
}

}

void registerHandlers() {
    auto& app = drogon::app();

    init_not_found();

    app.registerPreRoutingAdvice(
        [](const drogon::HttpRequestPtr& req,
           drogon::AdviceCallback&& cb,
           drogon::AdviceChainCallback&&) {
            hot_route(req, std::move(cb));
        });
}

}
