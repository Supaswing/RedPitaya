# Raw-IQ noise experiment — 2026-09-23

The target run completed successfully with 256 estimates per configuration and
6,656 underlying acquisitions. The two DDS phase increments were 1,030,792,151
and 1,030,792,152, corresponding to 29,999,999.9988358 Hz and
30,000,000.0279397 Hz (a 0.0291039 Hz step).

## Native-window reflection-ratio noise

The following values average the standard deviations from the two DDS settings.

| WINDOW_SHIFT | Samples | R magnitude mean | R magnitude SD | Relative SD |
| ---: | ---: | ---: | ---: | ---: |
| 16 | 65,536 | 2.512886 | 0.004153 | 0.1653% |
| 17 | 131,072 | 2.512811 | 0.004009 | 0.1595% |
| 18 | 262,144 | 2.513136 | 0.003511 | 0.1397% |
| 19 | 524,288 | 2.513452 | 0.002499 | 0.0994% |
| 20 | 1,048,576 | 2.513280 | 0.000805 | 0.0320% |

Noise does not follow the `1/sqrt(samples)` white-noise law from shifts 16
through 19. Relative to that extrapolation from shift 16, the measured R
magnitude noise is 1.36x, 1.69x, and 1.70x higher at shifts 17, 18, and 19.
This indicates a correlated/deterministic noise floor over the shorter windows.
Shift 20 is markedly better and is 0.78x the simple shift-16 white-noise
extrapolation. A repeat with randomized window order is required to distinguish
a genuine integration-length effect from run-order/non-stationary behavior.

Linear detrending within each 256-estimate block changes the standard deviations
negligibly, so a simple slow linear drift does not explain the result.

## Two shorter coherent averages versus one longer window

Ratios below are `SD(average of two lower-shift I/Q acquisitions) /
SD(one upper-shift acquisition)`, averaged across the two DDS settings. One
means equivalent noise; less than one favors two-acquisition averaging.

| Comparison | R real | R imaginary | R magnitude |
| --- | ---: | ---: | ---: |
| 2x16 vs 17 | 0.965 | 0.948 | 0.940 |
| 2x17 vs 18 | 0.950 | 0.952 | 0.956 |
| 2x18 vs 19 | 0.923 | 0.923 | 0.920 |
| 2x19 vs 20 | 0.752 | 0.794 | 0.837 |

Coherent CPU averaging of two shorter acquisitions is at least as good as one
doubled FPGA window for the reflection ratio in this run. The first three pairs
are close (within about 5-8% for R magnitude); 2x19 is about 16% quieter than
shift 20. This does not make the methods operationally identical: two
acquisitions incur two restarts, settling intervals, and ready waits.

Raw incident I/Q comparison ratios are generally 0.91-1.07. Raw reference I/Q
has excess variation for 2x18 versus 19 (ratios 1.69 and 1.90), while the complex
ratio remains near 0.92. The ratio is therefore rejecting substantial
common/correlated channel variation that is visible in the raw components.

### Reconstructed 16x shift-16 versus native shift-20

The 512 raw shift-16 acquisitions per DDS setting from the `average2` block can
be regrouped into 32 independent coherent 16-acquisition estimates. Their
standard-deviation ratios relative to native shift 20 are:

| Metric | DDS A | DDS B | Mean ratio |
| --- | ---: | ---: | ---: |
| incident I | 0.905 | 0.908 | 0.907 |
| incident Q | 1.010 | 1.003 | 1.006 |
| reference I | 1.350 | 1.338 | 1.344 |
| reference Q | 1.445 | 1.526 | 1.486 |
| R real | 0.697 | 0.855 | 0.776 |
| R imaginary | 0.682 | 0.793 | 0.737 |
| R magnitude | 0.859 | 0.912 | 0.885 |

Thus 16x shift-16 was approximately 11.5% quieter in R magnitude than one
shift-20 acquisition in this dataset. Only 32 averaged estimates are available,
so this advantage is suggestive rather than statistically decisive. The raw
reference components are noisier with repeated short acquisitions even though
their correlated contribution largely cancels in R.

Using complex vector noise `sqrt(SD(I)^2 + SD(Q)^2)`, incident and reference
have nearly equal absolute noise when averaged across all native window shifts
(incident/reference ratio 1.00). Since the reference signal magnitude is about
2.51 times larger, incident relative noise is about 2.50 times reference
relative noise on average. For the reconstructed 16x shift-16 result alone,
incident relative noise is 1.29 times reference; at native shift 20 it is 1.95
times reference.

## One-DDS-increment result

The original run always acquired the base increment first and `+1` second.
It reports positive R-magnitude differences of approximately 0.0012-0.0019,
with nominal paired z-scores from 4.5 to 31.5 depending on configuration.
Those numbers are not yet evidence that a 0.0291 Hz physical frequency change
is resolved: frequency and acquisition order are completely confounded. A
repeat must balance A-B and B-A order. The test source has been updated to
alternate that order on successive estimates.

## PERIOD_COUNT timing

`PERIOD_COUNT` has strong Linux/control-latency outliers. For example, native
shift 16 has medians near 23,000 periods but maxima of 101,495 and 168,139.
Correlation between period count and R magnitude is weak for all native groups
(`|r| <= 0.12`), so these pre-integration timing excursions do not appear to be
driving the measured reflection-ratio noise.

## Current recommendation

- Use `WINDOW_SHIFT=20` when minimum single-acquisition noise is more important
  than update rate.
- For tracking-rate tradeoffs, two coherent shift-19 acquisitions are a strong
  alternative and were slightly quieter than one shift-20 acquisition here.
- Base tracking decisions on complex R rather than individual raw components;
  the ratio removes meaningful common-mode variation.
- Do not claim single-DDS-LSB resolution until the balanced-order rerun confirms
  a consistent A/B difference.
