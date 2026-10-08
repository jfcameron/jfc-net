// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_LOOPBACK_H
#define JFC_NET_LOOPBACK_H

#include <jfc/net/transport.h>

#include <memory>

namespace jfc::net {
    /// \brief a transport within one process: a server, and the clients connected to it
    ///
    /// Thread safe: the server and each of its clients may each be used from a thread of its
    /// own, eg a game whose server steps on a thread beside the one that draws.
    class loopback_server final : public host {
    public:
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
