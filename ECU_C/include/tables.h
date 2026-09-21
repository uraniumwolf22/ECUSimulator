#ifndef TABLES_H
#define TABLES_H
//////// TABLE SIZES ////////

#define MAP_BINS 16
#define RPM_BINS 16

#define LTFTRPM_BINS 16
#define LTFTMAP_BINS 16

#include "includes.h"

extern const uint16_t mapAxis[MAP_BINS];

extern const uint16_t rpmAxis[RPM_BINS];

extern const uint8_t VETable[MAP_BINS * RPM_BINS];

extern const float afrTable[MAP_BINS * RPM_BINS];

extern const uint16_t LTFTRPMAxis[LTFTRPM_BINS];

extern const uint16_t LTFTMAPAxis[LTFTMAP_BINS];

extern float LTFT[256];

#endif