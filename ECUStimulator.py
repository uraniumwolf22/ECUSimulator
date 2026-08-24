import ctypes
from multiprocessing import shared_memory
import subprocess
import time
from pathlib import Path
import posix_ipc
import math

SIMULATOR_DIR = Path(__file__).resolve().parent
ECU_EMULATOR_PATH = SIMULATOR_DIR / "ECU"
SEM_NAME = "/engineSemaphore_local"
MEMORY_NAME = "engineStateMemory_local"

# Base parameters
flywheelMass = 18.0      # mass in Kg
flywheelRadius = 0.18    # radius in m
inertia = 0.5 * flywheelMass * (flywheelRadius ** 2) # moment of inertia

staticDrag    = 15.0 
linearDrag    = 0.01
quadraticDrag = 0.00002

TPS = 0                # current TPS
RPM = 500.0
velocity = RPM * (2 * math.pi) / 60.0  # Initialize velocity

physicsTimestep = 0.1

rpm_axis = [500, 800, 1100, 1400, 1700, 2000, 2300, 2600, 2900, 3200, 3500, 3800, 4100, 4400, 4700, 5000]

torque_percent = [75, 80, 85, 88, 92, 95, 97, 99, 100, 100, 97, 92, 86, 78, 69, 60]

peakTorque = 310.0  

# Define C types

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
        ("STFTCorrection", ctypes.c_uint16),
        ("LTFTCorrection", ctypes.c_uint16),
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


def getEngineTorque(current_rpm) -> float:
    # Clamp RPM
    clamped_rpm = max(rpm_axis[0], min(rpm_axis[-1], current_rpm))
    
    for i in range(len(rpm_axis) - 1):
        if rpm_axis[i] <= clamped_rpm <= rpm_axis[i+1]:
            # Linear interpolation
            rpm_range = rpm_axis[i+1] - rpm_axis[i]
            t_range = torque_percent[i+1] - torque_percent[i]
            
            ratio = (clamped_rpm - rpm_axis[i]) / rpm_range
            percent = torque_percent[i] + (ratio * t_range)
            
            # Convert percent to nm of peak tq
            return (percent / 100.0) * peakTorque
            
    return 0.0

def PhysicsStep(currentTPS, currentRPM):
    global velocity

    # current engine drag
    engineDrag = staticDrag + (linearDrag * currentRPM) + (quadraticDrag * (currentRPM ** 2))

    # Calculate current engine torque
    currentMaxTorque = getEngineTorque(currentRPM)
    combustionTorque = currentMaxTorque * (currentTPS / 100.0)

    # net torque
    netTorque = combustionTorque - engineDrag

    currentAcceleration = netTorque / inertia
    
    velocity += currentAcceleration * physicsTimestep

    # Prevent backwards velocity
    if velocity < 0:
        velocity = 0

    return velocity * 60.0 / (2 * math.pi)

def main():
    currentRPM = 0
    currentTPS = 0
    # ECUPROC = subprocess.Popen([str(ECU_EMULATOR_PATH)], cwd=str(SIMULATOR_DIR))  # Define the process to open (ECU Simulator)
    time.sleep(0.1)                                 # Delay for process to init

    sem = posix_ipc.Semaphore(SEM_NAME)   # define semophore.  Created by ECUSimulator
    enginedata = shared_memory.SharedMemory(name=MEMORY_NAME, create=False) # Define shared memory object

    engineStatus = Engine.from_buffer(enginedata.buf)                               # Update engine status struct

    try:
        while 1:
            time.sleep(physicsTimestep)
            sem.acquire()                   # Acquire sem lock
            currentTPS = engineStatus.TPS
            currentRPM = PhysicsStep(currentTPS,currentRPM)
            engineStatus.RPM = int(currentRPM)
            sem.release()
            #print(f"CURRENT ENGINE RPM: {engineStatus.RPM}")

    finally:
        del engineStatus
        enginedata.close()
        sem.close()
        #ECUPROC.terminate()


if __name__ == "__main__":
    main()
