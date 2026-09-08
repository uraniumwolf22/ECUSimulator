#include "utils.h"

word16 KtoFConversion(int F){           // This is used to convert F for the user to the internal representation in Kelvin.
    return ((F-32) * 5 / 9) + 273.15;
}

long long get_time_in_ms() {            // Get the current system time in ms
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    // Convert seconds and nanoseconds to total milliseconds
    return ((long long)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

int calculateLowerBinIdx(int value, const uint16_t axis[], int numBins){
    int currentBinIdx = 0;
    for(int i = numBins - 1; i >= 0; i--){
        if (value >= axis[i]){
            currentBinIdx = (i == 0) ? 0 : i - 1;
            break;
        }
    }
    return currentBinIdx;
}

struct Engine engineInstance = {0};         // Instantiate instance of engine values
struct ECUSchedule schedule;                // Instantiate the ECU Schedule

const char *name = "engineStateMemory_local";    // Define the location of the shared memory for engine struct
const char *engineSemName = "/engineSemaphore_local"; // Define location for engine shared memory semaphore

const int SIZE = sizeof(engineInstance);
sem_t *engineSem = NULL;
int sharedEngineMem = -1;
void *mappedPtr = NULL;

void cleanup_ipc(void){
    if (mappedPtr != NULL && mappedPtr != MAP_FAILED) {
        munmap(mappedPtr, SIZE);
        mappedPtr = NULL;
    }

    if (sharedEngineMem != -1) {
        close(sharedEngineMem);
        sharedEngineMem = -1;
    }

    if (engineSem != NULL) {
        sem_close(engineSem);
        engineSem = NULL;
    }

    sem_unlink(engineSemName);
    shm_unlink(name);
}

void handle_signal(int signalNumber){
    (void)signalNumber;
    cleanup_ipc();
    _exit(0);
}