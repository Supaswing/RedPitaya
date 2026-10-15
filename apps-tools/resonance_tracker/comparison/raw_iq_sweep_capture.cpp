#include "raw_iq_acquisition.hpp"

#include <chrono>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
volatile std::sig_atomic_t cancelled = 0;
void cancel(int) { cancelled = 1; }

std::uint32_t integer(const char* text, std::uint32_t minimum, std::uint32_t maximum)
{
    const std::string value(text);
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("arguments must be unsigned decimal integers");
    const auto parsed = std::stoull(value);
    if (parsed < minimum || parsed > maximum) throw std::invalid_argument("argument outside allowed range");
    return static_cast<std::uint32_t>(parsed);
}

std::string csvText(const std::string& value)
{
    std::string result = "\"";
    for (char c : value) {
        if (c == '"') result += '"';
        result += c;
    }
    return result + '"';
}
}

// Raw windows are retained individually. Repeated full sweeps alternate direction
// so frequency index remains independent of acquisition order and time drift.
int main(int argc, char** argv)
{
    if (argc != 7) {
        std::cerr << "usage: raw_iq_sweep_capture output.csv start_hz stop_hz points sweeps window_shift\n";
        return 2;
    }
    std::uint32_t start_hz, stop_hz, points, sweeps, shift;
    try {
        start_hz = integer(argv[2], 1, 62500000);
        stop_hz = integer(argv[3], start_hz + 1, 62500000);
        points = integer(argv[4], 2, 1000000);
        sweeps = integer(argv[5], 1, 1000000);
        shift = integer(argv[6], 0, 20);
        if (points - 1 > stop_hz - start_hz)
            throw std::invalid_argument("spacing must be at least 1 Hz");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    if (std::ifstream(argv[1]).good()) {
        std::cerr << "refusing to overwrite existing capture\n";
        return 2;
    }
    std::ofstream output(argv[1]);
    if (!output) { std::cerr << "cannot create output CSV\n"; return 2; }
    output << std::setprecision(17)
           << "sequence,sweep_index,direction,frequency_index,requested_hz,effective_hz,phase_increment,"
              "window_shift,integration_samples,timestamp_s,duration_us,valid,period_count,"
              "inc_i,inc_q,ref_i,ref_q,error\n";
    std::signal(SIGINT, cancel);
    std::signal(SIGTERM, cancel);
    struct Guard {
        RawIqAcquisition device;
        ~Guard() { device.close(); }
    } guard;
    if (!guard.device.open()) { std::cerr << "cannot open raw-IQ registers\n"; return 1; }
    std::string error;
    if (!guard.device.setWindowShift(shift, error)) { std::cerr << error << '\n'; return 1; }
    // This is the nominal clock used by the existing acquisition adapter.
    constexpr std::uint64_t clock_hz = 125000000ULL;
    constexpr std::uint64_t phase_scale = 1ULL << 32U;
    const auto start = std::chrono::steady_clock::now();
    std::uint64_t sequence = 0;
    for (std::uint32_t sweep = 0; sweep < sweeps; ++sweep) {
        const bool reverse = sweep % 2 != 0;
        for (std::uint32_t position = 0; position < points; ++position) {
            if (cancelled) { std::cerr << "CANCELLED after " << sequence << " readings\n"; return 130; }
            const auto index = reverse ? points - 1 - position : position;
            const auto requested = start_hz + static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(stop_hz - start_hz) * index + (points - 1) / 2) / (points - 1));
            const auto increment = static_cast<std::uint32_t>(
                (static_cast<std::uint64_t>(requested) * phase_scale + clock_hz / 2) / clock_hz);
            RawIqSample sample;
            error.clear();
            const auto before = std::chrono::steady_clock::now();
            const bool valid = guard.device.measure(requested, true, sample, error);
            const auto after = std::chrono::steady_clock::now();
            output << ++sequence << ',' << sweep << ',' << (reverse ? "reverse" : "forward") << ','
                   << index << ',' << requested << ',';
            // No effective-frequency assertion is made for a failed readback.
            if (valid) output << static_cast<double>(sample.phase_increment) * clock_hz / phase_scale;
            output << ',' << increment << ',' << shift << ',' << (1ULL << shift) << ','
                   << std::chrono::duration<double>(after - start).count() << ','
                   << std::chrono::duration<double, std::micro>(after - before).count() << ',' << valid << ',';
            if (valid) output << sample.period_count << ',' << sample.inc_i << ',' << sample.inc_q << ','
                              << sample.ref_i << ',' << sample.ref_q;
            else output << ",,,,";
            output << ',' << csvText(error) << '\n';
            output.flush();
            if (!output) { std::cerr << "CSV write failed\n"; return 1; }
            if (!valid) { std::cerr << "FAIL at sweep " << sweep << ", index " << index << ": " << error << '\n'; return 1; }
        }
        std::cerr << "sweep=" << sweep + 1 << '/' << sweeps << " readings=" << sequence << '\n';
    }
    if (cancelled) { std::cerr << "CANCELLED\n"; return 130; }
    std::cerr << "PASS: " << sequence << " ready-qualified readings in "
              << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() << " s\n";
    return 0;
}
