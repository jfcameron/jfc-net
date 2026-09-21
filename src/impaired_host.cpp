// © Joseph Cameron - All Rights Reserved

#include <jfc/net/impaired_host.h>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>

namespace jfc::net {
    namespace {
        void validate(const impairments &aImpairments) {
            const auto refused = [](const std::string &aWhy) {
                return std::invalid_argument("jfc::net::impaired_host: " + aWhy);
            };

            if (!(aImpairments.latency >= 0)) throw refused("a latency of less than nothing");
            if (!(aImpairments.jitter >= 0)) throw refused("a jitter of less than nothing");
            if (!(aImpairments.loss >= 0 && aImpairments.loss <= 1)) throw refused("a loss that is no chance: nought to one");
        }
    }

    double impaired_host::steady_seconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    impaired_host::impaired_host(std::shared_ptr<host> pWrapped, impairments aImpairments, clock_type aNow)
    : m_pWrapped(std::move(pWrapped))
    , m_Impairments(aImpairments)
    , m_Now(std::move(aNow))
    , m_Random(aImpairments.seed) {
        if (!m_pWrapped) throw std::invalid_argument("jfc::net::impaired_host: needs a host to wrap");
        if (!m_Now) throw std::invalid_argument("jfc::net::impaired_host: needs a clock");

        validate(m_Impairments);
    }

    void impaired_host::set(const impairments &aImpairments) {
        validate(aImpairments);

        m_Impairments = aImpairments;
    }

    double impaired_host::chance() {
        return static_cast<double>(m_Random()) / (static_cast<double>(std::mt19937::max()) + 1.0);
    }

    void impaired_host::send(const peer_id aPeer, const delivery aDelivery, const std::span<const std::byte> aMessage) {
        release();

        const bool reliable = aDelivery == delivery::reliable;

        const double lost = chance(), held = chance();

        if (!reliable && lost < m_Impairments.loss) return;

        double when = m_Now() + m_Impairments.latency + held * m_Impairments.jitter;

        held_type out{aPeer, aDelivery, {aMessage.begin(), aMessage.end()}, false, 0};

        auto &lastHeld = m_LastHeld[aPeer];

        if (!m_Impairments.reorder) when = std::max(when, lastHeld);

        lastHeld = std::max(lastHeld, when);

        if (reliable) {
            auto &last = m_LastInOrder[aPeer];

            when = std::max(when, last);
            last = when;
        }
        else out.sequence = ++m_UnreliableGiven[aPeer];

        m_Held.emplace(std::pair{when, m_Given++}, std::move(out));
    }

    void impaired_host::disconnect(const peer_id aPeer) {
        release();

        double when = m_Now() + m_Impairments.latency;

        for (const auto &[at, each] : m_Held) if (each.peer == aPeer) when = std::max(when, at.first);

        auto &last = m_LastInOrder[aPeer];

        when = std::max(when, last);
        last = when;

        m_LastHeld[aPeer] = std::max(m_LastHeld[aPeer], when);

        m_Held.emplace(std::pair{when, m_Given++}, held_type{aPeer, delivery::reliable, {}, true, 0});
    }

    std::optional<event> impaired_host::poll() {
        release();

        return m_pWrapped->poll();
    }

    void impaired_host::release() {
        const double now = m_Now();

        while (!m_Held.empty() && m_Held.begin()->first.first <= now) {
            auto each = std::move(m_Held.begin()->second);

            m_Held.erase(m_Held.begin());

            if (each.ending) {
                m_pWrapped->disconnect(each.peer);

                continue;
            }

            if (each.channel == delivery::unreliable) {
                auto &newest = m_UnreliableSent[each.peer];

                if (each.sequence < newest) continue;

                newest = each.sequence;
            }

            m_pWrapped->send(each.peer, each.channel, each.data);
        }
    }
}
