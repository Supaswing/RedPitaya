#include "raw_iq_measurement_source.hpp"

#include <complex>

RawIqMeasurementSource::RawIqMeasurementSource(RawIqAcquisition& acquisition) : acquisition_(acquisition) {}

bool RawIqMeasurementSource::acquire(std::uint32_t frequency_hz, bool first_point,
                                     ComplexMeasurement& measurement, std::string& error)
{
    RawIqSample sample;
    if (!acquisition_.measure(frequency_hz, first_point, sample, error)) return false;

    const std::complex<double> incident(sample.inc_i, sample.inc_q);
    const double incident_power = std::norm(incident);
    if (incident_power == 0.0) {
        error = "incident I/Q vector is zero";
        return false;
    }
    const std::complex<double> reflected(sample.ref_i, sample.ref_q);
    const std::complex<double> ratio = reflected / incident;
    measurement.requested_frequency_hz = sample.requested_frequency_hz;
    measurement.effective_frequency_hz = sample.effective_frequency_hz;
    measurement.real = ratio.real();
    measurement.imag = ratio.imag();
    return true;
}
