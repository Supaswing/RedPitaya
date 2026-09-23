#include "tracker_engine.hpp"
#include "tracking_quality.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
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
}

int main()
{
    tracksSmallShift(3);
    tracksSmallShift(5);
    qualityCounter();
    return 0;
}
