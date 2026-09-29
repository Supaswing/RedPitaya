# Two-sensor resonance reader comparison — review report

**Setup A · NanoVNA and Red Pitaya STEMlab 125-14 Gen 2 · 29 September 2026**  
**Status:** static frequency-noise and frame-rate results complete for the saved captures; physical matching and NanoVNA display state remain unverified. Sensor signal and SNR were not measured.

## 1. Objective and scope

This experiment compares repeated resonance-frequency estimates and complete two-sensor frame rates from the NanoVNA-derived reader and the Red Pitaya resonance tracker. The [updated Setup A metadata](setup_A_metadata.csv) describes antenna 1 as a small antenna with “Cser TBM” and geometry entered as `240mm (75cm)`, sensor `custom 1` at 6 cm and `custom 2` at 7 cm from reference `p1`, 0.15 m MMCX coax, no CMC, and a 50 Ω bridge reference. The meaning/value of Cser remains to be measured. The geometry entry is internally ambiguous and is preserved verbatim. Both readers scanned 20–26 MHz and found resonances near 21.12 and 24.69 MHz. Physical matching during the reader swap has not been independently verified, so these are **nominal Setup A** results.

The comparison uses a 20-second Red Pitaya `WINDOW_SHIFT=20` capture and the NanoVNA COM11 capture at 100 Hz tracking bandwidth, saved at 12:35 UTC on 28 September 2026. Each used one baseline to run five-point tracking followed by three-point tracking. **Only this NanoVNA capture is analyzed.** No point was removed from these tracking runs.

## 2. Acquisition and operating point

| Item | Red Pitaya main run | NanoVNA fresh run |
| --- | --- | --- |
| Time (UTC, 28 Sep 2026) | Approximately 12:30 | 12:35–12:36 |
| Coarse scan | 20–26 MHz, 151 points | 20–26 MHz, 101 points |
| Coarse/refinement averaging | 3 / 3 complex averages | 3 / 3 complex averages |
| Refinement | 21 points per selected candidate | 21 points per selected candidate |
| Tracking | Two sensors; five points, then middle three points from the same baseline | Two sensors; five points, then middle three points from the same baseline |
| Integration/bandwidth | FPGA `WINDOW_SHIFT=20`: 2²⁰ samples at 125 MS/s per point; effective noise bandwidth not calibrated here | 100 Hz configured tracking bandwidth; 16 kHz IF reported by baseline record |
| Source setting | User reports 800 mV **peak into 50 Ω** at amplitude register `0x10`; app writes `0x0800` | User reports DDS 8 mA into 50 Ω, 400 mV RMS at source, 200 mV RMS at antenna |
| Display state | No local display in the reader path | Not recorded for this capture; a display-off measurement has not been verified |
| Calibration/plane | Raw coherent incident/reflected I/Q ratio, no absolute S11 calibration claimed | Metadata says calibrated at coax output; tracker uses raw measurement/reference ratio before the usual NanoVNA calibration/error-term path |
| Received level / ADC %FS / TX-RX gain | Not recorded with a validated voltage or full-scale conversion | Not recorded |

For a sinusoid actually measuring 800 mV peak across 50 Ω, the Red Pitaya setting corresponds to 0.566 V RMS or 6.4 mW (+8.06 dBm). The stated NanoVNA DDS level of 400 mV RMS across 50 Ω corresponds to 3.2 mW (+5.05 dBm). These are conversions of user-supplied levels, not independent measurements. The reported NanoVNA level at the antenna is 200 mV RMS; the corresponding Red Pitaya antenna level is unknown. The source levels therefore differ, and the receivers’ voltage scales and reflection-ratio conventions have not been cross-calibrated. Their complex values are not compared as absolute S11.

The **configured source-voltage ratio** Red Pitaya/NanoVNA at their respective 50 Ω outputs is `0.566/0.400 = 1.414` in RMS voltage. The reported NanoVNA antenna/source ratio is `0.200/0.400 = 0.5`. No Red Pitaya voltage at the antenna was captured, so there is no measured *antenna-level* amplitude ratio between readers.

