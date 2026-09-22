# Resonance tracker architecture

## Control and ownership

```text
dashboard modules -> shared browser store -> one WebSocket
                                      |
                         parameter/signal adapter (main.cpp)
                                      |
             one acquisition worker + InstrumentStateMachine
                         /                         \
             BaselineAnalyzer              RawIqAcquisition
                         \                         /
                   RawIqMeasurementSource: R = REF / INC
                                      |
                         FPGA register block at 0x40700000
```

The worker is the only owner of acquisition sequencing. Web callbacks copy
validated controls into atomics and publish one mutex-protected telemetry
snapshot. A command carries a monotonically changing `RT_COMMAND_SEQUENCE`, so
a stale completion cannot satisfy a newer request. Baseline and diagnostic
arrays are replaced only after a complete acquisition; partial arrays are never
published as valid.

The frontend has one transport and one shared store. `ACTIVE_VIEW` exists only
in JavaScript. Dashboard `enter`, `update`, and `leave` methods render the store;
selecting a dashboard sends no hardware command. Only explicit buttons send an
`RT_COMMAND`.

## State transitions

| Current state | Command/result | Next state |
| --- | --- | --- |
| STOPPED, RAW_IQ, BASELINE_READY, ERROR | start baseline | BASELINE_ACQUIRING |
| BASELINE_ACQUIRING | overview complete | RESONANCE_FINDING |
| RESONANCE_FINDING | valid candidates and fits complete | BASELINE_READY |
| BASELINE_ACQUIRING, RESONANCE_FINDING | cancel | STOPPED |
| STOPPED, RAW_IQ, BASELINE_READY | start diagnostics | DIAGNOSTICS |
| DIAGNOSTICS | complete/cancel | BASELINE_READY if a baseline exists, otherwise STOPPED |
| STOPPED, BASELINE_READY, ERROR | start raw I/Q | RAW_IQ |
| Any state | stop | STOPPED |
| Any state | acquisition/analysis failure | ERROR |

Completions are legal only in their matching active state. New baseline
acquisition invalidates and clears the previous baseline publication. Cancelling
diagnostics retains the last complete diagnostic point set.

## Acquisition and analysis

`RawIqAcquisition` preserves the Milestone 1 register, reset, settling, ready,
and coherent four-register read behavior. `RawIqMeasurementSource` is the only
conversion layer used by sensor analysis:

```text
R = (I_ref + j Q_ref) / (I_inc + j Q_inc)
```

A zero incident vector is invalid rather than being represented as zero.
`BaselineAnalyzer` is hardware-independent and also accepts
`ReplayMeasurementSource`.

The Milestone 2A algorithm is ported from
`C:/Users/bud/Orthsens/sdsi_reader/resonance_tracker.c` and
`resonance_tracker.h`. Ported behavior includes the 101-point overview, three
complex averages, 11-point quadratic Savitzky-Golay smoothing, signed-curvature
candidate pairing, extrema fallback, overlap-aware ranking, 21-point
refinement, complex Lorentzian plus quadratic-background fit, replicate-based
frequency standard error, and five points at offsets -2 through +2. NanoVNA IF
bandwidth and hardware control are deliberately not ported.

Default baseline timing is 303 coherent windows for the overview, followed by
63 windows and five template windows per requested resonance. With one sensor
that is 371 windows plus DDS settling and software overhead. The actual duration
depends on `WINDOW_SHIFT`. Progress is weighted 70% overview, 2% candidate
finding, 20% refinement, and 8% template acquisition.

## Parameter contract

Existing Milestone 1 parameters retain their names. `RT_STATE` now maps to:

```text
0 STOPPED, 1 RAW_IQ, 2 BASELINE_ACQUIRING, 3 BASELINE_READY,
4 RESONANCE_FINDING, 5 DIAGNOSTICS, 6 SEARCHING, 7 TRACKING,
8 DEGRADED, 9 RELOCKING, 10 ERROR
```

Commands:

- `RT_COMMAND`: 1 start baseline, 2 cancel baseline, 3 start diagnostics,
  4 cancel diagnostics.
- `RT_COMMAND_SEQUENCE`: client command sequence; `RT_COMMAND_ACK` acknowledges
  receipt.
- `RT_BASELINE_START_HZ`, `RT_BASELINE_STOP_HZ`, and
  `RT_BASELINE_SENSOR_COUNT`: requested scan configuration. Until physical
  sensor multiplexing is defined, this count means the number of non-overlapping
  resonances to select; IDs are assigned in increasing-frequency order.
