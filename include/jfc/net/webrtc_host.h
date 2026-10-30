// © Joseph Cameron - MIT License

#ifndef JFC_NET_WEBRTC_HOST_H
#define JFC_NET_WEBRTC_HOST_H

#include <jfc/net/transport.h>

#include <memory>
#include <string>
#include <string_view>

namespace jfc::net {
    struct webrtc_ice_server final {
        std::vector<std::string> urls;
        std::string username;
        std::string credential;
    };

    struct webrtc_host_settings final {
        std::size_t most_peers = 16;
        std::size_t most_message_bytes = 256 * 1024;
        std::size_t most_queued_messages = 256;
        std::size_t most_queued_bytes = 4 * 1024 * 1024;
        //! Combined browser send buffers per peer. Reliable send throws when full;
        //! unreliable send drops the whole message. No additional send queue is kept.
        std::size_t most_buffered_bytes = 1024 * 1024;
        std::size_t most_signal_bytes = 128 * 1024;
        std::uint32_t timeout_ms = 30000;
        std::vector<webrtc_ice_server> ice_servers;
        //! Use only TURN candidates. Requires a caller-provided TURN server.
        bool relay_only = false;
    };

    struct webrtc_signal final {
        peer_id peer = 0;
        //! Opaque, versioned JSON containing an offer or answer and all ICE candidates.
        std::string description;
    };

    enum class webrtc_failure {
        none,
        closed,
        negotiation,
        transport,
        protocol,
        receive_overflow,
        timeout,
    };

    struct webrtc_peer_status final {
        enum class phase { negotiating, connected, closing, disconnected };
        phase state = phase::negotiating;
        webrtc_failure failure = webrtc_failure::none;
        std::size_t buffered_bytes = 0;
    };

    /// \brief Emscripten browser WebRTC transport; link jfcnet_webrtc.
    ///
    /// Use on the browser main thread. No signaling service, timers, threads or
    /// application loop are created. Both ends must use this wire protocol.
    class webrtc_host final : public host {
    public:
        explicit webrtc_host(const webrtc_host_settings &aSettings = {});
        ~webrtc_host() override;

        webrtc_host(const webrtc_host &) = delete;
        webrtc_host &operator=(const webrtc_host &) = delete;

        //! Starts negotiation; pass the eventual poll_signal result to the other end.
        [[nodiscard]] peer_id offer();
        //! Accepts an offer, creates a peer, and eventually produces an answer in poll_signal.
        [[nodiscard]] peer_id accept(std::string_view aOffer);
        //! Supplies the answer to a peer created by offer(). An answer is accepted only once.
        void answer(peer_id aPeer, std::string_view aAnswer);
        [[nodiscard]] std::optional<webrtc_signal> poll_signal();

        //! Caller supplies elapsed time for negotiation and graceful-close deadlines.
        //! Call regularly, including while awaiting signals. Connected idle peers do
        //! not time out here: the browser's transport detects network failure.
        void update(std::uint32_t aElapsedMs);

        //! Available until that peer's disconnected event is consumed.
        [[nodiscard]] std::optional<webrtc_peer_status> status(peer_id aPeer) const;

        //! Logical messages are fragmented into <=12 KiB frames, respecting the
        //! negotiated SCTP limit. Reliable overflow throws before any frame is sent.
        void send(peer_id aPeer, delivery aDelivery, std::span<const std::byte> aMessage) override;
        //! Flushes already accepted reliable sends before closing, subject to timeout_ms.
        void disconnect(peer_id aPeer) override;
        [[nodiscard]] std::optional<event> poll() override;

    private:
        int m_Handle = 0;
        std::size_t m_MostMessageBytes = 0;
        std::size_t m_MostSignalBytes = 0;
    };
}

#endif
