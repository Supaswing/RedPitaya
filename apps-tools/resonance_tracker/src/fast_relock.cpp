#include "fast_relock.hpp"

#include <cmath>

namespace FastRelock {
namespace {
using Complex = std::complex<double>;
constexpr double kEpsilon = 1e-18;
bool finite(const Complex& value)
{
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}
}

Prepared prepare(const Values& reference, const Values& derivative_per_hz, const Limits& limits)
{
    Prepared prepared;
    if (!std::isfinite(limits.maximum_shift_hz) || limits.maximum_shift_hz <= 0.0 ||
        !std::isfinite(limits.maximum_se_hz) || limits.maximum_se_hz <= 0.0 ||
        !std::isfinite(limits.maximum_residual) || limits.maximum_residual < 0.0 ||
        !std::isfinite(limits.minimum_gain) || limits.minimum_gain < 0.0) return prepared;
    Complex mean_t{}, mean_d{};
    for (std::size_t j = 0; j < 6; ++j) {
        if (!finite(reference[j]) || !finite(derivative_per_hz[j])) return prepared;
        mean_t += reference[j] / 6.0;
        mean_d += derivative_per_hz[j] / 6.0;
    }
    prepared.reference = reference;
    double template_energy = 0.0;
    Complex td{};
    for (std::size_t j = 0; j < 6; ++j) {
        const Complex t = reference[j] - mean_t;
        const Complex d = derivative_per_hz[j] - mean_d;
        prepared.centered_reference[j] = t;
        prepared.centered_derivative[j] = d;
        template_energy += std::norm(t);
        td += std::conj(t) * d;
    }
    if (!std::isfinite(template_energy) || template_energy < kEpsilon) return prepared;
    // Eliminate the complex gain and constant offset, leaving a real shift.
    const Complex derivative_gain = td / template_energy;
    Values projected_derivative{};
    double information = 0.0;
    for (std::size_t j = 0; j < 6; ++j) {
        projected_derivative[j] = prepared.centered_derivative[j] - derivative_gain * prepared.centered_reference[j];
        information += std::norm(projected_derivative[j]);
    }
    if (!std::isfinite(information) || information < kEpsilon) return prepared;
    for (std::size_t j = 0; j < 6; ++j) {
        prepared.shift_weights[j] = -projected_derivative[j] / information;
        prepared.gain_weights[j] = prepared.centered_reference[j] / template_energy;
        if (!finite(prepared.shift_weights[j]) || !finite(prepared.gain_weights[j])) return Prepared{};
    }
    prepared.derivative_gain = derivative_gain;
    prepared.template_energy = template_energy;
    // Twelve real observations minus shift, complex gain, and complex offset.
    prepared.se_variance_per_rss = 1.0 / (7.0 * information);
    prepared.maximum_shift_hz = limits.maximum_shift_hz;
    prepared.maximum_rss_for_se = 7.0 * information * limits.maximum_se_hz * limits.maximum_se_hz;
    prepared.residual_coefficient = limits.maximum_residual * template_energy;
    // Retain the reference implementation's denominator floor exactly.
    prepared.residual_floor = limits.maximum_residual * kEpsilon;
    prepared.minimum_gain_squared = limits.minimum_gain * limits.minimum_gain;
    prepared.valid = finite(derivative_gain) && std::isfinite(prepared.se_variance_per_rss) &&
                     prepared.se_variance_per_rss > 0.0 && std::isfinite(prepared.maximum_rss_for_se) &&
                     prepared.maximum_rss_for_se > 0.0 && std::isfinite(prepared.residual_coefficient) &&
                     std::isfinite(prepared.residual_floor) && std::isfinite(prepared.minimum_gain_squared);
    return prepared;
}

KernelResult evaluate(const Prepared& prepared, const Values& measured)
{
    KernelResult result;
    if (!prepared.valid) return result;
    constexpr double kInverseCount = 0.16666666666666666667;
    Complex mean_e{};
    Values e{};
    for (std::size_t j = 0; j < 6; ++j) {
        if (!finite(measured[j])) return result;
        e[j] = measured[j] - prepared.reference[j];
        mean_e += e[j] * kInverseCount;
    }
    Complex error_gain{};
    for (std::size_t j = 0; j < 6; ++j) {
        e[j] -= mean_e;
        // The derivative is already orthogonal to the template: no live
        // projection or reciprocal is needed in the shift MAC.
        result.shift_hz += prepared.shift_weights[j].real() * e[j].real() +
                           prepared.shift_weights[j].imag() * e[j].imag();
        error_gain += std::conj(prepared.gain_weights[j]) * e[j];
    }
    const Complex gain_change = error_gain + result.shift_hz * prepared.derivative_gain;
    result.gain_squared = std::norm(Complex(1.0, 0.0) + gain_change);
    for (std::size_t j = 0; j < 6; ++j)
        result.rss += std::norm(e[j] + result.shift_hz * prepared.centered_derivative[j] -
                               gain_change * prepared.centered_reference[j]);
    const double residual_limit = prepared.residual_coefficient * result.gain_squared + prepared.residual_floor;
    result.valid = std::isfinite(result.shift_hz) && std::isfinite(result.rss) && result.rss >= 0.0 &&
                   std::isfinite(result.gain_squared) && result.gain_squared >= 0.0 && std::isfinite(residual_limit);
    result.accepted = result.valid && std::abs(result.shift_hz) <= prepared.maximum_shift_hz &&
                      result.rss <= prepared.maximum_rss_for_se && result.rss <= residual_limit &&
                      result.gain_squared >= prepared.minimum_gain_squared;
    return result;
}

Result describe(const Prepared& prepared, const KernelResult& kernel)
{
    Result result;
    result.valid = prepared.valid && kernel.valid;
    if (!result.valid) return result;
    result.accepted = kernel.accepted;
    result.shift_hz = kernel.shift_hz;
    result.gain = std::sqrt(kernel.gain_squared);
    result.se_hz = std::sqrt(kernel.rss * prepared.se_variance_per_rss);
    result.normalized_residual = kernel.rss / (kernel.gain_squared * prepared.template_energy + kEpsilon);
    return result;
}

Result estimate(const Input& input, const Limits& limits)
{
    const auto prepared = prepare(input.reference, input.derivative_per_hz, limits);
    return describe(prepared, evaluate(prepared, input.measured));
}
}
