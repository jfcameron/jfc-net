# Browser WebRTC backend

`webrtc_host` implements `jfc::net::host` on the browser main thread. Link
`jfcnet_webrtc` when using `add_subdirectory`, or `jfc::net_webrtc` from the
installed package. The target links the JavaScript bridge automatically. No
application JavaScript, Asyncify, pthreads or game content are required.

Enable C++ exceptions consistently for the library and its consumer. The test
preset uses `-fwasm-exceptions`; applications choose their own exception mode.

The implementation uses the browser's `RTCPeerConnection` and two negotiated
data channels. This is a separate wire protocol from ENet. A native ENet client
cannot connect to it. A native WebRTC implementation remains future work.

## Connecting

```cpp
#include <jfc/net/webrtc_host.h>

jfc::net::webrtc_host_settings settings;
// No public servers or credentials are built in. Add your own if needed:
// settings.ice_servers.push_back({{"stun:stun.example.org:3478"}, {}, {}});
// settings.ice_servers.push_back({{"turn:turn.example.org:3478"}, user, credential});
jfc::net::webrtc_host connection(settings);

// Offering end:
const auto remote_peer = connection.offer();
// Later, forward connection.poll_signal()->description to the accepting end.

// Accepting end, using a separate host:
// const auto remote_peer = connection.accept(received_offer);
// Later, return its poll_signal()->description to the offering end.

// Offering end, once that answer arrives:
// connection.answer(remote_peer, received_answer);
```

Each `offer()` or `accept()` allocates a local peer ID. Route the answer back to
the offering peer ID; do not expect the two ends' IDs to match. A host can own
multiple independent peers, suitable for an authoritative host with several
clients. A peer ID is never reused during a host's lifetime.

Descriptions are opaque, versioned JSON. Each contains a complete SDP offer or
answer, including gathered ICE candidates. There is one outgoing description per
peer, returned only after ICE gathering completes. There is no trickle-ICE API or
renegotiation. A signaling service can carry these blobs; manual two-way
copy/paste works too. Invitation codes, authentication, lobby membership and
readiness belong to the application. Validate and authenticate signaling at that
boundary. Do not log descriptions or TURN credentials as public diagnostics.

In each application frame:

```cpp
connection.update(elapsed_ms); // application-owned elapsed time
while (const auto signal = connection.poll_signal()) {
    // Deliver signal->description through the application's signaling mechanism.
}
while (const auto event = connection.poll()) {
    // connected: may now send to event->peer
    // received: consume event->data and event->channel
    // disconnected: the final event for this peer
}
```

Calls must yield to the browser between frames so that ICE, promises and data
channel events can run. Do not busy-wait in C++. `update` advances only handshake
and graceful-close deadlines, not an idle timeout for connected peers. The browser
detects transport failure. A temporarily disconnected ICE connection may recover;
a failed or closed peer connection terminates the peer. `status(peer)` exposes
its phase, failure reason and combined browser send-buffer size, until the
disconnected event is consumed. Malformed API input throws synchronously;
asynchronous negotiation/transport failures produce a disconnected event.

## Delivery and resource limits

The reliable channel is ordered with retransmission. The replaceable channel is
unordered with zero retransmissions; uint32 transport sequence numbers reject
duplicates and old messages, including across sequence wrap. Neither channel
orders against the other. A readiness exchange on the reliable stream prevents
connection reporting before both ends have usable channels. Early unreliable
messages that overtake readiness can be dropped.

Logical messages default to a 256 KiB maximum. Frames have a 16-byte header and
are capped at 12 KiB, or the smaller negotiated SCTP `maxMessageSize`. Complete
messages are reassembled before reporting them. Each peer retains at most one
partial message per channel, each bounded by `most_message_bytes`; an incomplete
unreliable message is discarded when a newer one starts arriving. There are at
most 4096 fragments per message. Both peers should agree on limits. A malformed,
oversized or overlapping incoming frame terminates the peer with a protocol
failure. Empty messages and arbitrary binary bytes are supported.

