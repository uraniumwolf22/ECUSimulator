/*
This is the C based fueling-only ECU developed by Logan Ross <3
*/

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include "tables.h"
#include "semaphore.h"

#define DEBUG

#define loopSize 100   // Number of loops in frame at 10ms per loop - frame is 1s

// Tuning values
#define TPSCheck 20                // Rate at which the scheduler runs the TPS check - 200ms
#define TPSDeadband 2               // Band at which toe-in enrichment does not occur
#define toeInEnrichmentDecay 95     // % of enrichment to keep per itteration

#define onBootAFR 12.5              // AFR Value to initialize with
#define toeEnrichment 1.5           // Toe-in enrichment multiplier
#define coldStartEnrichment 1.3     // Engine cold start enrichment
#define crankingEnrichment 1.2      // Engine cranking enrichment
#define initFuelTrim 1.0            // Manual fuel trim multiplier

#define MAXSTFT 2000    // Maximum STFT correction
#define MINSTFT -2000   // Minimum STFT correction

#define LTFTSCALAR 0.1  // rate at which LTFT changes (%)
#define STFTDEADBAND 3  // % in which LTFT does not change based on STFT

#define CRANKING_RPM 500

const word16 DISPLACEMENT = 4;                            // Engine displacement in L
const word16 DISPLACEMENT_PER_REV = DISPLACEMENT / 2;     // This will be pre-calculated and stored in ROM

//  Utility functions
word16 KtoFConversion(int F){           // This is used to convert F for the user to the internal representation in Kelvin.
    return ((F-32) * 5 / 9) + 273.15;
}

long long get_time_in_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    // Convert seconds and nanoseconds to total milliseconds
    return ((long long)ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
}

//TODO Scheduler timing helper function

void calculateToeEnrichment(struct Engine *eng){                // Calculated the toe in enrichment based on the speed of the TPS sensor
  int16_t deltaTPS = eng->TPS - eng->lastTPSValue;              // Calculate the delta of the TPS sensor over time
  //printf("TPS_RATE: %d\n",deltaTPS);
  if(deltaTPS >= TPSDeadband){                                   // Make sure the delta is not outside of the deadband
    eng->toeEnrichmentMultiplier = deltaTPS * toeEnrichment;
    printf("TIE EVENT REGISTERED WITH DELTA OF %d\n",deltaTPS);
    //printf("TEM: %d\n", eng->toeEnrichmentMultiplier);


  }

  if (eng->toeEnrichmentMultiplier > 0){                        // Well obviously...
    eng->toeEnrichmentMultiplier = (eng->toeEnrichmentMultiplier * toeInEnrichmentDecay) / 100;
    //printf("TEM: %d\n", eng->toeEnrichmentMultiplier);

  }

  if (eng->toeEnrichmentMultiplier < 2){
    eng->toeEnrichmentMultiplier = 1;
  }

  eng->lastTPSValue = eng->TPS;

}

void calculateAFR(struct Engine *eng){      // Fetches current AFR with lookup table
    int MAPBin = MAP_BINS - 1;
    int RPMBin = RPM_BINS - 1;

    word16 MAPKPA = eng->MAP;

    for(int i = MAP_BINS - 1; i >= 0; i--){
        if(MAPKPA >= mapAxis[i]){
            MAPBin = (i == 0) ? 0 : i - 1;
            break;
        }
    }

    for(int i = RPM_BINS - 1; i >= 0; i--){
        if(eng->RPM >= rpmAxis[i]){
            RPMBin = (i == 0) ? 0 : i - 1;
            break;
        }
    }

    int AFRIndex = (MAPBin * RPM_BINS) + RPMBin;
    // printf("\eCURRENT AFRT IS: %d\nRPM_BIN: %d\nMAP_BIN: %d\n MAP: %d\n RPM: %d\n",afrTable[AFRIndex],RPMBin,MAPBin,MAPKPA,eng->RPM);

    eng->AFR_TARGET = afrTable[AFRIndex];
}

