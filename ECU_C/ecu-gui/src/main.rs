mod ecu;
mod gauge;
mod parameters;
mod templates;

use std::time::Duration;

use crate::ecu::Ecu;
use crate::parameters::{EngineParameter, PARAMETER_MAP, PARAMETERS};
use crate::templates::{GaugeTemplate, TEMPLATE_MAP, TEMPLATES};

use eframe::egui;

struct App {
    ecu: Ecu,
    template: GaugeTemplate,
    parameters: Vec<EngineParameter>,
    initialized: bool,
}

impl Default for App {
    fn default() -> Self {
        Self {
            ecu: Ecu::new(),
            template: TEMPLATES[0],
            parameters: [
                "Engine Speed",
                "Short Term Fuel Correction",
                "Long Term Fuel Correction",
                "Throttle Position",
                "Real AFR",
                "Target AFR",
                "Manifold Air Pressure",
                // Extras
                "Intake Air Temperature",
                "Coolant Temperature",
                "Volumetric Efficiency",
                "Oxygen Sensor Voltage",
                "Fuel Load",
            ]
            .iter()
            .map(|p| *PARAMETER_MAP.get(p).unwrap())
            .collect(),
            initialized: false,
        }
    }
}

impl App {
    fn load(&mut self, storage: &mut dyn eframe::Storage) {
        if let Some(data) = storage.get_string("parameters") {
            let parameters: Vec<&str> = ron::from_str(&data).unwrap();

            self.parameters = parameters
                .iter()
                .map(|p| *PARAMETER_MAP.get(p).unwrap_or(&PARAMETERS[0]))
                .collect();
        }

        if let Some(data) = storage.get_string("template") {
            let template: &str = ron::from_str(&data).unwrap();

            self.template = *TEMPLATE_MAP.get(&template).unwrap_or(&TEMPLATES[0]);
        }
    }

    fn context_menu(&mut self, ui: &mut egui::Ui, index: usize) {
        for (i, param) in (*PARAMETERS).iter().enumerate() {
            if ui.button(format!("{}. {}", i + 1, param.name)).clicked() {
                self.parameters[index] = *param;
            }
        }
    }
}

impl eframe::App for App {
    fn logic(&mut self, _ctx: &egui::Context, frame: &mut eframe::Frame) {
        if !self.initialized {
            self.load(frame.storage_mut().unwrap());
            self.initialized = true;
        }

        self.ecu.read();
    }

    fn save(&mut self, storage: &mut dyn eframe::Storage) {
        let parameters = ron::to_string(
            &self
                .parameters
                .iter()
                .map(|p| p.name)
                .collect::<Vec<&str>>(),
        )
        .unwrap();

        let template = ron::to_string(&self.template.name).unwrap();

        storage.set_string("parameters", parameters);
        storage.set_string("template", template);
    }

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        egui::CentralPanel::default().show(ui, |ui| {
            // Background
            egui::Image::new(egui::include_image!("./assets/background.png")).paint_at(
                ui,
                ui.ctx()
                    .viewport_rect()
                    .scale_from_center(ui.ctx().zoom_factor()),
            );

            // Gauge cluster
            ui.vertical_centered_justified(|ui| {
                ui.take_available_width();
                ui.horizontal_top(|ui| {
                    let gauges = (self.template.draw)(ui, &self.parameters, &self.ecu.state);

                    for (i, gauge) in gauges.iter().enumerate() {
                        gauge.context_menu(|ui| self.context_menu(ui, i));
                    }
                })
            });

            ui.add_space(8.0);

            // Bottom menu
            egui::Frame::new()
                .fill(egui::Color32::BLACK)
                .inner_margin(8.0)
                .corner_radius(8.0)
                .show(ui, |ui| {
                    ui.take_available_space();

                    ui.global_style_mut(|style| {
                        style.override_font_id = Some(egui::FontId::proportional(18.0));
                    });

                    ui.horizontal(|ui| {
                        ui.take_available_width();

                        ui.vertical(|ui| {
                            // Template switcher
                            egui::ComboBox::from_id_salt("template")
                                .selected_text(self.template.name)
                                .width(120.0)
                                .show_ui(ui, |ui| {
                                    for template in TEMPLATES {
                                        if ui
                                            .selectable_label(
                                                self.template.name == template.name,
                                                template.name,
                                            )
                                            .clicked()
                                        {
                                            self.template = *template;
                                        }
                                    }
                                });

                            // Checkboxes
                            let sw1 = ui.checkbox(&mut self.ecu.state.cold_start, "Cold Start");
                            let sw2 = ui.checkbox(&mut self.ecu.state.engine_cranking, "Cranking");

                            if sw1.changed() || sw2.changed() {
                                self.ecu.write();
                            }
                        });

                        ui.add_space(8.0);

                        // Sliders
                        let mut should_write = false;
                        for param in (*PARAMETERS).iter() {
                            ui.vertical(|ui| {
                                ui.set_min_width(60.0);

                                let min = param.value_min;
                                let max = param.value_max;

                                let mut value = (param.get)(&self.ecu.state);

                                ui.spacing_mut().slider_width = 64.0;

                                if ui
                                    .add(egui::Slider::new(&mut value, min..=max).vertical())
                                    .changed()
                                {
                                    (param.set)(&mut self.ecu.state, value);
                                    should_write = true;
                                }

                                ui.label(param.abbr);
                            });
                        }
                        if should_write {
                            self.ecu.write();
                        }
                    });
                });
        });

        ui.ctx().request_repaint_after(Duration::from_millis(100));
    }
}

fn main() {
    let options = eframe::NativeOptions {
        centered: true,
        persist_window: false,
        viewport: eframe::egui::ViewportBuilder::default()
            .with_app_id("ecu-gui")
            .with_inner_size([1024.0 + 64.0, 256.0 + 16.0 + 128.0 + 16.0])
            .with_resizable(true)
            .with_active(true),
        ..Default::default()
    };

    _ = eframe::run_native(
        "ECU GUI",
        options,
        Box::new(|cc| {
            egui_extras::install_image_loaders(&cc.egui_ctx);
            load_fonts(&cc.egui_ctx);
            Ok(Box::new(App::default()))
        }),
    );
}

fn load_fonts(ctx: &egui::Context) {
    let mut fonts = egui::FontDefinitions::default();
    fonts.font_data.insert(
        "Roboto Medium".into(),
        std::sync::Arc::new(egui::FontData::from_static(include_bytes!(
            "./assets/Roboto-Medium.ttf"
        ))),
    );
    fonts.font_data.insert(
        "Roboto Bold".into(),
        std::sync::Arc::new(egui::FontData::from_static(include_bytes!(
            "./assets/Roboto-Bold.ttf"
        ))),
    );

    fonts.families.insert(
        egui::FontFamily::Name("Roboto Medium".into()),
        vec!["Roboto Medium".into()],
    );
    fonts.families.insert(
        egui::FontFamily::Name("Roboto Bold".into()),
        vec!["Roboto Bold".into()],
    );

    ctx.set_fonts(fonts);
}
