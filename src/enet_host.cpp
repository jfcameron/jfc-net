// © Joseph Cameron - All Rights Reserved

#include <jfc/net/enet_host.h>

#include <enet/enet.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace jfc::net {
    namespace {
        constexpr enet_uint8 RELIABLE = 0;
        constexpr enet_uint8 UNRELIABLE = 1;
        constexpr std::size_t CHANNELS = 2;

        class library final {
        public:
            library() {
                const std::lock_guard lock(mutex());

                if (count()++ == 0 && enet_initialize() != 0) {
                    --count();

                    throw std::runtime_error("jfc::net::enet_host: ENet would not start");
                }
            }

            ~library() {
                const std::lock_guard lock(mutex());

                if (--count() == 0) enet_deinitialize();
            }

            library(const library &) = delete;
            library &operator=(const library &) = delete;

        private:
            static std::mutex &mutex() { static std::mutex m; return m; }
            static std::size_t &count() { static std::size_t c = 0; return c; }
        };
    }

    struct enet_host::impl final {
        library started;

        ENetHost *pHost = nullptr;

        std::map<peer_id, ENetPeer *> peers;
        peer_id next = 1;

        peer_id server = 0;

        enet_host_settings settings;

        void configure(ENetPeer *const pPeer) const {
            enet_peer_timeout(pPeer, 0, settings.timeout_ms, settings.timeout_ms);

            enet_peer_throttle_configure(pPeer, settings.throttle_interval_ms, settings.throttle_acceleration,
                settings.throttle_deceleration);
        }

        ~impl() {
            if (!pHost) return;

            for (auto &[id, pPeer] : peers) enet_peer_disconnect_now(pPeer, 0);

            enet_host_flush(pHost);
            enet_host_destroy(pHost);
        }

        peer_id id_of(ENetPeer *const pPeer) {
            if (pPeer->data) return static_cast<peer_id>(reinterpret_cast<std::uintptr_t>(pPeer->data));

            const auto id = next++;

            pPeer->data = reinterpret_cast<void *>(static_cast<std::uintptr_t>(id));

            peers.emplace(id, pPeer);

            return id;
        }
    };

    enet_host::enet_host(std::unique_ptr<impl> pImpl)
    : m_pImpl(std::move(pImpl)) {}

    enet_host::~enet_host() = default;

    std::unique_ptr<enet_host> enet_host::listen(const std::uint16_t aPort, const enet_host_settings aSettings) {
        auto pImpl = std::make_unique<impl>();

        pImpl->settings = aSettings;

        ENetAddress address{};

        address.host = ENET_HOST_ANY;
        address.port = aPort;

        pImpl->pHost = enet_host_create(&address, aSettings.most_peers, CHANNELS, 0, 0);

        if (!pImpl->pHost)
            throw std::runtime_error("jfc::net::enet_host: could not listen on port " + std::to_string(aPort));

        return std::unique_ptr<enet_host>(new enet_host(std::move(pImpl)));
    }

    std::unique_ptr<enet_host> enet_host::connect(const std::string &aAddress, const std::uint16_t aPort,
        const enet_host_settings aSettings) {
        auto pImpl = std::make_unique<impl>();

        pImpl->settings = aSettings;

        pImpl->pHost = enet_host_create(nullptr, aSettings.most_peers, CHANNELS, 0, 0);

        if (!pImpl->pHost) throw std::runtime_error("jfc::net::enet_host: could not make a host to connect from");

        ENetAddress address{};

        if (enet_address_set_host(&address, aAddress.c_str()) != 0)
            throw std::runtime_error("jfc::net::enet_host: there is no " + aAddress + " to connect to");

        address.port = aPort;

        ENetPeer *const pServer = enet_host_connect(pImpl->pHost, &address, CHANNELS, 0);

        if (!pServer) throw std::runtime_error("jfc::net::enet_host: could not begin connecting to " + aAddress);

        pImpl->server = pImpl->id_of(pServer);

        return std::unique_ptr<enet_host>(new enet_host(std::move(pImpl)));
    }

    std::uint16_t enet_host::port() const {
        ENetAddress address{};

        if (enet_socket_get_address(m_pImpl->pHost->socket, &address) != 0) return m_pImpl->pHost->address.port;

        return address.port;
    }

    peer_id enet_host::server_peer() const {
        return m_pImpl->server;
    }

    void enet_host::send(const peer_id aPeer, const delivery aDelivery, const std::span<const std::byte> aMessage) {
        const auto found = m_pImpl->peers.find(aPeer);

        if (found == m_pImpl->peers.end() || found->second->state != ENET_PEER_STATE_CONNECTED) return;

        const bool reliable = aDelivery == delivery::reliable;

        ENetPacket *const pPacket = enet_packet_create(aMessage.data(), aMessage.size(),
            reliable ? ENET_PACKET_FLAG_RELIABLE : 0);

        if (enet_peer_send(found->second, reliable ? RELIABLE : UNRELIABLE, pPacket) != 0) enet_packet_destroy(pPacket);

        enet_host_flush(m_pImpl->pHost);
    }

    void enet_host::disconnect(const peer_id aPeer) {
        const auto found = m_pImpl->peers.find(aPeer);

        if (found == m_pImpl->peers.end()) return;

        enet_peer_disconnect_later(found->second, 0);

        enet_host_flush(m_pImpl->pHost);
    }

    std::optional<event> enet_host::poll() {
        ENetEvent happened{};

        while (enet_host_service(m_pImpl->pHost, &happened, 0) > 0) {
            switch (happened.type) {
                case ENET_EVENT_TYPE_CONNECT:
                    m_pImpl->configure(happened.peer);

                    return event{event::kind::connected, m_pImpl->id_of(happened.peer), delivery::reliable, {}};

                case ENET_EVENT_TYPE_RECEIVE: {
                    const auto *const pBytes = reinterpret_cast<const std::byte *>(happened.packet->data);

                    event out{event::kind::received, m_pImpl->id_of(happened.peer),
                        happened.channelID == RELIABLE ? delivery::reliable : delivery::unreliable,
                        {pBytes, pBytes + happened.packet->dataLength}};

                    enet_packet_destroy(happened.packet);

                    return out;
                }

                case ENET_EVENT_TYPE_DISCONNECT: {
                    const auto id = m_pImpl->id_of(happened.peer);

                    m_pImpl->peers.erase(id);

                    happened.peer->data = nullptr;

                    return event{event::kind::disconnected, id, delivery::reliable, {}};
                }

                case ENET_EVENT_TYPE_NONE: break;
            }
        }

        return std::nullopt;
    }
}
