#include <CustomParameters.h>
#include <DataManager.h>

#include "iq_statistics.hpp"
#include "instrument_state.hpp"
#include "raw_iq_acquisition.hpp"
#include "raw_iq_measurement_source.hpp"
#include "resonance_analysis.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr int kStopped = static_cast<int>(InstrumentState::Stopped);
constexpr int kError = static_cast<int>(InstrumentState::Error);
constexpr std::size_t kHistorySize = 128;
constexpr int kDefaultFrequencyHz = 32000000;
constexpr int kDefaultTelemetryMs = 50;
constexpr int kDefaultWindowShift = 17;
constexpr int kDefaultPeriodCount = 0;
constexpr std::size_t kStatisticsSize = 128;
constexpr double kFpgaClockHz = 125000000.0;
constexpr std::size_t kBaselineSignalSize = 501;
constexpr std::size_t kRefinementSignalSize = 202;
constexpr std::size_t kDiagnosticSignalSize = 5;

enum class RequestedOperation {
    Idle,
    Baseline,
    Diagnostics,
};

enum class WebCommand {
    None = 0,
    StartBaseline = 1,
    CancelBaseline = 2,
    StartDiagnostics = 3,
    CancelDiagnostics = 4,
};

CBooleanParameter rt_run("RT_RUN", CBaseParameter::RW, false, 0);
CIntParameter rt_frequency("RT_FREQUENCY_HZ", CBaseParameter::RW, kDefaultFrequencyHz, 0, 1, 62500000);
CIntParameter rt_telemetry_ms("RT_TELEMETRY_MS", CBaseParameter::RW, kDefaultTelemetryMs, 0, 10, 1000);
CIntParameter rt_window_shift("RT_WINDOW_SHIFT", CBaseParameter::RW, kDefaultWindowShift, 0, 0, 20);
CIntParameter rt_command("RT_COMMAND", CBaseParameter::RW, 0, 0, 0, 4);
CIntParameter rt_command_sequence("RT_COMMAND_SEQUENCE", CBaseParameter::RW, 0, 0, 0, 2147483647);
CIntParameter rt_command_ack("RT_COMMAND_ACK", CBaseParameter::RO, 0, 0, 0, 2147483647);
CIntParameter rt_baseline_start("RT_BASELINE_START_HZ", CBaseParameter::RW, 30000000, 0, 1, 62500000);
CIntParameter rt_baseline_stop("RT_BASELINE_STOP_HZ", CBaseParameter::RW, 34000000, 0, 1, 62500000);
CIntParameter rt_baseline_sensors("RT_BASELINE_SENSOR_COUNT", CBaseParameter::RW, 1, 0, 1, 2);
CIntParameter rt_baseline_overview_points("RT_BASELINE_OVERVIEW_POINTS", CBaseParameter::RW, 101, 0, 15,
                                          kBaselineSignalSize);
