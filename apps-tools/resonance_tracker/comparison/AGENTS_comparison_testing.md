# AGENTS.md

## Project purpose

This work spans two repositories:

- `Redpitaya` — STEMlab 125-14 Gen2 / Red Pitaya FPGA and ARM/Linux implementation
- `Sdsi_reader` — NanoVNA-based reader / resonance-tracking implementation

Together, these repositories support performance characterization of two resonance-reader platforms:

- NanoVNA
- STEMlab 125-14 Gen2 / Red Pitaya

The goal is a short engineering report comparing the two platforms for reading weakly coupled passive LC sensors.

The report is intentionally limited in scope. Do not turn this into a full system characterization.

Primary comparison metrics:

1. sensor contrast (`|Delta Gamma|` and/or controlled `Delta f`)
2. frequency-estimation noise / `dF RMS`
3. resulting SNR
4. frame rate
5. important measurement artifacts observed during testing

Operating-point diagnostics should also be recorded so performance differences can be interpreted correctly:

- source amplitude
- received amplitude
- ADC full-scale utilization where available
- TX/RX gain or attenuation settings

Use one or at most two fixed antenna/sensor setups.

---

## Experimental philosophy

Keep the physical setup fixed when comparing platforms.

Do not optimize each platform independently to an unrealistic best case unless a test explicitly requests it.

The comparison should answer:

> With the same fixed antenna/sensor condition and reasonable acquisition settings, what noise, SNR, and frame rate are obtained with NanoVNA and Red Pitaya?

Prefer repeatable measurements over broad parameter sweeps.

---

## Test setups

Use one or two fixed setups only.

### Setup A

Record explicitly:

- antenna identifier / geometry
- sensor identifier
- nominal sensor resonance frequency
- antenna-to-sensor position
- distance / coupling geometry
- coax cable type and length
- CMC present or absent
- fixed antenna tuning components
- bridge/reference configuration
- any other hardware that changes the RF path

Do not change these parameters between NanoVNA and Red Pitaya measurements unless the test explicitly requires it.

### Setup B

Optional.

Use only if a second antenna/coupling condition is needed to show a meaningful difference, for example:

- stronger vs weaker coupling
- smaller vs larger belt antenna

Keep the sensor and geometry fixed within the setup.

---

## Sensor state

The sensor should remain physically fixed during noise measurements.

For signal/SNR measurements, use a controlled, documented change.

Preferred option:

- two fixed known sensor states

Avoid uncontrolled manual movement as the primary SNR measurement.

---

# Required measurements

## 1. Static repeated measurements

For each platform and setup, collect repeated measurements with no intentional physical change.

Target at least:

- 20 repeats for initial characterization
- more if acquisition time allows

Compute:

- mean frequency
- frequency SD
- `dF RMS`
- internal estimator SE if available
- normalized residual if available
- complex gain if available

For complex measurements also compute:

- mean `Re(Gamma)`
- mean `Im(Gamma)`
- `sigma_ReGamma`
- `sigma_ImGamma`
- magnitude noise RMS
- phase noise RMS

For Red Pitaya raw-IQ debug, also retain:

- incident I mean
- incident Q mean
- reflected I mean
- reflected Q mean
- raw I/Q standard deviations
- I/Q covariance if FPGA provides it

Do not substitute internal estimator SE for measured repeatability SD.

Both should be reported separately.

---

## 2. Controlled signal measurement

Measure a known sensor change.

For frequency-based analysis:

`Delta_f_signal = mean(f_state_2) - mean(f_state_1)`

Then calculate:

`SNR_f = abs(Delta_f_signal) / sigma_f`

State clearly which `sigma_f` is used.

Normally use the repeatability SD from the unchanged static condition.

For complex-response analysis:

`Delta_Gamma = Gamma_state_2 - Gamma_state_1`

Possible SNR metrics:

`SNR_Gamma = abs(Delta_Gamma) / sigma_Gamma`

If a scalar complex-noise metric is used, document its definition.

---

## Operating-point and dynamic-range measurements

For each platform/configuration, record enough information to distinguish genuine reader performance from a different excitation or receiver operating point.

Record where available:

- configured source amplitude
- measured RF source amplitude (`V_RMS`, `V_pp`, or dBm into 50 ohm)
- incident received amplitude
- reflected received amplitude
- TX gain/attenuation setting
- RX gain/attenuation setting
- incident ADC peak percentage of full scale
- reflected ADC peak percentage of full scale
- RMS ADC level where useful
- clipping/overflow indication

ADC full-scale utilization should be computed from peak amplitude when possible:

