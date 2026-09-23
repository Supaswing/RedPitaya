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
