#ifndef DEBUG_H
#define DEBUG_H

#include "includes.h"

// * Capture a snapshot under the semaphore (fast)
void debugCapture(struct Engine *eng, struct ECUSchedule *sched);

// * Render the latest snapshot at ~7 Hz (call outside the semaphore)
void debugDisplay(void);

#endif