`webrtc_host_settings` bounds peers (pending and closing included), signaling
bytes, logical message size, receive messages/bytes and browser send buffers.
There is no additional send queue. Before sending any fragment, the backend
checks the complete message's framed size against the remaining per-peer browser
buffer budget. A full buffer throws for reliable delivery and drops the complete
message for unreliable delivery. The caller chooses whether and when to retry a
reliable send. A browser exception during a partially sent message terminates
the connection, since retrying it cannot safely preserve the delivery contract.

A reliable receive-queue overflow terminates the peer after previously queued
messages; an unreliable overflow drops that message. Connection lifecycle events
have separate reserved capacity, bounded by two events per retained peer. Closing
peers retain their capacity until their terminal event is consumed and browser
cleanup completes. The total library receive storage is bounded by the host's
receive quota plus two partial messages per peer. Browser/SCTP internal receive
buffers are managed by the browser.

`disconnect` stops new sends, queues the local terminal event, and calls data
channel `close()` to flush accepted reliable sends. The peer connection is closed
after the reliable channel closes. A caller-supplied close deadline can force
termination if draining stalls. Network failure, destruction, protocol failure
and overload can also interrupt delivery, as with other connection-oriented
transports. Destruction invalidates all callbacks and promise continuations and
releases browser objects; it does not wait for a drain.

## Hosting and services

Static hosting distributes Wasm/JavaScript and data. It does not provide signaling
or TURN. Without ICE servers, only candidates available directly to the browser
are gathered. Some local connections may work; internet hosting generally needs
application-selected STUN and TURN services. Configure `relay_only` with a TURN
server to require relayed candidates. Credentials are supplied at runtime; short
lived TURN credentials should be issued by the application's chosen service.

This backend does not discover WAN/LAN addresses, test a raw open port, operate a
signaling service, migrate authority, or reconnect automatically. Browser address
privacy also means IP-only invitations are not a substitute for SDP exchange.

## Building and testing

From this repository:

```sh
export EM_CACHE="$PWD/out/emscripten-cache"
emcmake cmake --preset emscripten-test
cmake --build --preset emscripten-test
ctest --preset emscripten-test
```

CTest runs existing portable codec/impaired-host tests, Catch2 C++/Wasm bridge
tests, the JavaScript contract suite and the compiled probe with test-owned
WebRTC platform objects. Those fixtures open no sockets and do not establish
that actual ICE/SCTP, a NAT or a TURN deployment works. The native loopback suite
includes a real thread test; the ENet suite requires UDP. Neither is run in the
single-threaded Emscripten configuration.

The build also produces `out/build/emscripten-test/jfcnet_webrtc_probe.html`.
Serve that directory over HTTP and open the page in a browser. The probe makes
two real local peer connections, round-trips 64 KiB and empty reliable messages,
and verifies graceful disconnect. Its output ends with
`JFC_NET_WEBRTC_PROBE_PASS` on success. It uses no STUN/TURN or game assets.

For actual Chromium/Firefox integration through CTest, install Playwright and
its browsers, then opt in (CI does this):

```sh
npm install --no-save --package-lock=false playwright
npx playwright install --with-deps chromium firefox
emcmake cmake --preset emscripten-test -DJFC_NET_TEST_REAL_BROWSERS=ON
cmake --build --preset emscripten-test
ctest --preset emscripten-test
```

The real-browser test establishes connections between separate browser contexts,
checks binary fragmentation, reliable order, unreliable sequencing and draining
on disconnect, then runs the compiled C++/Wasm probe. Set `JFC_NET_BROWSERS` to a
comma-separated subset of `chromium,firefox` to select browsers. To also require a
real TURN-relayed path, supply `JFC_NET_TURN_URL`, `JFC_NET_TURN_USERNAME` and
`JFC_NET_TURN_CREDENTIAL` in the test environment. No relay service is provisioned
by the tests. Same-machine direct or relay tests still do not prove that actual
WAN/NAT combinations work; those need separate deployment/playtesting.

Platform contracts: [WebRTC specification](https://www.w3.org/TR/webrtc/),
[Emscripten JavaScript library integration](https://emscripten.org/docs/porting/connecting_cpp_and_javascript/Interacting-with-code.html).
