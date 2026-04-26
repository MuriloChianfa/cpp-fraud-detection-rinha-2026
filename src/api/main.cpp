#include <cstdio>
#include <cstdlib>
#include <string>

#include <drogon/drogon.h>
#include <trantor/utils/Logger.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "api/handlers.h"
#include "api/responses.h"

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

    rinha::init_responses();
    rinha::registerHandlers();

    drogon::app()
        .setLogLevel(trantor::Logger::kError)
        .setThreadNum(1)
        .setIdleConnectionTimeout(60)
        .setKeepaliveRequestsNumber(1'000'000)
        .setMaxConnectionNum(8192)
        .enableServerHeader(false)
        .enableDateHeader(false)
        .setClientMaxBodySize(8 * 1024)
        .addListener(bind, port)
        .run();
    return 0;
}
