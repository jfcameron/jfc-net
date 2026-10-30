// © Joseph Cameron - MIT License
// Opt-in integration test: actual WebRTC in separate browser contexts, plus the
// compiled C++ probe. Requires Playwright and its Chromium/Firefox installations.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const playwright = require('playwright');
const probe = process.argv[2];
const settings = {peers: 4, messageBytes: 262144, queuedMessages: 256,
    queuedBytes: 4194304, bufferedBytes: 1048576, signalBytes: 131072, timeout: 30000};
const bridge = fs.readFileSync(path.join(__dirname, '../../src/webrtc.js'));
const server = http.createServer((aRequest, aResponse) => {
    const name = new URL(aRequest.url, 'http://localhost').pathname;
    if (name === '/bridge.js') {
        aResponse.setHeader('Content-Type', 'text/javascript'); aResponse.end(bridge);
    } else if (name === '/') {
        aResponse.setHeader('Content-Type', 'text/html');
        aResponse.end('<script>function addToLibrary(aLibrary){window.rtc=aLibrary.$JfcNetRtc;}</script><script src="/bridge.js"></script>');
    } else if (probe && /^\/probe\/[^/]+$/.test(name)) {
        const file = path.join(path.dirname(probe), path.basename(name));
        if (!fs.existsSync(file)) { aResponse.writeHead(404); aResponse.end(); return; }
        const extension = path.extname(file);
        aResponse.setHeader('Content-Type', extension === '.wasm' ? 'application/wasm' :
            extension === '.html' ? 'text/html' : 'text/javascript');
        aResponse.end(fs.readFileSync(file));
    } else { aResponse.writeHead(404); aResponse.end(); }
});

async function connection(aBrowser, aBase, aRelay) {
    const left = await aBrowser.newContext(), right = await aBrowser.newContext();
    try {
        const a = await left.newPage(), b = await right.newPage();
        await Promise.all([a.goto(aBase), b.goto(aBase)]);
        for (const page of [a, b]) await page.evaluate(aConfig => {
            window.id = rtc.create(aConfig.settings);
            rtc.hosts.get(id).iceServers = aConfig.iceServers;
        }, {settings: {...settings, relayOnly: aRelay}, iceServers: aRelay ? [{
            urls: [process.env.JFC_NET_TURN_URL], username: process.env.JFC_NET_TURN_USERNAME || '',
            credential: process.env.JFC_NET_TURN_CREDENTIAL || ''}] : []});
        await a.evaluate(() => { window.peer = rtc.offer(id); });
        await a.waitForFunction(() => !!rtc.peek(id, true), null, {timeout: 35000});
        const offer = await a.evaluate(() => new TextDecoder().decode(rtc.take(id, true).data));
        await b.evaluate(aOffer => { window.peer = rtc.accept(id, aOffer); }, offer);
        await b.waitForFunction(() => !!rtc.peek(id, true), null, {timeout: 35000});
        const answer = await b.evaluate(() => new TextDecoder().decode(rtc.take(id, true).data));
        assert.equal(await a.evaluate(aAnswer => rtc.answer(id, peer, aAnswer), answer), 1);
        for (const page of [a, b]) {
            await page.waitForFunction(() => !!rtc.peek(id, false), null, {timeout: 35000});
            assert.equal(await page.evaluate(() => rtc.take(id, false).type), 0);
        }
        const candidates = await a.evaluate(async () => {
            const stats = await rtc.hosts.get(id).peers.get(peer).pc.getStats();
            for (const value of stats.values()) if (value.type === 'transport' && value.selectedCandidatePairId) {
                const pair = stats.get(value.selectedCandidatePairId);
                return [stats.get(pair.localCandidateId).candidateType, stats.get(pair.remoteCandidateId).candidateType];
            }
            return [];
        });
        if (aRelay) assert(candidates.includes('relay'), 'forced relay must select a TURN candidate');
        console.log('Connected separate browser contexts, candidate types: ' + candidates.join('/'));
        assert.equal(await a.evaluate(() => {
            const bytes = Uint8Array.from({length: 65536}, (aValue, aIndex) => aIndex % 256);
            rtc.send(id, peer, 0, bytes);
            rtc.send(id, peer, 0, new Uint8Array(0));
            for (let i = 0; i < 12; ++i) rtc.send(id, peer, 1, new Uint8Array([i]));
            return 0;
        }), 0);
        await b.waitForFunction(() => rtc.hosts.get(id).events.filter(aEvent => aEvent.type === 1 && aEvent.channel === 0).length === 2);
        const received = await b.evaluate(() => {
            const messages = [];
            for (let event; (event = rtc.take(id, false));) messages.push({channel: event.channel, data: [...event.data]});
            return messages;
        });
        const reliable = received.filter(aEvent => aEvent.channel === 0);
        assert.equal(reliable[0].data.length, 65536);
        assert(reliable[0].data.every((aValue, aIndex) => aValue === aIndex % 256));
        assert.equal(reliable[1].data.length, 0);
        const unreliable = received.filter(aEvent => aEvent.channel === 1);
        for (let i = 1; i < unreliable.length; ++i) assert(unreliable[i].data[0] > unreliable[i - 1].data[0]);
        // Send another large reliable message and close immediately. The other
        // endpoint must receive the complete message before its terminal event.
        await a.evaluate(() => {
            rtc.send(id, peer, 0, new Uint8Array(65536).fill(77)); rtc.disconnect(id, peer);
        });
        await b.waitForFunction(() => rtc.hosts.get(id).events.some(aEvent => aEvent.type === 2));
        const terminal = await b.evaluate(() => {
            const messages = []; for (let event; (event = rtc.take(id, false));)
                messages.push({type: event.type, channel: event.channel, size: event.data.length, valid: event.data.every(aByte => aByte === 77)});
            return messages;
        });
        const last = terminal.filter(aEvent => aEvent.type === 1 && aEvent.channel === 0);
        assert.deepEqual(last.map(aEvent => [aEvent.size, aEvent.valid]), [[65536, true]]);
        assert.equal(terminal.at(-1).type, 2);
        console.log('PASS real WebRTC: binary fragmentation, ordering, sequencing and graceful disconnect');
    } finally { await left.close(); await right.close(); }
}

(async () => {
    await new Promise(aResolve => server.listen(0, '127.0.0.1', aResolve));
    const base = 'http://127.0.0.1:' + server.address().port;
    for (const name of (process.env.JFC_NET_BROWSERS || 'chromium,firefox').split(',')) {
        const browser = await playwright[name].launch({headless: true});
        try {
            console.log('Testing ' + name);
            await connection(browser, base, false);
            if (process.env.JFC_NET_TURN_URL) await connection(browser, base, true);
            if (probe) {
                const page = await browser.newPage();
                const errors = [];
                page.on('pageerror', aError => errors.push(aError.message));
                await page.goto(base + '/probe/' + path.basename(probe));
                await page.waitForFunction(() => document.getElementById('output')?.value.includes('JFC_NET_WEBRTC_PROBE_PASS'), null, {timeout: 40000});
                assert.deepEqual(errors, []);
                console.log('PASS real browser C++/Wasm probe');
                await page.close();
            }
        } finally { await browser.close(); }
    }
})().catch(aError => { console.error(aError); process.exitCode = 1; }).finally(() => server.close());
