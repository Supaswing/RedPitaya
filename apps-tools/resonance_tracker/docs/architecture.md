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

The acquisition thread owns the mapped register block. Web callbacks only copy
validated configuration into the thread's parameter objects. Telemetry is sent
as scalar parameters plus bounded signal histories; browser refresh cannot block
measurement timing.

## Parameters

- `RT_RUN`: writable run/stop control.
- `RT_FREQUENCY_HZ`: requested integer frequency in Hz.
- `RT_TELEMETRY_MS`: web publication interval, default 50 ms.
- `RT_STATE`: `STOPPED`, `RUNNING`, or `ERROR`.
- `RT_ERROR`: backend error text.
- `RT_SEQUENCE`: software publication sequence.
- `RT_VALID`: complete ready/read result status.
- `RT_BUSY`: measurement in progress.
- `RT_OVERFLOW`: unavailable in the external hardware contract.
- `RT_REQUESTED_FREQUENCY_HZ` and `RT_EFFECTIVE_FREQUENCY_HZ`.
- `RT_INC_I`, `RT_INC_Q`, `RT_REF_I`, `RT_REF_Q` raw signed register values.
- `RT_INC_MAG`, `RT_INC_PHASE_DEG`, `RT_REF_MAG`, `RT_REF_PHASE_DEG`.
- `RT_ACQUISITION_RATE_HZ` and `RT_PUBLICATION_RATE_HZ`.

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
browser. Publication is bounded by `RT_TELEMETRY_MS` and defaults to 20 Hz.
History is bounded to 128 points. A frequency change restarts the generator,
waits for settling, clears stale status, and only then starts a new measurement.

## Deployment contract

The application builds `controllerhf.so`, the backend filename loaded by the
Bazaar/Nginx implementation in this repository. CMake places the shared library
and static web assets in the `resonance_tracker` package directory before the
directory is installed under `${INSTALL_DIR}/www/apps`. The root `Makefile`
target is `resonance_tracker`; the standalone target-device helper is
`apps-tools/resonance_tracker/build.sh`.
