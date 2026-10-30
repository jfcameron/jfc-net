// © Joseph Cameron - MIT License
// Browser objects stay here. Nothing calls into Wasm from an asynchronous callback.

addToLibrary({
    $JfcNetRtc: {
        hosts: new Map(),
        nextHost: 1,
        headerBytes: 16,
        frameBytes: 12 * 1024,

        create: function(aSettings) {
            if (typeof RTCPeerConnection !== 'function' || this.nextHost > 0x7fffffff) return 0;
            const id = this.nextHost++;
            this.hosts.set(id, {settings: aSettings, peers: new Map(), nextPeer: 1,
                events: [], queuedBytes: 0, queuedMessages: 0, iceServers: []});
            return id;
        },

        live: function(aHost, aPeer) {
            return !aHost.destroyed && aHost.peers.get(aPeer.id) === aPeer && !aPeer.ended;
        },

        cleanup: function(aHost, aPeer) {
            if (aPeer.ended && aPeer.consumed) aHost.peers.delete(aPeer.id);
        },

        terminal: function(aHost, aPeer, aReason) {
            if (aPeer.terminal) return;
            aPeer.terminal = true;
            aPeer.reason = aReason;
            aPeer.signal = null;
            aPeer.reassembly = [null, null];
            // Lifecycle events have reserved capacity: at most two per retained peer.
            aHost.events.push({type: 2, peer: aPeer.id, channel: 0, data: new Uint8Array(0)});
        },

        finish: function(aHost, aPeer, aReason) {
            if (!this.live(aHost, aPeer)) return;
            this.terminal(aHost, aPeer, aReason);
            aPeer.ended = true;
            for (const channel of aPeer.channels) {
                channel.onopen = channel.onclose = channel.onerror = channel.onmessage = null;
            }
            aPeer.pc.onicegatheringstatechange = aPeer.pc.onconnectionstatechange = null;
            aPeer.pc.close();
            this.cleanup(aHost, aPeer);
        },

        destroy: function(aHandle) {
            const host = this.hosts.get(aHandle);
            if (!host) return;
            for (const peer of host.peers.values()) this.finish(host, peer, 1);
            host.destroyed = true;
            host.peers.clear();
            host.events.length = 0;
            this.hosts.delete(aHandle);
        },

        parse: function(aHost, aText, aType) {
            if (!aText || aText.length > aHost.settings.signalBytes) throw Error('signal size');
            const value = JSON.parse(aText);
            if (!value || value.version !== 1 || value.type !== aType || typeof value.sdp !== 'string' ||
                !value.sdp.length || value.sdp.length > aHost.settings.signalBytes || value.sdp.includes('\0'))
                throw Error('signal format');
            return {type: value.type, sdp: value.sdp};
        },

        publish: function(aHost, aPeer) {
            if (!this.live(aHost, aPeer) || aPeer.terminal || aPeer.published ||
                aPeer.pc.iceGatheringState !== 'complete' || !aPeer.pc.localDescription) return;
            const description = aPeer.pc.localDescription;
            if (typeof description.sdp !== 'string' || description.sdp.length > aHost.settings.signalBytes) {
                this.finish(aHost, aPeer, 2); return;
            }
            const text = JSON.stringify({version: 1, type: description.type, sdp: description.sdp});
            const data = new TextEncoder().encode(text);
            if (data.length > aHost.settings.signalBytes) { this.finish(aHost, aPeer, 2); return; }
            aPeer.published = true;
            aPeer.signal = data;
        },

        connected: function(aHost, aPeer) {
            if (!aPeer.terminal && !aPeer.ready && aPeer.localReady && aPeer.remoteReady) {
                aPeer.ready = true;
                aHost.events.push({type: 0, peer: aPeer.id, channel: 0, data: new Uint8Array(0)});
            }
        },

        make: function(aHost, aOffering) {
            if (aHost.peers.size >= aHost.settings.peers || aHost.nextPeer > 0xffffffff) throw Error('peer limit');
            const pc = new RTCPeerConnection({iceServers: aHost.iceServers,
                iceTransportPolicy: aHost.settings.relayOnly ? 'relay' : 'all'});
            const peer = {id: aHost.nextPeer++, pc: pc, channels: [], offering: aOffering,
                answered: false, published: false, signal: null, age: 0, closing: false,
                ended: false, terminal: false, consumed: false, ready: false, reported: false,
                localReady: false, remoteReady: false,
                reason: 0, sent: [0, 0], received: [null, null], reassembly: [null, null]};
            aHost.peers.set(peer.id, peer);
            try {
                for (let i = 0; i < 2; ++i) {
                    const channel = pc.createDataChannel(i ? 'jfc-unreliable' : 'jfc-reliable',
                        i ? {negotiated: true, id: 1, ordered: false, maxRetransmits: 0, protocol: 'jfc-net/1'} :
                            {negotiated: true, id: 0, ordered: true, protocol: 'jfc-net/1'});
                    peer.channels.push(channel);
                    channel.binaryType = 'arraybuffer';
                    channel.onopen = () => {
                        if (!this.live(aHost, peer) || peer.terminal || peer.localReady) return;
                        if (peer.channels.length === 2 && peer.channels.every(aChannel => aChannel.readyState === 'open')) {
                            peer.localReady = true;
                            const ready = new ArrayBuffer(16);
                            new DataView(ready).setUint32(0, 0x3152464a, true); // JFR1
                            try { peer.channels[0].send(ready); }
                            catch (error) { this.finish(aHost, peer, 3); return; }
                            this.connected(aHost, peer);
                        }
                    };
                    channel.onmessage = aEvent => {
                        if (this.live(aHost, peer) && !peer.terminal) this.receive(aHost, peer, i, aEvent.data);
                    };
                    channel.onerror = () => this.finish(aHost, peer, 3);
                    channel.onclose = () => {
                        if (!this.live(aHost, peer)) return;
                        // The unreliable stream may close before reliable messages drain.
                        if (i === 0) this.finish(aHost, peer, 1);
                        else if (!peer.closing) { peer.closing = true; peer.age = 0; }
                    };
                }
                pc.onicegatheringstatechange = () => this.publish(aHost, peer);
                pc.onconnectionstatechange = () => {
                    if (pc.connectionState === 'failed' || pc.connectionState === 'closed') this.finish(aHost, peer, 3);
                };
                return peer;
            } catch (error) {
                // No peer was returned to the caller, so do not produce a ghost event.
                peer.consumed = true;
                peer.terminal = true;
                this.finish(aHost, peer, 2);
                throw error;
            }
        },

        offer: function(aHandle) {
            const host = this.hosts.get(aHandle);
            try {
                const peer = this.make(host, true);
                Promise.resolve().then(() => {
                    if (this.live(host, peer) && !peer.terminal) return peer.pc.createOffer();
                }).then(aDescription => {
                    if (aDescription && this.live(host, peer) && !peer.terminal) return peer.pc.setLocalDescription(aDescription);
                }).then(() => this.publish(host, peer)).catch(() => this.finish(host, peer, 2));
                return peer.id;
            } catch (error) { return 0; }
        },

        accept: function(aHandle, aText) {
            const host = this.hosts.get(aHandle);
            try {
                const description = this.parse(host, aText, 'offer');
                const peer = this.make(host, false);
                Promise.resolve().then(() => {
                    if (this.live(host, peer) && !peer.terminal) return peer.pc.setRemoteDescription(description);
                }).then(() => {
                    if (this.live(host, peer) && !peer.terminal) return peer.pc.createAnswer();
                }).then(aAnswer => {
                    if (aAnswer && this.live(host, peer) && !peer.terminal) return peer.pc.setLocalDescription(aAnswer);
                }).then(() => this.publish(host, peer)).catch(() => this.finish(host, peer, 2));
                return peer.id;
            } catch (error) { return 0; }
        },

        answer: function(aHandle, aId, aText) {
            const host = this.hosts.get(aHandle);
            const peer = host.peers.get(aId);
            try {
                const description = this.parse(host, aText, 'answer');
                if (!peer || peer.terminal || !peer.offering || peer.answered || !peer.published) return 0;
                peer.answered = true;
                Promise.resolve().then(() => {
                    if (this.live(host, peer) && !peer.terminal) return peer.pc.setRemoteDescription(description);
                }).catch(() => this.finish(host, peer, 2));
                return 1;
            } catch (error) { return 0; }
        },

        newer: function(aSequence, aPrevious) {
            return aPrevious === null || ((aSequence - aPrevious) >>> 0) > 0 && ((aSequence - aPrevious) >>> 0) < 0x80000000;
        },

        receive: function(aHost, aPeer, aChannel, aData) {
            const fail = () => this.finish(aHost, aPeer, 4);
            if (!(aData instanceof ArrayBuffer) || aData.byteLength < this.headerBytes ||
                aData.byteLength > this.frameBytes) { fail(); return; }
            const view = new DataView(aData);
            if (view.getUint32(0, true) === 0x3152464a) {
                if (aChannel || aPeer.remoteReady || aData.byteLength !== 16 || view.getUint32(4, true) ||
                    view.getUint32(8, true) || view.getUint32(12, true)) { fail(); return; }
                aPeer.remoteReady = true;
                this.connected(aHost, aPeer);
                return;
            }
            if (view.getUint32(0, true) !== 0x3143464a) { fail(); return; } // JFC1
            // Unreliable data may overtake the remote ready frame on the other
            // stream. Dropping it is allowed; reliable data must follow ready.
            if (!aPeer.ready) { if (!aChannel) fail(); return; }
            const sequence = view.getUint32(4, true);
            const length = view.getUint32(8, true);
            const offset = view.getUint32(12, true);
            const count = aData.byteLength - this.headerBytes;
            if (length > aHost.settings.messageBytes || offset > length || count > length - offset ||
                (length && !count)) { fail(); return; }
            if (!this.newer(sequence, aPeer.received[aChannel])) return;
            let assembly = aPeer.reassembly[aChannel];
            if (assembly && assembly.sequence !== sequence) {
                if (!aChannel) { fail(); return; }
                if (!this.newer(sequence, assembly.sequence)) return;
                assembly = null;
            }
            if (!assembly) {
                if (!aChannel && offset !== 0) { fail(); return; }
                assembly = {sequence: sequence, length: length, bytes: new Uint8Array(length),
                    ranges: new Map(), count: 0};
                aPeer.reassembly[aChannel] = assembly;
            }
            if (length !== assembly.length || (!aChannel && offset !== assembly.count)) { fail(); return; }
            // Frames can arrive unordered, but overlapping fragments are never valid.
            // At most 4096 fragments/message, including duplicates, to bound callback work.
            if (assembly.ranges.size >= 4096) { fail(); return; }
            for (const [start, size] of assembly.ranges) {
                if (start === offset && size === count && aChannel) return;
                if (offset < start + size && start < offset + count) { fail(); return; }
            }
            assembly.ranges.set(offset, count);
            assembly.bytes.set(new Uint8Array(aData, this.headerBytes), offset);
            assembly.count += count;
            if (assembly.count !== length) return;
            aPeer.reassembly[aChannel] = null;
            aPeer.received[aChannel] = sequence;
            if (aHost.queuedMessages >= aHost.settings.queuedMessages ||
                length > aHost.settings.queuedBytes - aHost.queuedBytes) {
                if (!aChannel) this.finish(aHost, aPeer, 5);
                return;
            }
            aHost.events.push({type: 1, peer: aPeer.id, channel: aChannel, data: assembly.bytes});
            ++aHost.queuedMessages;
            aHost.queuedBytes += length;
        },

        send: function(aHandle, aId, aChannel, aData) {
            const host = this.hosts.get(aHandle);
            const peer = host.peers.get(aId);
            if (!peer || !peer.reported || peer.terminal || peer.closing) return 0;
            const maximum = peer.pc.sctp && peer.pc.sctp.maxMessageSize;
            const frame = Math.min(this.frameBytes, maximum === 0 ? this.frameBytes : maximum || 65536);
            if (frame <= this.headerBytes) { this.finish(host, peer, 4); return 2; }
            const payload = frame - this.headerBytes;
            const frames = Math.max(1, Math.ceil(aData.length / payload));
            const bytes = aData.length + frames * this.headerBytes;
            const buffered = peer.channels[0].bufferedAmount + peer.channels[1].bufferedAmount;
            if (frames > 4096 || bytes > host.settings.bufferedBytes - buffered) return aChannel ? 0 : 1;
            const sequence = peer.sent[aChannel];
            peer.sent[aChannel] = (sequence + 1) >>> 0;
            try {
                for (let i = 0, offset = 0; i < frames; ++i) {
                    const count = Math.min(payload, aData.length - offset);
                    const data = new Uint8Array(this.headerBytes + count);
                    const view = new DataView(data.buffer);
                    view.setUint32(0, 0x3143464a, true);
                    view.setUint32(4, sequence, true);
                    view.setUint32(8, aData.length, true);
                    view.setUint32(12, offset, true);
                    data.set(aData.subarray(offset, offset + count), this.headerBytes);
                    peer.channels[aChannel].send(data.buffer);
                    offset += count;
                }
                return 0;
            } catch (error) {
                // A browser failure mid-message cannot safely be retried as a whole.
                this.finish(host, peer, 3);
                return 2;
            }
        },

        disconnect: function(aHandle, aId) {
            const host = this.hosts.get(aHandle);
            const peer = host.peers.get(aId);
            if (!peer || peer.terminal) return;
            if (!peer.ready) { this.finish(host, peer, 1); return; }
            peer.closing = true;
            peer.age = 0;
            this.terminal(host, peer, 1);
            // RTCDataChannel.close drains the send buffer. Closing the peer
            // connection here would instead discard accepted reliable messages.
            for (const channel of peer.channels) channel.close();
        },

        update: function(aHandle, aElapsed) {
            const host = this.hosts.get(aHandle);
            for (const peer of host.peers.values()) {
                if (peer.ended || (peer.ready && !peer.closing)) continue;
                peer.age += aElapsed;
                if (peer.age >= host.settings.timeout) this.finish(host, peer, 6);
            }
        },

        peek: function(aHandle, aSignal) {
            const host = this.hosts.get(aHandle);
            if (!aSignal) return host.events[0];
            for (const peer of host.peers.values()) if (peer.signal)
                return {type: 0, peer: peer.id, channel: 0, data: peer.signal};
            return null;
        },

        take: function(aHandle, aSignal) {
            const host = this.hosts.get(aHandle);
            const event = this.peek(aHandle, aSignal);
            if (!event) return null;
            const peer = host.peers.get(event.peer);
            if (aSignal) peer.signal = null;
            else {
                host.events.shift();
                if (event.type === 0) peer.reported = true;
                if (event.type === 1) { --host.queuedMessages; host.queuedBytes -= event.data.length; }
                if (event.type === 2) { peer.consumed = true; this.cleanup(host, peer); }
            }
            return event;
        }
    },

    jfc_net_rtc_create__deps: ['$JfcNetRtc'],
    jfc_net_rtc_create: function(aPeers, aMessage, aMessages, aQueued, aBuffered, aSignal, aTimeout, aRelay) {
        return JfcNetRtc.create({peers: aPeers, messageBytes: aMessage, queuedMessages: aMessages,
            queuedBytes: aQueued, bufferedBytes: aBuffered, signalBytes: aSignal, timeout: aTimeout >>> 0, relayOnly: !!aRelay});
    },
    jfc_net_rtc_destroy__deps: ['$JfcNetRtc'],
    jfc_net_rtc_destroy: function(aHost) { JfcNetRtc.destroy(aHost); },
    jfc_net_rtc_ice__deps: ['$JfcNetRtc', '$UTF8ToString'],
    jfc_net_rtc_ice: function(aHost, aUrl, aUrlBytes, aUser, aUserBytes, aCredential, aCredentialBytes, aFirst) {
        const host = JfcNetRtc.hosts.get(aHost);
        const url = UTF8ToString(aUrl, aUrlBytes);
        if (!/^(stun|stuns|turn|turns):[^\s]+$/.test(url)) return 0;
        if (aFirst) host.iceServers.push({urls: [], username: UTF8ToString(aUser, aUserBytes),
            credential: UTF8ToString(aCredential, aCredentialBytes)});
        host.iceServers[host.iceServers.length - 1].urls.push(url);
        return 1;
    },
    jfc_net_rtc_offer__deps: ['$JfcNetRtc'],
    jfc_net_rtc_offer: function(aHost) { return JfcNetRtc.offer(aHost); },
    jfc_net_rtc_accept__deps: ['$JfcNetRtc', '$UTF8ToString'],
    jfc_net_rtc_accept: function(aHost, aText, aBytes) { return JfcNetRtc.accept(aHost, UTF8ToString(aText, aBytes)); },
    jfc_net_rtc_answer__deps: ['$JfcNetRtc', '$UTF8ToString'],
    jfc_net_rtc_answer: function(aHost, aPeer, aText, aBytes) { return JfcNetRtc.answer(aHost, aPeer >>> 0, UTF8ToString(aText, aBytes)); },
    jfc_net_rtc_update__deps: ['$JfcNetRtc'],
    jfc_net_rtc_update: function(aHost, aElapsed) { JfcNetRtc.update(aHost, aElapsed >>> 0); },
    jfc_net_rtc_disconnect__deps: ['$JfcNetRtc'],
    jfc_net_rtc_disconnect: function(aHost, aPeer) { JfcNetRtc.disconnect(aHost, aPeer >>> 0); },
    jfc_net_rtc_send__deps: ['$JfcNetRtc'],
    jfc_net_rtc_send: function(aHost, aPeer, aChannel, aData, aBytes) {
        return JfcNetRtc.send(aHost, aPeer >>> 0, aChannel, HEAPU8.subarray(aData, aData + aBytes));
    },
    jfc_net_rtc_status__deps: ['$JfcNetRtc'],
    jfc_net_rtc_status: function(aHost, aId, aValues) {
        const peer = JfcNetRtc.hosts.get(aHost).peers.get(aId >>> 0);
        if (!peer || peer.consumed) return 0;
        HEAPU32[aValues >> 2] = peer.terminal ? 3 : peer.closing ? 2 : peer.ready ? 1 : 0;
        HEAPU32[(aValues + 4) >> 2] = peer.reason;
        HEAPU32[(aValues + 8) >> 2] = peer.channels.reduce((aSum, aChannel) => aSum + aChannel.bufferedAmount, 0);
        return 1;
    },
    jfc_net_rtc_peek__deps: ['$JfcNetRtc'],
    jfc_net_rtc_peek: function(aHost, aSignal, aMeta) {
        const event = JfcNetRtc.peek(aHost, aSignal);
        if (!event) return 0;
        HEAPU32[aMeta >> 2] = event.type;
        HEAPU32[(aMeta + 4) >> 2] = event.peer;
        HEAPU32[(aMeta + 8) >> 2] = event.channel;
        HEAPU32[(aMeta + 12) >> 2] = event.data.length;
        return 1;
    },
    jfc_net_rtc_take__deps: ['$JfcNetRtc'],
    jfc_net_rtc_take: function(aHost, aSignal, aData) {
        const event = JfcNetRtc.take(aHost, aSignal);
        HEAPU8.set(event.data, aData);
    }
});
