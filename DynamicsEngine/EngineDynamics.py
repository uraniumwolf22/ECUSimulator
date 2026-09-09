import time
import posix_ipc
from pathlib import Path
from math import sqrt, pi
from RPMFunctions import *
from MAPFunctions import *
from EngineTunables import *
from engineStruct import Engine
from multiprocessing import shared_memory
import random

SIMULATOR_DIR = Path(__file__).resolve().parent
ECU_EMULATOR_PATH = SIMULATOR_DIR / "ECU"
SEM_NAME = "/engineSemaphore_local"
MEMORY_NAME = "engineStateMemory_local"

# Static variables
heatRatioOfAir      = 1.4           # Specific heat ratio of air
airGasConst         = 287.05        # Ideal gas constant of dry air

# Mass flow unit conversions
manifoldVolume  = manifoldVolume / 1000     # Convert L to M^3
cylinderVolume  = cylinderVolume / 1000     # Convert L to M^3

# RPM Dynamics variables
inertia         = 0.5 * flywheelMass * (flywheelRadius ** 2) # moment of inertia
velocity        = 0 # Starting velocity

rpm_axis        = [500, 800, 1100, 1400,
                   1700, 2000, 2300, 2600,
                   2900, 3200, 3500, 3800,
                   4100, 4400, 4700, 5000]

torque_percent  = [75, 80, 85, 88,
                   92, 95, 97, 99,
                   100, 100, 97, 92,
                   86, 78, 69, 60]


def simulate_wideband_o2(target_afr, current_tps, last_tps, current_rpm, current_afr_reading, time_step):

    noise = random.gauss(0, 0.05)
    
    delta_tps = current_tps - last_tps

    transient_effect = delta_tps * 0.08 
    
    instant_exhaust_afr = target_afr + transient_effect + noise
    
    clamped_rpm = max(current_rpm, 100)
    
    base_response = 0.5

    response_rate = base_response * (clamped_rpm / 3000.0) * time_step
    
    alpha = min(1.0, max(0.01, response_rate))
    
    new_afr_reading = current_afr_reading + (instant_exhaust_afr - current_afr_reading) * alpha
    
    return new_afr_reading


def main():
    time.sleep(0.1)  # Delay for process's to init

    sem = posix_ipc.Semaphore(SEM_NAME)   
    enginedata = shared_memory.SharedMemory(name=MEMORY_NAME, create=False)
    engineStatus = Engine.from_buffer(enginedata.buf)                           

    currentRPM = 0

    last_tps = engineStatus.TPS
    simulated_afr = engineStatus.AFR_TARGET

    idlectl = 0
    try:
        while 1:
            time.sleep(timeStep)
            sem.acquire()                   

            # * Read the current states from shared memory

            currentTPS = engineStatus.TPS
            atmosphericPressure = engineStatus.AAP * 1000
            volumetric = engineStatus.VE / 100
            IAT = engineStatus.IAT
            butterflyPercentOpen = (currentTPS / 100.0) ** 2
            throttleArea = butterflyPercentOpen * throttleBodySize * dischargeCoeff

            currentAFRT = engineStatus.AFR_TARGET

            ######## RPM PHYSICS ########

            currentRPM = PhysicsStep(currentTPS, currentRPM, staticDrag + engineLoad, linearDrag,
                                     quadraticDrag, inertia,timeStep,
                                     rpm_axis, torque_percent, peakTorque)
            
            engineStatus.RPM = int(currentRPM)

            ######## MAP PHYSICS ########

            calculated_map = calculateManifoldPressure(
                engineStatus.MAP * 1000, timeStep, atmosphericPressure, 
                manifoldVolume, IAT, throttleArea, heatRatioOfAir, 
                airGasConst, cylinderVolume, currentRPM, volumetric
            )
            
            # Clamp MAP to atmosphere
            engineStatus.MAP = min(int(calculated_map / 1000), int(atmosphericPressure / 1000))

            ######## AFR NOISE ########
            simulated_afr = simulate_wideband_o2(
                            target_afr=currentAFRT, 
                            current_tps=currentTPS, 
                            last_tps=last_tps, 
                            current_rpm=currentRPM, 
                            current_afr_reading=simulated_afr,
                            time_step=timeStep
                        )
            
            engineStatus.REALAFR = simulated_afr
            
            # Update state for next loop
            last_tps = currentTPS

            ######## IDLE CONTROL LOOP ########
            if engineStatus.RPM < idleRPM + 100:
                if engineStatus.RPM < idleRPM and idlectl < 100:
                    idlectl = idlectl + 1
                    engineStatus.TPS = int(idlectl / 10)

                if engineStatus.RPM > idleRPM and idlectl > -100:
                    idlectl = idlectl - 1
                    engineStatus.TPS = int(idlectl / 10)
            

            

            sem.release()

    finally:                # * Cleanly close the SHM and SEM
        del engineStatus
        enginedata.close()
        sem.close()

if __name__ == "__main__":
    main()