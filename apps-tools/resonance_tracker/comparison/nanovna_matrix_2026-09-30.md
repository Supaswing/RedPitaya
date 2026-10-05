# NanoVNA COM11 matrix, 30 September 2026

COM11 responded with firmware version `2.1.1`. Maximum bandwidth probes returned 4, 8, and 16 kHz for the successive 192, 384, and 768 ksample/s blocks, consistent with 48 samples per step. After the user's 768k update, the local `sdsi_reader/build/ch.bin` had SHA-256 `bf5e5b28b5528d6001becc41159a713a593b54899745003cd7f1e9fed0155e52`. The probe verifies the nominal sample-rate setting, but the exact flashed binary hash cannot be read back and is not independently confirmed.

The capture requested two sensors, a 20–26 MHz baseline, 100/1000/4000 Hz tracking bandwidth, and 16/32 kHz IF at each sample rate. Each of the 18 completed conditions contains a baseline, at least 20 seconds each of five-point and three-point tracking, and 250 fixed-frequency complex samples per sensor. Some 768k debug frames were interrupted by relock; their completeness is recorded in the manifests. The serial `offset` command has no readback; the `RTB` field prints the compile-time IF constant (16 kHz in the 192k build, 32 kHz in the 384k and 768k builds) even after a runtime offset change. Runtime IF is therefore **requested, not verified**. A change in mean complex magnitude from approximately 2.00 to 2.24 between IF requests is consistent with a changed operating point, but is not proof of the actual IF.

The user confirms that the NanoVNA display **was not displaying** during the measurement series. This is an operator observation, not a captured firmware-state bit: the device's `help` list contains no `display` command and `display` returned `display?`. The observation establishes that no visible screen content was shown; it does not establish whether the backlight was off or whether LCD SPI transfers stopped. The setup geometry and calibration state were not independently rechecked during this run. The excluded second NanoVNA run from 28 September was not used.

## Quality finding

The 192k five-point and three-point windows had **zero relock events**. Across the six 20-second five-point conditions, the 384k run had **245** relock-begin events and the 768k run had **281**; the three-point windows had 185 and 527 respectively. These counts cover both sensors, counted once per event. Consequently, the 384k and 768k tracked-frequency RMS values below include loss and relock motion and **cannot be compared as stationary frequency noise**. They remain useful as observed tracking spread. The corresponding raw records contain `RTW` loss warnings and `RTP` relock events; this is a measured tracker stability problem, not a proven ADC noise increase.

Because the user observed no screen content during the series, visible display rendering cannot by itself explain the rise in relocks from 192k to 384k/768k. Fixed-frequency relative amplitude and phase noise generally remain on the same order across rates at a given requested bandwidth, while relocks rise sharply; this points to tracking, fitting, timing, or acquisition behavior as useful next checks, not to a proven intrinsic ADC-noise increase. Sample-rate behavior, integration timing, baseline bandwidth, IF behavior, and any remaining LCD bus activity have not been isolated.

At 768k, baseline selection also degraded. Three of six selected sensor 1 centers outside its earlier ~21.1 MHz region; the 16 kHz / 100 Hz run selected 24.63 and 25.43 MHz for sensors 1 and 2. Sensor 1 baseline model scores ranged **0.199–0.584**, compared with **0.856–0.947** at 384k and **0.955–0.993** at 192k. The 768k baseline reported **200 Hz** bandwidth, versus **100 Hz** in the earlier blocks, because its fixed baseline bandwidth setting quantizes differently. This is a material mismatch in the baseline search conditions.

[Noise and relock graph](output/setupA_nanovna_matrix_noise_quality_20260930.png) plots the three rates. Its frequency panels show observed spread including relocks. Its 768k amplitude and phase panels use the separate common-frequency capture described below.

## Complete two-sensor frame rate

