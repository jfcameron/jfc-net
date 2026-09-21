// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_ENET_HOST_H
#define JFC_NET_ENET_HOST_H

#include <jfc/net/transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace jfc::net {
    struct enet_host_settings final {
        //! the most peers it will have at once. e.g: max player count on a server
        std::size_t most_peers = 64;

        std::uint32_t timeout_ms = 5000;

        /// \name How much of what is sent unreliably ENet lets through
        /// @{
        std::uint32_t throttle_interval_ms = 5000;
        std::uint32_t throttle_acceleration = 32;
        std::uint32_t throttle_deceleration = 0;
        /// @}
    };

    /// \brief a transport between processes, over UDP using ENet
    ///
    /// Not thread safe.
    class enet_host final : public host {
    public:
        /// \brief a server's end: list for connections on aPort
        [[nodiscard]] static std::unique_ptr<enet_host> listen(std::uint16_t aPort, enet_host_settings aSettings = {});

        /// \brief a client's end: connecting to a server at aAddress, and aPort
        [[nodiscard]] static std::unique_ptr<enet_host> connect(const std::string &aAddress, std::uint16_t aPort,
            enet_host_settings aSettings = {});

        //! the port being used
        [[nodiscard]] std::uint16_t port() const;

        //! for a client to check if it is connected or not
        [[nodiscard]] peer_id server_peer() const;

        void send(peer_id aPeer, delivery aDelivery, std::span<const std::byte> aMessage) override;
        void disconnect(peer_id aPeer) override;
        [[nodiscard]] std::optional<event> poll() override;

        ~enet_host() override;

        enet_host(const enet_host &) = delete;
        enet_host &operator=(const enet_host &) = delete;

    private:
        struct impl;

        explicit enet_host(std::unique_ptr<impl> pImpl);

        std::unique_ptr<impl> m_pImpl;
    };
}

#endif
