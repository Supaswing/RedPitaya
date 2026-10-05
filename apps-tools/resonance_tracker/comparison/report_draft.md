# NanoVNA / Red Pitaya sensor comparison — working report

Superseded for review by [comparison_report_2026-09-29.md](comparison_report_2026-09-29.md).

Status: **fresh two-platform static captures; setup match and signal/SNR still
unverified**, 28 September 2026. This draft records the available data and the
measurement contract. The datasets below must not yet be used to rank the
platforms because the physical setup and source operating points have not
been verified as equivalent.

## Scope and setup

The target setup has one small antenna and two sensors described as “Cser
TBM.” Sensor 1 is 6 cm and sensor 2 is 7 cm from the antenna, with the distance
reference still to be specified. Sensor identities, antenna geometry, cable,
CMC, bridge/reference configuration, fixed tuning components, NanoVNA
calibration plane, and display state remain unrecorded. The setup metadata is
in `setup_A_metadata.csv`.

The Red Pitaya is a STEMlab 125-14 Gen 2 with coherent FPGA I/Q acquisition
and ARM-side resonance analysis. The application writes `0x0800` to amplitude
register offset `0x10` at acquisition startup. The user identifies this as an
800 mV peak source setting into 50 ohms. This is a configured level, not a
measured RF output voltage. If 800 mV peak is present across the 50-ohm load
as a sinusoid, the equivalent is 0.566 V RMS and 6.4 mW (+8.06 dBm). No
corresponding NanoVNA source level, receiver gain/attenuation, received
incident/reflected voltage, or ADC full-scale percentage has been recorded.

No controlled physical sensor-state change is available yet. Changing
`WINDOW_SHIFT` or averaging is an acquisition-setting experiment and must not
be treated as a sensor signal. Consequently `Delta f`, `|Delta Gamma|`, and
sensor SNR have no valid measured values in this draft.

## Available observations

| Dataset | Frames / points | Frequency or fit | Frequency noise / uncertainty | Rate | Interpretation |
| --- | ---: | ---: | ---: | ---: | --- |
| NanoVNA 100 Hz bandwidth, older one-sensor capture | 864 complete 5-point RTD frames | mean 24,684,005 Hz | sample SD 6,083.6 Hz; population dF RMS 6,080.0 Hz; mean internal SE 5,709.5 Hz | 14.47 one-sensor frames/s | Context only; setup match unverified |
| NanoVNA 4 kHz bandwidth, older one-sensor capture | 3,776 complete 5-point RTD frames | mean 24,680,780 Hz | sample SD 10,979.6 Hz; population dF RMS 10,978.1 Hz; mean internal SE 14,735.7 Hz | 63.16 one-sensor frames/s | Context only; setup match unverified |
| Red Pitaya 2-sensor baseline, `WINDOW_SHIFT=17`, coarse/refine averages 3 | 151 overview points; 21 refine points requested per sensor | sensor 1: 21,114,170 Hz, Q 104.76; sensor 2: 24,702,904 Hz, Q 84.41 | fit SE 3,636 Hz and 3,911 Hz; model quality 0.903 and 0.836 | baseline completed in about 1.6 s | Single baseline, not a repeatability or tracking-noise measurement |
| Red Pitaya, same setup attempt, 5-point static tracking | 34 sequence-matched complete **telemetry** frames, 2 sensors | sensor 1: 21,119,458 Hz; sensor 2: 24,693,911 Hz | sample SD 18,840 Hz / 21,976 Hz; population dF RMS 18,561 Hz / 21,650 Hz; mean internal SE 8,433 Hz / 32,700 Hz | backend-reported mean about 67 complete two-sensor frames/s | Telemetry is decimated; preliminary observed-subset noise only |
| Red Pitaya, same baseline, 3-point static tracking | 97 sequence-matched complete **telemetry** frames, 2 sensors | sensor 1: 21,122,208 Hz; sensor 2: 24,686,137 Hz | sample SD 6,485 Hz / 38,368 Hz; population dF RMS 6,452 Hz / 38,169 Hz; mean internal SE 8,572 Hz / 26,017 Hz | backend-reported mean about 75 complete two-sensor frames/s | Sequential run and unequal telemetry sampling prevent a clean 3-vs-5 noise ranking |
| Red Pitaya `WINDOW_SHIFT=20`, 3 baseline averages, 5-point tracking | 230 consecutive complete two-sensor frames | sensor 1: 21,120,793 Hz; sensor 2: 24,687,462 Hz | sample SD 3,473 Hz / 3,947 Hz; population dF RMS 3,466 Hz / 3,939 Hz; mean internal SE 2,243 Hz / 5,662 Hz | 11.48 observed complete frames/s; backend about 11.5/s | One near-complete timestamped run; network arrival jitter remains included |
| Red Pitaya `WINDOW_SHIFT=20`, same baseline, 3-point tracking | 250 complete telemetry frames from 383 backend sequences | sensor 1: 21,119,167 Hz; sensor 2: 24,687,334 Hz | sample SD 1,286 Hz / 3,808 Hz; population dF RMS 1,283 Hz / 3,800 Hz; mean internal SE 3,333 Hz / 2,904 Hz | backend about 19.1/s; matched telemetry subset about 12.5/s | Skipped telemetry sequences; rate and noise interpretations differ |
| NanoVNA fresh COM11 capture, 100 Hz tracking bandwidth, 5-point | 106 consecutive complete two-sensor RTD frames | sensor 1: 21,121,501 Hz; sensor 2: 24,688,131 Hz | sample SD 4,151 Hz / 4,696 Hz; population dF RMS 4,132 Hz / 4,674 Hz; mean internal SE 7,015 Hz / 6,994 Hz | 7.25 complete two-sensor frames/s | Setup match and source operating point require confirmation |
| NanoVNA same baseline, 100 Hz tracking bandwidth, 3-point | 169 consecutive complete two-sensor RTD frames | sensor 1: 21,121,597 Hz; sensor 2: 24,695,524 Hz | sample SD 5,868 Hz / 4,097 Hz; population dF RMS 5,851 Hz / 4,085 Hz; mean internal SE 12,754 Hz / 6,681 Hz | 11.73 complete two-sensor frames/s | Same template; sequential interval after five-point run |

