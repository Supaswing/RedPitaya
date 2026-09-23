#include "resonance_analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <utility>

namespace {
constexpr std::size_t kMaxCandidates = 12;
constexpr std::size_t kExtremaSearchSteps = 20;
constexpr double kMaximumFwhmFraction = 0.40;
constexpr double kRefineFwhmHalfSpan = 1.0;
constexpr double kSqrt3 = 1.7320508075688772;
constexpr double kMinimumCandidateQ = 50.0;
constexpr double kMaximumCandidateQ = 150.0;
constexpr double kMinimumRefinedModelQuality = 0.20;
constexpr std::size_t kCandidateAttemptsPerSensor = 3;
constexpr double kEpsilon = 1e-18;

using Complex = std::complex<double>;

std::uint32_t roundedFrequency(double frequency)
{
    if (frequency <= 0.0) return 0;
    if (frequency >= static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
        return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(frequency + 0.5);
}

double magnitudeSquared(const ComplexMeasurement& point)
{
    return point.real * point.real + point.imag * point.imag;
}

bool isCancelled(const CancellationCheck& cancelled)
{
    return cancelled && cancelled();
}

bool acquireAverage(ComplexMeasurementSource& source, std::uint32_t frequency_hz, std::size_t averages,
                    bool& first_point, ComplexMeasurement& result, std::string& error,
                    const CancellationCheck& cancelled)
{
    Complex sum{};
    std::uint32_t effective_frequency_hz = frequency_hz;
    if (averages == 0) {
        error = "acquisition average count is zero";
        return false;
    }
    for (std::size_t average = 0; average < averages; ++average) {
        if (isCancelled(cancelled)) {
            error = "cancelled";
            return false;
        }
        ComplexMeasurement point;
        if (!source.acquire(frequency_hz, first_point, point, error)) return false;
        first_point = false;
        effective_frequency_hz = point.effective_frequency_hz;
        sum += Complex(point.real, point.imag);
    }
    sum /= static_cast<double>(averages);
    result.requested_frequency_hz = frequency_hz;
    result.effective_frequency_hz = effective_frequency_hz;
    result.real = sum.real();
    result.imag = sum.imag();
    return true;
}

double interpolateCrossing(double x0, double y0, double x1, double y1, double level)
{
    const double denominator = y1 - y0;
    if (std::abs(denominator) < kEpsilon) return 0.5 * (x0 + x1);
    return x0 + (level - y0) * (x1 - x0) / denominator;
}

bool overlaps(const ResonanceCandidate& left, const ResonanceCandidate& right)
{
    return left.frequency_hz - 0.5 * left.fwhm_hz < right.frequency_hz + 0.5 * right.fwhm_hz &&
           right.frequency_hz - 0.5 * right.fwhm_hz < left.frequency_hz + 0.5 * left.fwhm_hz;
}

void retainCandidate(std::vector<ResonanceCandidate>& candidates, const ResonanceCandidate& candidate)
{
    if (candidates.size() < kMaxCandidates) {
        candidates.push_back(candidate);
        return;
    }
    const auto weakest = std::min_element(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return a.score < b.score;
    });
    if (weakest != candidates.end() && candidate.score > weakest->score) *weakest = candidate;
}

void selectCandidates(const std::vector<ResonanceCandidate>& source, std::size_t wanted,
                      std::vector<ResonanceCandidate>& selected)
{
    std::vector<bool> used(source.size(), false);
    while (selected.size() < wanted) {
        std::size_t best = source.size();
        bool best_model_supported = false;
        double best_area = -1.0;
        double best_quality = -1.0;
        double best_score = -1.0;
        for (std::size_t index = 0; index < source.size(); ++index) {
            if (used[index]) continue;
            bool candidate_overlaps = false;
            for (const auto& previous : selected) {
                if (overlaps(source[index], previous)) {
                    candidate_overlaps = true;
                    break;
                }
            }
            if (candidate_overlaps) {
                used[index] = true;
                continue;
            }
            const bool model_supported = source[index].selection_quality > 0.0;
            if ((model_supported && !best_model_supported) ||
                (model_supported == best_model_supported && source[index].curvature_area > best_area) ||
                (model_supported == best_model_supported && source[index].curvature_area == best_area &&
                 source[index].selection_quality > best_quality) ||
                (model_supported == best_model_supported && source[index].curvature_area == best_area &&
                 source[index].selection_quality == best_quality && source[index].score > best_score)) {
                best = index;
                best_model_supported = model_supported;
                best_area = source[index].curvature_area;
                best_quality = source[index].selection_quality;
                best_score = source[index].score;
            }
        }
        if (best == source.size()) break;
        selected.push_back(source[best]);
        used[best] = true;
    }
}

bool hasExpectedQ(const ResonanceCandidate& candidate, double minimum_q, double maximum_q)
{
    if (candidate.frequency_hz <= 0.0 || candidate.fwhm_hz <= 0.0) return false;
    const double q = candidate.frequency_hz / candidate.fwhm_hz;
    return q >= minimum_q && q <= maximum_q;
}

double smoothedAt(const std::vector<double>& smoothed, double index, std::size_t filter_radius)
{
    if (index <= static_cast<double>(filter_radius)) return smoothed[filter_radius];
    const std::size_t last = smoothed.size() - filter_radius - 1;
    if (index >= static_cast<double>(last)) return smoothed[last];
    const auto left = static_cast<std::size_t>(index);
    const double fraction = index - static_cast<double>(left);
    return smoothed[left] + fraction * (smoothed[left + 1] - smoothed[left]);
}

std::vector<ResonanceCandidate> extremaCandidates(const std::vector<double>& smoothed, double start,
                                                   double step, double maximum_fwhm, std::size_t filter_radius,
                                                   double minimum_q, double maximum_q)
{
    std::vector<ResonanceCandidate> candidates;
    const std::size_t first = filter_radius + 1;
    const std::size_t last = smoothed.size() - filter_radius - 2;
    for (std::size_t i = first; i <= last; ++i) {
        const double previous = smoothed[i - 1];
        const double center = smoothed[i];
        const double next = smoothed[i + 1];
        const bool minimum = center < previous && center <= next;
        const bool maximum = center > previous && center >= next;
        if (!minimum && !maximum) continue;
        const std::size_t search_first =
            std::max(filter_radius, i > kExtremaSearchSteps ? i - kExtremaSearchSteps : 0U);
        const std::size_t search_last = std::min(smoothed.size() - filter_radius - 1, i + kExtremaSearchSteps);
        double left_background = center;
        double right_background = center;
        for (std::size_t j = search_first; j < i; ++j)
            if ((minimum && smoothed[j] > left_background) || (maximum && smoothed[j] < left_background))
                left_background = smoothed[j];
        for (std::size_t j = i + 1; j <= search_last; ++j)
            if ((minimum && smoothed[j] > right_background) || (maximum && smoothed[j] < right_background))
                right_background = smoothed[j];
        const double background = 0.5 * (left_background + right_background);
        const double prominence = std::abs(center - background);
        if (prominence <= kEpsilon) continue;
        const double half_level = 0.5 * (center + background);
        double left_index = 0.0;
        double right_index = 0.0;
        bool left_found = false;
        bool right_found = false;
        for (std::size_t j = i; j > search_first; --j) {
            const double outer = smoothed[j - 1];
            const double inner = smoothed[j];
            if ((minimum && outer >= half_level && inner < half_level) ||
                (maximum && outer <= half_level && inner > half_level)) {
                left_index = interpolateCrossing(j - 1, outer, j, inner, half_level);
                left_found = true;
                break;
            }
        }
        for (std::size_t j = i; j < search_last; ++j) {
            const double inner = smoothed[j];
            const double outer = smoothed[j + 1];
            if ((minimum && inner < half_level && outer >= half_level) ||
                (maximum && inner > half_level && outer <= half_level)) {
                right_index = interpolateCrossing(j, inner, j + 1, outer, half_level);
                right_found = true;
                break;
            }
        }
        if (!left_found || !right_found || right_index <= left_index) continue;
        double center_index = static_cast<double>(i);
        const double denominator = previous - 2.0 * center + next;
        if (std::abs(denominator) > kEpsilon)
            center_index += std::clamp(0.5 * (previous - next) / denominator, -1.0, 1.0);
        ResonanceCandidate candidate;
        candidate.left_frequency_hz = start + left_index * step;
        candidate.right_frequency_hz = start + right_index * step;
        candidate.frequency_hz = start + center_index * step;
        candidate.fwhm_hz = candidate.right_frequency_hz - candidate.left_frequency_hz;
        candidate.score = prominence;
        if (candidate.fwhm_hz <= maximum_fwhm && hasExpectedQ(candidate, minimum_q, maximum_q))
            retainCandidate(candidates, candidate);
    }
    return candidates;
}

double removeQuadraticBackground(const std::vector<Complex>& data, std::vector<Complex>& residual)
{
    const std::size_t count = data.size();
    Complex mean{};
    Complex linear{};
    Complex quadratic{};
    double sum_x2 = 0.0;
    double sum_q2 = 0.0;
    double mean_x2 = 0.0;
    for (std::size_t point = 0; point < count; ++point) {
        const double x = (static_cast<double>(point) - 0.5 * (count - 1)) / (0.5 * (count - 1));
        mean += data[point];
        mean_x2 += x * x;
        sum_x2 += x * x;
    }
    mean /= static_cast<double>(count);
    mean_x2 /= static_cast<double>(count);
    for (std::size_t point = 0; point < count; ++point) {
        const double x = (static_cast<double>(point) - 0.5 * (count - 1)) / (0.5 * (count - 1));
        const double q = x * x - mean_x2;
        linear += x * data[point];
        quadratic += q * data[point];
        sum_q2 += q * q;
    }
    linear /= sum_x2;
    quadratic /= sum_q2;
    residual.resize(count);
    double energy = 0.0;
    for (std::size_t point = 0; point < count; ++point) {
        const double x = (static_cast<double>(point) - 0.5 * (count - 1)) / (0.5 * (count - 1));
        const double q = x * x - mean_x2;
        residual[point] = data[point] - mean - linear * x - quadratic * q;
        energy += std::norm(residual[point]);
    }
    return energy;
}

double resonanceScore(const std::vector<Complex>& residual, double start, double step, double f0, double hwhm,
                      int orientation)
{
    if (hwhm <= 0.0) return -1.0;
    const std::size_t count = residual.size();
    std::vector<Complex> basis(count);
    for (std::size_t point = 0; point < count; ++point) {
        const double frequency = start + point * step;
        const double z = orientation * (frequency - f0) / hwhm;
        basis[point] = Complex(1.0, -z) / (1.0 + z * z);
    }
    std::vector<Complex> projected;
    removeQuadraticBackground(basis, projected);
    Complex inner{};
    double denominator = 0.0;
    for (std::size_t point = 0; point < count; ++point) {
        inner += std::conj(projected[point]) * residual[point];
        denominator += std::norm(projected[point]);
    }
    if (denominator < kEpsilon) return -1.0;
    return std::norm(inner) / denominator;
}

bool fitComplexModel(const std::vector<ComplexMeasurement>& points, const std::vector<std::vector<Complex>>& replicates,
                     ResonanceEstimate& estimate)
{
    static const std::array<double, 13> width_multipliers =
        {0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0, 24.0, 32.0};
    static const std::array<double, 7> refine_multipliers = {0.65, 0.8, 0.9, 1.0, 1.1, 1.25, 1.5};
    if (points.size() < 5) return false;
    const double start = points.front().requested_frequency_hz;
    const double stop = points.back().requested_frequency_hz;
    const double step = (stop - start) / (points.size() - 1);
    std::vector<Complex> data;
    data.reserve(points.size());
    for (const auto& point : points) data.emplace_back(point.real, point.imag);
    std::vector<Complex> residual;
    const double base_energy = removeQuadraticBackground(data, residual);
    if (base_energy < kEpsilon) return false;

    double best_score = -1.0;
    double best_f0 = estimate.frequency_hz;
    double best_hwhm = 0.5 * estimate.fwhm_hz;
    int best_orientation = 1;
    for (int orientation : {-1, 1}) {
        for (std::size_t index = 2; index + 2 < points.size(); ++index) {
            const double f0 = start + index * step;
            for (double multiplier : width_multipliers) {
                const double hwhm = step * multiplier;
                const double score = resonanceScore(residual, start, step, f0, hwhm, orientation);
                if (score > best_score) {
                    best_score = score;
                    best_f0 = f0;
                    best_hwhm = hwhm;
                    best_orientation = orientation;
                }
            }
        }
    }
    for (std::size_t pass = 0; pass < 3; ++pass) {
        const double half_range = step / static_cast<double>(1U << (2U * pass));
        const double seed_f0 = best_f0;
        const double seed_hwhm = best_hwhm;
        for (std::size_t f_index = 0; f_index <= 16; ++f_index) {
            const double f0 = seed_f0 + (static_cast<double>(f_index) - 8.0) * half_range / 8.0;
            if (f0 < start + 2.0 * step || f0 > start + (points.size() - 3) * step) continue;
            for (double multiplier : refine_multipliers) {
                const double hwhm = seed_hwhm * multiplier;
                const double score = resonanceScore(residual, start, step, f0, hwhm, best_orientation);
                if (score > best_score) {
                    best_score = score;
                    best_f0 = f0;
                    best_hwhm = hwhm;
                }
            }
        }
    }
    if (best_score <= kEpsilon || best_hwhm <= 0.0) return false;
    estimate.frequency_hz = best_f0;
    estimate.fwhm_hz = 2.0 * best_hwhm;
    estimate.q = best_f0 / estimate.fwhm_hz;
    estimate.spacing_hz = std::max(0.5 * best_hwhm, step);
    estimate.model_explained_fraction = best_score / base_energy;
    estimate.complex_model_valid = true;

    std::vector<Complex> basis(points.size());
    for (std::size_t point = 0; point < points.size(); ++point) {
        const double frequency = start + point * step;
        const double z = best_orientation * (frequency - best_f0) / best_hwhm;
        basis[point] = Complex(1.0, -z) / (1.0 + z * z);
    }
    std::vector<Complex> projected_basis;
    removeQuadraticBackground(basis, projected_basis);
    Complex inner{};
    double basis_energy = 0.0;
    for (std::size_t point = 0; point < points.size(); ++point) {
        inner += std::conj(projected_basis[point]) * residual[point];
        basis_energy += std::norm(projected_basis[point]);
    }
    if (basis_energy >= kEpsilon) {
        const Complex amplitude = inner / basis_energy;
        estimate.model.reserve(points.size());
        for (std::size_t point = 0; point < points.size(); ++point) {
            const Complex fitted = data[point] - residual[point] + amplitude * projected_basis[point];
            estimate.model.push_back({points[point].requested_frequency_hz, points[point].effective_frequency_hz,
                                      fitted.real(), fitted.imag()});
        }
    }

    if (replicates.size() >= 2) {
        std::vector<double> centers;
        for (const auto& replicate : replicates) {
            std::vector<Complex> replicate_residual;
            if (replicate.size() != points.size() || removeQuadraticBackground(replicate, replicate_residual) < kEpsilon)
                continue;
            double replicate_score = -1.0;
            double replicate_f0 = best_f0;
            for (std::size_t index = 0; index <= 16; ++index) {
                const double f0 = best_f0 + (static_cast<double>(index) - 8.0) * (2.0 * step) / 8.0;
                const double score = resonanceScore(replicate_residual, start, step, f0, best_hwhm, best_orientation);
                if (score > replicate_score) {
                    replicate_score = score;
                    replicate_f0 = f0;
                }
            }
            centers.push_back(replicate_f0);
        }
        if (centers.size() >= 2) {
            double mean = 0.0;
            for (double center : centers) mean += center / centers.size();
            double squared_error = 0.0;
            for (double center : centers) squared_error += (center - mean) * (center - mean);
            estimate.frequency_se_hz = std::sqrt(squared_error / (centers.size() * (centers.size() - 1)));
        }
    }
    estimate.valid = true;
    return true;
}

double coarseModelQuality(const std::vector<ComplexMeasurement>& overview, const ResonanceCandidate& candidate)
{
    if (overview.size() < 9 || candidate.fwhm_hz <= 0.0) return 0.0;
    const double step = static_cast<double>(overview.back().requested_frequency_hz -
                                            overview.front().requested_frequency_hz) /
                        static_cast<double>(overview.size() - 1);
    const double half_span = std::max(1.5 * candidate.fwhm_hz, 6.0 * step);
    const double lower = candidate.frequency_hz - half_span;
    const double upper = candidate.frequency_hz + half_span;
    std::vector<ComplexMeasurement> local;
    for (const auto& point : overview) {
        if (point.requested_frequency_hz >= lower && point.requested_frequency_hz <= upper)
            local.push_back(point);
    }
    if (local.size() < 13) return 0.0;
    ResonanceEstimate estimate;
    estimate.frequency_hz = candidate.frequency_hz;
    estimate.fwhm_hz = candidate.fwhm_hz;
    if (!fitComplexModel(local, {}, estimate) || !estimate.complex_model_valid) return 0.0;
    if (estimate.frequency_hz < candidate.left_frequency_hz ||
        estimate.frequency_hz > candidate.right_frequency_hz)
        return 0.0;
    const double center_displacement = std::abs(estimate.frequency_hz - candidate.frequency_hz) /
                                       candidate.fwhm_hz;
    return std::clamp(estimate.model_explained_fraction, 0.0, 1.0) /
           (1.0 + center_displacement * center_displacement);
}

double complexCurvature(const std::vector<Complex>& data, std::size_t index)
{
    return std::norm(data[index - 1] - 2.0 * data[index] + data[index + 1]);
}

void fitFivePointQuadratic(const std::array<double, 5>& values, double& a, double& b, double& c)
{
    const double sum0 = values[0] + values[1] + values[2] + values[3] + values[4];
    const double sum2 = 4.0 * values[0] + values[1] + values[3] + 4.0 * values[4];
    a = (5.0 * sum2 - 10.0 * sum0) / 70.0;
    b = (-2.0 * values[0] - values[1] + values[3] + 2.0 * values[4]) / 10.0;
    c = (34.0 * sum0 - 10.0 * sum2) / 70.0;
}

bool fitCurvatureFallback(const std::vector<ComplexMeasurement>& points,
                          const std::vector<std::vector<Complex>>& replicates, ResonanceEstimate& estimate)
{
    if (points.size() < 9) return false;
    const double start = points.front().requested_frequency_hz;
    const double step = (points.back().requested_frequency_hz - start) / (points.size() - 1);
    std::vector<Complex> data;
    data.reserve(points.size());
    for (const auto& point : points) data.emplace_back(point.real, point.imag);
    std::vector<double> feature(points.size(), 0.0);
    for (std::size_t point = 1; point + 1 < points.size(); ++point)
        feature[point] = complexCurvature(data, point);
    std::size_t peak_index = 3;
    for (std::size_t point = 4; point + 3 < points.size(); ++point)
        if (feature[point] > feature[peak_index]) peak_index = point;
    std::array<double, 5> fit_values{};
    for (std::size_t point = 0; point < fit_values.size(); ++point)
        fit_values[point] = feature[peak_index + point - 2];
    double a = 0.0, b = 0.0, c = 0.0;
    fitFivePointQuadratic(fit_values, a, b, c);
    if (a >= -kEpsilon) return false;
    const double vertex = std::clamp(-b / (2.0 * a), -1.0, 1.0);
    estimate.frequency_hz = start + (peak_index + vertex) * step;
    const double peak = c - b * b / (4.0 * a);

    double left_baseline = feature[1];
    for (std::size_t point = 2; point < peak_index; ++point)
        left_baseline = std::min(left_baseline, feature[point]);
    double right_baseline = feature[peak_index + 1];
    for (std::size_t point = peak_index + 1; point + 1 < points.size(); ++point)
        right_baseline = std::min(right_baseline, feature[point]);
    const double half_level = 0.5 * (peak + 0.5 * (left_baseline + right_baseline));
    double left_crossing = 0.0, right_crossing = 0.0;
    bool left_found = false, right_found = false;
    for (std::size_t point = peak_index; point > 0; --point) {
        if (feature[point - 1] <= half_level) {
            left_crossing = interpolateCrossing(start + (point - 1) * step, feature[point - 1],
                                                start + point * step, feature[point], half_level);
            left_found = true;
            break;
        }
    }
    for (std::size_t point = peak_index; point + 1 < points.size(); ++point) {
        if (feature[point + 1] <= half_level) {
            right_crossing = interpolateCrossing(start + point * step, feature[point],
                                                 start + (point + 1) * step, feature[point + 1], half_level);
            right_found = true;
            break;
        }
    }
    if (left_found && right_found && right_crossing > left_crossing)
        estimate.fwhm_hz = kSqrt3 * (right_crossing - left_crossing);
    if (estimate.fwhm_hz <= 0.0) return false;
    estimate.q = estimate.frequency_hz / estimate.fwhm_hz;
    estimate.spacing_hz = std::max(0.25 * estimate.fwhm_hz, step);
    estimate.model_explained_fraction = 0.0;
    estimate.complex_model_valid = false;

    std::vector<double> vertices;
    for (const auto& replicate : replicates) {
        if (replicate.size() != points.size()) continue;
        for (std::size_t point = 0; point < fit_values.size(); ++point)
            fit_values[point] = complexCurvature(replicate, peak_index + point - 2);
        fitFivePointQuadratic(fit_values, a, b, c);
        if (a >= -kEpsilon) continue;
        const double replicate_vertex = -b / (2.0 * a);
        if (replicate_vertex >= -1.0 && replicate_vertex <= 1.0) vertices.push_back(replicate_vertex);
    }
    if (vertices.size() >= 2) {
        double mean = 0.0;
        for (double value : vertices) mean += value / vertices.size();
        double squared_error = 0.0;
        for (double value : vertices) squared_error += (value - mean) * (value - mean);
        estimate.frequency_se_hz = step * std::sqrt(squared_error / (vertices.size() * (vertices.size() - 1)));
    }
    estimate.valid = true;
    return true;
}
}