CIntParameter rt_baseline_filter_radius("RT_BASELINE_FILTER_RADIUS", CBaseParameter::RW, 5, 0, 1, 25);
CIntParameter rt_baseline_coarse_averages("RT_BASELINE_COARSE_AVERAGES", CBaseParameter::RW, 3, 0, 1, 32);
CIntParameter rt_baseline_refine_points("RT_BASELINE_REFINE_POINTS", CBaseParameter::RW, 21, 0, 5, 101);
CIntParameter rt_baseline_refine_averages("RT_BASELINE_REFINE_AVERAGES", CBaseParameter::RW, 3, 0, 1, 32);
CIntParameter rt_state("RT_STATE", CBaseParameter::RO, kStopped, 0, kStopped, kError);
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
CFloatSignal rt_baseline_frequency("RT_BASELINE_FREQUENCY", kBaselineSignalSize, 0.0f);
CFloatSignal rt_baseline_signal_sequence("RT_BASELINE_SIGNAL_SEQUENCE", 1, 0.0f);
CFloatSignal rt_baseline_real("RT_BASELINE_RE", kBaselineSignalSize, 0.0f);
CFloatSignal rt_baseline_imag("RT_BASELINE_IM", kBaselineSignalSize, 0.0f);
CFloatSignal rt_baseline_filtered_magnitude("RT_BASELINE_FILTERED_MAG", kBaselineSignalSize, 0.0f);
CFloatSignal rt_baseline_curvature("RT_BASELINE_CURVATURE", kBaselineSignalSize, 0.0f);
CFloatSignal rt_candidate_left("RT_CANDIDATE_LEFT_HZ", 2, 0.0f);
CFloatSignal rt_candidate_right("RT_CANDIDATE_RIGHT_HZ", 2, 0.0f);
CFloatSignal rt_candidate_score("RT_CANDIDATE_SCORE", 2, 0.0f);
CFloatSignal rt_candidate_is_inflection("RT_CANDIDATE_IS_INFLECTION", 2, 0.0f);
CFloatSignal rt_refine_sensor_id("RT_REFINE_SENSOR_ID", kRefinementSignalSize, 0.0f);
CFloatSignal rt_refine_frequency("RT_REFINE_FREQUENCY_HZ", kRefinementSignalSize, 0.0f);
CFloatSignal rt_refine_real("RT_REFINE_RE", kRefinementSignalSize, 0.0f);
CFloatSignal rt_refine_imag("RT_REFINE_IM", kRefinementSignalSize, 0.0f);
CFloatSignal rt_model_sensor_id("RT_MODEL_SENSOR_ID", kRefinementSignalSize, 0.0f);
CFloatSignal rt_model_frequency("RT_MODEL_FREQUENCY_HZ", kRefinementSignalSize, 0.0f);
CFloatSignal rt_model_real("RT_MODEL_RE", kRefinementSignalSize, 0.0f);
CFloatSignal rt_model_imag("RT_MODEL_IM", kRefinementSignalSize, 0.0f);
CFloatSignal rt_fit_frequency("RT_FIT_FREQUENCY_HZ", 2, 0.0f);
CFloatSignal rt_fit_fwhm("RT_FIT_FWHM_HZ", 2, 0.0f);
CFloatSignal rt_diag_offset("RT_DIAG_OFFSET", kDiagnosticSignalSize, 0.0f);
CFloatSignal rt_diag_signal_sequence("RT_DIAG_SIGNAL_SEQUENCE", 1, 0.0f);
CFloatSignal rt_diag_frequency("RT_DIAG_FREQUENCY_HZ", kDiagnosticSignalSize, 0.0f);
CFloatSignal rt_diag_real("RT_DIAG_RE", kDiagnosticSignalSize, 0.0f);
CFloatSignal rt_diag_imag("RT_DIAG_IM", kDiagnosticSignalSize, 0.0f);

CIntParameter rt_baseline_sequence("RT_BASELINE_SEQUENCE", CBaseParameter::RO, 0, 0, 0, 2147483647);
CFloatParameter rt_baseline_progress("RT_BASELINE_PROGRESS", CBaseParameter::RO, 0, 0, 0, 100);
CBooleanParameter rt_baseline_complete("RT_BASELINE_COMPLETE", CBaseParameter::RO, false, 0);
CBooleanParameter rt_baseline_valid("RT_BASELINE_VALID", CBaseParameter::RO, false, 0);
CIntParameter rt_resonance_count("RT_RESONANCE_COUNT", CBaseParameter::RO, 0, 0, 0, 2);
CIntParameter rt_resonance_selected("RT_RESONANCE_SELECTED", CBaseParameter::RO, 0, 0, 0, 2);
CDoubleParameter rt_resonance_frequency("RT_RESONANCE_FREQUENCY_HZ", CBaseParameter::RO, 0, 0, 0, 62500000);
CDoubleParameter rt_resonance_q("RT_RESONANCE_Q", CBaseParameter::RO, 0, 0, 0, 1e9);
CDoubleParameter rt_resonance_fwhm("RT_RESONANCE_FWHM_HZ", CBaseParameter::RO, 0, 0, 0, 62500000);
CDoubleParameter rt_resonance_se("RT_RESONANCE_SE_HZ", CBaseParameter::RO, 0, 0, 0, 62500000);
CFloatParameter rt_resonance_quality("RT_RESONANCE_MODEL_QUALITY", CBaseParameter::RO, 0, 0, 0, 1);
CBooleanParameter rt_resonance_model_valid("RT_RESONANCE_MODEL_VALID", CBaseParameter::RO, false, 0);
CIntParameter rt_diag_sequence("RT_DIAG_SEQUENCE", CBaseParameter::RO, 0, 0, 0, 2147483647);
CIntParameter rt_diag_sensor("RT_DIAG_SENSOR_ID", CBaseParameter::RO, 0, 0, 0, 2);
CBooleanParameter rt_diag_complete("RT_DIAG_COMPLETE", CBaseParameter::RO, false, 0);

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
    int command_ack = 0;
    int baseline_sequence = 0;
    float baseline_progress = 0.0f;
    bool baseline_complete = false;
    bool baseline_valid = false;
    int resonance_count = 0;
    int resonance_selected = 0;
    double resonance_frequency_hz = 0.0;
    double resonance_q = 0.0;
    double resonance_fwhm_hz = 0.0;
    double resonance_se_hz = 0.0;
    float resonance_model_quality = 0.0f;
    bool resonance_model_valid = false;
    std::vector<float> baseline_frequency;
    std::vector<float> baseline_real;
    std::vector<float> baseline_imag;
    std::vector<float> baseline_filtered_magnitude;
    std::vector<float> baseline_curvature;
    std::vector<float> candidate_left;
    std::vector<float> candidate_right;
    std::vector<float> candidate_score;
    std::vector<float> candidate_is_inflection;
    std::vector<float> refine_sensor_id;
    std::vector<float> refine_frequency;
    std::vector<float> refine_real;
    std::vector<float> refine_imag;
    std::vector<float> model_sensor_id;
    std::vector<float> model_frequency;
    std::vector<float> model_real;
    std::vector<float> model_imag;
    std::vector<float> fit_frequency;
    std::vector<float> fit_fwhm;
    int diagnostic_sequence = 0;
    int diagnostic_sensor_id = 0;
    bool diagnostic_complete = false;
    std::vector<float> diagnostic_offset;
    std::vector<float> diagnostic_frequency;
    std::vector<float> diagnostic_real;
    std::vector<float> diagnostic_imag;
};

