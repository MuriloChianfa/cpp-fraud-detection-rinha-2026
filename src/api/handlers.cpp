#include "api/handlers.h"

#include <cstdint>
#include <functional>
#include <string_view>

#include <drogon/drogon.h>

#include "api/json_parser.h"
#include "api/knn_avx2.h"
#include "api/responses.h"
#include "api/vectorize.h"

namespace rinha {

void registerHandlers() {
    auto& app = drogon::app();

    app.registerHandler(
        "/fraud-score",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            Payload p{};
            if (!parsePayload(req->getBody(), p)) {
                cb(fraudResp(0));
                return;
            }
            Query q;
            vectorize(p, q);
            const uint8_t fc = knn5_avx2_count(q);
            cb(fraudResp(fc));
        },
        {drogon::Post});

    app.registerHandler(
        "/ready",
        [](const drogon::HttpRequestPtr&,
           std::function<void(const drogon::HttpResponsePtr&)>&& cb) {
            cb(readyResp());
        },
        {drogon::Get});
}

}  // namespace rinha