ReplayMeasurementSource::ReplayMeasurementSource(std::vector<ComplexMeasurement> points) : points_(std::move(points))
{
    std::sort(points_.begin(), points_.end(), [](const auto& left, const auto& right) {
        return left.effective_frequency_hz < right.effective_frequency_hz;
    });
}

bool ReplayMeasurementSource::acquire(std::uint32_t frequency_hz, bool, ComplexMeasurement& measurement,
                                      std::string& error)
{
    if (points_.empty() || frequency_hz < points_.front().effective_frequency_hz ||
        frequency_hz > points_.back().effective_frequency_hz) {
        error = "replay frequency outside captured range";
        return false;
    }
    const auto right = std::lower_bound(points_.begin(), points_.end(), frequency_hz, [](const auto& point, auto value) {
        return point.effective_frequency_hz < value;
    });
    if (right == points_.begin() || right == points_.end() || right->effective_frequency_hz == frequency_hz) {
        measurement = right == points_.end() ? points_.back() : *right;
    } else {
        const auto left = right - 1;
        const double fraction = static_cast<double>(frequency_hz - left->effective_frequency_hz) /
                                static_cast<double>(right->effective_frequency_hz - left->effective_frequency_hz);
        measurement.real = left->real + fraction * (right->real - left->real);
        measurement.imag = left->imag + fraction * (right->imag - left->imag);
        measurement.effective_frequency_hz = frequency_hz;
    }
    measurement.requested_frequency_hz = frequency_hz;
    return true;
}

