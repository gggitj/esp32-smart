#pragma once

/* Watches the MAX17043 and puts the module into deep sleep before the cell is
 * drained. No effect when no battery is fitted (battery_V = null). */
void power_guard_start(void);
