#include "raw_iq_acquisition.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

// Uses the production acquisition sequence unchanged, including DDS restart,
// 100 us settling, ready qualification, and phase-increment readback.
int main(int argc, char** argv)
{
    if (argc != 2) {
        std::cerr << "usage: raw_iq_comb_capture output.csv\n";
        return 2;
    }
    std::ifstream existing(argv[1]);
    if (existing.good()) {
        std::cerr << "refusing to overwrite existing capture\n";
        return 2;
    }
    std::ofstream output(argv[1]);
    if (!output) return 2;
    output << std::setprecision(17);
    output << "sequence,window_shift,frequency_index,reading_index,phase_increment,frequency_hz,"
              "timestamp_s,duration_us,valid,period_count,inc_i,inc_q,ref_i,ref_q,error\n";
    struct Guard {
        RawIqAcquisition device;
        ~Guard() { device.close(); }
    } guard;
    if (!guard.device.open()) {
        std::cerr << "cannot open raw-IQ registers\n";
        return 1;
    }
    constexpr std::uint64_t clock_hz = 125000000ULL;
    constexpr std::uint64_t phase_scale = 1ULL << 32U;
    constexpr std::uint32_t base = (30000000ULL * phase_scale + clock_hz / 2U) / clock_hz;
    const auto start = std::chrono::steady_clock::now();
    unsigned sequence = 0;
    std::string error;
    for (unsigned shift = 18; shift <= 20; ++shift) {
        if (!guard.device.setWindowShift(shift, error)) {
            std::cerr << error << '\n';
            return 1;
        }
        // Alternating full-sweep direction separates frequency structure from
        // a monotonic time drift; each frequency still has 16 independent windows.
        for (unsigned reading = 0; reading < 16; ++reading) {
            for (unsigned position = 0; position < 128; ++position) {
                const unsigned index = reading % 2 == 0 ? position : 127 - position;
                const std::uint32_t increment = base + index;
                RawIqSample sample;
                error.clear();
                const auto before = std::chrono::steady_clock::now();
                const bool valid = guard.device.measurePhaseIncrement(increment, true, sample, error);
                const auto after = std::chrono::steady_clock::now();
                output << ++sequence << ',' << shift << ',' << index << ',' << reading << ','
                       << increment << ',' << static_cast<double>(increment) * clock_hz / phase_scale << ','
                       << std::chrono::duration<double>(after - start).count() << ','
                       << std::chrono::duration<double, std::micro>(after - before).count() << ','
                       << valid << ',' << sample.period_count << ',' << sample.inc_i << ',' << sample.inc_q
                       << ',' << sample.ref_i << ',' << sample.ref_q << ',' << error << '\n';
                if (!valid) {
                    output.flush();
                    std::cerr << "FAIL at shift " << shift << ", index " << index << ": " << error << '\n';
                    return 1;
                }
            }
            output.flush();
            std::cerr << "shift=" << shift << " pass=" << reading + 1 << "/16 readings=" << sequence << '\n';
        }
    }
    std::cerr << "PASS: " << sequence << " ready-qualified readings\n";
}
