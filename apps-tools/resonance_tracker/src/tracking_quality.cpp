#include "tracking_quality.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kLossShiftFraction = 0.40;
constexpr double kLossResidualLimit = 0.10;
constexpr double kLossGainMinimum = 0.25;
constexpr std::uint32_t kLossCycles = 3;
}

TrackingQualityDecision evaluateTrackingQuality(const TrackingQualityInput& quality)
{
    TrackingQualityDecision decision;
    decision.poor_fit = !quality.fit_valid || quality.spacing_hz <= 0.0 ||
                        std::abs(quality.requested_shift_hz) > kLossShiftFraction * quality.spacing_hz ||
                        quality.normalized_residual > kLossResidualLimit || quality.template_gain < kLossGainMinimum;
    decision.applied_shift_hz = decision.poor_fit ? 0.0 : quality.requested_shift_hz;
    decision.loss_counter = decision.poor_fit ? std::min(quality.loss_counter + 1U, kLossCycles) : 0U;
    decision.recovery_required = decision.loss_counter >= kLossCycles;
    return decision;
}
