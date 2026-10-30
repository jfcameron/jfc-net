// © Joseph Cameron - MIT License

#include <jfc/net/webrtc_host.h>
#include <jfc/net/exception.h>

extern "C" {
    int jfc_net_rtc_create(unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, int);
    void jfc_net_rtc_destroy(int);
    int jfc_net_rtc_ice(int, const char *, unsigned, const char *, unsigned, const char *, unsigned, int);
    unsigned jfc_net_rtc_offer(int);
    unsigned jfc_net_rtc_accept(int, const char *, unsigned);
    int jfc_net_rtc_answer(int, unsigned, const char *, unsigned);
    void jfc_net_rtc_update(int, unsigned);
    int jfc_net_rtc_status(int, unsigned, unsigned *);
    int jfc_net_rtc_send(int, unsigned, int, const void *, unsigned);
    void jfc_net_rtc_disconnect(int, unsigned);
    int jfc_net_rtc_peek(int, int, unsigned *);
    void jfc_net_rtc_take(int, int, void *);
}

namespace jfc::net {
    namespace {
        unsigned bounded(const std::size_t aValue) {
            if (!aValue || aValue > 16 * 1024 * 1024)
                throw exception("jfc::net::webrtc_host: invalid resource limit");
            return static_cast<unsigned>(aValue);
        }

        void check_signal(const std::string_view aSignal, const std::size_t aMost) {
            if (aSignal.empty() || aSignal.size() > aMost || aSignal.find('\0') != std::string_view::npos)
                throw exception("jfc::net::webrtc_host: invalid signaling description");
        }
    }

    webrtc_host::webrtc_host(const webrtc_host_settings &aSettings)
    : m_MostMessageBytes(aSettings.most_message_bytes), m_MostSignalBytes(aSettings.most_signal_bytes) {
        if (!aSettings.most_peers || aSettings.most_peers > 1024 || !aSettings.timeout_ms ||
            aSettings.most_buffered_bytes < aSettings.most_message_bytes + 16 ||
            aSettings.most_queued_bytes < aSettings.most_message_bytes)
            throw exception("jfc::net::webrtc_host: inconsistent resource limits");

        m_Handle = jfc_net_rtc_create(static_cast<unsigned>(aSettings.most_peers),
            bounded(aSettings.most_message_bytes), bounded(aSettings.most_queued_messages),
            bounded(aSettings.most_queued_bytes), bounded(aSettings.most_buffered_bytes),
            bounded(aSettings.most_signal_bytes), aSettings.timeout_ms, aSettings.relay_only);

        if (!m_Handle) throw exception("jfc::net::webrtc_host: browser WebRTC is unavailable");

        try {
            if (aSettings.ice_servers.size() > 32)
                throw exception("jfc::net::webrtc_host: too many ICE servers");
            for (const auto &server : aSettings.ice_servers) {
                if (server.urls.empty() || server.urls.size() > 32)
                    throw exception("jfc::net::webrtc_host: invalid ICE server URLs");
                bool first = true;
                for (const auto &url : server.urls) {
                    if (url.empty() || url.size() > 4096 || server.username.size() > 4096 ||
                        server.credential.size() > 4096 || url.find('\0') != std::string::npos ||
                        server.username.find('\0') != std::string::npos || server.credential.find('\0') != std::string::npos ||
                        !jfc_net_rtc_ice(m_Handle, url.data(), static_cast<unsigned>(url.size()),
                            server.username.data(), static_cast<unsigned>(server.username.size()),
                            server.credential.data(), static_cast<unsigned>(server.credential.size()), first))
                        throw exception("jfc::net::webrtc_host: invalid ICE server configuration");
                    first = false;
                }
            }
        } catch (...) {
            jfc_net_rtc_destroy(m_Handle);
            throw;
        }
    }

    webrtc_host::~webrtc_host() { jfc_net_rtc_destroy(m_Handle); }

    peer_id webrtc_host::offer() {
        const auto peer = jfc_net_rtc_offer(m_Handle);
        if (!peer) throw exception("jfc::net::webrtc_host: could not create an offer (peer limit or browser failure)");
        return peer;
    }

    peer_id webrtc_host::accept(const std::string_view aOffer) {
        check_signal(aOffer, m_MostSignalBytes);
        const auto peer = jfc_net_rtc_accept(m_Handle, aOffer.data(), static_cast<unsigned>(aOffer.size()));
        if (!peer) throw exception("jfc::net::webrtc_host: invalid offer, peer limit or browser failure");
        return peer;
    }

    void webrtc_host::answer(const peer_id aPeer, const std::string_view aAnswer) {
        check_signal(aAnswer, m_MostSignalBytes);
        if (!jfc_net_rtc_answer(m_Handle, aPeer, aAnswer.data(), static_cast<unsigned>(aAnswer.size())))
            throw exception("jfc::net::webrtc_host: invalid answer or peer is not awaiting one");
    }

    void webrtc_host::update(const std::uint32_t aElapsedMs) { jfc_net_rtc_update(m_Handle, aElapsedMs); }

    std::optional<webrtc_peer_status> webrtc_host::status(const peer_id aPeer) const {
        unsigned values[3]{};
        if (!jfc_net_rtc_status(m_Handle, aPeer, values)) return {};
        return webrtc_peer_status{static_cast<webrtc_peer_status::phase>(values[0]),
            static_cast<webrtc_failure>(values[1]), values[2]};
    }

    void webrtc_host::send(const peer_id aPeer, const delivery aDelivery, const std::span<const std::byte> aMessage) {
        if (aMessage.size() > m_MostMessageBytes)
            throw exception("jfc::net::webrtc_host: message exceeds most_message_bytes");
        const auto result = jfc_net_rtc_send(m_Handle, aPeer, aDelivery == delivery::unreliable,
            aMessage.data(), static_cast<unsigned>(aMessage.size()));
        if (result == 1) throw exception("jfc::net::webrtc_host: reliable send buffer is full");
        if (result == 2) throw exception("jfc::net::webrtc_host: browser send failed; peer disconnected");
    }

    void webrtc_host::disconnect(const peer_id aPeer) { jfc_net_rtc_disconnect(m_Handle, aPeer); }

    std::optional<event> webrtc_host::poll() {
        unsigned meta[4]{};
        if (!jfc_net_rtc_peek(m_Handle, 0, meta)) return {};
        event result{static_cast<event::kind>(meta[0]), meta[1], static_cast<delivery>(meta[2]), {}};
        result.data.resize(meta[3]);
        jfc_net_rtc_take(m_Handle, 0, result.data.data());
        return result;
    }

    std::optional<webrtc_signal> webrtc_host::poll_signal() {
        unsigned meta[4]{};
        if (!jfc_net_rtc_peek(m_Handle, 1, meta)) return {};
        webrtc_signal result{meta[1], std::string(meta[3], '\0')};
        jfc_net_rtc_take(m_Handle, 1, result.description.data());
        return result;
    }
}