Each rate is `(RTD frames - 1) / (last serial receipt time - first serial receipt time)` over one tracking window of at least 20 seconds. Each `RTD` record contains both sensor estimates, so this is a complete **two-sensor** frame rate. `5 / 3` in a cell denotes five-point / three-point tracking, in frames/s.

| Requested IF | Tracking BW | 192 ksample/s: 5 / 3 | 384 ksample/s: 5 / 3 | 768 ksample/s: 5 / 3 |
| ---: | ---: | ---: | ---: | ---: |
| 16 kHz | 100 Hz | 7.3 / 11.7 | 6.8 / 11.2 | 2.4 / 8.5 |
| 16 kHz | 1 kHz | 35.9 / 47.7 | 27.3 / 34.4 | 15.3 / 19.4 |
| 16 kHz | 4 kHz | 49.9 / 48.9 | 32.4 / 37.7 | 29.9 / 17.4 |
| 32 kHz | 100 Hz | 7.2 / 11.7 | 7.6 / 12.3 | 6.6 / 8.9 |
| 32 kHz | 1 kHz | 35.9 / 47.9 | 26.0 / 34.4 | 12.4 / 17.7 |
| 32 kHz | 4 kHz | 49.8 / 48.4 | 32.9 / 34.7 | 36.0 / 30.7 |

The 192k windows had no relocks and provide the cleanest throughput measurements. All 384k and 768k conditions had at least one relock; these observed rates include time spent detecting loss and relocking, so they are **effective tracker throughput**, not raw acquisition capacity. Times come from serial receipt at 38,400 baud and can also be limited by output/host transport, especially near 50 frames/s. The exact counts, timing-based rates, loss events, and sequence checks for each sensor and point mode are in the combined CSV.

## 192 ksample/s: five-point tracking and fixed-frequency noise

Frequency noise is the population RMS spread of the tracked frequency over the recorded 20-second window; it includes drift and tracker motion. Relative amplitude noise is population RMS of `|gamma|` divided by mean `|gamma|`; phase noise is circular phase residual RMS. The latter two use the separate 250-sample fixed-frequency `gamma` capture. Amplitude is in uncalibrated tracker complex units, not volts or calibrated S11.

| Requested IF | BW | Sensor | Frequency RMS | Mean `|gamma|` | Relative amplitude RMS | Phase RMS | Frames/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 kHz | 100 Hz | 1 | 5,413 Hz | 2.0033 | 2.06e-5 | 0.0012° | 7.3 |
| 16 kHz | 100 Hz | 2 | 6,816 Hz | 2.0017 | 2.63e-5 | 0.0014° | 7.3 |
| 16 kHz | 1 kHz | 1 | 12,717 Hz | 2.0028 | 5.21e-5 | 0.0028° | 35.9 |
| 16 kHz | 1 kHz | 2 | 14,320 Hz | 2.0015 | 6.00e-5 | 0.0033° | 35.9 |
| 16 kHz | 4 kHz | 1 | 14,508 Hz | 2.0024 | 1.30e-4 | 0.0056° | 49.9 |
| 16 kHz | 4 kHz | 2 | 14,528 Hz | 2.0009 | 1.36e-4 | 0.0053° | 49.9 |
| 32 kHz* | 100 Hz | 1 | 5,646 Hz | 2.2367 | 6.54e-5 | 0.0017° | 7.2 |
| 32 kHz* | 100 Hz | 2 | 6,025 Hz | 2.2352 | 2.84e-5 | 0.0011° | 7.2 |
| 32 kHz* | 1 kHz | 1 | 11,011 Hz | 2.2357 | 6.65e-5 | 0.0028° | 35.9 |
| 32 kHz* | 1 kHz | 2 | 12,817 Hz | 2.2340 | 6.12e-5 | 0.0035° | 35.9 |
| 32 kHz* | 4 kHz | 1 | 12,863 Hz | 2.2353 | 1.18e-4 | 0.0052° | 49.8 |
| 32 kHz* | 4 kHz | 2 | 13,479 Hz | 2.2338 | 1.14e-4 | 0.0056° | 49.8 |

