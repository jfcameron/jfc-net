// © Joseph Cameron - All Rights Reserved

#include <jfc/net/loopback.h>

#include <deque>
#include <map>

namespace jfc::net {
    namespace {
        using _queue = std::deque<event>;

        [[nodiscard]] event _connected(const peer_id aPeer) {
            return event{event::kind::connected, aPeer, delivery::reliable, {}};
        }

        [[nodiscard]] event _disconnected(const peer_id aPeer) {
            return event{event::kind::disconnected, aPeer, delivery::reliable, {}};
        }

        [[nodiscard]] event _received(const peer_id aPeer, const delivery aDelivery,
            const std::span<const std::byte> aMessage) {
            return event{event::kind::received, aPeer, aDelivery, {aMessage.begin(), aMessage.end()}};
        }

        [[nodiscard]] std::optional<event> _pop(_queue &aQueue) {
            if (aQueue.empty()) return std::nullopt;
            auto out = std::move(aQueue.front());
            aQueue.pop_front();
            return out;
        }
    }

    struct loopback_server::_state final {
        bool serverAlive = true;
        _queue serverInbox;
        std::map<peer_id, std::shared_ptr<_queue>> clients;
        peer_id nextPeer = 1;

        void end(const peer_id aPeer) {
            const auto found = clients.find(aPeer);
            if (found == clients.end()) return;
            found->second->push_back(_disconnected(SERVER_PEER));
            clients.erase(found);
            if (serverAlive) serverInbox.push_back(_disconnected(aPeer));
        }
    };

    class loopback_server::_client final : public host {
    public:
        _client(std::shared_ptr<_state> apState, const peer_id aId,
            std::shared_ptr<_queue> apInbox)
        : mpState(std::move(apState))
        , mId(aId)
        , mpInbox(std::move(apInbox)) {}

        ~_client() override {
            if (const auto found = mpState->clients.find(mId); found != mpState->clients.end()) {
                mpState->clients.erase(found);
                if (mpState->serverAlive) mpState->serverInbox.push_back(_disconnected(mId));
            }
        }

        _client(const _client &) = delete;
        _client &operator=(const _client &) = delete;

        void send(const peer_id aPeer, const delivery aDelivery,
            const std::span<const std::byte> aMessage) override {
            if (aPeer != SERVER_PEER || !_connected_now()) return;
            mpState->serverInbox.push_back(_received(mId, aDelivery, aMessage));
        }

        void disconnect(const peer_id aPeer) override {
            if (aPeer != SERVER_PEER || !_connected_now()) return;
            mpState->end(mId);
        }

        std::optional<event> poll() override {
            return _pop(*mpInbox);
        }

    private:
        [[nodiscard]] bool _connected_now() const {
            return mpState->serverAlive && mpState->clients.contains(mId);
        }

        std::shared_ptr<_state> mpState;
        peer_id mId;
        std::shared_ptr<_queue> mpInbox;
    };

    loopback_server::loopback_server()
    : mpState(std::make_shared<_state>()) {}

    loopback_server::~loopback_server() {
        mpState->serverAlive = false;
        for (auto &[id, pInbox] : mpState->clients) pInbox->push_back(_disconnected(SERVER_PEER));
        mpState->clients.clear();
    }

    std::unique_ptr<host> loopback_server::connect() {
        const auto id = mpState->nextPeer++;
        auto pInbox = std::make_shared<_queue>();

        pInbox->push_back(_connected(SERVER_PEER));
        mpState->clients.emplace(id, pInbox);
        mpState->serverInbox.push_back(_connected(id));

        return std::make_unique<_client>(mpState, id, std::move(pInbox));
    }

    void loopback_server::send(const peer_id aPeer, const delivery aDelivery,
        const std::span<const std::byte> aMessage) {
        if (const auto found = mpState->clients.find(aPeer); found != mpState->clients.end()) {
            found->second->push_back(_received(SERVER_PEER, aDelivery, aMessage));
        }
    }

    void loopback_server::disconnect(const peer_id aPeer) {
        mpState->end(aPeer);
    }

    std::optional<event> loopback_server::poll() {
        return _pop(mpState->serverInbox);
    }
}
