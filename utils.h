#ifndef UTILS_H
#define UTILS_H
#include "includes.h"

word16 KtoFConversion(int F);

long long get_time_in_ms();

int calculateLowerBinIdx(int value, const uint16_t axis[], int numBins);

#endif