*Runtime IF is not verified by the firmware's `RTB` record.* The mean amplitude ratio sensor 1 / sensor 2 is 1.0006–1.0008 across these conditions. This ratio compares the raw internal response magnitudes and is not an excitation or received-voltage ratio.

## 384 ksample/s: five-point tracking and fixed-frequency noise

The same definitions apply, but five of six conditions relocked repeatedly. The 42.5 kHz frequency RMS for sensor 1 at requested 32 kHz IF / 4 kHz bandwidth includes relock motion and is not a stationary noise estimate.

| Requested IF | BW | Sensor | Frequency RMS | Mean `|gamma|` | Relative amplitude RMS | Phase RMS | Frames/s |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 kHz* | 100 Hz | 1 | 11,637 Hz | 2.0062 | 2.89e-5 | 0.0011° | 6.8 |
| 16 kHz* | 100 Hz | 2 | 14,011 Hz | 2.0043 | 3.06e-5 | 0.0014° | 6.8 |
| 16 kHz* | 1 kHz | 1 | 15,398 Hz | 2.0053 | 6.24e-5 | 0.0032° | 27.3 |
| 16 kHz* | 1 kHz | 2 | 12,506 Hz | 2.0032 | 7.50e-5 | 0.0046° | 27.3 |
| 16 kHz* | 4 kHz | 1 | 23,091 Hz | 2.0055 | 1.16e-4 | 0.0077° | 32.4 |
| 16 kHz* | 4 kHz | 2 | 19,855 Hz | 2.0032 | 1.52e-4 | 0.0072° | 32.4 |
| 32 kHz | 100 Hz | 1 | 10,033 Hz | 2.2337 | 3.45e-5 | 0.0013° | 7.6 |
| 32 kHz | 100 Hz | 2 | 10,862 Hz | 2.2311 | 3.76e-5 | 0.0014° | 7.6 |
| 32 kHz | 1 kHz | 1 | 11,453 Hz | 2.2327 | 6.57e-5 | 0.0035° | 26.0 |
| 32 kHz | 1 kHz | 2 | 23,381 Hz | 2.2311 | 7.32e-5 | 0.0037° | 26.0 |
| 32 kHz | 4 kHz | 1 | 42,503 Hz | 2.2324 | 1.01e-4 | 0.0078° | 32.9 |
| 32 kHz | 4 kHz | 2 | 15,655 Hz | 2.2307 | 1.12e-4 | 0.0083° | 32.9 |

*The 384k build's compile-time IF is 32 kHz; the 16 kHz runtime request has no readback.* The 384k amplitude ratio sensor 1 / sensor 2 is 1.0007–1.0012.

## 768 ksample/s: tracking stability and common-frequency complex noise

The 768k tracker often relocked and sometimes chose the wrong baseline center. For a comparable complex-noise measurement, a separate run acquired 250 `gamma` samples at **21,125,536 Hz** for sensor 1 and **24,635,208 Hz** for sensor 2 under each IF and bandwidth setting. These are the centers from the earlier 192k / 16 kHz / 100 Hz baseline. The frequencies are fixed across all six 768k conditions. This reference run does not rely on the 768k baseline's candidate choice.

