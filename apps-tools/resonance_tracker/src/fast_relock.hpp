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

Result estimate(const Input& input, const Limits& limits);
}
