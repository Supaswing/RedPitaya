# Tracking and relocking calculations

This describes the checked-out Red Pitaya implementation, inspected on
2026-10-05. The authoritative functions are `FrequencyTracker::prepare`,
`fitFive`, `fitThree`, `calculateLiveQ`, `acquire`, and `relock` in
[`tracker_engine.cpp`](../src/tracker_engine.cpp), `evaluateTrackingQuality` in
[`tracking_quality.cpp`](../src/tracking_quality.cpp), `fitComplexModel` and
`acquireLocalRelock` in [`resonance_analysis.cpp`](../src/resonance_analysis.cpp),
and the acquisition worker in [`main.cpp`](../src/main.cpp).
See [porting provenance](nanovna_porting.md) for the NanoVNA reference and
[architecture](architecture.md) for the parameter/signal contract.

## Inputs, units, and sampling

The live source converts the coherent incident and reflected integer I/Q pairs
to a dimensionless complex response:

```text
gamma = (I_REF + i Q_REF) / (I_INC + i Q_INC)
```

A zero incident vector is an acquisition error. Raw integers remain available
through raw-IQ telemetry; gamma is not calibrated impedance or volts.
Sensor IDs 1 and 2 identify resonances in this single response.

Each resonance starts with a valid baseline center `F`, width `W` (FWHM), Q,
spacing `s`, and five measured template values `T[k]`, for `k = -2..2`.
The complex-model baseline sets `s = max(W/4, refinement_scan_step)`.
All frequencies, widths, spacing, shifts, and standard errors are in Hz.

A tracking frame measures around the current center `F` at requested frequencies
`max(1, floor(F + k*s + 0.5))`. Five-point mode uses `k = -2..2`; three-point
mode uses `k = -1..1`, but still requires the five-point baseline template.
If averaging is enabled, real and imaginary gamma components are averaged
coherently at each offset. Repeats must have identical effective frequency.
For `S` resonances and `A` averages, a frame uses `S * points * A` windows.

Requested and effective frequencies are retained in point telemetry. The shift
estimator uses the baseline spacing and signed offset grid, rather than
rebuilding a design matrix from the actual DDS-effective frequencies.
The live adapter handles DDS readback, restart/run, settling, ready polling, and
timeout; see [raw-IQ sequencing](raw_iq_interface.md).

## Template derivative and five-point fit

The derivative `D[k]` is per offset step, not per Hz:

```text
D[-2] = T[-1] - T[-2]
D[ 2] = T[ 2] - T[ 1]
D[k]  = (T[k+1] - T[k-1]) / 2    for k = -1, 0, 1
```

With live values `L[k]`, fit the complex change using five real parameters:

```text
L[k] - T[k] ~= -u D[k] + (a + i b) T[k] + c + i d
p = [u, a, b, c, d]
real row = [-Re(D), Re(T), -Im(T), 1, 0]
imag row = [-Im(D), Im(T),  Re(T), 0, 1]
p = inverse(X^T X) X^T y
```

There are ten real observations and five fitted parameters. The normal-matrix
inverse is prepared once from the template; an inversion pivot below `1e-18`
rejects configuration. Positive `u` requests a higher center frequency because
a resonance shifted upward appears as `T - u*D` at the old sampling positions.

```text
requested_shift = u * s
gain = hypot(1 + a, b)
RSS = sum of squared real and imaginary fit residuals
E = sum_k |T[k] - mean(T)|^2
normalized_residual = RSS / (gain^2 * E + 1e-18)
frequency_SE = s * sqrt((RSS / 5) * inverse(X^T X)[0,0])
```

The implementation returns zero SE when the computed variance is nonpositive.
The `RSS/5` term uses the five residual degrees of freedom. Gain represents the
magnitude of a complex template scale; the constant complex offset absorbs a
uniform response change. This is a local linearized shift fit.

## Three-point fit

Use only the inner three values and remove their complex means. Define
`r[k] = L[k] - T[k]`, `r_c = r - mean(r)`, and `D_c = D - mean(D)`.

```text
B = sum_k |D_c[k]|^2
u = -sum_k Re(conj(D_c[k]) * r_c[k]) / B
requested_shift = u * s
U[k] = T[k] - u D[k]
U_c = U - mean(U)
L_c = L - mean(L)
E = sum_k |U_c[k]|^2
g = sum_k conj(U_c[k]) * L_c[k] / E
gain = |g|
RSS = sum_k |L_c[k] - g U_c[k]|^2
normalized_residual = RSS / (gain^2 * E + 1e-18)
frequency_SE = s * sqrt(RSS / B)
```

`B < 1e-18` or `E < 1e-18` rejects the fit. Unlike five-point mode, the shift
is estimated before the complex gain. Its SE formula has no extra residual
degrees-of-freedom divisor. A failed fit supplies shift 0, SE `s`, residual 1,
and gain 0, and is explicitly marked invalid.

## Quality, center update, and loss detection

A frame is poor for a sensor if its fit is invalid, any metric is non-finite,
SE/residual/gain is negative, spacing is nonpositive, or any test below fails:

| Metric | Accepted value |
| --- | --- |
| Absolute requested shift | `<= 0.40*s` |
| Normalized residual | `<= 0.10` |
| Template gain | `>= 0.25` |

SE is checked for validity but has no positive upper threshold. A poor frame
applies zero shift and increments the sensor's consecutive-loss counter, capped
at 3. A good frame clears that counter and applies the requested shift, with an
additional clamp to `[-0.5*s, +0.5*s]`. The quality gate already limits accepted
shifts to `0.40*s`.

