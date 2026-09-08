import ctypes

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