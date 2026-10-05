# NanoVNA / Red Pitaya comparison data

The current review document is [comparison_report_2026-09-29.md](comparison_report_2026-09-29.md).
It uses the saved two-sensor captures, machine-readable comparison and
frequency-noise ratio CSVs, plus complex amplitude/phase statistics. The
report shows four figures. `report_draft.md` is the earlier working note.
The report and `build_comparison.py` use only the NanoVNA capture in
`output/setupA_nanovna_20260928_123520/`; later NanoVNA captures remain
preserved but are excluded from all reported calculations and figures.

For the next hardware connection, use [next_connection_matrix.md](next_connection_matrix.md).
It gives the NanoVNA 192k/384k firmware blocks, 16k/32k IF and
100/1000/4000 Hz bandwidth matrix, plus the Red Pitaya shift/average matrix.
The scripts are `capture_nanovna_matrix.py`, `analyze_nanovna_matrix.py`, and
`run_rp_matrix.ps1`.

This directory follows `AGENTS_comparison_testing.md`. The intended
matched setup is the user-specified small antenna, Cser TBM, with sensor 1 at
6 cm and sensor 2 at 7 cm. `setup_A_metadata.csv` records known fields and
leaves unknown hardware details blank. Neither existing capture is verified as
this setup, so the `output/context_*` files are **context, not platform results
to compare against each other**.

The user states the Red Pitaya's default source amplitude is 800 mV peak into
50 ohms and is controlled by register offset `0x10`. The application writes
`0x0800` to that register on acquisition startup. This is a configured level;
the RF output voltage has not been independently measured. If the stated
800 mV peak is the voltage across 50 ohms for a sinusoid, it corresponds to
0.566 V RMS, 6.4 mW, or about +8.06 dBm. Do not use that conditional conversion
as a measured output power. The user reports NanoVNA DDS output of 8 mA into
50 ohms, corresponding to 400 mV RMS at the source and 200 mV RMS at the
antenna. The source amplitudes were not matched in the saved captures. The user
confirms no visible NanoVNA display content during the selected 28 September
capture and the 30 September matrix; LCD bus activity was not measured.

## Existing source data

- NanoVNA: `C:/Users/bud/Orthsens/sdsi_reader/measurements/repeat_246_20260915_115728/bw100/raw.csv`
  and `bw4000/raw.csv`. These are one-sensor, five-point tracking plus a
  fixed-frequency complex capture on 2026-09-15. The original run records
  24.66 MHz fixed frequency, 100 Hz or 4 kHz tracking bandwidth, and a 16 kHz
  IF setting. It did not measure a controlled sensor-state change.
- Red Pitaya: `../test-results/2026-09-23/raw_iq_noise_samples.csv`. This is a
  fixed-frequency raw-I/Q experiment near 30 MHz under several `WINDOW_SHIFT`
  settings. It has no per-acquisition timestamps or resonance-frequency frames.

Run `analyze.ps1` with the two raw files and an output directory. The script
produces `tracking_summary.csv`, `complex_noise_summary.csv`, and
`redpitaya_raw_iq_summary.csv`. It accepts an optional
`-RedPitayaTrackingCsv` with one row per sensor and the columns shown in
`fixtures/two_sensor_frames.csv`. That fixture is synthetic and must never be
reported as a hardware result. Its `dF RMS` is population RMS around the
capture mean; `frequency_sd_sample_hz` divides by `N-1`. Frame rate is
`1 / mean(timestamp difference)` from recorded RTD timestamps. The raw I/Q
covariance is calculated on the CPU from repeated windows; it is not an FPGA
covariance register. Blank signal and SNR fields mean they were not measured.

## Matched run required

1. Complete `setup_A_metadata.csv`: sensor IDs, the distance reference,
   antenna/coax/CMC/bridge/tuning details, display and calibration state.
2. Define two repeatable sensor states with no other physical change. Keep
   both sensors and antenna fixed for static noise runs; capture at least 20
   complete frames per state and platform.
3. Capture both platforms with timestamped, sequence-complete five-point
   frames and original complex points. Use the middle three of the same five
   frequencies for the three-point comparison. Record actual acquisition
   settings and full two-sensor frame timestamps.
4. Record configured source level, measured incident/reflected amplitudes and
   ADC full-scale use where the hardware exposes a valid scale. Keep gain and
   attenuation settings explicit; leave unavailable fields blank.
5. Calculate separate per-sensor repeatability SD, population dF RMS, and
   internal SE. Calculate `Delta f` from the two state means and SNR from the
   unchanged-state repeatability SD. Keep per-sensor and complete-frame rates
   distinct. Preserve every raw capture and flag artifacts without deleting
   points.

The Red Pitaya host `rp-f0f8d5.local` was reachable on 2026-09-28. A complete
two-sensor baseline and a 20-second 5-point plus 20-second 3-point static
tracking capture are preserved in `output/`. `extract_tracking.ps1` accepts only
sequence-matched complete telemetry frames and exports the corresponding
complex points; `summarize_telemetry.ps1` reports preliminary noise from that
observed subset. The backend rate is measured over one-second windows, but
individual backend frames lack timestamps. The WebSocket is decimated and
cannot establish acquisition jitter. Matched NanoVNA captures remain
subject to physical-setup confirmation. A fresh COM11 NanoVNA capture is in
`output/setupA_nanovna_20260928_123520/`; `analyze_nanovna_quick.py` produced
its `summary.json` and `tracking_summary.csv`. See `report_draft.md` for
current figures and limits.

For the quickest NanoVNA capture at the same fixed physical setup, connect the
reader to the Windows PC and run:

```powershell
& C:\Users\bud\Orthsens\sdsi_reader\.venv\Scripts\python.exe apps-tools/resonance_tracker/comparison/capture_nanovna_quick.py --port COM11
```

Replace `COM11` with the actual Device Manager port. The script uses 38,400
baud, sets two sensors and 100 Hz tracking bandwidth, scans 20–26 MHz once,
asks for confirmation of the two fitted centers, and records at least 20
timestamped RTD frames in each of 5-point and 3-point modes. It saves a new
`output/setupA_nanovna_*` directory and never overwrites a prior capture.
It also records one separate debug frame with the complex RTM points; debug
output stays off during timed tracking.
Record display activity, calibration state/plane, coax/CMC, and the distance
reference separately. Leave the sensors and antenna fixed throughout. The
tracking bandwidth remains at 100 Hz afterward; restore the previous value
manually if needed.

The `WINDOW_SHIFT=20`, three-average run supplied 230 consecutive complete
five-point two-sensor frames, so its arrival timing closely follows the
backend's frame rate. A one-average baseline at shift 17 selected two nearby
24.7–24.9 MHz candidates and missed the ~21.12 MHz lobe; those tracking
records are preserved but excluded from sensor noise figures. Baseline SE of
zero with one average means the between-replicate SE is unavailable.
