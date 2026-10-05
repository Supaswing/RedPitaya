#include "iq_statistics.hpp"

#include <cassert>
#include <cmath>

namespace {
bool near(double left, double right)
{
    return std::abs(left - right) < 1e-12;
}

RawIqSample sample(std::int32_t inc_i, std::int32_t inc_q, std::int32_t ref_i, std::int32_t ref_q)
{
    RawIqSample result;
    result.inc_i = inc_i;
    result.inc_q = inc_q;
    result.ref_i = ref_i;
    result.ref_q = ref_q;
    return result;
}
}

int main()
{
    RollingIqStatistics statistics(2);
    statistics.add(sample(1, 0, 2, 0));
    statistics.add(sample(1, 0, 4, 0));

    IqStatisticsSnapshot result = statistics.snapshot();
    assert(result.sample_count == 2);
    assert(result.ratio_sample_count == 2);
    assert(result.ratio_valid);
    assert(near(result.ref_i.mean, 3.0));
    assert(near(result.ref_i.sample_standard_deviation, std::sqrt(2.0)));
    assert(near(result.ratio_real_statistics.mean, 3.0));
    assert(near(result.ratio_real_statistics.sample_standard_deviation, std::sqrt(2.0)));
    assert(near(result.mean_ratio_phase_deg, 0.0));
    assert(near(result.ratio_noise.amplitude_std, std::sqrt(2.0)));
    assert(near(result.ratio_noise.radial_std, std::sqrt(2.0)));
    assert(near(result.ratio_noise.tangential_std, 0.0));
    assert(near(result.ratio_noise.phase_std, 0.0));
    assert(result.ratio_noise.direction_valid);
    assert(result.ratio_noise.phase_count == 2);

    statistics.add(sample(1, 0, 6, 0));
    result = statistics.snapshot();
    assert(result.sample_count == 2);
    assert(near(result.ref_i.mean, 5.0));

    statistics.add(sample(0, 0, 1, 1));
    result = statistics.snapshot();
    assert(result.sample_count == 2);
    assert(result.ratio_sample_count == 1);
    assert(!result.ratio_valid);
    assert(result.ratio_noise.phase_count == 1);
    assert(near(result.ratio_noise.amplitude_std, 0.0));

    statistics.reset();
    assert(statistics.snapshot().sample_count == 0);

    statistics.add(sample(0, 1, 1, 0));
    result = statistics.snapshot();
    assert(near(result.ratio_real, 0.0));
    assert(near(result.ratio_imag, -1.0));
    assert(near(result.ratio_phase_deg, -90.0));

    // Constant amplitude, angular noise straddling the +/-180 degree boundary.
    statistics.reset();
    statistics.add(sample(1, 0, -100, 1));
    statistics.add(sample(1, 0, -100, -1));
    result = statistics.snapshot();
    assert(near(result.ratio_noise.amplitude_std, 0.0));
    assert(near(result.ratio_noise.radial_std, 0.0));
    assert(near(result.ratio_noise.tangential_std, std::sqrt(2.0)));
    assert(near(result.ratio_noise.phase_std, std::sqrt(2.0) * std::atan2(1.0, 100.0) * 180.0 / std::acos(-1.0)));

    // Rotating the response rotates the axes without changing their noise.
    statistics.reset();
    statistics.add(sample(1, 0, 1, 100));
    statistics.add(sample(1, 0, -1, 100));
    result = statistics.snapshot();
    assert(near(result.ref_noise.radial_std, 0.0));
    assert(near(result.ref_noise.tangential_std, std::sqrt(2.0)));
    assert(near(result.inc_noise.amplitude_std, 0.0));

    // Zero mean: amplitude exists, but phase and projection axes do not.
    statistics.reset();
    statistics.add(sample(1, 0, 1, 0));
    statistics.add(sample(1, 0, -1, 0));
    assert(!statistics.snapshot().ratio_noise.direction_valid);
    statistics.reset();
    statistics.add(sample(1, 0, 0, 0));
    statistics.add(sample(1, 0, 2, 0));
    result = statistics.snapshot();
    assert(result.ratio_noise.phase_count == 1);
    assert(near(result.ratio_noise.amplitude_std, std::sqrt(2.0)));
    statistics.reset();
    assert(!statistics.snapshot().ratio_noise.direction_valid);
    assert(statistics.snapshot().ratio_noise.phase_count == 0);
    return 0;
}
