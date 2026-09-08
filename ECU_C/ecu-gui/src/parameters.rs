use std::{collections::HashMap, sync::LazyLock};

use crate::ecu::EngineState;

#[derive(Debug, Copy, Clone)]
pub struct EngineParameter {
    pub name: &'static str,
    pub abbr: &'static str,
    pub unit: &'static str,

    pub value_min: f32,
    pub value_max: f32,
    pub danger_min: Option<f32>,
    pub danger_max: Option<f32>,

    pub get: fn(&EngineState) -> f32,
    pub set: fn(&mut EngineState, f32),
}

pub static PARAMETERS: &[EngineParameter] = &[
    EngineParameter {
        name: "Throttle Position",
        abbr: "TPS",
        unit: "%",

        value_min: 0.0,
        value_max: 100.0,
        danger_min: None,
        danger_max: None,

        get: |e| e.tps as f32,
        set: |e, v| e.tps = v as u16,
    },
    EngineParameter {
        name: "Engine Speed",
        abbr: "RPM",
        unit: "x1000",

        value_min: 0.0,
        value_max: 6.0,
        danger_min: None,
        danger_max: Some(4.5),

        get: |e| e.rpm as f32 / 1000.0,
        set: |e, v| e.rpm = (v * 1000.0) as u16,
    },
    EngineParameter {
        name: "Manifold Air Pressure",
        abbr: "MAP",
        unit: "kPa",

        value_min: 0.0,
        value_max: 120.0,
        danger_min: None,
        danger_max: None,

        get: |e| e.map as f32,
        set: |e, v| e.map = v as u16,
    },
    EngineParameter {
        name: "Ambient Air Pressure",
        abbr: "AAP",
        unit: "kPa",

        value_min: 70.0,
        value_max: 110.0,
        danger_min: None,
        danger_max: None,

        get: |e| e.aap as f32,
        set: |e, v| e.aap = v as u16,
    },
    EngineParameter {
        name: "Intake Air Temperature",
        abbr: "IAT",
        unit: "°F",

        value_min: -10.0,
        value_max: 170.0,
        danger_min: None,
        danger_max: Some(116.0),

        get: |e| k_to_f(e.iat as f32),
        set: |e, v| e.iat = f_to_k(v) as u16,
    },
    EngineParameter {
        name: "Coolant Temperature",
        abbr: "TEMP",
        unit: "°F",

        value_min: 0.0,
        value_max: 250.0,
        danger_min: None,
        danger_max: Some(220.0),

        get: |e| e.coolant as f32,
        set: |e, v| e.coolant = v as u16,
    },
    EngineParameter {
        name: "Oxygen Sensor Voltage",
        abbr: "OX",
        unit: "V",

        value_min: 0.0,
        value_max: 5.0,
        danger_min: None,
        danger_max: None,

        get: |e| e.ox_voltage as f32,
        set: |e, v| e.ox_voltage = v as u16,
    },
    EngineParameter {
        name: "Volumetric Efficiency",
        abbr: "VE",
        unit: "%",

        value_min: 0.0,
        value_max: 110.0,
        danger_min: None,
        danger_max: None,

        get: |e| e.ve as f32,
        set: |e, v| e.ve = v as u16,
    },
    EngineParameter {
        name: "Short Term Fuel Correction",
        abbr: "STFT",
        unit: "%",

        value_min: -50.0,
        value_max: 50.0,
        danger_min: Some(-20.0),
        danger_max: Some(20.0),

        get: |e| e.stft_correction as f32,
        set: |e, v| e.stft_correction = v as f32,
    },
    EngineParameter {
        name: "Long Term Fuel Correction",
        abbr: "LTFT",
        unit: "%",

        value_min: -50.0,
        value_max: 50.0,
        danger_min: Some(-20.0),
        danger_max: Some(20.0),

        get: |e| e.ltft_correction as f32,
        set: |e, v| e.ltft_correction = v as f32,
    },
    EngineParameter {
        name: "Real AFR",
        abbr: "AFR R",
        unit: "",

        value_min: 0.0,
        value_max: 20.0,
        danger_min: Some(12.0),
        danger_max: Some(16.0),

        get: |e| e.real_afr,
        set: |e, v| e.real_afr = v,
    },
    EngineParameter {
        name: "Target AFR",
        abbr: "AFR T",
        unit: "",

        value_min: 10.0,
        value_max: 20.0,
        danger_min: Some(12.0),
        danger_max: Some(16.0),

        get: |e| e.afr_target,
        set: |e, v| e.afr_target = v,
    },
    EngineParameter {
        name: "Fuel Load",
        abbr: "LOAD",
        unit: "g/s",

        value_min: 0.0,
        value_max: 15.0,
        danger_min: None,
        danger_max: None,

        get: |e| e.fuel_load as f32 / 100.0,
        set: |e, v| e.fuel_load = (v * 100.0) as u16,
    },
];

pub static PARAMETER_MAP: LazyLock<HashMap<&str, EngineParameter>> = LazyLock::new(|| {
    let mut map = HashMap::new();

    for param in PARAMETERS {
        map.insert(param.name, *param);
    }

    map
});

fn k_to_f(k: f32) -> f32 {
    (k - 273.15) * (9.0 / 5.0) + 32.0
}

fn f_to_k(f: f32) -> f32 {
    (f - 32.0) * (5.0 / 9.0) + 273.15
}
