# FPGA tracker arithmetic refactor and validation

Updated 2026-10-07. This work separates CPU coefficient preparation from the
five-point tracking and six-point fast-relock arithmetic intended for the FPGA.
The FPGA-facing kernels use multiplication, addition, and comparison, with no
division or square root. CPU presentation adapters retain gain, frequency SE,
and normalized residual in their existing telemetry units.

## Progress

| Area | Current implementation | Validation status |
| --- | --- | --- |
| Five-point fit | New `FivePointTracking::prepare/evaluate` API; CPU prepares parameter weights, template energy, and variance scale | Weighted versus original matrix equations pass |
| Tracking acceptance | RSS and gain-squared gate; existing shift limit and three-frame loss policy | Boundary, invalid-data, and saturation tests pass |
| Six-point fast relock | CPU prepares shift/gain weights and thresholds; runtime uses MACs and RSS | Weighted versus original projection equations pass |
| Tracker integration | Five-point tracking and fast-relock confirmation consume squared statistics | Existing tracking/relock replay regressions pass |
| Raw reflected input | Explicit `RawReflected` response selection in the acquisition adapter | Signed-word conversion and raw-scale tracking/recovery pass |
| FPGA and STM32 | Software arithmetic boundary only | RTL, firmware, fixed-point widths, and target validation remain future work |

## Coefficients and per-frame equations

For fast relock, the CPU subtracts reference and derivative means, then computes
`E = sum |t|^2`, `a_d = sum conj(t)*d / E`, `p = d - a_d*t`, and
`I = sum |p|^2`. It prepares `w = -p/I` and `v = t/E`.
The FPGA centers `e = measured - reference` and calculates:

```text
shift = sum Re(conj(w[j]) * e[j])
a_e = sum conj(v[j]) * e[j]
alpha = a_e + shift*a_d
gain_squared = (1 + Re(alpha))^2 + Im(alpha)^2
RSS = sum |e[j] + shift*d[j] - alpha*t[j]|^2
```

The shift weights absorb the reciprocal information. The projected derivative
is orthogonal to the template, so projecting the live error again is unnecessary
in exact arithmetic. Replay tests must bound the floating-point difference.

The CPU supplies `T_SE = 7*I*SE_max^2`, `C_R = R_max*E`, and `G_min_squared`.
The kernel accepts only when shift is within its bound and:

```text
RSS <= T_SE
RSS <= C_R*gain_squared + R_max*epsilon
gain_squared >= G_min_squared
```

The epsilon term preserves the existing `RSS/(gain_squared*E + epsilon)`
contract, with `epsilon = 1e-18`. It is prepared on the CPU.

For the five-point fit, the CPU prepares `inverse(X^T X)*X^T`, represented as
five sets of five complex weights. Each real parameter is the sum of the real
and imaginary error components multiplied by those weights. The runtime
calculates model RSS and gain squared. CPU preparation also supplies
`variance_per_RSS = spacing^2*inverse(X^T X)[0,0]/5`, so frequency variance
requires only one multiplication.

Tracking uses the existing shift limit `0.40*spacing`, residual limit `0.10`,
and minimum gain `0.25`. It still has no positive upper frequency-SE threshold;
adding one would change the tracking policy. Invalid or poor fits apply zero
shift and increment a saturating loss counter to three; good fits clear it.

## Coefficient ownership and invalidation

The CPU owns template construction, matrix inversion, reciprocals, reference
interpolation, and threshold preparation. Preparation is required whenever the
template, sample grid, spacing, or acceptance limits change. Fast-relock
interpolation uses actual effective probe frequencies translated back to the
baseline center. Its coefficients therefore cannot be assumed valid forever
after one baseline: recentering and DDS rounding can select different secants.

The current Red Pitaya code prepares fast coefficients after its six probes
provide effective-frequency readback. A future STM32 implementation can prepare
before acquisition only if it knows the exact grid the FPGA will use. Upload
all coefficients as one versioned bank and tag each frame with that version;
never mix points and weights from different grids or response representations.

