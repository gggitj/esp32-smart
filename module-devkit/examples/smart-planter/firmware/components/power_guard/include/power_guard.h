#pragma once

/* Soft protection only for confirmed battery supply and fresh MAX17043 data.
 * No inferred USB status; unknown supply cannot trigger automatic sleep. */
void power_guard_start(void);
const char *power_guard_source(void);
const char *power_guard_external_json(void);
const char *power_guard_status(void);
