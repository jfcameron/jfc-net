// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include "conformance.h"

#include <jfc/net/exception.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <utility>

using namespace jfc::net;
using jfc::net::conformance::implementation;

namespace {
    [[nodiscard]] std::vector<std::byte> bytes_of(const std::string &aText) {
        return {reinterpret_cast<const std::byte *>(aText.data()),
            reinterpret_cast<const std::byte *>(aText.data()) + aText.size()};
    }

    [[nodiscard]] std::string text_of(const std::span<const std::byte> aBytes) {
        return {reinterpret_cast<const char *>(aBytes.data()), aBytes.size()};
    }

    struct end final {
        std::shared_ptr<host> pHost;
        std::vector<event> heard;

        void listen() {
            if (pHost) while (auto e = pHost->poll()) heard.push_back(std::move(*e));
        }

        [[nodiscard]] std::size_t count(const event::kind aKind) const {
            return static_cast<std::size_t>(std::count_if(heard.begin(), heard.end(),
                [=](const event &each) { return each.type == aKind; }));
        }

        [[nodiscard]] std::vector<std::string> said() const {
            std::vector<std::string> out;

            for (const auto &each : heard) if (each.type == event::kind::received) out.push_back(text_of(each.data));

            return out;
        }

        [[nodiscard]] const event &first(const event::kind aKind) const {
            return *std::find_if(heard.begin(), heard.end(), [=](const event &each) { return each.type == aKind; });
        }

        //! who this end's first connection is to
        [[nodiscard]] peer_id peer() const { return first(event::kind::connected).peer; }
    };

    struct network final {
        end server;
        std::vector<end> clients;
        std::optional<std::size_t> most_message_bytes;

        explicit network(const std::size_t aClients = 1) {
            auto made = conformance::make_hosts(aClients);

            server.pHost = std::move(made.pServer);

            for (auto &pClient : made.clients) clients.push_back({std::move(pClient), {}});

            most_message_bytes = made.most_message_bytes;
        }

        [[nodiscard]] end &client() { return clients.front(); }

        void listen() {
            server.listen();

            for (auto &each : clients) each.listen();
        }

        bool until(const std::function<bool()> &aDone) {
            const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(10);

            for (;;) {
                listen();

                if (aDone()) return true;

                if (std::chrono::steady_clock::now() > giveUp) return false;

                conformance::wait();
            }
        }

        bool connected() {
            return until([this] {
                return server.count(event::kind::connected) == clients.size()
                    && std::all_of(clients.begin(), clients.end(),
                        [](const end &each) { return each.count(event::kind::connected) == 1; });
            });
        }

        //! long enough for what is on its way to have arrived
        void settle() {
            for (int i = 0; i < 50; ++i) {
                listen();

                conformance::wait();
            }

            listen();
        }
    };
}

TEST_CASE("each end of a connection is told of it", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    both.settle();

    REQUIRE(both.server.heard.size() == 1);
    REQUIRE(both.client().heard.size() == 1);
    REQUIRE(both.server.heard[0].type == event::kind::connected);
    REQUIRE(both.client().heard[0].type == event::kind::connected);
}

TEST_CASE("polling with nothing waiting finds nothing", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    both.settle();

    REQUIRE_FALSE(both.server.pHost->poll());
    REQUIRE_FALSE(both.client().pHost->poll());
}

TEST_CASE("reliable messages arrive whole, once and in order, whichever way they go", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    constexpr int MESSAGES = 64;

    std::vector<std::string> sent;

    for (int i = 0; i < MESSAGES; ++i) sent.push_back(std::to_string(i) + std::string(static_cast<std::size_t>(i * 16), 'x'));

    SECTION("to the server") {
        for (const auto &each : sent) both.client().pHost->send(both.client().peer(), delivery::reliable, bytes_of(each));

        REQUIRE(both.until([&] { return both.server.said().size() >= sent.size(); }));

        both.settle();

        REQUIRE(both.server.said() == sent);

        for (const auto &each : both.server.heard) {
            if (each.type != event::kind::received) continue;

            REQUIRE(each.peer == both.server.peer());
            REQUIRE(each.channel == delivery::reliable);
        }
    }

    SECTION("to a client") {
        for (const auto &each : sent) both.server.pHost->send(both.server.peer(), delivery::reliable, bytes_of(each));

        REQUIRE(both.until([&] { return both.client().said().size() >= sent.size(); }));

        both.settle();

        REQUIRE(both.client().said() == sent);
    }
}

