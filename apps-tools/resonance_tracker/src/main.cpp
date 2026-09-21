#include <CustomParameters.h>
#include <DataManager.h>

#include "raw_iq_acquisition.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

void UpdateParams(void);

namespace {
constexpr int kStopped = 0;
constexpr int kRunning = 1;
constexpr int kError = 2;
constexpr std::size_t kHistorySize = 128;

CBooleanParameter rt_run("RT_RUN", CBaseParameter::RW, false, 0);
CIntParameter rt_frequency("RT_FREQUENCY_HZ", CBaseParameter::RW, 32000000, 0, 1, 62500000);
CIntParameter rt_telemetry_ms("RT_TELEMETRY_MS", CBaseParameter::RW, 50, 0, 10, 1000);
CIntParameter rt_state("RT_STATE", CBaseParameter::RO, kStopped, 0, 0, 2);
CStringParameter rt_error("RT_ERROR", CBaseParameter::RO, "", 0);
CIntParameter rt_sequence("RT_SEQUENCE", CBaseParameter::RO, 0, 0, 0, 2147483647);
CBooleanParameter rt_valid("RT_VALID", CBaseParameter::RO, false, 0);
CBooleanParameter rt_busy("RT_BUSY", CBaseParameter::RO, false, 0);
CBooleanParameter rt_overflow("RT_OVERFLOW", CBaseParameter::RO, false, 0);
CIntParameter rt_requested_frequency("RT_REQUESTED_FREQUENCY_HZ", CBaseParameter::RO, 0, 0, 0, 62500000);
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
CFloatSignal rt_history_frequency("RT_HISTORY_FREQUENCY", kHistorySize, 0.0f);
CFloatSignal rt_history_inc_mag("RT_HISTORY_INC_MAG", kHistorySize, 0.0f);
CFloatSignal rt_history_ref_mag("RT_HISTORY_REF_MAG", kHistorySize, 0.0f);

RawIqAcquisition acquisition;
std::thread worker;
std::mutex state_mutex;
bool exit_requested = false;
std::vector<float> history_frequency;
std::vector<float> history_inc_mag;
std::vector<float> history_ref_mag;
int publication_sequence = 0;

void send_state(int state, const std::string& error)
{
    rt_state.SendValue(state);
    rt_error.SendValue(error);
}

void append_history(const RawIqSample& sample)
{
    history_frequency.push_back(static_cast<float>(sample.effective_frequency_hz));
    history_inc_mag.push_back(static_cast<float>(sample.inc_magnitude));
    history_ref_mag.push_back(static_cast<float>(sample.ref_magnitude));
    if (history_frequency.size() > kHistorySize) {
        history_frequency.erase(history_frequency.begin());
        history_inc_mag.erase(history_inc_mag.begin());
        history_ref_mag.erase(history_ref_mag.begin());
    }
}

void publish_sample(const RawIqSample& sample, double acquisition_rate)
{
    rt_sequence.SendValue(++publication_sequence);
    rt_valid.SendValue(true);
    rt_requested_frequency.SendValue(static_cast<int>(sample.requested_frequency_hz));
    rt_effective_frequency.SendValue(static_cast<int>(sample.effective_frequency_hz));
    rt_inc_i.SendValue(sample.inc_i);
    rt_inc_q.SendValue(sample.inc_q);
    rt_ref_i.SendValue(sample.ref_i);
    rt_ref_q.SendValue(sample.ref_q);
    rt_inc_mag.SendValue(static_cast<float>(sample.inc_magnitude));
    rt_inc_phase.SendValue(static_cast<float>(sample.inc_phase_deg));
    rt_ref_mag.SendValue(static_cast<float>(sample.ref_magnitude));
    rt_ref_phase.SendValue(static_cast<float>(sample.ref_phase_deg));
    rt_acquisition_rate.SendValue(static_cast<float>(acquisition_rate));
    append_history(sample);
    rt_history_frequency.Set(history_frequency);
    rt_history_inc_mag.Set(history_inc_mag);
    rt_history_ref_mag.Set(history_ref_mag);
}

void acquisition_loop()
{
    bool first_point = true;
    bool was_running = false;
    auto last_measurement = std::chrono::steady_clock::now();
    auto last_publication = last_measurement;
    int publication_count = 0;
    while (true) {
        {
            std::lock_guard<std::mutex> lock(state_mutex);
            if (exit_requested) break;
        }
        if (!rt_run.Value()) {
            if (was_running) send_state(kStopped, "");
            was_running = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!acquisition.isOpen()) {
            rt_valid.SendValue(false);
            rt_run.SendValue(false);
            send_state(kError, "VNA register block is not open");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        was_running = true;
        rt_busy.SendValue(true);
        RawIqSample sample;
        std::string error;
        const bool valid = acquisition.measure(static_cast<std::uint32_t>(rt_frequency.Value()), first_point, sample, error);
        first_point = false;
        const auto end = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(end - last_measurement).count();
        last_measurement = end;
        rt_busy.SendValue(false);
        if (!valid) {
            rt_valid.SendValue(false);
            send_state(kError, error);
            rt_run.SendValue(false);
            was_running = false;
            continue;
        }

        send_state(kRunning, "");
        publish_sample(sample, elapsed > 0.0 ? 1.0 / elapsed : 0.0);
        ++publication_count;
        const auto now = std::chrono::steady_clock::now();
        if (now - last_publication >= std::chrono::seconds(1)) {
            rt_publication_rate.SendValue(static_cast<float>(publication_count));
            publication_count = 0;
            last_publication = now;
        }
    }
}
}

extern "C" const char* rp_app_desc(void)
{
    return "Raw coherent I/Q resonance tracker diagnostic application.";
}

extern "C" int rp_app_init(void)
{
    CDataManager::GetInstance()->SetParamInterval(50);
    CDataManager::GetInstance()->SetSignalInterval(50);
    rt_overflow.SendValue(false);
    if (!acquisition.open()) send_state(kError, "Unable to map VNA register block at 0x40700000");
    else send_state(kStopped, "");
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        exit_requested = false;
    }
    publication_sequence = 0;
    worker = std::thread(acquisition_loop);
    return 0;
}

extern "C" int rp_app_exit(void)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex);
        exit_requested = true;
    }
    rt_run.SendValue(false);
    if (worker.joinable()) worker.join();
    acquisition.close();
    return 0;
}

void UpdateParams(void)
{
    if (rt_run.IsNewValue()) {
        rt_run.Update();
        std::fprintf(stderr, "[resonance_tracker] RT_RUN=%d\n", rt_run.Value() ? 1 : 0);
    }
    if (rt_frequency.IsNewValue()) rt_frequency.Update();
    if (rt_telemetry_ms.IsNewValue()) {
        rt_telemetry_ms.Update();
        const int interval = std::max(10, rt_telemetry_ms.Value());
        CDataManager::GetInstance()->SetParamInterval(interval);
        CDataManager::GetInstance()->SetSignalInterval(interval);
    }
}

void UpdateSignals(void) {}
void UpdateBinarySignals(void) {}
void PostUpdateSignals(void) {}
void PostUpdateBinarySignals(void) {}
void OnNewParams(void) { UpdateParams(); }
void OnNewSignals(void) {}
