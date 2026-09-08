#ifndef UTILS_H
#define UTILS_H
#include "includes.h"

word16 KtoFConversion(int F);

long long get_time_in_ms();

int calculateLowerBinIdx(int value, const uint16_t axis[], int numBins);

extern void cleanup_ipc(void);

extern void handle_signal(int signalNumber);

extern struct Engine engineInstance;         // Instantiate instance of engine values
extern struct ECUSchedule schedule;                // Instantiate the ECU Schedule

extern const char *name;    // Define the location of the shared memory for engine struct
extern const char *engineSemName; // Define location for engine shared memory semaphore

extern const int SIZE;
extern sem_t *engineSem;
extern int sharedEngineMem;
extern void *mappedPtr;

#endif