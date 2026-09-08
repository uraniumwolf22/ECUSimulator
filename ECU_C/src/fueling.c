#include "fueling.h"
#include "tunables.h"
#include "utils.h"
#include "tables.h"

void calculateToeEnrichment(struct Engine *eng){                // Calculated the toe in enrichment based on the speed of the TPS sensor
    float TEM = 1;                                              // Toe enrichment multiplier
  int16_t deltaTPS = eng->TPS - eng->lastTPSValue;              // Calculate the delta of the TPS sensor over time
  //printf("TPS_RATE: %d\n",deltaTPS);
  if(deltaTPS >= TPSDeadband){                                   // Make sure the delta is not outside of the deadband
    TEM = deltaTPS * toeEnrichment;                              // Scale the toe enrichment factor by the TPS delta
    //printf("TIE EVENT REGISTERED WITH DELTA OF %d\n",deltaTPS);
   }

  if (TEM < 1){         // Dont let TEM go negative
    TEM = 1;
  }
  if (eng->toeEnrichmentMultiplier < TEM){
    eng->toeEnrichmentMultiplier = TEM;     // Set the engine toe enrichment
  }
  eng->lastTPSValue = eng->TPS;             // Set the current TPS to the last, for the next loop

}

void calculateFuelLoad(struct Engine *eng){         // Calculate engine theoretical fuel loading

    word32 flowPerMinTh = eng->RPM * eng->displacementPerRev;                   // Calculate the theoretical air flow per minute
    word32 Density = (eng->MAP * 10 * 1000 ) / (287 * eng->IAT);                // Calculate the current air density and scale by 1000 to keep percision
    word32 realAirFlow = (flowPerMinTh * eng->VE) / 100;                        // Calculate the true air flow factoring in Volumetric efficiency
    word32 realAirMass = realAirFlow * Density;                                 // Calculate the real air mass entering the engine still scaled
    word16 fuelLoad = ((realAirMass * 10) / (eng->AFR_TARGET)/60);              // Calculate fuel load and scale back to Grams/Second.


    eng->fuelLoad = (word16)fuelLoad;     // cast to int16 and return fuel load in grams per minute
}

void calculateSTFT(struct Engine *eng){                     // Calculated STFT correction in %
    float AFRDELTA = (eng->REALAFR) - (eng->AFR_TARGET);
    //printf("REALAFR: %f\n",eng->REALAFR);
    //printf("AFRDELTA: %d\n",AFRDELTA);

    float correction = AFRDELTA * STFTCorrectionDamper;       // Intigrate AFR Delta with a damping factor.  May change damping factor based on magnitude of delta
    eng->STFTCorrection = eng->STFTCorrection + correction; // Add correction to STFT

    if(eng->STFTCorrection >= MAXSTFT){                     // Make sure STFT is not maxed out
        eng->STFTCorrection = MAXSTFT;
    }
    if(eng->STFTCorrection <= MINSTFT){
        eng->STFTCorrection = MINSTFT;
    }
    if(AFRDELTA >= 0 && eng->STFTCorrection < 0){
        eng->STFTCorrection = 0;
    }
    
    if(AFRDELTA <= 0 && eng->STFTCorrection > 0){
    eng->STFTCorrection = 0;
    }
}