`ADC_usage_percent = 100 * V_peak / V_FS_peak`

These are primarily operating-point diagnostics, not headline performance metrics. Do not rank platforms simply by internal gain or ADC utilization. Use them to explain differences in sensor contrast, noise, and SNR.

If source amplitudes differ materially between measurements, flag this explicitly. For a properly normalized complex reflection ratio, `Gamma` should remove much of the linear source-amplitude dependence, but source-level sweeps may be used to check linearity and determine whether additional excitation improves SNR.

## 3. Frame rate

Measure actual frame rate from timestamps, not only theoretical timing.

Report:

- mean frame interval
- median frame interval
- frame-rate mean
- frame-rate variation / jitter if meaningful

For two sensors, specify whether the rate is:

- per sensor
- complete two-sensor frame rate

Do not mix these definitions.

---

# Tracker comparison

Where practical, evaluate both:

- 3-point tracker
- 5-point tracker

Use the same baseline/template source for a fair comparison.

For a five-point template:

`f0 - 2s`
`f0 - s`
`f0`
`f0 + s`
`f0 + 2s`

The three-point tracker should use the middle three points:

`f0 - s`
`f0`
`f0 + s`

Prefer `s ~= FWHM / 4`, but align to the actual frequency grid where useful.

Do not independently optimize the 3-point and 5-point frequencies unless the purpose of the test is specifically to optimize them.

Compare:

- measured `dF RMS`
- measured frequency SD
- internal SE
- normalized residual
- gain stability
- frame rate

Previous Red Pitaya repeated-sweep work showed that the 5-point estimator can materially reduce frequency jitter relative to the 3-point estimator. Treat previous values as context only; regenerate results for the formal comparison dataset.

---

# NanoVNA acquisition notes

Record for every dataset:

- start frequency
- stop frequency
- number of sweep points
- actual frequency spacing
- bandwidth / IF setting
- averaging count
- source-power setting if configurable
- calibration state
- calibration reference plane
- tracker mode: 3 pt / 5 pt / full sweep
- timestamp

Do not compare calibrated and uncalibrated data without explicitly labeling them.

For S11-derived data, preserve the original complex values whenever possible.

Do not keep only magnitude.

---

# Red Pitaya acquisition notes

Platform:

- STEMlab 125-14 Gen2
- ARM Cortex-A9 Linux application
- FPGA performs coherent acquisition / accumulation
- ARM performs tracker logic and analysis

Record for every dataset:

- excitation frequency
- phase
- amplitude
- integration/window length
- FPGA `WINDOW_SHIFT` if used
- settling/discard samples
- repeated averages
- tracker points
- bitstream/version
- application git commit if available

Preserve raw incident and reflected I/Q whenever debug logging is enabled.

Reflection ratio convention must be explicit and consistent.

Use:

`Gamma = V_reflected / V_incident`

unless the existing hardware/API explicitly uses another convention.

Do not silently swap numerator and denominator.

---

# FPGA/debug points to monitor

## 25 MHz artifact

There is a recurring feature/noise artifact around 25 MHz that has also been observed with a 50-ohm load.

Treat this as a system artifact until proven otherwise.

When investigating it, record:

- 50-ohm load vs antenna
- sweep start/stop
- frequency grid / step
- integration length
- source amplitude
- DDS phase
- settling/discard length
- display/SPI activity state if applicable

Do not classify it as an antenna resonance if it persists with a 50-ohm load.

---

## Jigsaw / deterministic-looking noise

Investigate whether the irregular spectral pattern is related to:

- frequency-change transient
- NCO/DDS coherence
- integration-window length
- digital filter transient
- FPGA accumulation start
- ADC settling
- acquisition ordering

The FPGA design should support discarding an initial head of samples before accumulation.

Conceptually:

`set frequency -> settle/discard -> accumulate -> latch -> done`

Test multiple discard lengths before adding analog filtering.

---

## 35-40 MHz wideband antenna noise

A broad increase in noise has been observed with the antenna around 35-40 MHz.

Compare:

- 50-ohm load
- antenna
- antenna + CMC
- different coax length if requested

Use raw-IQ statistics and complex Gamma noise to determine whether the effect is:

- externally picked-up noise
- common-mode current
- antenna/cable resonance
- ADC/dynamic-range effect

---

## Display/SPI interference

Display/SPI activity has been observed to degrade performance.

For performance datasets, record whether the display is:

- disabled
- powered but static
- actively refreshing

Prefer a stable, documented display state for formal comparisons.

Do not mix display-on and display-off datasets in one noise statistic.

