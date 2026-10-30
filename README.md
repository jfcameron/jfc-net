# jfc-net
A small networking library for games. It defines a message based transport interface, provides implementations within a process, over UDP and through browser WebRTC, and provides a binary codec for building and reading the messages sent over it.

API documentation: https://jfcameron.github.io/jfc-net/

## Getting Started
Install cmake 3.21 or greater, and a c++20 compatible compiler
```
# initialize the tree
git submodule update --init --recursive

# generate the script to build the library and its tests on linux using gcc
cmake --preset linux-gcc

# build the library and tests
cmake --build --preset linux-gcc

# run the tests
ctest --preset linux-gcc
```
See `CMakePresets.json` for the full list of supported platforms and compilers. See `.github/workflows/ci.yml` to see exactly how CI runners build it on different platforms.

To use it from another project, add it with `add_subdirectory` and link `jfcnet` for the interface, the codec, the loopback and the impaired host, `jfcnet_enet` for the UDP host, or `jfcnet_webrtc` for browser WebRTC. ENet is linked privately, so its headers never reach the project using it. Installed CMake targets are `jfc::net`, `jfc::net_enet` and, with Emscripten, `jfc::net_webrtc`.

## Overview
### Hosts
A host is one end of a transport. It sends messages to peers, and reports what happened through `poll`, which returns the oldest event waiting, or nothing if nothing is waiting. There are three kinds of event: a peer connected, a message was received from a peer, and a peer disconnected. Disconnected is always the last event about a peer, and sending to a peer that is gone does nothing.

The library has no callbacks and starts no threads. The program polls each host until it is empty, usually once per tick, and decides for itself which thread does that. A host is not thread safe, so it is used from one thread at a time.

### Delivery
Every message is sent one of two ways. A reliable message arrives whole, once, and after everything sent reliably to the same peer before it. It is for what must not be lost: a chat line, a spawn, a request and its answer. An unreliable message may be lost, and is never delivered after a newer unreliable message to the same peer, so a late one is dropped rather than arriving out of order. It is for what a newer copy replaces: inputs, snapshots.

### Implementations
#### `loopback_server`
A server and its clients within one process. `connect` returns a new client. Messages arrive immediately, in order, and nothing is lost. Unlike other hosts, the server and each of its clients may each be used from a thread of its own, so a game can step its server on a thread beside the one that draws. This is the transport for single player and for tests.

#### `enet_host`
A transport between processes over UDP, built on [ENet](https://github.com/lsalzman/enet). `listen` makes a server's end on a port, and `connect` makes a client's end. Reliable and unreliable messages each travel on an ENet channel of their own, and a peer that goes silent is let go after a timeout, 5 seconds by default. Polling is what services the connection, so a host must be polled regularly to stay connected, and a peer should only be sent to after its connected event, since anything sent before is dropped. `lan_address` gives the address other machines on the local network would use to reach this one, for a hosting screen to show.

It is IPv4 only. Browsers do not allow UDP, so Emscripten builds provide `jfcnet_webrtc` instead of the native `jfcnet_enet` target.

#### `webrtc_host`
A browser transport over WebRTC data channels, built with Emscripten. It implements the same polled host interface. The caller exchanges opaque offer/answer descriptions, configures any STUN/TURN services, and supplies elapsed time for connection deadlines. It starts no threads, timers or application loop. See [the browser backend guide](docs/webrtc.md) for setup, delivery limits and tests.

#### `impaired_host`
Wraps another host and makes it deliberately unreliable, to simulate a bad network: latency, jitter, the loss of unreliable messages, and reordering if asked for. Reliable messages are still never lost or reordered, so the delivery rules above hold however bad it gets. It takes a seed, and optionally a clock, so the same impairments give the same run every time. This makes it possible to test how a game behaves at 200ms with 5% loss without needing a bad network to test on.

### Messages
#### `writer`
Builds a message's bytes one field after another: fixed width integers and floats, booleans, variable width integers, length prefixed strings and byte arrays, and raw bytes whose size the reader already knows. Fixed width values are little endian, floats keep their IEEE 754 bits, and variable width integers are LEB128, one byte below 128, with signed ones zigzagged so small negatives stay small.

#### `reader`
Reads back what a writer wrote, in the same order. What it reads came from the network, so it treats every message as untrusted. Reading a string or byte array requires a maximum length from the caller. There is no unbounded read, so a message cannot make the reader allocate more than the caller allowed. It also refuses truncated fields, booleans other than 0 or 1, varints longer than their value needs, and, at `expect_end`, bytes left over. A refusal is thrown as a `jfc::net::exception` saying what was being read and at which byte. The reader is fuzzed against the writer: anything it accepts must write back as exactly the same bytes.
