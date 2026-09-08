#ifndef TUNABLES_H
#define TUNABLES_H

//////// SCHEDULER ////////

#define TPSCheck                300    // 300ms
#define STFTInterval            50     // 50ms
#define LTFTInterval            50     // 50ms
#define loopSize                100    // Number of loops in frame (defines fram size)
#define loopTime                10     // ms in a single loop

//////// TOE ENRICHMENT ////////

#define TPSDeadband             2       // Toe in enrichment deadband
#define toeInEnrichmentDecay    99      // % of enrichment to keep per itteration
#define toeEnrichment           0.20    // Toe-in enrichment multiplier

//////// FUEL SCALARS ////////

#define coldStartEnrichment     1.3     // Engine cold start enrichment
#define crankingEnrichment      1.2     // Engine cranking enrichment
#define initFuelTrim            1.0     // Manual fuel trim multiplier

//////// STFT TUNABLES ////////

#define MAXSTFT                 20      // Maximum STFT correction
#define MINSTFT                 -20     // Minimum STFT correction
#define STFTCorrectionDamper    0.5     // How much of AFR delta is intigrated into STFT

//////// LTFT TUNABLES ////////

#define LTFTSCALAR              0.1     // rate at which LTFT changes (%)
#define STFTDEADBAND            3       // % in which LTFT does not change based on STFT

//////// ENGINE PROPERTIES ////////

#define engineDisplacement      4

//////// MISC ////////
#define onBootAFR               12.5    // AFR Value to initialize with
#define CRANKING_RPM            250     // RPM below which we consider the motor to be cranking

#endif