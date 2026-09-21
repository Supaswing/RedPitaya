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

    statistics.add(sample(1, 0, 6, 0));
    result = statistics.snapshot();
    assert(result.sample_count == 2);
    assert(near(result.ref_i.mean, 5.0));

    statistics.add(sample(0, 0, 1, 1));
    result = statistics.snapshot();
    assert(result.sample_count == 2);
    assert(result.ratio_sample_count == 1);
    assert(!result.ratio_valid);

    statistics.reset();
    assert(statistics.snapshot().sample_count == 0);

    statistics.add(sample(0, 1, 1, 0));
    result = statistics.snapshot();
    assert(near(result.ratio_real, 0.0));
    assert(near(result.ratio_imag, -1.0));
    assert(near(result.ratio_phase_deg, -90.0));
    return 0;
}
