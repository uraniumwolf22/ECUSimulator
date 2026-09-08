/*
This is the C based fueling-only ECU developed by Logan Ross <3
*/

#include "includes.h"
#include "tunables.h"
#include "fueling.h"
#include "utils.h"
#include "air.h"

const word16 DISPLACEMENT_PER_REV = engineDisplacement / 2;     // This will be pre-calculated and stored in ROM

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
  eng->toeEnrichmentMultiplier = TEM;   // Set the engine toe enrichment

  eng->lastTPSValue = eng->TPS;         // Set the current TPS to the last, for the next loop

}

void initValues(struct Engine *eng, struct ECUSchedule *sched){

    eng->AFR_TARGET = onBootAFR;                        // Define AFR Target on boot
    eng->coldCoolant = 100;                             // Tempurature where under is considered cold starting
    eng->displacementPerRev = DISPLACEMENT_PER_REV;     // Set engine displacement
    eng->fuelTrim = initFuelTrim;
    eng->IAT = KtoFConversion(70);                             // Set intake air tempurature to 70F on boot
    eng->toeEnrichmentMultiplier = 1;
    eng->REALAFR = eng->AFR_TARGET;
    eng->AAP = 101;

    sched->ECULoopSize = loopSize;                      // Set the total loop size before the logic repeats
    sched->ECUStep = 0;
    sched->crankCheckInterval = 5;                      // Interval at which the ECU checks for cranking
    sched->TPSCheckInterval = TPSCheck / 10;
    sched->loopIntervalTimeBase = get_time_in_ms();
    sched->STFTCheckInterval = STFTInterval / 10;

    sched->STFTCheckLock = false;                       // Init the scheduler locks
    sched->CrankCheckLock = false;
    sched->TPSCheckLock = false;

}

void performStep(struct Engine *eng, struct ECUSchedule *sched){
    

    if (sched->ECUStep % sched->crankCheckInterval == 0 && sched->CrankCheckLock == false){           // Check engine cranking status on interval
        if (eng->RPM < CRANKING_RPM){
            eng->EngineCranking = true;
        } else {eng->EngineCranking = false;}
        sched->CrankCheckLock = true;
    }

    if (eng->COOLANT < eng->coldCoolant){               // On init determine if engine is cold. If so, set the flag.
        eng->Coldstart = true;
    }

    if (sched->ECUStep % sched->TPSCheckInterval == 0 && sched->TPSCheckLock == false){             // Calculate Toe in Enrichment on schedule
        //calculateToeEnrichment(eng);   //Disabled until I can figure out whats going on
        sched->TPSCheckLock = true;
    }

    calculateVE(eng);                   // Update volumetric efficiency

    calculateAFR(eng);                  // Calculate VE

    calculateFuelLoad(eng);             // Calculate the base fuel load

    if (sched->ECUStep % sched->STFTCheckInterval == 0 && sched->STFTCheckLock == false){
        //printf("STFT TRIGGERED\n");
        calculateSTFT(eng);
        calculateSTFT(eng);

        sched->STFTCheckLock = true;
    }

    // CHANGE ME TO OWN SCHED
    // if (sched->ECUStep % sched->STFTCheckInterval == 0 && sched->STFTCheckLock == false){
    //     //printf("STFT TRIGGERED\n");
    //     calculateSTFT(eng);
    //     sched->STFTCheckLock = true;
    // }

    calculateLTFT(eng);

    correctFuelLoad(eng);               // Adjust fuel load for transient conditions


    long long currentTime = get_time_in_ms();                // Fetch the current time

    if(currentTime - sched->loopIntervalTimeBase >= loopTime){          // Check if 10MS has passed
        sched->ECUStep++;
        //printf("Increased\n");
        sched->TPSCheckLock = false;
        sched->CrankCheckLock = false;
        sched->STFTCheckLock = false;
        sched->loopIntervalTimeBase = currentTime;
        
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