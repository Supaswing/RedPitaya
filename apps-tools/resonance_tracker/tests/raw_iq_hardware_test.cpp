#include "raw_iq_acquisition.hpp"

#include <algorithm>
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

double expectedPeriods(std::uint32_t frequency_hz, std::uint32_t window_shift)
{
    return std::ldexp(static_cast<double>(frequency_hz) / kFpgaClockHz,
                      static_cast<int>(window_shift));
}

bool periodCountMatches(const RawIqSample& sample, std::uint32_t window_shift)
{
    return std::abs(static_cast<double>(sample.period_count) -
                    expectedPeriods(sample.effective_frequency_hz, window_shift)) <= 2.0;
}

void printSample(std::uint64_t sequence, const char* stage, const RawIqSample& sample,
                 std::uint32_t window_shift)
{
    std::cout << sequence << ',' << stage << ',' << sample.requested_frequency_hz << ','
              << sample.effective_frequency_hz << ',' << sample.period_count << ','
              << expectedPeriods(sample.effective_frequency_hz, window_shift) << ','
              << sample.inc_i << ',' << sample.inc_q << ',' << sample.ref_i << ',' << sample.ref_q << '\n';
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
        std::abs(expectedPeriods(frequency_a_hz, window_shift) -
                 expectedPeriods(frequency_b_hz, window_shift)) <= 4.0) {
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

    std::cout << "sequence,stage,requested_hz,effective_hz,period_count,expected_periods,"
                 "inc_i,inc_q,ref_i,ref_q\n";
    std::vector<std::uint32_t> fixed_period_counts;
    std::uint64_t sequence = 0;
    RawIqSample sample;
    for (std::uint32_t index = 0; index < fixed_samples; ++index) {
        if (!acquisition.measure(frequency_a_hz, index == 0U, sample, error)) {
            std::cerr << "FAIL: fixed-frequency acquisition " << index << ": " << error << '\n';
            return 1;
        }
        printSample(++sequence, "fixed_a", sample, window_shift);
        if (sample.requested_frequency_hz != frequency_a_hz || !periodCountMatches(sample, window_shift)) {
            std::cerr << "FAIL: fixed-frequency sample has stale frequency metadata or an unexpected DDS period count\n";
            return 1;
        }
        fixed_period_counts.push_back(sample.period_count);
    }
    const auto fixed_range = std::minmax_element(fixed_period_counts.begin(), fixed_period_counts.end());
    if (*fixed_range.second - *fixed_range.first > 2U) {
        std::cerr << "FAIL: PERIOD_COUNT is not stable at a fixed frequency\n";
        return 1;
    }

    RawIqSample changed;
    if (!acquisition.measure(frequency_b_hz, true, changed, error)) {
        std::cerr << "FAIL: changed-frequency acquisition: " << error << '\n';
        return 1;
    }
    printSample(++sequence, "changed_b", changed, window_shift);
    if (changed.requested_frequency_hz != frequency_b_hz || !periodCountMatches(changed, window_shift) ||
        changed.period_count == fixed_period_counts.back()) {
        std::cerr << "FAIL: controlled frequency change was not reflected by PERIOD_COUNT\n";
        return 1;
    }

    RawIqSample returned;
    if (!acquisition.measure(frequency_a_hz, true, returned, error)) {
        std::cerr << "FAIL: return-frequency acquisition: " << error << '\n';
        return 1;
    }
    printSample(++sequence, "returned_a", returned, window_shift);
    if (returned.requested_frequency_hz != frequency_a_hz || !periodCountMatches(returned, window_shift) ||
        std::abs(static_cast<long long>(returned.period_count) -
                 static_cast<long long>(fixed_period_counts.back())) > 2LL) {
        std::cerr << "FAIL: return to the original frequency did not restore its DDS period count\n";
        return 1;
    }

    std::cout << "PASS: " << sequence
              << " ready-qualified acquisitions completed in order; fixed-frequency PERIOD_COUNT range="
              << *fixed_range.first << ".." << *fixed_range.second
              << ", changed-frequency PERIOD_COUNT=" << changed.period_count << '\n';
    return 0;
}
