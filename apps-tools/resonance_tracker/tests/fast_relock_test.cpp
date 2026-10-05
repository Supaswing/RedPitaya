#include "fast_relock.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main()
{
    using Complex = std::complex<double>;
    FastRelock::Input input{};
    FastRelock::Limits limits;
    limits.maximum_shift_hz = 60000;
    limits.maximum_se_hz = 24000;
    for (double shift : {-30000.0, 0.0, 30000.0}) {
        for (std::size_t j = 0; j < 6; ++j) {
            const double z = FastRelock::kOffsetsHz[j] / 120000.0;
            const Complex denominator(1, z);
            input.reference[j] = Complex(1, 0.1) + Complex(-0.6, 0.18) / denominator;
            input.derivative_per_hz[j] = Complex(-0.6, 0.18) * Complex(0, -1.0 / 120000.0) /
                                        (denominator * denominator);
            input.measured[j] = input.reference[j] - shift * input.derivative_per_hz[j] +
                                Complex(0.05, 0.02) * input.reference[j] + Complex(0.2, -0.1);
        }
        const auto result = FastRelock::estimate(input, limits);
        assert(result.accepted);
        assert(std::abs(result.shift_hz - shift) < 1e-6);
        assert(result.se_hz < 1e-6);
    }
    auto invalid = input;
    invalid.measured[2] = Complex(std::numeric_limits<double>::quiet_NaN(), 0);
    assert(!FastRelock::estimate(invalid, limits).valid);
    invalid = input;
    invalid.derivative_per_hz.fill(Complex{});
    assert(!FastRelock::estimate(invalid, limits).valid);
    invalid = input;
    invalid.reference.fill(Complex(1, 0));
    assert(!FastRelock::estimate(invalid, limits).valid);
    invalid = input;
    invalid.derivative_per_hz = invalid.reference; // Gain and shift are indistinguishable.
    assert(!FastRelock::estimate(invalid, limits).valid);
    invalid = input;
    invalid.measured.fill(Complex{});
    assert(!FastRelock::estimate(invalid, limits).accepted);
    invalid = input;
    for (std::size_t j = 0; j < 6; ++j)
        invalid.measured[j] = invalid.reference[j] - 100000.0 * invalid.derivative_per_hz[j];
    assert(!FastRelock::estimate(invalid, limits).accepted);
    invalid = input;
    invalid.measured[0] += Complex(2, -3);
    assert(!FastRelock::estimate(invalid, limits).accepted);
    // Isolate the uncertainty gate from shift and residual acceptance.
    auto uncertain_limits = limits;
    uncertain_limits.maximum_shift_hz = 1e9;
    uncertain_limits.maximum_residual = 1e9;
    uncertain_limits.minimum_gain = 0;
    uncertain_limits.maximum_se_hz = 1e-6;
    const auto uncertain = FastRelock::estimate(invalid, uncertain_limits);
    assert(uncertain.valid && uncertain.se_hz > uncertain_limits.maximum_se_hz && !uncertain.accepted);
    limits.maximum_se_hz = 0;
    assert(!FastRelock::estimate(input, limits).valid);
}
