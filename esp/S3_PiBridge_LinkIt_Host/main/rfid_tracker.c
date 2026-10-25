#include "rfid_tracker.h"
#include <string.h>

void rfid_tracker_init(rfid_tracker_t *tracker)
{
    memset(tracker, 0, sizeof(*tracker));
    tracker->absent_since_ms = -1;
}

void rfid_tracker_present(rfid_tracker_t *tracker)
{
    tracker->absent_since_ms = -1;
}

void rfid_tracker_absent(rfid_tracker_t *tracker, int64_t now_ms, unsigned removal_ms)
{
    if (tracker->absent_since_ms < 0) tracker->absent_since_ms = now_ms;
    if (now_ms - tracker->absent_since_ms >= removal_ms) tracker->have_uid = false;
}

bool rfid_tracker_should_report(const rfid_tracker_t *tracker, const rc522_uid_t *uid)
{
    if (!uid || (uid->length != 4 && uid->length != 7 && uid->length != 10)) return false;
    return !tracker->have_uid || tracker->last_uid.length != uid->length ||
           memcmp(tracker->last_uid.bytes, uid->bytes, uid->length) != 0;
}

void rfid_tracker_reported(rfid_tracker_t *tracker, const rc522_uid_t *uid)
{
    tracker->last_uid = *uid;
    tracker->have_uid = true;
    rfid_tracker_present(tracker);
}
