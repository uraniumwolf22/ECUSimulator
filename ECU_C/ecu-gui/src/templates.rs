use std::collections::HashMap;
use std::sync::LazyLock;

use eframe::egui;

use crate::ecu::EngineState;
use crate::gauge::Gauge;
use crate::parameters::EngineParameter;

#[derive(Copy, Clone)]
pub struct GaugeTemplate {
    pub name: &'static str,
    pub draw: fn(&mut egui::Ui, &[EngineParameter], &EngineState) -> Vec<egui::Response>,
}

pub static TEMPLATES: &[GaugeTemplate] = &[
    GaugeTemplate {
        name: "Default",
        draw: |ui, params, state| {
            let mut res: Vec<egui::Response> = vec![ui.response(); 12];

            res[0] = ui.add(Gauge::large(params[0]).set_value(state));

            ui.vertical(|ui| {
                res[1] = ui.add(Gauge::small(params[1]).set_value(state));
                res[2] = ui.add(Gauge::small(params[2]).set_value(state));
            });

            res[3] = ui.add(Gauge::large(params[3]).set_value(state));

            ui.vertical(|ui| {
                res[4] = ui.add(Gauge::small(params[4]).set_value(state));
                res[5] = ui.add(Gauge::small(params[5]).set_value(state));
            });

            res[6] = ui.add(Gauge::large(params[6]).set_value(state));

            res
        },
    },
    GaugeTemplate {
        name: "Left Right",
        draw: |ui, params, state| {
            let mut res: Vec<egui::Response> = vec![ui.response(); 12];

            res[0] = ui.add(Gauge::large(params[0]).set_value(state));
            res[3] = ui.add(Gauge::large(params[3]).set_value(state));
            res[6] = ui.add(Gauge::large(params[6]).set_value(state));

            ui.vertical(|ui| {
                res[1] = ui.add(Gauge::small(params[1]).set_value(state));
                res[2] = ui.add(Gauge::small(params[2]).set_value(state));
            });

            ui.vertical(|ui| {
                res[4] = ui.add(Gauge::small(params[4]).set_value(state));
                res[5] = ui.add(Gauge::small(params[5]).set_value(state));
            });

            res
        },
    },
    GaugeTemplate {
        name: "Centered",
        draw: |ui, params, state| {
            let mut res: Vec<egui::Response> = vec![ui.response(); 12];

            ui.add_space(64.0);

            res[0] = ui.add(Gauge::large(params[0]).set_value(state));

            ui.vertical(|ui| {
                res[1] = ui.add(Gauge::small(params[1]).set_value(state));
                res[2] = ui.add(Gauge::small(params[2]).set_value(state));
            });

            ui.vertical(|ui| {
                res[4] = ui.add(Gauge::small(params[4]).set_value(state));
                res[5] = ui.add(Gauge::small(params[5]).set_value(state));
            });

            ui.vertical(|ui| {
                res[7] = ui.add(Gauge::small(params[7]).set_value(state));
                res[8] = ui.add(Gauge::small(params[8]).set_value(state));
            });

            res[6] = ui.add(Gauge::large(params[6]).set_value(state));

            res
        },
    },
    GaugeTemplate {
        name: "All Large",
        draw: |ui, params, state| {
            let mut res: Vec<egui::Response> = vec![ui.response(); 12];

            res[0] = ui.add(Gauge::large(params[0]).set_value(state));
            res[3] = ui.add(Gauge::large(params[3]).set_value(state));
            res[6] = ui.add(Gauge::large(params[6]).set_value(state));
            res[9] = ui.add(Gauge::large(params[9]).set_value(state));

            res
        },
    },
    GaugeTemplate {
        name: "All Small",
        draw: |ui, params, state| {
            let mut res: Vec<egui::Response> = vec![ui.response(); 12];

            ui.add_space(128.0 - 6.0);

            ui.vertical(|ui| {
                res[0] = ui.add(Gauge::small(params[0]).set_value(state));
                res[3] = ui.add(Gauge::small(params[3]).set_value(state));
            });

            ui.vertical(|ui| {
                res[6] = ui.add(Gauge::small(params[6]).set_value(state));
                res[9] = ui.add(Gauge::small(params[9]).set_value(state));
            });

            ui.vertical(|ui| {
                res[1] = ui.add(Gauge::small(params[1]).set_value(state));
                res[2] = ui.add(Gauge::small(params[2]).set_value(state));
            });

            ui.vertical(|ui| {
                res[4] = ui.add(Gauge::small(params[4]).set_value(state));
                res[5] = ui.add(Gauge::small(params[5]).set_value(state));
            });

            ui.vertical(|ui| {
                res[7] = ui.add(Gauge::small(params[7]).set_value(state));
                res[8] = ui.add(Gauge::small(params[8]).set_value(state));
            });

            ui.vertical(|ui| {
                res[10] = ui.add(Gauge::small(params[10]).set_value(state));
                res[11] = ui.add(Gauge::small(params[11]).set_value(state));
            });

            res
        },
    },
];

pub static TEMPLATE_MAP: LazyLock<HashMap<&str, GaugeTemplate>> = LazyLock::new(|| {
    let mut map = HashMap::new();

    for template in TEMPLATES {
        map.insert(template.name, *template);
    }

    map
});
