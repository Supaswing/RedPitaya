#include "resonance_analysis.hpp"

#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace {
using Complex = std::complex<double>;

struct SyntheticResonance {
    double frequency_hz;
    double hwhm_hz;
    Complex amplitude;
};

class SyntheticSource final : public ComplexMeasurementSource {
public:
    explicit SyntheticSource(std::vector<SyntheticResonance> resonances) : resonances_(std::move(resonances)) {}

    bool acquire(std::uint32_t frequency_hz, bool, ComplexMeasurement& measurement, std::string&) override
    {
        Complex response(1.0, 0.1);
        for (const auto& resonance : resonances_) {
            const double z = (static_cast<double>(frequency_hz) - resonance.frequency_hz) / resonance.hwhm_hz;
            response += resonance.amplitude * Complex(1.0, -z) / (1.0 + z * z);
        }
        measurement = {frequency_hz, frequency_hz, response.real(), response.imag()};
        return true;
    }

private:
    std::vector<SyntheticResonance> resonances_;
};

BaselineConfig config(std::size_t sensors)
{
    BaselineConfig value;
    value.start_frequency_hz = 30000000;
    value.stop_frequency_hz = 34000000;
    value.overview_points = 101;
    value.coarse_averages = 3;
    value.refine_points = 21;
    value.refine_averages = 3;
    value.sensor_count = sensors;
    return value;
}

ReplayMeasurementSource replay(std::vector<SyntheticResonance> resonances)
{
    SyntheticSource source(std::move(resonances));
    std::vector<ComplexMeasurement> captured;
    std::string error;
    for (std::uint32_t frequency = 30000000; frequency <= 34000000; frequency += 10000) {
        ComplexMeasurement point;
        assert(source.acquire(frequency, false, point, error));
        captured.push_back(point);
    }
    return ReplayMeasurementSource(std::move(captured));
}

void validSingleResonance()
{
    auto source = replay({{32150000.0, 130000.0, {-0.60, 0.18}}});
    BaselineAnalyzer analyzer;
    const BaselineResult result = analyzer.acquire(11, config(1), source);
    assert(result.complete);
    assert(result.valid);
    assert(result.sequence == 11);
    assert(result.resonances.size() == 1);
    assert(result.resonances[0].valid);
    assert(std::abs(result.resonances[0].frequency_hz - 32150000.0) < 80000.0);
    assert(result.resonances[0].q > 20.0);
    assert(result.resonances[0].template_points.size() == 5);

    const DiagnosticResult diagnostics = acquireDiagnostics(17, result.resonances[0], source);
    assert(diagnostics.complete);
    assert(diagnostics.sequence == 17);
    assert(diagnostics.points.size() == 5);
    assert(diagnostics.points.front().offset == -2);
    assert(diagnostics.points.back().offset == 2);
}

void multipleCandidates()
{
    auto source = replay({{31000000.0, 110000.0, {-0.52, 0.15}},
                          {33000000.0, 140000.0, {-0.68, -0.12}}});
    BaselineAnalyzer analyzer;
    const BaselineResult result = analyzer.acquire(21, config(2), source);
    assert(result.complete);
    assert(result.valid);
    assert(result.resonances.size() == 2);
    assert(result.resonances[0].frequency_hz < result.resonances[1].frequency_hz);
    assert(std::abs(result.resonances[0].frequency_hz - 31000000.0) < 90000.0);
    assert(std::abs(result.resonances[1].frequency_hz - 33000000.0) < 90000.0);
}

void missingResonance()
{
    auto source = replay({});
    BaselineAnalyzer analyzer;
    const BaselineResult result = analyzer.acquire(31, config(1), source);
    assert(result.complete);
    assert(!result.valid);
    assert(result.resonances.empty());
    assert(!result.error.empty());
}

void cancellationDoesNotComplete()
{
    SyntheticSource source({{32000000.0, 120000.0, {-0.5, 0.1}}});
    BaselineAnalyzer analyzer;
    std::size_t checks = 0;
    const BaselineResult result = analyzer.acquire(41, config(1), source, {}, [&checks]() {
        return ++checks > 10;
    });
    assert(!result.complete);
    assert(!result.valid);
    assert(result.error == "cancelled");
}
}

int main()
{
    validSingleResonance();
    multipleCandidates();
    missingResonance();
    cancellationDoesNotComplete();
    return 0;
}
