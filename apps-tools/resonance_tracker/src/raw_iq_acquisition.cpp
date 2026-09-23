#include "raw_iq_acquisition.hpp"

#include <cmath>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

namespace {
constexpr std::uintptr_t kBaseAddress = 0x40700000U;
constexpr std::size_t kRegisterSpan = 0x1000U;
constexpr std::uint32_t kControl = 0x00U;
constexpr std::uint32_t kPhaseIncrement = 0x04U;
constexpr std::uint32_t kPhaseOffset = 0x08U;
constexpr std::uint32_t kPeriodCount = 0x0cU;
constexpr std::uint32_t kAmplitude = 0x10U;
constexpr std::uint32_t kVnaControl = 0x14U;
constexpr std::uint32_t kWindowShift = 0x18U;
constexpr std::uint32_t kIncI = 0x1cU;
constexpr std::uint32_t kIncQ = 0x20U;
constexpr std::uint32_t kRefI = 0x24U;
constexpr std::uint32_t kRefQ = 0x28U;
constexpr std::uint32_t kStatus = 0x2cU;
constexpr std::uint32_t kRestart = 0x0fU;
constexpr std::uint32_t kRun = 0x0dU;
constexpr std::uint32_t kStop = 0x0cU;
constexpr std::uint32_t kMeasurementStart = 0x01U;
constexpr std::uint32_t kReady = 0x01U;
constexpr std::uint64_t kFpgaClockHz = 125000000ULL;
constexpr std::uint32_t kDefaultWindowShift = 17U;
constexpr std::uint32_t kDefaultAmplitude = 0x0800U;
constexpr std::uint32_t kSettlingUs = 100U;
constexpr std::uint32_t kTimeoutUs = 10000U;

std::uint64_t monotonic_ns()
{
    timespec timestamp{};
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    return static_cast<std::uint64_t>(timestamp.tv_sec) * 1000000000ULL +
           static_cast<std::uint64_t>(timestamp.tv_nsec);
}

std::uint32_t frequency_to_phase_increment(std::uint32_t frequency_hz)
{
    const std::uint64_t scaled = static_cast<std::uint64_t>(frequency_hz) * (1ULL << 32U);
    return static_cast<std::uint32_t>((scaled + kFpgaClockHz / 2U) / kFpgaClockHz);
}

std::uint32_t phase_increment_to_frequency(std::uint32_t phase_increment)
{
    const std::uint64_t scaled = static_cast<std::uint64_t>(phase_increment) * kFpgaClockHz;
    return static_cast<std::uint32_t>((scaled + (1ULL << 31U)) >> 32U);
}

}

bool RawIqAcquisition::open()
{
    close();
    const long page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return false;

    const std::uintptr_t page_base = kBaseAddress & ~static_cast<std::uintptr_t>(page_size - 1);
    const std::size_t page_offset = static_cast<std::size_t>(kBaseAddress - page_base);
    mapping_size_ = (page_offset + kRegisterSpan + static_cast<std::size_t>(page_size - 1)) &
                    ~static_cast<std::size_t>(page_size - 1);
    memory_fd_ = ::open("/dev/mem", O_RDWR | O_SYNC);
    if (memory_fd_ < 0) return false;
    mapping_ = mmap(nullptr, mapping_size_, PROT_READ | PROT_WRITE, MAP_SHARED, memory_fd_, static_cast<off_t>(page_base));
    if (mapping_ == MAP_FAILED) {
        mapping_ = nullptr;
        close();
        return false;
    }
    registers_ = reinterpret_cast<volatile std::uint32_t*>(static_cast<std::uint8_t*>(mapping_) + page_offset);
    std::string error;
    const bool initialized = write(kWindowShift, kDefaultWindowShift, error) &&
                             write(kAmplitude, kDefaultAmplitude, error) &&
                             write(kPhaseOffset, 0U, error) && write(kStatus, 0U, error);
    if (!initialized) close();
    return initialized;
}

void RawIqAcquisition::close()
{
    if (mapping_ != nullptr) munmap(mapping_, mapping_size_);
    if (memory_fd_ >= 0) ::close(memory_fd_);
    mapping_ = nullptr;
    registers_ = nullptr;
    mapping_size_ = 0;
    memory_fd_ = -1;
}

