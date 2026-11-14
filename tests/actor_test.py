"""2Dと3Dで同じ個体契約を通し、接触・削除時期・異常列を実VMで検査する。"""
from pathlib import Path
import subprocess
import sys

compiler, executable, destination = sys.argv[1:]
output = Path(destination)
output.mkdir(parents=True, exist_ok=True)
base = """
struct Actor { previous: VECTOR; position: VECTOR; color: vec3; radius: f32; angle: f32; visual: u32; mask: u32; }
struct Shot { previous: VECTOR; position: VECTOR; }
state actors: pool<Actor> = pool(8);
state shots: pool<Shot> = pool(8);
state threats: pool<Shot> = pool(8);
state hp: i32 = 5;
state health: i32 = 100;
state contacted: bool = false;
export fn configure_world(a: VECTOR, b: VECTOR, c: VECTOR, d: VECTOR) {}
export fn configure_rules(a: i32, b: i32, c: f32, d: f32, e: f32) {}
export fn initialize(preset: u32) {
    let a = actors.push(Actor { previous: ZERO, position: ZERO, color: vec3(1.0, 0.5, 0.0),
        radius: 0.2, angle: 0.0, visual: 0, mask: 1 });
    let b = actors.push(Actor { previous: ZERO, position: ZERO, color: vec3(0.0, 1.0, 0.5),
        radius: 0.2, angle: 0.0, visual: 1, mask: 3 });
}
export fn step(direction: VECTOR, fire: bool, dt: f32) {
    let added = shots.push(Shot { previous: LEFT, position: RIGHT });
}
export fn apply_hits(group: u32, first: u32, mask: u32) { health = -999; }
export fn finish_step() { shots.clear(); if contacted { foreach a in actors { if a.visual == 1 { remove a; } } } }
export fn player_position() -> VECTOR { return ZERO; }
export fn player_previous() -> VECTOR { return ZERO; }
export fn player_hp() -> i32 { return hp; }
export fn target_hp() -> i32 { return health; }
export fn result() -> u32 { return 0; }
export fn audio_events() -> u32 { return 0; }
export fn dropped_shots() -> u32 { return 0; }
export fn actor_api_version() -> u32 { return 1; }
export fn actor_apply_hit(shot: u32, target: u32) { health = health - 10 - i32(target); }
export fn actor_apply_contact(target: u32) { hp = hp + 1; contacted = true; }
export fn status_api_version() -> u32 { return 1; }
export fn status_count() -> u32 { return 1; }
export fn status_label(index: u32) -> u32 { return 0; }
export fn status_value(index: u32) -> i32 { return health; }
export fn status_maximum(index: u32) -> i32 { return 100; }
"""
fixtures = {
    "valid": (base, "valid"),
    "version": (base.replace("version() -> u32 { return 1;", "version() -> u32 { return 3;"), "unsupported"),
    "capacity": (base.replace("pool<Actor> = pool(8)", "pool<Actor> = pool(1025)"), "exceeds 1024"),
    "column": (base.replace("color", "tint"), "UnknownField"),
    "radius": (base.replace("radius: 0.2", "radius: -1.0"), "out of range"),
    "mask": (base.replace("mask: 3", "mask: 4"), "out of range"),
    "visual-limit": (base.replace("visual: 0", "visual: 18"), "out of range"),
    "structure": (base.replace("health = health - 10 - i32(target);", "actors.clear();"), "defer structural changes"),
    "missing": (base[:base.index("export fn actor_apply_contact")], "ExportNotFound"),
    "status-count": (base.replace("count() -> u32 { return 1;", "count() -> u32 { return 5;"), "exceeds four"),
    "status-label": (base.replace("label(index: u32) -> u32 { return 0;", "label(index: u32) -> u32 { return 256;"), "out of range"),
}
# 任意の表示・窓契約も実VMで型と値域を検査し、旧actor版の既定opacityを保つ。
presentation = """
export fn presentation_api_version() -> u32 { return 1; }
export fn presentation_scene() -> u32 { return 3; }
export fn presentation_cue() -> u32 { return 10; }
export fn presentation_cue_seconds() -> f32 { return 2.5; }
"""
windows = """
export fn window_api_version() -> u32 { return 1; }
export fn window_view_count() -> u32 { return 3; }
export fn window_scale() -> f32 { return 0.75; }
export fn window_view_size() -> vec2 { return vec2(12.0, 6.75); }
export fn window_view_center(index: u32) -> vec2 { return vec2(-16.0, 0.0); }
export fn play_preset_count() -> u32 { return 10; }
"""
alpha = base.replace("color: vec3;", "color: vec3; opacity: f32;").replace("radius: 0.2", "opacity: 0.4, radius: 0.2").replace("actor_api_version() -> u32 { return 1;", "actor_api_version() -> u32 { return 2;")
fixtures.update({
    "alpha": (alpha, "valid"),
    "alpha-range": (alpha.replace("opacity: 0.4", "opacity: 1.1"), "out of range"),
    "alpha-column": (base.replace("actor_api_version() -> u32 { return 1;", "actor_api_version() -> u32 { return 2;"), "UnknownField"),
    "presentation": (base + presentation, "valid"),
    "presentation-scene": (base + presentation.replace("scene() -> u32 { return 3;", "scene() -> u32 { return 17;"), "out of range"),
    "presentation-time": (base + presentation.replace("return 2.5;", "return 31.0;"), "out of range"),
})
for dimension in (2, 3):
    dimension_cases = {**fixtures, "atlas-visual": (base.replace("visual: 0", "visual: 17"),
                                                   "valid" if dimension == 2 else "out of range")}
    dimension_cases["linked-views"] = (base + windows, "valid" if dimension == 2 else "requires a 2D")
    if dimension == 3:
        for key in ("alpha", "alpha-range", "alpha-column"):
            dimension_cases[key] = (dimension_cases[key][0], "requires a 2D")
    if dimension == 2:
        dimension_cases.update({
            "detail-visual": (alpha.replace("visual: 0", "visual: 33"), "valid"),
            "window-count": (base + windows.replace("count() -> u32 { return 3;", "count() -> u32 { return 4;"), "out of range"),
            "window-scale": (base + windows.replace("return 0.75;", "return 0.4;"), "out of range"),
            "window-aspect": (base + windows.replace("12.0, 6.75", "12.0, 4.0"), "out of range"),
            "window-center": (base + windows.replace("-16.0", "1000001.0"), "out of range"),
            "preset-limit": (base + windows.replace("return 10;", "return 33;"), "must be 1..32"),
        })
    for name, (source, expected) in dimension_cases.items():
        for token, replacement in {
            "VECTOR": f"vec{dimension}",
            "ZERO": f"vec{dimension}(" + ", ".join(["0.0"] * dimension) + ")",
            "LEFT": f"vec{dimension}(-2.0, 0.0" + (", 0.0" if dimension == 3 else "") + ")",
            "RIGHT": f"vec{dimension}(2.0, 0.0" + (", 0.0" if dimension == 3 else "") + ")",
        }.items():
            source = source.replace(token, replacement)
        path = output / f"{dimension}d-{name}.glang"
        module = path.with_suffix(".glbc")
        path.write_text(source, encoding="utf-8")
        subprocess.run([compiler, str(path), "-o", str(module)], check=True)
        subprocess.run([executable, str(module), expected, f"{dimension}d"], check=True)
print("actor, presentation and linked-window contract cases passed")
