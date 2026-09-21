// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/net/enet_host.h>

#include <chrono>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace jfc::net;

namespace {
    [[nodiscard]] std::vector<std::byte> bytes_of(const std::string &aText) {
        return {reinterpret_cast<const std::byte *>(aText.data()), reinterpret_cast<const std::byte *>(aText.data()) + aText.size()};
    }

    [[nodiscard]] std::string text_of(const std::vector<std::byte> &aBytes) {
        return {reinterpret_cast<const char *>(aBytes.data()), aBytes.size()};
    }

    struct pair final {
        std::unique_ptr<enet_host> pServer = enet_host::listen(0);
        std::unique_ptr<enet_host> pClient = enet_host::connect("127.0.0.1", pServer->port());

        std::vector<event> serverHeard, clientHeard;

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

        [[nodiscard]] static std::size_t count(const std::vector<event> &aHeard, const event::kind aKind) {
            std::size_t out = 0;

            for (const auto &each : aHeard) out += each.type == aKind;

            return out;
        }

        [[nodiscard]] static std::vector<std::string> said(const std::vector<event> &aHeard) {
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
}

TEST_CASE("**a connection that cannot be made is not left waiting for ever**", "[net][enet][.slow]") {
    auto pClient = enet_host::connect("127.0.0.1", 9);

    std::vector<event> heard;

    const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(40);

    while (heard.empty() && std::chrono::steady_clock::now() < giveUp) {
        while (auto e = pClient->poll()) heard.push_back(std::move(*e));

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    REQUIRE(heard.size() == 1);
    REQUIRE(heard.front().type == event::kind::disconnected);
    REQUIRE(heard.front().peer == pClient->server_peer());
}

TEST_CASE("**an address that is nowhere is refused, saying which**", "[net][enet]") {
    REQUIRE_THROWS_WITH(enet_host::connect("no.such.host.invalid", 7777), Catch::Matchers::Contains("no.such.host.invalid"));
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
