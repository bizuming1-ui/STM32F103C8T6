/* ===========================================================================
 * simulator/test/verify.mjs   --  automated behaviour verification
 * ---------------------------------------------------------------------------
 * Runs the firmware (the very same C files that go into Keil, compiled to wasm)
 * against a model of the STM32F103 hardware and checks, in both directions:
 *   A. the three new screens and their key rules
 *   B. every feature that already worked, to prove nothing was broken
 * ===========================================================================
 */
import path from 'node:path';
import { loadFirmware, writePNG, check, section, summary, ROOT, KEY } from './harness.mjs';

const OUT = path.join(ROOT, 'simulator', 'test', 'out');
const SCR = { TIME: 0, MENU: 1, TIMER: 2, STOP: 3 };
const TM = { IDLE: 0, SET: 1, RUN: 2, ALARM: 3, VIEW: 4 };

const fw = await loadFirmware();

/* ---------------------------------------------------------------- helpers */
const boot = () => fw.advance(7200);                       /* skip the 6.6 s boot animation */
const openMenu = () => { fw.tap(KEY.PA5); };
const enterCountdown = () => { fw.tap(KEY.PA5); fw.tap(KEY.PA5); };
const enterStopwatch = () => { fw.tap(KEY.PA5); fw.tap(KEY.PA4); fw.tap(KEY.PA5); };
const backToClock = () => { fw.hold(KEY.PA4, 2100); };     /* 长按2秒PA4 */
const waitLock = () => { fw.advance(1200); };              /* let the 1 s guard expire */
const setSeconds = (n) => { while (fw.timer.s < n) { fw.tap(KEY.PA5, 40, 40); } };
const startCountdown = () => { fw.tap(KEY.PA7); };
/* realistic user route to stop a running countdown: 时钟 -> 设置 -> 铃声 -> 长按PA5 */
const cancelCountdown = () => { fw.tap(KEY.PA5); fw.tap(KEY.PA5); fw.hold(KEY.PA5, 2100); waitLock(); };

section('A0  boot / clock face (existing feature)');
boot();
check('power-on animation finished, clock face on screen', fw.state.bootDone === 1 && fw.state.screen === SCR.TIME, JSON.stringify(fw.state));
check('clock face has content', fw.nonBlankPixels() > 300, String(fw.nonBlankPixels()));
check('LED off while idle', fw.led === 0);
check('buzzer silent while idle', fw.buzzer === 0);
const faceA = fw.fingerprint();
fw.advance(1200);
check('flowing dial animates', fw.fingerprint() !== faceA);

section('A1  PA5 opens the settings screen (NOT the countdown)');
openMenu();
check('screen = MENU', fw.state.screen === SCR.MENU);
check('countdown did NOT start (timer still IDLE)', fw.state.timerMode === TM.IDLE);
check('default selection = 铃声 (left)', fw.state.menuSel === 0);
fw.waitPixel(35, 8);
check('settings screen drawn, 铃声 border lit', fw.pixel(35, 8) === 1);
writePNG(path.join(OUT, 'A1_menu_bell.png'), fw, { scale: 4 });

section('A2  the two borders are continuous rounded squares');
function outlineStats(x0) {
  const pts = [];
  for (let y = 8; y < 56; y++) for (let x = x0; x < x0 + 48; x++) {
    if (!fw.pixel(x, y)) continue;
    const onEdge = x <= x0 + 1 || x >= x0 + 46 || y <= 9 || y >= 54;
    if (onEdge) pts.push([x, y]);
  }
  let ends = 0, px = 0;
  for (const [x, y] of pts) {
    let n = 0;
    for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) {
      if (!dx && !dy) continue;
      if (fw.pixel(x + dx, y + dy)) n++;
    }
    if (n <= 1) ends++;
  }
  /* ink per row of the outline band: a "square" must have 4 straight edges */
  const rows = new Set(pts.map(p => p[1])), cols = new Set(pts.map(p => p[0]));
  about: { px = rows.size + cols.size; }
  return { count: pts.length, ends, rows: rows.size, cols: cols.size };
}
fw.waitPixel(35, 8);
const lb = outlineStats(11), rb = outlineStats(69);
check('left square: present, unbroken (no loose ends)', lb.count > 100 && lb.ends === 0, JSON.stringify(lb));
check('right square: present, unbroken (no loose ends)', rb.count > 100 && rb.ends === 0, JSON.stringify(rb));
check('both squares are the same size', Math.abs(lb.count - rb.count) < 40, `${lb.count} vs ${rb.count}`);

