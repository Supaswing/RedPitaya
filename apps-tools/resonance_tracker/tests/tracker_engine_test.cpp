#include "tracker_engine.hpp"
#include "tracking_quality.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace {
using Complex = std::complex<double>;

Complex response(double frequency_hz, double center_hz, double hwhm_hz)
{
    const double z = (frequency_hz - center_hz) / hwhm_hz;
    return Complex(1.0, 0.1) + Complex(-0.60, 0.18) * Complex(1.0, -z) / (1.0 + z * z);
}

class ShiftedSource final : public ComplexMeasurementSource {
public:
    explicit ShiftedSource(double center_hz) : center_hz_(center_hz) {}

    bool acquire(std::uint32_t frequency_hz, bool, ComplexMeasurement& measurement, std::string&) override
    {
        const Complex value = response(frequency_hz, center_hz_, 120000.0);
        measurement = {frequency_hz, frequency_hz, value.real(), value.imag()};
        return true;
    }

private:
    double center_hz_;
};

class FlatSource final : public ComplexMeasurementSource {
public:
    std::size_t count = 0;
    bool acquire(std::uint32_t frequency_hz, bool, ComplexMeasurement& measurement, std::string&) override
    {
        ++count;
        measurement = {frequency_hz, frequency_hz, 0.0, 0.0};
        return true;
    }
};

class ExpandedSource final : public ComplexMeasurementSource {
public:
    bool acquire(std::uint32_t frequency_hz, bool, ComplexMeasurement& measurement, std::string&) override
    {
        const Complex value = frequency_hz <= 32360000 ? Complex{} : response(frequency_hz, 32500000.0, 120000.0);
        measurement = {frequency_hz, frequency_hz, value.real(), value.imag()};
        return true;
    }
};

class DualSource final : public ComplexMeasurementSource {
public:
    bool lose_first = true;
    bool acquire(std::uint32_t frequency_hz, bool, ComplexMeasurement& measurement, std::string&) override
    {
        const Complex value = frequency_hz < 32500000 ?
            (lose_first ? Complex{} : response(frequency_hz, 32200000.0, 120000.0)) :
            response(frequency_hz, 33000000.0, 120000.0);
        measurement = {frequency_hz, frequency_hz, value.real(), value.imag()};
        return true;
    }
};

ResonanceEstimate baselineEstimate()
{
    ResonanceEstimate estimate;
    estimate.valid = true;
    estimate.sensor_id = 1;
    estimate.frequency_hz = 32000000.0;
    estimate.fwhm_hz = 240000.0;
    estimate.q = estimate.frequency_hz / estimate.fwhm_hz;
    estimate.spacing_hz = 60000.0;
    for (int offset = -2; offset <= 2; ++offset) {
        const auto frequency = static_cast<std::uint32_t>(estimate.frequency_hz + offset * estimate.spacing_hz);
        const Complex value = response(frequency, estimate.frequency_hz, 120000.0);
        estimate.template_points.push_back({frequency, frequency, value.real(), value.imag()});
    }
    return estimate;
}

void tracksSmallShift(std::size_t points)
{
    FrequencyTracker tracker;
    std::string error;
    assert(tracker.configure({baselineEstimate()}, points, error));
    ShiftedSource source(32012000.0);
    const TrackingFrame frame = tracker.acquire(1, source);
    assert(frame.complete);
    assert(frame.points_per_sensor == points);
    assert(frame.points.size() == points);
    assert(frame.sensors.size() == 1);
    assert(frame.sensors[0].fit_valid);
    assert(!frame.sensors[0].poor_fit);
    assert(frame.sensors[0].requested_shift_hz > 0.0);
    assert(frame.sensors[0].frequency_hz > 32000000.0);
    assert(frame.sensors[0].normalized_residual < 0.10);
}

void qualityCounter()
{
    TrackingQualityInput input;
    input.fit_valid = true;
    input.spacing_hz = 60000.0;
    input.template_gain = 1.0;
    input.normalized_residual = 0.2;
    auto decision = evaluateTrackingQuality(input);
    assert(decision.poor_fit && decision.loss_counter == 1 && !decision.recovery_required);
    input.loss_counter = decision.loss_counter;
    decision = evaluateTrackingQuality(input);
    assert(decision.loss_counter == 2 && !decision.recovery_required);
    input.loss_counter = decision.loss_counter;
    decision = evaluateTrackingQuality(input);
    assert(decision.loss_counter == 3 && decision.recovery_required);
    input.normalized_residual = 0.0;
    input.loss_counter = decision.loss_counter;
    decision = evaluateTrackingQuality(input);
    assert(!decision.poor_fit && decision.loss_counter == 0);
}

