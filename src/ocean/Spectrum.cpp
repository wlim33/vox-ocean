#include "ocean/Spectrum.h"
#include <cmath>
#include <random>

namespace vox {

static constexpr float G = 9.81f;

float phillips(glm::vec2 k, glm::vec2 wind_dir, float wind_speed, float A,
               float swell, float max_wavelength_m) {
    float k2 = k.x*k.x + k.y*k.y;
    if (k2 < 1e-12f) return 0.0f;
    float k4 = k2 * k2;
    float L = wind_speed * wind_speed / G;
    float kw = (glm::length(k) > 0.0f ? glm::dot(glm::normalize(k), wind_dir) : 0.0f);
    float damping = std::exp(-k2 * (L * 0.001f) * (L * 0.001f));
    float spread = std::pow(kw * kw, 1.0f + 3.0f * swell);

    // Diorama high-pass: suppress wavelengths beyond max_wavelength_m so a
    // finite patch isn't dominated by swell larger than itself (0 = off).
    float highpass = 1.0f;
    if (max_wavelength_m > 0.0f) {
        // Quartic shoulder: smooth in k (hard steps ring in the IFFT), ~0.1%
        // at a quarter of the cutoff frequency, ~63% at the cutoff itself.
        float k_min2 = (6.2831853f / max_wavelength_m) * (6.2831853f / max_wavelength_m);
        float r = k2 / k_min2;            // (k / k_min)^2
        highpass = 1.0f - std::exp(-r * r);
    }
    return A * std::exp(-1.0f / (k2 * L * L)) / k4 * spread * damping * highpass;
}

void displacement_spectrum_from_height(
    const std::vector<glm::vec2>& h, int N, float L,
    std::vector<glm::vec2>& dx, std::vector<glm::vec2>& dz)
{
    const float two_pi = 6.2831853071795864769f;
    dx.assign((size_t)N * N, glm::vec2(0.0f));
    dz.assign((size_t)N * N, glm::vec2(0.0f));
    for (int j = 0; j < N; ++j) {
        for (int i = 0; i < N; ++i) {
            int ic = i - N / 2;
            int jc = j - N / 2;
            if (ic == 0 && jc == 0) continue;
            // Nyquist bins (ic or jc == -N/2) are their own mirror partner, so
            // -i·k̂·ĥ would be purely imaginary there — breaking the Hermitian
            // symmetry that keeps the IFFT'd displacement fields real. Zero them.
            if (i == 0 || j == 0) continue;
            float kx = two_pi * (float)ic / L;
            float kz = two_pi * (float)jc / L;
            float kmag = std::sqrt(kx * kx + kz * kz);
            glm::vec2 hk = h[(size_t)j * N + i];
            // -i · (a + i·b) = b - i·a
            glm::vec2 minus_i_h{ hk.y, -hk.x };
            dx[(size_t)j * N + i] = (kx / kmag) * minus_i_h;
            dz[(size_t)j * N + i] = (kz / kmag) * minus_i_h;
        }
    }
}

std::vector<glm::vec4> generate_h0(const SpectrumParams& p) {
    std::vector<glm::vec4> out((size_t)p.N * p.N);
    std::mt19937 rng(p.seed);
    std::normal_distribution<float> nd(0.0f, 1.0f);
    glm::vec2 wd { std::cos(p.wind_dir_rad), std::sin(p.wind_dir_rad) };

    // Pass 1: independent draws for h0(k) in xy.
    for (int j = 0; j < p.N; ++j) {
        for (int i = 0; i < p.N; ++i) {
            int  ic = i - p.N / 2;
            int  jc = j - p.N / 2;
            glm::vec2 k = { 2.0f * 3.1415926535f * (float)ic / p.L,
                             2.0f * 3.1415926535f * (float)jc / p.L };
            float ph = phillips(k, wd, p.wind_speed, p.amplitude, p.swell,
                                p.max_wavelength_m);
            float kr = nd(rng), ki = nd(rng);
            float s = 1.0f / std::sqrt(2.0f);
            out[(size_t)j * p.N + i].x = s * kr * std::sqrt(ph);
            out[(size_t)j * p.N + i].y = s * ki * std::sqrt(ph);
        }
    }
    // Pass 2: zw = h0(-k)* taken from the mirrored bin, so that
    // h̃(k,t) = h0(k)e^{iωt} + h0*(-k)e^{-iωt} is Hermitian and the IFFT'd
    // height field is real (the pipeline reads only the real channel).
    for (int j = 0; j < p.N; ++j) {
        for (int i = 0; i < p.N; ++i) {
            int im = (p.N - i) % p.N;
            int jm = (p.N - j) % p.N;
            const glm::vec4& m = out[(size_t)jm * p.N + im];
            out[(size_t)j * p.N + i].z =  m.x;
            out[(size_t)j * p.N + i].w = -m.y;
        }
    }
    return out;
}

}
