# This Python file uses the following encoding: utf-8
import gc
import sys
import ctypes
import subprocess
import time
from pathlib import Path
from multiprocessing import shared_memory

import posix_ipc
from PySide6.QtWidgets import QApplication, QWidget

from ui_form import Ui_ECUGUI

SIMULATOR_DIR = Path(__file__).resolve().parent.parent
ECU_EMULATOR_PATH = SIMULATOR_DIR / "ECU"

sem = None
enginedata = None
engineStatus = None
ECUPROC = None

SEM_NAME = "/engineSemaphore_local"
MEMORY_NAME = "engineStateMemory_local"

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
        ("REALAFR", ctypes.c_uint16),
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


class ECUGUI(QWidget):                  # ECU Widget
    def __init__(self, parent=None):    # Init the UI
        super().__init__(parent)
        self.ui = Ui_ECUGUI()
        self.ui.setupUi(self)
        self.ui.coolantSlider.valueChanged.connect(self.updateCoolantTemp)  # Connect coolant slider to update function on value change


    def updateCoolantTemp(self):                        # Send the new coolant temp to the ECU
        current_value = self.ui.coolantSlider.value()   # Fetch current slider value
        if sem is None or engineStatus is None:         # Return if engineStatus or semaphore is broken
            return

        sem.acquire()                                   # Get SEM Lock
        engineStatus.COOLANT = current_value            # Set shared memory to new value
        sem.release()                                   # Release SEM lock

        print(f"Engine Coolant is at {engineStatus.COOLANT}")   


def launch_ecu():                                                                   # Launch the ECU Process
    if not ECU_EMULATOR_PATH.exists():                                              # Check if ECU executable exists
        raise FileNotFoundError(f"ECU executable not found... {ECU_EMULATOR_PATH}") # 

    return subprocess.Popen([str(ECU_EMULATOR_PATH)], cwd=str(SIMULATOR_DIR))


def wait_for_shared_resources(timeout=5.0):         # Function that waits for resources to initialize
    deadline = time.monotonic() + timeout           # Define timeout delta
    last_error = None

    while time.monotonic() < deadline:
        try:
            sem_obj = posix_ipc.Semaphore(SEM_NAME)                                     # Establish SEM connection
            shared_mem = shared_memory.SharedMemory(name=MEMORY_NAME, create=False)     # Establish shared memory
            return sem_obj, shared_mem
        
        except (FileNotFoundError, posix_ipc.ExistentialError, OSError) as exc:
            last_error = exc
            time.sleep(0.1)

    raise RuntimeError(f"Too much time passed.  broke bc: {last_error}") from last_error


def release_shared_resources():             # Break shared memory and semaphore
    global sem, enginedata, engineStatus

    if engineStatus is not None:            # Remove internal engine status structure
        engineStatus = None

    if enginedata is not None:
        try:
            enginedata.close()              # Close shared memory
        except Exception:
            pass
        enginedata = None

    if sem is not None:                     # Close semaphore
        try:
            sem.close()
        except Exception:
            pass
        sem = None

    try:
        posix_ipc.Semaphore(SEM_NAME).unlink()  # Unlink the SEM
    except Exception:
        pass

    try:
        shared_memory.SharedMemory(name=MEMORY_NAME, create=False).unlink()
    except Exception:
        pass

    gc.collect()    # idk maybe this will help not break shi


if __name__ == "__main__":
    try:
        ECUPROC = launch_ecu()      # Lauch the ECU instance

        sem, enginedata = wait_for_shared_resources()       # Open the shared resources

        engineStatus = Engine.from_buffer(enginedata.buf)   # Point internal struct to shared memory

        app = QApplication(sys.argv)                        # IDK what these do they came with QT.  should figure that out.
        widget = ECUGUI()
        widget.show()               # as its name implies probably

        try:
            sys.exit(app.exec())   # close QT application

        finally:
            if ECUPROC is not None and ECUPROC.poll() is None:
                ECUPROC.terminate() # Force ECU to terminate
                try:
                    ECUPROC.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    ECUPROC.kill()  # If ECU proc does not play nice,  kill.
                    ECUPROC.wait(timeout=2)

            release_shared_resources()  # Release the shared resources

    except Exception as exc:
        print(f"ECU GUI Failed: {exc}") # General error.
        sys.exit(1)
