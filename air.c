#include "air.h"
#include "utils.h"



void calculateVE(struct Engine *eng){        // Calculate the engine VE
    int MAPBin = MAP_BINS - 1;
    int RPMBin = RPM_BINS - 1;

    uint16_t MAPKPA = eng->MAP;

    for(int i = MAP_BINS - 1 ; i >= 0; i--){        // Calculate bin of MAP
        if(MAPKPA  >= mapAxis[i] ){                 // Check if we are in the correct bin
            MAPBin = (i == 0) ? 0 : i - 1;          // Make sure the bin isnt negative
            break;
        }
    }

    for(int i = RPM_BINS - 1; i >= 0; i--){         // Calculate bin of RPM
        if(eng->RPM >= rpmAxis[i] ){                // Check if we are in the correct bin
            RPMBin = (i == 0) ? 0 : i - 1;          // Make sure the bin isnt nagative
            break;
        }
    }

    int VEIndex = (MAPBin * RPM_BINS) + RPMBin;     // Calculate VE value location in 1D map using calculated bins
    // printf("\eCURRENT VE IS: %d\nRPM_BIN: %d\nMAP_BIN: %d\n MAP: %d\n RPM: %d\n",VETable[VEIndex],RPMBin,MAPBin,MAPKPA,eng->RPM);
    eng->VE = VETable[VEIndex];                     // Return VE value fetched from table

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