## Raw reflected I and Q

The algorithm can operate on `I_REF + i*Q_REF` in ADC/accumulator units. The
reference, derivative, refinement scan, and live points must all use these
units. A baseline obtained from reflected/incident ratios cannot be reused with
raw reflected live points. Reacquire the baseline and rebuild coefficients when
changing representation, excitation amplitude, or integration/scaling settings.

Raw reflected response retains excitation and receive-chain changes that the
incident-normalized ratio can suppress. A uniform complex gain and offset are
model nuisance parameters; frequency-dependent source/receiver variation can
still bias a shift or raise residuals. This requires captured RF validation.

Raw integers are converted to double in a named response conversion layer.
The kernel itself needs neither an incident division nor absolute-voltage
calibration. Existing raw register acquisition, ready sequencing, and telemetry
remain the reference path. The live application's response selection and UI
unit labels must agree before activating raw response in production.

`RawIqMeasurementSource` accepts a constructor argument of
`ComplexResponseRepresentation::RawReflected`. Its immutable selection applies
to every point acquired through that source. The existing web worker still uses
the default `IncidentNormalized` selection, so the deployed UI's current units
remain consistent. There is no new browser control or silent live-mode switch.
The planned FPGA/STM32 path can feed raw reflected points directly into the
new arithmetic APIs after acquiring its baseline in those same units.

## Numerical concerns

- Cross-multiplied comparisons preserve the mathematical inequalities, but
  floating-point rounding can alter decisions exactly at thresholds. Tests cover
  equality, either side, and old-equation equivalence away from the boundary.
- Invalid templates, zero information, non-finite coefficients, and overflowed
  squared thresholds must be rejected. The double reference cannot substitute
  for RTL saturation/overflow flags.
- Raw-scale samples enlarge RSS and energy while reducing reciprocal weights.
  Coefficient and accumulator widths need separate range analysis. Signed
  32-bit register values are authoritative inputs; do not assume a narrower
  output range from older documentation without confirming the loaded bitstream.
- Fixed-point quantization can break template orthogonality. Compare quantized
  weights against this reference and establish error bounds before removing any
  safeguard or changing quality thresholds.
- The existing five-parameter normal-equation solver remains CPU setup work.
  Large offsets or extreme dynamic range can make it ill-conditioned; raw-scale
  replay tests check representative values, not every possible RF condition.
- Three-point tracking, live-Q calculation, baseline/model fitting, and local
  scan fallback retain CPU arithmetic. They are outside this division-free
  five-point/fast-estimator scope.
- CPU display adapters still divide for normalized residual and take square
  roots for gain/SE. Their outputs do not determine FPGA-kernel acceptance.

## Arithmetic counts

These counts are per sensor, on a complete valid execution, with a valid
coefficient bank already prepared. A complex multiply costs four real
multiplications; a real dot product of two complex vectors costs two per point;
a complex value multiplied by a real scalar costs two; squared magnitude costs
two. Constant reciprocal multiplication is counted as multiplication.
Compile-time-only calculations, additions/subtractions, comparisons, and sign
changes are outside the multiplication/division table. These are mathematical
operation counts, not DSP slice counts or measured instruction totals.

| FPGA-facing block | Real multiplications | Real divisions | Square roots |
| --- | ---: | ---: | ---: |
| Five-point parameter fit and shift conversion | 51 | 0 | 0 |
| Five-point quality statistics and acceptance/loss decision | 44 | 0 | 0 |
| Five-point tracking plus quality total | **95** | **0** | **0** |
| Six-point fast estimator including quality | **101** | **0** | **0** |
| Fast estimator followed by five-point confirmation and its quality gate | **196** | **0** | **0** |

Five-point fit: five parameters times five points times two real components
gives 50 multiplications; converting shift steps to Hz adds one. Quality costs
40 for model prediction/RSS (five points times eight), two for squared gain,
one for frequency variance, and one for the residual threshold. The standalone
quality decision consumes **one multiplication**, with no division or square
root. SE variance is returned for CPU display; the tracking policy checks its
validity but does not introduce an upper SE limit.

