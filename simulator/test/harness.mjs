/* ===========================================================================
 * simulator/test/harness.mjs
 * ---------------------------------------------------------------------------
 * Headless driver for the wasm build of the firmware.  It plays the part of
 * the STM32F103 hardware:
 *
 *   - TIM2->CNT is written from a virtual clock (PSC=7199 @72MHz => 10 ticks
 *     per millisecond, exactly like the board);
 *   - PA4..PA7 levels are written into GPIOA->IDR;
 *   - the LED / buzzer pin levels and the OLED_GRAM framebuffer are read back.
 *
 * Because the firmware is the very same C code Keil compiles, every check made
 * here is a check on the shipped program.
 * ===========================================================================
 */
import fs from 'node:fs';
import path from 'node:path';
import zlib from 'node:zlib';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '..', '..');
export const SIM_DIR = path.resolve(HERE, '..');

export const KEY = { PA4: 1, PA5: 2, PA6: 4, PA7: 8 };

export async function loadFirmware(wasmPath = path.join(SIM_DIR, 'fw_sim.wasm')) {
  const bytes = fs.readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(bytes, {});
  return new Fw(instance);
}

export class Fw {
  constructor(instance) {
    this.instance = instance;
    this.e = instance.exports;
    this.mem = new Uint8Array(this.e.memory.buffer);
    this.regBase = this.e.sim_reg_base() >>> 0;
    this.gramPtr = this.e.sim_gram_ptr() >>> 0;
    this.regs = new Uint32Array(this.e.memory.buffer, this.regBase, 512);
    this.gram = new Uint8Array(this.e.memory.buffer, this.gramPtr, 1024);
    this.elapsedMs = 0;
    this.frames = 0;
    this.keys = 0;
    this.trace = [];
    this.e.sim_set_keys(0);
    this.e.sim_set_sensor(245);
    this.#sync();
  }
  /* ---- low level -------------------------------------------------------- */
  #sync() {
    this.e.sim_set_tim2_ticks(Math.floor(this.elapsedMs * 10) & 0xffff);
    this.e.sim_set_keys(this.keys);
  }
  reg(off) { return this.regs[off >> 2]; }
  /* GPIOA ODR bit 8 = buzzer, GPIOC ODR bit 13 = LED (mirrors are authoritative) */
  get led() { return this.e.sim_get_led() & 1; }
  get buzzer() { return this.e.sim_get_buzzer() & 1; }
  get keyIdr() { return (this.reg(64 + 8) >>> 4) & 0xf; }
  get tim2() { return this.reg(256 + 0x24) & 0xffff; }
  get timer() {
    const v = this.e.sim_timer_state() >>> 0;
    return { h: v & 0xff, m: (v >> 8) & 0xff, s: (v >> 16) & 0xff, selected: (v >> 24) & 0xff };
  }
  /* Wait until a pixel lights up (used to sample steady phases of a blink). */
  waitPixel(x, y, timeoutMs = 1200) {
    const target = this.elapsedMs + timeoutMs;
    while (this.elapsedMs < target) { this.step(16.6); if (this.pixel(x, y)) return true; }
    return this.pixel(x, y) === 1;
  }
  /* decoded state machine (see main.c Sim_AppState) */
  get state() {
    const v = this.e.sim_app_state() >>> 0;
    return {
      raw: v,
      screen: v & 0xf, timerMode: (v >> 4) & 0xf, view: (v >> 8) & 1,
      menuSel: (v >> 9) & 1, pa4Block: (v >> 10) & 1, transOn: (v >> 11) & 1,
      bootDone: (v >> 12) & 1, timerSelected: (v >> 13) & 3, pa5Block: (v >> 15) & 1,
    };
  }
  get oled() { return this.oledOn; }
  pixel(x, y) { return (this.gram[(y >> 3) * 128 + x] >> (y & 7)) & 1; }
  /* ---- time ------------------------------------------------------------- */
  /* Run one hardware loop iteration with the clock advanced by dtMs. */
  step(dtMs) {
    this.elapsedMs += dtMs;
    this.#sync();
    this.e.fw_step();
    if (this.mem.byteLength !== this.e.memory.buffer.byteLength) { this.#rebind(); }
    this.frames++;
    return this;
  }
  /* Advance by `ms` of virtual time at ~60 fps, running `iters` iterations. */
  advance(ms, frameMs = 16.6) {
    const target = this.elapsedMs + ms;
    while (this.elapsedMs < target) { this.step(Math.min(frameMs, target - this.elapsedMs)); }
    return this;
  }
  #rebind() {
    this.mem = new Uint8Array(this.e.memory.buffer);
    this.regs = new Uint32Array(this.e.memory.buffer, this.regBase, 512);
    this.gram = new Uint8Array(this.e.memory.buffer, this.gramPtr, 1024);
  }
  /* ---- keys ------------------------------------------------------------- */
  setKeys(mask) { this.keys = mask & 0xf; this.#sync(); return this; }
  /* Press and release with the given hold time (debounce is 20 ms). */
  tap(mask, holdMs = 60, gapMs = 60) {
    this.setKeys(mask);
    this.advance(holdMs);
    this.setKeys(0);
    this.advance(gapMs);
    return this;
  }
  /* Hold a key for `holdMs`, then release. */
  hold(mask, holdMs, gapMs = 80) { return this.tap(mask, holdMs, gapMs); }
  /* ---- sensor ----------------------------------------------------------- */
  setSensor(tenths) { this.e.sim_set_sensor(tenths); return this; }
  /* ---- inspection ------------------------------------------------------- */
  fingerprint() {
    let h = 2166136261 >>> 0;
    for (let i = 0; i < 1024; i++) { h = ((h ^ this.gram[i]) * 16777619) >>> 0; }
    return h.toString(16).padStart(8, '0');
  }
  nonBlankPixels() {
    let n = 0;
    for (let i = 0; i < 1024; i++) { n += popcount(this.gram[i]); }
    return n;
  }
  /* ASCII art of the framebuffer, for logs */
  ascii(x0 = 0, y0 = 0, w = 128, h = 64, stepX = 1, stepY = 1) {
    let out = '';
    for (let y = y0; y < y0 + h; y += stepY) {
      let line = '';
      for (let x = x0; x < x0 + w; x += stepX) { line += this.pixel(x, y) ? '#' : '.'; }
      out += line + '\n';
    }
    return out;
  }
}

