#include "tracker_engine.hpp"

#include "tracking_quality.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kEpsilon = 1e-18;
constexpr std::size_t kParameters = 5;

using Complex = std::complex<double>;
using Matrix5 = std::array<std::array<double, kParameters>, kParameters>;

bool invert5x5(Matrix5 matrix, Matrix5& inverse)
{
    for (std::size_t row = 0; row < kParameters; ++row)
        for (std::size_t column = 0; column < kParameters; ++column)
            inverse[row][column] = row == column ? 1.0 : 0.0;
    for (std::size_t column = 0; column < kParameters; ++column) {
        std::size_t pivot = column;
        double largest = std::abs(matrix[column][column]);
        for (std::size_t row = column + 1; row < kParameters; ++row) {
            const double candidate = std::abs(matrix[row][column]);
            if (candidate > largest) {
                largest = candidate;
                pivot = row;
            }
        }
        if (largest < kEpsilon) return false;
        if (pivot != column) {
            std::swap(matrix[column], matrix[pivot]);
            std::swap(inverse[column], inverse[pivot]);
        }
        const double divisor = matrix[column][column];
        for (std::size_t index = 0; index < kParameters; ++index) {
            matrix[column][index] /= divisor;
            inverse[column][index] /= divisor;
        }
        for (std::size_t row = 0; row < kParameters; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (std::size_t index = 0; index < kParameters; ++index) {
                matrix[row][index] -= factor * matrix[column][index];
                inverse[row][index] -= factor * inverse[column][index];
            }
        }
    }
    return true;
}

std::array<double, kParameters> designRow(const Complex& sample, const Complex& derivative, bool imaginary)
{
    if (!imaginary) return {-derivative.real(), sample.real(), -sample.imag(), 1.0, 0.0};
    return {-derivative.imag(), sample.imag(), sample.real(), 0.0, 1.0};
}

double featureEnergy(const std::array<Complex, 5>& values, std::size_t first, std::size_t last)
{
    Complex mean{};
    const double count = static_cast<double>(last - first + 1);
    for (std::size_t point = first; point <= last; ++point) mean += values[point] / count;
    double energy = 0.0;
    for (std::size_t point = first; point <= last; ++point) energy += std::norm(values[point] - mean);
    return energy;
}
}

bool FrequencyTracker::prepare(PreparedResonance& resonance)
{
    resonance.derivative_iq[0] = resonance.template_iq[1] - resonance.template_iq[0];
    resonance.derivative_iq[4] = resonance.template_iq[4] - resonance.template_iq[3];
    for (std::size_t point = 1; point < 4; ++point)
        resonance.derivative_iq[point] = 0.5 * (resonance.template_iq[point + 1] -
                                                resonance.template_iq[point - 1]);
    Matrix5 normal{};
    for (std::size_t point = 0; point < 5; ++point) {
        for (bool imaginary : {false, true}) {
            const auto row = designRow(resonance.template_iq[point], resonance.derivative_iq[point], imaginary);
            for (std::size_t i = 0; i < kParameters; ++i)
                for (std::size_t j = 0; j < kParameters; ++j) normal[i][j] += row[i] * row[j];
        }
    }
    return invert5x5(normal, resonance.normal_inverse);
}

bool FrequencyTracker::configure(const std::vector<ResonanceEstimate>& resonances, std::size_t points,
                                 std::string& error)
{
    resonances_.clear();
    points_ = 0;
    if (points != 3 && points != 5) {
        error = "tracking point count must be 3 or 5";
        return false;
    }
    if (resonances.empty() || resonances.size() > 2) {
        error = "tracking requires one or two baseline resonances";
        return false;
    }
    for (const auto& source : resonances) {
        if (!source.valid || source.spacing_hz <= 0.0 || source.template_points.size() != 5) {
            error = "tracking requires a valid five-point baseline template";
            resonances_.clear();
            return false;
        }
        PreparedResonance resonance;
        resonance.sensor_id = source.sensor_id;
        resonance.baseline_frequency_hz = source.frequency_hz;
        resonance.tracked_frequency_hz = source.frequency_hz;
        resonance.baseline_q = source.q;
        resonance.spacing_hz = source.spacing_hz;
        for (std::size_t point = 0; point < 5; ++point)
            resonance.template_iq[point] = Complex(source.template_points[point].real,
                                                   source.template_points[point].imag);
        if (!prepare(resonance)) {
            error = "tracking template normal matrix is singular";
            resonances_.clear();
            return false;
        }
        resonances_.push_back(resonance);
    }
    points_ = points;
    return true;
}

bool FrequencyTracker::configured() const
{
    return points_ == 3 || points_ == 5;
}