TEST_CASE("an unreliable message says how it travelled", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    REQUIRE(both.until([&] {
        if (!both.server.said().empty()) return true;

        both.client().pHost->send(both.client().peer(), delivery::unreliable, bytes_of("again"));

        return false;
    }));

    const auto &message = both.server.first(event::kind::received);

    REQUIRE(message.channel == delivery::unreliable);
    REQUIRE(message.peer == both.server.peer());
    REQUIRE(text_of(message.data) == "again");
}

TEST_CASE("an unreliable message is never delivered after a newer one", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    for (int i = 0; i < 64; ++i) {
        both.client().pHost->send(both.client().peer(), delivery::unreliable, bytes_of(std::to_string(i)));

        if (i % 4 == 0) conformance::wait();
    }

    both.settle();

    std::vector<int> arrived;

    for (const auto &each : both.server.said()) arrived.push_back(std::stoi(each));

    REQUIRE(std::adjacent_find(arrived.begin(), arrived.end(), std::greater_equal<>{}) == arrived.end());
}

TEST_CASE("a message is the sender's own again once it is sent", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    auto message = bytes_of("first");

    both.client().pHost->send(both.client().peer(), delivery::reliable, message);

    message[0] = std::byte{'!'};

    REQUIRE(both.until([&] { return !both.server.said().empty(); }));

    REQUIRE(both.server.said() == std::vector<std::string>{"first"});
}

TEST_CASE("an empty message is a message", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    both.client().pHost->send(both.client().peer(), delivery::reliable, {});

    REQUIRE(both.until([&] { return !both.server.said().empty(); }));

    REQUIRE(both.server.first(event::kind::received).data.empty());
}

TEST_CASE("a message as long as a host carries arrives whole, and a longer one is refused", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    const auto longest = both.most_message_bytes.value_or(1024 * 1024);

    std::vector<std::byte> message(longest);

    for (std::size_t i = 0; i < message.size(); ++i) message[i] = static_cast<std::byte>(i * 31);

    both.client().pHost->send(both.client().peer(), delivery::reliable, message);

    REQUIRE(both.until([&] { return both.server.count(event::kind::received) == 1; }));

    REQUIRE(both.server.first(event::kind::received).data == message);

    if (!both.most_message_bytes) return;

    message.push_back(std::byte{0});

    REQUIRE_THROWS_AS(both.client().pHost->send(both.client().peer(), delivery::reliable, message), exception);
}

TEST_CASE("sending to a peer this host has not been told of does nothing", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    for (peer_id peer = 0; peer < 8; ++peer) both.client().pHost->send(peer, delivery::reliable, bytes_of("too soon"));

    REQUIRE(both.connected());

    both.client().pHost->send(both.client().peer() + 1000, delivery::reliable, bytes_of("nobody"));
    both.client().pHost->disconnect(both.client().peer() + 1000);

    both.client().pHost->send(both.client().peer(), delivery::reliable, bytes_of("in time"));

    REQUIRE(both.until([&] { return !both.server.said().empty(); }));

    both.settle();

    REQUIRE(both.server.said() == std::vector<std::string>{"in time"});
    REQUIRE(both.server.count(event::kind::disconnected) == 0);
    REQUIRE(both.client().count(event::kind::disconnected) == 0);
}

