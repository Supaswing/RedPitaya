#include "complex_response.hpp"
#include "fast_relock.hpp"
#include "five_point_tracker.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

namespace {
using Complex = std::complex<double>;
constexpr double kEpsilon = 1e-18;

bool close(double a, double b, double relative = 1e-8, double absolute = 1e-7)
{
    return std::abs(a - b) <= absolute + relative * std::max(std::abs(a), std::abs(b));
}

Complex response(double z)
{
    return Complex(1, 0.1) + Complex(-0.6, 0.18) / Complex(1, z);
}

// Original runtime equations: independent projection of the live error and
// division-based metrics/acceptance, retained as the numerical reference.
FastRelock::Result originalFast(const FastRelock::Input& input, const FastRelock::Limits& limits)
{
    FastRelock::Result result;
    Complex mean_t{}, mean_d{}, mean_e{};
    for (std::size_t j = 0; j < 6; ++j) {
        mean_t += input.reference[j] / 6.0;
        mean_d += input.derivative_per_hz[j] / 6.0;
        mean_e += (input.measured[j] - input.reference[j]) / 6.0;
    }
    FastRelock::Values t{}, d{}, e{};
    double energy = 0;
    Complex td{}, te{};
    for (std::size_t j = 0; j < 6; ++j) {
        t[j] = input.reference[j] - mean_t;
        d[j] = input.derivative_per_hz[j] - mean_d;
        e[j] = input.measured[j] - input.reference[j] - mean_e;
        energy += std::norm(t[j]);
        td += std::conj(t[j]) * d[j];
        te += std::conj(t[j]) * e[j];
    }
    if (energy < kEpsilon) return result;
    const Complex derivative_gain = td / energy;
    const Complex error_gain = te / energy;
    double information = 0, numerator = 0;
    for (std::size_t j = 0; j < 6; ++j) {
        const Complex pd = d[j] - derivative_gain * t[j];
        const Complex pe = e[j] - error_gain * t[j];
        information += std::norm(pd);
        numerator += (std::conj(pd) * pe).real();
    }
    if (information < kEpsilon) return result;
    result.shift_hz = -numerator / information;
    const Complex alpha = error_gain + result.shift_hz * derivative_gain;
    result.gain = std::abs(Complex(1, 0) + alpha);
    double rss = 0;
    for (std::size_t j = 0; j < 6; ++j)
        rss += std::norm(e[j] + result.shift_hz * d[j] - alpha * t[j]);
    result.se_hz = std::sqrt((rss / 7.0) / information);
    result.normalized_residual = rss / (result.gain * result.gain * energy + kEpsilon);
    result.valid = true;
    result.accepted = std::abs(result.shift_hz) <= limits.maximum_shift_hz &&
        result.se_hz <= limits.maximum_se_hz && result.normalized_residual <= limits.maximum_residual &&
        result.gain >= limits.minimum_gain;
    return result;
}

void fastEquivalence()
{
    std::mt19937 random(7102026);
    std::uniform_real_distribution<double> noise(-0.02, 0.02);
    FastRelock::Limits limits{60000, 24000, 0.10, 0.25};
    for (double scale : {1.0, 4096.0, 1000000.0}) {
        FastRelock::Input input{};
        for (std::size_t j = 0; j < 6; ++j) {
            const Complex denominator(1, FastRelock::kOffsetsHz[j] / 120000.0);
            input.reference[j] = scale * response(FastRelock::kOffsetsHz[j] / 120000.0);
            input.derivative_per_hz[j] = scale * Complex(-0.6, 0.18) * Complex(0, -1.0 / 120000.0) /
                (denominator * denominator);
        }
        const auto prepared = FastRelock::prepare(input.reference, input.derivative_per_hz, limits);
        assert(prepared.valid);
        // Reuse one coefficient bank over changing live frames.
        for (double shift : {-100000.0, -30000.0, 0.0, 30000.0, 100000.0}) {
            for (Complex alpha : {Complex(0.05, 0.02), Complex(-0.9, 0), Complex(0.3, -0.4)}) {
                for (int frame = 0; frame < 8; ++frame) {
                    for (std::size_t j = 0; j < 6; ++j)
                        input.measured[j] = input.reference[j] - shift * input.derivative_per_hz[j] +
                            alpha * input.reference[j] + scale * Complex(0.2 + noise(random), -0.1 + noise(random));
                    const auto expected = originalFast(input, limits);
                    const auto kernel = FastRelock::evaluate(prepared, input.measured);
                    const auto actual = FastRelock::describe(prepared, kernel);
                    assert(actual.valid == expected.valid && actual.accepted == expected.accepted);
                    assert(close(actual.shift_hz, expected.shift_hz));
                    assert(close(actual.gain, expected.gain));
                    assert(close(actual.se_hz, expected.se_hz));
                    assert(close(actual.normalized_residual, expected.normalized_residual));
                }
            }
        }
    }
}

TrackingQualitySums originalFive(const FivePointTracking::Prepared& prepared,
                                 const FivePointTracking::Values& measured)
{
    std::array<std::array<double, 5>, 10> rows{};
    std::array<double, 10> errors{};
    std::array<double, 5> xty{}, parameters{};
    for (std::size_t j = 0; j < 5; ++j) {
        const auto& t = prepared.reference[j];
        const auto& d = prepared.derivative[j];
        rows[2 * j] = {-d.real(), t.real(), -t.imag(), 1, 0};
        rows[2 * j + 1] = {-d.imag(), t.imag(), t.real(), 0, 1};
        errors[2 * j] = (measured[j] - t).real();
        errors[2 * j + 1] = (measured[j] - t).imag();
    }
    for (std::size_t j = 0; j < 10; ++j)
        for (std::size_t i = 0; i < 5; ++i) xty[i] += rows[j][i] * errors[j];
    for (std::size_t i = 0; i < 5; ++i)
        for (std::size_t j = 0; j < 5; ++j) parameters[i] += prepared.normal_inverse[i][j] * xty[j];
    TrackingQualitySums result;
    for (std::size_t j = 0; j < 10; ++j) {
        double predicted = 0;
        for (std::size_t i = 0; i < 5; ++i) predicted += rows[j][i] * parameters[i];
        result.rss += (errors[j] - predicted) * (errors[j] - predicted);
    }
    result.fit_valid = true;
    result.requested_shift_hz = parameters[0] * prepared.spacing_hz;
    const double gain = std::hypot(1 + parameters[1], parameters[2]);
    result.gain_squared = gain * gain;
    const double se = prepared.spacing_hz * std::sqrt((result.rss / 5.0) * prepared.normal_inverse[0][0]);
    result.frequency_variance_hz2 = se * se;
    return result;
}

void fiveEquivalence()
{
    std::mt19937 random(7102026);
    std::uniform_real_distribution<double> noise(-0.02, 0.02);
    for (double scale : {1.0, 4096.0, 1000000.0}) {
        FivePointTracking::Values reference{}, live{};
        for (std::size_t j = 0; j < 5; ++j) reference[j] = scale * response((static_cast<double>(j) - 2) * 0.5);
        const auto prepared = FivePointTracking::prepare(reference, 60000);
        assert(prepared.valid);
        for (int frame = 0; frame < 80; ++frame) {
            const double shift_steps = (frame - 40) * 0.02;
            const Complex alpha(frame % 2 ? 0.05 : -0.9, frame % 2 ? 0.02 : 0);
            for (std::size_t j = 0; j < 5; ++j)
                live[j] = reference[j] - shift_steps * prepared.derivative[j] + alpha * reference[j] +
                    scale * Complex(0.2 + noise(random), -0.1 + noise(random));
            const auto expected = originalFive(prepared, live);
            const auto actual = FivePointTracking::evaluate(prepared, live);
            assert(actual.fit_valid);
            assert(close(actual.requested_shift_hz, expected.requested_shift_hz));
            assert(close(actual.rss, expected.rss));
            assert(close(actual.gain_squared, expected.gain_squared));
            assert(close(actual.frequency_variance_hz2, expected.frequency_variance_hz2));
            TrackingQualityInput legacy;
            legacy.fit_valid = true;
            legacy.spacing_hz = prepared.spacing_hz;
            legacy.requested_shift_hz = expected.requested_shift_hz;
            legacy.frequency_se_hz = std::sqrt(expected.frequency_variance_hz2);
            legacy.template_gain = std::sqrt(expected.gain_squared);
            legacy.normalized_residual = expected.rss / (expected.gain_squared * prepared.template_energy + kEpsilon);
            assert(evaluateTrackingQuality(legacy).poor_fit ==
                evaluateTrackingQualitySums(prepared.quality, actual).poor_fit);
        }
        live[0] = Complex(std::numeric_limits<double>::infinity(), 0);
        assert(!FivePointTracking::evaluate(prepared, live).fit_valid);
    }
    FivePointTracking::Values flat{};
    flat.fill(Complex(1, 0));
    assert(!FivePointTracking::prepare(flat, 60000).valid);
    assert(!FivePointTracking::prepare(flat, 0).valid);
    flat[0] = Complex(std::numeric_limits<double>::quiet_NaN(), 0);
    assert(!FivePointTracking::prepare(flat, 60000).valid);
}

void qualityBoundaries()
{
    const auto coefficients = prepareTrackingQuality(60000, 2.0);
    assert(coefficients.valid);
    TrackingQualitySums sums;
    sums.fit_valid = true;
    sums.requested_shift_hz = coefficients.maximum_shift_hz;
    sums.gain_squared = coefficients.minimum_gain_squared;
    sums.rss = coefficients.residual_coefficient * sums.gain_squared + coefficients.residual_floor;
    assert(!evaluateTrackingQualitySums(coefficients, sums).poor_fit);
    auto invalid = sums;
    invalid.requested_shift_hz = std::nextafter(sums.requested_shift_hz, INFINITY);
    assert(evaluateTrackingQualitySums(coefficients, invalid).poor_fit);
    invalid = sums;
    invalid.gain_squared = std::nextafter(sums.gain_squared, 0.0);
    assert(evaluateTrackingQualitySums(coefficients, invalid).poor_fit);
    invalid = sums;
    invalid.rss = std::nextafter(sums.rss, INFINITY);
    assert(evaluateTrackingQualitySums(coefficients, invalid).poor_fit);
    invalid.loss_counter = std::numeric_limits<std::uint32_t>::max();
    const auto saturated = evaluateTrackingQualitySums(coefficients, invalid);
    assert(saturated.loss_counter == 3 && saturated.recovery_required && saturated.applied_shift_hz == 0);
    sums.loss_counter = 3;
    assert(evaluateTrackingQualitySums(coefficients, sums).loss_counter == 0);
    for (double value : {-1.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        invalid = sums;
        invalid.frequency_variance_hz2 = value;
        assert(evaluateTrackingQualitySums(coefficients, invalid).poor_fit);
        invalid = sums;
        invalid.rss = value;
        assert(evaluateTrackingQualitySums(coefficients, invalid).poor_fit);
        invalid = sums;
        invalid.gain_squared = value;
        assert(evaluateTrackingQualitySums(coefficients, invalid).poor_fit);
    }
    invalid = sums;
    invalid.gain_squared = std::numeric_limits<double>::max();
    const auto large = prepareTrackingQuality(60000, 100);
    assert(evaluateTrackingQualitySums(large, invalid).poor_fit); // Threshold product overflow.
    assert(!prepareTrackingQuality(0, 2).valid);
    assert(!prepareTrackingQuality(60000, 0).valid);
}

void fastThresholdsAndInvalidData()
{
    FastRelock::Input input{};
    FastRelock::Limits limits{60000, 24000, 0.1, 0.25};
    for (std::size_t j = 0; j < 6; ++j) {
        const double z = FastRelock::kOffsetsHz[j] / 120000.0;
        input.reference[j] = response(z);
        input.derivative_per_hz[j] = Complex(-0.6, 0.18) * Complex(0, -1.0 / 120000.0) /
            (Complex(1, z) * Complex(1, z));
        input.measured[j] = input.reference[j] - 30000.0 * input.derivative_per_hz[j];
    }
    input.measured[0] += Complex(0.01, -0.015);
    auto prepared = FastRelock::prepare(input.reference, input.derivative_per_hz, limits);
    const auto kernel = FastRelock::evaluate(prepared, input.measured);
    assert(kernel.accepted && kernel.rss > 0);
    prepared.maximum_rss_for_se = kernel.rss;
    assert(FastRelock::evaluate(prepared, input.measured).accepted);
    prepared.maximum_rss_for_se = std::nextafter(kernel.rss, 0.0);
    assert(!FastRelock::evaluate(prepared, input.measured).accepted);
    prepared = FastRelock::prepare(input.reference, input.derivative_per_hz, limits);
    prepared.minimum_gain_squared = kernel.gain_squared;
    assert(FastRelock::evaluate(prepared, input.measured).accepted);
    prepared.minimum_gain_squared = std::nextafter(kernel.gain_squared, INFINITY);
    assert(!FastRelock::evaluate(prepared, input.measured).accepted);
    prepared = FastRelock::prepare(input.reference, input.derivative_per_hz, limits);
    prepared.maximum_shift_hz = std::abs(kernel.shift_hz);
    assert(FastRelock::evaluate(prepared, input.measured).accepted);
    prepared.maximum_shift_hz = std::nextafter(prepared.maximum_shift_hz, 0.0);
    assert(!FastRelock::evaluate(prepared, input.measured).accepted);
    prepared = FastRelock::prepare(input.reference, input.derivative_per_hz, limits);
    prepared.residual_coefficient = 0;
    prepared.residual_floor = kernel.rss;
    assert(FastRelock::evaluate(prepared, input.measured).accepted);
    prepared.residual_floor = std::nextafter(kernel.rss, 0.0);
    assert(!FastRelock::evaluate(prepared, input.measured).accepted);
    limits.maximum_se_hz = std::numeric_limits<double>::max();
    assert(!FastRelock::prepare(input.reference, input.derivative_per_hz, limits).valid);
    input.measured[0] = Complex(std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
    assert(!FastRelock::evaluate(prepared, input.measured).valid);
}

void rawConversion()
{
    RawIqSample sample;
    sample.ref_i = std::numeric_limits<std::int32_t>::min();
    sample.ref_q = std::numeric_limits<std::int32_t>::max();
    Complex value;
    std::string error;
    assert(convertRawIqResponse(sample, ComplexResponseRepresentation::RawReflected, value, error));
    assert(value.real() == -2147483648.0 && value.imag() == 2147483647.0);
    assert(!convertRawIqResponse(sample, ComplexResponseRepresentation::IncidentNormalized, value, error));
    sample.inc_i = 3;
    sample.inc_q = 4;
    sample.ref_i = -11;
    sample.ref_q = 2;
    assert(convertRawIqResponse(sample, ComplexResponseRepresentation::IncidentNormalized, value, error));
    assert(std::abs(value - Complex(-1, 2)) < 1e-12);
}
}

int main()
{
    fastEquivalence();
    fiveEquivalence();
    qualityBoundaries();
    fastThresholdsAndInvalidData();
    rawConversion();
}
