// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/net/enet_host.h>
#include <jfc/net/exception.h>

#include <enet/enet.h>

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>
#include <span>

using namespace jfc::net;

namespace {
    [[nodiscard]] std::vector<std::byte> bytes_of(const std::string &aText) {
        return {reinterpret_cast<const std::byte *>(aText.data()), reinterpret_cast<const std::byte *>(aText.data()) + aText.size()};
    }

    [[nodiscard]] std::string text_of(std::span<const std::byte> aBytes) {
        return {reinterpret_cast<const char *>(aBytes.data()), aBytes.size()};
    }

    struct pair final {
        std::unique_ptr<enet_host> pServer;
        std::unique_ptr<enet_host> pClient;

        std::vector<event> serverHeard, clientHeard;

        explicit pair(const enet_host_settings &aSettings = {})
        : pServer(enet_host::listen(0, aSettings))
        , pClient(enet_host::connect("127.0.0.1", pServer->port(), aSettings)) {}

        bool until(const std::function<bool()> &aDone) {
            const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(2);

            while (!aDone()) {
                if (std::chrono::steady_clock::now() > giveUp) return false;

                if (pServer) while (auto e = pServer->poll()) serverHeard.push_back(std::move(*e));
                if (pClient) while (auto e = pClient->poll()) clientHeard.push_back(std::move(*e));

                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            return true;
        }

        [[nodiscard]] static std::size_t count(std::span<const event> aHeard, const event::kind aKind) {
            std::size_t out = 0;

            for (const auto &each : aHeard) out += each.type == aKind;

            return out;
        }

        [[nodiscard]] static std::vector<std::string> said(std::span<const event> aHeard) {
            std::vector<std::string> out;

            for (const auto &each : aHeard) if (each.type == event::kind::received) out.push_back(text_of(each.data));

            return out;
        }

        bool connected() {
            return until([this] {
                return count(serverHeard, event::kind::connected) == 1 && count(clientHeard, event::kind::connected) == 1;
            });
        }
    };
}

TEST_CASE("**a client connects to a server over UDP, and both are told**", "[net][enet]") {
    pair both;

    REQUIRE(both.pServer->port() != 0);

    REQUIRE(both.connected());

    REQUIRE(both.clientHeard.front().peer == both.pClient->server_peer());

    const auto client = both.serverHeard.front().peer;
    const auto server = both.pClient->server_peer();

    SECTION("**reliable messages go both ways, whole and in order**") {
        for (int i = 0; i < 20; ++i) both.pClient->send(server, delivery::reliable, bytes_of("up " + std::to_string(i)));

        both.pServer->send(client, delivery::reliable, bytes_of("down"));

        REQUIRE(both.until([&] { return pair::said(both.serverHeard).size() == 20 && pair::said(both.clientHeard).size() == 1; }));

        for (int i = 0; i < 20; ++i) REQUIRE(pair::said(both.serverHeard)[static_cast<std::size_t>(i)] == "up " + std::to_string(i));

        REQUIRE(pair::said(both.clientHeard).front() == "down");

        for (const auto &each : both.serverHeard)
            if (each.type == event::kind::received) REQUIRE(each.channel == delivery::reliable);
    }

    SECTION("**an unreliable message comes, over this machine's own address, and says how it came**") {
        both.pServer->send(client, delivery::unreliable, bytes_of("where everyone is"));

        REQUIRE(both.until([&] { return !pair::said(both.clientHeard).empty(); }));

        REQUIRE(both.clientHeard.back().channel == delivery::unreliable);
    }

    SECTION("**an empty message is a message**") {
        both.pClient->send(server, delivery::reliable, {});

        REQUIRE(both.until([&] { return pair::count(both.serverHeard, event::kind::received) == 1; }));
    }

    SECTION("**the server ends it: both told**") {
        both.pServer->disconnect(client);

        REQUIRE(both.until([&] {
            return pair::count(both.serverHeard, event::kind::disconnected) == 1
                && pair::count(both.clientHeard, event::kind::disconnected) == 1;
        }));

        SECTION("**and a message to a peer gone is dropped, not an error**") {
            both.pServer->send(client, delivery::reliable, bytes_of("anyone?"));
        }
    }

    SECTION("**the client ends it: both told**") {
        both.pClient->disconnect(server);

        REQUIRE(both.until([&] {
            return pair::count(both.serverHeard, event::kind::disconnected) == 1
                && pair::count(both.clientHeard, event::kind::disconnected) == 1;
        }));
    }

    SECTION("**the server goes: the client is told**") {
        both.pServer.reset();

        REQUIRE(both.until([&] { return pair::count(both.clientHeard, event::kind::disconnected) == 1; }));
    }

    SECTION("**the client goes: the server is told**") {
        both.pClient.reset();

        REQUIRE(both.until([&] { return pair::count(both.serverHeard, event::kind::disconnected) == 1; }));

        REQUIRE(both.serverHeard.back().peer == client);
    }

    SECTION("**the server ends it after sending, and what it sent comes first, whole and in order**") {
        const auto message = [](const int aIndex) { return std::to_string(aIndex) + std::string(64 * 1024, 'x'); };

        for (int i = 0; i < 20; ++i) both.pServer->send(client, delivery::reliable, bytes_of(message(i)));

        both.pServer->disconnect(client);

        REQUIRE(both.until([&] { return pair::count(both.clientHeard, event::kind::disconnected) == 1; }));

        REQUIRE(both.clientHeard.back().type == event::kind::disconnected);

        const auto said = pair::said(both.clientHeard);

        REQUIRE(said.size() == 20);

        for (int i = 0; i < 20; ++i) REQUIRE(said[static_cast<std::size_t>(i)] == message(i));
    }

    SECTION("**ended twice, it is told once**") {
        both.pServer->disconnect(client);
        both.pServer->disconnect(client);

        REQUIRE(both.until([&] {
            return pair::count(both.serverHeard, event::kind::disconnected) == 1
                && pair::count(both.clientHeard, event::kind::disconnected) == 1;
        }));

        both.pServer->disconnect(client);

        const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);

        both.until([&] { return std::chrono::steady_clock::now() > settle; });

        REQUIRE(pair::count(both.serverHeard, event::kind::disconnected) == 1);
        REQUIRE(pair::count(both.clientHeard, event::kind::disconnected) == 1);
    }
}

