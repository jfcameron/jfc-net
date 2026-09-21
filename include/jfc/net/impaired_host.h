// © Joseph Cameron - All Rights Reserved

#ifndef JFC_NET_IMPAIRED_HOST_H
#define JFC_NET_IMPAIRED_HOST_H

#include <jfc/net/transport.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace jfc::net {
    /// \brief what an impaired_host does to what it sends
    ///
    /// The plain values impair nothing: a host with them sends as the host it wraps would.
    struct impairments final {
        //! delay in seconds before a message is sent. 
        double latency = 0;

        //! seconds more of delay at random, to simulate an uneven network connection
        double jitter = 0;

        //! 0 to 1 value that increases the chance a message isnt sent at all
        double loss = 0;

        //! RNG seed
        std::uint32_t seed = 1;

        /// whether a message may overtake one sent before it to the same peer. 
        bool reorder = false;
    };

    /// \brief a host that delays messages, drops them etc. deliberately unreliable to
    /// be used to simulate a bad network
    class impaired_host final : public host {
    public:
        using clock_type = std::function<double()>;

        [[nodiscard]] static double steady_seconds();

        /// \brief from now on, what is sent is impaired as aImpairments say. What is already held
        /// keeps the time it was given. The seed is taken only when this is made
        void set(const impairments &aImpairments);

        [[nodiscard]] const impairments &impaired() const { return m_Impairments; }

        //! messages and disconnections held not yet handed to the wrapped host
        [[nodiscard]] std::size_t held() const { return m_Held.size(); }

        void send(peer_id aPeer, delivery aDelivery, std::span<const std::byte> aMessage) override;
        void disconnect(peer_id aPeer) override;
        [[nodiscard]] std::optional<event> poll() override;

        /// \param pWrapped the host that sends what this has held
        /// \throws std::invalid_argument for no host, or an impairment out of range: a negative
        ///         time, or a chance outside nought to one
        impaired_host(std::shared_ptr<host> pWrapped, impairments aImpairments = {},
            clock_type aNow = steady_seconds);

    private:
        struct held_type final {
            peer_id peer = 0;
            delivery channel = delivery::reliable;
            std::vector<std::byte> data;

            bool ending = false;

            std::uint64_t sequence = 0;
        };

        //! everything held whose time has come, handed on, in the order of its time
        void release();

        //! zero to one, from the generator
        [[nodiscard]] double chance();

        std::shared_ptr<host> m_pWrapped;
        impairments m_Impairments;
        clock_type m_Now;
        std::mt19937 m_Random;

        std::multimap<std::pair<double, std::uint64_t>, held_type> m_Held;
        std::uint64_t m_Given = 0;

        std::map<peer_id, double> m_LastInOrder;

        std::map<peer_id, double> m_LastHeld;

        std::map<peer_id, std::uint64_t> m_UnreliableGiven, m_UnreliableSent;
    };
}

#endif
