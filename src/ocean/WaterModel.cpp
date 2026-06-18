#include "ocean/WaterModel.h"
#include "core/Config.h"
#include <cmath>
namespace vox {
namespace {
constexpr float kG     = 9.81f;        // gravity; matches the GPU dispersion relation
constexpr float kTwoPi = 6.2831853f;
// CPU/GPU amplitude reconciliation: the GPU uses a Phillips amplitude (~4000)
// where surface height scales ~ sqrt(amplitude). This gain maps that onto a
// meters-scale wave sum. It is a fixed hand-tuned bridge with no runtime
// cross-check; to re-derive, compare height_at() against GPU surface_tex_.r
// statistics for a representative WaveConfig.
constexpr float kHeightGain = 0.012f;
}

// Build the directional wave bank: kWaves harmonics derived from `wave`, summed
// by height_at() into a zero-mean (mean sea level y = 0) surface. This formula
// sets how closely CPU physics tracks the GPU's visual ocean.
void WaterModel::configure(const WaveConfig& wave) {
    // Longest wave is bounded by the diorama (max_wavelength_m); shorter
    // harmonics use coprime-ish ratios so crests don't reinforce on a grid,
    // mirroring the cascade patch-size philosophy.
    float base_lambda = wave.max_wavelength_m > 0.0f ? wave.max_wavelength_m : 20.0f;
    const float lambda_ratio[kWaves] = {1.0f, 0.53f, 0.29f, 0.17f};
    // Directional spread narrows as swell -> 1 (long-crested), broadens for
    // wind sea; symmetric around wind_dir.
    float spread   = (1.0f - wave.swell) * 0.6f;
    float base_amp = std::sqrt(wave.amplitude) * kHeightGain
                   * (0.5f + 0.05f * wave.wind_speed_mps);
    for (int i = 0; i < kWaves; ++i) {
        float lambda = base_lambda * lambda_ratio[i];
        float ang    = wave.wind_dir_rad + spread * ((float)i - (kWaves - 1) * 0.5f);
        waves_[i].dir   = glm::vec2(std::cos(ang), std::sin(ang));
        waves_[i].k     = kTwoPi / lambda;
        waves_[i].omega = std::sqrt(kG * waves_[i].k);
        waves_[i].amp   = base_amp * lambda_ratio[i];   // shorter waves carry less energy
        waves_[i].phase = (float)i * 1.7f;              // decorrelate the harmonics
    }
}

float WaterModel::height_at(float x, float z, float t) const {
    float h = 0.0f;
    for (const Wave& w : waves_) {
        float ph = w.k * (w.dir.x * x + w.dir.y * z) - w.omega * t + w.phase;
        h += w.amp * std::sin(ph);
    }
    return h;   // mean sea level = 0
}
}
