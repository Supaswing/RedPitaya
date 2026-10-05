#include "iq_statistics.hpp"

#include <cmath>

namespace {
constexpr double kRadiansToDegrees = 180.0 / 3.14159265358979323846;

template <typename Records, typename Getter, typename Predicate>
ScalarStatistics calculate_scalar(const Records& records, Getter getter, Predicate include)
{
    std::size_t count = 0;
    double mean = 0.0;
    double sum_squared_difference = 0.0;
    for (const auto& record : records) {
        if (!include(record)) continue;
        const double value = getter(record);
        ++count;
        const double difference = value - mean;
        mean += difference / static_cast<double>(count);
        sum_squared_difference += difference * (value - mean);
    }
    ScalarStatistics result;
    result.mean = mean;
    if (count > 1) result.sample_standard_deviation = std::sqrt(sum_squared_difference / (count - 1));
    return result;
}

template <typename Records, typename Real, typename Imag, typename Predicate>
ComplexNoiseStatistics calculate_noise(const Records& records, Real real, Imag imag, Predicate include)
{
    ComplexNoiseStatistics result;
    result.amplitude_std = calculate_scalar(records, [&](const auto& r) {
        return std::hypot(real(r), imag(r));
    }, include).sample_standard_deviation;
    const double mean_real = calculate_scalar(records, real, include).mean;
    const double mean_imag = calculate_scalar(records, imag, include).mean;
    const double magnitude = std::hypot(mean_real, mean_imag);
    if (magnitude == 0.0) return result; // A zero mean has no radial/phase reference direction.
    result.direction_valid = true;
    const double ux = mean_real / magnitude, uy = mean_imag / magnitude;
    result.radial_std = calculate_scalar(records, [&](const auto& r) {
        return (real(r) - mean_real) * ux + (imag(r) - mean_imag) * uy;
    }, include).sample_standard_deviation;
    result.tangential_std = calculate_scalar(records, [&](const auto& r) {
        return -(real(r) - mean_real) * uy + (imag(r) - mean_imag) * ux;
    }, include).sample_standard_deviation;
    const auto include_phase = [&](const auto& r) {
        return include(r) && std::hypot(real(r), imag(r)) > 0.0;
    };
    for (const auto& r : records) if (include_phase(r)) ++result.phase_count;
    result.phase_std = calculate_scalar(records, [&](const auto& r) {
        // Rotate into the mean direction before atan2, avoiding the +/-180 degree wrap.
        return std::atan2(-real(r) * uy + imag(r) * ux, real(r) * ux + imag(r) * uy) * kRadiansToDegrees;
    }, include_phase).sample_standard_deviation;
    return result;
}
}

RollingIqStatistics::RollingIqStatistics(std::size_t capacity) : capacity_(capacity) {}

void RollingIqStatistics::reset()
{
    records_.clear();
}

void RollingIqStatistics::add(const RawIqSample& sample)
{
    Record record;
    record.inc_i = sample.inc_i;
    record.inc_q = sample.inc_q;
    record.ref_i = sample.ref_i;
    record.ref_q = sample.ref_q;

    const double denominator = record.inc_i * record.inc_i + record.inc_q * record.inc_q;
    if (denominator > 0.0) {
        record.ratio_valid = true;
        record.ratio_real = (record.ref_i * record.inc_i + record.ref_q * record.inc_q) / denominator;
        record.ratio_imag = (record.ref_q * record.inc_i - record.ref_i * record.inc_q) / denominator;
        record.ratio_magnitude = std::hypot(record.ratio_real, record.ratio_imag);
        record.ratio_phase_deg = std::atan2(record.ratio_imag, record.ratio_real) * kRadiansToDegrees;
    }

    records_.push_back(record);
    if (records_.size() > capacity_) records_.pop_front();
}

IqStatisticsSnapshot RollingIqStatistics::snapshot() const
{
    IqStatisticsSnapshot result;
    result.sample_count = records_.size();
    if (records_.empty()) return result;

    const auto include_all = [](const Record&) { return true; };
    const auto include_ratio = [](const Record& record) { return record.ratio_valid; };
    result.inc_noise = calculate_noise(records_, [](const Record& r) { return r.inc_i; },
                                      [](const Record& r) { return r.inc_q; }, include_all);
    result.ref_noise = calculate_noise(records_, [](const Record& r) { return r.ref_i; },
                                      [](const Record& r) { return r.ref_q; }, include_all);
    result.ratio_noise = calculate_noise(records_, [](const Record& r) { return r.ratio_real; },
                                        [](const Record& r) { return r.ratio_imag; }, include_ratio);
    result.inc_i = calculate_scalar(records_, [](const Record& record) { return record.inc_i; }, include_all);
    result.inc_q = calculate_scalar(records_, [](const Record& record) { return record.inc_q; }, include_all);
    result.ref_i = calculate_scalar(records_, [](const Record& record) { return record.ref_i; }, include_all);
    result.ref_q = calculate_scalar(records_, [](const Record& record) { return record.ref_q; }, include_all);
    result.ratio_real_statistics =
        calculate_scalar(records_, [](const Record& record) { return record.ratio_real; }, include_ratio);
    result.ratio_imag_statistics =
        calculate_scalar(records_, [](const Record& record) { return record.ratio_imag; }, include_ratio);
    result.ratio_magnitude_statistics =
        calculate_scalar(records_, [](const Record& record) { return record.ratio_magnitude; }, include_ratio);

    for (const Record& record : records_) {
        if (record.ratio_valid) ++result.ratio_sample_count;
    }
    const Record& latest = records_.back();
    result.ratio_valid = latest.ratio_valid;
    result.ratio_real = latest.ratio_real;
    result.ratio_imag = latest.ratio_imag;
    result.ratio_magnitude = latest.ratio_magnitude;
    result.ratio_phase_deg = latest.ratio_phase_deg;
    if (result.ratio_sample_count != 0) {
        result.mean_ratio_phase_deg =
            std::atan2(result.ratio_imag_statistics.mean, result.ratio_real_statistics.mean) * kRadiansToDegrees;
    }
    return result;
}
