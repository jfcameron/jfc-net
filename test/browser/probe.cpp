// © Joseph Cameron - MIT License
// Standalone, game-independent real-browser probe. The fixture runner replaces
// only the platform objects; the C++ API and compiled bridge are the same.

#include <jfc/net/webrtc_host.h>

#include <emscripten.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace {
    using namespace jfc::net;

    struct probe final {
        webrtc_host first;
        webrtc_host second;
        peer_id outgoing = first.offer();
        peer_id incoming = 0;
        double last = emscripten_get_now();
        double age = 0;
        unsigned received = 0;
        unsigned disconnected = 0;
        bool sent = false;
        bool closing = false;
        std::vector<std::byte> bytes = std::vector<std::byte>(65536);

        probe() {
            for (std::size_t i = 0; i < bytes.size(); ++i) bytes[i] = static_cast<std::byte>(i % 256);
        }

        void tick() {
            const auto now = emscripten_get_now();
            const auto elapsed = static_cast<std::uint32_t>(std::max(0.0, now - last));
            age += now - last;
            last = now;
            first.update(elapsed);
            second.update(elapsed);
            if (age > 35000) throw std::runtime_error("probe timeout");
            if (const auto signal = first.poll_signal()) incoming = second.accept(signal->description);
            if (const auto signal = second.poll_signal()) first.answer(outgoing, signal->description);
            while (const auto event = first.poll()) {
                if (event->type == event::kind::connected) {
                    sent = true;
                    first.send(outgoing, delivery::reliable, bytes);
                    first.send(outgoing, delivery::reliable, {});
                } else if (event->type == event::kind::received) {
                    if ((received == 0 && event->data != bytes) || (received == 1 && !event->data.empty()))
                        throw std::runtime_error("probe payload mismatch");
                    ++received;
                } else {
                    if (!closing) throw std::runtime_error("unexpected first disconnect");
                    ++disconnected;
                }
            }
            while (const auto event = second.poll()) {
                if (event->type == event::kind::received) second.send(incoming, delivery::reliable, event->data);
                else if (event->type == event::kind::disconnected) {
                    if (!closing) throw std::runtime_error("unexpected second disconnect");
                    ++disconnected;
                }
            }
            if (sent && received == 2 && !closing) {
                closing = true;
                first.disconnect(outgoing);
            }
            if (disconnected == 2) {
                std::puts("JFC_NET_WEBRTC_PROBE_PASS: 64 KiB and empty message round trips; graceful disconnect");
                emscripten_cancel_main_loop();
                emscripten_force_exit(0);
            }
        }
    };

    void tick(void *apProbe) {
        try { static_cast<probe *>(apProbe)->tick(); }
        catch (const std::exception &aError) {
            std::fprintf(stderr, "JFC_NET_WEBRTC_PROBE_FAIL: %s\n", aError.what());
            emscripten_cancel_main_loop();
            emscripten_force_exit(1);
        }
    }
}

int main() {
    static probe instance;
    std::puts("Connecting two browser peers using real WebRTC (no STUN/TURN, local candidates only)...");
    emscripten_set_main_loop_arg(tick, &instance, 0, true);
}
