use eframe::{
    egui::{self, pos2, vec2},
    emath::RectTransform,
};
use std::f32::consts::{FRAC_PI_2, FRAC_PI_4, PI, TAU};

use crate::{ecu::EngineState, parameters::EngineParameter};

#[derive(Debug, Copy, Clone)]
pub enum GaugeType {
    Large,
    Small,
}

#[derive(Debug, Copy, Clone)]
pub struct Gauge {
    pub gauge_type: GaugeType,
    pub parameter: EngineParameter,
    value: f32,
}

impl Gauge {
    pub fn large(parameter: EngineParameter) -> Self {
        Self {
            gauge_type: GaugeType::Large,
            parameter,
            value: parameter.value_min,
        }
    }

    pub fn small(parameter: EngineParameter) -> Self {
        Self {
            gauge_type: GaugeType::Small,
            parameter,
            value: parameter.value_min,
        }
    }

    pub fn set_value(&mut self, value: &EngineState) -> &mut Self {
        self.value = (self.parameter.get)(value);
        self
    }

    fn large_ui(&mut self, ui: &mut egui::Ui) -> egui::Response {
        let (rect, response) = ui.allocate_exact_size(vec2(256.0, 256.0), egui::Sense::CLICK);

        let value_animated = ui.ctx().animate_value_with_time(
            response.id,
            self.value
                .clamp(self.parameter.value_min, self.parameter.value_max),
            0.125,
        );

        let angle_start = -3.0 * FRAC_PI_4;
        let angle_end = 3.0 * FRAC_PI_4;

        let needle_rotation = egui::lerp(
            angle_start..=angle_end,
            (value_animated - self.parameter.value_min)
                / (self.parameter.value_max - self.parameter.value_min),
        );

        if ui.is_rect_visible(rect) {
            let transform = RectTransform::from_to(
                egui::Rect::from_min_max(pos2(-1.0, 1.0), pos2(1.0, -1.0)),
                rect,
            );

            egui::Image::new(egui::include_image!("assets/gauge1.png")).paint_at(ui, rect);

            let font_medium =
                egui::FontId::new(12.0, egui::FontFamily::Name("Roboto Medium".into()));
            let font_bold = egui::FontId::new(16.0, egui::FontFamily::Name("Roboto Bold".into()));

            TickMarks::new(self.parameter.value_min, self.parameter.value_max)
                .angle(angle_start, angle_end)
                .radius(0.65, 0.75)
                .labels(TickLabels::All)
                .outline(TickOutline::Outside)
                .low_redline(
                    self.parameter
                        .danger_min
                        .unwrap_or(self.parameter.value_min),
                )
                .high_redline(
                    self.parameter
                        .danger_max
                        .unwrap_or(self.parameter.value_max),
                )
                .line_weight(3.0)
                .font(font_bold.clone())
                .paint(transform, ui.painter());

            ui.painter().text(
                transform.transform_pos(pos2(0.0, -0.275)),
                egui::Align2::CENTER_CENTER,
                self.parameter.abbr,
                font_bold,
                egui::Color32::LIGHT_GRAY,
            );

            ui.painter().text(
                transform.transform_pos(pos2(0.0, -0.4)),
                egui::Align2::CENTER_CENTER,
                self.parameter.unit,
                font_medium,
                egui::Color32::LIGHT_GRAY,
            );

            let counter_transform = RectTransform::from_to(
                egui::Rect::from_min_max(pos2(-1.0, -1.0), pos2(1.0, 1.0)),
                transform.transform_rect(egui::Rect::from_min_max(
                    pos2(-0.3, -0.4875),
                    pos2(0.3, -0.6375),
                )),
            );

            paint_counter(counter_transform, ui.painter(), value_animated, 6);

            egui::Image::new(egui::include_image!("assets/gauge2.png"))
                .rotate(needle_rotation, egui::Vec2::splat(0.5))
                .paint_at(ui, rect);

            egui::Image::new(egui::include_image!("assets/gauge3_bevel.png")).paint_at(ui, rect);
        }

        response
    }

