#include "air.h"
#include "utils.h"



void calculateVE(struct Engine *eng){               // Calculate the engine VE

    int MAPBin = calculateLowerBinIdx(eng->MAP, mapAxis, MAP_BINS);                         // Calculate lower index of the MAP bin 

    int RPMBin = calculateLowerBinIdx(eng->RPM, rpmAxis, RPM_BINS);                         // Calculate lower index of the RPM bin

    
    RPMBin = RPMBin > RPM_BINS - 2 ? RPM_BINS - 2 : RPMBin;

    int VEIndex = (MAPBin * RPM_BINS) + RPMBin;                                             // Calculate VE value in 1D table using calculated bins (Y Axis * Bins per row) + X Axis

    int16_t RPMDelta = eng->RPM - rpmAxis[RPMBin];                                          // How far the actual RPM is from the bottom of the bin

    RPMDelta = RPMDelta < 0 ? 0 : RPMDelta > rpmAxis[0] ? rpmAxis[0] : RPMDelta;            // Clamp delta

    float RPMRatio = (float)RPMDelta / (float)rpmAxis[0];                                   // What % of the way are we through the bin

    eng->VE = VETable[VEIndex] + ((VETable[VEIndex + 1] - VETable[VEIndex]) * RPMRatio);    // Calculated interpolated VE value
}


void calculateAFR(struct Engine *eng){                                  // Fetches current AFR with lookup table

    int MAPBin = calculateLowerBinIdx(eng->MAP, mapAxis, MAP_BINS);     // Calculate lower index of the MAP bin 

    int RPMBin = calculateLowerBinIdx(eng->RPM, rpmAxis, RPM_BINS);     // Calculate lower index of the RPM bin

    int AFRIndex = (MAPBin * RPM_BINS) + RPMBin;                        // Get the AFR from a 1D array using the current bins

    eng->AFR_TARGET = afrTable[AFRIndex];                               // Set the AFR Target
}