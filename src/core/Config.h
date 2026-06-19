#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace vox {

enum class CameraPath { Static, Orbit, Flyby };

struct CascadeConfig {
    float size_m = 250.0f;
    int   resolution = 256;
};

struct WaveConfig {
    float wind_speed_mps = 12.0f;
    float wind_dir_rad   = 0.5f;
    float choppiness     = 0.8f;
    float swell          = 0.3f;
    // Phillips spectrum amplitude. Wave height scales with sqrt(amplitude).
    float amplitude      = 4000.0f;
    // Suppress spectral components with wavelength beyond this (high-pass).
    // The diorama can't host waves longer than itself; without this the
    // largest cascade renders as a single dome. 0 disables (open ocean).
    float max_wavelength_m = 20.0f;
};

struct ShadingConfig {
    float foam_threshold = 0.15f;
    float foam_strength  = 1.0f;
    // Tuned for M2's real water-path lengths: floor fades out around ~8m.
    float depth_fog_density = 0.10f;
    glm::vec3 deep_water_color {0.01f, 0.06f, 0.10f};
    glm::vec3 extinction_rgb   {0.9f, 0.5f, 0.3f};
    float sun_shininess        = 256.0f;
    glm::vec3 sun_color        {1.4f, 1.25f, 1.0f};
    float water_ior = 1.33f;   // refraction index at the water surface (1.0 = no bend)
};

struct VoxelConfig {
    int   grid_extent   = 192;   // columns per diorama side
    float voxel_size_m  = 0.5f;  // world size of one voxel
    float height_step_m = 0.25f; // vertical quantization step
    float base_depth_m  = 10.0f; // diorama floor below y=0
    int   height_cells  = 64;    // vertical cells above the diorama base
    int   floor_seed    = 7;     // procedural ocean-floor seed
};

struct MarchConfig {
    int   max_steps    = 512;   // DDA safety/perf lever
    float render_scale = 1.0f;  // march-target resolution factor (iOS escape hatch)
};

struct RenderConfig { std::string backend = "raymarch"; };

struct RippleConfig {
    // Interactive surface ripples (2D wave equation), summed with the FFT.
    float wave_speed_mps = 6.0f;   // propagation speed c
    float damping        = 0.995f; // per-step energy retention
    float rain_rate      = 0.0f;   // debug splashes per second (0 = off)
    float foam           = 0.5f;   // ripple amplitude -> foam coupling
};

struct EntityConfig {
    bool  boat_enabled   = true;
    float boat_speed_mps = 1.5f;   // cruise speed, clamp [0,5]
    float wake_amp       = 0.25f;  // stern splash amplitude (m), clamp [0,2]
};

struct KelpConfig {
    bool  enabled       = true;
    float density       = 0.02f;   // stalks as a fraction of columns, clamp [0,0.3]
    int   max_stalks    = 8192;    // hard cap on stalk count (0 = unlimited); bounds the
                                   // density*extent^2 per-frame stamp cost at large grids
    float max_height_m  = 6.0f;    // tallest stalk, clamp [1,30]
    float sway_strength = 0.6f;    // water-gradient coupling gain, clamp [0,4]
    float sway_ambient  = 0.15f;   // idle sway so becalmed kelp still breathes, clamp [0,2]
    int   seed          = 101;
};

struct FishConfig {
    bool  enabled      = true;
    int   school_count = 4;        // clamp [0,32]
    int   per_school   = 40;       // clamp [0,256]
    float speed_mps    = 2.0f;     // clamp [0,8]
    float depth_frac   = 0.5f;     // 0=floor .. 1=surface, clamp [0,1]
    float spread_m     = 2.5f;     // school formation radius, clamp [0,20]
    int   seed         = 202;
};

struct SandConfig {
    bool enabled         = false;  // off by default — existing scenes stay identical
    int  spawn_radius    = 6;      // half-extent (columns) of the seeded slab, clamp [1,64]
    int  spawn_thickness = 8;      // slab height in cells, dropped from the grid top, clamp [1,64]
};

struct SkyConfig {
    int cubemap_resolution = 128;
    float sun_elevation_rad = 0.7f;
    float sun_azimuth_rad   = 1.1f;
    float turbidity         = 3.0f;
};

struct BenchConfig {
    bool bench_mode = false;
    int warmup_frames = 60;
    int measure_frames = 600;
    CameraPath camera_path = CameraPath::Orbit;
    std::string output_path = "bench-{timestamp}.csv";
};

struct Config {
    int cascade_count = 3;
    // Patch sizes use coprime/irrational-ish ratios so the cascades don't
    // reinforce on a regular grid (which produces visible tile seams).
    std::array<CascadeConfig, 4> cascades {
        CascadeConfig{271.0f, 256},
        CascadeConfig{ 73.0f, 256},
        CascadeConfig{ 17.0f, 256},
        CascadeConfig{  3.7f, 256}};

    WaveConfig wave;
    VoxelConfig voxel;
    MarchConfig march;
    RenderConfig render;
    RippleConfig ripple;
    EntityConfig entity;
    KelpConfig kelp;
    FishConfig fish;
    SandConfig sand;

    SkyConfig sky;
    ShadingConfig shading;

    int max_in_flight_frames = 3;

    BenchConfig bench;
};

struct LoadResult {
    Config config;
    std::vector<std::string> warnings;
};

LoadResult load_config_from_string(const std::string& toml_text);
LoadResult load_config_from_file(const std::string& path);
LoadResult apply_overrides(LoadResult in, const std::vector<std::string>& key_value_pairs);
uint64_t   config_hash(const Config& c);
} // namespace vox