    fn small_ui(&mut self, ui: &mut egui::Ui) -> egui::Response {
        let (rect, response) = ui.allocate_exact_size(vec2(128.0, 128.0), egui::Sense::CLICK);

        let value_animated = ui.ctx().animate_value_with_time(
            response.id,
            self.value
                .clamp(self.parameter.value_min, self.parameter.value_max),
            0.125,
        );

        let angle_start = -FRAC_PI_4;
        let angle_end = FRAC_PI_4;

        let needle_rotation = egui::lerp(
            angle_start..=angle_end,
            (value_animated - self.parameter.value_min)
                / (self.parameter.value_max - self.parameter.value_min),
        );

        if ui.is_rect_visible(rect) {
            let transform = RectTransform::from_to(
                egui::Rect::from_min_max(pos2(-1.0, 1.0), pos2(1.0, -1.0)),
                rect,
            );

            let tickmarks_transform = RectTransform::from_to(
                egui::Rect::from_min_max(pos2(-1.0, 1.0), pos2(1.0, -1.0)),
                rect.translate(vec2(0.0, 18.0)),
            );

            egui::Image::new(egui::include_image!("assets/gauge_small1.png")).paint_at(ui, rect);

            let font = egui::FontId::new(10.0, egui::FontFamily::Name("Roboto Medium".into()));

            TickMarks::new(self.parameter.value_min, self.parameter.value_max)
                .angle(angle_start, angle_end)
                .radius(0.5, 0.75)
                .labels(TickLabels::MinMax)
                .low_redline(
                    self.parameter
                        .danger_min
                        .unwrap_or(self.parameter.value_min),
                )
                .high_redline(
                    self.parameter
                        .danger_max
                        .unwrap_or(self.parameter.value_max),
                )
                .line_weight(2.0)
                .font(font.clone())
                .paint(tickmarks_transform, ui.painter());

            ui.painter().text(
                transform.transform_pos(egui::pos2(0.0, -0.55)),
                egui::Align2::CENTER_CENTER,
                self.parameter.abbr,
                font,
                egui::Color32::LIGHT_GRAY,
            );

            egui::Image::new(egui::include_image!("assets/gauge_small2.png"))
                .rotate(needle_rotation, vec2(0.5, 0.65))
                .paint_at(ui, rect);

            egui::Image::new(egui::include_image!("assets/gauge_small3_bevel.png"))
                .paint_at(ui, rect);
        }

        response
    }
}

impl egui::Widget for &mut Gauge {
    fn ui(self, ui: &mut egui::Ui) -> egui::Response {
        match self.gauge_type {
            GaugeType::Large => self.large_ui(ui),
            GaugeType::Small => self.small_ui(ui),
        }
    }
}

#[allow(unused)]
#[derive(Default)]
enum TickLabels {
    None,
    MinMax,
    #[default]
    All,
}

#[allow(unused)]
#[derive(Default)]
enum TickOutline {
    #[default]
    None,
    Inside,
    Outside,
    Both,
}

#[derive(Default)]
struct TickMarks {
    stroke: egui::Stroke,
    font: egui::FontId,
    labels: TickLabels,
    outline: TickOutline,

    angle_start: f32,
    angle_end: f32,
    radius_start: f32,
    radius_end: f32,
    value_min: f32,
    value_max: f32,
    danger_min: Option<f32>,
    danger_max: Option<f32>,
}

impl TickMarks {
    pub fn new(min: f32, max: f32) -> Self {
        Self {
            stroke: egui::Stroke::new(3.0, egui::Color32::WHITE),
            font: egui::FontId::proportional(12.0),

            angle_start: 0.0,
            angle_end: PI,
            radius_start: 0.5,
            radius_end: 1.0,
            value_min: min,
            value_max: max,
            ..Default::default()
        }
    }

    pub fn line_weight(self, weight: f32) -> Self {
        Self {
            stroke: egui::Stroke::new(weight, egui::Color32::WHITE),
            ..self
        }
    }

