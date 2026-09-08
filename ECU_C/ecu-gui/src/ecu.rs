use libc::{
    MAP_SHARED, O_RDWR, PROT_READ, PROT_WRITE, mmap, munmap, sem_close, sem_open, sem_post, sem_t,
    sem_wait, shm_open,
};
use std::{ffi::CStr, ptr::null_mut};

const SEM_DIR: &CStr = c"/engineSemaphore_local";
const MEM_DIR: &CStr = c"/engineStateMemory_local";

#[repr(C)]
#[derive(Default, Debug, Copy, Clone)]
pub struct EngineState {
    pub displacement_per_rev: i32,
    pub cold_coolant: i32,

    pub tps: u16,
    pub rpm: u16,
    pub map: u16,
    pub aap: u16,
    pub iat: u16,
    pub ox_voltage: u16,
    pub coolant: u16,
    pub fuel_trim: u16,

    pub fuel_load: u16,
    pub ve: u16,
    pub stft_correction: f32,
    pub ltft_correction: f32,
    pub real_afr: f32,
    pub afr_target: f32,
    pub toe_enrichment_multiplier: f32,

    pub engine_cranking: bool,
    pub cold_start: bool,

    pub last_tps_value: u16,
    pub tife: u16,
    pub afr_integral_accumulator: i32,
}

pub struct Ecu {
    pub state: EngineState,
    mem: *mut EngineState,
    sem: *mut sem_t,
}

impl Ecu {
    pub fn new() -> Self {
        Self {
            state: EngineState::default(),
            mem: Self::open_memory(),
            sem: Self::open_semaphore(),
        }
    }

    pub fn read(&mut self) {
        unsafe {
            sem_wait(self.sem);
            self.state = self.mem.read_volatile();
            sem_post(self.sem);
        }
    }

    pub fn write(&mut self) {
        unsafe {
            sem_wait(self.sem);
            self.mem.write_volatile(self.state);
            sem_post(self.sem);
        }
    }

    fn open_semaphore() -> *mut sem_t {
        unsafe { sem_open(SEM_DIR.as_ptr(), libc::O_RDWR) }
    }

    fn close_sempaphore(sem: *mut sem_t) {
        unsafe {
            sem_close(sem);
        }
    }

    fn open_memory() -> *mut EngineState {
        unsafe {
            let shm = shm_open(MEM_DIR.as_ptr(), O_RDWR, 0666);
            let size = size_of::<EngineState>();
            let mem = mmap(null_mut(), size, PROT_READ | PROT_WRITE, MAP_SHARED, shm, 0);
            mem.cast::<EngineState>()
        }
    }

    fn close_memory(mem: *mut EngineState) {
        unsafe {
            munmap(mem.cast(), size_of::<EngineState>());
        }
    }
}

impl Drop for Ecu {
    fn drop(&mut self) {
        Self::close_sempaphore(self.sem);
        Self::close_memory(self.mem);
    }
}
