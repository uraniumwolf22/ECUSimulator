#include "air.h"
#include "utils.h"



void calculateVE(struct Engine *eng){        // Calculate the engine VE
    int MAPBin = 0;
    int RPMBin = 0;

    uint16_t MAPKPA = eng->MAP;

    for(int i = MAP_BINS - 1 ; i >= 0; i--){        // Calculate bin of MAP
        if(MAPKPA  >= mapAxis[i] ){                 // Check if we are in the correct bin
            MAPBin = (i == 0) ? 0 : i;          // Make sure the bin isnt negative
            break;
        }
    }

    for(int i = RPM_BINS - 1; i >= 0; i--){         // Calculate bin of RPM
        if(eng->RPM >= rpmAxis[i] ){                // Check if we are in the correct bin
            RPMBin = (i == 0) ? 0 : i;          // Make sure the bin isnt nagative
            break;
        }
    }

    RPMBin = RPMBin > RPM_BINS - 2 ? RPM_BINS - 2 : RPMBin;

    int VEIndex = (MAPBin * RPM_BINS) + RPMBin;     // Calculate VE value location in 1D map using calculated bins

    int16_t RPMDelta = eng->RPM - rpmAxis[RPMBin];

    RPMDelta = RPMDelta < 0 ? 0 : RPMDelta > rpmAxis[0] ? rpmAxis[0] : RPMDelta;

    float RPMRatio = (float)RPMDelta / (float)rpmAxis[0];

    

    eng->VE = VETable[VEIndex] + ((VETable[VEIndex + 1] - VETable[VEIndex]) * RPMRatio);
}

void calculateAFR(struct Engine *eng){      // Fetches current AFR with lookup table
    int MAPBin = MAP_BINS - 1;
    int RPMBin = RPM_BINS - 1;

    word16 MAPKPA = eng->MAP;

    for(int i = MAP_BINS - 1; i >= 0; i--){     // Find the current MAP bin
        if(MAPKPA >= mapAxis[i]){
            MAPBin = (i == 0) ? 0 : i - 1;
            break;
        }
    }

    for(int i = RPM_BINS - 1; i >= 0; i--){     // Find the current RPM bin
        if(eng->RPM >= rpmAxis[i]){
            RPMBin = (i == 0) ? 0 : i - 1;  
            break;
        }
    }

    int AFRIndex = (MAPBin * RPM_BINS) + RPMBin;    // Get the AFR from a 1D array using the current bins

    eng->AFR_TARGET = afrTable[AFRIndex];           // Set the AFR Target
}