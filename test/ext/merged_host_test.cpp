// © Joseph Cameron - MIT License

#include <jfc/catch.hpp>

#include <jfc/net/exception.h>
#include <jfc/net/ext/merged_host.h>
#include <jfc/net/loopback.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

using namespace jfc::net;
using namespace jfc::net::ext;

namespace {
    class scripted final : public host {
    public:
        struct sent_message final {
            peer_id peer = 0;
            delivery channel = delivery::reliable;
            std::vector<std::byte> data;
        };

        std::deque<event> events;
        std::vector<sent_message> sent;
        std::vector<peer_id> dropped;
        bool refuse = false;

        void send(const peer_id aPeer, const delivery aDelivery, const std::span<const std::byte> aMessage) override {
            if (refuse)
                throw exception("scripted: refused a send");
            sent.push_back({aPeer, aDelivery, {aMessage.begin(), aMessage.end()}});
        }

        void disconnect(const peer_id aPeer) override { dropped.push_back(aPeer); }

        [[nodiscard]] std::optional<event> poll() override {
            if (events.empty())
                return std::nullopt;
            auto out = std::move(events.front());
            events.pop_front();
            return out;
        }
    };

    event of(const event::kind aType, const peer_id aPeer, const std::byte aValue = std::byte{0}) {
        event out{aType, aPeer, delivery::reliable, {}};
        if (aType == event::kind::received)
            out.data = {aValue};
        return out;
    }

    std::vector<std::byte> one(const std::byte aValue) { return {aValue}; }
}

TEST_CASE("the same id on two hosts is two peers, and sends and disconnects reach the host and peer they came from") {
    auto pFirst = std::make_shared<scripted>(), pSecond = std::make_shared<scripted>();
    pFirst->events.push_back(of(event::kind::connected, 7));
    pSecond->events.push_back(of(event::kind::connected, 7));
    merged_host merged({pFirst, pSecond});

    const auto first = merged.poll(), second = merged.poll();
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first->peer != second->peer);
    REQUIRE_FALSE(merged.poll());

    const auto value = one(std::byte{42});
    merged.send(second->peer, delivery::unreliable, value);
    merged.disconnect(first->peer);
    REQUIRE(pFirst->sent.empty());
    REQUIRE(pSecond->sent.size() == 1);
    REQUIRE(pSecond->sent[0].peer == 7);
    REQUIRE(pSecond->sent[0].channel == delivery::unreliable);
    REQUIRE(pSecond->sent[0].data == value);
    REQUIRE(pFirst->dropped == std::vector<peer_id>{7});
    REQUIRE(pSecond->dropped.empty());
}

TEST_CASE("an id a host gives again after its peer left is a new peer") {
    auto pHost = std::make_shared<scripted>();
    pHost->events = {of(event::kind::connected, 3), of(event::kind::disconnected, 3), of(event::kind::connected, 3)};
    merged_host merged({pHost});

    const auto before = merged.poll();
    const auto gone = merged.poll();
    const auto after = merged.poll();
    REQUIRE(gone->type == event::kind::disconnected);
    REQUIRE(gone->peer == before->peer);
    REQUIRE(after->type == event::kind::connected);
    REQUIRE(after->peer != before->peer);

    merged.send(before->peer, delivery::reliable, one(std::byte{1}));
    REQUIRE(pHost->sent.empty());
    merged.send(after->peer, delivery::reliable, one(std::byte{2}));
    REQUIRE(pHost->sent.size() == 1);
}

TEST_CASE("a peer a host reports only as gone is reported once, under an id of its own, and is unknown after") {
    auto pHost = std::make_shared<scripted>();
    pHost->events = {of(event::kind::disconnected, 9)};
    merged_host merged({pHost});

    const auto gone = merged.poll();
    REQUIRE(gone);
    REQUIRE(gone->type == event::kind::disconnected);
    REQUIRE(gone->peer != 0);
    REQUIRE_FALSE(merged.poll());

    merged.send(gone->peer, delivery::reliable, one(std::byte{1}));
    merged.disconnect(gone->peer);
    REQUIRE(pHost->sent.empty());
    REQUIRE(pHost->dropped.empty());
}

