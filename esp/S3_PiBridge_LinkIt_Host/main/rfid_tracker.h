#pragma once

#include "rc522.h"

typedef struct {
    rc522_uid_t last_uid;
    bool have_uid;
    int64_t absent_since_ms;
} rfid_tracker_t;

void rfid_tracker_init(rfid_tracker_t *tracker);
void rfid_tracker_present(rfid_tracker_t *tracker);
void rfid_tracker_absent(rfid_tracker_t *tracker, int64_t now_ms, unsigned removal_ms);
bool rfid_tracker_should_report(const rfid_tracker_t *tracker, const rc522_uid_t *uid);
/* Call only once the event has been accepted by the PBR1 sender. */
void rfid_tracker_reported(rfid_tracker_t *tracker, const rc522_uid_t *uid);
