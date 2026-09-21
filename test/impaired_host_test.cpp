// © Joseph Cameron - All Rights Reserved

#include <jfc/catch.hpp>

#include <jfc/net/impaired_host.h>
#include <jfc/net/loopback.h>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

using namespace jfc::net;

namespace {
    [[nodiscard]] std::vector<std::byte> bytes_of(const int aValue) {
        return {static_cast<std::byte>(aValue & 0xFF), static_cast<std::byte>(aValue >> 8)};
    }

    [[nodiscard]] int value_of(const std::vector<std::byte> &aBytes) {
        return static_cast<int>(aBytes[0]) | (static_cast<int>(aBytes[1]) << 8);
    }

    struct fixture final {
        double now = 0;

        std::shared_ptr<loopback_server> pServer = std::make_shared<loopback_server>();

        impaired_host client;

        explicit fixture(const impairments &aImpairments)
        : client(std::shared_ptr<host>(pServer->connect()), aImpairments, [this] { return now; }) {
            while (client.poll()) {}
            while (pServer->poll()) {}
        }

        void send(const int aValue, const delivery aDelivery = delivery::reliable) {
            client.send(loopback_server::SERVER_PEER, aDelivery, bytes_of(aValue));
        }

        [[nodiscard]] std::vector<event> at(const double aSeconds) {
            now = aSeconds;

            while (client.poll()) {}

            std::vector<event> out;

            while (auto e = pServer->poll()) out.push_back(std::move(*e));

            return out;
        }

        [[nodiscard]] static std::vector<int> values_of(const std::vector<event> &aEvents) {
            std::vector<int> out;

            for (const auto &each : aEvents) if (each.type == event::kind::received) out.push_back(value_of(each.data));

            return out;
        }
    };
}

TEST_CASE("**the plain impairments impair nothing**", "[net][impaired]") {
    fixture f({});

    f.send(1);
    f.send(2, delivery::unreliable);

    REQUIRE(fixture::values_of(f.at(0)) == std::vector<int>{1, 2});
}

TEST_CASE("**a message is held for its latency, and not a moment less**", "[net][impaired]") {
    fixture f({0.1});

    f.send(7);

    REQUIRE(f.at(0.099).empty());
    REQUIRE(fixture::values_of(f.at(0.1)) == std::vector<int>{7});

    SECTION("**nothing is sent twice**") {
        REQUIRE(f.at(1).empty());
    }
}

TEST_CASE("**reliable messages are never lost, and never reordered, whatever the jitter**", "[net][impaired]") {
    fixture f({0.05, 0.2, 1.0});

    for (int each = 0; each < 200; ++each) {
        f.now = each * 0.001;
        f.send(each);
    }

    std::vector<int> expected;

    for (int each = 0; each < 200; ++each) expected.push_back(each);

    REQUIRE(fixture::values_of(f.at(10)) == expected);
}

TEST_CASE("**unreliable messages are lost at the chance given**", "[net][impaired]") {
    const auto arrived = [](const double aLoss) {
        fixture f({0, 0, aLoss});

        for (int each = 0; each < 1000; ++each) f.send(each, delivery::unreliable);

        return fixture::values_of(f.at(1)).size();
    };

    REQUIRE(arrived(0) == 1000);
    REQUIRE(arrived(1) == 0);

    const auto half = arrived(0.5);

    REQUIRE(half > 400);
    REQUIRE(half < 600);
}

TEST_CASE("**in order by default, however the jitter falls: late, and never lost to being overtaken**", "[net][impaired]") {
    fixture f({0.01, 0.2, 0});

    for (int each = 0; each < 200; ++each) {
        f.now = each * 0.05;
        f.send(each, delivery::unreliable);
    }

    std::vector<int> expected;

    for (int each = 0; each < 200; ++each) expected.push_back(each);

    REQUIRE(fixture::values_of(f.at(100)) == expected);
}

TEST_CASE("**reordering, an unreliable message held past a newer one is dropped, never delivered late**", "[net][impaired]") {
    fixture f({0.01, 0.2, 0, 1, true});

    for (int each = 0; each < 200; ++each) {
        f.now = each * 0.05;
        f.send(each, delivery::unreliable);
    }

    const auto values = fixture::values_of(f.at(100));

    REQUIRE_FALSE(values.empty());
    REQUIRE(values.size() < 200);

    for (std::size_t each = 1; each < values.size(); ++each) REQUIRE(values[each] > values[each - 1]);
}

TEST_CASE("**a disconnection waits behind what was sent to the peer**", "[net][impaired]") {
    fixture f({0.1, 0.3});

    for (int each = 0; each < 10; ++each) f.send(each);

    f.client.disconnect(loopback_server::SERVER_PEER);

    const auto heard = f.at(10);

    REQUIRE(fixture::values_of(heard).size() == 10);
    REQUIRE(heard.back().type == event::kind::disconnected);
}

TEST_CASE("**the same seed is the same run**", "[net][impaired]") {
    const auto run = [](const std::uint32_t aSeed) {
        fixture f({0.01, 0.05, 0.3, aSeed});

        std::vector<std::pair<double, int>> out;

        for (int each = 0; each < 300; ++each) {
            f.send(each, delivery::unreliable);

            for (const auto value : fixture::values_of(f.at((each + 1) * 0.02))) out.emplace_back(f.now, value);
        }

        return out;
    };

    REQUIRE(run(3) == run(3));
    REQUIRE(run(3) != run(4));
}

TEST_CASE("**changed, only what is sent after is impaired the new way**", "[net][impaired]") {
    fixture f({1.0});

    f.send(1);

    f.client.set({0});

    f.send(2);

    REQUIRE(f.at(0.5).empty());
    REQUIRE(fixture::values_of(f.at(1)) == std::vector<int>{1, 2});

    f.send(3);

    REQUIRE(fixture::values_of(f.at(1)) == std::vector<int>{3});
}

TEST_CASE("**impairments that are no impairments are refused**", "[net][impaired]") {
    const auto pServer = std::make_shared<loopback_server>();
    const auto pClient = std::shared_ptr<host>(pServer->connect());

    REQUIRE_THROWS_AS(impaired_host(pClient, {-1}), std::invalid_argument);
    REQUIRE_THROWS_AS(impaired_host(pClient, {0, -1}), std::invalid_argument);
    REQUIRE_THROWS_AS(impaired_host(pClient, {0, 0, 1.5}), std::invalid_argument);
    REQUIRE_THROWS_AS(impaired_host(nullptr), std::invalid_argument);
}