void calculateVE(struct Engine *eng){        // Calculate the engine VE
    int MAPBin = MAP_BINS - 1;
    int RPMBin = RPM_BINS - 1;

    word16 MAPKPA = eng->MAP;

    for(int i = MAP_BINS - 1 ; i >= 0; i--){     // Calculate bin of MAP
        if(MAPKPA  >= mapAxis[i] ){         // Check if we are in the correct bin
            MAPBin = (i == 0) ? 0 : i - 1;  // Make sure the bin isnt negative
            break;
        }
    }

    for(int i = RPM_BINS - 1; i >= 0; i--){     // Calculate bin of RPM
        if(eng->RPM >= rpmAxis[i] ){        // Check if we are in the correct bin
            RPMBin = (i == 0) ? 0 : i - 1;  // Make sure the bin isnt nagative
            break;
        }
    }

    int VEIndex = (MAPBin * RPM_BINS) + RPMBin;     // Calculate VE value location in 1D map using calculated bins
    // printf("\eCURRENT VE IS: %d\nRPM_BIN: %d\nMAP_BIN: %d\n MAP: %d\n RPM: %d\n",VETable[VEIndex],RPMBin,MAPBin,MAPKPA,eng->RPM);
    eng->VE = VETable[VEIndex];                     // Return VE value fetched from table

}


void calculateFuelLoad(struct Engine *eng){         // Calculate engine theoretical fuel loading

    word32 flowPerMinTh = eng->RPM * eng->displacementPerRev;                   // Calculate the theoretical air flow per minute
    word32 Density = (eng->MAP * 10 * 1000 ) / (287 * eng->IAT);                // Calculate the current air density and scale by 1000 to keep percision
    word32 realAirFlow = (flowPerMinTh * eng->VE) / 100;                        // Calculate the true air flow factoring in Volumetric efficiency
    word32 realAirMass = realAirFlow * Density;                                 // Calculate the real air mass entering the engine still scaled
    word16 fuelLoad = ((realAirMass * 10) / (eng->AFR_TARGET)/60);                   // Calculate fuel load and scale back to Grams/Second.


    eng->fuelLoad = (word16)fuelLoad;     // cast to int16 and return fuel load in grams per minute
    //printf("\eRPM: %d\nMAP: %d\nDensity: %d\n RAF: %d\nRAM: %d\nFL: %d\n",
    //    eng->RPM,eng->MAP,Density,realAirFlow,realAirMass,fuelLoad);
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
    //eng->fuelLoad = eng->fuelLoad * eng->STFTCorrection;    // Adjust for STFT

    // LTFT probably should not update during cranking or cold start
    //eng->fuelLoad = eng->fuelLoad * eng->LTFTCorrection;    // Adjust for LTFT

}

void calculateSTFT(struct Engine *eng){
    int AFRDELTA = (int)(eng->REALAFR * 10) - (int)(eng->AFR_TARGET * 10) * 10;  // Calculate AFR delta and convert to intager eg 14.7 = 147

    int P = AFRDELTA >> 1;      // Calculate porportional,  by dividing the delta by 2

    int IntigralStep = AFRDELTA >> 3;   // Calculate the intigral for the current step

    eng->AFRIntigralAccumulator += IntigralStep;    // Apply intigral to the accumulator

    if (eng->AFRIntigralAccumulator > MAXSTFT){     // Check if MAXSTFT is hit
        eng->AFRIntigralAccumulator = MAXSTFT;      
    } else if (eng->AFRIntigralAccumulator < MINSTFT){  // Check in MINSTFT is hit
        eng->AFRIntigralAccumulator = MINSTFT;
    }
    eng->STFTCorrection = (P + eng->AFRIntigralAccumulator) / 100; // Set the oxygen correction and scale back.
} 

