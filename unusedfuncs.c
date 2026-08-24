// PI controller broke.  Going with different approach
void calculateSTFT(struct Engine *eng){
    int AFRDELTA = (eng->REALAFR) - (int)(eng->AFR_TARGET);  // Calculate AFR delta and convert to intager eg 14.7 = 147
    printf("AFR DELTA: %d\n",AFRDELTA);

    int P = AFRDELTA / 2;      // Calculate porportional,  by dividing the delta by 2

    int IntigralStep = AFRDELTA >> 3;   // Calculate the intigral for the current step

    eng->AFRIntigralAccumulator += IntigralStep;    // Apply intigral to the accumulator

    printf("ACCUMULATOR: %d\n",eng->AFRIntigralAccumulator);
    if (eng->AFRIntigralAccumulator > MAXSTFT){     // Check if MAXSTFT is hit
        eng->AFRIntigralAccumulator = MAXSTFT;          
    } else if (eng->AFRIntigralAccumulator < MINSTFT){  // Check in MINSTFT is hit
        eng->AFRIntigralAccumulator = MINSTFT;
    }
    eng->STFTCorrection = (P + eng->AFRIntigralAccumulator); // Set the oxygen correction and scale back.
    printf("STFTCORR: %d",eng->STFTCorrection);
} 