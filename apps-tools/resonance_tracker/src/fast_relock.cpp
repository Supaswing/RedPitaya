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

Result estimate(const Input& input, const Limits& limits)
{
    Result result;
    if (!std::isfinite(limits.maximum_shift_hz) || limits.maximum_shift_hz <= 0.0 ||
        !std::isfinite(limits.maximum_se_hz) || limits.maximum_se_hz <= 0.0 ||
        !std::isfinite(limits.maximum_residual) || limits.maximum_residual < 0.0 ||
        !std::isfinite(limits.minimum_gain) || limits.minimum_gain < 0.0) return result;
    Complex mean_t{}, mean_d{}, mean_e{};
    for (std::size_t j = 0; j < 6; ++j) {
        if (!finite(input.reference[j]) || !finite(input.derivative_per_hz[j]) ||
            !finite(input.measured[j])) return result;
        mean_t += input.reference[j] / 6.0;
        mean_d += input.derivative_per_hz[j] / 6.0;
        mean_e += (input.measured[j] - input.reference[j]) / 6.0;
    }
    Values t{}, d{}, e{};
    double template_energy = 0.0;
    Complex td{}, te{};
    for (std::size_t j = 0; j < 6; ++j) {
        t[j] = input.reference[j] - mean_t;
        d[j] = input.derivative_per_hz[j] - mean_d;
        e[j] = input.measured[j] - input.reference[j] - mean_e;
        template_energy += std::norm(t[j]);
        td += std::conj(t[j]) * d[j];
        te += std::conj(t[j]) * e[j];
    }
    if (!std::isfinite(template_energy) || template_energy < kEpsilon) return result;
    // Eliminate the complex gain and constant offset, leaving a real shift.
    const Complex derivative_gain = td / template_energy;
    const Complex error_gain = te / template_energy;
    double information = 0.0, numerator = 0.0;
    for (std::size_t j = 0; j < 6; ++j) {
        const Complex projected_d = d[j] - derivative_gain * t[j];
        const Complex projected_e = e[j] - error_gain * t[j];
        information += std::norm(projected_d);
        numerator += (std::conj(projected_d) * projected_e).real();
    }
    if (!std::isfinite(information) || information < kEpsilon) return result;
    result.shift_hz = -numerator / information;
    const Complex gain_change = error_gain + result.shift_hz * derivative_gain;
    result.gain = std::abs(Complex(1.0, 0.0) + gain_change);
    double rss = 0.0;
    for (std::size_t j = 0; j < 6; ++j)
        rss += std::norm(e[j] + result.shift_hz * d[j] - gain_change * t[j]);
    // Twelve real observations minus shift, complex gain, and complex offset.
    result.se_hz = std::sqrt((rss / 7.0) / information);
    result.normalized_residual = rss / (result.gain * result.gain * template_energy + kEpsilon);
    result.valid = std::isfinite(result.shift_hz) && std::isfinite(result.se_hz) &&
                   std::isfinite(result.gain) && std::isfinite(result.normalized_residual);
    result.accepted = result.valid && std::abs(result.shift_hz) <= limits.maximum_shift_hz &&
                      result.se_hz <= limits.maximum_se_hz &&
                      result.normalized_residual <= limits.maximum_residual && result.gain >= limits.minimum_gain;
    return result;
}
}
