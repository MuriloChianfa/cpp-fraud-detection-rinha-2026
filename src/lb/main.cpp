#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <functional>
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

#include "api/drogon_compat.h"

namespace {

constexpr int kClientsPerBackend = 8;

std::vector<std::string> g_backends;

struct ThreadClients {
    std::vector<drogon::HttpClientPtr> clients;
    size_t                             rr = 0;
};

[[gnu::hot]] inline void tune_upstream_sock(int fd) noexcept {
    int one = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,  &one, sizeof(one));
    ::setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
    ::setsockopt(fd, SOL_SOCKET,  SO_KEEPALIVE, &one, sizeof(one));

    int tfo = 1;
    ::setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN_CONNECT, &tfo, sizeof(tfo));

    int idle = 60;
    int intvl = 10;
    int cnt = 3;
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    ::setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));

    int lowat = 16 * 1024;
    ::setsockopt(fd, IPPROTO_TCP, TCP_NOTSENT_LOWAT, &lowat, sizeof(lowat));
}

[[gnu::always_inline]] inline ThreadClients& clients_for_thread() {
    thread_local ThreadClients tc;
    if (tc.clients.empty()) {
        auto* loop = trantor::EventLoop::getEventLoopOfCurrentThread();
        tc.clients.reserve(g_backends.size() * kClientsPerBackend);
        for (int c = 0; c < kClientsPerBackend; ++c) {
            for (const auto& url : g_backends) {
                auto cli = drogon::HttpClient::newHttpClient(url, loop);
                cli->setPipeliningDepth(1);
                cli->setSockOptCallback(tune_upstream_sock);
                tc.clients.push_back(std::move(cli));
            }
        }
    }
    return tc;
}

[[gnu::hot, gnu::flatten]]
void hot_relay(const drogon::HttpRequestPtr& req,
               drogon::AdviceCallback&& cb) noexcept {
    req->setPassThrough(true);
    auto& tc = clients_for_thread();
    auto& cli = tc.clients[tc.rr % tc.clients.size()];
    ++tc.rr;
    cli->sendRequest(req,
        [cb = std::move(cb)](drogon::ReqResult r,
                             const drogon::HttpResponsePtr& resp) {
            if (r == drogon::ReqResult::Ok && resp) {
                resp->setPassThrough(true);
                cb(resp);
            } else {
                auto err = drogon::HttpResponse::newHttpResponse();
                err->setStatusCode(drogon::k502BadGateway);
                cb(err);
            }
        });
}

}

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

    std::signal(SIGPIPE, SIG_IGN);

    using namespace drogon;

    app().registerPreRoutingAdvice(
        [](const HttpRequestPtr& req,
           AdviceCallback&& cb,
           AdviceChainCallback&&) {
            hot_relay(req, std::move(cb));
        });

    rinha::drogon_compat::set_before_listen_sockopt(app(), std::function<void(int)>{[](int fd) {
        int qlen = 4096;
        ::setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }});

    rinha::drogon_compat::set_after_accept_sockopt(app(), std::function<void(int)>{[](int fd) {
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ::setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
        int lowat = 16 * 1024;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NOTSENT_LOWAT, &lowat, sizeof(lowat));
    }});

    app()
        .setLogLevel(trantor::Logger::kError)
        .setThreadNum(1)
        .setIdleConnectionTimeout(3600)
        .setKeepaliveRequestsNumber(static_cast<size_t>(LLONG_MAX))
        .setMaxConnectionNum(8192)
        .enableServerHeader(false)
        .enableDateHeader(false)
        .setClientMaxBodySize(8 * 1024)
        .addListener("0.0.0.0", port)
        .run();
    return 0;
}
