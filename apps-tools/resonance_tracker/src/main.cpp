#include <CustomParameters.h>
#include <DataManager.h>

#include "iq_statistics.hpp"
#include "raw_iq_acquisition.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {
constexpr int kStopped = 0;
constexpr int kRunning = 1;
constexpr int kError = 2;
constexpr std::size_t kHistorySize = 128;
constexpr int kDefaultFrequencyHz = 32000000;
constexpr int kDefaultTelemetryMs = 50;
constexpr int kDefaultWindowShift = 17;
constexpr int kDefaultPeriodCount = 0;
constexpr std::size_t kStatisticsSize = 128;
constexpr double kFpgaClockHz = 125000000.0;

CBooleanParameter rt_run("RT_RUN", CBaseParameter::RW, false, 0);
CIntParameter rt_frequency("RT_FREQUENCY_HZ", CBaseParameter::RW, kDefaultFrequencyHz, 0, 1, 62500000);
CIntParameter rt_telemetry_ms("RT_TELEMETRY_MS", CBaseParameter::RW, kDefaultTelemetryMs, 0, 10, 1000);
CIntParameter rt_window_shift("RT_WINDOW_SHIFT", CBaseParameter::RW, kDefaultWindowShift, 0, 0, 20);
CIntParameter rt_state("RT_STATE", CBaseParameter::RO, kStopped, 0, 0, 2);
CStringParameter rt_error("RT_ERROR", CBaseParameter::RO, "", 0);
CIntParameter rt_sequence("RT_SEQUENCE", CBaseParameter::RO, 0, 0, 0, 2147483647);
CBooleanParameter rt_valid("RT_VALID", CBaseParameter::RO, false, 0);
CBooleanParameter rt_busy("RT_BUSY", CBaseParameter::RO, false, 0);
CBooleanParameter rt_overflow("RT_OVERFLOW", CBaseParameter::RO, false, 0);
CIntParameter rt_requested_frequency("RT_REQUESTED_FREQUENCY_HZ", CBaseParameter::RO, kDefaultFrequencyHz, 0, 0,
                                     62500000);
