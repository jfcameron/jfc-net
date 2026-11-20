// © Joseph Cameron - MIT License

#include "conformance.h"

#include <jfc/net/ext/merged_host.h>
#include <jfc/net/loopback.h>

namespace jfc::net::conformance {
    hosts make_hosts(const std::size_t aClients) {
        const std::vector<std::shared_ptr<loopback_server>> servers{std::make_shared<loopback_server>(),
            std::make_shared<loopback_server>()};

        hosts out{std::make_shared<ext::merged_host>(std::vector<std::shared_ptr<host>>(servers.begin(), servers.end())),
            {}, {}};

        for (std::size_t i = 0; i < aClients; ++i)
            out.clients.push_back(std::shared_ptr<host>(servers[(i + 1) % servers.size()]->connect()));

        return out;
    }

    void wait() {}

    std::string_view implementation() { return "merged loopbacks"; }
}
