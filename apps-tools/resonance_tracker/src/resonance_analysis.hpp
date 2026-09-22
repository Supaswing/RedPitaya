#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ComplexMeasurement {
    std::uint32_t requested_frequency_hz = 0;
    std::uint32_t effective_frequency_hz = 0;
    double real = 0.0;
    double imag = 0.0;
};

class ComplexMeasurementSource {
public:
    virtual ~ComplexMeasurementSource() = default;
    virtual bool acquire(std::uint32_t frequency_hz, bool first_point, ComplexMeasurement& measurement,
                         std::string& error) = 0;
};

class ReplayMeasurementSource : public ComplexMeasurementSource {
public:
    explicit ReplayMeasurementSource(std::vector<ComplexMeasurement> points);
    bool acquire(std::uint32_t frequency_hz, bool first_point, ComplexMeasurement& measurement,
                 std::string& error) override;

private:
    std::vector<ComplexMeasurement> points_;
};

struct BaselineConfig {
    std::uint32_t start_frequency_hz = 30000000;
    std::uint32_t stop_frequency_hz = 34000000;
    std::size_t overview_points = 101;
    std::size_t filter_radius = 5;
    std::size_t coarse_averages = 3;
    std::size_t refine_points = 21;
    std::size_t refine_averages = 3;
    std::size_t sensor_count = 1;
};

struct ResonanceCandidate {
    bool from_inflection_pair = false;
    double left_frequency_hz = 0.0;
    double right_frequency_hz = 0.0;
    double frequency_hz = 0.0;
    double fwhm_hz = 0.0;
    double score = 0.0;
    double curvature_area = 0.0;
    double selection_quality = 0.0;
};

struct ResonanceEstimate {
    bool valid = false;
    std::uint32_t sensor_id = 0;
    double frequency_hz = 0.0;
    double q = 0.0;
    double fwhm_hz = 0.0;
    double spacing_hz = 0.0;
    double frequency_se_hz = 0.0;
    double model_explained_fraction = 0.0;
    bool complex_model_valid = false;
    ResonanceCandidate candidate;
    std::vector<ComplexMeasurement> refinement;
    std::vector<ComplexMeasurement> model;
    std::vector<ComplexMeasurement> template_points;
};

struct BaselineResult {
    bool complete = false;
    bool valid = false;
    std::uint64_t sequence = 0;
    std::string error;
    BaselineConfig config;
    std::vector<ComplexMeasurement> overview;
    std::vector<double> smoothed_magnitude_squared;
    std::vector<double> signed_curvature;
    std::vector<ResonanceCandidate> candidates;
    std::vector<ResonanceEstimate> resonances;
};

enum class BaselineStage {
    Overview,
    Finding,
    Refining,
    Template,
};

using BaselineProgress = std::function<void(BaselineStage stage, std::size_t completed, std::size_t total)>;
using CancellationCheck = std::function<bool()>;

class BaselineAnalyzer {
public:
    BaselineResult acquire(std::uint64_t sequence, const BaselineConfig& config, ComplexMeasurementSource& source,
                           const BaselineProgress& progress = {}, const CancellationCheck& cancelled = {}) const;
    static std::vector<ResonanceCandidate> findCandidates(const std::vector<ComplexMeasurement>& overview,
                                                           std::size_t wanted,
                                                           std::size_t filter_radius,
                                                           double minimum_q,
                                                           double maximum_q,
                                                           std::vector<double>* smoothed = nullptr,
                                                           std::vector<double>* curvature = nullptr);
};

struct DiagnosticPoint {
    int offset = 0;
    ComplexMeasurement measurement;
};

struct DiagnosticResult {
    bool complete = false;
    std::uint64_t sequence = 0;
    std::uint32_t sensor_id = 0;
    std::string error;
    std::vector<DiagnosticPoint> points;
};

DiagnosticResult acquireDiagnostics(std::uint64_t sequence, const ResonanceEstimate& resonance,
                                    ComplexMeasurementSource& source, const CancellationCheck& cancelled = {});