std::vector<ResonanceCandidate> BaselineAnalyzer::findCandidates(const std::vector<ComplexMeasurement>& overview,
                                                                 std::size_t wanted,
                                                                 std::size_t filter_radius,
                                                                 double minimum_q,
                                                                 double maximum_q,
                                                                 std::vector<double>* smoothed_output,
                                                                 std::vector<double>* curvature_output)
{
    std::vector<ResonanceCandidate> selected;
    const std::size_t curvature_edge = filter_radius + 1;
    if (filter_radius == 0 || overview.size() < 2 * curvature_edge + 3 || wanted == 0 ||
        minimum_q <= 0.0 || maximum_q < minimum_q)
        return selected;
    std::vector<double> magnitude(overview.size());
    std::vector<double> smoothed(overview.size(), 0.0);
    std::vector<double> curvature(overview.size(), 0.0);
    for (std::size_t i = 0; i < overview.size(); ++i) magnitude[i] = magnitudeSquared(overview[i]);
    // Center coefficient of a least-squares quadratic fit over 2*radius+1
    // equally spaced samples. The symmetric kernel has zero group delay.
    double sum_x2 = 0.0;
    double sum_x4 = 0.0;
    for (std::size_t offset = 1; offset <= filter_radius; ++offset) {
        const double x2 = static_cast<double>(offset * offset);
        sum_x2 += 2.0 * x2;
        sum_x4 += 2.0 * x2 * x2;
    }
    const double count = static_cast<double>(2 * filter_radius + 1);
    const double denominator = count * sum_x4 - sum_x2 * sum_x2;
    if (denominator <= kEpsilon) return selected;
    for (std::size_t i = filter_radius; i + filter_radius < overview.size(); ++i) {
        for (std::ptrdiff_t offset = -static_cast<std::ptrdiff_t>(filter_radius);
             offset <= static_cast<std::ptrdiff_t>(filter_radius); ++offset) {
            const double x2 = static_cast<double>(offset * offset);
            const double coefficient = (sum_x4 - sum_x2 * x2) / denominator;
            smoothed[i] += coefficient * magnitude[static_cast<std::size_t>(static_cast<std::ptrdiff_t>(i) + offset)];
        }
    }
    for (std::size_t i = curvature_edge; i + curvature_edge < overview.size(); ++i)
        curvature[i] = smoothed[i - 1] - 2.0 * smoothed[i] + smoothed[i + 1];
    if (smoothed_output) *smoothed_output = smoothed;
    if (curvature_output) *curvature_output = curvature;

    const double start = overview.front().requested_frequency_hz;
    const double stop = overview.back().requested_frequency_hz;
    const double step = (stop - start) / (overview.size() - 1);
    const double maximum_fwhm = kMaximumFwhmFraction * (stop - start);
    std::vector<ResonanceCandidate> pairs;
    // A resonance can appear as either a dip or a peak after an arbitrary
    // coherent complex background is projected onto |R|^2. Build hypotheses
    // from both positive and negative signed-curvature lobes. In each pass the
    // selected polarity is transformed to a positive lobe so scoring remains
    // identical and its immediately adjacent opposite-polarity area provides
    // the balance check.
    for (double polarity : {1.0, -1.0}) {
        bool inside = false;
        std::size_t left_sample_index = curvature_edge;
        double left_index = 0.0;
        double integrated_score = 0.0;
        for (std::size_t i = curvature_edge; i + curvature_edge + 1 < overview.size(); ++i) {
            const double left = polarity * curvature[i];
            const double right = polarity * curvature[i + 1];
            if (!inside && left <= 0.0 && right > 0.0) {
                left_index = i - left / (right - left);
                left_sample_index = i;
                integrated_score = right;
                inside = true;
            } else if (inside) {
                if (right > 0.0) integrated_score += right;
                if (left > 0.0 && right <= 0.0) {
                    const double right_index = i - left / (right - left);
                    ResonanceCandidate candidate;
                    candidate.from_inflection_pair = true;
                    candidate.left_frequency_hz = start + left_index * step;
                    candidate.right_frequency_hz = start + right_index * step;
                    candidate.frequency_hz = start + 0.5 * (left_index + right_index) * step;
                    candidate.fwhm_hz = kSqrt3 * (candidate.right_frequency_hz - candidate.left_frequency_hz);
                    double left_opposite_area = 0.0;
                    for (std::size_t point = left_sample_index; point >= curvature_edge; --point) {
                        const double value = polarity * curvature[point];
                        if (value > 0.0) break;
                        left_opposite_area -= value;
                        if (point == curvature_edge) break;
                    }
                    double right_opposite_area = 0.0;
                    for (std::size_t point = i + 1; point + curvature_edge < overview.size(); ++point) {
                        const double value = polarity * curvature[point];
                        if (value > 0.0) break;
                        right_opposite_area -= value;
                    }
                    const double surrounding_opposite_area = left_opposite_area + right_opposite_area;
                    candidate.curvature_area = 2.0 * std::min(integrated_score, surrounding_opposite_area);
                    candidate.score =
                        std::abs(smoothedAt(smoothed, 0.5 * (left_index + right_index), filter_radius) -
                                 0.5 * (smoothedAt(smoothed, left_index, filter_radius) +
                                        smoothedAt(smoothed, right_index, filter_radius))) +
                        integrated_score;
                    if (candidate.fwhm_hz <= maximum_fwhm && candidate.score > 0.0 &&
                        hasExpectedQ(candidate, minimum_q, maximum_q))
                        retainCandidate(pairs, candidate);
                    inside = false;
                }
            }
        }
    }
    const auto extrema =
        extremaCandidates(smoothed, start, step, maximum_fwhm, filter_radius, minimum_q, maximum_q);
    std::vector<ResonanceCandidate> hypotheses = pairs;
    for (const auto& candidate : extrema) retainCandidate(hypotheses, candidate);
    for (auto& candidate : hypotheses) candidate.selection_quality = coarseModelQuality(overview, candidate);
    selectCandidates(hypotheses, wanted, selected);
    return selected;
}

