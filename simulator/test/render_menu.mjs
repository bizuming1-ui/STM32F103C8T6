/* Renders the settings screen (both selections, both blink phases) to PNG so
 * the icons / rounded borders can be inspected pixel by pixel. */
import path from 'node:path';
import { loadFirmware, writePNG, ROOT, KEY } from './harness.mjs';

const OUT = path.join(ROOT, 'simulator', 'test', 'out');
const log = (t, fw) => console.log(`${t.padEnd(22)} state=${JSON.stringify(fw.state)}`);

const fw = await loadFirmware();
console.log('booting (skipping the 6.6 s power-on animation) ...');
fw.advance(7200);
log('after boot', fw);
console.log('non blank pixels on the clock face:', fw.nonBlankPixels());
writePNG(path.join(OUT, '00_clock.png'), fw, { scale: 3 });

/* open the settings screen: short PA5 */
fw.tap(KEY.PA5);
fw.advance(100);
log('after PA5 tap', fw);
writePNG(path.join(OUT, '01_menu_bell_on.png'), fw, { scale: 4 });
fw.advance(400);   /* blink half period */
writePNG(path.join(OUT, '02_menu_bell_off.png'), fw, { scale: 4 });

/* select the stopwatch: short PA4 */
fw.tap(KEY.PA4);
fw.advance(100);
log('after PA4 tap', fw);
writePNG(path.join(OUT, '03_menu_stop_on.png'), fw, { scale: 4 });
fw.advance(400);
writePNG(path.join(OUT, '04_menu_stop_off.png'), fw, { scale: 4 });

console.log('---- left icon (bell) ----');
console.log(fw.ascii(9, 6, 52, 50));
console.log('---- right icon (stopwatch) ----');
console.log(fw.ascii(67, 6, 52, 50));
console.log('files written to', OUT);