RawIqAcquisition acquisition;
std::thread worker;
std::atomic<bool> exit_requested{false};
std::atomic<bool> run_requested{false};
std::atomic<int> requested_frequency_hz{kDefaultFrequencyHz};
std::atomic<int> telemetry_interval_ms{kDefaultTelemetryMs};
std::atomic<int> requested_window_shift{kDefaultWindowShift};
std::atomic<int> baseline_start_hz{30000000};
std::atomic<int> baseline_stop_hz{34000000};
std::atomic<int> baseline_sensor_count{1};
std::atomic<int> baseline_overview_points{101};
std::atomic<int> baseline_filter_radius{5};
std::atomic<int> baseline_coarse_averages{3};
std::atomic<int> baseline_refine_points{21};
std::atomic<int> baseline_refine_averages{3};
std::mutex operation_mutex;
RequestedOperation requested_operation = RequestedOperation::Idle;
std::uint64_t operation_generation = 0;
std::mutex instrument_state_mutex;
InstrumentStateMachine instrument_state_machine;
std::mutex telemetry_mutex;
TelemetrySnapshot telemetry;

std::pair<RequestedOperation, std::uint64_t> operation_snapshot()
{
    std::lock_guard<std::mutex> lock(operation_mutex);
    return {requested_operation, operation_generation};
}

void request_operation(RequestedOperation operation)
{
    std::lock_guard<std::mutex> lock(operation_mutex);
    ++operation_generation;
    requested_operation = operation;
}

bool operation_is_current(RequestedOperation operation, std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(operation_mutex);
    return requested_operation == operation && operation_generation == generation;
}

bool complete_operation(RequestedOperation operation, std::uint64_t generation)
{
    std::lock_guard<std::mutex> lock(operation_mutex);
    if (requested_operation != operation || operation_generation != generation) return false;
    requested_operation = RequestedOperation::Idle;
    return true;
}

void acknowledge_command(int sequence)
{
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.command_ack = sequence;
}

void set_baseline_progress(BaselineStage stage, std::size_t completed, std::size_t total)
{
    double progress = 0.0;
    if (stage == BaselineStage::Overview)
        progress = total == 0 ? 0.0 : 70.0 * completed / total;
    else if (stage == BaselineStage::Finding)
        progress = 72.0;
    else if (stage == BaselineStage::Refining)
        progress = total == 0 ? 72.0 : 72.0 + 20.0 * completed / total;
    else
        progress = total == 0 ? 92.0 : 92.0 + 8.0 * completed / total;
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.baseline_progress = static_cast<float>(std::min(progress, 100.0));
}

