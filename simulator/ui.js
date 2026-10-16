/* ===========================================================================
 * simulator/ui.js
 * ---------------------------------------------------------------------------
 * Drives the board UI around the firmware:
 *   - 120 Hz fixed-step firmware stepping (smooth, deterministic),
 *   - OLED framebuffer blit,
 *   - PA4..PA7 buttons (mouse / touch / keyboard) with a 2 s hold indicator,
 *   - PC13 LED and buzzer indicators + WebAudio tone for the active buzzer,
 *   - status panel and an event log written from the firmware's own state.
 * ===========================================================================
 */
import { Firmware, KEY } from './sim.js';

const STEP_MS = 1000 / 120;         /* the firmware is stepped at 120 Hz          */
const MAX_ITERS = 24;               /* guard against long tab stalls              */

const $ = (s) => document.querySelector(s);
const cvs = $('#oled');
const ctx = cvs.getContext('2d');
const img = ctx.createImageData(128, 64);
const COLOR_ON = [206, 234, 255], COLOR_OFF = [3, 6, 10];
for (let i = 0; i < 128 * 64; i++) {
  img.data[i * 4 + 0] = COLOR_OFF[0]; img.data[i * 4 + 1] = COLOR_OFF[1];
  img.data[i * 4 + 2] = COLOR_OFF[2]; img.data[i * 4 + 3] = 255;
}

/* ------------------------------------------------------------------ runtime */
let fw = null, speed = 1, pending = 0, lastTs = 0, frames = 0, fpsAt = 0, fps = 0;
const heldKeys = new Set();
const holdStart = new Map();
const els = {
  led: $('#led'), buzz: $('#buzz'), stat: $('#stat'), log: $('#log'), fps: $('#fps'),
};
let muted = false;

function log(msg, cls = '') {
  const t = (fw ? (fw.elapsedMs / 1000).toFixed(2) : '0.00') + 's';
  const line = document.createElement('div');
  line.innerHTML = `<span style="color:#5f7c94">${t}</span> ${cls ? `<b>${cls}</b>` : ''} ${msg}`;
  els.log.prepend(line);
  while (els.log.childElementCount > 120) els.log.lastChild.remove();
}

/* ------------------------------------------------------------------ audio */
let audioCtx = null, osc = null, gain = null, audioOn = false;
function initAudio() {
  if (audioCtx) return;
  const AC = window.AudioContext || window.webkitAudioContext;
  if (!AC) return;
  audioCtx = new AC();
  osc = audioCtx.createOscillator();
  gain = audioCtx.createGain();
  osc.type = 'square';
  osc.frequency.value = 2500;            /* 有源蜂鸣器固定音调 */
  gain.gain.value = 0;
  osc.connect(gain).connect(audioCtx.destination);
  osc.start();
}
function setBuzzer(on) {
  if (on === audioOn) return;
  audioOn = on;
  els.buzz.classList.toggle('on', !!on);
  if (!audioCtx || muted) return;
  const t = audioCtx.currentTime;
  gain.gain.cancelScheduledValues(t);
  gain.gain.linearRampToValueAtTime(on ? 0.045 : 0, t + 0.008);
}

/* ------------------------------------------------------------------ OLED */
function blit() {
  const g = fw.gram;
  const d = img.data;
  for (let p = 0; p < 8; p++) {
    for (let x = 0; x < 128; x++) {
      const bits = g[p * 128 + x];
      const base = ((p * 8) * 128 + x) * 4;
      for (let b = 0; b < 8; b++) {
        const on = (bits >> b) & 1;
        const c = on ? COLOR_ON : COLOR_OFF;
        const o = base + b * 128 * 4;
        d[o] = c[0]; d[o + 1] = c[1]; d[o + 2] = c[2];
      }
    }
  }
  ctx.putImageData(img, 0, 0);
}

/* --------------------------------------------------------------- status UI */
const last = {};
function statusRow(k, v, cls = '') { return `<div><i>${k}</i><span class="badge ${cls}">${v}</span></div>`; }
function drawStatus() {
  const s = fw.state;
  const timerTxt = `${String(s.h).padStart(2, '0')}:${String(s.m).padStart(2, '0')}:${String(s.s).padStart(2, '0')}`;
  els.stat.innerHTML =
    statusRow('当前界面', s.screen, s.screenId === 0 ? 'ok' : 'warn') +
    statusRow('时钟节目', s.view, '') +
    statusRow('设置选中', s.screenId === 1 ? s.menuSel : '—', '') +
    statusRow('倒计时', `${s.timer} ${timerTxt}`, s.timerId === 2 ? 'ok' : (s.timerId === 3 ? 'warn' : '')) +
    statusRow('PA4 防误触', s.lock ? '锁定中(1秒)' : '可切换', s.lock ? 'warn' : 'ok') +
    statusRow('PC13 LED', fw.led ? '点亮' : '熄灭', fw.led ? 'ok' : 'off');

  /* event log: report only the transitions that matter to the user */
  const sig = `${s.screenId}|${s.viewId}|${s.menuSel}|${s.timerId}`;
  if (last.sig !== undefined && last.sig !== sig) {
    if (s.screenId !== last.screenId) log(`界面 → ${s.screen}`, '');
    if (s.viewId !== last.viewId) log(`时钟节目 → ${s.view}`, '');
    if (s.screenId === 1 && s.menuSel !== last.menuSel) log(`选中 → ${s.menuSel}`, '');
    if (s.timerId !== last.timerId) log(`倒计时 → ${s.timer}`, '');
  }
  last.sig = sig; last.screenId = s.screenId; last.viewId = s.viewId;
  last.menuSel = s.menuSel; last.timerId = s.timerId;
  els.led.classList.toggle('on', !!fw.led);
}

