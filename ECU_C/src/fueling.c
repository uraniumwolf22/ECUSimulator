#include "fueling.h"
#include "tunables.h"
#include "utils.h"
#include "tables.h"


void calculateToeEnrichment(struct Engine *eng){                // Calculated the toe in enrichment based on the speed of the TPS sensor
    float TEM = 1;                                              // Local enrichment multiplier

  int16_t deltaTPS = eng->TPS - eng->lastTPSValue;              // Calculate the delta of the TPS sensor over time

  if(deltaTPS >= TPSDeadband){                                  // Make sure the delta is not outside of the deadband
    TEM = deltaTPS * toeEnrichment;                             // Scale the toe enrichment factor by the TPS delta
   }

   TEM = TEM < 1 ? 1 : TEM;                                     // Clamp TEM to positive values

  if (eng->toeEnrichmentMultiplier < TEM){
    eng->toeEnrichmentMultiplier = TEM;                         // Set TEM to new value if larger than previous
  }

  eng->lastTPSValue = eng->TPS;                                 // Set new TPS value

}


void calculateFuelLoad(struct Engine *eng){                                     // Calculate engine theoretical fuel loading

    word32 flowPerMinTh = eng->RPM * eng->displacementPerRev;                   // Calculate the theoretical air flow per minute
    word32 Density = (eng->MAP * 10 * 1000 ) / (287 * eng->IAT);                // Calculate the current air density and scale by 1000 to keep percision
    word32 realAirFlow = (flowPerMinTh * eng->VE) / 100;                        // Calculate the true air flow factoring in Volumetric efficiency
    word32 realAirMass = realAirFlow * Density;                                 // Calculate the real air mass entering the engine still scaled
    word16 fuelLoad = ((realAirMass * 10) / (eng->AFR_TARGET)/60);              // Calculate fuel load and scale back to Grams/Second.

    eng->fuelLoad = (word16)fuelLoad;                                           // Return fuel load in grams / minute
}


void calculateSTFT(struct Engine *eng){                                         // Calculated STFT correction in %
    float AFRDELTA = (eng->REALAFR) - (eng->AFR_TARGET);                        // Calculate AFR delta

    float correction = AFRDELTA * STFTCorrectionDamper;                         // Intigrate AFR Delta with a damping factor

    eng->STFTCorrection = eng->STFTCorrection + correction;                     // Add correction to STFT


    if                                                                          // Keep STFT within bounds
    (eng->STFTCorrection >= MAXSTFT){                             
        eng->STFTCorrection = MAXSTFT;
    }

    else if
    (eng->STFTCorrection <= MINSTFT){
        eng->STFTCorrection = MINSTFT;
    }

    // Snap STFT back to center only on a clear AFR cross (hysteresis).
    // A zero threshold lets sensor noise wipe STFT before LTFT can learn.
    if(AFRDELTA >= 0.15f && eng->STFTCorrection < 0){
        eng->STFTCorrection = 0;
    }
    
    if(AFRDELTA <= -0.15f && eng->STFTCorrection > 0){
        eng->STFTCorrection = 0;
    }
}

