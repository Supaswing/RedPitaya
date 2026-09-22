#pragma once

#include "raw_iq_acquisition.hpp"
#include "resonance_analysis.hpp"

class RawIqMeasurementSource final : public ComplexMeasurementSource {
public:
    explicit RawIqMeasurementSource(RawIqAcquisition& acquisition);
    bool acquire(std::uint32_t frequency_hz, bool first_point, ComplexMeasurement& measurement,
                 std::string& error) override;

private:
    RawIqAcquisition& acquisition_;
};