    pub fn outline(self, outline: TickOutline) -> Self {
        Self { outline, ..self }
    }

    pub fn font(self, font: egui::FontId) -> Self {
        Self { font, ..self }
    }

    pub fn angle(self, angle_start: f32, angle_end: f32) -> Self {
        Self {
            angle_start: -angle_start + FRAC_PI_2,
            angle_end: -angle_end + FRAC_PI_2,
            ..self
        }
    }

    pub fn radius(self, radius_start: f32, radius_end: f32) -> Self {
        Self {
            radius_start,
            radius_end,
            ..self
        }
    }

    pub fn labels(self, labels: TickLabels) -> Self {
        Self { labels, ..self }
    }

    pub fn low_redline(self, threshold: f32) -> Self {
        Self {
            danger_min: Some(threshold),
            ..self
        }
    }

    pub fn high_redline(self, threshold: f32) -> Self {
        Self {
            danger_max: Some(threshold),
            ..self
        }
    }

    pub fn paint(self, transform: RectTransform, painter: &egui::Painter) {
        if let Some(danger_min) = self.danger_min
            && danger_min > self.value_min
        {
            let arc_end = self.angle_start
                + (self.angle_end - self.angle_start)
                    * ((danger_min - self.value_min) / (self.value_max - self.value_min));

            let stroke = egui::epaint::PathStroke::new(8.0, egui::Color32::LIGHT_RED).inside();

            paint_arc(
                transform,
                painter,
                self.angle_start,
                arc_end,
                self.radius_end,
                stroke,
            );
        }

        if let Some(danger_max) = self.danger_max
            && danger_max < self.value_max
        {
            let arc_start = self.angle_start
                + (self.angle_end - self.angle_start)
                    * ((danger_max - self.value_min) / (self.value_max - self.value_min));

            let stroke = egui::epaint::PathStroke::new(8.0, egui::Color32::LIGHT_RED).inside();

            paint_arc(
                transform,
                painter,
                arc_start,
                self.angle_end,
                self.radius_end - 0.01,
                stroke,
            );
        }

        if matches!(self.outline, TickOutline::Outside | TickOutline::Both) {
            let stroke =
                egui::epaint::PathStroke::new(self.stroke.width, self.stroke.color).inside();
            paint_arc(
                transform,
                painter,
                self.angle_start,
                self.angle_end,
                self.radius_end + 0.01,
                stroke,
            );
        }

        if matches!(self.outline, TickOutline::Inside | TickOutline::Both) {
            let stroke =
                egui::epaint::PathStroke::new(self.stroke.width, self.stroke.color).inside();
            paint_arc(
                transform,
                painter,
                self.angle_start,
                self.angle_end,
                self.radius_start,
                stroke,
            );
        }

        let max_ticks = (self.angle_end - self.angle_start).abs() / 20.0_f32.to_radians();
        let mut ticks = heckbert(self.value_min, self.value_max, max_ticks);
        if ticks.first().unwrap() < &self.value_min {
            ticks.remove(0);
        }
        if ticks.last().unwrap() > &self.value_max {
            ticks.pop();
        }

        for (i, &tick) in ticks.iter().enumerate() {
            let factor = (tick - self.value_min) / (self.value_max - self.value_min);
            let angle = self.angle_start + (self.angle_end - self.angle_start) * factor;

            let line_start = transform.transform_pos(pol_to_cart(self.radius_start, angle));
            let line_end = transform.transform_pos(pol_to_cart(self.radius_end, angle));

            painter.line_segment([line_start, line_end], self.stroke);

            if matches!(self.labels, TickLabels::None) {
                continue;
            }

            if ![0, ticks.len() - 1].contains(&i) && matches!(self.labels, TickLabels::MinMax) {
                continue;
            }

            let text_offset = (line_end - line_start).normalized() * 12.0;

            painter.text(
                line_start - text_offset,
                egui::Align2::CENTER_CENTER,
                print_float(tick),
                self.font.clone(),
                egui::Color32::LIGHT_GRAY,
            );
        }
    }
}

