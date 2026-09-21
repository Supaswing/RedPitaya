#pragma once

#include "raw_iq_acquisition.hpp"

#include <cstddef>
#include <deque>

struct ScalarStatistics {
    double mean = 0.0;
    double sample_standard_deviation = 0.0;
};

struct IqStatisticsSnapshot {
    std::size_t sample_count = 0;
    std::size_t ratio_sample_count = 0;
    bool ratio_valid = false;
    double ratio_real = 0.0;
    double ratio_imag = 0.0;
    double ratio_magnitude = 0.0;
    double ratio_phase_deg = 0.0;
    ScalarStatistics inc_i;
    ScalarStatistics inc_q;
    ScalarStatistics ref_i;
    ScalarStatistics ref_q;
    ScalarStatistics ratio_real_statistics;
    ScalarStatistics ratio_imag_statistics;
    ScalarStatistics ratio_magnitude_statistics;
    double mean_ratio_phase_deg = 0.0;
};

class RollingIqStatistics {
public:
    explicit RollingIqStatistics(std::size_t capacity);

    void reset();
    void add(const RawIqSample& sample);
    IqStatisticsSnapshot snapshot() const;

private:
    struct Record {
        double inc_i = 0.0;
        double inc_q = 0.0;
        double ref_i = 0.0;
        double ref_q = 0.0;
        bool ratio_valid = false;
        double ratio_real = 0.0;
        double ratio_imag = 0.0;
        double ratio_magnitude = 0.0;
        double ratio_phase_deg = 0.0;
    };

    std::size_t capacity_;
    std::deque<Record> records_;
};