/* ------------------------------------------------------------------ keys */
function keyDown(mask, el) {
  if (heldKeys.has(mask)) return;
  initAudio();
  if (audioCtx && audioCtx.state === 'suspended') audioCtx.resume();
  heldKeys.add(mask);
  holdStart.set(mask, performance.now());
  fw.setKey(mask, true);
  el?.classList.add('down');
}
function keyUp(mask, el) {
  if (!heldKeys.has(mask)) return;
  heldKeys.delete(mask);
  holdStart.delete(mask);
  fw.setKey(mask, false);
  el?.classList.remove('down');
  const bar = el?.querySelector('.hold');
  if (bar) bar.style.width = '0%';
}
for (const el of document.querySelectorAll('.key')) {
  const mask = Number(el.dataset.key);
  el.addEventListener('pointerdown', (e) => { e.preventDefault(); el.setPointerCapture(e.pointerId); keyDown(mask, el); });
  el.addEventListener('pointerup', () => keyUp(mask, el));
  el.addEventListener('pointercancel', () => keyUp(mask, el));
  el.addEventListener('lostpointercapture', () => keyUp(mask, el));
}
const KEYMAP = { a: 1, s: 2, d: 4, f: 8, '1': 1, '2': 2, '3': 4, '4': 8 };
addEventListener('keydown', (e) => {
  const k = KEYMAP[e.key.toLowerCase()];
  if (!k || e.repeat) return;
  keyDown(k, document.querySelector(`.key[data-key="${k}"]`));
});
addEventListener('keyup', (e) => {
  const k = KEYMAP[e.key.toLowerCase()];
  if (!k) return;
  keyUp(k, document.querySelector(`.key[data-key="${k}"]`));
});

/* --------------------------------------------------------------- controls */
for (const b of document.querySelectorAll('#speed button')) {
  b.addEventListener('click', () => {
    speed = Number(b.dataset.sp);
    for (const o of document.querySelectorAll('#speed button')) o.classList.toggle('primary', o === b);
  });
}
const tempEl = $('#temp'), tempv = $('#tempv');
function applyTemp() {
  const v = Number(tempEl.value);
  fw.setSensor(v);
  tempv.textContent = (v / 10).toFixed(1) + ' °C';
}
tempEl.addEventListener('input', applyTemp);
$('#mute').addEventListener('click', (e) => {
  muted = !muted;
  e.target.textContent = muted ? '🔇 已静音' : '🔊 声音开';
  if (audioCtx) gain.gain.value = 0;
  audioOn = false;
});
$('#skip').addEventListener('click', () => {
  /* run the power-on animation as fast as possible (~6.6 s of virtual time) */
  log('跳过开机动画', '');
  const target = fw.elapsedMs + 7000;
  while (fw.elapsedMs < target) fw.step(16.6);
});
$('#reset').addEventListener('click', async () => {
  log('MCU 复位', '');
  last.sig = undefined;
  await boot(true);
});

/* ------------------------------------------------------------------ loop */
function frame(ts) {
  if (!lastTs) lastTs = ts;
  let dt = (ts - lastTs) * speed;
  if (!(dt > 0)) dt = 0;                      /* never rewind the virtual clock */
  lastTs = ts;
  if (dt > 250) dt = 250;                     /* tab was in the background */
  pending += dt;
  let iters = 0;
  while (pending >= STEP_MS && iters < MAX_ITERS) { fw.step(STEP_MS); pending -= STEP_MS; iters++; }
  if (iters >= MAX_ITERS) pending = 0;

  /* 2 s hold indicator on the buttons */
  const now = performance.now();
  for (const [mask, t0] of holdStart) {
    const el = document.querySelector(`.key[data-key="${mask}"]`);
    const bar = el?.querySelector('.hold');
    if (bar) bar.style.width = Math.min(100, ((now - t0) / 2000) * 100) + '%';
  }

  blit();
  setBuzzer(!!fw.buzzer);
  drawStatus();

  frames++;
  if (ts - fpsAt > 500) {
    fps = Math.round((frames * 1000) / (ts - fpsAt));
    els.fps.textContent = `${fps} FPS · 虚拟时间 ${(fw.elapsedMs / 1000).toFixed(1)}s`;
    frames = 0; fpsAt = ts;
  }
  requestAnimationFrame(frame);
}

/* ------------------------------------------------------------------ start */
async function boot(reset = false) {
  fw = await Firmware.load('fw_sim.wasm');
  tempEl.value = 245;
  applyTemp();
  if (!reset) {
    log('上电复位 · 开机动画 (约 6.6 秒，可点“跳过开机动画”)', '');
    log('提示：时间界面 PA5 短按 → 设置界面；PA4 长按 2 秒 → 返回时间界面', '');
  }
  lastTs = 0; pending = 0;
  requestAnimationFrame(frame);
}

boot().catch((err) => {
  document.body.insertAdjacentHTML('beforeend',
    `<p style="color:#ff6b6b">模拟器启动失败：${err.message}<br>请通过 HTTP 访问本页（例如 <code>python3 -m http.server</code>），不要用 file:// 直接打开。</p>`);
});
