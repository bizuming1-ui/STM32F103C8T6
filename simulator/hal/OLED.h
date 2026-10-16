/* simulator/hal/OLED.h -- case bridge.
 *
 * The Keil project stores the OLED header as "OELD.h" (a historic typo).
 * Keil runs on Windows, where the filesystem is case-insensitive, so
 * #include "OLED.h" finds OELD.h.  A case-sensitive host cannot do that,
 * therefore this alias lives in the simulator include path only.
 * The firmware's own OELD.h is the single source of truth.
 */
#ifndef SIM_OLED_ALIAS_H
#define SIM_OLED_ALIAS_H
#include "OELD.h"
#endif
