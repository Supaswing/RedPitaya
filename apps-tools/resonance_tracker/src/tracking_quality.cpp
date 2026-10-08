#include "tracking_quality.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kLossShiftFraction = 0.40;
constexpr double kLossResidualLimit = 0.10;
constexpr double kLossGainMinimum = 0.25;
constexpr std::uint32_t kLossCycles = 3;
constexpr double kEpsilon = 1e-18;

TrackingQualityDecision decide(bool poor_fit, double shift_hz, std::uint32_t loss_counter)
{
    TrackingQualityDecision decision;
    decision.poor_fit = poor_fit;
    decision.applied_shift_hz = poor_fit ? 0.0 : shift_hz;
    // Saturate before incrementing, including a corrupted UINT32_MAX input.
    decision.loss_counter = poor_fit ? std::min(loss_counter, kLossCycles - 1U) + 1U : 0U;
    decision.recovery_required = decision.loss_counter >= kLossCycles;
    return decision;
}
}

TrackingQualityDecision evaluateTrackingQuality(const TrackingQualityInput& quality)
{
    TrackingQualityDecision decision;
    decision.poor_fit = !quality.fit_valid || !std::isfinite(quality.requested_shift_hz) ||
                        !std::isfinite(quality.frequency_se_hz) || quality.frequency_se_hz < 0.0 ||
                        !std::isfinite(quality.normalized_residual) || !std::isfinite(quality.template_gain) ||
                        quality.normalized_residual < 0.0 || quality.template_gain < 0.0 ||
                        !std::isfinite(quality.spacing_hz) || quality.spacing_hz <= 0.0 ||
                        std::abs(quality.requested_shift_hz) > kLossShiftFraction * quality.spacing_hz ||
                        quality.normalized_residual > kLossResidualLimit || quality.template_gain < kLossGainMinimum;
    return decide(decision.poor_fit, quality.requested_shift_hz, quality.loss_counter);
}

TrackingQualityCoefficients prepareTrackingQuality(double spacing_hz, double template_energy)
{
    TrackingQualityCoefficients coefficients;
    if (!std::isfinite(spacing_hz) || spacing_hz <= 0.0 ||
        !std::isfinite(template_energy) || template_energy < kEpsilon) return coefficients;
    coefficients.maximum_shift_hz = kLossShiftFraction * spacing_hz;
    coefficients.residual_coefficient = kLossResidualLimit * template_energy;
    coefficients.residual_floor = kLossResidualLimit * kEpsilon;
    coefficients.minimum_gain_squared = kLossGainMinimum * kLossGainMinimum;
    coefficients.valid = std::isfinite(coefficients.maximum_shift_hz) &&
                         std::isfinite(coefficients.residual_coefficient);
    return coefficients;
}

TrackingQualityDecision evaluateTrackingQualitySums(const TrackingQualityCoefficients& coefficients,
                                                   const TrackingQualitySums& quality)
{
    const double residual_limit = coefficients.residual_coefficient * quality.gain_squared + coefficients.residual_floor;
    const bool poor_fit = !coefficients.valid || !quality.fit_valid ||
        !std::isfinite(quality.requested_shift_hz) || !std::isfinite(quality.rss) || quality.rss < 0.0 ||
        !std::isfinite(quality.gain_squared) || quality.gain_squared < 0.0 ||
        !std::isfinite(quality.frequency_variance_hz2) || quality.frequency_variance_hz2 < 0.0 ||
        !std::isfinite(residual_limit) ||
        std::abs(quality.requested_shift_hz) > coefficients.maximum_shift_hz ||
        quality.rss > residual_limit || quality.gain_squared < coefficients.minimum_gain_squared;
    // The legacy tracking policy has no positive upper SE limit. Fast relock
    // uses a separate precomputed RSS limit for its uncertainty gate.
    return decide(poor_fit, quality.requested_shift_hz, quality.loss_counter);
}