The selected two-sensor NanoVNA capture used **100 Hz tracking bandwidth**. A 4 kHz NanoVNA file exists in an older one-sensor context dataset, but it did not use a verified matching setup and is excluded from the platform comparison. The baseline `16 kHz IF` setting and `100 Hz tracking bandwidth` describe different acquisition stages. On Red Pitaya, `WINDOW_SHIFT=20` means **1,048,576 samples per complex point** at 125 MS/s, or **8.388608 ms per point**. Ten point acquisitions for a five-point, two-sensor frame have an ideal window-time floor of **83.88608 ms**, before settling, readout, and processing. The observed mean interval was 87.13 ms. This integration time is not an experimentally measured equivalent noise bandwidth and should not be equated directly to NanoVNA’s 100 Hz setting.

The NanoVNA baseline fitted sensor 1 at **21,121,256 Hz**, Q **106.36**, model quality **0.9866**, and sensor 2 at **24,692,258 Hz**, Q **91.335**, quality **0.9900**. The Red Pitaya shift-20 baseline fitted **21,119,826 Hz**, Q **107.42**, quality **0.9971**, and **24,689,838 Hz**, Q **86.85**, quality **0.9931**. These centers agree within a few kilohertz; this supports the intended sensor correspondence but does not prove unchanged geometry or equal excitation. Both baselines reported frequency SE of zero. Their estimator searches have finite center resolution, and zero should not be interpreted as zero physical uncertainty.

## 3. Static frequency noise

For each unchanged tracking interval, `dF RMS` is the population root-mean-square deviation from that interval’s mean frequency, dividing by `N`. Sample SD divides by `N−1`. Internal SE is the estimator’s per-frame quantity averaged over the interval; it is kept separate from measured repeatability. The table gives kilohertz except for mean frequency.

| Reader and mode | Sensor | Complete estimates used | Mean frequency (MHz) | dF RMS (kHz) | Sample SD (kHz) | Mean internal SE (kHz) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| NanoVNA, five-point | 1 | 106 | 21.121501 | 4.132 | 4.151 | 7.015 |
| NanoVNA, five-point | 2 | 106 | 24.688131 | 4.674 | 4.696 | 6.994 |
| Red Pitaya, five-point, shift 20 | 1 | 230 | 21.120793 | 3.466 | 3.473 | 2.243 |
| Red Pitaya, five-point, shift 20 | 2 | 230 | 24.687462 | 3.939 | 3.947 | 5.662 |
| NanoVNA, three-point | 1 | 169 | 21.121597 | 5.851 | 5.868 | 12.754 |
| NanoVNA, three-point | 2 | 169 | 24.695524 | 4.085 | 4.097 | 6.681 |
| Red Pitaya, three-point, shift 20* | 1 | 250 of 383 backend sequences | 21.119167 | 1.283 | 1.286 | 3.333 |
| Red Pitaya, three-point, shift 20* | 2 | 250 of 383 backend sequences | 24.687334 | 3.800 | 3.808 | 2.904 |

\*The Red Pitaya three-point SD and dF RMS use only sequence-matched WebSocket frames (65.3% of backend sequences). They are **observed-subset statistics**, not established full-backend repeatability. Its apparent sensor-1 improvement over five-point tracking must not be treated as a mode effect.

In the two complete five-point records, the Red Pitaya dF RMS was about **0.84 times** the NanoVNA value for each sensor, at the settings above. That describes these recordings; source level, bandwidth, display activity, and exact geometry are not controlled well enough to attribute the difference to the platform.

![Five-point frequency noise by reader](output/setupA_frequency_noise.png)

*Figure 1.* Population dF RMS of every complete five-point frequency estimate in the selected captures.

Define the **frequency-noise ratio** as NanoVNA five-point dF RMS divided by Red Pitaya shift-20 five-point dF RMS for the same sensor. It is **1.192 / 1.187** for sensors 1 / 2. A ratio above one means higher observed frequency scatter in the selected NanoVNA capture. It is not sensor SNR, a complex reflection-ratio noise statistic, or an intrinsic reader-noise ratio; excitation and display conditions differ or are unverified.

![NanoVNA to Red Pitaya frequency-noise ratio](output/setupA_frequency_noise_ratio.png)

*Figure 2.* Dimensionless NanoVNA/Red Pitaya dF RMS ratio, computed from the selected five-point captures. Exact values are in the [ratio CSV](output/setupA_frequency_noise_ratio.csv).

