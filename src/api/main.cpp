#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "api/drogon_compat.h"
#include "api/handlers.h"
#include "api/responses.h"
#include "common/refs_data.h"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) {
        std::fprintf(stderr, "usage: %s <port> [bind_addr]\n", argv[0]);
        return 2;
    }
    int port = std::atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        std::fprintf(stderr, "invalid port: %s\n", argv[1]);
        return 2;
    }
    std::string bind = (argc == 3) ? argv[2] : std::string{"0.0.0.0"};

    std::signal(SIGPIPE, SIG_IGN);

    rinha::init_refs_residency();
    rinha::init_responses();
    rinha::registerHandlers();

    rinha::drogon_compat::set_before_listen_sockopt(drogon::app(), std::function<void(int)>{[](int fd) {
        int qlen = 4096;
        ::setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &qlen, sizeof(qlen));
        int one = 1;
        ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    }});

    rinha::drogon_compat::set_after_accept_sockopt(drogon::app(), std::function<void(int)>{[](int fd) {
        int one = 1;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        ::setsockopt(fd, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
        int lowat = 16 * 1024;
        ::setsockopt(fd, IPPROTO_TCP, TCP_NOTSENT_LOWAT, &lowat, sizeof(lowat));
    }});

    drogon::app()
        .setLogLevel(trantor::Logger::kError)
        .setThreadNum(1)
        .setIdleConnectionTimeout(3600)
        .setKeepaliveRequestsNumber(static_cast<size_t>(LLONG_MAX))
        .setMaxConnectionNum(8192)
        .enableServerHeader(false)
        .enableDateHeader(false)
        .setClientMaxBodySize(8 * 1024)
        .addListener(bind, port)
        .run();
    return 0;
}
