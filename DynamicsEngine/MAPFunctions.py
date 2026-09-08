from math import sqrt

def calculatePR(atmosphericPressure, MAP):  # Calculates the pressure ratio the intake
    PR = MAP / atmosphericPressure
    if PR < 0.52:           # Set lower bound of pressure ratio to simulate choked flow
        PR = 0.52

    if PR > 1:              # NA motor so pressure ratio can not be above 1
        PR = 1
    return PR

def downStreamMassFlow(cylinderVolume, engineRPM, volumetric,
                       MAP, airGasConst, IAT):           # Calculate the mass flow downstream of throttle body

    # Using the speed density equasion calculate the flow coming out of the throttle body
    massOut =  (cylinderVolume * engineRPM * volumetric * MAP) / (2 * airGasConst * IAT * 60)  # Flow in Kg/s
    return massOut

def upStreamMassFlow(throttleArea, atmosphericPressure, heatRatioOfAir, IAT, PR, airGasConst):

    # Using compressable flow equasions calculate the air flowing into the TB
    leadCoeff   = throttleArea * (atmosphericPressure / sqrt(airGasConst * IAT))
    gammaMult   = (2 * heatRatioOfAir) / (heatRatioOfAir - 1)
    pressRatioTerm  = (PR ** (2 / heatRatioOfAir)) - (PR ** ((heatRatioOfAir + 1) / heatRatioOfAir))

    massIn      = leadCoeff * sqrt(gammaMult * pressRatioTerm)
    return massIn

def calculateRateChange(manifoldVolume, airGasConst, IAT):
    return (airGasConst * IAT) / manifoldVolume

def getDpDt(currentMAP,atmosphericPressure, manifoldVolume,
            IAT, throttleArea, heatRatioOfAir, airGasConst,
            cylinderVolume,engineRPM, volumetric):
    
    PR      = calculatePR(atmosphericPressure, currentMAP)

    gain    = calculateRateChange(manifoldVolume, airGasConst, IAT)

    massIn  = upStreamMassFlow(throttleArea, atmosphericPressure, 
                               heatRatioOfAir, IAT, PR, airGasConst)
    
    massOut = downStreamMassFlow(cylinderVolume, engineRPM,
                                 volumetric, currentMAP, airGasConst, IAT)

    dPdT = gain * (massIn - massOut)
    return dPdT

def calculateManifoldPressure(currentMAP, dt, atmosphericPressure, manifoldVolume,
                              IAT, throttleArea, heatRatioOfAir, airGasConst, cylinderVolume,
                              engineRPM,volumetric):

    k1 = getDpDt(currentMAP, atmosphericPressure, manifoldVolume, IAT,
                 throttleArea, heatRatioOfAir, airGasConst, cylinderVolume,
                 engineRPM, volumetric)
    
    k2 = getDpDt(currentMAP + 0.5 * dt * k1, atmosphericPressure, manifoldVolume, IAT,
                 throttleArea, heatRatioOfAir, airGasConst, cylinderVolume,
                 engineRPM, volumetric)
    
    k3 = getDpDt(currentMAP + 0.5 * dt * k2, atmosphericPressure, manifoldVolume, IAT,
                 throttleArea, heatRatioOfAir, airGasConst, cylinderVolume,
                 engineRPM, volumetric)
    
    k4 = getDpDt(currentMAP + dt * k3, atmosphericPressure, manifoldVolume, IAT,
                 throttleArea, heatRatioOfAir, airGasConst, cylinderVolume,
                 engineRPM, volumetric)

    futureMAP = currentMAP + (dt / 6.0) * (k1 + 2*k2 + 2*k3 + k4)
    return futureMAP