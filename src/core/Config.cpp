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
    "cascade_count",
    "max_in_flight_frames",
    "cascades","wave","sky","shading","bench","voxel","march"
};

void check_unknown_keys(const toml::table& t, LoadResult& r) {
    for (auto&& [k, _] : t) {
        std::string key{k.str()};
        if (std::find(KNOWN_TOP_KEYS.begin(), KNOWN_TOP_KEYS.end(), key) == KNOWN_TOP_KEYS.end())
            r.warnings.push_back("unknown top-level key: " + key);
    }
}

void load_wave(const toml::table& t, WaveConfig& w) {
    if (auto v = t["wind_speed_mps"].value<double>()) w.wind_speed_mps = (float)*v;
    if (auto v = t["wind_dir_rad"].value<double>())   w.wind_dir_rad   = (float)*v;
    if (auto v = t["choppiness"].value<double>())     w.choppiness     = (float)*v;
    if (auto v = t["swell"].value<double>())          w.swell          = (float)*v;
    if (auto v = t["amplitude"].value<double>())      w.amplitude      = (float)*v;
    if (auto v = t["max_wavelength_m"].value<double>()) w.max_wavelength_m = (float)*v;
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
    if (auto v = tbl["cascade_count"].value<int64_t>()) {
        int n = (int)*v;
        if (n < 1 || n > 4) r.warnings.push_back("cascade_count out of [1,4], clamped");
        c.cascade_count = clamp(n, 1, 4);
    }
    if (auto v = tbl["max_in_flight_frames"].value<int64_t>())
        c.max_in_flight_frames = clamp((int)*v, 1, 3);

    if (auto* w = tbl["wave"].as_table())  load_wave(*w, c.wave);
    if (auto* vx = tbl["voxel"].as_table()) load_voxel(*vx, c.voxel, r);
    if (auto* mc = tbl["march"].as_table()) load_march(*mc, c.march, r);
    // sky, shading, cascades, bench loaders follow the same pattern; extend as needed.
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
            if      (key == "wave.wind_speed_mps")        in.config.wave.wind_speed_mps = std::stof(val);
            else if (key == "wave.amplitude")             in.config.wave.amplitude      = std::stof(val);
            else if (key == "wave.choppiness")            in.config.wave.choppiness     = std::stof(val);
            else if (key == "wave.swell")                 in.config.wave.swell          = std::stof(val);
            else if (key == "wave.max_wavelength_m")      in.config.wave.max_wavelength_m = std::stof(val);
            else if (key == "cascade_count")              in.config.cascade_count       = std::stoi(val);
            else if (key == "voxel.grid_extent") {
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
            else if (key == "bench.bench_mode")           in.config.bench.bench_mode    = (val == "true" || val == "1");
            else in.warnings.push_back("unknown override key: " + key);
        } catch (const std::exception&) {
            in.warnings.push_back("invalid value for " + key + ": " + val);
        }
    }
    return in;
}

uint64_t config_hash(const Config& c) {
    uint64_t h = 0xcbf29ce484222325ull;
    // Wave
    h = fnv1a64(&c.wave.wind_speed_mps, sizeof(c.wave.wind_speed_mps), h);
    h = fnv1a64(&c.wave.wind_dir_rad,   sizeof(c.wave.wind_dir_rad),   h);
    h = fnv1a64(&c.wave.choppiness,     sizeof(c.wave.choppiness),     h);
    h = fnv1a64(&c.wave.swell,          sizeof(c.wave.swell),          h);
    h = fnv1a64(&c.wave.amplitude,      sizeof(c.wave.amplitude),      h);
    h = fnv1a64(&c.wave.max_wavelength_m, sizeof(c.wave.max_wavelength_m), h);
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
    // Grid / cascades
    h = fnv1a64(&c.cascade_count,       sizeof(c.cascade_count),       h);
    for (int i = 0; i < 4; ++i) {
        h = fnv1a64(&c.cascades[i].size_m,     sizeof(float), h);
        h = fnv1a64(&c.cascades[i].resolution, sizeof(int),   h);
    }
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
    // Frame controls
    h = fnv1a64(&c.max_in_flight_frames,  sizeof(c.max_in_flight_frames),  h);
    return h;
}

} // namespace vox
