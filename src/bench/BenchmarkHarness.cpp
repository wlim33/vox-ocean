#include "bench/BenchmarkHarness.h"
#include "bench/BenchPaths.h"
#include <chrono>
#include <sstream>
#include <iomanip>
namespace vox {

// Relative output paths land in the platform bench dir (cwd on macOS/tests,
// app Documents on iOS); absolute paths ('/...') pass through unchanged.
static std::string resolve_output_path(std::string path) {
    if (!path.empty() && path[0] == '/') return path;
    return bench_output_dir() + "/" + path;
}

static std::string substitute_timestamp(std::string path) {
    auto pos = path.find("{timestamp}");
    if (pos == std::string::npos) return path;
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::ostringstream ts; std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    ts << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return path.substr(0, pos) + ts.str() + path.substr(pos + 11);
}

void BenchmarkHarness::start(const Config& cfg, uint64_t cfg_hash) {
    if (!cfg.bench.bench_mode) return;
    active_ = true;
    warmup_ = cfg.bench.warmup_frames;
    measure_ = cfg.bench.measure_frames;
    hash_ = cfg_hash;
    out_.open(resolve_output_path(substitute_timestamp(cfg.bench.output_path)));
    // Hyperparameter header: records the tunable knobs this run was measured
    // under, so a CSV is self-describing without the originating config.
    out_ << std::hex << std::showbase
         << "# config_hash=" << cfg_hash << std::dec << std::noshowbase
         << " grid_extent=" << cfg.voxel.grid_extent
         << " voxel_size_m=" << cfg.voxel.voxel_size_m
         << " height_step_m=" << cfg.voxel.height_step_m
         << " base_depth_m=" << cfg.voxel.base_depth_m
         << " cascade_count=" << cfg.cascade_count
         << " height_cells=" << cfg.voxel.height_cells
         << " floor_seed=" << cfg.voxel.floor_seed
         << " max_steps=" << cfg.march.max_steps
         << " render_scale=" << cfg.march.render_scale
         << " max_wavelength_m=" << cfg.wave.max_wavelength_m
         << " ripple_wave_speed=" << cfg.ripple.wave_speed_mps
         << " ripple_damping=" << cfg.ripple.damping
         << " ripple_rain_rate=" << cfg.ripple.rain_rate
         << " boat_enabled=" << cfg.entity.boat_enabled
         << " boat_speed=" << cfg.entity.boat_speed_mps
         << " kelp_enabled=" << cfg.kelp.enabled
         << " kelp_density=" << cfg.kelp.density
         << " kelp_max_stalks=" << cfg.kelp.max_stalks
         << " fish_schools=" << cfg.fish.school_count
         << " fish_per_school=" << cfg.fish.per_school
         << " backend=" << cfg.render.backend
         << " fft_size=" << cfg.cascades[0].resolution << '\n';
    out_ << "frame_idx,cpu_ms,gpu_total_ms,drawable_wait_ms,config_hash\n";
}

void BenchmarkHarness::record(const FrameTiming& t) {
    if (!active_) return;
    if (frame_idx_ >= warmup_) {
        out_ << t.frame_idx << ',' << t.cpu_ms << ',' << t.gpu_total_ms
             << ',' << t.drawable_wait_ms << ',' << hash_ << '\n';
        // Termination races the tail of the completed-handler queue; flush so
        // a buffered partial batch isn't lost when the app exits.
        out_.flush();
    }
    ++frame_idx_;
}

bool BenchmarkHarness::should_exit() const {
    return active_ && frame_idx_ >= warmup_ + measure_;
}
}