bool RawIqAcquisition::isOpen() const
{
    return registers_ != nullptr;
}

bool RawIqAcquisition::setWindowShift(std::uint32_t window_shift, std::string& error)
{
    if (!write(kWindowShift, window_shift, error)) return false;
    std::uint32_t applied_window_shift = 0;
    if (!read(kWindowShift, applied_window_shift, error)) return false;
    if (applied_window_shift != window_shift) {
        error = "VNA window shift readback mismatch";
        return false;
    }
    return true;
}

volatile std::uint32_t* RawIqAcquisition::register_address(std::uint32_t offset) const
{
    if (registers_ == nullptr || offset % sizeof(std::uint32_t) != 0U || offset + sizeof(std::uint32_t) > kRegisterSpan)
        return nullptr;
    return registers_ + offset / sizeof(std::uint32_t);
}

bool RawIqAcquisition::read(std::uint32_t offset, std::uint32_t& value, std::string& error) const
{
    volatile std::uint32_t* address = register_address(offset);
    if (address == nullptr) {
        error = "invalid VNA register read";
        return false;
    }
    value = *address;
    __sync_synchronize();
    return true;
}

bool RawIqAcquisition::write(std::uint32_t offset, std::uint32_t value, std::string& error) const
{
    volatile std::uint32_t* address = register_address(offset);
    if (address == nullptr) {
        error = "invalid VNA register write";
        return false;
    }
    *address = value;
    __sync_synchronize();
    return true;
}

bool RawIqAcquisition::measure(std::uint32_t frequency_hz, bool first_point, RawIqSample& sample, std::string& error)
{
    if (!isOpen()) {
        error = "VNA register block is not open";
        return false;
    }

    const std::uint32_t phase_increment = frequency_to_phase_increment(frequency_hz);
    if (!write(kPhaseIncrement, phase_increment, error) || !write(kControl, kRestart, error) ||
        !write(kControl, kRun, error)) return false;
    (void)first_point;
    usleep(kSettlingUs);
    if (!write(kStatus, 0U, error) || !write(kVnaControl, kMeasurementStart, error)) return false;

    const std::uint64_t deadline = monotonic_ns() + static_cast<std::uint64_t>(kTimeoutUs) * 1000ULL;
    std::uint32_t status = 0;
    while (true) {
        if (!read(kStatus, status, error)) return false;
        if ((status & kReady) != 0U) break;
        if (monotonic_ns() >= deadline) {
            write(kStatus, 0U, error);
            error = "VNA measurement timeout";
            return false;
        }
        usleep(1);
    }

    std::uint32_t inc_i = 0;
    std::uint32_t inc_q = 0;
    std::uint32_t ref_i = 0;
    std::uint32_t ref_q = 0;
    std::uint32_t period_count = 0;
    if (!read(kIncI, inc_i, error) || !read(kIncQ, inc_q, error) || !read(kRefI, ref_i, error) ||
        !read(kRefQ, ref_q, error) || !read(kPeriodCount, period_count, error)) return false;
    if (!write(kStatus, 0U, error)) return false;

    sample.requested_frequency_hz = frequency_hz;
    sample.effective_frequency_hz = phase_increment_to_frequency(phase_increment);
    sample.inc_i = static_cast<std::int32_t>(inc_i);
    sample.inc_q = static_cast<std::int32_t>(inc_q);
    sample.ref_i = static_cast<std::int32_t>(ref_i);
    sample.ref_q = static_cast<std::int32_t>(ref_q);
    sample.period_count = period_count;
    sample.inc_magnitude = std::hypot(static_cast<double>(sample.inc_i), static_cast<double>(sample.inc_q));
    sample.inc_phase_deg = std::atan2(static_cast<double>(sample.inc_q), static_cast<double>(sample.inc_i)) * 180.0 / M_PI;
    sample.ref_magnitude = std::hypot(static_cast<double>(sample.ref_i), static_cast<double>(sample.ref_q));
    sample.ref_phase_deg = std::atan2(static_cast<double>(sample.ref_q), static_cast<double>(sample.ref_i)) * 180.0 / M_PI;
    return true;
}
