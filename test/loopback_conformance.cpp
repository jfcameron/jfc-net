// © Joseph Cameron - MIT License

#include "conformance.h"

#include <jfc/net/loopback.h>

namespace jfc::net::conformance {
    hosts make_hosts(const std::size_t aClients) {
        auto pServer = std::make_shared<loopback_server>();

        hosts out{pServer, {}, {}};

        for (std::size_t i = 0; i < aClients; ++i) out.clients.push_back(pServer->connect());

        return out;
    }

    void wait() {}

    std::string_view implementation() { return "loopback"; }
}
