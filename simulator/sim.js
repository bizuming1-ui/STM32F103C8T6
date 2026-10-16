/* ===========================================================================
 * simulator/sim.js
 * ---------------------------------------------------------------------------
 * Hardware model + wasm bridge for the browser simulator.
 *
 * The page does not re-implement the firmware: it loads fw_sim.wasm, which is
 * the project's own C files (main.c, OLED.c, Key.c, LED.c, Scene.c, ...)
 * compiled for wasm32.  This file only plays the part of the STM32F103C8T6:
 *
 *   TIM2->CNT  <- virtual clock, 10 ticks per millisecond (PSC 7199 @ 72 MHz)
 *   GPIOA IDR  <- PA4..PA7 button levels (1 = pressed, active high)
 *   LED / buzzer pin levels  -> read back for the on-screen indicators
 *   DS18B20    -> temperature slider
 *
 * Register block layout: see simulator/hal/stm32f10x.h (SIM_OFF_*).
 * ===========================================================================
 */

export const KEY = { PA4: 1, PA5: 2, PA6: 4, PA7: 8 };
const OFF = { RCC: 0, GPIOA: 64, GPIOB: 96, GPIOC: 128, TIM1: 160, TIM2: 256 };
const TICKS_PER_MS = 10;          /* TIM2 PSC = 7199 -> 10 kHz */

export class Firmware {
  constructor(instance) {
    this.instance = instance;
    this.e = instance.exports;
    this._bind();
    this.elapsedMs = 0;           /* virtual milliseconds since reset */
    this.keys = 0;
    this.e.sim_set_keys(0);
    this.e.sim_set_sensor(245);
    this.sync();
  }

  static async load(url = 'fw_sim.wasm') {
    /* resolve against the page so the simulator also works when it is served
     * from a sub-directory */
    const href = new URL(url, (typeof document !== 'undefined' && document.baseURI) || 'http://localhost/').href;
    const res = await fetch(href);
    if (!res.ok) throw new Error(`无法加载 ${url} (${res.status})`);
    const bytes = await res.arrayBuffer();
    const { instance } = await WebAssembly.instantiate(bytes, {});
    return new Firmware(instance);
  }

  _bind() {
    this.mem = new Uint8Array(this.e.memory.buffer);
    this.regBase = this.e.sim_reg_base() >>> 0;
    this.gramPtr = this.e.sim_gram_ptr() >>> 0;
    this.regs = new Uint32Array(this.e.memory.buffer, this.regBase, 512);
    this.gram = new Uint8Array(this.e.memory.buffer, this.gramPtr, 1024);
    this.oled = { data: this.gram, width: 128, height: 64 };
  }

  sync() {
    this.e.sim_set_tim2_ticks(Math.floor(this.elapsedMs * TICKS_PER_MS) & 0xffff);
    this.e.sim_set_keys(this.keys);
  }

  /* one iteration of the firmware's while(1) loop, dtMs later */
  step(dtMs) {
    this.elapsedMs += dtMs;
    this.sync();
    this.e.fw_step();
    if (this.e.memory.buffer.byteLength !== this.mem.byteLength) this._bind();
  }

  setKey(mask, down) {
    this.keys = down ? (this.keys | mask) : (this.keys & ~mask);
    this.sync();
  }
  setSensor(tenths) { this.e.sim_set_sensor(tenths); }

  get led() { return this.e.sim_get_led() & 1; }
  get buzzer() { return this.e.sim_get_buzzer() & 1; }
  get keyLevel() { return (this.regs[(OFF.GPIOA + 8) >> 2] >>> 4) & 0xf; }
  pixel(x, y) { return (this.gram[(y >> 3) * 128 + x] >> (y & 7)) & 1; }

  /* decoded firmware state (see main.c / Key.c simulator exports) */
  get state() {
    const v = this.e.sim_app_state() >>> 0;
    const t = this.e.sim_timer_state() >>> 0;
    return {
      screen: ['时间界面', '设置界面', '倒计时界面', '秒表界面'][v & 0xf] ?? '?',
      screenId: v & 0xf,
      timerId: (v >> 4) & 0xf,
      timer: ['空闲', '设置中', '运行中', '闹钟(烟花)', '进度界面'][(v >> 4) & 0xf] ?? '?',
      view: ((v >> 8) & 1) ? '黑洞时间界面' : '流动表盘',
      viewId: (v >> 8) & 1,
      menuSel: ((v >> 9) & 1) ? '秒表' : '铃声',
      lock: (v >> 10) & 1,
      transition: (v >> 11) & 1,
      bootDone: (v >> 12) & 1,
      field: ['时', '分', '秒'][(v >> 13) & 3] ?? '?',
      h: t & 0xff, m: (t >> 8) & 0xff, s: (t >> 16) & 0xff,
    };
  }
}

/* ------------------------------------------------------------------ helpers */
export function decodeRow(gram, y) {
  const page = gram[y >> 3];
  return page;
}
