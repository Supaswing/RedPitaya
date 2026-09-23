#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct RawIqSample {
    std::int32_t inc_i = 0;
    std::int32_t inc_q = 0;
    std::int32_t ref_i = 0;
    std::int32_t ref_q = 0;
    std::uint32_t period_count = 0;
    std::uint32_t requested_frequency_hz = 0;
    std::uint32_t effective_frequency_hz = 0;
    double inc_magnitude = 0.0;
    double inc_phase_deg = 0.0;
    double ref_magnitude = 0.0;
    double ref_phase_deg = 0.0;
};

class RawIqAcquisition {
public:
    bool open();
    void close();
    bool setWindowShift(std::uint32_t window_shift, std::string& error);
    bool measure(std::uint32_t frequency_hz, bool first_point, RawIqSample& sample, std::string& error);
    bool isOpen() const;

private:
    volatile std::uint32_t* register_address(std::uint32_t offset) const;
    bool read(std::uint32_t offset, std::uint32_t& value, std::string& error) const;
    bool write(std::uint32_t offset, std::uint32_t value, std::string& error) const;

    int memory_fd_ = -1;
    void* mapping_ = nullptr;
    std::size_t mapping_size_ = 0;
    volatile std::uint32_t* registers_ = nullptr;
};