// https://cran.r-project.org/package=labeling
fn heckbert(min: f32, max: f32, m: f32) -> Vec<f32> {
    let range = nicenum(max - min, false);
    let step = nicenum(range / (m - 1.0), true);

    let range_min = (min / step).floor() * step;
    let range_max = (max / step).ceil() * step;

    std::iter::successors(Some(range_min), |x| {
        let next = x + step;
        if next <= range_max { Some(next) } else { None }
    })
    .collect()
}

// https://cran.r-project.org/package=labeling
fn nicenum(x: f32, round: bool) -> f32 {
    let e = x.log10().floor();
    let f = x / 10.0_f32.powf(e);
    let nf: f32;

    if round {
        if f < 1.5 {
            nf = 1.0
        } else if f < 3.0 {
            nf = 2.0
        } else if f < 4.5 {
            nf = 4.0
        } else if f < 7.0 {
            nf = 5.0
        } else {
            nf = 10.0
        }
    } else {
        if f <= 1.0 {
            nf = 1.0
        } else if f <= 2.0 {
            nf = 2.0
        } else if f <= 4.0 {
            nf = 4.0
        } else if f <= 5.0 {
            nf = 5.0
        } else {
            nf = 10.0
        }
    }

    nf * 10.0_f32.powf(e)
}

fn print_float(value: f32) -> String {
    format!("{:.2}", value)
        .trim_end_matches('0')
        .trim_end_matches('.')
        .to_owned()
}

fn paint_counter(transform: RectTransform, painter: &egui::Painter, value: f32, digits: u8) {
    let rect = *transform.to();
    let width_per_digit = rect.width() / digits as f32;

    for i in 0..digits {
        let digit_transform = RectTransform::from_to(
            *transform.from(),
            egui::Rect::from_min_max(
                pos2(rect.max.x - (i + 1) as f32 * width_per_digit, rect.min.y),
                pos2(rect.max.x - i as f32 * width_per_digit, rect.max.y),
            ),
        );

        paint_counter_digit(
            digit_transform,
            painter,
            value * (1.0 / 10.0_f32.powi(i as i32)),
        );
    }
}

fn paint_counter_digit(transform: RectTransform, painter: &egui::Painter, value: f32) {
    let painter = painter.with_clip_rect(*transform.to());

    // painter.rect_stroke(
    //     *transform.to(),
    //     0.0,
    //     egui::Stroke::new(1.0, egui::Color32::RED),
    //     egui::StrokeKind::Inside,
    // );

    let center = value.floor();
    let offset = value - center;

    let font = egui::FontId::new(12.0, egui::FontFamily::Name("Roboto Bold".into()));

    for i in -1..=1 {
        let number = center as i32 - i;

        painter.text(
            transform.transform_pos(pos2(0.0, i as f32 * 1.5 + offset * 1.5)),
            egui::Align2::CENTER_CENTER,
            number.rem_euclid(10),
            font.clone(),
            egui::Color32::LIGHT_GRAY,
        );
    }
}

fn paint_arc(
    transform: RectTransform,
    painter: &egui::Painter,
    angle_start: f32,
    angle_end: f32,
    radius: f32,
    stroke: impl Into<egui::epaint::PathStroke>,
) {
    let point_count = (64.0 * ((angle_end - angle_start).abs() / TAU)).ceil() as i32;
    let angle_per_point = (angle_end - angle_start) / (point_count - 1) as f32;

    let points: Vec<egui::Pos2> = (0..point_count)
        .map(|point| {
            transform.transform_pos(pol_to_cart(
                radius,
                angle_start + angle_per_point * point as f32,
            ))
        })
        .collect();

    painter.line(points, stroke.into());
}

fn pol_to_cart(r: f32, angle: f32) -> egui::Pos2 {
    pos2(r * angle.cos(), r * angle.sin())
}