The NanoVNA rates are calculated from capture timestamps as the reciprocal of
the mean adjacent RTD interval. They are **one-sensor** frame rates. The Red
Pitaya baseline time is command-to-completion elapsed time and is not a
tracking-frame rate. Internal fit SE describes one estimate; it is not the
standard deviation of repeated estimates. The Red Pitaya's two baseline
resonance identities require physical verification before sensor labels are
used in a platform comparison.

The 5-point and 3-point rows above use one newly acquired two-sensor baseline
(21,122,554 Hz and 24,682,094 Hz; Q 104.06 and 90.44). The runs each lasted
20 seconds in that order. The 5-point and 3-point backend rates are averages
of the app's `RT_TRACK_RATE_HZ`, computed over approximately one-second windows
from a monotonic clock. They are supported by sequence growth over the timed
runs, but the app does not expose individual backend-frame timestamps or their
jitter. The WebSocket delivered only a subset of backend frames, and parameters
and signals are published separately. The extractor accepts a frame only when
both share a sequence and have both sensors and all expected signed offsets.
It found 131 such frames and rejected no matched pairs. The observed arrival
rates of these sequence-matched telemetry frames were only about 1.7/s for
5-point and 4.8/s for 3-point; these are **not acquisition rates**. Their
frequency SDs can be affected by decimation and timing selection. The
different sequential time windows also confound a direct 3-vs-5 noise ranking.

At `WINDOW_SHIFT=20`, the longer integration slowed the backend and allowed
the WebSocket to carry all 230 five-point sequences in order during the timed
run. The elapsed arrival intervals imply 11.48 frames/s, close to the
backend's one-second-window rate of 11.5 frames/s. Their 0.028 s sample SD
includes transport and scheduling jitter and is not a pure acquisition-jitter
measurement. The 3-point run skipped backend sequences in telemetry, so its
observed arrival rate must not be substituted for its backend rate. The
lower observed dF RMS at shift 20 is consistent with longer integration, but
the runs are sequential and the source level/geometry were not independently
verified between them.

The Red Pitaya baseline was captured in
`output/setupA_redpitaya_baseline_shift17_avg3_full_20260928.jsonl` with
sequence-matched result signals. The older NanoVNA raw files are in the
documented sibling `sdsi_reader` checkout; summaries are under
`output/context_bw100` and `output/context_bw4000`. The earlier Red Pitaya
fixed-frequency raw-I/Q experiment near 30 MHz is preserved in
`../test-results/2026-09-23/raw_iq_noise_samples.csv`; it is not a resonance
tracking capture and has no per-acquisition timestamps.

The new Red Pitaya raw tracking stream is in
`output/setupA_redpitaya_static_shift17_avg3_5pt3pt_20260928_1215.jsonl`.
`extract_tracking.ps1` produces its paired estimate and complex-point CSVs;
`summarize_telemetry.ps1` produces a labelled observed-subset summary. The
point CSV has 922 complex points from the 131 accepted telemetry frames.
The analogous shift-20 capture and extracted files use the prefix
`output/setupA_redpitaya_static_shift20_avg3_5pt3pt_20260928_1230`.
The fresh NanoVNA raw serial log and metadata are in
`output/setupA_nanovna_20260928_123520/`. The `analyze_nanovna_quick.py`
summary confirms 106 and 169 complete RTD frames with no sequence gaps and
uses their arrival timestamps to calculate complete two-sensor frame rates.
The NanoVNA baseline scanned 20–26 MHz with 101 coarse points, 3 coarse
averages, 21 refinement points and 3 refinement averages, 16 kHz IF, and
100 Hz tracking bandwidth. It fitted 21,121,256 Hz (Q 106.36, model quality
0.9866) and 24,692,258 Hz (Q 91.335, model quality 0.9900). A separate
debug frame preserves ten complex RTM points, five per sensor. Baseline SE
was reported as zero for both sensors, so it must not be interpreted as zero
physical uncertainty.
The live mean Q changed from about 81/92 in five-point mode to 159/182 in
three-point mode without a deliberate physical change. The sparse live Q
diagnostic is therefore unsuitable as evidence of a physical Q change here.