TEST_CASE("polling takes turns between hosts, starting with the first given, and each host's events keep their order") {
    auto pFirst = std::make_shared<scripted>(), pSecond = std::make_shared<scripted>();
    pFirst->events = {of(event::kind::connected, 1), of(event::kind::received, 1, std::byte{10}),
        of(event::kind::received, 1, std::byte{11})};
    pSecond->events = {of(event::kind::connected, 1), of(event::kind::received, 1, std::byte{20})};
    merged_host merged({pFirst, pSecond});

    std::vector<std::pair<peer_id, std::optional<std::byte>>> seen;
    while (const auto polled = merged.poll())
        seen.emplace_back(polled->peer,
            polled->data.empty() ? std::nullopt : std::optional<std::byte>(polled->data.front()));

    REQUIRE(seen.size() == 5);
    const auto a = seen[0].first, b = seen[1].first;
    REQUIRE(a != b);
    REQUIRE(seen[2] == std::pair{a, std::optional{std::byte{10}}});
    REQUIRE(seen[3] == std::pair{b, std::optional{std::byte{20}}});
    REQUIRE(seen[4] == std::pair{a, std::optional{std::byte{11}}});
}

TEST_CASE("a host with nothing waiting does not keep the others from their turn") {
    auto pQuiet = std::make_shared<scripted>(), pBusy = std::make_shared<scripted>();
    for (int i = 0; i < 4; ++i)
        pBusy->events.push_back(of(event::kind::connected, static_cast<peer_id>(i + 1)));
    merged_host merged({pQuiet, pBusy});

    for (int i = 0; i < 4; ++i)
        REQUIRE(merged.poll());
    REQUIRE_FALSE(merged.poll());
}

TEST_CASE("sending to or disconnecting an id never given does nothing") {
    auto pHost = std::make_shared<scripted>();
    merged_host merged({pHost});

    merged.send(1, delivery::reliable, one(std::byte{1}));
    merged.disconnect(1);
    REQUIRE(pHost->sent.empty());
    REQUIRE(pHost->dropped.empty());
}

TEST_CASE("no hosts, or a null one, is refused, and a host's refusal to send is thrown on") {
    REQUIRE_THROWS_WITH(merged_host({}), Catch::Contains("no hosts"));
    REQUIRE_THROWS_WITH(merged_host({std::make_shared<scripted>(), nullptr}), Catch::Contains("host 1 is null"));

    auto pHost = std::make_shared<scripted>();
    pHost->events.push_back(of(event::kind::connected, 1));
    merged_host merged({pHost});
    const auto peer = merged.poll()->peer;
    pHost->refuse = true;
    REQUIRE_THROWS_AS(merged.send(peer, delivery::reliable, one(std::byte{1})), exception);
}

TEST_CASE("a loopback's own players and another's play through one merged host") {
    auto pLocal = std::make_shared<loopback_server>(), pRemote = std::make_shared<loopback_server>();
    merged_host merged({pLocal, pRemote});
    auto pGuest = pRemote->connect();
    auto pSeat = pLocal->connect();

    std::vector<peer_id> connected;
    while (const auto polled = merged.poll())
        connected.push_back(polled->peer);
    REQUIRE(connected.size() == 2);
    while (pSeat->poll()) {}
    while (pGuest->poll()) {}

    merged.send(connected[0], delivery::reliable, one(std::byte{1}));
    merged.send(connected[1], delivery::reliable, one(std::byte{2}));
    const auto seat = pSeat->poll(), guest = pGuest->poll();
    REQUIRE(seat->data == one(std::byte{1}));
    REQUIRE(guest->data == one(std::byte{2}));
}
