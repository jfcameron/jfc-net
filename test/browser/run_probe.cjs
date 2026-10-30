// © Joseph Cameron - MIT License
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const program = process.argv[2].replace(/\.html$/, '.js');
vm.runInThisContext(fs.readFileSync(path.join(__dirname, 'fixture.js'), 'utf8'));
let passed = false;
global.require = require;
global.__dirname = path.dirname(program);
global.__filename = program;
global.module = {exports: {}};
global.requestAnimationFrame = aCallback => setTimeout(() => aCallback(performance.now()), 1);
global.cancelAnimationFrame = clearTimeout;
global.Module = {
    arguments: [],
    print: aText => { console.log(aText); if (aText.includes('JFC_NET_WEBRTC_PROBE_PASS')) passed = true; },
    printErr: console.error,
    onExit: aCode => { if (!passed || aCode) process.exitCode = 1; }
};
process.on('exit', () => { if (!passed) process.exitCode = 1; });
setTimeout(() => { console.error('probe watchdog expired'); process.exit(1); }, 45000).unref();
vm.runInThisContext(fs.readFileSync(program, 'utf8'), {filename: program});