![Frequency deviations over each static capture](output/setupA_frequency_traces.png)

*Figure 3.* Frequency deviation from each capture’s own mean. Every plotted estimate is retained. The Red Pitaya three-point plot contains only sequence-matched telemetry frames.

### Complex amplitude ratio and phase noise

For each complex tracking point, the Red Pitaya reports `Γ = reflected / incident` from coherent raw I/Q. The NanoVNA debug record is its raw measurement/reference ratio before calibration. The amplitude ratio is `|Γ| = hypot(Re Γ, Im Γ)` and phase is `atan2(Im Γ, Re Γ)` in degrees. At tracker offset zero, each frame samples a **moving center frequency**; the noise below includes any change caused by tracker motion or drift. Amplitude noise is the population RMS deviation of `|Γ|` from its mean. Relative amplitude noise divides that RMS by mean `|Γ|`. Phase noise is the population RMS of wrapped phase residuals around the phase of the mean complex ratio.

| Reader, five-point center sample | Sensor | Complex frames | Mean amplitude ratio `|Γ|` | Amplitude noise RMS | Relative amplitude noise | Mean phase (°) | Phase noise RMS (°) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Red Pitaya, shift 20 | 1 | 230 | 2.209694 | 0.00005649 | 0.00256% | 59.319 | 0.01038 |
| Red Pitaya, shift 20 | 2 | 230 | 2.286308 | 0.00013868 | 0.00607% | 73.145 | 0.01475 |
| NanoVNA, debug frame | 1 | 1 | 2.004477 | unavailable | unavailable | −36.128 | unavailable |
| NanoVNA, debug frame | 2 | 1 | 2.000058 | unavailable | unavailable | −44.330 | unavailable |

![Red Pitaya complex amplitude and phase scatter](output/setupA_amplitude_phase_noise.png)

*Figure 4.* Complex amplitude-ratio and phase noise from the complete Red Pitaya five-point series at offset zero. The [complex statistics CSV](output/setupA_complex_amplitude_phase.csv) also includes its incomplete-coverage three-point subset and the selected NanoVNA capture’s single debug frame. The NanoVNA timed tracking did not save repeated complex points, so NanoVNA amplitude and phase **noise** cannot be estimated. Ratios above unity and the different phase origins reflect uncalibrated internal response conventions; these values are not absolute reflection coefficients or directly comparable across readers.

## 4. Complete two-sensor frame rate

| Reader and mode | Rate (frames/s) | Mean observed interval (ms) | Interval sample SD (ms) | Timing basis |
| --- | ---: | ---: | ---: | --- |
| NanoVNA, five-point | 7.25 | 137.98 | 26.08 | 106 consecutive timestamped RTD records |
| NanoVNA, three-point | 11.73 | 85.26 | 24.14 | 169 consecutive timestamped RTD records |
| Red Pitaya shift 20, five-point | 11.48 | 87.13 | 28.01 | 230 consecutive, sequence-matched WebSocket frames; backend reports ~11.5/s |
| Red Pitaya shift 20, three-point | about 19.1 | unavailable | unavailable | Backend sequence growth during the requested 20 s; WebSocket retained 250/383 sequences |

Rates refer to a **complete pair of sensor estimates**, not the per-sensor point-acquisition rate. NanoVNA timestamps are serial receipt times; Red Pitaya five-point timestamps are WebSocket receipt times. Their interval SDs include host and transport jitter and are not pure hardware-acquisition jitter. The Red Pitaya five-point sequence was consecutive, and its observed 11.48 frames/s agrees with the backend’s roughly 11.5 frames/s. At these settings, that is about **1.58 times** the NanoVNA five-point rate. The Red Pitaya three-point rate is less directly timed because individual backend-frame timestamps were not exported.

The [frequency-noise versus frame-rate plot](output/setupA_noise_rate.png) is retained as a supplementary analysis figure. Its hollow Red Pitaya three-point markers identify noise calculated from a sampled WebSocket subset.

## 5. Signal, SNR, and measurement artifacts

