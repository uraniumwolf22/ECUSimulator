######## PLANT FUEL ERROR TABLE ########
# % fuel the true engine needs vs what the ECU VE/AFR tables assume.
# Same axes as the ECU LTFT table so learned LTFT should converge here.
# Positive = lean plant (needs more fuel), Negative = rich plant.
#
# Magnitudes are kept ABOVE STFTDEADBAND (±3%) in most cells so STFT
# is forced past the deadband and LTFT actually learns.

PLANT_RPM_BINS = 16
PLANT_MAP_BINS = 16

plantRPMAxis = [375, 750, 1125, 1500, 1875, 2250, 2625, 3000,
                3375, 3750, 4125, 4500, 4875, 5250, 5625, 6000]

plantMAPAxis = [8, 16, 24, 32, 40, 48, 56, 64,
                72, 80, 88, 96, 104, 112, 120, 128]

# 16x16  [MAP row][RPM col]   values in %
plantFuelError = [
    # 375   750  1125  1500  1875  2250  2625  3000  3375  3750  4125  4500  4875  5250  5625  6000
    [ 12,   11,   11,   10,   10,    9,    9,    9,   10,   10,   11,   11,   12,   12,   13,   13],  # 8 kPa
    [ 11,   11,   10,   10,    9,    9,    8,    8,    9,    9,   10,   10,   11,   11,   12,   12],  # 16
    [ 11,   10,   10,    9,    9,    8,    8,    8,    8,    9,    9,   10,   10,   11,   11,   12],  # 24
    [ 10,   10,    9,    9,    8,    8,    7,    7,    8,    8,    9,    9,   10,   10,   11,   11],  # 32
    [ 10,    9,    9,    8,    8,    7,    7,    7,    7,    8,    8,    9,    9,   10,   10,   11],  # 40
    [  9,    9,    8,    8,    7,    7,    6,    6,    7,    7,    8,    8,    9,    9,   10,   10],  # 48
    [  9,    8,    8,    7,    7,    6,    6,    5,    6,    7,    7,    8,    8,    9,    9,   10],  # 56
    [  8,    8,    7,    7,    6,    6,    5,    5,    6,    6,    7,    7,    8,    8,    9,    9],  # 64
    [  8,    7,    7,    6,    6,    5,    5,    4,    5,    6,    6,    7,    7,    8,    8,    9],  # 72
    [  7,    7,    6,    6,    5,    5,    4,    4,    5,    5,    6,    6,    7,    7,    8,    8],  # 80
    [  5,    4,    4,    3,    2,   -4,   -5,   -6,   -5,   -4,    3,    4,    5,    6,    7,    8],  # 88
    [  4,    3,    2,   -4,   -5,   -6,   -7,   -8,   -7,   -5,   -4,    3,    4,    5,    6,    7],  # 96
    [  8,    7,    6,    5,   -4,   -5,   -6,   -7,   -5,    4,    6,    8,    9,   10,   11,   12],  # 104
    [  9,    8,    7,    6,    5,   -4,   -5,   -5,    4,    6,    8,    9,   10,   11,   12,   13],  # 112
    [ 10,    9,    8,    7,    6,    5,    4,    5,    6,    8,    9,   10,   11,   12,   13,   14],  # 120
    [ 11,   10,    9,    8,    7,    6,    5,    6,    8,    9,   10,   11,   12,   13,   14,   15],  # 128
]


def calculateLowerBinIdx(value, axis):                      # Calculate the lower bin index for a value in an array
    currentBinIdx = 0
    for i in range(len(axis) - 1, -1, -1):
        if value >= axis[i]:
            currentBinIdx = i                               # Axis point we are at or past
            break
    if currentBinIdx > len(axis) - 2:                       # Leave room for upper = lower + 1
        currentBinIdx = len(axis) - 2
    return currentBinIdx


def lookupPlantFuelError(rpm, map_kpa):                     # Bilinear interpolate plant fuel error (%)
    lowerRPMBin = calculateLowerBinIdx(rpm, plantRPMAxis)
    upperRPMBin = lowerRPMBin + 1

    lowerMAPBin = calculateLowerBinIdx(map_kpa, plantMAPAxis)
    upperMAPBin = lowerMAPBin + 1

    RPMWeight = (rpm - plantRPMAxis[lowerRPMBin]) / float(plantRPMAxis[0])
    MAPWeight = (map_kpa - plantMAPAxis[lowerMAPBin]) / float(plantMAPAxis[0])

    # Clamp weights in case we land past the last bin edge
    RPMWeight = max(0.0, min(1.0, RPMWeight))
    MAPWeight = max(0.0, min(1.0, MAPWeight))

    topLeft     = plantFuelError[lowerMAPBin][lowerRPMBin]
    topRight    = plantFuelError[lowerMAPBin][upperRPMBin]
    bottomLeft  = plantFuelError[upperMAPBin][lowerRPMBin]
    bottomRight = plantFuelError[upperMAPBin][upperRPMBin]

    topLeftShare     = (1.0 - RPMWeight) * (1.0 - MAPWeight)
    topRightShare    =        RPMWeight  * (1.0 - MAPWeight)
    bottomLeftShare  = (1.0 - RPMWeight) *        MAPWeight
    bottomRightShare =        RPMWeight  *        MAPWeight

    return (topLeft     * topLeftShare)     + \
           (topRight    * topRightShare)    + \
           (bottomLeft  * bottomLeftShare)  + \
           (bottomRight * bottomRightShare)
