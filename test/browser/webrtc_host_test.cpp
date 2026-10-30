// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>
#include <jfc/net/webrtc_host.h>
#include <jfc/net/exception.h>

#include <emscripten.h>

using namespace jfc::net;

TEST_CASE("WebRTC validates resource and signaling inputs", "[net][webrtc]") {
    webrtc_host_settings settings;
    settings.most_peers = 0;
    REQUIRE_THROWS_AS(webrtc_host(settings), exception);
    settings = {};
    settings.ice_servers.push_back({{"https://not-an-ice-server"}, {}, {}});
    REQUIRE_THROWS_AS(webrtc_host(settings), exception);
    webrtc_host connection;
    REQUIRE_THROWS_AS(connection.accept("{}"), exception);
    REQUIRE_THROWS_AS(connection.accept(std::string(129 * 1024, 'x')), exception);
    REQUIRE_THROWS_AS(connection.answer(999, "{}"), exception);
    REQUIRE_FALSE(connection.poll());
    REQUIRE_FALSE(connection.poll_signal());
    REQUIRE_FALSE(connection.status(999));
}

TEST_CASE("WebRTC bridge carries events and preserves send gating", "[net][webrtc]") {
    webrtc_host connection;
    const auto peer = connection.offer();
    REQUIRE(peer == 1);
    REQUIRE(connection.status(peer)->state == webrtc_peer_status::phase::negotiating);
    const std::byte data[]{std::byte{0}, std::byte{255}};
    connection.send(peer, delivery::reliable, data);
    REQUIRE_FALSE(connection.poll());
    EM_ASM({
        const pc = JfcRtcFixture.peers[JfcRtcFixture.peers.length - 1];
        JfcRtcFixture.open(pc);
        JfcRtcFixture.ready(pc);
        // Test-owned wire packet, independent of production game data.
        const frame = new Uint8Array(18);
        const header = new DataView(frame.buffer);
        header.setUint32(0, 0x3143464a, true);
        header.setUint32(8, 2, true);
        frame[16] = 0; frame[17] = 255;
        pc.channels[0].onmessage({data: frame.buffer});
    });
    const auto connected = connection.poll();
    REQUIRE(connected->type == event::kind::connected);
    REQUIRE(connected->peer == peer);
    const auto message = connection.poll();
    REQUIRE(message->type == event::kind::received);
    REQUIRE(message->data == std::vector<std::byte>(std::begin(data), std::end(data)));
    REQUIRE(message->channel == delivery::reliable);
    REQUIRE_FALSE(connection.poll());
    REQUIRE_THROWS_AS(connection.send(peer, delivery::reliable, std::vector<std::byte>(256 * 1024 + 1)), exception);
    EM_ASM({ JfcRtcFixture.peers[JfcRtcFixture.peers.length - 1].channels[0].bufferedAmount = 1024 * 1024; });
    REQUIRE_THROWS_AS(connection.send(peer, delivery::reliable, data), exception);
    REQUIRE_NOTHROW(connection.send(peer, delivery::unreliable, data));
    connection.disconnect(peer);
    REQUIRE(connection.status(peer)->failure == webrtc_failure::closed);
    REQUIRE(connection.poll()->type == event::kind::disconnected);
    REQUIRE_FALSE(connection.status(peer));
    REQUIRE_FALSE(connection.poll());
    REQUIRE_NOTHROW(connection.send(peer, delivery::reliable, data));
}

TEST_CASE("WebRTC negotiation deadlines use only caller time", "[net][webrtc]") {
    webrtc_host_settings settings;
    settings.timeout_ms = 10;
    settings.most_peers = 1;
    webrtc_host connection(settings);
    const auto peer = connection.offer();
    REQUIRE_THROWS_AS(connection.offer(), exception);
    connection.update(9);
    REQUIRE_FALSE(connection.poll());
    connection.update(1);
    REQUIRE(connection.status(peer)->failure == webrtc_failure::timeout);
    REQUIRE(connection.poll()->type == event::kind::disconnected);
    REQUIRE(connection.offer() == 2);
}

TEST_CASE("WebRTC ICE configuration is caller owned and copied", "[net][webrtc]") {
    webrtc_host_settings settings;
    settings.ice_servers.push_back({{"turn:example.invalid:3478", "turns:example.invalid:5349"}, "test-user", "test-secret"});
    settings.relay_only = true;
    webrtc_host connection(settings);
    settings.ice_servers.clear();
    const auto peer = connection.offer();
    REQUIRE(peer != 0);
    REQUIRE(EM_ASM_INT({
        const config = JfcRtcFixture.peers[JfcRtcFixture.peers.length - 1].config;
        return config.iceTransportPolicy === 'relay' && config.iceServers.length === 1 &&
            config.iceServers[0].urls.length === 2 && config.iceServers[0].username === 'test-user' &&
            config.iceServers[0].credential === 'test-secret';
    }));
}

TEST_CASE("WebRTC preserves unsigned peer IDs and elapsed times across the Wasm boundary", "[net][webrtc]") {
    webrtc_host_settings settings;
    settings.timeout_ms = 0xffffffffu;
    webrtc_host connection(settings);
    EM_ASM({
        const hosts = Array.from(JfcNetRtc.hosts.values());
        hosts[hosts.length - 1].nextPeer = 0x80000001;
    });
    const auto peer = connection.offer();
    REQUIRE(peer == 0x80000001u);
    REQUIRE(connection.status(peer));
    connection.update(0x80000000u);
    REQUIRE_FALSE(connection.poll());
    connection.update(0x7fffffffu);
    REQUIRE(connection.status(peer)->failure == webrtc_failure::timeout);
    REQUIRE(connection.poll()->peer == peer);
    REQUIRE_FALSE(connection.status(peer));
}