void publish_baseline(const BaselineResult& result)
{
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.baseline_sequence = static_cast<int>(result.sequence);
    telemetry.baseline_progress = 100.0f;
    telemetry.baseline_complete = result.complete;
    telemetry.baseline_valid = result.valid;
    telemetry.resonance_count = static_cast<int>(result.resonances.size());
    telemetry.resonance_selected = result.resonances.empty() ? 0 : 1;
    telemetry.baseline_frequency.clear();
    telemetry.baseline_real.clear();
    telemetry.baseline_imag.clear();
    telemetry.baseline_filtered_magnitude.clear();
    telemetry.baseline_curvature.clear();
    for (const auto& point : result.overview) {
        telemetry.baseline_frequency.push_back(static_cast<float>(point.effective_frequency_hz));
        telemetry.baseline_real.push_back(static_cast<float>(point.real));
        telemetry.baseline_imag.push_back(static_cast<float>(point.imag));
    }
    for (double magnitude_squared : result.smoothed_magnitude_squared)
        telemetry.baseline_filtered_magnitude.push_back(
            static_cast<float>(std::sqrt(std::max(0.0, magnitude_squared))));
    for (double curvature : result.signed_curvature)
        telemetry.baseline_curvature.push_back(static_cast<float>(curvature));
    telemetry.candidate_left.clear();
    telemetry.candidate_right.clear();
    telemetry.candidate_score.clear();
    telemetry.candidate_is_inflection.clear();
    for (const auto& candidate : result.candidates) {
        telemetry.candidate_left.push_back(static_cast<float>(candidate.left_frequency_hz));
        telemetry.candidate_right.push_back(static_cast<float>(candidate.right_frequency_hz));
        telemetry.candidate_score.push_back(static_cast<float>(candidate.score));
        telemetry.candidate_is_inflection.push_back(candidate.from_inflection_pair ? 1.0f : 0.0f);
    }
    telemetry.refine_sensor_id.clear();
    telemetry.refine_frequency.clear();
    telemetry.refine_real.clear();
    telemetry.refine_imag.clear();
    telemetry.model_sensor_id.clear();
    telemetry.model_frequency.clear();
    telemetry.model_real.clear();
    telemetry.model_imag.clear();
    telemetry.fit_frequency.clear();
    telemetry.fit_fwhm.clear();
    for (const auto& resonance : result.resonances) {
        telemetry.fit_frequency.push_back(static_cast<float>(resonance.frequency_hz));
        telemetry.fit_fwhm.push_back(static_cast<float>(resonance.fwhm_hz));
        for (const auto& point : resonance.refinement) {
            telemetry.refine_sensor_id.push_back(static_cast<float>(resonance.sensor_id));
            telemetry.refine_frequency.push_back(static_cast<float>(point.effective_frequency_hz));
            telemetry.refine_real.push_back(static_cast<float>(point.real));
            telemetry.refine_imag.push_back(static_cast<float>(point.imag));
        }
        for (const auto& point : resonance.model) {
            telemetry.model_sensor_id.push_back(static_cast<float>(resonance.sensor_id));
            telemetry.model_frequency.push_back(static_cast<float>(point.effective_frequency_hz));
            telemetry.model_real.push_back(static_cast<float>(point.real));
            telemetry.model_imag.push_back(static_cast<float>(point.imag));
        }
    }
    if (!result.resonances.empty()) {
        const auto& resonance = result.resonances.front();
        telemetry.resonance_frequency_hz = resonance.frequency_hz;
        telemetry.resonance_q = resonance.q;
        telemetry.resonance_fwhm_hz = resonance.fwhm_hz;
        telemetry.resonance_se_hz = resonance.frequency_se_hz;
        telemetry.resonance_model_quality = static_cast<float>(resonance.model_explained_fraction);
        telemetry.resonance_model_valid = resonance.complex_model_valid;
    }
}

void publish_diagnostics(const DiagnosticResult& result)
{
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.diagnostic_sequence = static_cast<int>(result.sequence);
    telemetry.diagnostic_sensor_id = static_cast<int>(result.sensor_id);
    telemetry.diagnostic_complete = result.complete;
    telemetry.diagnostic_offset.clear();
    telemetry.diagnostic_frequency.clear();
    telemetry.diagnostic_real.clear();
    telemetry.diagnostic_imag.clear();
    for (const auto& point : result.points) {
        telemetry.diagnostic_offset.push_back(static_cast<float>(point.offset));
        telemetry.diagnostic_frequency.push_back(static_cast<float>(point.measurement.effective_frequency_hz));
        telemetry.diagnostic_real.push_back(static_cast<float>(point.measurement.real));
        telemetry.diagnostic_imag.push_back(static_cast<float>(point.measurement.imag));
    }
}