- `RT_BASELINE_OVERVIEW_POINTS` (15-501), `RT_BASELINE_FILTER_RADIUS` (1-25),
  `RT_BASELINE_COARSE_AVERAGES` (1-32), `RT_BASELINE_REFINE_POINTS` (5-101),
  and `RT_BASELINE_REFINE_AVERAGES` (1-32) configure the overview and dense
  refinement acquisitions. Averaging is coherent complex averaging. Filter
  radius is measured in coarse-scan points and defines a centered `2r+1`
  quadratic Savitzky-Golay window; a run requires at least `2r+5` overview
  points.

Baseline/result scalars:

- `RT_BASELINE_SEQUENCE`, `RT_BASELINE_PROGRESS`, `RT_BASELINE_COMPLETE`,
  `RT_BASELINE_VALID`. A completed rejected scan may be inspected but cannot be
  used for diagnostics or tracking.
- `RT_RESONANCE_COUNT`, `RT_RESONANCE_SELECTED`.
- `RT_RESONANCE_FREQUENCY_HZ`, `RT_RESONANCE_Q`,
  `RT_RESONANCE_FWHM_HZ`, `RT_RESONANCE_SE_HZ`, and
  `RT_RESONANCE_MODEL_QUALITY` describe the first selected result.
- `RT_RESONANCE_MODEL_VALID` distinguishes the primary complex-model fit from
  the curvature fallback. Model quality is zero when the fallback is used.

Baseline signals:

- `RT_BASELINE_FREQUENCY`, `RT_BASELINE_RE`, `RT_BASELINE_IM`.
- `RT_BASELINE_SIGNAL_SEQUENCE` is a one-element signal and must equal
  `RT_BASELINE_SEQUENCE` before the browser combines these arrays.
- `RT_CANDIDATE_LEFT_HZ`, `RT_CANDIDATE_RIGHT_HZ`,
  `RT_CANDIDATE_SCORE`. Inflection and extrema candidates are accepted only
  when their derived `frequency/FWHM` lies in the temporary expected range
  50-150.
- `RT_REFINE_SENSOR_ID`, `RT_REFINE_FREQUENCY_HZ`, `RT_REFINE_RE`, and
  `RT_REFINE_IM` publish measured dense-refinement points.
- `RT_MODEL_SENSOR_ID`, `RT_MODEL_FREQUENCY_HZ`, `RT_MODEL_RE`, and
  `RT_MODEL_IM` publish the complex fitted model at those points when the
  primary model is valid. `RT_FIT_FREQUENCY_HZ` and `RT_FIT_FWHM_HZ` mark each
  fitted center and width, including curvature-fallback results.

Diagnostics scalars/signals:

- `RT_DIAG_SEQUENCE`, `RT_DIAG_SENSOR_ID`, `RT_DIAG_COMPLETE`.
- `RT_DIAG_OFFSET`, `RT_DIAG_FREQUENCY_HZ`, `RT_DIAG_RE`, `RT_DIAG_IM`.
- `RT_DIAG_SIGNAL_SEQUENCE` must equal `RT_DIAG_SEQUENCE` before rendering.

Each diagnostic point is the RTM-like tuple `(sequence, sensor_id,
signed_offset, effective_frequency_hz, complex_response)`. The browser draws
only the current or last complete sequence.

## Rates and bounded rendering

Raw acquisition runs at the fastest ready-driven rate supported by the current
window. Web publication defaults to 20 Hz and remains independent. Raw history
is bounded to 128 points. Baseline has exactly 101 overview points and
diagnostics exactly five points. Hidden dashboards receive no render calls and
own no timers.

`RT_WINDOW_SHIFT` is global hardware acquisition configuration and its control
is displayed in the application header. In `RAW_IQ`, a
change is applied and read back before the next published measurement and resets
rolling statistics. In an idle or baseline-ready state, it remains requested
configuration until the next raw, baseline, or diagnostics operation starts.
The control is locked during baseline, resonance-finding, and diagnostics so a
single result sequence cannot contain mixed integration lengths.

`RT_TELEMETRY_MS` applies immediately to parameter and signal publication for
all dashboards. It also bounds raw-history point publication, but it does not
pace coherent FPGA measurements, baseline points, refinement averages, or
diagnostics acquisition.

## Scope boundary

The `SEARCHING`, `TRACKING`, `DEGRADED`, and `RELOCKING` values reserve the
Milestone 2B state contract. Continuous tracking, loss detection, and relocking
are not started by this Milestone 2A implementation.