std::size_t FrequencyTracker::pointCount() const
{
    return points_;
}

bool FrequencyTracker::fitFive(const PreparedResonance& resonance, const std::array<Complex, 5>& live,
                               TrackingSensorResult& result) const
{
    std::array<double, kParameters> xty{};
    std::array<double, kParameters> parameters{};
    for (std::size_t point = 0; point < 5; ++point) {
        for (bool imaginary : {false, true}) {
            const double value = imaginary ? (live[point] - resonance.template_iq[point]).imag()
                                           : (live[point] - resonance.template_iq[point]).real();
            const auto row = designRow(resonance.template_iq[point], resonance.derivative_iq[point], imaginary);
            for (std::size_t index = 0; index < kParameters; ++index) xty[index] += row[index] * value;
        }
    }
    for (std::size_t i = 0; i < kParameters; ++i)
        for (std::size_t j = 0; j < kParameters; ++j)
            parameters[i] += resonance.normal_inverse[i][j] * xty[j];
    double rss = 0.0;
    for (std::size_t point = 0; point < 5; ++point) {
        for (bool imaginary : {false, true}) {
            const auto row = designRow(resonance.template_iq[point], resonance.derivative_iq[point], imaginary);
            double predicted = 0.0;
            for (std::size_t index = 0; index < kParameters; ++index)
                predicted += row[index] * parameters[index];
            const double observed = imaginary ? (live[point] - resonance.template_iq[point]).imag()
                                              : (live[point] - resonance.template_iq[point]).real();
            const double residual = observed - predicted;
            rss += residual * residual;
        }
    }
    result.requested_shift_hz = parameters[0] * resonance.spacing_hz;
    result.template_gain = std::hypot(1.0 + parameters[1], parameters[2]);
    result.normalized_residual = rss /
        (result.template_gain * result.template_gain * featureEnergy(resonance.template_iq, 0, 4) + kEpsilon);
    const double variance = (rss / 5.0) * resonance.normal_inverse[0][0];
    result.frequency_se_hz = variance > 0.0 ? resonance.spacing_hz * std::sqrt(variance) : 0.0;
    return true;
}

bool FrequencyTracker::fitThree(const PreparedResonance& resonance, const std::array<Complex, 5>& live,
                                TrackingSensorResult& result) const
{
    Complex mean_residual{};
    Complex mean_derivative{};
    for (std::size_t point = 1; point <= 3; ++point) {
        mean_residual += (live[point] - resonance.template_iq[point]) / 3.0;
        mean_derivative += resonance.derivative_iq[point] / 3.0;
    }
    double numerator = 0.0;
    double denominator = 0.0;
    for (std::size_t point = 1; point <= 3; ++point) {
        const Complex residual = live[point] - resonance.template_iq[point] - mean_residual;
        const Complex derivative = resonance.derivative_iq[point] - mean_derivative;
        numerator += derivative.real() * residual.real() + derivative.imag() * residual.imag();
        denominator += std::norm(derivative);
    }
    if (denominator < kEpsilon) return false;
    result.requested_shift_hz = -resonance.spacing_hz * numerator / denominator;
    std::array<Complex, 3> shifted_template{};
    Complex template_mean{};
    Complex live_mean{};
    for (std::size_t point = 1; point <= 3; ++point) {
        const std::size_t local = point - 1;
        shifted_template[local] = resonance.template_iq[point] -
            (result.requested_shift_hz / resonance.spacing_hz) * resonance.derivative_iq[point];
        template_mean += shifted_template[local] / 3.0;
        live_mean += live[point] / 3.0;
    }
    Complex gain_numerator{};
    double energy = 0.0;
    for (std::size_t point = 0; point < 3; ++point) {
        const Complex template_value = shifted_template[point] - template_mean;
        gain_numerator += std::conj(template_value) * (live[point + 1] - live_mean);
        energy += std::norm(template_value);
    }
    if (energy < kEpsilon) return false;
    const Complex gain = gain_numerator / energy;
    result.template_gain = std::abs(gain);
    double rss = 0.0;
    for (std::size_t point = 0; point < 3; ++point) {
        const Complex residual = live[point + 1] - live_mean - gain * (shifted_template[point] - template_mean);
        rss += std::norm(residual);
    }
    result.frequency_se_hz = resonance.spacing_hz * std::sqrt(rss / denominator);
    result.normalized_residual = rss / (result.template_gain * result.template_gain * energy + kEpsilon);
    return true;
}