The published center is `F_new = F_old + applied_shift`. Sampling points in that
frame were acquired around `F_old`. Any poor sensor makes the instrument
`DEGRADED`; three consecutive poor frames for any sensor request `RELOCKING`.
A good frame returns the instrument to `TRACKING` when no sensor remains poor.

For example, `s = 100000` Hz accepts a requested shift of 40000 Hz but rejects
40001 Hz. A rejected shift leaves the center unchanged. Three consecutive
rejections trigger recovery; an intervening good frame resets the count.

## Live Q calculation

Fit `y[k] = |L[k]|^2` to `a*k^2 + b*k + c` on the measured offsets.
Three-point coefficients are:

```text
a = (y[-1] + y[1] - 2*y[0]) / 2
b = (y[1] - y[-1]) / 2
c = y[0]
```

Five-point coefficients are:

```text
S0 = y[-2] + y[-1] + y[0] + y[1] + y[2]
S2 = 4*y[-2] + y[-1] + y[1] + 4*y[2]
a = (5*S2 - 10*S0) / 70
b = (-2*y[-2] - y[-1] + y[1] + 2*y[2]) / 10
c = (34*S0 - 10*S2) / 70
vertex = -b / (2*a)
y_min = c - b^2 / (4*a)
y_edge = (y[first_offset] + y[last_offset]) / 2
h = sqrt((y_edge - y_min) / a)
Q_live = F_new / (2*h*s)
```

Use baseline Q instead when `a <= 1e-18`, the vertex lies outside the measured
offset range, `y_edge <= y_min`, or `h < 0.1`. This local quadratic width estimate
is distinct from baseline Lorentzian FWHM. Q is calculated even for poor frames;
it does not determine the loss decision.

## Local relock scans and model

Only sensors with loss counter 3 are scanned, sequentially. Both attempts use
the frozen tracking center and original baseline width `W`:

| Attempt | Points | Requested interval before clipping |
| --- | --- | --- |
| 1 | 11 | `F - 1.5*W` through `F + 1.5*W` |
| 2 | 21 | `F - 3*W` through `F + 3*W` |

Clip each interval to the saved baseline start/stop frequencies. Skip an attempt
if less than one baseline FWHM remains. Sample uniformly, rounded to integer
requested Hz, with one complex acquisition per point and no tracking averaging.

The fitter removes a least-squares complex quadratic background from the scan.
For candidate center `f0`, half-width `w`, and orientation `o = -1 or +1`, form
the Lorentzian basis and remove its quadratic background too:

```text
z[j] = o * (f[j] - f0) / w
b[j] = (1 - i*z[j]) / (1 + z[j]^2)
r = background-removed scan
b_perp = background-removed basis
score = |sum_j conj(b_perp[j]) * r[j]|^2 / sum_j |b_perp[j]|^2
model_explained_fraction = best_score / sum_j |r[j]|^2
FWHM_fit = 2*w
```

The explained fraction measures how much of the background-removed energy is
captured by the resonance, rather than how much of the total response is fit.
Flat residual energy below `1e-18` or nonpositive best score rejects the model.

The initial search evaluates centers at scan indices 2 through `N-3`, both
orientations, and half-widths equal to scan step times
`{0.5, 0.75, 1, 1.5, 2, 3, 4, 6, 8, 12, 16, 24, 32}`, plus `W/2`.
Width candidates are constrained to `0.67*W <= 2*w <= 1.50*W`.
Three refinement passes evaluate 17 centers around the current best center,
with half-ranges `step`, `step/4`, and `step/16`, and width multipliers
`{0.65, 0.8, 0.9, 1, 1.1, 1.25, 1.5}`. Centers remain at least two scan
steps inside the fitter's requested-frequency endpoints.

Acceptance additionally requires explained fraction `>= 0.20`, the same width
ratio bounds, and a center strictly more than one nominal scan step inside
each clipped interval edge. A successful relock changes only the tracked center
and clears the loss counter. The original template, derivatives, spacing,
baseline width, and baseline Q remain unchanged.

## Bounds, fallback, and publication

Local relock uses at most 32 acquisitions per lost sensor (64 for two sensors).
The worker supplies one two-second deadline for the whole batch, checked before
each point and after scan acquisition. This is a cooperative deadline: a
measurement or model fit already running may finish after it. The live
measurement ready timeout is 10 ms per window. Stop/new-command cancellation
also uses these checks and the worker's operation generation.

Local success resumes tracking. Rejected scans or deadline expiry schedule a
full baseline with the saved configuration; tracking restarts only after a
valid new baseline and template configuration. The two-second deadline applies
to local recovery, not to that full baseline or the number of later recoveries.
An acquisition error enters `ERROR`. A superseding stop/command cancels recovery
without scheduling fallback. If two sensors need relock, an earlier successful
sensor can be recentered before a later sensor fails; fallback replaces the
baseline for the configured sensor set.

Only complete tracking frames are published, with one sequence, sensor IDs,
signed offsets, effective frequencies, complex values, estimates, and quality.
During recovery the last complete frame remains available. Relock progress is
reported separately through `RT_RELOCK_*`; attempt progress uses the 32-point
maximum and success sets it to 100%. Dashboard selection does not affect the
calculations or hardware mode.

## Verification and limits

[`tracker_engine_test.cpp`](../tests/tracker_engine_test.cpp) covers positive
shifts in both modes, coherent averaging, threshold decisions, invalid metrics,
three-frame loss with no center motion, first/expanded relock, rejection,
cancellation, and recovery of only the lost sensor. See
[Milestone 2B validation](milestone_2b_validation.md) for prior test results and
remaining target checks. Captured complex-input numerical golden replays for
relock remain pending; existing event logs alone cannot validate the equations.
