# Next connection: Setup A measurement matrix

Keep antenna 1, the two sensors, their 6 cm / 7 cm positions, the 0.15 m MMCX path,
bridge, and tuning unchanged. Use the same 20–26 MHz baseline interval. Save
the untouched serial/WebSocket logs before comparing noise. The second 2026-09-28
NanoVNA run is excluded from the review report and is not a reference for this
matrix.

## NanoVNA

Prepare two firmware binaries from the NanoVNA checkout, both with
`AUDIO_SAMPLES_COUNT=48`: one with `AUDIO_ADC_FREQ_K=192`, one with `=384`.
The checkout defaults to 192 ksample/s and 16 kHz IF. Its `offset` command
changes IF at runtime; the requested IF is checked against the `RTB` baseline
record. The 192 ksample/s / 32 kHz IF combination is not yet hardware validated;
the script stops and retains the log if it does not produce a valid two-sensor
baseline. Record the SHA-256 of each binary. Flash between the two six-condition
blocks using the NanoVNA project's documented DFU procedure. Do not flash
during an active capture.

For each firmware image, run all six conditions:

| ADC sample rate | IF | Tracking bandwidth |
| ---: | ---: | ---: |
| 192 or 384 ksample/s | 16 or 32 kHz | 100, 1000, 4000 Hz |

The script saves one baseline, 20 seconds each of five-point and three-point
tracking, one complete complex debug frame, and 250 fixed-frequency `G` samples
at each fitted sensor center per condition. It checks the reported bandwidth,
IF, two centers, RTD count, RTM count, and `GAMMA_END`. At 48 samples per step,
an 8 kHz bandwidth request reports 4 kHz on the 192k build and 8 kHz on the
384k build; the script uses this as a sample-rate sanity check. The firmware
hash and operator flash confirmation provide the remaining provenance.

The checked-out NanoVNA command table has no enabled, readable display-state
command, but the user reports one in the device firmware. The capture script
requires its OFF command, status command, and an exact OFF reply pattern. It
sends and verifies them at every condition, then asks the operator to confirm
that the panel is dark. The command syntax is pending the user's device-specific
reply. A dark panel/status reply alone does not prove that all SPI writes stop;
that remains a separate firmware or logic-analyzer check.

From the Red Pitaya repository root, preview without touching the port:

```powershell
& C:\Users\bud\Orthsens\sdsi_reader\.venv\Scripts\python.exe apps-tools/resonance_tracker/comparison/capture_nanovna_matrix.py --sample-rate-khz 192 --firmware C:\path\to\nanovna_192k.bin --plan
```

After flashing and reconnecting COM11, run each block with its own image:

```powershell
& C:\Users\bud\Orthsens\sdsi_reader\.venv\Scripts\python.exe apps-tools/resonance_tracker/comparison/capture_nanovna_matrix.py --port COM11 --sample-rate-khz 192 --firmware C:\path\to\nanovna_192k.bin --display-off-command '<OFF_COMMAND>' --display-status-command '<STATUS_COMMAND>' --display-off-pattern '<OFF_REPLY_REGEX>'
& C:\Users\bud\Orthsens\sdsi_reader\.venv\Scripts\python.exe apps-tools/resonance_tracker/comparison/capture_nanovna_matrix.py --port COM11 --sample-rate-khz 384 --firmware C:\path\to\nanovna_384k.bin --display-off-command '<OFF_COMMAND>' --display-status-command '<STATUS_COMMAND>' --display-off-pattern '<OFF_REPLY_REGEX>'
```

Flash the 384k image **between** those two commands. Each block gets a unique
directory. Run `analyze_nanovna_matrix.py <block-directory>` afterward to make
`comparison_summary.csv` with frequency noise, frame rate, fixed-frequency
mean complex amplitude ratio, amplitude noise, and phase noise. The `G` samples
are held at one frequency per sensor; this avoids mixing moving tracker-center
motion into the complex noise measurement. Complex ratios are uncalibrated
internal response units, not absolute S11.

## Red Pitaya

Deploy the updated resonance_tracker app before running the matrix. It exposes
`RT_TRACK_AVERAGES` (1–8), a coherent **complex-ratio average at each tracking
frequency**. The capture script rejects an app without matching parameter
readback. Baseline coarse/refinement averages stay at three for every condition.
Tracking averages affect live tracking only and are fixed at each run start.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File apps-tools/resonance_tracker/comparison/run_rp_matrix.ps1 -Plan
powershell -NoProfile -ExecutionPolicy Bypass -File apps-tools/resonance_tracker/comparison/run_rp_matrix.ps1 -HostName rp-f0f8d5.local -TrackSeconds 20
```

The matrix includes shifts 20, 19, 18 and combinations with 1, 2, or 4
tracking averages. Shift 16 × 1 and shift 14 × 1 give nominal integration-time
reference points near 1 kHz and 4 kHz, respectively. The `-Plan` output lists
the exact integration times and rectangular-window white-noise ENBW estimates:
`125 MHz / (2 × 2^shift × averages)`. These ENBW values are **models**, not
measured effective bandwidth; settling, window gaps, complex normalization,
and correlated noise may change the result. Compare measured dF RMS and fixed
complex noise at the recorded operating points, not nominal bandwidth alone.

The runner creates unique `.jsonl` logs, `plan.csv`, and `results.csv`, and
stops on the first failed condition without overwriting earlier captures.