BaselineResult BaselineAnalyzer::acquire(std::uint64_t sequence, const BaselineConfig& config,
                                         ComplexMeasurementSource& source, const BaselineProgress& progress,
                                         const CancellationCheck& cancelled) const
{
    BaselineResult result;
    result.sequence = sequence;
    result.config = config;
    if (config.filter_radius == 0 || config.overview_points < 2 * config.filter_radius + 5) {
        result.error = "filter radius requires at least 2r+5 overview points";
        return result;
    }
    if (config.start_frequency_hz >= config.stop_frequency_hz || config.overview_points < 15 ||
        config.coarse_averages == 0 || config.refine_points < 5 || config.refine_averages == 0 ||
        config.sensor_count == 0 || config.sensor_count > 2) {
        result.error = "invalid baseline configuration";
        return result;
    }
    bool first_point = true;
    const double coarse_step = static_cast<double>(config.stop_frequency_hz - config.start_frequency_hz) /
                               static_cast<double>(config.overview_points - 1);
    for (std::size_t index = 0; index < config.overview_points; ++index) {
        ComplexMeasurement point;
        if (!acquireAverage(source, roundedFrequency(config.start_frequency_hz + index * coarse_step),
                            config.coarse_averages, first_point, point, result.error, cancelled))
            return result;
        result.overview.push_back(point);
        if (progress) progress(BaselineStage::Overview, index + 1, config.overview_points);
    }
    if (progress) progress(BaselineStage::Finding, 0, config.sensor_count);
    const std::size_t candidate_limit = kCandidateAttemptsPerSensor * config.sensor_count;
    const auto candidate_pool = findCandidates(result.overview, candidate_limit, config.filter_radius,
                                               kMinimumCandidateQ, kMaximumCandidateQ,
                                               &result.smoothed_magnitude_squared, &result.signed_curvature);
    if (candidate_pool.size() < config.sensor_count) {
        result.complete = true;
        result.error = "required coarse resonance candidates not found";
        return result;
    }

    std::size_t attempt = 0;
    for (const auto& candidate : candidate_pool) {
        if (result.resonances.size() == config.sensor_count || attempt == candidate_limit) break;
        ++attempt;
        ResonanceEstimate estimate;
        estimate.candidate = candidate;
        estimate.frequency_hz = candidate.frequency_hz;
        estimate.fwhm_hz = candidate.fwhm_hz;
        estimate.q = candidate.frequency_hz / candidate.fwhm_hz;
        double half_span = std::max(3.0 * coarse_step, kRefineFwhmHalfSpan * candidate.fwhm_hz);
        double refine_start = std::max(static_cast<double>(config.start_frequency_hz), candidate.frequency_hz - half_span);
        double refine_stop = std::min(static_cast<double>(config.stop_frequency_hz), candidate.frequency_hz + half_span);
        if (refine_stop - refine_start < 4.0 * coarse_step) continue;
        const double refine_step = (refine_stop - refine_start) / (config.refine_points - 1);
        std::vector<std::vector<Complex>> replicates(config.refine_averages,
                                                      std::vector<Complex>(config.refine_points));
        for (std::size_t point_index = 0; point_index < config.refine_points; ++point_index) {
            Complex sum{};
            const auto frequency = roundedFrequency(refine_start + point_index * refine_step);
            std::uint32_t effective_frequency = frequency;
            for (std::size_t average = 0; average < config.refine_averages; ++average) {
                if (isCancelled(cancelled)) {
                    result.error = "cancelled";
                    return result;
                }
                ComplexMeasurement point;
                if (!source.acquire(frequency, first_point, point, result.error)) return result;
                first_point = false;
                effective_frequency = point.effective_frequency_hz;
                replicates[average][point_index] = Complex(point.real, point.imag);
                sum += replicates[average][point_index];
            }
            sum /= static_cast<double>(config.refine_averages);
            estimate.refinement.push_back({frequency, effective_frequency, sum.real(), sum.imag()});
            if (progress) progress(BaselineStage::Refining,
                                   (attempt - 1) * config.refine_points + point_index + 1,
                                   candidate_limit * config.refine_points);
        }
        if (!fitComplexModel(estimate.refinement, replicates, estimate) ||
            !estimate.complex_model_valid || estimate.model_explained_fraction < kMinimumRefinedModelQuality)
            continue;
        result.resonances.push_back(std::move(estimate));
    }
    if (result.resonances.size() != config.sensor_count) {
        result.complete = true;
        result.error = "no candidate passed the refined model quality threshold";
        return result;
    }

    std::sort(result.resonances.begin(), result.resonances.end(), [](const auto& left, const auto& right) {
        return left.frequency_hz < right.frequency_hz;
    });
    result.candidates.clear();
    for (std::size_t sensor = 0; sensor < result.resonances.size(); ++sensor) {
        ResonanceEstimate& estimate = result.resonances[sensor];
        estimate.sensor_id = static_cast<std::uint32_t>(sensor + 1);
        result.candidates.push_back(estimate.candidate);
        for (int offset = -2; offset <= 2; ++offset) {
            ComplexMeasurement point;
            const auto frequency = roundedFrequency(estimate.frequency_hz + offset * estimate.spacing_hz);
            if (!source.acquire(frequency, first_point, point, result.error)) return result;
            first_point = false;
            estimate.template_points.push_back(point);
            if (progress) progress(BaselineStage::Template,
                                   sensor * 5 + static_cast<std::size_t>(offset + 3), config.sensor_count * 5);
        }
    }
    result.complete = true;
    result.valid = true;
    return result;
}

DiagnosticResult acquireDiagnostics(std::uint64_t sequence, const ResonanceEstimate& resonance,
                                    ComplexMeasurementSource& source, const CancellationCheck& cancelled)
{
    DiagnosticResult result;
    result.sequence = sequence;
    result.sensor_id = resonance.sensor_id;
    if (!resonance.valid || resonance.spacing_hz <= 0.0) {
        result.error = "diagnostics requires a valid selected resonance";
        return result;
    }
    bool first_point = true;
    for (int offset = -2; offset <= 2; ++offset) {
        if (isCancelled(cancelled)) {
            result.error = "cancelled";
            return result;
        }
        DiagnosticPoint point;
        point.offset = offset;
        const auto frequency = roundedFrequency(resonance.frequency_hz + offset * resonance.spacing_hz);
        if (!source.acquire(frequency, first_point, point.measurement, result.error)) return result;
        first_point = false;
        result.points.push_back(point);
    }
    result.complete = true;
    return result;
}
