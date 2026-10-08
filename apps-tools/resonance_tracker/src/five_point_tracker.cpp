#include "five_point_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace FivePointTracking {
namespace {
constexpr double kEpsilon = 1e-18;
using Complex = std::complex<double>;

bool finite(const Complex& value)
{
    return std::isfinite(value.real()) && std::isfinite(value.imag());
}

bool invert(Matrix matrix, Matrix& inverse)
{
    for (std::size_t row = 0; row < 5; ++row)
        for (std::size_t column = 0; column < 5; ++column)
            inverse[row][column] = row == column ? 1.0 : 0.0;
    for (std::size_t column = 0; column < 5; ++column) {
        std::size_t pivot = column;
        double largest = std::abs(matrix[column][column]);
        for (std::size_t row = column + 1; row < 5; ++row) {
            if (std::abs(matrix[row][column]) > largest) {
                largest = std::abs(matrix[row][column]);
                pivot = row;
            }
        }
        if (!std::isfinite(largest) || largest < kEpsilon) return false;
        if (pivot != column) {
            std::swap(matrix[column], matrix[pivot]);
            std::swap(inverse[column], inverse[pivot]);
        }
        const double divisor = matrix[column][column];
        for (std::size_t index = 0; index < 5; ++index) {
            matrix[column][index] /= divisor;
            inverse[column][index] /= divisor;
        }
        for (std::size_t row = 0; row < 5; ++row) {
            if (row == column) continue;
            const double factor = matrix[row][column];
            for (std::size_t index = 0; index < 5; ++index) {
                matrix[row][index] -= factor * matrix[column][index];
                inverse[row][index] -= factor * inverse[column][index];
            }
        }
    }
    return true;
}

std::array<double, 5> designRow(const Complex& sample, const Complex& derivative, bool imaginary)
{
    if (!imaginary) return {-derivative.real(), sample.real(), -sample.imag(), 1.0, 0.0};
    return {-derivative.imag(), sample.imag(), sample.real(), 0.0, 1.0};
}
}

Prepared prepare(const Values& reference, double spacing_hz)
{
    Prepared prepared;
    if (!std::isfinite(spacing_hz) || spacing_hz <= 0.0) return prepared;
    for (const auto& value : reference) if (!finite(value)) return prepared;
    prepared.reference = reference;
    prepared.spacing_hz = spacing_hz;
    prepared.derivative[0] = reference[1] - reference[0];
    prepared.derivative[4] = reference[4] - reference[3];
    for (std::size_t point = 1; point < 4; ++point)
        prepared.derivative[point] = 0.5 * (reference[point + 1] - reference[point - 1]);
    Matrix normal{};
    Complex mean{};
    for (std::size_t point = 0; point < 5; ++point) {
        mean += reference[point] / 5.0;
        for (bool imaginary : {false, true}) {
            const auto row = designRow(reference[point], prepared.derivative[point], imaginary);
            for (std::size_t i = 0; i < 5; ++i)
                for (std::size_t j = 0; j < 5; ++j) normal[i][j] += row[i] * row[j];
        }
    }
    if (!invert(normal, prepared.normal_inverse)) return prepared;
    for (std::size_t point = 0; point < 5; ++point) {
        prepared.template_energy += std::norm(reference[point] - mean);
        const auto real_row = designRow(reference[point], prepared.derivative[point], false);
        const auto imag_row = designRow(reference[point], prepared.derivative[point], true);
        for (std::size_t parameter = 0; parameter < 5; ++parameter) {
            Complex weight{};
            for (std::size_t j = 0; j < 5; ++j)
                weight += prepared.normal_inverse[parameter][j] * Complex(real_row[j], imag_row[j]);
            if (!finite(weight)) return Prepared{};
            prepared.parameter_weights[parameter][point] = weight;
        }
    }
    prepared.frequency_variance_per_rss = spacing_hz * spacing_hz * prepared.normal_inverse[0][0] / 5.0;
    prepared.quality = prepareTrackingQuality(spacing_hz, prepared.template_energy);
    prepared.valid = prepared.quality.valid && std::isfinite(prepared.frequency_variance_per_rss) &&
                     prepared.frequency_variance_per_rss >= 0.0;
    return prepared;
}

TrackingQualitySums evaluate(const Prepared& prepared, const Values& measured)
{
    TrackingQualitySums result;
    if (!prepared.valid) return result;
    std::array<double, 5> parameters{};
    Values errors{};
    for (std::size_t point = 0; point < 5; ++point) {
        if (!finite(measured[point])) return result;
        errors[point] = measured[point] - prepared.reference[point];
        for (std::size_t parameter = 0; parameter < 5; ++parameter) {
            const auto& weight = prepared.parameter_weights[parameter][point];
            parameters[parameter] += weight.real() * errors[point].real() + weight.imag() * errors[point].imag();
        }
    }
    const Complex gain_change(parameters[1], parameters[2]);
    const Complex offset(parameters[3], parameters[4]);
    for (std::size_t point = 0; point < 5; ++point) {
        const Complex predicted = -parameters[0] * prepared.derivative[point] +
                                  gain_change * prepared.reference[point] + offset;
        result.rss += std::norm(errors[point] - predicted);
    }
    result.requested_shift_hz = parameters[0] * prepared.spacing_hz;
    result.gain_squared = std::norm(Complex(1.0, 0.0) + gain_change);
    result.frequency_variance_hz2 = result.rss * prepared.frequency_variance_per_rss;
    result.fit_valid = std::isfinite(result.requested_shift_hz) && std::isfinite(result.rss) &&
                       std::isfinite(result.gain_squared) && std::isfinite(result.frequency_variance_hz2);
    return result;
}
}
