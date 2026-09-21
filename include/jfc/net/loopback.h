// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_LOOPBACK_H
#define JFC_NET_LOOPBACK_H

#include <jfc/net/transport.h>

#include <memory>

namespace jfc::net {
    /// \brief a transport within one process: a server, and the clients connected to it
    ///
    /// Not thread safe: a server and all of its clients are one host, as far as threads go, and are
    /// used from one thread at a time.
    class loopback_server final : public host {
    public:
        /// \brief a client connected to this server. Both sides are told with a connected event;
        /// messages may be sent either way before it is polled. The client knows its server as
        /// SERVER_PEER
        [[nodiscard]] std::unique_ptr<host> connect();

        static constexpr peer_id SERVER_PEER = 0;

        void send(const peer_id aPeer, const delivery aDelivery, const std::span<const std::byte> aMessage) override;
        void disconnect(const peer_id aPeer) override;
        [[nodiscard]] std::optional<event> poll() override;

        loopback_server();
        ~loopback_server() override;
        loopback_server(const loopback_server &) = delete;
        loopback_server &operator=(const loopback_server &) = delete;
        loopback_server(loopback_server &&) = delete;
        loopback_server &operator=(loopback_server &&) = delete;

    private:
        struct _state;
        class _client;

        std::shared_ptr<_state> mpState;
    };
}

#endif