int calculateLowerBinIdx(int value, const uint16_t axis[], int numBins){
    int currentBinIdx = 0;
    for(int i = 0; i < numBins; i++){
        if (value < axis[i]){
            currentBinIdx = i - 1;
        }
    }
    return currentBinIdx;
}

void calculateLTFT(struct Engine *eng){
    // X is RPM Y is KPA
    // X coordinate is the current RPM bin you are in same for Y but with Kpa
    int engineRPM = eng->RPM;
    int MAPKPA = eng->MAP / 100;

    // Find upper and lower bins of RPM (X)
    int lowerRPMBin = calculateLowerBinIdx(engineRPM, LTFTRPMAxis, LTFTRPM_BINS);   // Lower bin on X axis
    int upperRPMBin = lowerRPMBin + 1;                                              // Upper bin on X axis

    // Find upper and lower bins of MAP (Y)
    int lowerMAPBin = calculateLowerBinIdx(MAPKPA, LTFTMAPAxis, LTFTMAP_BINS);      // Lower bin on Y axis
    int upperMAPBin = lowerMAPBin + 1;                                              // Upper bin on Y axis

    float RPMWeight = (engineRPM - LTFTRPMAxis[lowerRPMBin]) / LTFTRPMAxis[0];           // Calculate bin bias for RPM (X)
    float MAPWeight = (MAPKPA - LTFTMAPAxis[lowerMAPBin]) / LTFTMAPAxis[0];              // Calculate bin bias for MAP (Y)

    float topLeftShare = (1 - RPMWeight) * MAPWeight;                       // Calculate % shares for each cell
    float topRightShare = RPMWeight * MAPWeight;
    float bottomLeftShare = (1 - RPMWeight) * (1 - MAPWeight);
    float bottomRightShare = RPMWeight * (1 - MAPWeight);

    // Calculate cell indexes
    int topLeftCell_idx = (lowerMAPBin * RPM_BINS) + lowerRPMBin;           // Index of top left cell
    int topRightCell_idx = (lowerMAPBin * RPM_BINS) + upperRPMBin;          // Index of top right cell
    int bottomLeftCell_idx = (upperMAPBin * RPM_BINS) + lowerRPMBin;        // Index of bottom left cell
    int bottomRightCell_idx = (upperMAPBin * RPM_BINS) + upperMAPBin;       // Index of bottom right cell

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


void initValues(struct Engine *eng, struct ECUSchedule *sched){

    eng->AFR_TARGET = onBootAFR;                        // Define AFR Target on boot
    eng->coldCoolant = 100;                             // Tempurature where under is considered cold starting
    eng->displacementPerRev = DISPLACEMENT_PER_REV;     // Set engine displacement
    eng->fuelTrim = initFuelTrim;
    eng->IAT = KtoFConversion(70);                             // Set intake air tempurature to 70F on boot

    sched->ECULoopSize = loopSize;                      // Set the total loop size before the logic repeats
    sched->ECUStep = 0;
    sched->crankCheckInterval = 5;                      // Interval at which the ECU checks for cranking
    sched->TPSCheckInterval = TPSCheck;
    sched->timeLastChecked = get_time_in_ms();

}

void performStep(struct Engine *eng, struct ECUSchedule *sched){
    if (sched->ECUStep % sched->crankCheckInterval == 0){           // Check engine cranking status on interval
        if (eng->RPM < CRANKING_RPM){
            eng->EngineCranking = true;
        } else {eng->EngineCranking = false;}
    }

    if (eng->COOLANT < eng->coldCoolant){               // On init determine if engine is cold. If so, set the flag.
        eng->Coldstart = true;
    }

    if (sched->ECUStep % sched->TPSCheckInterval == 0){             // Calculate Toe in Enrichment on schedule
        //printf("Enrichment Calculated");
        calculateToeEnrichment(eng);
    }

    calculateVE(eng);                   // Update volumetric efficiency

    calculateAFR(eng);                  // Calculate VE

    calculateFuelLoad(eng);             // Calculate the base fuel load

    calculateSTFT(eng);

    calculateLTFT(eng);

    correctFuelLoad(eng);               // Adjust fuel load for transient conditions


    long long currentTime = get_time_in_ms();                // Fetch the current time

    if(currentTime >= sched->timeLastChecked + 10){          // Check if 10MS has passed
        sched->ECUStep++;
        //printf("Increased\n");
        sched->timeLastChecked = currentTime;
        
    }

    //printf("Current Looptime: %d\n",currentTime - sched->timeLastChecked);

    if(sched->ECUStep >= sched->ECULoopSize){               // Check if we are at the end of our loop
        sched->ECUStep = 0;                                 // Reset loop
    }
    

}

struct Engine engineInstance = {0};         // Instantiate instance of engine values
struct ECUSchedule schedule;                // Instantiate the ECU Schedule

const char *name = "engineStateMemory_local";    // Define the location of the shared memory for engine struct
const char *engineSemName = "/engineSemaphore_local"; // Define location for engine shared memory semaphore

const int SIZE = sizeof(engineInstance);
static sem_t *engineSem = NULL;
static int sharedEngineMem = -1;
static void *mappedPtr = NULL;

static void cleanup_ipc(void){
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

static void handle_signal(int signalNumber){
    (void)signalNumber;
    cleanup_ipc();
    _exit(0);
}

int main(){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    atexit(cleanup_ipc);

    initValues(&engineInstance, &schedule);                                 // Initialize the ECU Values

    sem_unlink(engineSemName);
    shm_unlink(name);

    engineSem = sem_open(engineSemName, O_CREAT, 0666, 1);                    // Create the semaphore

    if (engineSem == SEM_FAILED){
        perror("failed to open semophore!!! Exiting");
        return 1;
    }

    sharedEngineMem = shm_open(name, O_CREAT | O_RDWR, 0666);           // Create the shared memory
    if (sharedEngineMem == -1){
        perror("failed to open shared memory!!! Exiting");
        sem_close(engineSem);
        engineSem = NULL;
        sem_unlink(engineSemName);
        return 1;
    }

    if (ftruncate(sharedEngineMem, SIZE) == -1){
        perror("failed to resize shared memory!!! Exiting");
        close(sharedEngineMem);
        sharedEngineMem = -1;
        sem_close(engineSem);
        engineSem = NULL;
        sem_unlink(engineSemName);
        shm_unlink(name);
        return 1;
    }

    mappedPtr = mmap(0, SIZE, PROT_WRITE, MAP_SHARED, sharedEngineMem, 0);  // create a pointer to shared memory
    if (mappedPtr == MAP_FAILED){
        perror("failed to map shared memory!!! Exiting");
        close(sharedEngineMem);
        sharedEngineMem = -1;
        sem_close(engineSem);
        engineSem = NULL;
        sem_unlink(engineSemName);
        shm_unlink(name);
        return 1;
    }

    struct Engine *sharedData = (struct Engine *)mappedPtr;                       // define object pointer with type of engine struct and cast onto shared memory
    *sharedData = engineInstance;                                           // update shared memory with real ECU instance

    int lastTPS = 0;    
    while(1){
        sem_wait(engineSem);        // Lock SEM for data update

        engineInstance = *sharedData;              // Pull latest shared state back into local ECU copy
        performStep(&engineInstance, &schedule);   // Update ECU

        *sharedData = engineInstance;              // Update shared data

        sem_post(engineSem);        // Unlock SEM for other programs
	    //debug(&engineInstance);
    }

    // while(1){
    //     for(schedule.ECUStep = 0; schedule.ECUStep < schedule.ECULoopSize; schedule.ECUStep++){ // Iterate through the ECU loop
    //         performStep(&engineInstance, &schedule);                                            // take a single step of the loop
    //     }

    //}
}