void calculateLTFT(struct Engine *eng){

    int engineRPM = eng->RPM;
    word16 MAPKPA = eng->MAP;

    int lowerRPMBin = calculateLowerBinIdx(engineRPM, LTFTRPMAxis, LTFTRPM_BINS);   // Lower bin on X axis
    int upperRPMBin = lowerRPMBin + 1;                                              // Upper bin on X axis

    int lowerMAPBin = calculateLowerBinIdx(MAPKPA, LTFTMAPAxis, LTFTMAP_BINS);      // Lower bin on Y axis
    int upperMAPBin = lowerMAPBin + 1;                                              // Upper bin on Y axis

    float RPMWeight = (float)(engineRPM - LTFTRPMAxis[lowerRPMBin]) / (float)LTFTRPMAxis[0];  // Calculate bin bias for RPM (X)
    float MAPWeight = (float)(MAPKPA    - LTFTMAPAxis[lowerMAPBin]) / (float)LTFTMAPAxis[0];  // Calculate bin bias for MAP (Y)

    if (RPMWeight < 0.0f) RPMWeight = 0.0f;                                         // Clamp weights to the cell
    if (RPMWeight > 1.0f) RPMWeight = 1.0f;
    if (MAPWeight < 0.0f) MAPWeight = 0.0f;
    if (MAPWeight > 1.0f) MAPWeight = 1.0f;

    float topLeftShare     = (1.0 - RPMWeight) * (1.0 - MAPWeight); 
    float topRightShare    =        RPMWeight  * (1.0 - MAPWeight); 
    float bottomLeftShare  = (1.0 - RPMWeight) *        MAPWeight;         
    float bottomRightShare =        RPMWeight  *        MAPWeight;

    int topLeftCell_idx     = (lowerMAPBin * LTFTRPM_BINS) + lowerRPMBin;           // Index of top left cell
    int topRightCell_idx    = (lowerMAPBin * LTFTRPM_BINS) + upperRPMBin;           // Index of top right cell
    int bottomLeftCell_idx  = (upperMAPBin * LTFTRPM_BINS) + lowerRPMBin;           // Index of bottom left cell
    int bottomRightCell_idx = (upperMAPBin * LTFTRPM_BINS) + upperRPMBin;           // Index of bottom right cell

    float stepDirection = 0.0;

    if (eng->STFTCorrection > STFTDEADBAND){                                        // Check if we are in deadband
          stepDirection = 1.0;                                                      // Raise LTFT on addition of fuel

    } else if (eng->STFTCorrection < -STFTDEADBAND){
         stepDirection = -1.0;                                                      // Lower LTFT on subtraction of fuel
    }

    if (stepDirection != 0.0) {
        LTFT[bottomLeftCell_idx]  += (stepDirection * LTFTSCALAR * bottomLeftShare);        // Adjust each cell according to its share
        LTFT[bottomRightCell_idx] += (stepDirection * LTFTSCALAR * bottomRightShare);
        LTFT[topLeftCell_idx]     += (stepDirection * LTFTSCALAR * topLeftShare);
        LTFT[topRightCell_idx]    += (stepDirection * LTFTSCALAR * topRightShare);

        if (LTFT[bottomLeftCell_idx] > MAXLTFT) LTFT[bottomLeftCell_idx] = MAXLTFT;         // Clamp bottom left cell
        if (LTFT[bottomLeftCell_idx] < MINLTFT) LTFT[bottomLeftCell_idx] = MINLTFT;
        
        if (LTFT[bottomRightCell_idx] > MAXLTFT) LTFT[bottomRightCell_idx] = MAXLTFT;       // Clamp bottom right cell
        if (LTFT[bottomRightCell_idx] < MINLTFT) LTFT[bottomRightCell_idx] = MINLTFT;
        
        if (LTFT[topLeftCell_idx] > MAXLTFT) LTFT[topLeftCell_idx] = MAXLTFT;               // Clamp top left cell
        if (LTFT[topLeftCell_idx] < MINLTFT) LTFT[topLeftCell_idx] = MINLTFT;
        
        if (LTFT[topRightCell_idx] > MAXLTFT) LTFT[topRightCell_idx] = MAXLTFT;             // Clamp top right cell
        if (LTFT[topRightCell_idx] < MINLTFT) LTFT[topRightCell_idx] = MINLTFT;
    }

    eng->LTFTCorrection = (LTFT[bottomLeftCell_idx]  * bottomLeftShare)  +                  // Interpolate the LTFT table to get fuel correction multiplier
                          (LTFT[bottomRightCell_idx] * bottomRightShare) +
                          (LTFT[topLeftCell_idx]     * topLeftShare)     +
                          (LTFT[topRightCell_idx]    * topRightShare);
}

void correctFuelLoad(struct Engine *eng){
    if(eng->Coldstart == true && eng->COOLANT <= eng->coldCoolant){                         // Adjust fuel loading for cold starts
        eng->fuelLoad = eng->fuelLoad * coldStartEnrichment;
    } else {
        eng->Coldstart = false;
    }

    if (eng->EngineCranking == true){                                                       // Adjust fuel loading for engine cranking
        eng->fuelLoad = eng->fuelLoad * crankingEnrichment;
    }

    if (eng->fuelTrim != 1){                                                                // Adjust fuel loading for manual fuel trim
        eng->fuelLoad = eng->fuelLoad * eng->fuelTrim;
    }

    eng->fuelLoad = eng->fuelLoad * eng->toeEnrichmentMultiplier;                           // Adkist fuel loading for toe in enrichment

    eng->fuelLoad = eng->fuelLoad + (eng->fuelLoad * (eng->STFTCorrection / 100));          // Adjust fuel loading for short term O2 correction

    if (eng->Coldstart == false && eng->EngineCranking == false){                           // Adjust for long term fuel trim if engine is not cranking or cold
        eng->fuelLoad = eng->fuelLoad + (eng->fuelLoad * (eng->LTFTCorrection / 100));
    }

    eng->fuelLoad = eng->fuelLoad > maxFuelLoad ? maxFuelLoad : eng->fuelLoad;

}