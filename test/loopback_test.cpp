// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/net/loopback.h>

#include <cstddef>
#include <initializer_list>
#include <memory>
#include <vector>

using namespace jfc::net;

namespace {
    [[nodiscard]] std::vector<std::byte> bytes_of(const std::initializer_list<int> aValues) {
        std::vector<std::byte> out;
        for (const auto value : aValues) out.push_back(static_cast<std::byte>(value));
        return out;
    }

    [[nodiscard]] event next(host &aHost) {
        auto polled = aHost.poll();
        REQUIRE(polled.has_value());
        return std::move(*polled);
    }

    void require_connected(host &aHost, const peer_id aPeer) {
        const auto e = next(aHost);
        REQUIRE(e.type == event::kind::connected);
        REQUIRE(e.peer == aPeer);
    }

    void require_disconnected(host &aHost, const peer_id aPeer) {
        const auto e = next(aHost);
        REQUIRE(e.type == event::kind::disconnected);
        REQUIRE(e.peer == aPeer);
    }

    void require_received(host &aHost, const peer_id aPeer, const delivery aDelivery,
        const std::vector<std::byte> &aData) {
        const auto e = next(aHost);
        REQUIRE(e.type == event::kind::received);
        REQUIRE(e.peer == aPeer);
        REQUIRE(e.channel == aDelivery);
        REQUIRE(e.data == aData);
    }

    constexpr auto SERVER = loopback_server::SERVER_PEER;
}

TEST_CASE("a loopback connection", "[net][loopback]") {
    loopback_server server;

    REQUIRE_FALSE(server.poll());

    const auto pClient = server.connect();

    SECTION("both sides are told") {
        require_connected(server, 1);
        require_connected(*pClient, SERVER);
        REQUIRE_FALSE(server.poll());
        REQUIRE_FALSE(pClient->poll());
    }

    SECTION("messages go both ways, with how they travelled") {
        require_connected(server, 1);
        require_connected(*pClient, SERVER);

        pClient->send(SERVER, delivery::unreliable, bytes_of({1, 2}));
        server.send(1, delivery::reliable, bytes_of({3}));

        require_received(server, 1, delivery::unreliable, bytes_of({1, 2}));
        require_received(*pClient, SERVER, delivery::reliable, bytes_of({3}));
    }

    SECTION("messages arrive in the order they were sent") {
        for (int i = 0; i < 10; ++i) {
            pClient->send(SERVER, i % 2 ? delivery::reliable : delivery::unreliable, bytes_of({i}));
        }

        require_connected(server, 1);
        for (int i = 0; i < 10; ++i) {
            require_received(server, 1, i % 2 ? delivery::reliable : delivery::unreliable, bytes_of({i}));
        }
    }

    SECTION("a message is copied on send, not borrowed") {
        auto data = bytes_of({5});
        server.send(1, delivery::reliable, data);
        data[0] = std::byte{6};

        require_connected(*pClient, SERVER);
        require_received(*pClient, SERVER, delivery::reliable, bytes_of({5}));
    }

    SECTION("an empty message is a message") {
        pClient->send(SERVER, delivery::reliable, {});

        require_connected(server, 1);
        require_received(server, 1, delivery::reliable, {});
    }
}

TEST_CASE("a loopback server with many clients", "[net][loopback]") {
    loopback_server server;

    const auto pFirst = server.connect();
    const auto pSecond = server.connect();

    require_connected(server, 1);
    require_connected(server, 2);

    SECTION("each is told apart, and sent to alone") {
        pSecond->send(SERVER, delivery::reliable, bytes_of({2}));
        pFirst->send(SERVER, delivery::reliable, bytes_of({1}));
        server.send(2, delivery::reliable, bytes_of({20}));

        require_received(server, 2, delivery::reliable, bytes_of({2}));
        require_received(server, 1, delivery::reliable, bytes_of({1}));

        require_connected(*pFirst, SERVER);
        REQUIRE_FALSE(pFirst->poll());
        require_connected(*pSecond, SERVER);
        require_received(*pSecond, SERVER, delivery::reliable, bytes_of({20}));
    }

    SECTION("an id is never handed out twice") {
        server.disconnect(1);
        require_disconnected(server, 1);

        const auto pThird = server.connect();

        require_connected(server, 3);
    }
}

TEST_CASE("a loopback connection ends", "[net][loopback]") {
    loopback_server server;
    auto pClient = server.connect();

    require_connected(server, 1);
    require_connected(*pClient, SERVER);

    SECTION("the server disconnects, what was sent first still arrives, then both are told") {
        server.send(1, delivery::reliable, bytes_of({9}));
        server.disconnect(1);

        require_disconnected(server, 1);
        require_received(*pClient, SERVER, delivery::reliable, bytes_of({9}));
        require_disconnected(*pClient, SERVER);

        server.send(1, delivery::reliable, bytes_of({10}));
        pClient->send(SERVER, delivery::reliable, bytes_of({11}));
        server.disconnect(1);
        pClient->disconnect(SERVER);

        REQUIRE_FALSE(server.poll());
        REQUIRE_FALSE(pClient->poll());
    }

    SECTION("the client disconnects, what it sent first still arrives, then both are told") {
        pClient->send(SERVER, delivery::reliable, bytes_of({9}));
        pClient->disconnect(SERVER);

        require_disconnected(*pClient, SERVER);
        require_received(server, 1, delivery::reliable, bytes_of({9}));
        require_disconnected(server, 1);
        REQUIRE_FALSE(server.poll());
    }

    SECTION("the client is destroyed, the server is told") {
        pClient->send(SERVER, delivery::reliable, bytes_of({9}));
        pClient.reset();

        require_received(server, 1, delivery::reliable, bytes_of({9}));
        require_disconnected(server, 1);
        REQUIRE_FALSE(server.poll());
    }

    SECTION("the server is destroyed, the client is told, after what it was sent") {
        auto pServer = std::make_unique<loopback_server>();
        auto pOther = pServer->connect();
        require_connected(*pOther, SERVER);

        pServer->send(1, delivery::reliable, bytes_of({9}));
        pServer.reset();

        require_received(*pOther, SERVER, delivery::reliable, bytes_of({9}));
        require_disconnected(*pOther, SERVER);

        pOther->send(SERVER, delivery::reliable, bytes_of({1}));
        pOther->disconnect(SERVER);
        REQUIRE_FALSE(pOther->poll());
    }

    SECTION("sending to a peer that never was does nothing") {
        server.send(99, delivery::reliable, bytes_of({1}));
        server.disconnect(99);
        pClient->send(5, delivery::reliable, bytes_of({1}));

        REQUIRE_FALSE(server.poll());
        REQUIRE_FALSE(pClient->poll());
    }
}
