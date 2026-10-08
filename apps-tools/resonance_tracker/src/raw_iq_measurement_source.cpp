#include "raw_iq_measurement_source.hpp"

#include <complex>

RawIqMeasurementSource::RawIqMeasurementSource(RawIqAcquisition& acquisition,
    ComplexResponseRepresentation representation) : acquisition_(acquisition), representation_(representation) {}

bool RawIqMeasurementSource::acquire(std::uint32_t frequency_hz, bool first_point,
                                     ComplexMeasurement& measurement, std::string& error)
{
    RawIqSample sample;
    if (!acquisition_.measure(frequency_hz, first_point, sample, error)) return false;

    std::complex<double> response;
    if (!convertRawIqResponse(sample, representation_, response, error)) return false;
    measurement.requested_frequency_hz = sample.requested_frequency_hz;
    measurement.effective_frequency_hz = sample.effective_frequency_hz;
    measurement.real = response.real();
    measurement.imag = response.imag();
    return true;
}