| Requested IF | BW | Baseline center S1 / S2 (MHz) | Five-point relocks | Tracked frequency RMS S1 / S2 (kHz) | Relative amplitude RMS S1 / S2 | Phase RMS S1 / S2 (degrees) |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 16 kHz | 100 Hz | 24.633 / 25.433 | 29 | 804.6 / 199.6 | 5.38e-5 / 2.93e-5 | 0.0025 / 0.0015 |
| 16 kHz | 1 kHz | 22.673 / 24.681 | 64 | 547.8 / 90.2 | 6.40e-5 / 6.44e-5 | 0.0035 / 0.0038 |
| 16 kHz | 4 kHz | 22.658 / 24.676 | 75 | 200.5 / 40.1 | 1.28e-4 / 1.33e-4 | 0.0082 / 0.0086 |
| 32 kHz | 100 Hz | 21.116 / 24.650 | 13 | 32.2 / 16.9 | 3.94e-5 / 3.27e-5 | 0.0016 / 0.0014 |
| 32 kHz | 1 kHz | 23.900 / 24.648 | 56 | 683.9 / 59.6 | 5.90e-5 / 6.44e-5 | 0.0038 / 0.0074 |
| 32 kHz | 4 kHz | 21.116 / 24.655 | 44 | 12.5 / 163.0 | 1.07e-4 / 1.27e-4 | 0.0084 / 0.0076 |

The 768k fixed-frequency mean amplitudes are approximately **2.002–2.005** at requested 16 kHz IF and **2.232–2.234** at requested 32 kHz IF, in uncalibrated internal units. The sensor 1 / sensor 2 mean-amplitude ratio is **1.0007–1.0013**. The separate noise-ratio CSV divides 768k relative amplitude and phase RMS by the corresponding 192k and 384k values; it is descriptive because those earlier captures used each run's fitted center rather than these exact reference frequencies. Even the 768k conditions with plausible baseline centers had relocks; no 768k tracked-frequency row qualifies as a clean stationary-noise comparison.

## Files and remaining measurements

- [Combined summary](output/setupA_nanovna_matrix_192k_384k_768k_20260930.csv) — all 72 sensor × point-mode rows across three rates, including loss/relock and sequence-gap counts.
- [768k common-frequency complex-noise summary](output/setupA_nanovna_reference_gamma_768k_20260930_102353/reference_gamma_summary.csv) and [raw-capture manifest](output/setupA_nanovna_reference_gamma_768k_20260930_102353/manifest.json).
- [768k / earlier-rate amplitude and phase noise ratios](output/setupA_nanovna_768k_noise_ratios_20260930.csv) — per IF, bandwidth, and sensor.
- [192k 16 kHz request raw block](output/setupA_nanovna_192k_20260930_065630/manifest.json), [192k 32 kHz request raw block](output/setupA_nanovna_192k_20260930_070127/manifest.json), [384k raw block](output/setupA_nanovna_384k_20260930_074601/manifest.json), [768k initial block](output/setupA_nanovna_768k_20260930_100917/manifest.json), and [768k retry block](output/setupA_nanovna_768k_20260930_101714/manifest.json) — condition manifests and untouched serial CSVs. Failed attempts remain in the block directories but are excluded from the 72-row summary.
- The user confirms no visible display content during both the selected 28 September capture and this matrix. Verifying LCD power or SPI inactivity would require firmware status or an electrical measurement. The NanoVNA firmware also needs an IF getter or direct IF readback to prove runtime IF changes.

The analyzer reference test passed with `python test_analyze_nanovna_matrix.py`. The capture script checked bandwidth replies, two resonance centers, tracking record counts, and exact 250-sample fixed-frequency runs per sensor. It did not establish a physical sensor-state signal or SNR because no controlled state change was available. The next diagnostic priority is to identify why higher sample-rate builds repeatedly lose tracking and why the 768k baseline fits degrade before comparing frequency noise or retuning thresholds.

The earlier [NanoVNA–Red Pitaya comparison](comparison_report_2026-09-29.md) uses a separate 28 September 192k/100 Hz NanoVNA capture and a Red Pitaya shift-20 capture. The user confirms no visible display content in that NanoVNA capture too. Its source-level, geometry, effective-bandwidth, and unverified LCD bus-state limits still apply. No Red Pitaya condition matching the new 384k or 768k operating points was available for a valid platform noise ratio; the higher-rate relocks further prevent a stationary tracked-frequency comparison.