TEST_CASE("**a server with many clients tells them apart, and sends to each alone**", "[net][enet]") {
    const auto pServer = enet_host::listen(0);

    std::vector<std::unique_ptr<enet_host>> clients;
    std::vector<std::vector<event>> clientHeard;
    std::vector<event> serverHeard;

    const auto join = [&] {
        clients.push_back(enet_host::connect("127.0.0.1", pServer->port()));
        clientHeard.emplace_back();
    };

    const auto until = [&](const std::function<bool()> &aDone) {
        const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        while (!aDone()) {
            if (std::chrono::steady_clock::now() > giveUp) return false;

            while (auto e = pServer->poll()) serverHeard.push_back(std::move(*e));

            for (std::size_t i = 0; i < clients.size(); ++i)
                if (clients[i]) while (auto e = clients[i]->poll()) clientHeard[i].push_back(std::move(*e));

            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        return true;
    };

    join();
    join();

    REQUIRE(until([&] {
        return pair::count(serverHeard, event::kind::connected) == 2
            && pair::count(clientHeard[0], event::kind::connected) == 1
            && pair::count(clientHeard[1], event::kind::connected) == 1;
    }));

    clients[0]->send(clients[0]->server_peer(), delivery::reliable, bytes_of("zero"));
    clients[1]->send(clients[1]->server_peer(), delivery::reliable, bytes_of("one"));

    REQUIRE(until([&] { return pair::said(serverHeard).size() == 2; }));

    std::map<std::string, peer_id> peerOf;

    for (const auto &each : serverHeard) if (each.type == event::kind::received) peerOf[text_of(each.data)] = each.peer;

    REQUIRE(peerOf.size() == 2);
    REQUIRE(peerOf["zero"] != peerOf["one"]);

    SECTION("**sent to one, only that one hears it**") {
        pServer->send(peerOf["one"], delivery::reliable, bytes_of("for one"));

        REQUIRE(until([&] { return !pair::said(clientHeard[1]).empty(); }));

        const auto settle = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);

        until([&] { return std::chrono::steady_clock::now() > settle; });

        REQUIRE(pair::said(clientHeard[1]) == std::vector<std::string>{"for one"});
        REQUIRE(pair::said(clientHeard[0]).empty());
    }

    SECTION("**an id is never handed out twice**") {
        clients[0]->disconnect(clients[0]->server_peer());

        REQUIRE(until([&] { return pair::count(serverHeard, event::kind::disconnected) == 1; }));
        REQUIRE(serverHeard.back().peer == peerOf["zero"]);

        join();

        REQUIRE(until([&] { return pair::count(serverHeard, event::kind::connected) == 3; }));

        const auto third = serverHeard.back().peer;

        REQUIRE(third != peerOf["zero"]);
        REQUIRE(third != peerOf["one"]);
    }
}

