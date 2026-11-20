// © Joseph Cameron - MIT License

#include <jfc/net/ext/merged_host.h>
#include <jfc/net/exception.h>

#include <limits>
#include <string>
#include <utility>

namespace jfc::net::ext {
    merged_host::merged_host(std::vector<std::shared_ptr<host>> aHosts)
        : mHosts(std::move(aHosts)), mOuter(mHosts.size()) {
        if (mHosts.empty())
            throw exception("merged_host: no hosts to merge");

        for (std::size_t i = 0; i < mHosts.size(); ++i)
            if (!mHosts[i])
                throw exception("merged_host: host " + std::to_string(i) + " is null");
    }

    void merged_host::send(const peer_id aPeer, const delivery aDelivery, const std::span<const std::byte> aMessage) {
        if (const auto found = mInner.find(aPeer); found != mInner.end())
            mHosts[found->second.host]->send(found->second.peer, aDelivery, aMessage);
    }

    void merged_host::disconnect(const peer_id aPeer) {
        if (const auto found = mInner.find(aPeer); found != mInner.end())
            mHosts[found->second.host]->disconnect(found->second.peer);
    }

    std::optional<event> merged_host::poll() {
        for (std::size_t tried = 0; tried < mHosts.size(); ++tried) {
            const auto at = mTurn;
            mTurn = (mTurn + 1) % mHosts.size();
            auto polled = mHosts[at]->poll();
            if (!polled)
                continue;

            auto &outer = mOuter[at];
            auto found = outer.find(polled->peer);
            if (found == outer.end()) {
                if (mNext == std::numeric_limits<peer_id>::max())
                    throw exception("merged_host: every peer id has been given");
                found = outer.emplace(polled->peer, mNext).first;
                mInner.emplace(mNext, inner{at, polled->peer});
                ++mNext;
            }

            polled->peer = found->second;
            if (polled->type == event::kind::disconnected) {
                mInner.erase(found->second);
                outer.erase(found);
            }
            return polled;
        }
        return std::nullopt;
    }
}
