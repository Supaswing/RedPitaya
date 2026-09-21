# Resonance tracker architecture

## Milestone 1 scope

The application is a thin Red Pitaya adapter around the external VNA register
contract. It does not implement resonance fitting or tracking yet.

```text
Browser
  -> Bazaar start/stop lifecycle (`resonance_tracker`)
  -> /wss WebSocket
  -> CDataManager parameters/signals
  -> resonance_tracker callbacks
  -> acquisition thread
  -> VNA register adapter
  -> /dev/mem register block at 0x40700000
```

The acquisition thread owns the mapped register block. Web callbacks publish
validated configuration through atomics. The worker publishes a complete
`TelemetrySnapshot` under one mutex; only the Red Pitaya callbacks copy that
snapshot into transport parameters and signals. This prevents the WebSocket
serializer from observing mismatched I/Q, frequency, sequence, or status fields.
Browser refresh cannot block measurement timing.

## Parameters

- `RT_RUN`: writable run/stop control.
- `RT_FREQUENCY_HZ`: requested integer frequency in Hz.
- `RT_TELEMETRY_MS`: web publication interval, default 50 ms.
- `RT_WINDOW_SHIFT`: requested integration exponent, 0-20. Integration length
  is `2^RT_WINDOW_SHIFT` samples at 125 MHz.
- `RT_STATE`: `STOPPED`, `RUNNING`, or `ERROR`.
- `RT_ERROR`: backend error text.
- `RT_SEQUENCE`: monotonically increasing valid acquisition sequence. It may
  advance by more than one between web updates.
- `RT_VALID`: complete ready/read result status.
- `RT_BUSY`: measurement in progress.
- `RT_OVERFLOW`: unavailable in the external hardware contract.
- `RT_REQUESTED_FREQUENCY_HZ` and `RT_EFFECTIVE_FREQUENCY_HZ`.
- `RT_INC_I`, `RT_INC_Q`, `RT_REF_I`, `RT_REF_Q` raw signed register values.
- `RT_INC_MAG`, `RT_INC_PHASE_DEG`, `RT_REF_MAG`, `RT_REF_PHASE_DEG`.
- `RT_ACQUISITION_RATE_HZ` and `RT_PUBLICATION_RATE_HZ`.
- `RT_EFFECTIVE_WINDOW_SHIFT`, `RT_INTEGRATION_SAMPLES`, and
  `RT_INTEGRATION_TIME_US` report the verified applied integration setting.
- `RT_R_REAL`, `RT_R_IMAG`, `RT_R_MAG`, and `RT_R_PHASE_DEG` describe the
  current complex reflection ratio; `RT_R_VALID` is false for a zero incident
  vector.
- `RT_STATS_COUNT` and `RT_RATIO_STATS_COUNT` report rolling-window occupancy.
- `RT_{INC,REF}_{I,Q}_{MEAN,STDDEV}` and
  `RT_R_{REAL,IMAG,MAG}_{MEAN,STDDEV}` contain rolling statistics.
- `RT_R_MEAN_PHASE_DEG` is the phase of the mean complex ratio.

Signals `RT_HISTORY_FREQUENCY`, `RT_HISTORY_INC_MAG`, and
`RT_HISTORY_REF_MAG` contain at most 128 recent valid points.

## Reuse decisions

Reused from `apps-tools/impedance_analyzer`: CMake/install layout, the
`rp_app_init`/`rp_app_exit` lifecycle, `CDataManager` parameter and signal
transport, a worker thread, and the WebSocket client pattern.

Not reused: impedance conversion, calibration model, LCR extension handling,
frequency sweep algorithm, and its UI. The VNA register adapter follows the
external `InterfaceVna` repository and is kept separate from the future tracker
engine.

## Rates and ownership

Acquisition is driven by valid FPGA windows and is not timer-driven by the
browser. `RT_ACQUISITION_RATE_HZ` measures completed acquisitions. History
sampling and WebSocket publication are bounded by `RT_TELEMETRY_MS`, default to
20 Hz, and `RT_PUBLICATION_RATE_HZ` reports the measured history-publication
rate. History is bounded to 128 points. A frequency change marks the old sample
invalid, restarts the generator, waits for settling, clears stale status, and
only then publishes a result bearing the new effective frequency.

## Statistical contract

`INC` and `REF` are the coherent incident/reference pair at one frequency; they
are independent of the future logical-sensor selection. The backend calculates

```text
R = (I_ref + j Q_ref) / (I_inc + j Q_inc)
```

using double-precision intermediates. Means and sample standard deviations use
at most the latest 128 valid acquisitions. I/Q statistics include every valid
acquisition. Ratio statistics exclude samples whose incident vector is exactly
zero. The window resets at each RUN transition and whenever frequency or
`WINDOW_SHIFT` changes, so statistics never combine operating points or
integration lengths. JavaScript only formats backend results.

## Deployment contract

The application builds `controllerhf.so`, the backend filename loaded by the
Bazaar/Nginx implementation in this repository. CMake places the shared library
and static web assets in the `resonance_tracker` package directory before the
directory is installed under `${INSTALL_DIR}/www/apps`. The root `Makefile`
target is `resonance_tracker`; the standalone target-device helper is
`apps-tools/resonance_tracker/build.sh`.