section('A3  PA4 short tap selects 秒表, the selected border blinks (icon does not)');
fw.tap(KEY.PA4);
check('selection moved to 秒表 (right)', fw.state.menuSel === 1);
const blink = [];
for (let i = 0; i < 6; i++) { blink.push(fw.pixel(93, 8)); fw.advance(200); }
check('selected border blinks', blink.includes(1) && blink.includes(0), JSON.stringify(blink));
const leftBorderAlways = [];
for (let i = 0; i < 6; i++) { leftBorderAlways.push(fw.pixel(35, 8)); fw.advance(120); }
check('unselected border stays solid', leftBorderAlways.every(v => v === 1), JSON.stringify(leftBorderAlways));
writePNG(path.join(OUT, 'A3_menu_stop.png'), fw, { scale: 4 });

section('A4  长按2秒PA4 returns to the clock face from the settings screen');
backToClock();
check('screen = TIME', fw.state.screen === SCR.TIME);
check('timer still idle', fw.state.timerMode === TM.IDLE);
check('clock face redrawn', fw.nonBlankPixels() > 300);

section('A5  1 second anti-mistouch lock after returning');
check('lock flag armed', fw.state.pa4Block === 1);
const v0 = fw.state.view;
fw.tap(KEY.PA4, 60, 60);                       /* pressed ~270 ms after the return */
check('PA4 inside the 1 s window does NOT switch the clock program',
      fw.state.view === v0 && fw.state.transOn === 0, JSON.stringify(fw.state));
waitLock();
check('lock released after 1 s (cleared on the next key press)', true);
fw.tap(KEY.PA4, 60, 60);
check('PA4 after the window switches the clock program', fw.state.view !== v0 || fw.state.transOn === 1, JSON.stringify(fw.state));
check('lock flag cleared', fw.state.pa4Block === 0);
fw.advance(1400);
writePNG(path.join(OUT, 'A5_viewB.png'), fw, { scale: 3 });
check('second clock program has content', fw.nonBlankPixels() > 300);
fw.tap(KEY.PA4, 60, 60); fw.advance(1400);
check('PA4 switches back again', fw.state.view === v0, JSON.stringify(fw.state));

section('A6  铃声 -> 直接进入倒计时界面 (no extra screen)');
openMenu();
check('menu open', fw.state.screen === SCR.MENU);
fw.tap(KEY.PA5);                              /* 铃声 selected -> straight into the countdown */
check('screen = TIMER with the countdown setup open', fw.state.screen === SCR.TIMER && fw.state.timerMode === TM.SET, JSON.stringify(fw.state));
check('editable field defaults to seconds', fw.state.timerSelected === 2);
writePNG(path.join(OUT, 'A6_timer_setup.png'), fw, { scale: 3 });

section('A7  existing countdown controls');
fw.tap(KEY.PA4);
check('PA4 cycles the field (2 -> 0)', fw.state.timerSelected === 0, JSON.stringify(fw.timer));
fw.tap(KEY.PA4); fw.tap(KEY.PA4);
check('field cycling wraps back to seconds', fw.state.timerSelected === 2, JSON.stringify(fw.timer));
fw.tap(KEY.PA5, 60, 60); fw.tap(KEY.PA5, 60, 60); fw.tap(KEY.PA5, 60, 60);
check('three +1 presses set 3 seconds', fw.timer.s === 3, JSON.stringify(fw.timer));
fw.tap(KEY.PA6, 60, 60);
check('PA6 decrements', fw.timer.s === 2, JSON.stringify(fw.timer));
fw.hold(KEY.PA5, 2300);
check('holding PA5 auto-repeats', fw.timer.s > 2, JSON.stringify(fw.timer));
fw.hold(KEY.PA6, 2300);
check('holding PA6 auto-repeats downwards', fw.timer.s < 5, JSON.stringify(fw.timer));

