from math import pi
import random
from EngineTunables import tqVariation
def getEngineTorque(current_rpm, rpm_axis, torque_percent, peakTorque) -> float:
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

def PhysicsStep(currentTPS, currentRPM, staticDrag, linearDrag, quadraticDrag, inertia, physicsTimestep, rpm_axis, torque_percent, peakTorque):
    velocity = currentRPM * (2 * pi) / 60.0
    # current engine drag
    engineDrag = staticDrag + (linearDrag * currentRPM) + (quadraticDrag * (currentRPM ** 2))

    engineDrag = engineDrag + random.uniform(-(peakTorque * tqVariation), (peakTorque * tqVariation))

    # Calculate current engine torque
    currentMaxTorque = getEngineTorque(currentRPM, rpm_axis, torque_percent,peakTorque)
    combustionTorque = currentMaxTorque * (currentTPS / 100.0)

    # Net torque
    netTorque = combustionTorque - engineDrag

    currentAcceleration = netTorque / inertia
    
    velocity += currentAcceleration * physicsTimestep
# 
    # Prevent backwards velocity
    if velocity < 0:
        velocity = 0

    return velocity * 60.0 / (2 * pi)