TEST_CASE("**a server that is full lets nobody more in**", "[net][enet]") {
    enet_host_settings settings;

    settings.most_peers = 1;
    settings.timeout_ms = 1000;

    pair both(settings);

    REQUIRE(both.connected());

    const auto pLate = enet_host::connect("127.0.0.1", both.pServer->port(), settings);

    std::vector<event> lateHeard;

    REQUIRE(both.until([&] {
        while (auto e = pLate->poll()) lateHeard.push_back(std::move(*e));

        return !lateHeard.empty();
    }));

    REQUIRE(lateHeard.size() == 1);
    REQUIRE(lateHeard.front().type == event::kind::disconnected);
    REQUIRE(pair::count(both.serverHeard, event::kind::connected) == 1);
}

TEST_CASE("**a connection ended before it is made is told, once**", "[net][enet]") {
    pair both;

    const auto server = both.pClient->server_peer();

    const auto settle = [&] {
        const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(300);

        both.until([&] { return std::chrono::steady_clock::now() > until; });
    };

    both.pClient->disconnect(server);

    REQUIRE(both.until([&] { return pair::count(both.clientHeard, event::kind::disconnected) == 1; }));

    settle();

    REQUIRE(both.clientHeard.size() == 1);
    REQUIRE(both.clientHeard.front().peer == server);
    REQUIRE(both.serverHeard.empty());

    SECTION("**and sending to it or ending it again does nothing**") {
        both.pClient->send(server, delivery::reliable, bytes_of("anyone?"));
        both.pClient->disconnect(server);

        settle();

        REQUIRE(both.clientHeard.size() == 1);
        REQUIRE(both.serverHeard.empty());
    }
}

TEST_CASE("**a message sent before the connected event is polled is dropped**", "[net][enet]") {
    pair both;

    both.pClient->send(both.pClient->server_peer(), delivery::reliable, bytes_of("too soon"));

    REQUIRE(both.connected());

    both.pClient->send(both.pClient->server_peer(), delivery::reliable, bytes_of("in time"));

    REQUIRE(both.until([&] { return !pair::said(both.serverHeard).empty(); }));

    REQUIRE(pair::said(both.serverHeard) == std::vector<std::string>{"in time"});
}

TEST_CASE("**a message longer than the most allowed is refused, saying how long**", "[net][enet]") {
    enet_host_settings settings;

    settings.most_message_bytes = 2000;

    pair both(settings);

    REQUIRE(both.connected());

    const auto server = both.pClient->server_peer();

    for (const auto how : {delivery::reliable, delivery::unreliable}) {
        REQUIRE_THROWS_AS(both.pClient->send(server, how, bytes_of(std::string(2001, 'x'))), exception);
        REQUIRE_THROWS_WITH(both.pClient->send(server, how, bytes_of(std::string(2001, 'x'))),
            Catch::Matchers::Contains("2001 bytes") && Catch::Matchers::Contains("2000 allowed"));
    }

    SECTION("**and one at the most arrives whole**") {
        both.pClient->send(server, delivery::reliable, bytes_of(std::string(2000, 'x')));

        REQUIRE(both.until([&] { return !pair::said(both.serverHeard).empty(); }));

        REQUIRE(pair::said(both.serverHeard).front() == std::string(2000, 'x'));
    }
}

TEST_CASE("**a message longer than the receiver takes is dropped, and the connection kept**", "[net][enet]") {
    enet_host_settings small;

    small.most_message_bytes = 2000;

    auto pServer = enet_host::listen(0, small);

    pair both;

    both.pServer = std::move(pServer);
    both.pClient = enet_host::connect("127.0.0.1", both.pServer->port());

    REQUIRE(both.connected());

    const auto server = both.pClient->server_peer();

    both.pClient->send(server, delivery::unreliable, bytes_of(std::string(2001, 'x')));
    both.pClient->send(server, delivery::reliable, bytes_of("after"));

    REQUIRE(both.until([&] { return !pair::said(both.serverHeard).empty(); }));

    REQUIRE(pair::said(both.serverHeard) == std::vector<std::string>{"after"});
    REQUIRE(pair::count(both.serverHeard, event::kind::disconnected) == 0);
}

