#pragma once

#include <cstdint>

struct TrackingQualityInput {
    bool fit_valid = false;
    double requested_shift_hz = 0.0;
    double frequency_se_hz = 0.0;
    double spacing_hz = 0.0;
    double normalized_residual = 1.0;
    double template_gain = 0.0;
    std::uint32_t loss_counter = 0;
};

struct TrackingQualityDecision {
    bool poor_fit = true;
    double applied_shift_hz = 0.0;
    std::uint32_t loss_counter = 0;
    bool recovery_required = false;
};

TrackingQualityDecision evaluateTrackingQuality(const TrackingQualityInput& quality);

struct TrackingQualityCoefficients {
    bool valid = false;
    double maximum_shift_hz = 0.0;
    double residual_coefficient = 0.0;
    double residual_floor = 0.0;
    double minimum_gain_squared = 0.0;
};

struct TrackingQualitySums {
    bool fit_valid = false;
    double requested_shift_hz = 0.0;
    double rss = 0.0;
    double gain_squared = 0.0;
    double frequency_variance_hz2 = 0.0;
    std::uint32_t loss_counter = 0;
};

// CPU setup, followed by a division/square-root-free acceptance kernel.
TrackingQualityCoefficients prepareTrackingQuality(double spacing_hz, double template_energy);
TrackingQualityDecision evaluateTrackingQualitySums(const TrackingQualityCoefficients& coefficients,
                                                   const TrackingQualitySums& quality);