Fast estimator: error mean 12, shift dot products 12, complex gain dot products
24, gain correction two, squared gain two, model RSS 48, and residual threshold
one. The uncertainty limit is a stored RSS threshold, so its comparison adds
no multiplication. Total: `12 + 12 + 24 + 2 + 2 + 48 + 1 = 101`.

| Work outside the FPGA kernel table | Real multiplications | Real divisions | Square roots |
| --- | ---: | ---: | ---: |
| CPU five-point telemetry conversion | 1 | 1 | 2 |
| CPU fast-estimator telemetry conversion, when requested | 2 | 1 | 2 |
| CPU reference interpolation for six fast probes | 12 | 12 | 0 |
| Optional CPU live-Q calculation, full accepted five-point path | 24 | 7 | 1 |
| Raw signed reflected-word conversion to double | 0 | 0 | 0 |

CPU template preparation includes matrix inversion or reciprocal energy and
information calculations; its divisions are outside the per-frame kernels.
Fast-relock interpolation and preparing its external shift/SE limits also stay
on the CPU. Acquisition, excitation-frequency generation, coherent averaging,
center clamps, local-scan fitting, and three-point confirmation are excluded.
Fast five-point confirmation uses the squared gate directly and needs no CPU
gain/SE/residual presentation conversion.

For comparison, the previous split counted 76 multiplications for the five-point
parameter fit and 77 multiplications, 12 divisions, and two square roots for
tracking quality. Their combined count falls from 153 to 95 multiplications.
The six-point estimator falls from 186 multiplications, 44 divisions, and two
square roots to 101 multiplications with neither division nor square root.
Source constants and precomputed template work have moved to CPU preparation;
display conversion remains explicit CPU work.

## Validation and remaining target work

MSYS2 UCRT GCC host checks passed with `-std=c++20 -O2 -Wall -Wextra
-Wpedantic`:

| Test | Result and coverage |
| --- | --- |
| `division_free_test` | PASS: 360 fast-estimator and 240 five-point comparisons against original equations; scales 1, 4096, and 1000000; signed shifts, gain/offset, noise, rejection, squared boundaries, non-finite/overflow rejection, saturated loss counter, raw int32 limits, and normalized conversion compatibility |
| `tracker_engine_test` | PASS: existing three/five-point acquisition, averaging, loss, local/expanded/fast recovery, fallback, cancellation, failed confirmation, and healthy second sensor; added raw-scale five-point tracking and 11-point fast recovery |
| `fast_relock_test` | PASS: existing sign, gain/offset elimination, singular/flat derivatives, invalid data, gain collapse, shift/residual/uncertainty rejection |
| `resonance_analysis_test` | PASS: existing baseline/candidate/model replay regressions |
| `instrument_state_test` | PASS: existing state/cancellation regressions |
| `iq_statistics_test` | PASS: existing raw-IQ statistics regressions |

The original-equation tests use explicit relative and absolute tolerances and
compare acceptance decisions. Raw-scale inputs are synthetic double traces,
not captured hardware data or a fixed-point quantization test.

Optimized host object disassembly (`objdump -drC`) contains no division or
square-root instructions in `FivePointTracking::evaluate`,
`evaluateTrackingQualitySums`, or `FastRelock::evaluate`. The named raw response
adapter also compiles on the host. `git diff --check` passes. The existing
unused `fitCurvatureFallback` warning remains; changed/new arithmetic modules
compile without warnings. Host executables are under
`build/host-division-free/` (ignored build output).

The app's CMake source glob includes the new kernel automatically. Its tracker
test target links that source explicitly and adds `division_free_test`. The
Red Pitaya ARM SDK is unavailable on this Windows host, so the complete target
shared-library build and hardware/browser runtime are not verified here.

Target work still includes the Red Pitaya SDK build and RF/browser regressions,
STM32 coefficient upload, version handling, FPGA RTL, fixed-point simulation,
and timing/resource synthesis. No hardware register contract is changed by this
software refactor.
