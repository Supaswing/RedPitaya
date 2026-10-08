#pragma once

#include "tracking_quality.hpp"

#include <array>
#include <complex>

namespace FivePointTracking {
using Values = std::array<std::complex<double>, 5>;
using Matrix = std::array<std::array<double, 5>, 5>;

struct Prepared {
    bool valid = false;
    Values reference{};
    Values derivative{};
    Matrix normal_inverse{};
    // Each row maps five complex errors to one real fitted parameter.
    std::array<Values, 5> parameter_weights{};
    double spacing_hz = 0.0;
    double template_energy = 0.0;
    double frequency_variance_per_rss = 0.0;
    TrackingQualityCoefficients quality{};
};

Prepared prepare(const Values& reference, double spacing_hz);
// FPGA-facing fit and quality sufficient statistics; no division or sqrt.
TrackingQualitySums evaluate(const Prepared& prepared, const Values& measured);
}
