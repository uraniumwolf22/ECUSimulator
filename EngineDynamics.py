import ctypes
from multiprocessing import shared_memory
import subprocess
import time
from pathlib import Path
import posix_ipc
from math import sqrt, pi
from MAPFunctions import *
from RPMFunctions import *
from EngineTunables import *

SIMULATOR_DIR = Path(__file__).resolve().parent
ECU_EMULATOR_PATH = SIMULATOR_DIR / "ECU"
SEM_NAME = "/engineSemaphore_local"
MEMORY_NAME = "engineStateMemory_local"

# GLOBAL PARAMETERS

timeStep = 0.01

# Static variables
heatRatioOfAir      = 1.4           # Specific heat ratio of air
airGasConst         = 287.05        # Ideal gas constant of dry air

# Mass flow unit conversions
manifoldVolume  = manifoldVolume / 1000     # Convert L to M^3
cylinderVolume  = cylinderVolume / 1000     # Convert L to M^3

# RPM Dynamics variables
inertia         = 0.5 * flywheelMass * (flywheelRadius ** 2) # moment of inertia
velocity        = 0 # Starting velocity

rpm_axis = [500, 800, 1100, 1400,
            1700, 2000, 2300, 2600,
            2900, 3200, 3500, 3800,
            4100, 4400, 4700, 5000]

torque_percent = [75, 80, 85, 88,
                  92, 95, 97, 99,
                  100, 100, 97, 92,
                  86, 78, 69, 60]




# Define the engine struct modeled after tables.h
class Engine(ctypes.Structure):
    _fields_ = [
        # Static engine variables
        ("displacementPerRev", ctypes.c_int),
        ("coldCoolant", ctypes.c_int),

        # Sensors
        ("TPS", ctypes.c_uint16),
        ("RPM", ctypes.c_uint16),
        ("MAP", ctypes.c_uint16),
        ("AAP", ctypes.c_uint16),
        ("IAT", ctypes.c_uint16),
        ("OXVoltage", ctypes.c_uint16),
        ("COOLANT", ctypes.c_uint16),
        ("fuelTrim", ctypes.c_uint16),

        # Calculated Values
        ("fuelLoad", ctypes.c_uint16),
        ("VE", ctypes.c_uint16),
        ("STFTCorrection", ctypes.c_float),
        ("LTFTCorrection", ctypes.c_float),
        ("REALAFR", ctypes.c_float),
        ("AFR_TARGET", ctypes.c_float),
        ("toeEnrichmentMultiplier", ctypes.c_float),

        # Engine flags
        ("EngineCranking", ctypes.c_bool),
        ("Coldstart", ctypes.c_bool),

        # Utility Variables
        ("lastTPSValue", ctypes.c_uint16),
        ("TIFE", ctypes.c_uint16),
        ("AFRIntigralAccumulator", ctypes.c_uint16)
    ]

def main():
    time.sleep(0.1)  # Delay for process's to init

    sem = posix_ipc.Semaphore(SEM_NAME)   
    enginedata = shared_memory.SharedMemory(name=MEMORY_NAME, create=False)
    engineStatus = Engine.from_buffer(enginedata.buf)                           

    currentRPM = 0

    try:
        while 1:
            time.sleep(timeStep)
            sem.acquire()                   

            # Read the current states
            currentTPS = engineStatus.TPS
            atmosphericPressure = engineStatus.AAP * 1000
            volumetric = engineStatus.VE / 100
            IAT = engineStatus.IAT
            butterflyPercentOpen = (currentTPS / 100.0) ** 2
            throttleArea = butterflyPercentOpen * throttleBodySize * dischargeCoeff
            # RPM Physics
            currentRPM = PhysicsStep(currentTPS, currentRPM, staticDrag, linearDrag,
                                     quadraticDrag, inertia,timeStep,
                                     rpm_axis, torque_percent, peakTorque)
            
            engineStatus.RPM = int(currentRPM)

            # 3. MAP Physics
            calculated_map = calculateManifoldPressure(
                engineStatus.MAP * 1000, timeStep, atmosphericPressure, 
                manifoldVolume, IAT, throttleArea, heatRatioOfAir, 
                airGasConst, cylinderVolume, currentRPM, volumetric
            )
            
            # Clamp to atmosphere
            engineStatus.MAP = min(int(calculated_map / 1000), int(atmosphericPressure / 1000))
            
            sem.release()

    finally:
        del engineStatus
        enginedata.close()
        sem.close()

if __name__ == "__main__":
    main()