TEST_CASE("**an unreliable message too big for one datagram still travels unreliably**", "[net][enet]") {
    REQUIRE(enet_initialize() == 0);

    struct raw_host final {
        ENetHost *pHost;

        ~raw_host() {
            enet_host_destroy(pHost);
            enet_deinitialize();
        }
    };

    ENetAddress any{};

    any.host = ENET_HOST_ANY;

    const raw_host raw{enet_host_create(&any, 1, 2, 0, 0)};

    REQUIRE(raw.pHost);

    ENetAddress bound{};

    REQUIRE(enet_socket_get_address(raw.pHost->socket, &bound) == 0);

    auto pClient = enet_host::connect("127.0.0.1", bound.port);

    const auto heard = [&](const ENetEventType aType) {
        const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(2);

        ENetEvent happened{};

        while (std::chrono::steady_clock::now() < giveUp) {
            while (pClient->poll()) {}

            if (enet_host_service(raw.pHost, &happened, 1) > 0 && happened.type == aType) return happened;
        }

        return ENetEvent{};
    };

    REQUIRE(heard(ENET_EVENT_TYPE_CONNECT).type == ENET_EVENT_TYPE_CONNECT);

    const std::string big(4 * ENET_HOST_DEFAULT_MTU, 'x');

    pClient->send(pClient->server_peer(), delivery::unreliable, bytes_of(big));

    const auto received = heard(ENET_EVENT_TYPE_RECEIVE);

    REQUIRE(received.type == ENET_EVENT_TYPE_RECEIVE);

    const auto flags = received.packet->flags;
    const auto length = received.packet->dataLength;

    enet_packet_destroy(received.packet);

    REQUIRE(length == big.size());
    REQUIRE((flags & ENET_PACKET_FLAG_UNRELIABLE_FRAGMENT) != 0);
    REQUIRE((flags & ENET_PACKET_FLAG_RELIABLE) == 0);
}

TEST_CASE("**a connection that cannot be made is given up within its timeout**", "[net][enet]") {
    enet_host_settings settings;

    settings.timeout_ms = 1000;

    const auto begun = std::chrono::steady_clock::now();

    auto pClient = enet_host::connect("127.0.0.1", 9, settings);

    std::vector<event> heard;

    const auto giveUp = begun + std::chrono::seconds(4);

    while (heard.empty() && std::chrono::steady_clock::now() < giveUp) {
        while (auto e = pClient->poll()) heard.push_back(std::move(*e));

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const auto took = std::chrono::steady_clock::now() - begun;

    REQUIRE(heard.size() == 1);
    REQUIRE(heard.front().type == event::kind::disconnected);
    REQUIRE(heard.front().peer == pClient->server_peer());

    REQUIRE(took >= std::chrono::milliseconds(900));
    REQUIRE(took <= std::chrono::milliseconds(2500));
}

TEST_CASE("**an address that is nowhere is refused, saying which**", "[net][enet]") {
    REQUIRE_THROWS_AS(enet_host::connect("no.such.host.invalid", 7777), exception);
    REQUIRE_THROWS_WITH(enet_host::connect("no.such.host.invalid", 7777), Catch::Matchers::Contains("no.such.host.invalid"));
}

TEST_CASE("**a port already listened on is refused, saying which**", "[net][enet]") {
    const auto pFirst = enet_host::listen(0);
    const auto port = pFirst->port();

    REQUIRE_THROWS_AS(enet_host::listen(port), exception);
    REQUIRE_THROWS_WITH(enet_host::listen(port), Catch::Matchers::Contains(std::to_string(port)));
}

TEST_CASE("**a peer that goes silent -- crashed, its connection lost -- is let go within its timeout**", "[net][enet]") {
    enet_host_settings settings;

    settings.timeout_ms = 1000;

    auto pServer = enet_host::listen(0, settings);
    auto pClient = enet_host::connect("127.0.0.1", pServer->port(), settings);

    std::vector<event> heard;

    const auto until = [&](const std::chrono::milliseconds aFor, const bool aClientToo, const event::kind aKind) {
        const auto giveUp = std::chrono::steady_clock::now() + aFor;

        while (std::chrono::steady_clock::now() < giveUp) {
            while (auto e = pServer->poll()) heard.push_back(std::move(*e));

            if (aClientToo) while (pClient->poll()) {}

            for (const auto &each : heard) if (each.type == aKind) return true;

            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        return false;
    };

    REQUIRE(until(std::chrono::seconds(2), true, event::kind::connected));

    const auto silent = std::chrono::steady_clock::now();

    REQUIRE(until(std::chrono::seconds(4), false, event::kind::disconnected));

    const auto took = std::chrono::steady_clock::now() - silent;

    REQUIRE(took >= std::chrono::milliseconds(900));
    REQUIRE(took <= std::chrono::milliseconds(2500));
}

TEST_CASE("**the address this machine sends from toward itself is the loopback**", "[net][enet]") {
    const auto toward = local_address_toward("127.0.0.1");

    REQUIRE(toward);
    REQUIRE(*toward == "127.0.0.1");

    REQUIRE_FALSE(local_address_toward("not an address"));
}

TEST_CASE("**a LAN address is one another machine could use, or none**", "[net][enet]") {
    const auto lan = lan_address();

    if (!lan) return;

    REQUIRE(lan->rfind("127.", 0) != 0);
    REQUIRE(*lan != "0.0.0.0");
    REQUIRE(std::count(lan->begin(), lan->end(), '.') == 3);
}
