#include "raw_iq_acquisition.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr double kFpgaClockHz = 125000000.0;
constexpr std::uint64_t kPhaseScale = 1ULL << 32U;
constexpr std::uint32_t kFrequencyHz = 30000000U;
constexpr std::uint32_t kFirstWindowShift = 16U;
constexpr std::uint32_t kLastWindowShift = 20U;

struct RunningStatistics {
    std::size_t count = 0;
    double mean = 0.0;
    double sum_squared_difference = 0.0;

    void add(double value)
    {
        ++count;
        const double difference = value - mean;
        mean += difference / static_cast<double>(count);
        sum_squared_difference += difference * (value - mean);
    }

    double standardDeviation() const
    {
        return count > 1U ? std::sqrt(sum_squared_difference / static_cast<double>(count - 1U)) : 0.0;
    }
};

enum Metric : std::size_t {
    IncI,
    IncQ,
    RefI,
    RefQ,
    RatioReal,
    RatioImag,
    RatioMagnitude,
    PeriodCount,
    MetricCount
};

constexpr std::array<const char*, MetricCount> kMetricNames = {
    "inc_i", "inc_q", "ref_i", "ref_q", "ratio_real", "ratio_imag", "ratio_magnitude", "period_count"};

struct Observation {
    std::array<double, MetricCount> values{};
};

struct Result {
    const char* mode = "native";
    std::uint32_t window_shift = 0;
    std::uint32_t averages = 1;
    std::uint32_t phase_increment = 0;
    std::array<RunningStatistics, MetricCount> statistics{};
};

struct DeltaResult {
    const char* mode = "native";
    std::uint32_t window_shift = 0;
    std::uint32_t averages = 1;
    std::array<RunningStatistics, MetricCount> statistics{};
};

bool parseCount(const char* text, std::uint32_t& value)
{
    if (text == nullptr || *text == '\0') return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (*end != '\0' || parsed == 0UL || parsed > 100000UL) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

std::uint32_t frequencyToPhaseIncrement(std::uint32_t frequency_hz)
{
    const std::uint64_t scaled = static_cast<std::uint64_t>(frequency_hz) * kPhaseScale;
    return static_cast<std::uint32_t>((scaled + 62500000ULL) / 125000000ULL);
}

double phaseIncrementToFrequency(std::uint32_t phase_increment)
{
    return static_cast<double>(phase_increment) * kFpgaClockHz / static_cast<double>(kPhaseScale);
}

Observation makeObservation(double inc_i, double inc_q, double ref_i, double ref_q, double period_count)
{
    Observation observation;
    observation.values[IncI] = inc_i;
    observation.values[IncQ] = inc_q;
    observation.values[RefI] = ref_i;
    observation.values[RefQ] = ref_q;
    observation.values[PeriodCount] = period_count;
    const double denominator = inc_i * inc_i + inc_q * inc_q;
    if (denominator > 0.0) {
        observation.values[RatioReal] = (ref_i * inc_i + ref_q * inc_q) / denominator;
        observation.values[RatioImag] = (ref_q * inc_i - ref_i * inc_q) / denominator;
        observation.values[RatioMagnitude] =
            std::hypot(observation.values[RatioReal], observation.values[RatioImag]);
    } else {
        observation.values[RatioReal] = std::numeric_limits<double>::quiet_NaN();
        observation.values[RatioImag] = std::numeric_limits<double>::quiet_NaN();
        observation.values[RatioMagnitude] = std::numeric_limits<double>::quiet_NaN();
    }
    return observation;
}

void addObservation(Result& result, const Observation& observation)
{
    for (std::size_t metric = 0; metric < MetricCount; ++metric) {
        if (std::isfinite(observation.values[metric])) result.statistics[metric].add(observation.values[metric]);
    }
}

void addDelta(DeltaResult& result, const Observation& base, const Observation& plus_one)
{
    for (std::size_t metric = 0; metric < MetricCount; ++metric) {
        const double difference = plus_one.values[metric] - base.values[metric];
        if (std::isfinite(difference)) result.statistics[metric].add(difference);
    }
}

bool acquireObservation(RawIqAcquisition& acquisition, std::uint32_t phase_increment,
                        std::uint32_t averages, std::uint32_t estimate_index, const char* mode,
                        std::uint32_t window_shift, std::ofstream& raw_output,
                        Observation& observation, std::string& error)
{
    double inc_i = 0.0;
    double inc_q = 0.0;
    double ref_i = 0.0;
    double ref_q = 0.0;
    double period_count = 0.0;
    for (std::uint32_t sub_sample = 0; sub_sample < averages; ++sub_sample) {
        RawIqSample sample;
        if (!acquisition.measurePhaseIncrement(phase_increment, sub_sample == 0U, sample, error)) return false;
        if (sample.phase_increment != phase_increment) {
            error = "DDS phase increment readback differs from request";
            return false;
        }
        raw_output << mode << ',' << window_shift << ',' << averages << ',' << estimate_index << ','
                   << sub_sample << ',' << phase_increment << ',' << phaseIncrementToFrequency(phase_increment) << ','
                   << sample.period_count << ',' << sample.inc_i << ',' << sample.inc_q << ','
                   << sample.ref_i << ',' << sample.ref_q << '\n';
        inc_i += sample.inc_i;
        inc_q += sample.inc_q;
        ref_i += sample.ref_i;
        ref_q += sample.ref_q;
        period_count += sample.period_count;
    }
    const double divisor = static_cast<double>(averages);
    observation = makeObservation(inc_i / divisor, inc_q / divisor, ref_i / divisor,
                                  ref_q / divisor, period_count / divisor);
    return true;
}

const Result* findResult(const std::vector<Result>& results, const char* mode,
                         std::uint32_t window_shift, std::uint32_t phase_increment)
{
    for (const Result& result : results) {
        if (std::string(result.mode) == mode && result.window_shift == window_shift &&
            result.phase_increment == phase_increment) return &result;
    }
    return nullptr;
}
}

