#include "ocean/WaterModel.h"
#include "core/Config.h"
#include <cmath>
namespace vox {
namespace {
constexpr float kG     = 9.81f;        // gravity; matches the GPU dispersion relation
constexpr float kTwoPi = 6.2831853f;
// CPU/GPU amplitude reconciliation: the GPU uses a Phillips amplitude (~4000)
// where surface height scales ~ sqrt(amplitude). A gain like this maps that
// onto a meters-scale wave sum; expect to tune it against the Task 2 parity log.
constexpr float kHeightGain = 0.012f;
}

// ============================================================================
// YOUR CONTRIBUTION — the wave bank.
//
// Goal: fill waves_[] with kWaves (=4) directional waves derived from `wave`,
// so that height_at() below sums into a believable meters-scale surface around
// mean sea level y = 0. This formula IS the model's character: it decides how
// closely CPU physics tracks the GPU's visual ocean.
//
// Inputs available on `wave` (see src/core/Config.h::WaveConfig):
//   wave.wind_dir_rad      base propagation direction (radians)
//   wave.wind_speed_mps    wind speed (m/s) -- bigger seas at higher wind
//   wave.swell             0 = broad wind sea ... 1 = long-crested swell
//   wave.amplitude         Phillips amplitude (~4000); height ~ sqrt(amplitude)
//   wave.max_wavelength_m  longest wave the diorama can host (0 = open ocean)
//
// Per-wave fields to set (struct Wave in WaterModel.h):
//   dir   = unit vector {cos(angle), sin(angle)}
//   k     = wavenumber = kTwoPi / wavelength
//   omega = angular frequency = sqrt(kG * k)   (deep-water dispersion)
//   amp   = amplitude in meters
//   phase = a static offset so the harmonics don't all peak together
//
// Hints (optional, ignore if you have your own approach):
//   - Bound the longest wavelength by max_wavelength_m; derive shorter
//     harmonics with coprime-ish ratios so crests don't reinforce on a grid.
//   - Narrow the directional spread as swell -> 1; widen it for wind sea.
//   - Scale overall amplitude from sqrt(wave.amplitude) * kHeightGain and let
//     shorter waves carry less energy.
//
// Reference implementation lives in the plan
// (docs/superpowers/plans/2026-06-18-water-model-cut-readback.md, Task 1 Step 4)
// if you want to compare after writing your own.
// ============================================================================
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
