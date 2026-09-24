# Resonance tracker architecture

## Control and ownership

```text
dashboard modules -> shared browser store -> one WebSocket
                                      |
                         parameter/signal adapter (main.cpp)
                                      |
             one acquisition worker + InstrumentStateMachine
                         /                         \
       BaselineAnalyzer / FrequencyTracker       RawIqAcquisition
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
published as valid. Tracking sensor results and their 3/5-point measurements
are likewise published as one sequence-stamped frame.

The frontend has one transport and one shared store. `ACTIVE_VIEW` exists only
in JavaScript. Dashboard `enter`, `update`, and `leave` methods render the store;
selecting a dashboard sends no hardware command. Only explicit buttons send an
`RT_COMMAND`.

The tuning dashboard is also browser-local. It combines the latest coherent
tracking estimate (falling back to the last valid baseline fit), the completed
baseline curve, and raw-IQ statistics. Its target frequency, target separation,
and tolerance are local display settings; changing them never changes FPGA or
tracker state. The view does not infer amplitude transfer or delivered power:
the current interface exposes incident and reflected/reference I/Q but no
transmission channel.

## State transitions

| Current state | Command/result | Next state |
| --- | --- | --- |
| STOPPED, RAW_IQ, BASELINE_READY, ERROR | start baseline | BASELINE_ACQUIRING |
| BASELINE_ACQUIRING | overview complete | RESONANCE_FINDING |
| RESONANCE_FINDING | valid candidates and fits complete | BASELINE_READY |
| BASELINE_ACQUIRING, RESONANCE_FINDING | cancel | STOPPED |
| STOPPED, RAW_IQ, BASELINE_READY | start diagnostics | DIAGNOSTICS |
| DIAGNOSTICS | complete/cancel | BASELINE_READY if a baseline exists, otherwise STOPPED |
| BASELINE_READY | start tracking | TRACKING |
| TRACKING, DEGRADED | good tracking frame | TRACKING |
| TRACKING, DEGRADED | poor tracking frame | DEGRADED |
| DEGRADED | three consecutive poor frames for a sensor | RELOCKING |
| RELOCKING | local model fit accepted | TRACKING |
| RELOCKING | both local scans rejected or 2 s deadline reached | SEARCHING, then BASELINE_ACQUIRING |
| BASELINE_ACQUIRING (recovery) | valid baseline and fits complete | BASELINE_READY, then TRACKING |
| BASELINE_ACQUIRING (recovery) | failed baseline | ERROR |
| SEARCHING, TRACKING, DEGRADED, RELOCKING | stop tracking | BASELINE_READY |
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
  4 cancel diagnostics, 5 start tracking, 6 stop tracking.
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
- `RT_TRACK_POINTS` accepts only 3 or 5. It is fixed while tracking is active.

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

- `RT_BASELINE_FREQUENCY`, `RT_BASELINE_RE`, `RT_BASELINE_IM`, and
  `RT_BASELINE_FILTERED_MAG`, and `RT_BASELINE_CURVATURE`. The filtered-magnitude
  and signed-curvature arrays are aligned with the overview arrays; unused edge
  samples are zero and are not plotted. Curvature is rendered in a separate
  zero-centered panel because its units and scale differ from magnitude.
- `RT_BASELINE_SIGNAL_SEQUENCE` is a one-element signal and must equal
  `RT_BASELINE_SEQUENCE` before the browser combines these arrays.
- `RT_CANDIDATE_LEFT_HZ`, `RT_CANDIDATE_RIGHT_HZ`,
  `RT_CANDIDATE_SCORE`, `RT_CANDIDATE_CURVATURE_AREA`,
  `RT_CANDIDATE_SELECTION_QUALITY`, and `RT_CANDIDATE_IS_INFLECTION`.
  Inflection and extrema candidates are accepted only when their derived
  `frequency/FWHM` lies in the temporary expected range 50-150. Inflection lobes
  are generated for both signed-curvature polarities because a coherent complex
  background can make the same resonance appear as a magnitude dip, peak, or
  shoulder. They are ranked using balanced curvature area: twice the smaller of
  the selected lobe area and the combined immediately adjacent opposite-polarity
  area. This suppresses isolated, near-zero crossing ripples. A hypothesis gets
  model-supported priority only when a local coarse complex fit succeeds and its
  fitted center remains inside the proposed zero-crossing interval; this prevents
  a side lobe from claiming a nearby resonance that merely falls inside its wider
  refinement window. Among model-supported hypotheses, balanced area remains the
  primary ordering metric. The legacy curvature/prominence score is the final
  tie-breaker. The coarse calculation uses only the already acquired overview
  points.
- Dense refinement tries candidates in ranked order. A candidate is accepted
  only when its complex-model explained fraction is at least 0.20. Otherwise the
  next candidate is refined, with at most three attempts per requested sensor.
  Thus retries add bounded RF acquisitions only when earlier fits are poor.
- A coarse hypothesis with zero complex-model support is discarded before
  ranking. This excludes broad side lobes from an out-of-range narrow feature.
- For selected inflection-pair candidates, the browser interpolates
  `RT_BASELINE_FILTERED_MAG` at the published left/right frequencies to mark the
  exact selected curvature crossings. Extrema-fallback boundaries are not
  labelled as inflection points.
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

Tracking scalars/signals:

- `RT_TRACK_SEQUENCE`, `RT_TRACK_POINTS_USED`, `RT_TRACK_SENSOR_COUNT`,
  `RT_TRACK_COMPLETE`, and `RT_TRACK_RECOVERY_REQUIRED` describe the latest
  complete frame. `RT_TRACK_RATE_HZ` is the number of complete multi-sensor
  tracking frames per second, averaged over elapsed windows of at least one
  second; it is not the individual coherent-point acquisition rate.
- Per-sensor arrays are `RT_TRACK_SENSOR_ID`, `RT_TRACK_FREQUENCY_HZ`,
  `RT_TRACK_Q`, `RT_TRACK_SE_HZ`, `RT_TRACK_NORMALIZED_RESIDUAL`,
  `RT_TRACK_TEMPLATE_GAIN`, `RT_TRACK_REQUESTED_SHIFT_HZ`,
  `RT_TRACK_APPLIED_SHIFT_HZ`, `RT_TRACK_LOSS_COUNTER`, and
  `RT_TRACK_FIT_VALID`.
- Point arrays are `RT_TRACK_POINT_SENSOR_ID`, `RT_TRACK_POINT_OFFSET`,
  `RT_TRACK_POINT_FREQUENCY_HZ`, `RT_TRACK_POINT_RE`, and
  `RT_TRACK_POINT_IM`. The browser groups them by sensor and signed offset.
- `RT_TRACK_SIGNAL_SEQUENCE` must equal `RT_TRACK_SEQUENCE` before any of these
  arrays are combined or displayed.

Three-point mode samples offsets -1, 0, +1. Five-point mode samples -2 through
+2 and solves the five-parameter linearized complex fit. Both modes report the
post-fit normalized residual, complex template gain magnitude, requested and
applied shift, residual-based frequency SE, and live Q. A frame is poor when
the fit is invalid/non-finite, the requested shift exceeds 0.40 spacing, the
normalized residual exceeds 0.10, or gain is below 0.25. Poor frames apply zero
shift; a good frame clears the loss counter. Three consecutive poor frames set
`RT_TRACK_RECOVERY_REQUIRED`.

The tracking dashboard retains at most 180 received complete frames per sensor
and draws frequency change relative to each sensor's first retained value. It
resets this browser-local history when the baseline sequence changes. Hidden
dashboards do not append points or redraw, and skipped web publications are not
misrepresented as measured frames; the backend frame-rate scalar remains
independent of the web publication rate.

## Rates and bounded rendering

Raw acquisition runs at the fastest ready-driven rate supported by the current
window. Web publication defaults to 20 Hz and remains independent. Raw history
is bounded to 128 points. Baseline has exactly 101 overview points and
diagnostics exactly five points. Hidden dashboards receive no render calls and
own no timers. The tuning view redraws only while visible. Its Acquire Baseline
button sends the existing baseline command using the current backend baseline
configuration. It observes completed, sequence-matched baseline publications
even while another dashboard is selected and saves up to 24 completed curves
in browser local storage. Each table checkbox controls one curve overlay; view
selection sends no hardware command. Pending, failed, rejected, and cancelled
attempts remain table rows without a curve. The table reports sensor 1. Fit
identifies the complex model; Quality is the backend explained residual
fraction. Noise is the RMS complex deviation of individual refinement windows
from each point's coherent mean in normalized reflection units. Local slope is
the magnitude of the fitted complex response difference across the two model
samples bracketing the resonance, divided by effective frequency spacing, in
1/Hz. SE is the existing frequency standard error in Hz. Raw I/Q is available
from the Advanced diagnostics navigation menu; its RUN control remains
independent of navigation.

`RT_WINDOW_SHIFT` is global hardware acquisition configuration and its control
is displayed in the application header. In `RAW_IQ`, a
change is applied and read back before the next published measurement and resets
rolling statistics. In an idle or baseline-ready state, it remains requested
configuration until the next raw, baseline, or diagnostics operation starts.
The control is locked during baseline, resonance-finding, and diagnostics so a
single result sequence cannot contain mixed integration lengths.

`RT_PERIOD_COUNT` is read from FPGA offset `0x0c` after ready and published with
the latest raw-IQ sample. It is the completed DDS-period count for that
integration window; the application no longer writes or substitutes a constant
value for this register.

`RT_TELEMETRY_MS` applies immediately to parameter and signal publication for
all dashboards. It also bounds raw-history point publication, but it does not
pace coherent FPGA measurements, baseline points, refinement averages, or
diagnostics acquisition.

## Scope boundary

Milestone 2B now includes bounded local relock. The first scan takes 11 coherent
complex points over ±1.5 baseline FWHM around the frozen tracking center; the
second takes 21 points over ±3 FWHM. Both clip to the baseline frequency
limits and require at least one FWHM of span. The same complex resonance model
used for baseline analysis fits each scan with the prior linewidth constrained
to 0.67–1.50 times its baseline value. Acceptance requires explained residual
fraction at least 0.20 and a center more than one scan step inside each edge.
The original five-point template, width, and spacing remain unchanged when a
local fit is accepted; only the tracked center and loss counter change.

The worker limits local recovery to two attempts, at most 32 windows per lost
sensor and two seconds total for the batch. The live adapter has a 10 ms
per-window ready timeout, and cancellation is checked before each point. The
per-sensor progress fields are `RT_RELOCK_ACTIVE`, `RT_RELOCK_SENSOR_ID`,
`RT_RELOCK_ATTEMPT`, `RT_RELOCK_PROGRESS`, `RT_RELOCK_FALLBACK`, and
`RT_RELOCK_REASON`. A failed local fit requests a complete baseline with the
configuration that produced the tracking template, then starts tracking again
only after that baseline is valid. A cancelled request never starts fallback.
An acquisition error enters `ERROR` with its reason. The previous complete
tracking frame remains sequence coherent while recovery runs.

## Milestone 3 two-resonance contract

The FPGA still supplies one incident/reference I/Q pair. Sensor 1 and sensor 2
are logical resonances in that response, not separate ADC inputs. The writable
`RT_SENSOR_ENABLE_MASK` uses bit 0 for sensor 1 and bit 1 for sensor 2. A zero
mask is allowed as an idle configuration, but baseline start rejects it. The
legacy `RT_BASELINE_SENSOR_COUNT` remains as a compatibility control; writes of
1 or 2 map to masks 1 or 3, respectively. The mask is authoritative when both
controls are present in a message. Enable changes are locked during active
baseline, diagnostics, and tracking operations. After a mask change, a new
baseline is needed before diagnostics or tracking; `RT_BASELINE_ACTIVE_MASK`
identifies the configuration used for the last completed scan. Browser Show
checkboxes affect rendering only and never write a hardware parameter.

With both resonances enabled, the analyzer assigns IDs by increasing fitted
frequency. With only sensor 2 enabled, the strongest accepted candidate gets ID
2. Thus identity can change when the enabled set changes; stable physical
sensor routing is unavailable in this FPGA contract. A new baseline sequence
invalidates the old results as before.

Complete baseline publications now include paired arrays
`RT_BASELINE_RESULT_SENSOR_ID`, `RT_BASELINE_RESULT_FREQUENCY_HZ`,
`RT_BASELINE_RESULT_Q`, `RT_BASELINE_RESULT_SE_HZ`, and
`RT_BASELINE_RESULT_MODEL_QUALITY`. They use the existing
`RT_BASELINE_SIGNAL_SEQUENCE` and `RT_BASELINE_VALID` guard. The older scalar
resonance fields still report the first result.

One diagnostics command acquires five offsets for each enabled resonance in
sequence and commits only after all points succeed. `RT_DIAG_SENSOR_COUNT` and
`RT_DIAG_POINT_SENSOR_ID` extend the existing offset/frequency/complex arrays;
all entries share `RT_DIAG_SEQUENCE`. The browser validates point counts,
sensor IDs, offsets, and `RT_DIAG_SIGNAL_SEQUENCE`. During tracking the same
view can display a complete `RT_TRACK_*` point frame without issuing a
diagnostics command. The selector and Show checkboxes filter displayed data.

`RT_TRACK_SENSOR_STATE` supplies per-sensor status with 1 = tracking,
2 = degraded, and 3 = lost/recovery required. The tracking dashboard keeps at
most 180 complete published frames per sensor. Its frequency mean and sample
SD, distribution histogram, and normalized-residual trend are browser summaries
of those received frames; skipped publications are not reconstructed. The
complex trajectory uses the current complete frame. Hidden dashboards do not
redraw or append history.
