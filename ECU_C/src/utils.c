#include "utils.h"
#include "tunables.h"

word16 KtoFConversion(int F){           // Convert F to K
    return ((F-32) * 5 / 9) + 273.15;
}

long long get_time_in_ms() {            // Get the current system time in ms
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    return ((long long)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);          // Convert seconds and nanoseconds to total milliseconds
}

int calculateLowerBinIdx(int value, const uint16_t axis[], int numBins){    // Calculate the lower bin index for a value in an array
    int currentBinIdx = 0;
    for(int i = numBins - 1; i >= 0; i--){
        if (value >= axis[i]){
            currentBinIdx = i;                                              // Axis point we are at or past
            break;
        }
    }
    if (currentBinIdx > numBins - 2){                                       // Leave room for upper = lower + 1
        currentBinIdx = numBins - 2;
    }
    return currentBinIdx;
}

struct Engine engineInstance = {0};                     // Instantiate instance of engine values
struct ECUSchedule schedule;                            // Instantiate the ECU Schedule

const char *name = "engineStateMemory_local";           // Define the location of the shared memory for engine struct
const char *engineSemName = "/engineSemaphore_local";   // Define location for engine shared memory semaphore

const int SIZE = sizeof(engineInstance);                // Define the size of the engine struct
sem_t *engineSem = NULL;
int sharedEngineMem = -1;
void *mappedPtr = NULL;

void cleanup_ipc(void){                                 // Clean up the semaphore and shared memory
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
    printf("\e[?25h\e[0m\n");  // restore cursor / colors
    cleanup_ipc();
    _exit(0);
}
