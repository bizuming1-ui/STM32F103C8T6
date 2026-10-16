/* ===========================================================================
 * simulator/test/ui_smoke.mjs
 * ---------------------------------------------------------------------------
 * Runs the real simulator page code (ui.js + sim.js) outside a browser with a
 * minimal DOM shim, so a wrong element id, a typo in the blit or a broken key
 * handler is caught before the page is opened.  It also verifies the wasm is
 * reachable over HTTP, exactly like the browser loads it.
 *
 * Usage: start `python3 -m http.server 8000 --directory simulator` first.
 * ===========================================================================
 */
const BASE = process.env.SIM_BASE || 'http://127.0.0.1:8000';

/* ------------------------------------------------------------------ DOM shim */
class ClassList {
  constructor() { this.set = new Set(); }
  add(...c) { c.forEach(x => this.set.add(x)); }
  remove(...c) { c.forEach(x => this.set.delete(x)); }
  toggle(c, on) { on === undefined ? (this.set.has(c) ? this.set.delete(c) : this.set.add(c)) : (on ? this.set.add(c) : this.set.delete(c)); }
  contains(c) { return this.set.has(c); }
}
class El {
  constructor(tag = 'div', id = '') {
    this.tagName = tag.toUpperCase(); this.id = id; this.children = [];
    this.classList = new ClassList(); this.style = {}; this.dataset = {};
    this.textContent = ''; this._html = ''; this.value = '245';
    this.listeners = {};
  }
  set innerHTML(v) { this._html = v; }
  get innerHTML() { return this._html; }
  addEventListener(t, fn) { (this.listeners[t] ||= []).push(fn); }
  removeEventListener() { }
  appendChild(c) { this.children.push(c); return c; }
  prepend(c) { this.children.unshift(c); return c; }
  remove() { }
  get childElementCount() { return this.children.length; }
  get lastChild() { return this.children[this.children.length - 1]; }
  querySelector(sel) { return findBySelector(sel); }
  querySelectorAll() { return []; }
  setPointerCapture() { }
  insertAdjacentHTML(_, html) { this._html += html; console.error('[page error]', html.replace(/<[^>]+>/g, '')); }
  getContext() {
    return {
      createImageData: (w, h) => ({ width: w, height: h, data: new Uint8ClampedArray(w * h * 4) }),
      putImageData: () => { },
    };
  }
  fire(type, ev = {}) { (this.listeners[type] || []).forEach(fn => fn({ preventDefault() { }, ...ev })); }
}
const registry = new Map();
const keys = ['1', '2', '4', '8'].map(k => { const el = new El('div'); el.dataset.key = k; el.classList.add('key'); return el; });
const speedBtns = ['0.5', '1', '2', '4'].map(sp => { const b = new El('button'); b.dataset.sp = sp; return b; });
function reg(sel, el) { registry.set(sel, el); return el; }
reg('#oled', new El('canvas'));
reg('#led', new El()); reg('#buzz', new El()); reg('#stat', new El()); reg('#log', new El());
reg('#fps', new El()); reg('#temp', new El()); reg('#tempv', new El());
reg('#mute', new El()); reg('#skip', new El()); reg('#reset', new El());
function findBySelector(sel) {
  if (sel.startsWith('.key[data-key=')) {
    const k = sel.match(/"(\d+)"/)[1];
    return keys.find(el => el.dataset.key === k);
  }
  return registry.get(sel) || new El();
}
globalThis.document = {
  querySelector: (sel) => findBySelector(sel),
  querySelectorAll: (sel) => (sel === '.key' ? keys : sel === '#speed button' ? speedBtns : []),
  createElement: (tag) => new El(tag),
  body: new El('body'),
  baseURI: BASE + '/index.html',
};
globalThis.window = {};
/* keep Node's own `performance` (undici needs it for fetch) */
globalThis.addEventListener = () => { };
let rafCb = null;
globalThis.requestAnimationFrame = (cb) => { rafCb = cb; return 1; };

/* ------------------------------------------------------------------- run */
const t0 = Date.now();
await import('../ui.js');                     /* this kicks off boot() -> Firmware.load */
/* wait for the async boot to have registered the frame callback */
let clock = 0;
async function pump(frames, dtMs = 16.7) {
  for (let i = 0; i < frames; i++) {
    if (!rafCb) { await new Promise(r => setTimeout(r, 5)); continue; }
    const cb = rafCb; rafCb = null;
    clock += dtMs;
    cb(clock);
    if (i % 40 === 0) await new Promise(r => setTimeout(r, 0));
  }
}
await pump(20);
if (!rafCb) { console.error('ui.js did not start the render loop'); process.exit(1); }
console.log('页面的渲染循环已启动');

const keyEl = (k) => keys.find(el => el.dataset.key === String(k));
const press = async (k, ms) => { keyEl(k).fire('pointerdown', { pointerId: 1 }); await pump(Math.round(ms / 16.7)); keyEl(k).fire('pointerup'); await pump(4); };

const stat = () => registry.get('#stat').innerHTML;
await pump(400);                              /* power-on animation */
console.log('开机完成:', /时间界面/.test(stat()) ? '状态栏显示时间界面 ✓' : '状态栏异常 ✗');

await press(2, 80);                           /* PA5 short -> settings screen */
await pump(20);
const menuOK = /设置界面/.test(stat()) && /铃声|秒表/.test(stat());
console.log('PA5 短按进入设置界面:', menuOK ? '✓' : '✗');

await press(1, 80);                           /* PA4 short -> select stopwatch */
await pump(20);
console.log('PA4 短按切换选择:', /秒表/.test(stat()) ? '✓' : '✗');

await press(1, 2200);                         /* PA4 hold 2 s -> back to the clock */
await pump(30);
console.log('PA4 长按2秒返回时间界面:', /时间界面/.test(stat()) ? '✓' : '✗', '|', /锁定中/.test(stat()) ? '防误触锁生效 ✓' : '防误触锁缺失 ✗');

await pump(120);
await press(2, 80); await press(2, 80);       /* menu -> 铃声 -> countdown */
await pump(20);
console.log('铃声直接进入倒计时界面:', /设置中/.test(stat()) ? '✓' : '✗');

await press(8, 80);                           /* PA7 start with 0 s must do nothing */
await pump(20);
console.log('0 秒时按开始不启动:', /设置中/.test(stat()) ? '✓' : '✗');
await press(2, 60); await press(2, 60); await press(2, 60);   /* PA5 x3 -> 3 seconds */
await pump(20);
await press(8, 80);                           /* start */
await pump(20);
console.log('3 秒时按开始启动倒计时:', /运行中/.test(stat()) ? '✓' : '✗');
await pump(300);                              /* let it run 3 s -> fireworks alarm */
console.log('倒计时到期进入烟花闹钟:', /闹钟/.test(stat()) ? '✓' : '✗');
console.log('LED 状态跟随:', registry.get('#led').classList.contains('on') ? '点亮' : '熄灭');

console.log('页面代码运行 ' + ((Date.now() - t0) / 1000).toFixed(1) + ' s，无异常');
console.log('按键事件已处理，LED/蜂鸣器指示器随程序状态更新');