CIntParameter rt_effective_frequency("RT_EFFECTIVE_FREQUENCY_HZ", CBaseParameter::RO, 0, 0, 0, 62500000);
CIntParameter rt_inc_i("RT_INC_I", CBaseParameter::RO, 0, 0, -2147483647, 2147483647);
CIntParameter rt_inc_q("RT_INC_Q", CBaseParameter::RO, 0, 0, -2147483647, 2147483647);
CIntParameter rt_ref_i("RT_REF_I", CBaseParameter::RO, 0, 0, -2147483647, 2147483647);
CIntParameter rt_ref_q("RT_REF_Q", CBaseParameter::RO, 0, 0, -2147483647, 2147483647);
CFloatParameter rt_inc_mag("RT_INC_MAG", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_inc_phase("RT_INC_PHASE_DEG", CBaseParameter::RO, 0, 0, -180, 180);
CFloatParameter rt_ref_mag("RT_REF_MAG", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ref_phase("RT_REF_PHASE_DEG", CBaseParameter::RO, 0, 0, -180, 180);
CFloatParameter rt_acquisition_rate("RT_ACQUISITION_RATE_HZ", CBaseParameter::RO, 0, 0, 0, 1e6);
CFloatParameter rt_publication_rate("RT_PUBLICATION_RATE_HZ", CBaseParameter::RO, 0, 0, 0, 1e6);
CIntParameter rt_effective_window_shift("RT_EFFECTIVE_WINDOW_SHIFT", CBaseParameter::RO, kDefaultWindowShift, 0, 0,
                                        20);
CDoubleParameter rt_integration_samples("RT_INTEGRATION_SAMPLES", CBaseParameter::RO, 131072.0, 0, 1.0,
                                        1048576.0);
CFloatParameter rt_integration_time_us("RT_INTEGRATION_TIME_US", CBaseParameter::RO, 1048.576f, 0, 0, 8388.608f);
CIntParameter rt_period_count("RT_PERIOD_COUNT", CBaseParameter::RO, kDefaultPeriodCount, 0, 0, 2147483647);
CIntParameter rt_stats_count("RT_STATS_COUNT", CBaseParameter::RO, 0, 0, 0, kStatisticsSize);
CIntParameter rt_ratio_stats_count("RT_RATIO_STATS_COUNT", CBaseParameter::RO, 0, 0, 0, kStatisticsSize);
CBooleanParameter rt_ratio_valid("RT_R_VALID", CBaseParameter::RO, false, 0);
CFloatParameter rt_ratio_real("RT_R_REAL", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_ratio_imag("RT_R_IMAG", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_ratio_magnitude("RT_R_MAG", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ratio_phase("RT_R_PHASE_DEG", CBaseParameter::RO, 0, 0, -180, 180);
CFloatParameter rt_inc_i_mean("RT_INC_I_MEAN", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_inc_i_stddev("RT_INC_I_STDDEV", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_inc_q_mean("RT_INC_Q_MEAN", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_inc_q_stddev("RT_INC_Q_STDDEV", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ref_i_mean("RT_REF_I_MEAN", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_ref_i_stddev("RT_REF_I_STDDEV", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ref_q_mean("RT_REF_Q_MEAN", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_ref_q_stddev("RT_REF_Q_STDDEV", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ratio_real_mean("RT_R_REAL_MEAN", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_ratio_real_stddev("RT_R_REAL_STDDEV", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ratio_imag_mean("RT_R_IMAG_MEAN", CBaseParameter::RO, 0, 0, -1e12, 1e12);
CFloatParameter rt_ratio_imag_stddev("RT_R_IMAG_STDDEV", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ratio_magnitude_mean("RT_R_MAG_MEAN", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ratio_magnitude_stddev("RT_R_MAG_STDDEV", CBaseParameter::RO, 0, 0, 0, 1e12);
CFloatParameter rt_ratio_mean_phase("RT_R_MEAN_PHASE_DEG", CBaseParameter::RO, 0, 0, -180, 180);
CFloatSignal rt_history_frequency("RT_HISTORY_FREQUENCY", kHistorySize, 0.0f);
CFloatSignal rt_history_inc_mag("RT_HISTORY_INC_MAG", kHistorySize, 0.0f);
CFloatSignal rt_history_ref_mag("RT_HISTORY_REF_MAG", kHistorySize, 0.0f);

struct TelemetrySnapshot {
    int state = kStopped;
    std::string error;
    int sequence = 0;
    bool valid = false;
    bool busy = false;
    bool overflow = false;
    int requested_frequency_hz = kDefaultFrequencyHz;
    int effective_frequency_hz = 0;
    std::int32_t inc_i = 0;
    std::int32_t inc_q = 0;
    std::int32_t ref_i = 0;
    std::int32_t ref_q = 0;
    float inc_magnitude = 0.0f;
    float inc_phase_deg = 0.0f;
    float ref_magnitude = 0.0f;
    float ref_phase_deg = 0.0f;
    float acquisition_rate_hz = 0.0f;
    float publication_rate_hz = 0.0f;
    int effective_window_shift = kDefaultWindowShift;
    double integration_samples = 131072.0;
    float integration_time_us = 1048.576f;
    IqStatisticsSnapshot statistics;
    std::vector<float> history_frequency;
    std::vector<float> history_inc_mag;
    std::vector<float> history_ref_mag;
};

RawIqAcquisition acquisition;
std::thread worker;
std::atomic<bool> exit_requested{false};
std::atomic<bool> run_requested{false};
std::atomic<int> requested_frequency_hz{kDefaultFrequencyHz};
std::atomic<int> telemetry_interval_ms{kDefaultTelemetryMs};
std::atomic<int> requested_window_shift{kDefaultWindowShift};
std::mutex telemetry_mutex;
TelemetrySnapshot telemetry;

void set_state(int state, const std::string& error, bool valid, bool busy)
{
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.state = state;
    telemetry.error = error;
    telemetry.valid = valid;
    telemetry.busy = busy;
}

void append_history(TelemetrySnapshot& snapshot, const RawIqSample& sample)
{
    snapshot.history_frequency.push_back(static_cast<float>(sample.effective_frequency_hz));
    snapshot.history_inc_mag.push_back(static_cast<float>(sample.inc_magnitude));
    snapshot.history_ref_mag.push_back(static_cast<float>(sample.ref_magnitude));
    if (snapshot.history_frequency.size() > kHistorySize) {
        snapshot.history_frequency.erase(snapshot.history_frequency.begin());
        snapshot.history_inc_mag.erase(snapshot.history_inc_mag.begin());
        snapshot.history_ref_mag.erase(snapshot.history_ref_mag.begin());
    }
}

void publish_sample(const RawIqSample& sample, const IqStatisticsSnapshot& statistics, double acquisition_rate,
                    bool publish_history, double publication_rate)
{
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    ++telemetry.sequence;
    telemetry.state = kRunning;
    telemetry.error.clear();
    telemetry.valid = true;
    telemetry.busy = false;
    telemetry.requested_frequency_hz = static_cast<int>(sample.requested_frequency_hz);
    telemetry.effective_frequency_hz = static_cast<int>(sample.effective_frequency_hz);
    telemetry.inc_i = sample.inc_i;
    telemetry.inc_q = sample.inc_q;
    telemetry.ref_i = sample.ref_i;
    telemetry.ref_q = sample.ref_q;
    telemetry.inc_magnitude = static_cast<float>(sample.inc_magnitude);
    telemetry.inc_phase_deg = static_cast<float>(sample.inc_phase_deg);
    telemetry.ref_magnitude = static_cast<float>(sample.ref_magnitude);
    telemetry.ref_phase_deg = static_cast<float>(sample.ref_phase_deg);
    telemetry.acquisition_rate_hz = static_cast<float>(acquisition_rate);
    telemetry.statistics = statistics;
    if (publish_history) append_history(telemetry, sample);
    if (publication_rate >= 0.0) telemetry.publication_rate_hz = static_cast<float>(publication_rate);
}

void acquisition_loop()
{
    RollingIqStatistics statistics(kStatisticsSize);
    bool was_running = false;
    bool first_point = true;
    int last_frequency_hz = requested_frequency_hz.load();
    int applied_window_shift = kDefaultWindowShift;
    auto last_measurement = std::chrono::steady_clock::now();
    auto last_history = last_measurement - std::chrono::milliseconds(telemetry_interval_ms.load());
    auto publication_window = last_measurement;
    int publication_count = 0;

    while (!exit_requested.load()) {
        if (!run_requested.load()) {
            if (was_running) set_state(kStopped, "", false, false);
            was_running = false;
            first_point = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!acquisition.isOpen()) {
            run_requested.store(false);
            set_state(kError, "VNA register block is not open", false, false);
            continue;
        }

        const int frequency_hz = requested_frequency_hz.load();
        const bool frequency_changed = frequency_hz != last_frequency_hz;
        const int window_shift = requested_window_shift.load();
        const bool window_shift_changed = window_shift != applied_window_shift;
        last_frequency_hz = frequency_hz;
        if (!was_running || frequency_changed || window_shift_changed) {
            statistics.reset();
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            telemetry.statistics = IqStatisticsSnapshot{};
            telemetry.valid = false;
        }
        if (window_shift_changed) {
            std::string configuration_error;
            if (!acquisition.setWindowShift(static_cast<std::uint32_t>(window_shift), configuration_error)) {
                run_requested.store(false);
                set_state(kError, configuration_error, false, false);
                was_running = false;
                continue;
            }
            applied_window_shift = window_shift;
            const double samples = std::ldexp(1.0, applied_window_shift);
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            telemetry.effective_window_shift = applied_window_shift;
            telemetry.integration_samples = samples;
            telemetry.integration_time_us = static_cast<float>(samples * 1e6 / kFpgaClockHz);
        }
        was_running = true;
        {
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            telemetry.busy = true;
            if (first_point || frequency_changed) telemetry.valid = false;
            telemetry.requested_frequency_hz = frequency_hz;
        }

        RawIqSample sample;
        std::string error;
        const bool valid = acquisition.measure(static_cast<std::uint32_t>(frequency_hz), first_point || frequency_changed,
                                               sample, error);
        first_point = false;
        const auto now = std::chrono::steady_clock::now();
        const double acquisition_elapsed = std::chrono::duration<double>(now - last_measurement).count();
        last_measurement = now;
        if (!valid) {
            run_requested.store(false);
            set_state(kError, error, false, false);
            was_running = false;
            continue;
        }
        if (!run_requested.load()) {
            set_state(kStopped, "", false, false);
            was_running = false;
            first_point = true;
            continue;
        }
        if (requested_frequency_hz.load() != frequency_hz) {
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            telemetry.valid = false;
            telemetry.busy = false;
            telemetry.requested_frequency_hz = requested_frequency_hz.load();
            first_point = true;
            continue;
        }
        if (requested_window_shift.load() != window_shift) {
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            telemetry.valid = false;
            telemetry.busy = false;
            first_point = true;
            continue;
        }

        const auto history_interval = std::chrono::milliseconds(telemetry_interval_ms.load());
        const bool publish_history = now - last_history >= history_interval;
        double publication_rate = -1.0;
        if (publish_history) {
            last_history = now;
            ++publication_count;
        }
        const double publication_elapsed = std::chrono::duration<double>(now - publication_window).count();
        if (publication_elapsed >= 1.0) {
            publication_rate = publication_count / publication_elapsed;
            publication_count = 0;
            publication_window = now;
        }
        statistics.add(sample);
        publish_sample(sample, statistics.snapshot(), acquisition_elapsed > 0.0 ? 1.0 / acquisition_elapsed : 0.0,
                       publish_history, publication_rate);
    }
}

TelemetrySnapshot copy_telemetry()
{
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    return telemetry;
}
}

extern "C" const char* rp_app_desc(void)
{
    return "Raw coherent I/Q resonance tracker diagnostic application.";
}

extern "C" int rp_app_init(void)
{
    CDataManager::GetInstance()->SetParamInterval(kDefaultTelemetryMs);
    CDataManager::GetInstance()->SetSignalInterval(kDefaultTelemetryMs);
    exit_requested.store(false);
    run_requested.store(false);
    requested_frequency_hz.store(kDefaultFrequencyHz);
    telemetry_interval_ms.store(kDefaultTelemetryMs);
    requested_window_shift.store(kDefaultWindowShift);
    {
        std::lock_guard<std::mutex> lock(telemetry_mutex);
        telemetry = TelemetrySnapshot{};
    }
    if (!acquisition.open()) set_state(kError, "Unable to map VNA register block at 0x40700000", false, false);
    worker = std::thread(acquisition_loop);
    return 0;
}

extern "C" int rp_app_exit(void)
{
    exit_requested.store(true);
    run_requested.store(false);
    if (worker.joinable()) worker.join();
    acquisition.close();
    return 0;
}

void UpdateParams(void)
{
    const TelemetrySnapshot snapshot = copy_telemetry();
    rt_run.SendValue(run_requested.load());
    rt_frequency.SendValue(requested_frequency_hz.load());
    rt_telemetry_ms.SendValue(telemetry_interval_ms.load());
    rt_window_shift.SendValue(requested_window_shift.load());
    rt_state.SendValue(snapshot.state);
    rt_error.SendValue(snapshot.error);
    rt_sequence.SendValue(snapshot.sequence);
    rt_valid.SendValue(snapshot.valid);
    rt_busy.SendValue(snapshot.busy);
    rt_overflow.SendValue(snapshot.overflow);
    rt_requested_frequency.SendValue(snapshot.requested_frequency_hz);
    rt_effective_frequency.SendValue(snapshot.effective_frequency_hz);
    rt_inc_i.SendValue(snapshot.inc_i);
    rt_inc_q.SendValue(snapshot.inc_q);
    rt_ref_i.SendValue(snapshot.ref_i);
    rt_ref_q.SendValue(snapshot.ref_q);
    rt_inc_mag.SendValue(snapshot.inc_magnitude);
    rt_inc_phase.SendValue(snapshot.inc_phase_deg);
    rt_ref_mag.SendValue(snapshot.ref_magnitude);
    rt_ref_phase.SendValue(snapshot.ref_phase_deg);
    rt_acquisition_rate.SendValue(snapshot.acquisition_rate_hz);
    rt_publication_rate.SendValue(snapshot.publication_rate_hz);
    rt_effective_window_shift.SendValue(snapshot.effective_window_shift);
    rt_integration_samples.SendValue(snapshot.integration_samples);
    rt_integration_time_us.SendValue(snapshot.integration_time_us);
    rt_period_count.SendValue(kDefaultPeriodCount);
    rt_stats_count.SendValue(static_cast<int>(snapshot.statistics.sample_count));
    rt_ratio_stats_count.SendValue(static_cast<int>(snapshot.statistics.ratio_sample_count));
    rt_ratio_valid.SendValue(snapshot.statistics.ratio_valid);
    rt_ratio_real.SendValue(static_cast<float>(snapshot.statistics.ratio_real));
    rt_ratio_imag.SendValue(static_cast<float>(snapshot.statistics.ratio_imag));
    rt_ratio_magnitude.SendValue(static_cast<float>(snapshot.statistics.ratio_magnitude));
    rt_ratio_phase.SendValue(static_cast<float>(snapshot.statistics.ratio_phase_deg));
    rt_inc_i_mean.SendValue(static_cast<float>(snapshot.statistics.inc_i.mean));
    rt_inc_i_stddev.SendValue(static_cast<float>(snapshot.statistics.inc_i.sample_standard_deviation));
    rt_inc_q_mean.SendValue(static_cast<float>(snapshot.statistics.inc_q.mean));
    rt_inc_q_stddev.SendValue(static_cast<float>(snapshot.statistics.inc_q.sample_standard_deviation));
    rt_ref_i_mean.SendValue(static_cast<float>(snapshot.statistics.ref_i.mean));
    rt_ref_i_stddev.SendValue(static_cast<float>(snapshot.statistics.ref_i.sample_standard_deviation));
    rt_ref_q_mean.SendValue(static_cast<float>(snapshot.statistics.ref_q.mean));
    rt_ref_q_stddev.SendValue(static_cast<float>(snapshot.statistics.ref_q.sample_standard_deviation));
    rt_ratio_real_mean.SendValue(static_cast<float>(snapshot.statistics.ratio_real_statistics.mean));
    rt_ratio_real_stddev.SendValue(
        static_cast<float>(snapshot.statistics.ratio_real_statistics.sample_standard_deviation));
    rt_ratio_imag_mean.SendValue(static_cast<float>(snapshot.statistics.ratio_imag_statistics.mean));
    rt_ratio_imag_stddev.SendValue(
        static_cast<float>(snapshot.statistics.ratio_imag_statistics.sample_standard_deviation));
    rt_ratio_magnitude_mean.SendValue(static_cast<float>(snapshot.statistics.ratio_magnitude_statistics.mean));
    rt_ratio_magnitude_stddev.SendValue(
        static_cast<float>(snapshot.statistics.ratio_magnitude_statistics.sample_standard_deviation));
    rt_ratio_mean_phase.SendValue(static_cast<float>(snapshot.statistics.mean_ratio_phase_deg));
}

void UpdateSignals(void)
{
    const TelemetrySnapshot snapshot = copy_telemetry();
    rt_history_frequency.Set(snapshot.history_frequency);
    rt_history_inc_mag.Set(snapshot.history_inc_mag);
    rt_history_ref_mag.Set(snapshot.history_ref_mag);
}

void UpdateBinarySignals(void) {}
void PostUpdateSignals(void) {}
void PostUpdateBinarySignals(void) {}

void OnNewParams(void)
{
    if (rt_run.IsNewValue()) {
        rt_run.Update();
        run_requested.store(rt_run.Value());
        std::fprintf(stderr, "[resonance_tracker] RT_RUN=%d\n", rt_run.Value() ? 1 : 0);
    }
    if (rt_frequency.IsNewValue()) {
        rt_frequency.Update();
        requested_frequency_hz.store(rt_frequency.Value());
    }
    if (rt_telemetry_ms.IsNewValue()) {
        rt_telemetry_ms.Update();
        const int interval = std::clamp(rt_telemetry_ms.Value(), 10, 1000);
        telemetry_interval_ms.store(interval);
        CDataManager::GetInstance()->SetParamInterval(interval);
        CDataManager::GetInstance()->SetSignalInterval(interval);
    }
    if (rt_window_shift.IsNewValue()) {
        rt_window_shift.Update();
        requested_window_shift.store(rt_window_shift.Value());
    }
}

void OnNewSignals(void) {}