double FrequencyTracker::calculateLiveQ(const PreparedResonance& resonance, const std::array<Complex, 5>& live,
                                        double frequency_hz) const
{
    std::array<double, 5> y{};
    const std::size_t first = points_ == 3 ? 1 : 0;
    const std::size_t last = points_ == 3 ? 3 : 4;
    for (std::size_t point = first; point <= last; ++point) y[point] = std::norm(live[point]);
    double a = 0.0, b = 0.0, c = 0.0;
    if (points_ == 3) {
        a = 0.5 * (y[1] + y[3] - 2.0 * y[2]);
        b = 0.5 * (y[3] - y[1]);
        c = y[2];
    } else {
        const double sum0 = y[0] + y[1] + y[2] + y[3] + y[4];
        const double sum2 = 4.0 * y[0] + y[1] + y[3] + 4.0 * y[4];
        a = (5.0 * sum2 - 10.0 * sum0) / 70.0;
        b = (-2.0 * y[0] - y[1] + y[3] + 2.0 * y[4]) / 10.0;
        c = (34.0 * sum0 - 10.0 * sum2) / 70.0;
    }
    if (a <= kEpsilon) return resonance.baseline_q;
    const double vertex = -b / (2.0 * a);
    const double limit = points_ == 3 ? 1.0 : 2.0;
    if (vertex < -limit || vertex > limit) return resonance.baseline_q;
    const double edge = 0.5 * (y[first] + y[last]);
    const double minimum = c - b * b / (4.0 * a);
    if (edge <= minimum) return resonance.baseline_q;
    const double half_width = std::sqrt((edge - minimum) / a);
    if (half_width < 0.1) return resonance.baseline_q;
    return frequency_hz / (2.0 * half_width * resonance.spacing_hz);
}

TrackingFrame FrequencyTracker::acquire(std::uint64_t sequence, ComplexMeasurementSource& source,
                                        const CancellationCheck& cancelled)
{
    TrackingFrame frame;
    frame.sequence = sequence;
    frame.points_per_sensor = points_;
    if (!configured() || resonances_.empty()) {
        frame.error = "tracker is not configured";
        return frame;
    }
    bool first_measurement = true;
    for (auto& resonance : resonances_) {
        std::array<Complex, 5> live{};
        const int first_offset = points_ == 3 ? -1 : -2;
        const int last_offset = points_ == 3 ? 1 : 2;
        for (int offset = first_offset; offset <= last_offset; ++offset) {
            if (cancelled && cancelled()) {
                frame.error = "cancelled";
                return frame;
            }
            const std::uint32_t frequency = static_cast<std::uint32_t>(
                std::max(1.0, std::floor(resonance.tracked_frequency_hz + offset * resonance.spacing_hz + 0.5)));
            ComplexMeasurement measurement;
            if (!source.acquire(frequency, first_measurement, measurement, frame.error)) return frame;
            first_measurement = false;
            const std::size_t point = static_cast<std::size_t>(offset + 2);
            live[point] = Complex(measurement.real, measurement.imag);
            frame.points.push_back({resonance.sensor_id, offset, measurement});
        }
        TrackingSensorResult result;
        result.sensor_id = resonance.sensor_id;
        result.fit_valid = points_ == 5 ? fitFive(resonance, live, result) : fitThree(resonance, live, result);
        if (!result.fit_valid) {
            result.requested_shift_hz = 0.0;
            result.frequency_se_hz = resonance.spacing_hz;
            result.normalized_residual = 1.0;
            result.template_gain = 0.0;
        }
        TrackingQualityInput quality;
        quality.fit_valid = result.fit_valid;
        quality.requested_shift_hz = result.requested_shift_hz;
        quality.frequency_se_hz = result.frequency_se_hz;
        quality.spacing_hz = resonance.spacing_hz;
        quality.normalized_residual = result.normalized_residual;
        quality.template_gain = result.template_gain;
        quality.loss_counter = resonance.loss_counter;
        const TrackingQualityDecision decision = evaluateTrackingQuality(quality);
        resonance.loss_counter = decision.loss_counter;
        result.poor_fit = decision.poor_fit;
        result.recovery_required = decision.recovery_required;
        result.loss_counter = decision.loss_counter;
        result.applied_shift_hz = std::clamp(decision.applied_shift_hz,
                                             -0.5 * resonance.spacing_hz, 0.5 * resonance.spacing_hz);
        resonance.tracked_frequency_hz += result.applied_shift_hz;
        result.frequency_hz = resonance.tracked_frequency_hz;
        result.q = calculateLiveQ(resonance, live, result.frequency_hz);
        frame.degraded = frame.degraded || result.poor_fit;
        frame.recovery_required = frame.recovery_required || result.recovery_required;
        frame.sensors.push_back(result);
    }
    frame.complete = true;
    return frame;
}
