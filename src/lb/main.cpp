#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <trantor/net/EventLoop.h>
#include <trantor/utils/Logger.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

namespace {

constexpr int kClientsPerBackend = 4;

std::vector<std::string> g_backends;

struct ThreadClients {
    std::vector<drogon::HttpClientPtr> clients;
    size_t                             rr = 0;
};

[[gnu::always_inline]] inline ThreadClients& clients_for_thread() {
    thread_local ThreadClients tc;
    if (tc.clients.empty()) {
        auto* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        tc.clients.reserve(g_backends.size() * kClientsPerBackend);
        for (int c = 0; c < kClientsPerBackend; ++c) {
            for (const auto& url : g_backends) {
                auto cli = drogon::HttpClient::newHttpClient(url, loop);
                cli->setPipeliningDepth(64);
                tc.clients.push_back(std::move(cli));
            }
        }
    }
    return tc;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <port> <backend1> [backend2 ...]\n", argv[0]);
        return 2;
    }

    int port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "invalid port: %s\n", argv[1]);
        return 2;
    }

    for (int i = 2; i < argc; ++i) {
        std::string url = argv[i];
        if (url.find("://") == std::string::npos) {
            url = "http://" + url;
        }
        g_backends.push_back(std::move(url));
    }
    if (g_backends.empty()) {
        std::fprintf(stderr, "no backends specified\n");
        return 2;
    }

    using namespace drogon;

    app().registerPreRoutingAdvice(
        [](const HttpRequestPtr& req,
           AdviceCallback&& cb,
           AdviceChainCallback&& /*chain*/) {
            req->setPassThrough(true);
            auto& tc = clients_for_thread();
            auto& cli = tc.clients[tc.rr % tc.clients.size()];
            ++tc.rr;
            cli->sendRequest(req,
                [cb = std::move(cb)](ReqResult r, const HttpResponsePtr& resp) {
                    if (r == ReqResult::Ok && resp) {
                        resp->setPassThrough(true);
                        cb(resp);
                    } else {
                        auto err = HttpResponse::newHttpResponse();
                        err->setStatusCode(k502BadGateway);
                        cb(err);
                    }
                });
        });

    app()
        .setLogLevel(trantor::Logger::kError)
        .setThreadNum(1)
        .setIdleConnectionTimeout(60)
        .setKeepaliveRequestsNumber(1'000'000)
        .setMaxConnectionNum(8192)
        .enableServerHeader(false)
        .enableDateHeader(false)
        .setClientMaxBodySize(8 * 1024)
        .addListener("0.0.0.0", port)
        .run();
    return 0;
}