## Measurement definitions for the matched run

For each unchanged sensor state, use at least 20 sequence-complete repeated
frames per sensor and platform. Report mean frequency, sample SD using `N-1`,
population dF RMS around the run mean using `N`, mean internal estimator SE,
normalized residual, template gain, and elapsed-time statistics. Keep
per-sensor and complete two-sensor frame rates separate. The three-point run
should use the middle three frequencies of the five-point template from the
same baseline, with no independent template optimization.

Retain original complex values. Define the Red Pitaya response as
`Gamma = V_reflected / V_incident`. For fixed-frequency complex repeats,
report mean real and imaginary Gamma, sample SD of each component, complex
noise RMS as the square root of the sum of the component population
variances, magnitude noise RMS, and wrapped phase noise RMS. Preserve raw
incident/reflected I/Q and calculate I/Q covariance from repeated windows
when the FPGA does not supply covariance.

For a controlled two-state change at fixed geometry, define
`Delta_f_signal = mean(f_state_2) - mean(f_state_1)` and
`SNR_f = abs(Delta_f_signal) / sample_SD(f_state_1)`. Alternatively, define
`Delta_Gamma = mean(Gamma_state_2) - mean(Gamma_state_1)` and divide its
magnitude by the stated complex noise RMS. Do not use internal estimator SE
as the measured repeatability denominator.

## Artifacts and limits

The comparison guide calls out a recurring feature near 25 MHz, including
with a 50-ohm load, and structured frequency-dependent noise. The present
20–26 MHz Red Pitaya baseline passes through that region. No artifact
classification is justified from this one capture. The apparent sensor 1
baseline at 21.114 MHz also differs from an earlier user-visible example
around 21.68 MHz; a repeat scan and physical sensor identification are needed.

The Red Pitaya web controller was absent from one process list after an earlier
successful baseline, while nginx remained running. Subsequent WebSocket
connections temporarily carried system parameters but no `RT_*` telemetry.
The device journal showed FPGA loads on launch attempts and no explicit
controller failure message. A later approved start restored telemetry and the
static tracking run succeeded. The browser frontend calls `/bazaar?stop` on
tab unload, which may explain one interruption but was not proven to be its
cause. Earlier failed capture files are retained and not counted as results.

A single one-average baseline at `WINDOW_SHIFT=17` selected 24.729 and
24.895 MHz (Q 116.4 and 530.1; model quality 0.841 and 0.277), missing the
~21.12 MHz lobe found with three averages. The backend still marked this
baseline valid. Its subsequent tracking data are preserved under the
`output/setupA_redpitaya_static_shift17_avg1_5pt3pt_20260928_1240` prefix
but excluded from the sensor noise table because the candidate-to-sensor
assignment is not credible. This is one run, not a measured failure rate.
Its baseline SE fields were both zero because one replicate cannot estimate
a between-replicate SE; zero must not be interpreted as zero uncertainty.
The shift-20 three-average baseline also reported zero SE despite nonzero
tracking SE, consistent with identical replicate center-grid choices. The
baseline SE representation needs an explicit availability/resolution flag.

## Completion conditions

1. For a defensible backend-frame jitter and frequency-noise result, capture
   every backend tracking frame with its monotonic timestamp and complex
   points, or demonstrate that WebSocket sampling is unbiased. Repeat with
   the selected `WINDOW_SHIFT` and averaging settings.
2. Confirm that the fresh NanoVNA capture used the same fixed antenna/sensor
   positions and RF path as the Red Pitaya run. Record the NanoVNA source
   level, display state, calibration state/plane, coax/CMC, and any reader
   reconnection changes. The two-sensor baseline, bandwidth, averaging,
   timestamps, and one complex frame are already captured.
3. Record a repeatable physical second sensor state if signal/SNR is needed.
   Until then, leave signal and SNR columns blank.
4. Record or measure source and received levels and ADC utilization where a
   valid scale exists. Compare actual loaded RF source voltages before drawing
   a platform-performance conclusion.

The comparison calculations are reproducible with `analyze.ps1`. The
`validate.ps1` check passes against the existing NanoVNA reference statistics
and a labelled synthetic two-sensor fixture; the fixture is not hardware data.
