// © Joseph Cameron - MIT License

#ifndef JFC_NET_CONFORMANCE_H
#define JFC_NET_CONFORMANCE_H

#include <jfc/net/transport.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace jfc::net::conformance {
    /// \brief a server's host and one host for each of its clients, connected or on the way to it
    struct hosts final {
        std::shared_ptr<host> pServer;
        std::vector<std::shared_ptr<host>> clients;

        //! the longest message these carry, none if they carry any length
        std::optional<std::size_t> most_message_bytes;
    };

    [[nodiscard]] hosts make_hosts(std::size_t aClients);

    //! gives the transport a moment to move what it has in hand
    void wait();

    [[nodiscard]] std::string_view implementation();
}

#endif