---

# Red Pitaya FPGA acquisition contract

The FPGA should expose a deterministic acquisition primitive.

Inputs should include, as applicable:

- frequency
- phase
- amplitude
- integration samples / window length
- settling/discard samples

Outputs should include:

- incident I/Q
- reflected I/Q
- sample count
- sequence/result counter
- optional raw-IQ statistics

For debug statistics, desirable FPGA accumulators are:

- `sum(I)`
- `sum(Q)`
- `sum(I^2)`
- `sum(Q^2)`
- `sum(I*Q)`

for both incident and reflected channels.

The same acquisition gate/window must be used for both channels and for all statistics.

Do not create independent incident/reflected windows.

Results should be latched atomically at acquisition completion so the ARM cannot read values from mixed acquisitions.

---

# Linux / ARM implementation

Keep the resonance-tracking algorithm separate from hardware access.

Preferred layering:

`resonance_tracker`
-> `rp_acquisition`
-> `rp_fpga`
-> FPGA registers

The tracker should ask for:

`measure(frequency) -> complex Gamma`

and should not manipulate FPGA registers directly.

Using `/dev/mem` + `mmap()` is acceptable for the prototype if the physical register map is verified.

Add FPGA ID/version checking if registers are available.

---

# Data storage

Never overwrite raw measurement files.

Use separate directories, for example:

```text
data/
  setup_A/
    nanovna/
      raw/
      processed/
    redpitaya/
      raw/
      processed/
  setup_B/
    ...
```

Each dataset should have either metadata in the file or a neighboring metadata file.

Recommended metadata fields:

```text
platform
setup_id
date_time
sensor_id
antenna_id
sensor_state
start_hz
stop_hz
points
bandwidth_hz
integration_samples
settling_samples
averages
tracker_points
source_amplitude
calibration_state
display_state
cmc_state
tuning_state
notes
```

---

# Naming convention

Prefer filenames such as:

```text
setupA_redpitaya_static_5pt_run001.csv
setupA_redpitaya_state2_5pt_run001.csv
setupA_nanovna_static_5pt_run001.csv
```

Avoid names such as:

```text
test3_new_final2.csv
```

---

# Analysis outputs

Produce machine-readable CSV summaries first.

A typical summary row should contain:

```text
platform
setup
tracker_points
acquisition_setting
mean_frequency_hz
frequency_sd_hz
df_rms_hz
mean_internal_se_hz
mean_normalized_residual
sensor_contrast_abs_delta_gamma
source_amplitude
incident_amplitude
reflected_amplitude
incident_adc_percent_fs
reflected_adc_percent_fs
tx_gain_attenuation
rx_gain_attenuation
sigma_re_gamma
sigma_im_gamma
phase_noise_deg
frame_rate_hz
signal_delta_f_hz
snr_f
```

Not every field will exist for every platform.

Use blank/NA rather than inventing unavailable values.

---

# Plots for the short report

Keep the final report concise.

Target only a few useful figures:

1. Representative resonance response: NanoVNA vs Red Pitaya.
2. Repeated frequency estimate: static-condition `dF` over repeated measurements.
3. Performance summary: SNR and/or `dF RMS` together with frame rate.

Do not generate large numbers of exploratory plots for the final report.

Exploratory plots may remain in the analysis directory.

---

# Final report target

The final report should remain short, approximately 4-6 pages.

Suggested sections:

1. Objective
2. Test setups
3. Acquisition settings
4. Noise performance
5. Signal and SNR
6. Frame-rate comparison
7. Short conclusion

The core summary table should look approximately like:

| Platform | Setup | Tracker | Source level | ADC %FS | Sensor contrast | dF RMS | Signal Delta f | SNR | Frame rate |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|

Avoid expanding the report into unrelated topics such as complete bridge optimization, FEM/SAR, product design, or exhaustive antenna matching unless explicitly requested.

---

# General instructions for Codex

- Preserve raw data.
- Do not silently modify measurement settings.
- Do not infer missing hardware parameters.
- Make experiment metadata explicit.
- Prefer scripts that can be rerun.
- Put analysis code under version control.
- Keep platform-specific acquisition code separate from common analysis.
- Keep units in column names when practical.
- Always distinguish RMS, population SD, sample SD, and estimator SE.
- Always distinguish sensor signal from measurement noise.
- Always distinguish per-sensor frame rate from full two-sensor frame rate.
- Flag suspicious results rather than automatically removing them.
- Never remove outliers without preserving the original data and documenting the rule.
- For platform comparisons, use matched physical conditions whenever possible.
