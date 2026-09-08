#ifndef FUELING_H
#define FUELING_H
#include "includes.h"

void calculateToeEnrichment(struct Engine *eng);

void calculateFuelLoad  (struct Engine *eng);

void calculateSTFT      (struct Engine *eng);

void calculateLTFT      (struct Engine *eng);

void correctFuelLoad    (struct Engine *eng);

#endif