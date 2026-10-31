// © Joseph Cameron - MIT License

#include "conformance.h"

#include <jfc/net/ext/impaired_host.h>
#include <jfc/net/loopback.h>

namespace jfc::net::conformance {
    hosts make_hosts(const std::size_t aClients) {
        auto pLoopback = std::make_shared<loopback_server>();

        hosts out{std::make_shared<ext::impaired_host>(pLoopback), {}, {}};

        for (std::size_t i = 0; i < aClients; ++i)
            out.clients.push_back(std::make_shared<ext::impaired_host>(std::shared_ptr<host>(pLoopback->connect())));

        return out;
    }

    void wait() {}

    std::string_view implementation() { return "impaired loopback"; }
}