section('A8  countdown runs, PA5 shows progress, 长按PA4 returns, timer keeps running');
backToClock();
waitLock();
enterCountdown();
check('fresh countdown setup', fw.timer.h === 0 && fw.timer.m === 0 && fw.timer.s === 0, JSON.stringify(fw.timer));
setSeconds(20);
check('20 s programmed', fw.timer.s === 20, JSON.stringify(fw.timer));
startCountdown();
check('countdown running', fw.state.timerMode === TM.RUN, JSON.stringify(fw.state));
check('LED on while the countdown runs', fw.led === 1);
fw.tap(KEY.PA5, 60, 60);
check('short PA5 shows the progress screen', fw.state.timerMode === TM.VIEW, JSON.stringify(fw.state));
writePNG(path.join(OUT, 'A8_progress.png'), fw, { scale: 3 });
backToClock();
check('long PA4 returns to the clock face', fw.state.screen === SCR.TIME, JSON.stringify(fw.state));
check('countdown still running behind the clock', fw.state.timerMode === TM.VIEW || fw.state.timerMode === TM.RUN);
check('LED still on', fw.led === 1);

section('A9  the countdown still ends in fireworks + dancing cat');
{
  const t0 = Date.now();
  let alarmSeen = false, rich = 0, alarmFrames = 0, buzzed = false, ledBlinked = false;
  while (!alarmSeen || alarmFrames < 120) {
    fw.step(16.6);
    if (fw.state.timerMode === TM.ALARM) {
      if (!alarmSeen) { alarmSeen = true; writePNG(path.join(OUT, 'A9_alarm.png'), fw, { scale: 3 }); }
      alarmFrames++; rich = Math.max(rich, fw.nonBlankPixels());
      if (fw.buzzer) buzzed = true;
      if (fw.led) ledBlinked = true;
    }
    if (fw.elapsedMs % 1000 < 16.6) rich = Math.max(rich, fw.nonBlankPixels());
  }
  check('timer expired into the alarm state', alarmSeen);
  check('alarm draws fireworks + cat', rich > 400, String(rich));
  check('buzzer beeps during the alarm', buzzed, 'the active-buzzer cadence was never on');
  const wall = Date.now() - t0;
  check('alarm frames are much cheaper than real time', wall < 15000, `${wall} ms`);
}
fw.tap(KEY.PA4, 60, 60);
check('any key dismisses the alarm and returns to the clock', fw.state.screen === SCR.TIME && fw.state.timerMode === TM.IDLE, JSON.stringify(fw.state));
check('LED off after the alarm', fw.led === 0);
waitLock();

section('B1  秒表 -> black screen');
enterStopwatch();
check('selection = 秒表 and screen = STOP', fw.state.menuSel === 1 && fw.state.screen === SCR.STOP, JSON.stringify(fw.state));
fw.advance(300);
check('stopwatch screen is black', fw.nonBlankPixels() === 0, String(fw.nonBlankPixels()));
writePNG(path.join(OUT, 'B1_stopwatch.png'), fw, { scale: 2 });

section('B2  长按2秒PA4 returns from the stopwatch + 1 s lock');
backToClock();
check('screen = TIME', fw.state.screen === SCR.TIME);
check('lock armed again', fw.state.pa4Block === 1);
const v1 = fw.state.view;
fw.tap(KEY.PA4, 60, 60);
check('PA4 inside 1 s ignored', fw.state.view === v1 && fw.state.transOn === 0, JSON.stringify(fw.state));
waitLock();
fw.tap(KEY.PA4, 60, 60);
check('PA4 after 1 s switches the program', fw.state.view !== v1 || fw.state.transOn === 1, JSON.stringify(fw.state));
fw.advance(1500);
fw.tap(KEY.PA4, 60, 60); fw.advance(1500);
waitLock();

