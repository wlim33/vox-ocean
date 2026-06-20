#include "core/Config.h"
#include "core/Hash.h"
#include <toml++/toml.h>
#include <sstream>
#include <fstream>
#include <algorithm>

namespace vox {
namespace {

template<typename T>
T clamp(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }

const std::vector<std::string> KNOWN_TOP_KEYS = {
    "max_in_flight_frames",
    "sky","shading","bench","voxel","march","render","entity","kelp","fish","sand"
};

void check_unknown_keys(const toml::table& t, LoadResult& r) {
    for (auto&& [k, _] : t) {
        std::string key{k.str()};
        if (std::find(KNOWN_TOP_KEYS.begin(), KNOWN_TOP_KEYS.end(), key) == KNOWN_TOP_KEYS.end())
            r.warnings.push_back("unknown top-level key: " + key);
    }
}

void load_voxel(const toml::table& t, VoxelConfig& v, LoadResult& r) {
    if (auto n = t["grid_extent"].value<int64_t>()) {
        int val = (int)*n;
        if (val < 8 || val > 1024) r.warnings.push_back("voxel.grid_extent out of [8,1024], clamped");
        v.grid_extent = clamp(val, 8, 1024);
    }
    if (auto n = t["voxel_size_m"].value<double>()) {
        float val = (float)*n;
        if (val < 0.05f || val > 10.0f) r.warnings.push_back("voxel.voxel_size_m out of [0.05,10.0], clamped");
        v.voxel_size_m = clamp(val, 0.05f, 10.0f);
    }
    if (auto n = t["height_step_m"].value<double>()) {
        float val = (float)*n;
        if (val < 0.01f || val > 10.0f) r.warnings.push_back("voxel.height_step_m out of [0.01,10.0], clamped");
        v.height_step_m = clamp(val, 0.01f, 10.0f);
    }
    if (auto n = t["base_depth_m"].value<double>()) {
        float val = (float)*n;
        if (val < 0.5f || val > 100.0f) r.warnings.push_back("voxel.base_depth_m out of [0.5,100.0], clamped");
        v.base_depth_m = clamp(val, 0.5f, 100.0f);
    }
    if (auto n = t["height_cells"].value<int64_t>()) {
        int val = (int)*n;
        if (val < 16 || val > 512) r.warnings.push_back("voxel.height_cells out of [16,512], clamped");
        v.height_cells = clamp(val, 16, 512);
    }
    if (auto n = t["floor_seed"].value<int64_t>()) v.floor_seed = (int)*n;
}

void load_march(const toml::table& t, MarchConfig& m, LoadResult& r) {
    if (auto n = t["max_steps"].value<int64_t>()) {
        int val = (int)*n;
        if (val < 32 || val > 4096) r.warnings.push_back("march.max_steps out of [32,4096], clamped");
        m.max_steps = clamp(val, 32, 4096);
    }
    if (auto n = t["render_scale"].value<double>()) {
        float val = (float)*n;
        if (val < 0.25f || val > 1.0f) r.warnings.push_back("march.render_scale out of [0.25,1.0], clamped");
        m.render_scale = clamp(val, 0.25f, 1.0f);
    }
}

void load_render(const toml::table& t, RenderConfig& rc) {
    if (auto v = t["backend"].value<std::string>()) rc.backend = *v;
}

void load_entity(const toml::table& t, EntityConfig& e, LoadResult& r) {
    if (auto v = t["boat_enabled"].value<bool>()) e.boat_enabled = *v;
    if (auto v = t["boat_speed_mps"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 5.0f) r.warnings.push_back("entity.boat_speed_mps out of [0,5], clamped");
        e.boat_speed_mps = clamp(val, 0.0f, 5.0f);
    }
    if (auto v = t["wake_amp"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 2.0f) r.warnings.push_back("entity.wake_amp out of [0,2], clamped");
        e.wake_amp = clamp(val, 0.0f, 2.0f);
    }
}

void load_kelp(const toml::table& t, KelpConfig& k, LoadResult& r) {
    if (auto v = t["enabled"].value<bool>()) k.enabled = *v;
    if (auto v = t["density"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 0.3f) r.warnings.push_back("kelp.density out of [0,0.3], clamped");
        k.density = clamp(val, 0.0f, 0.3f);
    }
    if (auto v = t["max_stalks"].value<int64_t>()) {
        int val = (int)*v;
        if (val < 0 || val > 1000000) r.warnings.push_back("kelp.max_stalks out of [0,1000000], clamped");
        k.max_stalks = clamp(val, 0, 1000000);
    }
    if (auto v = t["max_height_m"].value<double>()) {
        float val = (float)*v;
        if (val < 1.0f || val > 30.0f) r.warnings.push_back("kelp.max_height_m out of [1,30], clamped");
        k.max_height_m = clamp(val, 1.0f, 30.0f);
    }
    if (auto v = t["sway_strength"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 4.0f) r.warnings.push_back("kelp.sway_strength out of [0,4], clamped");
        k.sway_strength = clamp(val, 0.0f, 4.0f);
    }
    if (auto v = t["sway_ambient"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 2.0f) r.warnings.push_back("kelp.sway_ambient out of [0,2], clamped");
        k.sway_ambient = clamp(val, 0.0f, 2.0f);
    }
    if (auto v = t["seed"].value<int64_t>()) k.seed = (int)*v;
}

void load_fish(const toml::table& t, FishConfig& f, LoadResult& r) {
    if (auto v = t["enabled"].value<bool>()) f.enabled = *v;
    if (auto v = t["school_count"].value<int64_t>()) {
        int val = (int)*v;
        if (val < 0 || val > 32) r.warnings.push_back("fish.school_count out of [0,32], clamped");
        f.school_count = clamp(val, 0, 32);
    }
    if (auto v = t["per_school"].value<int64_t>()) {
        int val = (int)*v;
        if (val < 0 || val > 256) r.warnings.push_back("fish.per_school out of [0,256], clamped");
        f.per_school = clamp(val, 0, 256);
    }
    if (auto v = t["speed_mps"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 8.0f) r.warnings.push_back("fish.speed_mps out of [0,8], clamped");
        f.speed_mps = clamp(val, 0.0f, 8.0f);
    }
    if (auto v = t["depth_frac"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 1.0f) r.warnings.push_back("fish.depth_frac out of [0,1], clamped");
        f.depth_frac = clamp(val, 0.0f, 1.0f);
    }
    if (auto v = t["spread_m"].value<double>()) {
        float val = (float)*v;
        if (val < 0.0f || val > 20.0f) r.warnings.push_back("fish.spread_m out of [0,20], clamped");
        f.spread_m = clamp(val, 0.0f, 20.0f);
    }
    if (auto v = t["seed"].value<int64_t>()) f.seed = (int)*v;
}

void load_sand(const toml::table& t, SandConfig& s, LoadResult& r) {
    if (auto v = t["enabled"].value<bool>()) s.enabled = *v;
    if (auto v = t["spawn_radius"].value<int64_t>()) {
        int val = (int)*v;
        if (val < 1 || val > 64) r.warnings.push_back("sand.spawn_radius out of [1,64], clamped");
        s.spawn_radius = clamp(val, 1, 64);
    }
    if (auto v = t["spawn_thickness"].value<int64_t>()) {
        int val = (int)*v;
        if (val < 1 || val > 64) r.warnings.push_back("sand.spawn_thickness out of [1,64], clamped");
        s.spawn_thickness = clamp(val, 1, 64);
    }
}

} // namespace

LoadResult load_config_from_string(const std::string& text) {
    LoadResult r;
    toml::table tbl;
    try { tbl = toml::parse(text); }
    catch (const toml::parse_error& e) {
        r.warnings.push_back(std::string("parse error: ") + e.what());
        return r;
    }
    check_unknown_keys(tbl, r);

    auto& c = r.config;
    if (auto v = tbl["max_in_flight_frames"].value<int64_t>())
        c.max_in_flight_frames = clamp((int)*v, 1, 3);

    if (auto* vx = tbl["voxel"].as_table()) load_voxel(*vx, c.voxel, r);
    if (auto* mc = tbl["march"].as_table()) load_march(*mc, c.march, r);
    if (auto* rd = tbl["render"].as_table()) load_render(*rd, c.render);
    if (auto* en = tbl["entity"].as_table()) load_entity(*en, c.entity, r);
    if (auto* kp = tbl["kelp"].as_table()) load_kelp(*kp, c.kelp, r);
    if (auto* fp = tbl["fish"].as_table()) load_fish(*fp, c.fish, r);
    if (auto* sp = tbl["sand"].as_table()) load_sand(*sp, c.sand, r);
    // sky, shading, bench loaders follow the same pattern; extend as needed.
    return r;
}

LoadResult load_config_from_file(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss; ss << in.rdbuf();
    return load_config_from_string(ss.str());
}

LoadResult apply_overrides(LoadResult in, const std::vector<std::string>& kv) {
    for (auto& s : kv) {
        auto eq = s.find('=');
        if (eq == std::string::npos) { in.warnings.push_back("bad --set " + s); continue; }
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        try {
            if      (key == "voxel.grid_extent") {
                int n = std::stoi(val);
                if (n < 8 || n > 1024) in.warnings.push_back("voxel.grid_extent out of [8,1024], clamped");
                in.config.voxel.grid_extent = clamp(n, 8, 1024);
            }
            else if (key == "voxel.voxel_size_m") {
                float f = std::stof(val);
                if (f < 0.05f || f > 10.0f) in.warnings.push_back("voxel.voxel_size_m out of [0.05,10.0], clamped");
                in.config.voxel.voxel_size_m = clamp(f, 0.05f, 10.0f);
            }
            else if (key == "voxel.height_step_m") {
                float f = std::stof(val);
                if (f < 0.01f || f > 10.0f) in.warnings.push_back("voxel.height_step_m out of [0.01,10.0], clamped");
                in.config.voxel.height_step_m = clamp(f, 0.01f, 10.0f);
            }
            else if (key == "voxel.base_depth_m") {
                float f = std::stof(val);
                if (f < 0.5f || f > 100.0f) in.warnings.push_back("voxel.base_depth_m out of [0.5,100.0], clamped");
                in.config.voxel.base_depth_m = clamp(f, 0.5f, 100.0f);
            }
            else if (key == "voxel.height_cells") {
                int n = std::stoi(val);
                if (n < 16 || n > 512) in.warnings.push_back("voxel.height_cells out of [16,512], clamped");
                in.config.voxel.height_cells = clamp(n, 16, 512);
            }
            else if (key == "voxel.floor_seed")  in.config.voxel.floor_seed = std::stoi(val);
            else if (key == "march.max_steps") {
                int n = std::stoi(val);
                if (n < 32 || n > 4096) in.warnings.push_back("march.max_steps out of [32,4096], clamped");
                in.config.march.max_steps = clamp(n, 32, 4096);
            }
            else if (key == "march.render_scale") {
                float f = std::stof(val);
                if (f < 0.25f || f > 1.0f) in.warnings.push_back("march.render_scale out of [0.25,1.0], clamped");
                in.config.march.render_scale = clamp(f, 0.25f, 1.0f);
            }
            else if (key == "render.backend") in.config.render.backend = val;
            else if (key == "bench.bench_mode")           in.config.bench.bench_mode    = (val == "true" || val == "1");
            else if (key == "entity.boat_enabled") in.config.entity.boat_enabled = (val == "true" || val == "1");
            else if (key == "entity.boat_speed_mps") {
                float f = std::stof(val);
                if (f < 0.0f || f > 5.0f) in.warnings.push_back("entity.boat_speed_mps out of [0,5], clamped");
                in.config.entity.boat_speed_mps = clamp(f, 0.0f, 5.0f);
            }
            else if (key == "entity.wake_amp") {
                float f = std::stof(val);
                if (f < 0.0f || f > 2.0f) in.warnings.push_back("entity.wake_amp out of [0,2], clamped");
                in.config.entity.wake_amp = clamp(f, 0.0f, 2.0f);
            }
            else if (key == "kelp.enabled") in.config.kelp.enabled = (val == "true" || val == "1");
            else if (key == "kelp.density") {
                float f = std::stof(val);
                if (f < 0.0f || f > 0.3f) in.warnings.push_back("kelp.density out of [0,0.3], clamped");
                in.config.kelp.density = clamp(f, 0.0f, 0.3f);
            }
            else if (key == "kelp.max_stalks") {
                int n = std::stoi(val);
                if (n < 0 || n > 1000000) in.warnings.push_back("kelp.max_stalks out of [0,1000000], clamped");
                in.config.kelp.max_stalks = clamp(n, 0, 1000000);
            }
            else if (key == "kelp.max_height_m") {
                float f = std::stof(val);
                if (f < 1.0f || f > 30.0f) in.warnings.push_back("kelp.max_height_m out of [1,30], clamped");
                in.config.kelp.max_height_m = clamp(f, 1.0f, 30.0f);
            }
            else if (key == "kelp.sway_strength") {
                float f = std::stof(val);
                if (f < 0.0f || f > 4.0f) in.warnings.push_back("kelp.sway_strength out of [0,4], clamped");
                in.config.kelp.sway_strength = clamp(f, 0.0f, 4.0f);
            }
            else if (key == "kelp.sway_ambient") {
                float f = std::stof(val);
                if (f < 0.0f || f > 2.0f) in.warnings.push_back("kelp.sway_ambient out of [0,2], clamped");
                in.config.kelp.sway_ambient = clamp(f, 0.0f, 2.0f);
            }
            else if (key == "kelp.seed") in.config.kelp.seed = std::stoi(val);
            else if (key == "fish.enabled") in.config.fish.enabled = (val == "true" || val == "1");
            else if (key == "fish.school_count") {
                int n = std::stoi(val);
                if (n < 0 || n > 32) in.warnings.push_back("fish.school_count out of [0,32], clamped");
                in.config.fish.school_count = clamp(n, 0, 32);
            }
            else if (key == "fish.per_school") {
                int n = std::stoi(val);
                if (n < 0 || n > 256) in.warnings.push_back("fish.per_school out of [0,256], clamped");
                in.config.fish.per_school = clamp(n, 0, 256);
            }
            else if (key == "fish.speed_mps") {
                float f = std::stof(val);
                if (f < 0.0f || f > 8.0f) in.warnings.push_back("fish.speed_mps out of [0,8], clamped");
                in.config.fish.speed_mps = clamp(f, 0.0f, 8.0f);
            }
            else if (key == "fish.depth_frac") {
                float f = std::stof(val);
                if (f < 0.0f || f > 1.0f) in.warnings.push_back("fish.depth_frac out of [0,1], clamped");
                in.config.fish.depth_frac = clamp(f, 0.0f, 1.0f);
            }
            else if (key == "fish.spread_m") {
                float f = std::stof(val);
                if (f < 0.0f || f > 20.0f) in.warnings.push_back("fish.spread_m out of [0,20], clamped");
                in.config.fish.spread_m = clamp(f, 0.0f, 20.0f);
            }
            else if (key == "fish.seed") in.config.fish.seed = std::stoi(val);
            else if (key == "sand.enabled") in.config.sand.enabled = (val == "true" || val == "1");
            else if (key == "sand.spawn_radius") {
                int n = std::stoi(val);
                if (n < 1 || n > 64) in.warnings.push_back("sand.spawn_radius out of [1,64], clamped");
                in.config.sand.spawn_radius = clamp(n, 1, 64);
            }
            else if (key == "sand.spawn_thickness") {
                int n = std::stoi(val);
                if (n < 1 || n > 64) in.warnings.push_back("sand.spawn_thickness out of [1,64], clamped");
                in.config.sand.spawn_thickness = clamp(n, 1, 64);
            }
            else in.warnings.push_back("unknown override key: " + key);
        } catch (const std::exception&) {
            in.warnings.push_back("invalid value for " + key + ": " + val);
        }
    }
    return in;
}

uint64_t config_hash(const Config& c) {
    uint64_t h = 0xcbf29ce484222325ull;
    // Voxel
    h = fnv1a64(&c.voxel.grid_extent,   sizeof(c.voxel.grid_extent),   h);
    h = fnv1a64(&c.voxel.voxel_size_m,  sizeof(c.voxel.voxel_size_m),  h);
    h = fnv1a64(&c.voxel.height_step_m, sizeof(c.voxel.height_step_m), h);
    h = fnv1a64(&c.voxel.base_depth_m,  sizeof(c.voxel.base_depth_m),  h);
    h = fnv1a64(&c.voxel.height_cells,  sizeof(c.voxel.height_cells),  h);
    h = fnv1a64(&c.voxel.floor_seed,    sizeof(c.voxel.floor_seed),    h);
    // March
    h = fnv1a64(&c.march.max_steps,    sizeof(c.march.max_steps),    h);
    h = fnv1a64(&c.march.render_scale, sizeof(c.march.render_scale), h);
    // Entity
    h = fnv1a64(&c.entity.boat_enabled,   sizeof(c.entity.boat_enabled),   h);
    h = fnv1a64(&c.entity.boat_speed_mps, sizeof(c.entity.boat_speed_mps), h);
    h = fnv1a64(&c.entity.wake_amp,       sizeof(c.entity.wake_amp),       h);
    // Kelp
    h = fnv1a64(&c.kelp.enabled,       sizeof(c.kelp.enabled),       h);
    h = fnv1a64(&c.kelp.density,       sizeof(c.kelp.density),       h);
    h = fnv1a64(&c.kelp.max_stalks,    sizeof(c.kelp.max_stalks),    h);
    h = fnv1a64(&c.kelp.max_height_m,  sizeof(c.kelp.max_height_m),  h);
    h = fnv1a64(&c.kelp.sway_strength, sizeof(c.kelp.sway_strength), h);
    h = fnv1a64(&c.kelp.sway_ambient,  sizeof(c.kelp.sway_ambient),  h);
    h = fnv1a64(&c.kelp.seed,          sizeof(c.kelp.seed),          h);
    // Fish
    h = fnv1a64(&c.fish.enabled,       sizeof(c.fish.enabled),       h);
    h = fnv1a64(&c.fish.school_count,  sizeof(c.fish.school_count),  h);
    h = fnv1a64(&c.fish.per_school,    sizeof(c.fish.per_school),    h);
    h = fnv1a64(&c.fish.speed_mps,     sizeof(c.fish.speed_mps),     h);
    h = fnv1a64(&c.fish.depth_frac,    sizeof(c.fish.depth_frac),    h);
    h = fnv1a64(&c.fish.spread_m,      sizeof(c.fish.spread_m),      h);
    h = fnv1a64(&c.fish.seed,          sizeof(c.fish.seed),          h);
    // Sand
    h = fnv1a64(&c.sand.enabled,         sizeof(c.sand.enabled),         h);
    h = fnv1a64(&c.sand.spawn_radius,    sizeof(c.sand.spawn_radius),    h);
    h = fnv1a64(&c.sand.spawn_thickness, sizeof(c.sand.spawn_thickness), h);
    // Sky
    h = fnv1a64(&c.sky.cubemap_resolution, sizeof(c.sky.cubemap_resolution), h);
    h = fnv1a64(&c.sky.sun_elevation_rad,  sizeof(c.sky.sun_elevation_rad),  h);
    h = fnv1a64(&c.sky.sun_azimuth_rad,    sizeof(c.sky.sun_azimuth_rad),    h);
    h = fnv1a64(&c.sky.turbidity,          sizeof(c.sky.turbidity),          h);
    // Shading
    h = fnv1a64(&c.shading.foam_threshold,    sizeof(c.shading.foam_threshold),    h);
    h = fnv1a64(&c.shading.foam_strength,     sizeof(c.shading.foam_strength),     h);
    h = fnv1a64(&c.shading.depth_fog_density, sizeof(c.shading.depth_fog_density), h);
    h = fnv1a64(&c.shading.sun_shininess,     sizeof(c.shading.sun_shininess),     h);
    h = fnv1a64(&c.shading.water_ior,         sizeof(c.shading.water_ior),         h);
    // Bench (hash bench_mode + frame counts; skip output_path to avoid string pointer instability)
    h = fnv1a64(&c.bench.bench_mode,      sizeof(c.bench.bench_mode),      h);
    h = fnv1a64(&c.bench.warmup_frames,   sizeof(c.bench.warmup_frames),   h);
    h = fnv1a64(&c.bench.measure_frames,  sizeof(c.bench.measure_frames),  h);
    h = fnv1a64(&c.bench.camera_path,     sizeof(c.bench.camera_path),     h);
    // bench.output_path: hash the string content, not raw bytes
    h = fnv1a64(c.bench.output_path.data(), c.bench.output_path.size(), h);
    // Render backend: hash the string content, not raw bytes
    h = fnv1a64(c.render.backend.data(), c.render.backend.size(), h);
    // Frame controls
    h = fnv1a64(&c.max_in_flight_frames,  sizeof(c.max_in_flight_frames),  h);
    return h;
}

} // namespace vox
