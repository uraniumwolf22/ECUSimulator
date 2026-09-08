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