No controlled second physical sensor state was available. **Sensor signal Δf, complex contrast |ΔΓ|, and sensor SNR are therefore unavailable.** Changing `WINDOW_SHIFT` or averaging changes the reader’s acquisition conditions; it is not a sensor-state signal. The machine-readable comparison table leaves these fields blank. When a repeatable state change is available, the specified frequency SNR is `|mean(f_state2) − mean(f_state1)| / sample_SD(f_state1)` under fixed geometry and settings. Internal SE must not replace the denominator.

The selected NanoVNA timed records contain frequency estimates but no repeated complex Γ points; its separate debug capture has one complex frame. Thus a matched **complex reflection-ratio noise** comparison cannot be calculated from these records. The frequency-noise ratio graph above is explicitly a *ratio of frequency noise values*.

The Red Pitaya setting sweep exposed a candidate-selection risk. At shift 17 with one coarse/refinement average, one baseline selected **24.729 and 24.895 MHz**, missing the ~21.12 MHz lobe, yet the backend marked it valid; the second fit’s model quality was **0.277**. That run’s tracking records are preserved but excluded from the sensor comparison. With three averages, the intended two lobes were recovered. This is one observed failure, not an estimated failure probability. At shift 17 and three averages the tracker ran near 67 five-point and 75 three-point backend frames/s, but only 34 and 97 complete matched WebSocket frames were retained; those noise estimates are not directly comparable to the complete shift-20 series.

The NanoVNA live mean Q changed from about **81/92** in five-point mode to **159/182** in three-point mode without a deliberate physical change. This sparse live Q diagnostic should not be interpreted as a physical Q shift. NanoVNA and Red Pitaya baseline SE values of zero also need an explicit validity/resolution indicator before use as uncertainty evidence. A known response feature near 25 MHz may affect candidate selection; these captures do not distinguish an antenna response from a reader artifact.

## 6. Conclusions and review items

The saved data support a **descriptive static comparison**: both readers tracked the two intended frequency regions, and the selected five-point captures contain consecutive complete two-sensor frames. In those captures the NanoVNA produced 7.25 frames/s with dF RMS 4.132/4.674 kHz, while the Red Pitaya at shift 20 produced 11.48 frames/s with dF RMS 3.466/3.939 kHz. This single matched-in-time capture pair is insufficient for a robust platform ranking. The three-point Red Pitaya noise estimate has incomplete WebSocket coverage.

Before treating this as a matched performance result, verify whether antenna/sensor positions and the recorded 0.15 m MMCX RF path stayed fixed when swapping readers; clarify the geometry entry `240mm (75cm)`; and establish NanoVNA display state for the selected run (display-off is **not verified**). The metadata records calibration at the coax output, but this does not establish calibration of the tracker’s raw ratio. Measured source, received, and ADC levels are still needed to compare operating points. A controlled physical sensor change is required for signal and SNR. None of these pending quantities has been filled with an inferred value.

### Reproducibility and source files

- [Machine-readable comparison](output/setupA_comparison_summary.csv), [frequency-noise ratio values](output/setupA_frequency_noise_ratio.csv), [complex amplitude/phase statistics](output/setupA_complex_amplitude_phase.csv), and [plot/CSV generator](build_comparison.py).
- [Selected NanoVNA raw serial log](output/setupA_nanovna_20260928_123520/raw.csv) and [summary](output/setupA_nanovna_20260928_123520/summary.json). Other NanoVNA captures are preserved but excluded from this analysis.
- [Red Pitaya shift-20 raw WebSocket log](output/setupA_redpitaya_static_shift20_avg3_5pt3pt_20260928_1230.jsonl), [sequence-matched estimates](output/setupA_redpitaya_static_shift20_avg3_5pt3pt_20260928_1230.csv), [complex points](output/setupA_redpitaya_static_shift20_avg3_5pt3pt_20260928_1230.points.csv), and [summary](output/setupA_redpitaya_static_shift20_avg3_5pt3pt_20260928_1230.summary.csv).
- [Updated setup metadata](setup_A_metadata.csv) and [comparison protocol](AGENTS_comparison_testing.md).

All analysis retains the raw files. No outlier rejection or smoothing of frequency estimates was applied. The existing validation script passes its reference-statistics and synthetic two-sensor checks; the live capture files were additionally checked for sequence continuity and expected point counts.
