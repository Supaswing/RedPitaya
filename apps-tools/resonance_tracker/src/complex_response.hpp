#pragma once

#include "raw_iq_acquisition.hpp"

#include <complex>
#include <string>

enum class ComplexResponseRepresentation {
    IncidentNormalized,
    RawReflected
};

// Preserve signed raw words until this conversion. RawReflected has no
// incident normalization and remains in ADC/accumulator units, not volts.
inline bool convertRawIqResponse(const RawIqSample& sample, ComplexResponseRepresentation representation,
                                 std::complex<double>& response, std::string& error)
{
    const std::complex<double> reflected(static_cast<double>(sample.ref_i), static_cast<double>(sample.ref_q));
    if (representation == ComplexResponseRepresentation::RawReflected) {
        response = reflected;
        return true;
    }
    if (representation != ComplexResponseRepresentation::IncidentNormalized) {
        error = "unsupported complex response representation";
        return false;
    }
    const std::complex<double> incident(static_cast<double>(sample.inc_i), static_cast<double>(sample.inc_q));
    if (std::norm(incident) == 0.0) {
        error = "incident I/Q vector is zero";
        return false;
    }
    response = reflected / incident;
    return true;
}