function popcount(v) { let n = 0; while (v) { n += v & 1; v >>= 1; } return n; }

/* --------------------------------------------------------------- PNG output */
export function writePNG(file, fw, { scale = 4, x0 = 0, y0 = 0, w = 128, h = 64, on = [255, 255, 255], off = [0, 0, 0] } = {}) {
  const width = w * scale, height = h * scale;
  const raw = Buffer.alloc((width * 3 + 1) * height);
  let p = 0;
  for (let y = 0; y < height; y++) {
    raw[p++] = 0;
    for (let x = 0; x < width; x++) {
      const lit = fw.pixel(x0 + Math.floor(x / scale), y0 + Math.floor(y / scale));
      const c = lit ? on : off;
      raw[p++] = c[0]; raw[p++] = c[1]; raw[p++] = c[2];
    }
  }
  const chunks = [Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])];
  const chunk = (type, data) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
    const td = Buffer.concat([Buffer.from(type, 'latin1'), data]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td) >>> 0);
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0); ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  chunks.push(chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0)));
  fs.writeFileSync(file, Buffer.concat(chunks));
  return file;
}

let CRC_TABLE = null;
function crc32(buf) {
  if (!CRC_TABLE) {
    CRC_TABLE = new Int32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) { c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1; }
      CRC_TABLE[n] = c;
    }
  }
  let c = 0xffffffff;
  for (let i = 0; i < buf.length; i++) { c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8); }
  return c ^ 0xffffffff;
}

/* --------------------------------------------------------------- assertions */
export const results = { pass: 0, fail: 0, failures: [] };
export function check(name, cond, extra = '') {
  if (cond) { results.pass++; console.log(`  \u2713 ${name}`); }
  else { results.fail++; results.failures.push(name); console.log(`  \u2717 ${name} ${extra}`); }
  return cond;
}
export function section(title) { console.log(`\n=== ${title} ===`); }
export function summary() {
  console.log(`\n---- ${results.pass} passed, ${results.fail} failed ----`);
  if (results.fail) { console.log('failed: ' + results.failures.join(', ')); process.exitCode = 1; }
  return results.fail === 0;
}