TEST_CASE("a connection ended by either end tells both, after what was sent", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    const auto ended_by = [&](end &aEnder, end &aOther) {
        for (const auto text : {"one", "two", "three"}) aEnder.pHost->send(aEnder.peer(), delivery::reliable, bytes_of(text));

        aEnder.pHost->disconnect(aEnder.peer());

        REQUIRE(both.until([&] {
            return aEnder.count(event::kind::disconnected) == 1 && aOther.count(event::kind::disconnected) == 1;
        }));

        REQUIRE(aOther.said() == std::vector<std::string>{"one", "two", "three"});
        REQUIRE(aOther.heard.back().type == event::kind::disconnected);
        REQUIRE(aOther.heard.back().peer == aOther.peer());
        REQUIRE(aEnder.heard.back().type == event::kind::disconnected);
        REQUIRE(aEnder.heard.back().peer == aEnder.peer());
    };

    SECTION("the server") { ended_by(both.server, both.client()); }

    SECTION("a client") { ended_by(both.client(), both.server); }
}

TEST_CASE("a peer gone is gone", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    const auto peer = both.server.peer();

    both.server.pHost->disconnect(peer);

    REQUIRE(both.until([&] {
        return both.server.count(event::kind::disconnected) == 1 && both.client().count(event::kind::disconnected) == 1;
    }));

    SECTION("sending to it does nothing") {
        both.server.pHost->send(peer, delivery::reliable, bytes_of("too late"));

        both.settle();

        REQUIRE(both.client().said().empty());
    }

    SECTION("ending it again tells nobody") {
        both.server.pHost->disconnect(peer);

        both.settle();

        REQUIRE(both.server.count(event::kind::disconnected) == 1);
        REQUIRE(both.client().count(event::kind::disconnected) == 1);
    }
}

TEST_CASE("a host that goes away is a peer gone", "[net][conformance]") {
    CAPTURE(implementation());

    network both;

    REQUIRE(both.connected());

    SECTION("a client") {
        both.client().pHost.reset();

        REQUIRE(both.until([&] { return both.server.count(event::kind::disconnected) == 1; }));
    }

    SECTION("the server") {
        both.server.pHost.reset();

        REQUIRE(both.until([&] { return both.client().count(event::kind::disconnected) == 1; }));
    }
}

TEST_CASE("a host with many peers tells them apart", "[net][conformance]") {
    CAPTURE(implementation());

    network all(3);

    REQUIRE(all.connected());

    std::map<std::string, peer_id> peers;

    for (std::size_t i = 0; i < all.clients.size(); ++i)
        all.clients[i].pHost->send(all.clients[i].peer(), delivery::reliable, bytes_of(std::to_string(i)));

    REQUIRE(all.until([&] { return all.server.said().size() == all.clients.size(); }));

    for (const auto &each : all.server.heard)
        if (each.type == event::kind::received) peers[text_of(each.data)] = each.peer;

    REQUIRE(peers.size() == 3);
    REQUIRE(peers["0"] != peers["1"]);
    REQUIRE(peers["1"] != peers["2"]);
    REQUIRE(peers["0"] != peers["2"]);

    SECTION("sent to one, only that one hears") {
        all.server.pHost->send(peers["1"], delivery::reliable, bytes_of("yours"));

        REQUIRE(all.until([&] { return !all.clients[1].said().empty(); }));

        all.settle();

        REQUIRE(all.clients[1].said() == std::vector<std::string>{"yours"});
        REQUIRE(all.clients[0].said().empty());
        REQUIRE(all.clients[2].said().empty());
    }

    SECTION("one going leaves the rest connected") {
        all.server.pHost->disconnect(peers["1"]);

        REQUIRE(all.until([&] { return all.clients[1].count(event::kind::disconnected) == 1; }));

        all.server.pHost->send(peers["2"], delivery::reliable, bytes_of("still here"));

        REQUIRE(all.until([&] { return !all.clients[2].said().empty(); }));

        REQUIRE(all.clients[0].count(event::kind::disconnected) == 0);
        REQUIRE(all.clients[2].count(event::kind::disconnected) == 0);
    }
}
