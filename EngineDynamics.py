import math

# Base parameters
flywheelMass = 18.0      # mass in Kg
flywheelRadius = 0.18    # radius in m
inertia = 0.5 * flywheelMass * (flywheelRadius ** 2) # moment of inertia

staticDrag    = 15.0 
linearDrag    = 0.01
quadraticDrag = 0.00002

TPS = 10                # current TPS
RPM = 500.0
velocity = RPM * (2 * math.pi) / 60.0  # Initialize velocity

physicsTimestep = 0.1

rpm_axis = [500, 800, 1100, 1400, 1700, 2000, 2300, 2600, 2900, 3200, 3500, 3800, 4100, 4400, 4700, 5000]

torque_percent = [75, 80, 85, 88, 92, 95, 97, 99, 100, 100, 97, 92, 86, 78, 69, 60]

peakTorque = 310.0  

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

def PhysicsStep():
    global RPM, velocity

    # current engine drag
    engineDrag = staticDrag + (linearDrag * RPM) + (quadraticDrag * (RPM ** 2))

    # Calculate current engine torque
    currentMaxTorque = getEngineTorque(RPM)
    combustionTorque = currentMaxTorque * (TPS / 100.0)

    # net torque
    netTorque = combustionTorque - engineDrag

    currentAcceleration = netTorque / inertia
    
    velocity += currentAcceleration * physicsTimestep

    # Prevent backwards velocity
    if velocity < 0:
        velocity = 0

    RPM = velocity * 60.0 / (2 * math.pi)

print("Time | RPM")
print("----------------")
for i in range(100):
    PhysicsStep()
    current_time = round((i + 1) * physicsTimestep, 1)
    print(f"{current_time:5}s  | {int(RPM)}")