bool apply_state_command(InstrumentCommand command, const std::string& detail = "")
{
    StateTransitionResult result;
    std::string state_error;
    {
        std::lock_guard<std::mutex> state_lock(instrument_state_mutex);
        result = instrument_state_machine.apply(command, detail);
        state_error = result.accepted ? instrument_state_machine.error() : result.error;
    }
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.state = static_cast<int>(result.current);
    telemetry.error = state_error;
    return result.accepted;
}

void set_activity(bool valid, bool busy)
{
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.valid = valid;
    telemetry.busy = busy;
}

bool apply_window_shift(int window_shift, int& applied_window_shift)
{
    if (window_shift == applied_window_shift) return true;
    std::string error;
    if (!acquisition.setWindowShift(static_cast<std::uint32_t>(window_shift), error)) {
        apply_state_command(InstrumentCommand::Fail, error);
        return false;
    }
    applied_window_shift = window_shift;
    const double samples = std::ldexp(1.0, applied_window_shift);
    std::lock_guard<std::mutex> lock(telemetry_mutex);
    telemetry.effective_window_shift = applied_window_shift;
    telemetry.integration_samples = samples;
    telemetry.integration_time_us = static_cast<float>(samples * 1e6 / kFpgaClockHz);
    return true;
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
    RawIqMeasurementSource measurement_source(acquisition);
    BaselineAnalyzer baseline_analyzer;
    BaselineResult last_baseline;
    std::uint64_t baseline_sequence = 0;
    std::uint64_t diagnostic_sequence = 0;
    bool was_running = false;
    bool first_point = true;
    int last_frequency_hz = requested_frequency_hz.load();
    int applied_window_shift = kDefaultWindowShift;
    auto last_measurement = std::chrono::steady_clock::now();
    auto last_history = last_measurement - std::chrono::milliseconds(telemetry_interval_ms.load());
    auto publication_window = last_measurement;
    int publication_count = 0;

    while (!exit_requested.load()) {
        const auto [operation, generation] = operation_snapshot();
        if (operation == RequestedOperation::Baseline) {
            run_requested.store(false);
            was_running = false;
            first_point = true;
            if (!acquisition.isOpen()) {
                complete_operation(operation, generation);
                apply_state_command(InstrumentCommand::Fail, "VNA register block is not open");
                set_activity(false, false);
                continue;
            }
            if (!apply_window_shift(requested_window_shift.load(), applied_window_shift)) {
                complete_operation(operation, generation);
                set_activity(false, false);
                continue;
            }
            if (!apply_state_command(InstrumentCommand::StartBaseline)) {
                complete_operation(operation, generation);
                set_activity(false, false);
                continue;
            }
            set_activity(false, true);
            last_baseline = BaselineResult{};
            {
                std::lock_guard<std::mutex> lock(telemetry_mutex);
                telemetry.baseline_progress = 0.0f;
                telemetry.baseline_complete = false;
                telemetry.baseline_valid = false;
                telemetry.resonance_count = 0;
                telemetry.baseline_frequency.clear();
                telemetry.baseline_real.clear();
                telemetry.baseline_imag.clear();
                telemetry.baseline_filtered_magnitude.clear();
                telemetry.baseline_curvature.clear();
                telemetry.candidate_left.clear();
                telemetry.candidate_right.clear();
                telemetry.candidate_score.clear();
                telemetry.candidate_is_inflection.clear();
                telemetry.refine_sensor_id.clear();
                telemetry.refine_frequency.clear();
                telemetry.refine_real.clear();
                telemetry.refine_imag.clear();
                telemetry.model_sensor_id.clear();
                telemetry.model_frequency.clear();
                telemetry.model_real.clear();
                telemetry.model_imag.clear();
                telemetry.fit_frequency.clear();
                telemetry.fit_fwhm.clear();
            }
            BaselineConfig config;
            config.start_frequency_hz = static_cast<std::uint32_t>(baseline_start_hz.load());
            config.stop_frequency_hz = static_cast<std::uint32_t>(baseline_stop_hz.load());
            config.sensor_count = static_cast<std::size_t>(baseline_sensor_count.load());
            config.overview_points = static_cast<std::size_t>(baseline_overview_points.load());
            config.filter_radius = static_cast<std::size_t>(baseline_filter_radius.load());
            config.coarse_averages = static_cast<std::size_t>(baseline_coarse_averages.load());
            config.refine_points = static_cast<std::size_t>(baseline_refine_points.load());
            config.refine_averages = static_cast<std::size_t>(baseline_refine_averages.load());
            bool finding_state_entered = false;
            BaselineResult result = baseline_analyzer.acquire(
                ++baseline_sequence, config, measurement_source,
                [&](BaselineStage stage, std::size_t completed, std::size_t total) {
                    if (stage == BaselineStage::Finding && !finding_state_entered) {
                        apply_state_command(InstrumentCommand::StartResonanceFinding);
                        finding_state_entered = true;
                    }
                    set_baseline_progress(stage, completed, total);
                },
                [&]() {
                    return exit_requested.load() || !operation_is_current(RequestedOperation::Baseline, generation);
                });
            if (!complete_operation(RequestedOperation::Baseline, generation)) {
                apply_state_command(InstrumentCommand::CancelBaseline);
                set_activity(false, false);
                continue;
            }
            if (!result.valid) {
                if (result.complete) publish_baseline(result);
                apply_state_command(InstrumentCommand::Fail,
                                    result.error.empty() ? "baseline acquisition failed" : result.error);
                set_activity(false, false);
                continue;
            }
            if (!finding_state_entered) {
                apply_state_command(InstrumentCommand::StartResonanceFinding);
                finding_state_entered = true;
            }
            last_baseline = std::move(result);
            publish_baseline(last_baseline);
            apply_state_command(InstrumentCommand::CompleteResonanceFinding);
            set_activity(false, false);
            continue;
        }

        if (operation == RequestedOperation::Diagnostics) {
            run_requested.store(false);
            was_running = false;
            first_point = true;
            if (!apply_window_shift(requested_window_shift.load(), applied_window_shift)) {
                complete_operation(operation, generation);
                set_activity(false, false);
                continue;
            }
            if (!last_baseline.valid || last_baseline.resonances.empty()) {
                complete_operation(operation, generation);
                std::lock_guard<std::mutex> lock(telemetry_mutex);
                telemetry.error = "diagnostics requires a valid completed baseline";
                telemetry.valid = false;
                telemetry.busy = false;
                continue;
            }
            if (!apply_state_command(InstrumentCommand::StartDiagnostics)) {
                complete_operation(operation, generation);
                set_activity(false, false);
                continue;
            }
            set_activity(false, true);
            const DiagnosticResult result = acquireDiagnostics(
                ++diagnostic_sequence, last_baseline.resonances.front(), measurement_source, [&]() {
                    return exit_requested.load() || !operation_is_current(RequestedOperation::Diagnostics, generation);
                });
            if (!complete_operation(RequestedOperation::Diagnostics, generation)) {
                apply_state_command(InstrumentCommand::CancelDiagnostics);
                set_activity(false, false);
                continue;
            }
            if (!result.complete) {
                apply_state_command(InstrumentCommand::Fail,
                                    result.error.empty() ? "diagnostics acquisition failed" : result.error);
                set_activity(false, false);
                continue;
            }
            publish_diagnostics(result);
            apply_state_command(InstrumentCommand::CompleteDiagnostics);
            set_activity(false, false);
            continue;
        }

        if (!run_requested.load()) {
            if (was_running) {
                apply_state_command(InstrumentCommand::Stop);
                set_activity(false, false);
            }
            was_running = false;
            first_point = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (!acquisition.isOpen()) {
            run_requested.store(false);
            apply_state_command(InstrumentCommand::Fail, "VNA register block is not open");
            set_activity(false, false);
            continue;
        }

        const int frequency_hz = requested_frequency_hz.load();
        const bool frequency_changed = frequency_hz != last_frequency_hz;
        const int window_shift = requested_window_shift.load();
        const bool window_shift_changed = window_shift != applied_window_shift;
        last_frequency_hz = frequency_hz;
        if (!was_running) {
            if (!apply_state_command(InstrumentCommand::StartRawIq)) {
                run_requested.store(false);
                continue;
            }
        }
        if (!was_running || frequency_changed || window_shift_changed) {
            statistics.reset();
            std::lock_guard<std::mutex> lock(telemetry_mutex);
            telemetry.statistics = IqStatisticsSnapshot{};
            telemetry.valid = false;
        }
        if (window_shift_changed) {
            if (!apply_window_shift(window_shift, applied_window_shift)) {
                run_requested.store(false);
                set_activity(false, false);
                was_running = false;
                continue;
            }
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
            apply_state_command(InstrumentCommand::Fail, error);
            set_activity(false, false);
            was_running = false;
            continue;
        }
        if (!run_requested.load()) {
            apply_state_command(InstrumentCommand::Stop);
            set_activity(false, false);
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
    return "Coherent I/Q baseline, resonance analysis, and diagnostic application.";
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
    baseline_start_hz.store(30000000);
    baseline_stop_hz.store(34000000);
    baseline_sensor_count.store(1);
    baseline_overview_points.store(101);
    baseline_filter_radius.store(5);
    baseline_coarse_averages.store(3);
    baseline_refine_points.store(21);
    baseline_refine_averages.store(3);
    {
        std::lock_guard<std::mutex> lock(operation_mutex);
        requested_operation = RequestedOperation::Idle;
        operation_generation = 0;
    }
    {
        std::lock_guard<std::mutex> state_lock(instrument_state_mutex);
        instrument_state_machine.reset();
    }
    {
        std::lock_guard<std::mutex> lock(telemetry_mutex);
        telemetry = TelemetrySnapshot{};
    }
    if (!acquisition.open()) {
        apply_state_command(InstrumentCommand::Fail, "Unable to map VNA register block at 0x40700000");
        set_activity(false, false);
    }
    worker = std::thread(acquisition_loop);
    return 0;
}

extern "C" int rp_app_exit(void)
{
    exit_requested.store(true);
    run_requested.store(false);
    request_operation(RequestedOperation::Idle);
    if (worker.joinable()) worker.join();
    apply_state_command(InstrumentCommand::Stop);
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
    rt_command.SendValue(0);
    rt_command_sequence.SendValue(snapshot.command_ack);
    rt_command_ack.SendValue(snapshot.command_ack);
    rt_baseline_start.SendValue(baseline_start_hz.load());
    rt_baseline_stop.SendValue(baseline_stop_hz.load());
    rt_baseline_sensors.SendValue(baseline_sensor_count.load());
    rt_baseline_overview_points.SendValue(baseline_overview_points.load());
    rt_baseline_filter_radius.SendValue(baseline_filter_radius.load());
    rt_baseline_coarse_averages.SendValue(baseline_coarse_averages.load());
    rt_baseline_refine_points.SendValue(baseline_refine_points.load());
    rt_baseline_refine_averages.SendValue(baseline_refine_averages.load());
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
    rt_baseline_sequence.SendValue(snapshot.baseline_sequence);
    rt_baseline_progress.SendValue(snapshot.baseline_progress);
    rt_baseline_complete.SendValue(snapshot.baseline_complete);
    rt_baseline_valid.SendValue(snapshot.baseline_valid);
    rt_resonance_count.SendValue(snapshot.resonance_count);
    rt_resonance_selected.SendValue(snapshot.resonance_selected);
    rt_resonance_frequency.SendValue(snapshot.resonance_frequency_hz);
    rt_resonance_q.SendValue(snapshot.resonance_q);
    rt_resonance_fwhm.SendValue(snapshot.resonance_fwhm_hz);
    rt_resonance_se.SendValue(snapshot.resonance_se_hz);
    rt_resonance_quality.SendValue(snapshot.resonance_model_quality);
    rt_resonance_model_valid.SendValue(snapshot.resonance_model_valid);
    rt_diag_sequence.SendValue(snapshot.diagnostic_sequence);
    rt_diag_sensor.SendValue(snapshot.diagnostic_sensor_id);
    rt_diag_complete.SendValue(snapshot.diagnostic_complete);
}

void UpdateSignals(void)
{
    const TelemetrySnapshot snapshot = copy_telemetry();
    rt_history_frequency.Set(snapshot.history_frequency);
    rt_history_inc_mag.Set(snapshot.history_inc_mag);
    rt_history_ref_mag.Set(snapshot.history_ref_mag);
    rt_baseline_signal_sequence.Set(std::vector<float>{static_cast<float>(snapshot.baseline_sequence)});
    rt_baseline_frequency.Set(snapshot.baseline_frequency);
    rt_baseline_real.Set(snapshot.baseline_real);
    rt_baseline_imag.Set(snapshot.baseline_imag);
    rt_baseline_filtered_magnitude.Set(snapshot.baseline_filtered_magnitude);
    rt_baseline_curvature.Set(snapshot.baseline_curvature);
    rt_candidate_left.Set(snapshot.candidate_left);
    rt_candidate_right.Set(snapshot.candidate_right);
    rt_candidate_score.Set(snapshot.candidate_score);
    rt_candidate_is_inflection.Set(snapshot.candidate_is_inflection);
    rt_refine_sensor_id.Set(snapshot.refine_sensor_id);
    rt_refine_frequency.Set(snapshot.refine_frequency);
    rt_refine_real.Set(snapshot.refine_real);
    rt_refine_imag.Set(snapshot.refine_imag);
    rt_model_sensor_id.Set(snapshot.model_sensor_id);
    rt_model_frequency.Set(snapshot.model_frequency);
    rt_model_real.Set(snapshot.model_real);
    rt_model_imag.Set(snapshot.model_imag);
    rt_fit_frequency.Set(snapshot.fit_frequency);
    rt_fit_fwhm.Set(snapshot.fit_fwhm);
    rt_diag_signal_sequence.Set(std::vector<float>{static_cast<float>(snapshot.diagnostic_sequence)});
    rt_diag_offset.Set(snapshot.diagnostic_offset);
    rt_diag_frequency.Set(snapshot.diagnostic_frequency);
    rt_diag_real.Set(snapshot.diagnostic_real);
    rt_diag_imag.Set(snapshot.diagnostic_imag);
}

void UpdateBinarySignals(void) {}
void PostUpdateSignals(void) {}
void PostUpdateBinarySignals(void) {}

void OnNewParams(void)
{
    if (rt_run.IsNewValue()) {
        rt_run.Update();
        if (rt_run.Value()) {
            request_operation(RequestedOperation::Idle);
        }
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
    if (rt_baseline_start.IsNewValue()) {
        rt_baseline_start.Update();
        baseline_start_hz.store(rt_baseline_start.Value());
    }
    if (rt_baseline_stop.IsNewValue()) {
        rt_baseline_stop.Update();
        baseline_stop_hz.store(rt_baseline_stop.Value());
    }
    if (rt_baseline_sensors.IsNewValue()) {
        rt_baseline_sensors.Update();
        baseline_sensor_count.store(rt_baseline_sensors.Value());
    }
    if (rt_baseline_overview_points.IsNewValue()) {
        rt_baseline_overview_points.Update();
        baseline_overview_points.store(rt_baseline_overview_points.Value());
    }
    if (rt_baseline_filter_radius.IsNewValue()) {
        rt_baseline_filter_radius.Update();
        baseline_filter_radius.store(rt_baseline_filter_radius.Value());
    }
    if (rt_baseline_coarse_averages.IsNewValue()) {
        rt_baseline_coarse_averages.Update();
        baseline_coarse_averages.store(rt_baseline_coarse_averages.Value());
    }
    if (rt_baseline_refine_points.IsNewValue()) {
        rt_baseline_refine_points.Update();
        baseline_refine_points.store(rt_baseline_refine_points.Value());
    }
    if (rt_baseline_refine_averages.IsNewValue()) {
        rt_baseline_refine_averages.Update();
        baseline_refine_averages.store(rt_baseline_refine_averages.Value());
    }
    if (rt_command.IsNewValue()) rt_command.Update();
    if (rt_command_sequence.IsNewValue()) {
        rt_command_sequence.Update();
        const int sequence = rt_command_sequence.Value();
        const auto command = static_cast<WebCommand>(rt_command.Value());
        if (command == WebCommand::StartBaseline) {
            run_requested.store(false);
            request_operation(RequestedOperation::Baseline);
        } else if (command == WebCommand::CancelBaseline) {
            request_operation(RequestedOperation::Idle);
        } else if (command == WebCommand::StartDiagnostics) {
            run_requested.store(false);
            request_operation(RequestedOperation::Diagnostics);
        } else if (command == WebCommand::CancelDiagnostics) {
            request_operation(RequestedOperation::Idle);
        }
        acknowledge_command(sequence);
    }
}

void OnNewSignals(void) {}
