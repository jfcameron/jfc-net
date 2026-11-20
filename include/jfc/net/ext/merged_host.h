// © Joseph Cameron - MIT License

#ifndef JFC_NET_EXT_MERGED_HOST_H
#define JFC_NET_EXT_MERGED_HOST_H

#include <jfc/net/transport.h>

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

namespace jfc::net::ext {
    /// \brief several hosts as one: the peers of all of them, each under an id of this host's own
    ///
    /// A peer is given an id when one of the hosts first reports it, and no id is given twice, whatever ids the hosts
    /// reuse. Each host's events keep their order; between hosts, poll takes turns, starting with the first given.
    class merged_host final : public host {
    public:
        /// \throws exception for no hosts, or a null one
        explicit merged_host(std::vector<std::shared_ptr<host>> aHosts);

        void send(peer_id aPeer, delivery aDelivery, std::span<const std::byte> aMessage) override;
        void disconnect(peer_id aPeer) override;
        /// \throws exception once every id has been given
        [[nodiscard]] std::optional<event> poll() override;

    private:
        struct inner final {
            std::size_t host = 0;
            peer_id peer = 0;
        };

        std::vector<std::shared_ptr<host>> mHosts;
        std::map<peer_id, inner> mInner;
        std::vector<std::map<peer_id, peer_id>> mOuter;
        peer_id mNext = 1;
        std::size_t mTurn = 0;
    };
}

#endif
