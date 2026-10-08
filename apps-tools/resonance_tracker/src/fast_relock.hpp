#pragma once

#include <array>
#include <complex>

// Fixed-size arithmetic boundary for a future FPGA implementation. No hardware,
// ownership, clocks, allocation, or application state enters this interface.
namespace FastRelock {
constexpr std::array<double, 6> kOffsetsHz{-200000, -100000, -50000, 50000, 100000, 200000};
using Values = std::array<std::complex<double>, 6>;

struct Input {
    Values reference;
    Values derivative_per_hz;
    Values measured;
};

struct Limits {
    double maximum_shift_hz = 0.0;
    double maximum_se_hz = 0.0;
    double maximum_residual = 0.10;
    double minimum_gain = 0.25;
};

struct Result {
    bool valid = false;
    bool accepted = false;
    double shift_hz = 0.0;
    double se_hz = 0.0;
    double normalized_residual = 0.0;
    double gain = 0.0;
};

// CPU/STM32-owned preparation. Reference, derivatives and measured points must
// share one response representation and integration/amplitude configuration.
struct Prepared {
    bool valid = false;
    Values reference{};
    Values centered_reference{};
    Values centered_derivative{};
    Values shift_weights{};
    Values gain_weights{};
    std::complex<double> derivative_gain{};
    double template_energy = 0.0;
    double se_variance_per_rss = 0.0;
    double maximum_shift_hz = 0.0;
    double maximum_rss_for_se = 0.0;
    double residual_coefficient = 0.0;
    double residual_floor = 0.0;
    double minimum_gain_squared = 0.0;
};

struct KernelResult {
    bool valid = false;
    bool accepted = false;
    double shift_hz = 0.0;
    double rss = 0.0;
    double gain_squared = 0.0;
};

Prepared prepare(const Values& reference, const Values& derivative_per_hz, const Limits& limits);
// FPGA-facing arithmetic: no division, square root, or complex magnitude.
KernelResult evaluate(const Prepared& prepared, const Values& measured);
// CPU-only presentation adapter; never use its metrics for kernel acceptance.
Result describe(const Prepared& prepared, const KernelResult& result);
// Compatibility convenience for CPU callers; includes coefficient preparation.
Result estimate(const Input& input, const Limits& limits);
}
