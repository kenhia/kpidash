const fs = require('fs');
const html = fs.readFileSync(process.argv[2], 'utf8'); // the mock-up
// Loads the mock-up's script with a stub DOM and exposes its layout() and
// emulator as module.exports. process.argv[3] picks the program.
let src = html.slice(html.indexOf('<script>') + 8, html.lastIndexOf('</script>'));
src = src.replace('function layout(n) {', 'globalThis.T = { layout, reset, cycle, get m() { return m; }, get acc() { return acc; }, get total() { return total; }, get mem() { return mem; } };\n  function layout(n) {');
const vals = { n: '12', scheme: 'func', pal: 'led', prog: process.argv[3] || 'walk', speed: '0.037', rate: '2', levels: '16', maxb: '100', scale: '0.5' };
const ctx = new Proxy({}, { get: (t, k) => (k in t ? t[k] : () => ({})), set: (t, k, v) => (t[k] = v, true) });
const el = (id) => ({ value: vals[id], add() {}, addEventListener() {}, style: {}, getContext: () => ctx, innerHTML: '' });
globalThis.document = { getElementById: el };
globalThis.location = { search: '' };
globalThis.requestAnimationFrame = () => {};
globalThis.Option = function () {};
eval(src);
module.exports = T;