int main(int argc, char** argv)
{
    std::uint32_t estimates = 128U;
    std::string raw_path = "raw_iq_noise_samples.csv";
    if ((argc > 1 && !parseCount(argv[1], estimates)) || argc > 3) {
        std::cerr << "usage: raw_iq_noise_test [estimates_per_configuration [raw_csv_path]]\n";
        return 2;
    }
    if (argc > 2) raw_path = argv[2];

    std::ofstream raw_output(raw_path);
    if (!raw_output) {
        std::cerr << "FAIL: cannot open raw sample output " << raw_path << '\n';
        return 1;
    }
    raw_output << std::setprecision(15);
    raw_output << "mode,window_shift,averages,estimate_index,sub_sample,phase_increment,frequency_hz,"
                  "period_count,inc_i,inc_q,ref_i,ref_q\n";

    struct DeviceGuard {
        RawIqAcquisition acquisition;
        ~DeviceGuard() { acquisition.close(); }
    } device;
    if (!device.acquisition.open()) {
        std::cerr << "FAIL: unable to map the VNA register block; run as root after loading the bitstream\n";
        return 1;
    }

    const std::uint32_t base_increment = frequencyToPhaseIncrement(kFrequencyHz);
    const std::array<std::uint32_t, 2> increments = {base_increment, base_increment + 1U};
    std::vector<Result> results;
    std::vector<DeltaResult> delta_results;
    std::string error;

    for (std::uint32_t window_shift = kFirstWindowShift; window_shift <= kLastWindowShift; ++window_shift) {
        if (!device.acquisition.setWindowShift(window_shift, error)) {
            std::cerr << "FAIL: WINDOW_SHIFT=" << window_shift << ": " << error << '\n';
            return 1;
        }
        std::array<Result, 2> native{};
        for (std::size_t index = 0; index < increments.size(); ++index) {
            native[index].mode = "native";
            native[index].window_shift = window_shift;
            native[index].averages = 1U;
            native[index].phase_increment = increments[index];
        }
        DeltaResult native_delta{"native", window_shift, 1U, {}};
        for (std::uint32_t estimate = 0; estimate < estimates; ++estimate) {
            std::array<Observation, 2> observations{};
            for (std::size_t order_index = 0; order_index < increments.size(); ++order_index) {
                const std::size_t index = estimate % 2U == 0U ? order_index : increments.size() - 1U - order_index;
                if (!acquireObservation(device.acquisition, increments[index], 1U, estimate, "native",
                                        window_shift, raw_output, observations[index], error)) {
                    std::cerr << "FAIL: native acquisition: " << error << '\n';
                    return 1;
                }
                addObservation(native[index], observations[index]);
            }
            addDelta(native_delta, observations[0], observations[1]);
        }
        results.insert(results.end(), native.begin(), native.end());
        delta_results.push_back(native_delta);

        if (window_shift == kLastWindowShift) continue;
        std::array<Result, 2> averaged{};
        for (std::size_t index = 0; index < increments.size(); ++index) {
            averaged[index].mode = "average2";
            averaged[index].window_shift = window_shift;
            averaged[index].averages = 2U;
            averaged[index].phase_increment = increments[index];
        }
        DeltaResult averaged_delta{"average2", window_shift, 2U, {}};
        for (std::uint32_t estimate = 0; estimate < estimates; ++estimate) {
            std::array<Observation, 2> observations{};
            for (std::size_t order_index = 0; order_index < increments.size(); ++order_index) {
                const std::size_t index = estimate % 2U == 0U ? order_index : increments.size() - 1U - order_index;
                if (!acquireObservation(device.acquisition, increments[index], 2U, estimate, "average2",
                                        window_shift, raw_output, observations[index], error)) {
                    std::cerr << "FAIL: averaged acquisition: " << error << '\n';
                    return 1;
                }
                addObservation(averaged[index], observations[index]);
            }
            addDelta(averaged_delta, observations[0], observations[1]);
        }
        results.insert(results.end(), averaged.begin(), averaged.end());
        delta_results.push_back(averaged_delta);
    }

    std::cout << std::setprecision(15);
    std::cout << "SUMMARY,mode,window_shift,averages,phase_increment,frequency_hz,metric,count,mean,stddev\n";
    for (const Result& result : results) {
        for (std::size_t metric = 0; metric < MetricCount; ++metric) {
            const RunningStatistics& statistics = result.statistics[metric];
            std::cout << "SUMMARY," << result.mode << ',' << result.window_shift << ',' << result.averages << ','
                      << result.phase_increment << ',' << phaseIncrementToFrequency(result.phase_increment) << ','
                      << kMetricNames[metric] << ',' << statistics.count << ',' << statistics.mean << ','
                      << statistics.standardDeviation() << '\n';
        }
    }

    std::cout << "DELTA,mode,window_shift,averages,base_phase_increment,plus_one_phase_increment,"
                 "frequency_step_hz,metric,count,mean_delta,stddev_delta,standard_error,z_score\n";
    for (const DeltaResult& result : delta_results) {
        for (std::size_t metric = 0; metric < MetricCount; ++metric) {
            const RunningStatistics& statistics = result.statistics[metric];
            const double standard_error = statistics.count > 0U
                                              ? statistics.standardDeviation() /
                                                    std::sqrt(static_cast<double>(statistics.count))
                                              : 0.0;
            const double z_score = standard_error > 0.0
                                       ? statistics.mean / standard_error
                                       : std::numeric_limits<double>::quiet_NaN();
            std::cout << "DELTA," << result.mode << ',' << result.window_shift << ',' << result.averages << ','
                      << increments[0] << ',' << increments[1] << ','
                      << phaseIncrementToFrequency(increments[1]) - phaseIncrementToFrequency(increments[0]) << ','
                      << kMetricNames[metric] << ',' << statistics.count << ',' << statistics.mean << ','
                      << statistics.standardDeviation() << ',' << standard_error << ',' << z_score << '\n';
        }
    }

    std::cout << "COMPARISON,phase_increment,frequency_hz,lower_shift,upper_shift,metric,"
                 "average2_lower_stddev,native_upper_stddev,stddev_ratio\n";
    for (std::uint32_t lower_shift = kFirstWindowShift; lower_shift < kLastWindowShift; ++lower_shift) {
        for (const std::uint32_t increment : increments) {
            const Result* averaged = findResult(results, "average2", lower_shift, increment);
            const Result* native = findResult(results, "native", lower_shift + 1U, increment);
            if (averaged == nullptr || native == nullptr) continue;
            for (std::size_t metric = 0; metric < MetricCount; ++metric) {
                const double averaged_sd = averaged->statistics[metric].standardDeviation();
                const double native_sd = native->statistics[metric].standardDeviation();
                const double ratio = native_sd > 0.0 ? averaged_sd / native_sd
                                                     : std::numeric_limits<double>::quiet_NaN();
                std::cout << "COMPARISON," << increment << ',' << phaseIncrementToFrequency(increment) << ','
                          << lower_shift << ',' << lower_shift + 1U << ',' << kMetricNames[metric] << ','
                          << averaged_sd << ',' << native_sd << ',' << ratio << '\n';
            }
        }
    }
    std::cout << "PASS: noise acquisition complete; raw samples=" << raw_path
              << ", estimates per configuration=" << estimates << '\n';
    return 0;
}