void calculateLTFT(struct Engine *eng){
    // X is RPM Y is KPA
    // X coordinate is the current RPM bin you are in same for Y but with Kpa
    int engineRPM = eng->RPM;
    word16 MAPKPA = eng->MAP;

    // Find upper and lower bins of RPM (X)
    int lowerRPMBin = calculateLowerBinIdx(engineRPM, LTFTRPMAxis, LTFTRPM_BINS);   // Lower bin on X axis
    int upperRPMBin = lowerRPMBin + 1;                                              // Upper bin on X axis

    // Find upper and lower bins of MAP (Y)
    int lowerMAPBin = calculateLowerBinIdx(MAPKPA, LTFTMAPAxis, LTFTMAP_BINS);      // Lower bin on Y axis
    int upperMAPBin = lowerMAPBin + 1;                                              // Upper bin on Y axis

    float RPMWeight = (engineRPM - LTFTRPMAxis[lowerRPMBin]) / LTFTRPMAxis[0];           // Calculate bin bias for RPM (X)
    float MAPWeight = (MAPKPA - LTFTMAPAxis[lowerMAPBin]) / LTFTMAPAxis[0];              // Calculate bin bias for MAP (Y)

    //printf("RPMWeight: %f\nMAPWeight: %f\n");

    float topLeftShare = (1 - RPMWeight) * MAPWeight;                       // Calculate % shares for each cell
    float topRightShare = RPMWeight * MAPWeight;
    float bottomLeftShare = (1 - RPMWeight) * (1 - MAPWeight);
    float bottomRightShare = RPMWeight * (1 - MAPWeight);

    //printf("\e[H\nTOPLEFT %f TOPRIGHT: %f\n BOTLEFT %f BOTRIGHT %f\n",topLeftShare,topRightShare,bottomLeftShare,bottomRightShare);

    // Calculate cell indexes
    int topLeftCell_idx = (lowerMAPBin * LTFTRPM_BINS) + lowerRPMBin;           // Index of top left cell
    int topRightCell_idx = (lowerMAPBin * LTFTRPM_BINS) + upperRPMBin;          // Index of top right cell
    int bottomLeftCell_idx = (upperMAPBin * LTFTRPM_BINS) + lowerRPMBin;        // Index of bottom left cell
    int bottomRightCell_idx = (upperMAPBin * LTFTRPM_BINS) + upperRPMBin;       // Index of bottom right cell

    //printf("\e[H\nTLIDX: %d\nTRIDX: %d\nBLIDX: %d\nBRIDX: %d\n",topLeftCell_idx,topRightCell_idx,bottomLeftCell_idx,bottomRightCell_idx);


    float stepDirection = 0.0;

    if (eng->STFTCorrection > STFTDEADBAND){      // Check if we are in deadband
        stepDirection = 1.0;                    // Adding fuel,  so step up LTFT

    } else if (eng->STFTCorrection < -STFTDEADBAND){
        stepDirection = -1.0;                   // Removing fuel, lower LTFT
    }

    if (stepDirection != 0.0) {
        LTFT[bottomLeftCell_idx]  += (stepDirection * LTFTSCALAR * bottomLeftShare);    // Adjust each cell according to its share
        LTFT[bottomRightCell_idx] += (stepDirection * LTFTSCALAR * bottomRightShare);
        LTFT[topLeftCell_idx]     += (stepDirection * LTFTSCALAR * topLeftShare);
        LTFT[topRightCell_idx]    += (stepDirection * LTFTSCALAR * topRightShare);
    }

    eng->LTFTCorrection = (LTFT[bottomLeftCell_idx]  * bottomLeftShare)  +              // Interpolate the LTFT table to get fuel correction multiplier
                          (LTFT[bottomRightCell_idx] * bottomRightShare) +
                          (LTFT[topLeftCell_idx]     * topLeftShare)     +
                          (LTFT[topRightCell_idx]    * topRightShare);

}

void correctFuelLoad(struct Engine *eng){
    if(eng->Coldstart == true && eng->COOLANT <= eng->coldCoolant){
        eng->fuelLoad = eng->fuelLoad * coldStartEnrichment;    //TODO: Convert this to adjusting Target AFR not actual fuel load
    } else {
        eng->Coldstart = false;
    }
    if (eng->EngineCranking == true){                           //TODO: Convert this to adjusting Target AFR not actual fuel load
        eng->fuelLoad = eng->fuelLoad * crankingEnrichment;
    }
    if (eng->fuelTrim != 1){
        eng->fuelLoad = eng->fuelLoad * eng->fuelTrim;
    }

    //TODO: You will need to condition the function to take the multiplier and convert it into actually how much the fuel load should change
    eng->fuelLoad = eng->fuelLoad * eng->toeEnrichmentMultiplier;

    //TODO:  
    eng->fuelLoad = eng->fuelLoad + (eng->fuelLoad * (eng->STFTCorrection / 100));    // Adjust for STFT

    // LTFT probably should not update during cranking or cold start
    //eng->fuelLoad = eng->fuelLoad * eng->LTFTCorrection;    // Adjust for LTFT

}