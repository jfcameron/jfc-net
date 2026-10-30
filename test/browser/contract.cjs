// © Joseph Cameron - MIT License
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
vm.runInThisContext(fs.readFileSync(path.join(__dirname, 'fixture.js'), 'utf8'));
global.addToLibrary = aLibrary => { global.JfcNetRtc = aLibrary.$JfcNetRtc; };
vm.runInThisContext(fs.readFileSync(path.join(__dirname, '../../src/webrtc.js'), 'utf8'));
const rtc = JfcNetRtc;
const settings = {peers: 4, messageBytes: 262144, queuedMessages: 8, queuedBytes: 1048576,
    bufferedBytes: 1048576, signalBytes: 131072, timeout: 100, relayOnly: false};
const text = aEvent => new TextDecoder().decode(aEvent.data);
const tick = async () => { for (let i = 0; i < 20; ++i) await Promise.resolve(); };
const make = aSettings => rtc.create({...settings, ...aSettings});
const next = (aHost, aType) => { const event = rtc.take(aHost, false); assert.equal(event?.type, aType); return event; };
const frame = (aSequence, aLength, aOffset, aBytes) => {
    const result = new Uint8Array(16 + aBytes.length);
    const header = new DataView(result.buffer);
    header.setUint32(0, 0x3143464a, true);
    header.setUint32(4, aSequence, true);
    header.setUint32(8, aLength, true);
    header.setUint32(12, aOffset, true);
    result.set(aBytes, 16);
    return result.buffer;
};
async function pair(aSettings = {}) {
    const a = make(aSettings), b = make(aSettings);
    const x = rtc.offer(a);
    await tick();
    const offer = rtc.take(a, true);
    assert.equal(JSON.parse(text(offer)).type, 'offer');
    const y = rtc.accept(b, text(offer));
    assert(y);
    await tick();
    assert.equal(rtc.answer(a, x, text(rtc.take(b, true))), 1);
    await tick();
    return {a, b, x, y, p: rtc.hosts.get(a).peers.get(x), q: rtc.hosts.get(b).peers.get(y)};
}
async function test(aName, aBody) {
    try { await aBody(); console.log('PASS ' + aName); }
    finally {
        for (const id of [...rtc.hosts.keys()]) rtc.destroy(id);
        JfcRtcFixture.hold = JfcRtcFixture.failOffer = JfcRtcFixture.failRemote = false;
        JfcRtcFixture.packets = [];
        JfcRtcFixture.maxMessageSize = 65536;
        await tick();
        assert.equal(rtc.hosts.size, 0);
    }
}
(async () => {
    await test('complete SDP exchange, channel settings, connected before received and send gating', async () => {
        const {a, b, x, p} = await pair();
        assert.equal(p.channels[0].settings.ordered, true);
        assert.equal(p.channels[0].settings.maxRetransmits, undefined);
        assert.equal(p.channels[1].settings.ordered, false);
        assert.equal(p.channels[1].settings.maxRetransmits, 0);
        rtc.send(a, x, 0, new Uint8Array([9])); // before connected is consumed: dropped
        next(a, 0);
        rtc.send(a, x, 0, new Uint8Array([0, 128, 255]));
        await tick();
        next(b, 0);
        assert.deepEqual([...next(b, 1).data], [0, 128, 255]);
        assert.equal(rtc.take(b, false), null);
        assert.equal(rtc.answer(a, x, '{"version":1,"type":"answer","sdp":"x"}'), 0);
    });
    await test('only both open channels make a connection', async () => {
        const a = make(); const x = rtc.offer(a);
        const p = rtc.hosts.get(a).peers.get(x);
        p.channels[0].readyState = 'open'; p.channels[0].onopen();
        assert.equal(rtc.peek(a, false), undefined);
        JfcRtcFixture.ready(p.pc);
        assert.equal(rtc.peek(a, false), undefined);
        p.channels[1].readyState = 'open'; p.channels[1].onopen();
        next(a, 0); p.channels[1].onopen();
        assert.equal(rtc.peek(a, false), undefined);
    });
    await test('64 KiB binary messages fragment at the negotiated SCTP limit and reliable order holds', async () => {
        JfcRtcFixture.maxMessageSize = 1024;
        const {a, b, x} = await pair(); next(a, 0); next(b, 0);
        const bytes = Uint8Array.from({length: 65536}, (aValue, aIndex) => aIndex % 256);
        rtc.send(a, x, 0, bytes);
        rtc.send(a, x, 0, new Uint8Array(0));
        rtc.send(a, x, 0, new Uint8Array([5]));
        await tick();
        assert.deepEqual(next(b, 1).data, bytes);
        assert.equal(next(b, 1).data.length, 0);
        assert.deepEqual([...next(b, 1).data], [5]);
    });
    await test('unreliable fragments reassemble out of order; stale, duplicate and abandoned messages drop', async () => {
        const {b, q} = await pair(); next(b, 0);
        rtc.receive(rtc.hosts.get(b), q, 1, frame(0, 3, 2, [3]));
        rtc.receive(rtc.hosts.get(b), q, 1, frame(0, 3, 2, [3]));
        rtc.receive(rtc.hosts.get(b), q, 1, frame(0, 3, 0, [1, 2]));
        assert.deepEqual([...next(b, 1).data], [1, 2, 3]);
        rtc.receive(rtc.hosts.get(b), q, 1, frame(1, 2, 0, [4]));
        rtc.receive(rtc.hosts.get(b), q, 1, frame(2, 1, 0, [6]));
        rtc.receive(rtc.hosts.get(b), q, 1, frame(1, 2, 1, [5]));
        rtc.receive(rtc.hosts.get(b), q, 1, frame(2, 1, 0, [6]));
        assert.deepEqual([...next(b, 1).data], [6]);
        assert.equal(rtc.peek(b, false), undefined);
    });
    await test('large unreliable sends reassemble after all fragments arrive in reverse order', async () => {
        const {a, b, x} = await pair(); next(a, 0); next(b, 0);
        const bytes = Uint8Array.from({length: 65536}, (aValue, aIndex) => aIndex % 251);
        JfcRtcFixture.hold = true;
        rtc.send(a, x, 1, bytes);
        assert(JfcRtcFixture.packets.length > 1);
        for (const packet of JfcRtcFixture.packets.reverse()) JfcRtcFixture.deliver(packet);
        JfcRtcFixture.packets = [];
        assert.deepEqual(next(b, 1).data, bytes);
        assert.equal(rtc.peek(b, false), undefined);
    });
    await test('sequence comparison wraps without delivering old replaceable data', async () => {
        const {b, q} = await pair(); next(b, 0);
        q.received[1] = 0xfffffffe;
        for (const sequence of [0xffffffff, 0, 0xffffffff, 1])
            rtc.receive(rtc.hosts.get(b), q, 1, frame(sequence, 0, 0, []));
        next(b, 1); next(b, 1); next(b, 1);
        assert.equal(rtc.peek(b, false), undefined);
    });
    await test('backpressure rejects reliable messages atomically and drops unreliable messages', async () => {
        const {a, x, p} = await pair(); next(a, 0);
        p.channels[0].bufferedAmount = settings.bufferedBytes - 20000;
        JfcRtcFixture.hold = true;
        assert.equal(rtc.send(a, x, 0, new Uint8Array(65536)), 1);
        assert.equal(rtc.send(a, x, 1, new Uint8Array(65536)), 0);
        assert.equal(JfcRtcFixture.packets.length, 0);
        assert.equal(p.sent[0], 0);
    });
    await test('reliable receive overflow terminates after earlier queued messages; unreliable overflow drops', async () => {
        const {a, b, x, y, q} = await pair({queuedMessages: 1}); next(a, 0); next(b, 0);
        rtc.send(a, x, 1, new Uint8Array([1]));
        rtc.send(a, x, 1, new Uint8Array([2]));
        await tick();
        assert.equal(rtc.hosts.get(b).queuedMessages, 1);
        assert.equal(q.terminal, false);
        rtc.send(a, x, 0, new Uint8Array([3]));
        await tick();
        assert.equal(q.reason, 5);
        assert.deepEqual([...next(b, 1).data], [1]);
        assert.equal(next(b, 2).peer, y);
        assert.equal(rtc.peek(b, false), undefined);
    });
    await test('byte quota also bounds received messages', async () => {
        const {b, q} = await pair({queuedBytes: 2}); next(b, 0);
        rtc.receive(rtc.hosts.get(b), q, 0, frame(0, 3, 0, [1, 2, 3]));
        assert.equal(q.reason, 5); next(b, 2);
    });
    await test('malformed frames terminate exactly once and never emit post-disconnect events', async () => {
        for (const packet of [new ArrayBuffer(0), 'text', frame(0, 262145, 0, [1]),
            frame(0, 1, 2, [1]), frame(0, 1, 0, []), frame(0, 1, 1, [1])]) {
            const {b, q} = await pair(); next(b, 0);
            const callback = q.channels[0].onmessage;
            callback({data: packet});
            assert.equal(q.reason, 4);
            next(b, 2); callback({data: frame(1, 1, 0, [7])});
            assert.equal(rtc.peek(b, false), undefined);
        }
    });
    await test('overlapping or inconsistent fragments are rejected', async () => {
        for (const packet of [frame(0, 3, 1, [1, 2]), frame(0, 4, 2, [3, 4])]) {
            const {b, q} = await pair(); next(b, 0);
            rtc.receive(rtc.hosts.get(b), q, 1, frame(0, 3, 0, [1, 2]));
            rtc.receive(rtc.hosts.get(b), q, 1, packet);
            assert.equal(q.reason, 4); next(b, 2);
        }
    });
    await test('disconnect drains accepted reliable sends and is terminal on both ends', async () => {
        const {a, b, x, p} = await pair(); next(a, 0); next(b, 0);
        JfcRtcFixture.hold = true;
        rtc.send(a, x, 0, new Uint8Array([9]));
        rtc.disconnect(a, x); next(a, 2);
        assert.equal(p.ended, false);
        rtc.send(a, x, 0, new Uint8Array([10]));
        await tick();
        assert.deepEqual([...next(b, 1).data], [9]); next(b, 2);
        assert.equal(rtc.peek(a, false), undefined);
        assert.equal(rtc.peek(b, false), undefined);
        assert.equal(rtc.hosts.get(a).peers.size, 0);
    });
    await test('peer limits include unconsumed terminal events and IDs are never reused', async () => {
        const a = make({peers: 1}); const x = rtc.offer(a);
        assert.equal(rtc.offer(a), 0);
        rtc.disconnect(a, x);
        assert.equal(rtc.offer(a), 0); next(a, 2);
        assert.equal(rtc.offer(a), 2);
    });
    await test('caller time bounds stalled negotiations and discards late promise results', async () => {
        const a = make(); const x = rtc.offer(a); const p = rtc.hosts.get(a).peers.get(x);
        rtc.update(a, 99); assert.equal(rtc.peek(a, false), undefined);
        rtc.update(a, 1); assert.equal(p.reason, 6); next(a, 2);
        await tick(); assert.equal(rtc.peek(a, true), null);
    });
    await test('async negotiation rejection becomes a disconnected event with a reason', async () => {
        JfcRtcFixture.failOffer = true;
        const a = make(); const x = rtc.offer(a); await tick();
        assert.equal(rtc.hosts.get(a).peers.get(x).reason, 2); next(a, 2);
    });
    await test('asynchronous remote SDP rejection and browser send failure terminate peers', async () => {
        const a = make(), b = make(); rtc.offer(a); await tick();
        JfcRtcFixture.failRemote = true;
        const y = rtc.accept(b, text(rtc.take(a, true))); await tick();
        assert.equal(rtc.hosts.get(b).peers.get(y).reason, 2); next(b, 2);
        JfcRtcFixture.failRemote = false;
        const pairValue = await pair(); next(pairValue.a, 0);
        pairValue.p.channels[0].send = () => { throw Error('platform buffer rejected send'); };
        assert.equal(rtc.send(pairValue.a, pairValue.x, 0, new Uint8Array([1])), 2);
        assert.equal(pairValue.p.reason, 3); next(pairValue.a, 2);
    });
    await test('unreliable data can overtake readiness without disconnecting or arriving early', async () => {
        const a = make(); const x = rtc.offer(a); const p = rtc.hosts.get(a).peers.get(x);
        JfcRtcFixture.open(p.pc);
        rtc.receive(rtc.hosts.get(a), p, 1, frame(0, 1, 0, [1]));
        assert.equal(rtc.peek(a, false), undefined);
        JfcRtcFixture.ready(p.pc); next(a, 0);
        rtc.receive(rtc.hosts.get(a), p, 1, frame(1, 1, 0, [2]));
        assert.deepEqual([...next(a, 1).data], [2]);
    });
    await test('a closing connection has a caller-controlled drain deadline', async () => {
        const {a, x, p} = await pair(); next(a, 0);
        for (const channel of p.channels) channel.close = () => {};
        rtc.disconnect(a, x); next(a, 2);
        assert.equal(rtc.hosts.get(a).peers.size, 1);
        rtc.update(a, 100);
        assert.equal(rtc.hosts.get(a).peers.size, 0);
        assert.equal(rtc.peek(a, false), undefined);
    });
    await test('invalid, oversized and wrong-version signals never allocate peers', async () => {
        const a = make();
        for (const signal of ['{}', 'null', 'x'.repeat(131073),
            '{"version":2,"type":"offer","sdp":"x"}', '{"version":1,"type":"answer","sdp":"x"}'])
            assert.equal(rtc.accept(a, signal), 0);
        assert.equal(rtc.hosts.get(a).peers.size, 0);
    });
    await test('late callbacks and promises cannot revive a destroyed host', async () => {
        const a = make(); const x = rtc.offer(a); const p = rtc.hosts.get(a).peers.get(x);
        const callback = p.channels[0].onopen;
        const ice = p.pc.onicegatheringstatechange;
        rtc.destroy(a); callback(); ice(); await tick();
        assert.equal(p.ended, true);
    });
    await test('seeded malformed signaling and wire inputs stay within configured receive bounds', async () => {
        let seed = 0x1234abcd;
        const random = () => {
            seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return seed >>> 0;
        };
        for (let i = 0; i < 512; ++i) {
            const a = make({messageBytes: 1024});
            const junk = Uint8Array.from({length: 16 + random() % 64}, () => random() % 256);
            assert.equal(rtc.accept(a, String.fromCharCode(...junk)), 0);
            const x = rtc.offer(a); const p = rtc.hosts.get(a).peers.get(x);
            JfcRtcFixture.open(p.pc); JfcRtcFixture.ready(p.pc); next(a, 0);
            if (i % 2) {
                const view = new DataView(junk.buffer);
                view.setUint32(0, 0x3143464a, true);
                view.setUint32(8, random() % 2048, true);
                view.setUint32(12, random() % 2048, true);
            }
            rtc.receive(rtc.hosts.get(a), p, i % 2, junk.buffer);
            for (const assembly of p.reassembly) if (assembly) assert(assembly.bytes.length <= 1024);
            assert(rtc.hosts.get(a).queuedMessages <= settings.queuedMessages);
            assert(rtc.hosts.get(a).queuedBytes <= settings.queuedBytes);
            rtc.destroy(a);
        }
    });
    console.log('WebRTC transport contract passed (test platform double).');
})().catch(aError => { console.error(aError); process.exitCode = 1; });
