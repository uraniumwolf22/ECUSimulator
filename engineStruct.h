#ifndef ENGINE_STRUCT_H
#define ENGINE_STRUCT_H

#include "includes.h"

typedef uint16_t word16;
typedef uint32_t word32;

struct Engine {
    // Static engine variables
    int displacementPerRev; // Define the engine displacement in L
    int coldCoolant;        // Threshold under which coolant is considered cold

    // Dynamic engine variables
    // Sensors
    word16 TPS;             // Throttle position sensor
    word16 RPM;             // Engine speed in RPM
    word16 MAP;             // Manifold air pressure represented as KPa
    word16 AAP;             // Ambient pressure represented as KPa
    word16 IAT;             // Intake air temperature measured in K
    word16 OXVoltage;       // Oxygen sensor voltage
    word16 COOLANT;         // Coolant tempurature in F
    word16 fuelTrim;        // Manual fuel trim

    // Calculated values
    word16 fuelLoad;                // Current fuel load
    word16 VE;                      // Current volumetric efficiency
    float STFTCorrection;             // Fuel correction value based on POSTAFR and AFR delta
    float LTFTCorrection;
    float REALAFR;                 // AFR detected by the oxygen sensor
    float AFR_TARGET;               // Current target AFR
    float toeEnrichmentMultiplier;  // Enrichment based off the rate of change of the TPS sensor

    // Engine Flags
    bool EngineCranking;    // Cranking state of engine
    bool Coldstart;         // Is the ECU Booting into a cold start condition

    // Utility variables
    word16 lastTPSValue;        // TPS Value on the last loop
    word16 TIFE;                // toe-in fuel enrichment
    int AFRIntigralAccumulator; // accumulator for the STFT intigral

};

struct ECUSchedule {        // Schedule certian unimportant heavy tasks to leave headroom for primary calculations
    int ECULoopSize;        // Size of steps per loop of the scheduler
    int ECUStep;            // Current step of the loop the ECU is on
    
    int crankCheckInterval; // How often we check for a cranking condition
    int TPSCheckInterval;   // How often we check the throttle position sensor
    int STFTCheckInterval;  // How often we check for stft changes

    bool CrankCheckLock;    // Locks the crank check function until the ECUStep increases so the function isnt repeated multiple times per ms
    bool TPSCheckLock;
    bool STFTCheckLock;

    long long loopIntervalTimeBase; // The current system time when the last ECUStep was incrimented
};

// void debug(struct Engine *engine) {
//   printf("\e[H\nTPS: %d\nRPM: %d\nMAP: %d\nAAP: %d\nIAT: %d\nOXVoltage: %d\nVE: %d\nAFRTAR: %f\nCOOLANT: %d\nfuelTrim: %d\nTOEENR: %f\nSTFT: %f\nRAFR: %f\n",
//          engine->TPS, engine->RPM, engine->MAP, engine->AAP, engine->IAT,
//          engine->OXVoltage, engine->VE, engine->AFR_TARGET, engine->COOLANT, engine->fuelTrim,engine->toeEnrichmentMultiplier,engine->STFTCorrection,engine->REALAFR);
// }

#endif