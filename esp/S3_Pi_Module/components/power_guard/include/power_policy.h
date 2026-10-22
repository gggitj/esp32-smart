#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <math.h>
typedef struct { int64_t low_since_us; } power_policy_t;
// Pure policy: only confirmed battery power and fresh, plausible measurements
// may start a shutdown. Unknown source/read failure must never imply battery.
static inline bool power_policy_update(power_policy_t *p, int source, bool fresh, float volts, int64_t now) {
    if (source != 0 || !fresh || !isfinite(volts) || volts < 2.0f || volts > 4.5f) {
        p->low_since_us = 0; return false;
    }
    if (volts >= 3.40f) { p->low_since_us = 0; return false; }
    if (!p->low_since_us || now < p->low_since_us) p->low_since_us = now;
    return now - p->low_since_us >= 20000000;
}