void rejectsInvalidConfigurationAndMetrics()
{
    FrequencyTracker tracker;
    std::string error;
    assert(!tracker.configure({baselineEstimate()}, 4, error));
    assert(!error.empty());
    assert(!tracker.configure({baselineEstimate(), baselineEstimate()}, 5, error));
    auto invalid = baselineEstimate();
    invalid.template_points[0].real = std::numeric_limits<double>::quiet_NaN();
    assert(!tracker.configure({invalid}, 5, error));

    TrackingQualityInput input;
    input.fit_valid = true;
    input.spacing_hz = 60000.0;
    input.template_gain = 1.0;
    input.normalized_residual = 0.10;
    input.requested_shift_hz = -24000.0;
    assert(!evaluateTrackingQuality(input).poor_fit);
    input.normalized_residual = std::numeric_limits<double>::quiet_NaN();
    assert(evaluateTrackingQuality(input).poor_fit);
    input.normalized_residual = 0.01;
    input.frequency_se_hz = std::numeric_limits<double>::quiet_NaN();
    assert(evaluateTrackingQuality(input).poor_fit);
}

void poorFramesDoNotMoveCenter(std::size_t points)
{
    FrequencyTracker tracker;
    std::string error;
    assert(tracker.configure({baselineEstimate()}, points, error));
    FlatSource source;
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
        const TrackingFrame frame = tracker.acquire(sequence, source);
        assert(frame.complete && frame.degraded);
        assert(frame.sensors.size() == 1);
        assert(frame.sensors[0].poor_fit);
        assert(frame.sensors[0].applied_shift_hz == 0.0);
        assert(frame.sensors[0].frequency_hz == 32000000.0);
        assert(frame.sensors[0].loss_counter == sequence);
        assert(frame.recovery_required == (sequence == 3));
    }
}

void loseTracking(FrequencyTracker& tracker)
{
    FlatSource source;
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence)
        assert(tracker.acquire(sequence, source).complete);
}

void localRelockSucceeds()
{
    FrequencyTracker tracker;
    std::string error;
    assert(tracker.configure({baselineEstimate()}, 5, error));
    loseTracking(tracker);
    ShiftedSource source(32200000.0);
    const auto relock = tracker.relock(30000000, 34000000, source);
    assert(relock.success && !relock.cancelled && relock.sensors.size() == 1);
    assert(relock.sensors[0].attempts == 1);
    assert(relock.sensors[0].measured_points == 11);
    assert(std::abs(relock.sensors[0].frequency_hz - 32200000.0) < 50000.0);
    const auto next = tracker.acquire(4, source);
    assert(next.complete && !next.recovery_required);
    assert(next.sensors[0].loss_counter == 0);
}

void expandedRelockSucceeds()
{
    FrequencyTracker tracker;
    std::string error;
    assert(tracker.configure({baselineEstimate()}, 5, error));
    loseTracking(tracker);
    ExpandedSource source;
    const auto relock = tracker.relock(30000000, 34000000, source);
    assert(relock.success && relock.sensors[0].attempts == 2);
    assert(relock.sensors[0].measured_points == 32);
}

void failedRelockIsBounded()
{
    FrequencyTracker tracker;
    std::string error;
    assert(tracker.configure({baselineEstimate()}, 5, error));
    loseTracking(tracker);
    FlatSource source;
    const auto relock = tracker.relock(30000000, 34000000, source);
    assert(!relock.success && !relock.cancelled && !relock.acquisition_error);
    assert(relock.sensors[0].attempts == 2);
    assert(source.count == 32);
    assert(!relock.reason.empty());
}

void cancelledRelockStopsEarly()
{
    FrequencyTracker tracker;
    std::string error;
    assert(tracker.configure({baselineEstimate()}, 5, error));
    loseTracking(tracker);
    FlatSource source;
    const auto relock = tracker.relock(30000000, 34000000, source, [&source]() { return source.count >= 4; });
    assert(!relock.success && relock.cancelled);
    assert(source.count == 4);
}

void relockOnlyLostSensor()
{
    auto second = baselineEstimate();
    second.sensor_id = 2;
    second.frequency_hz = 33000000.0;
    second.q = second.frequency_hz / second.fwhm_hz;
    second.template_points.clear();
    for (int offset = -2; offset <= 2; ++offset) {
        const auto frequency = static_cast<std::uint32_t>(second.frequency_hz + offset * second.spacing_hz);
        const auto value = response(frequency, second.frequency_hz, 120000.0);
        second.template_points.push_back({frequency, frequency, value.real(), value.imag()});
    }
    FrequencyTracker tracker;
    std::string error;
    assert(tracker.configure({baselineEstimate(), second}, 5, error));
    DualSource source;
    for (std::uint64_t sequence = 1; sequence <= 3; ++sequence) {
        const auto frame = tracker.acquire(sequence, source);
        assert(frame.complete && frame.sensors.size() == 2);
    }
    source.lose_first = false;
    const auto relock = tracker.relock(30000000, 34000000, source);
    assert(relock.success && relock.sensors.size() == 1);
    assert(relock.sensors[0].sensor_id == 1);
}
}

int main()
{
    tracksSmallShift(3);
    tracksSmallShift(5);
    qualityCounter();
    rejectsInvalidConfigurationAndMetrics();
    poorFramesDoNotMoveCenter(3);
    poorFramesDoNotMoveCenter(5);
    localRelockSucceeds();
    expandedRelockSucceeds();
    failedRelockIsBounded();
    cancelledRelockStopsEarly();
    relockOnlyLostSensor();
    return 0;
}