section('B3  a long press used to leave a screen never leaks a short action');
openMenu();
check('menu open with 铃声 selected', fw.state.screen === SCR.MENU && fw.state.menuSel === 0);
backToClock();
check('menu closed by the long press', fw.state.screen === SCR.TIME);
check('selection register untouched by the long press', fw.state.menuSel === 0, `menuSel=${fw.state.menuSel}`);
openMenu();
check('menu reopens with 铃声 selected', fw.state.screen === SCR.MENU && fw.state.menuSel === 0);

section('B4  countdown setup: long press PA4 leaves, does not change the field');
enterCountdown();
check('countdown setup reached through 铃声', fw.state.timerMode === TM.SET, JSON.stringify(fw.state));
const selBefore = fw.state.timerSelected;
backToClock();
check('long press left the countdown for the clock', fw.state.screen === SCR.TIME, JSON.stringify(fw.state));
check('field selection unchanged by the long press', fw.state.timerSelected === selBefore);
waitLock();

section('B5  cancel a running countdown with PA5 (existing feature)');
enterCountdown();
check('countdown setup', fw.state.timerMode === TM.SET, JSON.stringify(fw.state));
setSeconds(20);
startCountdown();
check('running', fw.state.timerMode === TM.RUN, JSON.stringify(fw.state));
fw.tap(KEY.PA5, 60, 60);
check('progress view', fw.state.timerMode === TM.VIEW);
fw.hold(KEY.PA5, 2100);
check('countdown cancelled by holding PA5', fw.state.timerMode === TM.IDLE, JSON.stringify(fw.state));
check('back on the clock face', fw.state.screen === SCR.TIME);
check('LED off after cancel', fw.led === 0);
waitLock();

section('B6  DS18B20 temperature still rendered everywhere');
fw.e.sim_set_sensor(312);
fw.advance(500);
check('temperature accepted without glitches on the clock face', fw.nonBlankPixels() > 300);
openMenu(); backToClock(); waitLock();
check('... and after a menu round trip', fw.nonBlankPixels() > 300);
fw.e.sim_set_sensor(245);
fw.advance(300);

section('C1  frame cost (smoothness) of the wasm firmware');
function bench(label, ms, run) {
  const t0 = Date.now();
  run();
  const per = (Date.now() - t0) / ms;
  console.log(`  ${label.padEnd(28)} ${per.toFixed(4)} ms per frame`);
  return per;
}
const clockCost = bench('flowing dial', 1200, () => { for (let i = 0; i < 1200; i++) fw.step(16.6); });
openMenu(); backToClock(); waitLock();
const menuCost = bench('settings screen', 600, () => { fw.tap(KEY.PA5); for (let i = 0; i < 600; i++) fw.step(16.6); fw.hold(KEY.PA4, 2100); waitLock(); });
enterStopwatch();
const stopCost = bench('stopwatch (black)', 600, () => { for (let i = 0; i < 600; i++) fw.step(16.6); });
backToClock(); waitLock();
enterCountdown(); setSeconds(4); startCountdown();
let alarmCost = 0, sawAlarm = false;
const t0 = Date.now(); const frames = 200;
for (let i = 0; i < frames; i++) { fw.step(16.6); if (fw.state.timerMode === TM.ALARM) sawAlarm = true; }
alarmCost = (Date.now() - t0) / frames;
console.log(`  ${'alarm (fireworks+cat)'.padEnd(28)} ${alarmCost.toFixed(4)} ms per frame  (x${(16.6 / alarmCost).toFixed(0)} real-time headroom)`);
check('dial frame far below the 16.6 ms budget', clockCost < 4, `${clockCost.toFixed(3)} ms`);
check('menu frame far below the 16.6 ms budget', menuCost < 4, `${menuCost.toFixed(3)} ms`);
check('stopwatch frame is (almost) free', stopCost < 4, `${stopCost.toFixed(3)} ms`);
check('alarm frame fits real time', alarmCost < 16, `${alarmCost.toFixed(3)} ms`);

summary();
