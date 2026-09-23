#include "raw_iq_acquisition.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
constexpr double kFpgaClockHz = 125000000.0;

bool parseUnsigned(const char* text, std::uint32_t& value)
{
    if (text == nullptr || *text == '\0') return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text, &end, 10);
    if (*end != '\0' || parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

double minimumIntegrationPeriods(std::uint32_t frequency_hz, std::uint32_t window_shift)
{
    return std::ldexp(static_cast<double>(frequency_hz) / kFpgaClockHz,
                      static_cast<int>(window_shift));
}

double periodCountDurationUs(const RawIqSample& sample)
{
    return 1.0e6 * static_cast<double>(sample.period_count) /
           static_cast<double>(sample.effective_frequency_hz);
}

void printSample(std::uint64_t sequence, const char* stage, const RawIqSample& sample,
                 std::uint32_t window_shift, double call_duration_us)
{
    std::cout << sequence << ',' << stage << ',' << sample.requested_frequency_hz << ','
              << sample.effective_frequency_hz << ',' << sample.period_count << ','
              << minimumIntegrationPeriods(sample.effective_frequency_hz, window_shift) << ','
              << periodCountDurationUs(sample) << ',' << call_duration_us << ','
              << sample.inc_i << ',' << sample.inc_q << ',' << sample.ref_i << ',' << sample.ref_q << '\n';
}

bool periodCountIsPlausible(const RawIqSample& sample, std::uint32_t window_shift,
                            double call_duration_us)
{
    constexpr double kTimingToleranceUs = 100.0;
    const double minimum_periods = minimumIntegrationPeriods(sample.effective_frequency_hz, window_shift);
    const double counted_duration_us = periodCountDurationUs(sample);
    return sample.period_count != 0U &&
           static_cast<double>(sample.period_count) + 2.0 >= minimum_periods &&
           counted_duration_us <= call_duration_us + kTimingToleranceUs;
}

bool measureTimed(RawIqAcquisition& acquisition, std::uint32_t frequency_hz, bool first_point,
                  RawIqSample& sample, std::string& error, double& call_duration_us)
{
    const auto started = std::chrono::steady_clock::now();
    const bool measured = acquisition.measure(frequency_hz, first_point, sample, error);
    const auto completed = std::chrono::steady_clock::now();
    call_duration_us = std::chrono::duration<double, std::micro>(completed - started).count();
    return measured;
}
}

int main(int argc, char** argv)
{
    std::uint32_t frequency_a_hz = 32000000U;
    std::uint32_t frequency_b_hz = 34000000U;
    std::uint32_t fixed_samples = 10U;
    std::uint32_t window_shift = 17U;
    if ((argc > 1 && !parseUnsigned(argv[1], frequency_a_hz)) ||
        (argc > 2 && !parseUnsigned(argv[2], frequency_b_hz)) ||
        (argc > 3 && !parseUnsigned(argv[3], fixed_samples)) ||
        (argc > 4 && !parseUnsigned(argv[4], window_shift)) || argc > 5 ||
        frequency_a_hz == 0U || frequency_b_hz == 0U || frequency_a_hz == frequency_b_hz ||
        fixed_samples < 2U || window_shift > 20U ||
        std::abs(minimumIntegrationPeriods(frequency_a_hz, window_shift) -
                 minimumIntegrationPeriods(frequency_b_hz, window_shift)) <= 4.0) {
        std::cerr << "usage: raw_iq_hardware_test [frequency_a_hz frequency_b_hz fixed_samples window_shift]\n";
        return 2;
    }

    struct DeviceGuard {
        RawIqAcquisition acquisition;
        ~DeviceGuard() { acquisition.close(); }
    } device;
    RawIqAcquisition& acquisition = device.acquisition;
    if (!acquisition.open()) {
        std::cerr << "FAIL: unable to map the VNA register block; run as root after loading the bitstream\n";
        return 1;
    }
    std::string error;
    if (!acquisition.setWindowShift(window_shift, error)) {
        std::cerr << "FAIL: unable to apply WINDOW_SHIFT: " << error << '\n';
        return 1;
    }

    std::cout << "sequence,stage,requested_hz,effective_hz,period_count,minimum_integration_periods,"
                 "period_count_duration_us,measurement_call_duration_us,inc_i,inc_q,ref_i,ref_q\n";
    std::vector<std::uint32_t> fixed_period_counts;
    std::uint64_t sequence = 0;
    RawIqSample sample;
    for (std::uint32_t index = 0; index < fixed_samples; ++index) {
        double call_duration_us = 0.0;
        if (!measureTimed(acquisition, frequency_a_hz, index == 0U, sample, error, call_duration_us)) {
            std::cerr << "FAIL: fixed-frequency acquisition " << index << ": " << error << '\n';
            return 1;
        }
        printSample(++sequence, "fixed_a", sample, window_shift, call_duration_us);
        if (sample.requested_frequency_hz != frequency_a_hz ||
            !periodCountIsPlausible(sample, window_shift, call_duration_us)) {
            std::cerr << "FAIL: fixed-frequency sample has an implausible DDS period count\n";
            return 1;
        }
        fixed_period_counts.push_back(sample.period_count);
    }
    const auto fixed_range = std::minmax_element(fixed_period_counts.begin(), fixed_period_counts.end());

    RawIqSample changed;
    double changed_call_duration_us = 0.0;
    if (!measureTimed(acquisition, frequency_b_hz, true, changed, error, changed_call_duration_us)) {
        std::cerr << "FAIL: changed-frequency acquisition: " << error << '\n';
        return 1;
    }
    printSample(++sequence, "changed_b", changed, window_shift, changed_call_duration_us);
    if (changed.requested_frequency_hz != frequency_b_hz ||
        !periodCountIsPlausible(changed, window_shift, changed_call_duration_us)) {
        std::cerr << "FAIL: controlled frequency change produced an implausible DDS period count\n";
        return 1;
    }

    RawIqSample returned;
    double returned_call_duration_us = 0.0;
    if (!measureTimed(acquisition, frequency_a_hz, true, returned, error, returned_call_duration_us)) {
        std::cerr << "FAIL: return-frequency acquisition: " << error << '\n';
        return 1;
    }
    printSample(++sequence, "returned_a", returned, window_shift, returned_call_duration_us);
    if (returned.requested_frequency_hz != frequency_a_hz ||
        !periodCountIsPlausible(returned, window_shift, returned_call_duration_us)) {
        std::cerr << "FAIL: return to the original frequency produced an implausible DDS period count\n";
        return 1;
    }

    std::cout << "PASS: " << sequence
              << " ready-qualified acquisitions completed in order with DDS readback; fixed-frequency PERIOD_COUNT range="
              << *fixed_range.first << ".." << *fixed_range.second
              << ", changed-frequency PERIOD_COUNT=" << changed.period_count << '\n';
    return 0;
}
