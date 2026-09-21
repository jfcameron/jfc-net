// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_TRANSPORT_H
#define JFC_NET_TRANSPORT_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace jfc::net {
    /// \brief who a message is to or from as one host knows them
    using peer_id = std::uint32_t;

    /// \brief how a message travels. The first is the default, since it is the one that promises everything
    enum class delivery {
        /// arrives, whole, once, and after everything sent reliably to the same peer before it.
        /// For what must not be lost: a chunk, a spawn, a chat line, a request and its answer
        reliable,
        /// may be lost, and is never delivered after a newer unreliable message from the same peer:
        /// a late one is dropped rather than arriving out of order. For what a newer copy replaces:
        /// inputs, snapshots. Unordered with respect to reliable messages
        unreliable,
    };

    /// \brief something that happened on a host, returned by host::poll
    struct event final {
        enum class kind {
            /// a peer is there: messages to it can be sent, and it can be sent from
            connected,
            /// a message from a peer, in bytes
            received,
            /// a peer is gone, by either side's choice or because the connection failed; it is the
            /// last event about that peer, and sending to it after does nothing
            disconnected,
        };

        kind type = kind::connected;
        peer_id peer = 0;
        delivery channel = delivery::reliable;
        std::vector<std::byte> data;
    };

    /// \brief one end of a transport: sends messages to peers, and reports what arrived
    ///
    /// Not thread safe: one host is used from one thread at a time. Implementations: loopback.h, for
    /// one process and for tests; enet_host.h, over UDP.
    class host {
    public:
        /// \brief queues a message to a peer. 
        virtual void send(const peer_id aPeer, const delivery aDelivery, const std::span<const std::byte> aMessage) = 0;

        /// \brief ends a connection. Messages already sent to the peer still arrive, before it learns
        /// it is disconnected. 
        virtual void disconnect(const peer_id aPeer) = 0;

        /// \brief the next thing that happened, oldest first, or nothing if nothing is waiting
        [[nodiscard]] virtual std::optional<event> poll() = 0;

        virtual ~host() = default;
    };
}

#endif
