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
