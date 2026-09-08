/* 
This is the C based fueling-only ECU developed by Logan Ross <3

   ___     ___    _   _    ___     ___   __  __  
  | __|   / __|  | | | |  / __|   |_ _| |  \/  | 
  | _|   | (__   | |_| |  \__ \    | |  | |\/| | 
  |___|   \___|   \___/   |___/   |___| |_|__|_| 
_|"""""|_|"""""|_|"""""|_|"""""|_|"""""|_|"""""| 
"`-0-0-'"`-0-0-'"`-0-0-'"`-0-0-'"`-0-0-'"`-0-0-' 
                                                       
*/

#include "includes.h"
#include "tunables.h"
#include "fueling.h"
#include "utils.h"
#include "air.h"

void initValues(struct Engine *eng, struct ECUSchedule *sched){

    eng->AFR_TARGET                 = onBootAFR;                    // * AFR target on startup
    eng->coldCoolant                = 100;                          // * Tempurature under which is considered cold start
    eng->displacementPerRev         = engineDisplacement / 2;       // * Engine displacement per revolution
    eng->fuelTrim                   = initFuelTrim;
    eng->IAT                        = KtoFConversion(68);           // * Set intake air temp to room tempurature
    eng->toeEnrichmentMultiplier    = 1;
    eng->REALAFR                    = eng->AFR_TARGET;              // * Set the REALAFR to its initial value
    eng->AAP                        = 101;

    sched->ECULoopSize              = loopSize;                     // * Set the total scheduler loop size
    sched->ECUStep                  = 0;                            // * What step the ECU is on in the loop
    sched->crankCheckInterval       = 5;                            // * Cranking scheduler interval
    sched->TPSCheckInterval         = TPSCheck / 10;                // * TPS Check interval
    sched->loopIntervalTimeBase     = get_time_in_ms();             // * Set the base time the the current system time
    sched->STFTCheckInterval        = STFTInterval / 10;            // * STFT Scheduler interval

    sched->STFTCheckLock            = false;                        // * STFT Scheduler lock
    sched->CrankCheckLock           = false;                        // * Cranking scheduler lock
    sched->TPSCheckLock             = false;                        // * TPS Check scheduler lock

}

void performStep(struct Engine *eng, struct ECUSchedule *sched){
    /*
    ####################################
    ######## CHECK CRANKING RPM ########
    ####################################
    */

    if (sched->ECUStep % sched->crankCheckInterval == 0 
        && sched->CrankCheckLock == false){

        if (eng->RPM < CRANKING_RPM){
            eng->EngineCranking = true;
        }else{
            eng->EngineCranking = false;
        }

        sched->CrankCheckLock = true;
    }

    /*
    ####################################
    ######## CHECK COOLANT TEMP ########
    ####################################
    */

    if (eng->COOLANT < eng->coldCoolant){
        eng->Coldstart = true;
    }
    
    /*
    ######################################
    ######## CHECK TOE ENRICHMENT ########
    ######################################
    */

    if (sched->ECUStep % sched->TPSCheckInterval == 0 && sched->TPSCheckLock == false){             // Calculate Toe in Enrichment on schedule
        if (eng->toeEnrichmentMultiplier > 1){
            eng->toeEnrichmentMultiplier = eng->toeEnrichmentMultiplier * (toeInEnrichmentDecay / 100.0);
        }
        calculateToeEnrichment(eng);   // ! Disabled until I can figure out whats going on
        sched->TPSCheckLock = true;
    }

    calculateVE(eng);                   // * Update volumetric efficiency

    calculateAFR(eng);                  // * Calculate air to fuel ratio

    calculateFuelLoad(eng);             // * Calculate the theoretical fuel load

    /*
    ##################################
    ######## RECALCULATE STFT ########
    ##################################
    */

    if (sched->ECUStep % sched->STFTCheckInterval == 0 && 
        sched->STFTCheckLock == false){

        calculateSTFT(eng);
        calculateLTFT(eng);

        sched->STFTCheckLock = true;
    }
    
    correctFuelLoad(eng);               // Adjust fuel load for transient conditions


    /*
    ##################################
    ######## UPDATE SCHEDULER ########
    ##################################
    */

    long long currentTime = get_time_in_ms();                // Fetch the current time

    if(currentTime - sched->loopIntervalTimeBase >= loopTime){          // Check if 10MS has passed
        sched->ECUStep++;
        //printf("Increased\n");
        sched->TPSCheckLock = false;
        sched->CrankCheckLock = false;
        sched->STFTCheckLock = false;
        sched->loopIntervalTimeBase = currentTime;
    }

    if(sched->ECUStep >= sched->ECULoopSize){               // Check if we are at the end of our loop
        sched->ECUStep = 0;                                 // Reset loop
    }
}

int main(){
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    atexit(cleanup_ipc);

    initValues(&engineInstance, &schedule);                                 // Initialize the ECU Values

    sem_unlink(engineSemName);      // Remove any previous SEM or SHM objects
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

    while(1){
        sem_wait(engineSem);        // Lock SEM for data update

        engineInstance = *sharedData;              // Pull latest shared state back into local ECU copy
        performStep(&engineInstance, &schedule);   // Update ECU

        *sharedData = engineInstance;              // Update shared data

        sem_post(engineSem);        // Unlock SEM for other programs
    }
}