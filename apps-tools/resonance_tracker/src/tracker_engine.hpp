#pragma once

#include "resonance_analysis.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct TrackingPoint {
    std::uint32_t sensor_id = 0;
    int offset = 0;
    ComplexMeasurement measurement;
};

struct TrackingSensorResult {
    std::uint32_t sensor_id = 0;
    bool fit_valid = false;
    bool poor_fit = true;
    bool recovery_required = false;
    double frequency_hz = 0.0;
    double q = 0.0;
    double frequency_se_hz = 0.0;
    double normalized_residual = 1.0;
    double template_gain = 0.0;
    double requested_shift_hz = 0.0;
    double applied_shift_hz = 0.0;
    std::uint32_t loss_counter = 0;
};

struct TrackingFrame {
    bool complete = false;
    bool degraded = false;
    bool recovery_required = false;
    std::uint64_t sequence = 0;
    std::size_t points_per_sensor = 0;
    std::string error;
    std::vector<TrackingPoint> points;
    std::vector<TrackingSensorResult> sensors;
};

class FrequencyTracker {
public:
    bool configure(const std::vector<ResonanceEstimate>& resonances, std::size_t points, std::string& error);
    TrackingFrame acquire(std::uint64_t sequence, ComplexMeasurementSource& source,
                          const CancellationCheck& cancelled = {});
    bool configured() const;
    std::size_t pointCount() const;

private:
    using Complex = std::complex<double>;

    struct PreparedResonance {
        std::uint32_t sensor_id = 0;
        double baseline_frequency_hz = 0.0;
        double tracked_frequency_hz = 0.0;
        double baseline_q = 0.0;
        double spacing_hz = 0.0;
        std::array<Complex, 5> template_iq{};
        std::array<Complex, 5> derivative_iq{};
        std::array<std::array<double, 5>, 5> normal_inverse{};
        std::uint32_t loss_counter = 0;
    };

    bool prepare(PreparedResonance& resonance);
    bool fitFive(const PreparedResonance& resonance, const std::array<Complex, 5>& live,
                 TrackingSensorResult& result) const;
    bool fitThree(const PreparedResonance& resonance, const std::array<Complex, 5>& live,
                  TrackingSensorResult& result) const;
    double calculateLiveQ(const PreparedResonance& resonance, const std::array<Complex, 5>& live,
                          double frequency_hz) const;

    std::size_t points_ = 0;
    std::vector<PreparedResonance> resonances_;
};
