// © Joseph Cameron - MIT License
// Test-owned WebRTC platform double. It does not open a socket or implement SCTP/ICE.
globalThis.JfcRtcFixture = {
    peers: [],
    hold: false,
    packets: [],
    failOffer: false,
    failRemote: false,
    maxMessageSize: 65536,
    deliver: function(aPacket) {
        const other = aPacket.channel.pc.other;
        const channel = other && other.channels[aPacket.channel.id];
        if (channel && channel.onmessage) channel.onmessage({data: aPacket.data});
    },
    open: function(aPeer) {
        for (const channel of aPeer.channels) {
            channel.readyState = 'open';
            if (channel.onopen) channel.onopen();
        }
    },
    ready: function(aPeer) {
        const data = new ArrayBuffer(16);
        new DataView(data).setUint32(0, 0x3152464a, true);
        aPeer.channels[0].onmessage({data: data});
    }
};

globalThis.RTCPeerConnection = class {
    constructor(aConfig) {
        this.id = JfcRtcFixture.peers.length;
        JfcRtcFixture.peers.push(this);
        this.config = aConfig;
        this.channels = [];
        this.iceGatheringState = 'new';
        this.connectionState = 'new';
        this.sctp = {maxMessageSize: JfcRtcFixture.maxMessageSize};
    }
    createDataChannel(aLabel, aSettings) {
        const pc = this;
        const channel = {
            pc: pc, id: aSettings.id, settings: aSettings, label: aLabel,
            readyState: 'connecting', bufferedAmount: 0,
            send: function(aData) {
                if (this.readyState !== 'open') throw Error('closed channel');
                if (aData.byteLength > pc.sctp.maxMessageSize && pc.sctp.maxMessageSize) throw Error('SCTP limit');
                const packet = {channel: this, data: aData.slice(0)};
                if (JfcRtcFixture.hold) { JfcRtcFixture.packets.push(packet); this.bufferedAmount += aData.byteLength; }
                else queueMicrotask(() => JfcRtcFixture.deliver(packet));
            },
            close: function() {
                this.readyState = 'closing';
                queueMicrotask(() => {
                    for (const packet of JfcRtcFixture.packets.filter(aPacket => aPacket.channel === this))
                        JfcRtcFixture.deliver(packet);
                    JfcRtcFixture.packets = JfcRtcFixture.packets.filter(aPacket => aPacket.channel !== this);
                    this.bufferedAmount = 0;
                    this.readyState = 'closed';
                    const other = pc.other && pc.other.channels[this.id];
                    if (other) { other.readyState = 'closed'; if (other.onclose) other.onclose(); }
                    if (this.onclose) this.onclose();
                });
            }
        };
        this.channels.push(channel);
        return channel;
    }
    createOffer() {
        return JfcRtcFixture.failOffer ? Promise.reject(Error('offer failed')) :
            Promise.resolve({type: 'offer', sdp: 'fixture:' + this.id});
    }
    createAnswer() { return Promise.resolve({type: 'answer', sdp: 'fixture:' + this.id}); }
    setLocalDescription(aDescription) {
        this.localDescription = aDescription;
        this.iceGatheringState = 'complete';
        if (this.onicegatheringstatechange) this.onicegatheringstatechange();
        return Promise.resolve();
    }
    setRemoteDescription(aDescription) {
        if (JfcRtcFixture.failRemote) return Promise.reject(Error('SDP failed'));
        this.other = JfcRtcFixture.peers[Number(aDescription.sdp.split(':')[1])];
        if (!this.other) return Promise.reject(Error('bad peer'));
        this.remoteDescription = aDescription;
        if (this.other.remoteDescription) {
            JfcRtcFixture.open(this);
            JfcRtcFixture.open(this.other);
        }
        return Promise.resolve();
    }
    close() { this.connectionState = 'closed'; if (this.onconnectionstatechange) this.onconnectionstatechange(); }
};
