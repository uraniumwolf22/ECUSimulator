from math import sqrt

########? IMPORTANT DEFINITIONS ?################################
#?  TBA     = Total area of throttle body butterfly valve       #
#?  ATM     = Atmospheric pressure in Pa                        #
#?  CVOL    = Total swept volume of all cylinders in M^3        #
#?  RPM     = Engine RPM                                        #
#?  VE      = Volumetric efficiency of engine in % (85% = 0.85) #
#?  AGC     = Air gas constant                                  #
#?  HR      = Heat ratio of air (1.4)                           #
#?  MVOL    = Total volume of manifold                          #
#?  CMAP    = Current manifold pressure in Pa                   #
#?###############################################################

def calculatePR(ATM, MAP):  #* Calculate throttle body pressure ratio
    PR = MAP / ATM
    if PR < 0.52:           #* clamp pressure ratio to simulate choked flow
        PR = 0.52
    else:
        if  PR > 1:         #* for NA motor pressure ratio is clamped at 1
            PR = 1

    return PR

def downStreamMassFlow(CVOL, RPM, VE, MAP, AGC, IAT):

    # * Calculate air flowing out of the throttle body
    massOut =  (CVOL * RPM * VE * MAP) / (2 * AGC * IAT * 60)  # Flow in Kg/s
    return massOut

def upStreamMassFlow(TBA, ATM, HR, IAT, PR, AGC):

    # * Calculate air flowing into the throttle body
    leadCoeff       = TBA * (ATM / sqrt(AGC * IAT))
    gammaMult       = (2 * HR) / (HR - 1)
    pressRatioTerm  = (PR ** (2 / HR)) - (PR ** ((HR + 1) / HR))

    massIn          = leadCoeff * sqrt(gammaMult * pressRatioTerm)
    return massIn

#* Calculate the rate change of pressure in the throttle body
def calculateRateChange(MVOL, AGC, IAT):
    return (AGC * IAT) / MVOL

#* define the first order differential dp/dt
def get_dp_dt(CMAP,ATM, MVOL, IAT, TBA, HR, AGC, CVOL,RPM, VE):
    
    PR      = calculatePR(ATM, CMAP)

    gain    = calculateRateChange(MVOL, AGC, IAT)

    massIn  = upStreamMassFlow(TBA, ATM, HR, IAT, PR, AGC)
    
    massOut = downStreamMassFlow(CVOL, RPM, VE, CMAP, AGC, IAT)

    dPdT = gain * (massIn - massOut)
    return dPdT

def calculateManifoldPressure(CMAP, dt, ATM, MVOL, IAT, TBA, HR, AGC, CVOL, RPM,VE):

    #* RK4 algorithm for solving the differential

    k1 = get_dp_dt(CMAP, ATM, MVOL, IAT, TBA, HR, AGC, CVOL, RPM, VE)
    
    k2 = get_dp_dt(CMAP + 0.5 * dt * k1, ATM, MVOL, IAT, TBA, HR, AGC, CVOL, RPM, VE)
    
    k3 = get_dp_dt(CMAP + 0.5 * dt * k2, ATM, MVOL, IAT, TBA, HR, AGC, CVOL, RPM, VE)
    
    k4 = get_dp_dt(CMAP + dt * k3, ATM, MVOL, IAT, TBA, HR, AGC, CVOL, RPM, VE)

    #* Calculate MAP at P_n+1
    futureMAP = CMAP + (dt / 6.0) * (k1 + 2*k2 + 2*k3 + k4)

    return futureMAP