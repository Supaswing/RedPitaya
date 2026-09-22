# NanoVNA tracker porting inventory

## Reference state and provenance

- Source checkout: `C:/Users/bud/Orthsens/sdsi_reader`
- Reference commit: `2f4c66b740d5630dffe92b0a603b5f2a6c0f41c0`
  (`gitigonre change`)
- Reference tree was not clean: `nanovna_core.c` and `resonance_tracker.c`
  were modified, and `complex_slope_analysis/` and `measurements/` were
  untracked. These files were inspected read-only and were not modified by the
  Red Pitaya port.
- No standalone `LICENSE`, `COPYING`, or source-file license header was found in
  the reference checkout. `TODO(user)`: confirm that the NanoVNA tracker code
  may be redistributed in the Red Pitaya project. The C++ implementation is an
  adapted port rather than a verbatim file copy.

## Inventory

| NanoVNA file/symbol | Responsibility | Hardware-dependent? | Red Pitaya target | Reuse strategy | Test/golden-data source | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `resonance_tracker.c` constants | Two resonances; 101 coarse points; 3 coarse/refine averages; 21 refine points; 5 offsets; smoothing and width limits | IF bandwidth and sweep timing are hardware-specific | `resonance_analysis.*`, `BaselineConfig` | Preserve dimensions and dimensionless thresholds; omit NanoVNA delays/bandwidth settings | Synthetic captured complex replay | 2A ported |
| `rt_acquire` | Set frequency, acquire and normalize complex gamma | Yes | `ComplexMeasurementSource`, `RawIqMeasurementSource` | Replace with interface; calculate `REF/INC` only in the live adapter | Raw-IQ regression and replay source | 2A adapted |
| `rt_scan_iq`, `rt_scan_mag2` | Coarse baseline and coherent averaging | Acquisition adapted | `BaselineAnalyzer::acquire` | Average complex response, not magnitudes | `resonance_analysis_test` | 2A ported |
| 11-point SG loop in `resonance_tracker_baseline` | Smooth coarse magnitude squared | No | `BaselineAnalyzer::findCandidates` | Mechanically preserve coefficients and edge rules | Valid/multiple/missing replay cases | 2A ported |
| `rt_find_resonances`, `rt_find_extrema_candidates`, `rt_select_candidates` | Inflection pairing, extrema fallback, overlap-aware ranking | No | `BaselineAnalyzer::findCandidates` | Faithful C++ port using bounded vectors | Multiple-candidate replay | 2A ported |
| `rt_refine_resonance` | Dense candidate scan and refinement bounds | Acquisition adapted | `BaselineAnalyzer::acquire` | Preserve placement and midpoint clipping; acquire through interface | Replay tests | 2A ported |
| `rt_remove_quadratic_background`, `rt_resonance_score`, `rt_fit_complex_resonance_model` | Complex background removal and Lorentzian model search | No | `resonance_analysis.cpp` | Faithful double-precision adaptation | Valid resonance replay | 2A ported |
| `rt_complex_curvature`, `rt_quadratic` fallback | Estimate center/width when the complex model fails | No | `fitCurvatureFallback` | Preserve five-point quadratic and crossing behavior | Weak/non-model data still needs captured golden input | 2A ported; golden case pending |
| replicate loop in `rt_refine_resonance` | Frequency standard error | No | complex-model and curvature fit helpers | Preserve standard-error calculation; use `double` | Deterministic identical replicates and future noisy captures | 2A ported |
| `rt_prepare_complex_fit`, `rt_design_row`, `rt_fit_shift_3`, `rt_fit_shift_5` | Tracking template, gain, shift, residual and SE | No | Future tracker engine/fit module | Port only for 2B after golden comparison | NanoVNA captures and quality tests | 2B pending |
| `resonance_tracker_quality.c::resonance_tracker_evaluate_fit` | Quality thresholds, loss counter, apply-shift/recovery decision | No | Future `tracking_quality.*` | Direct typed adaptation with threshold tests | `test/Test_resonance_tracker.c` | 2B pending |
| `rt_local_relock`, `rt_recover_tracking` | Two bounded local relock attempts then full baseline | Frequency acquisition adapted | Future tracker engine | Preserve bounds/decision policy; replace acquisition and scheduling | Captures needed for success/failure cases | 2B pending |
| `RTB*` emitters in `resonance_tracker_baseline` | Baseline configuration, candidates, refine ranges, results, model and points | Serialization only | Structured baseline telemetry | Preserve units and meanings, not CSV internally | `doc/resonance_tracker.md` | 2A partial: structured equivalents, no UART adapter |
| `RTM` emitter in `resonance_tracker_process` | Coherent signed-offset complex frame | Serialization only | `DiagnosticResult` and diagnostic signals | Preserve sequence/id/offset/effective-frequency/complex tuple | Five-point diagnostic replay | 2A ported |
| `RTD`, `RTQ` emitters | Per-frame estimates and quality | Serialization only | Future tracking snapshot | Typed structures first; optional versioned CSV adapter | Tracking replay pending | 2B pending |
| `resonance_tracker_status` `RTS` emitter | `baseline_valid,running,sensors,points,debug,start,stop,f1,f2,q1,q2` | Serialization/status | Instrument state plus future tracking telemetry | Do not reuse NanoVNA display state as hardware state | State-machine tests | 2A state model ported; full fields pending |
| static fixed work buffers | Allocation and ownership | NanoVNA memory-layout-specific | Worker-owned vectors/results | Preserve bounded sizes, not fixed global layout | Unit tests and target memory observation | Adapted |

## Record compatibility

The checked-out emitter confirms:

```text
RTB,3,sensors,start_hz,stop_hz,scan_points,if_hz,
    baseline_bandwidth_hz,tracking_bandwidth_hz,coarse_averages,raw_complex
RTB_INF,id,left_hz,right_hz,fwhm_hz,score
RTB_REF,id,start_hz,stop_hz,points,averages
RTB_RES,id,f_hz,q,fwhm_hz,spacing_hz,f_se_hz,quad_a,quad_b,quad_c
RTB_MODEL,id,explained_residual_fraction
RTB_PT,id,offset,frequency_hz,gamma_re,gamma_im
RTB_DONE,f1_hz,f2_hz,q1,q2
RTM,sequence,id,offset,frequency_hz,gamma_re,gamma_im
RTD,sequence,f1_hz,q1,f1_se_hz,f2_hz,q2,f2_se_hz,points
RTQ,sequence,id,normalized_residual,template_gain,requested_shift_hz,se_hz,loss_counter
RTS,baseline_valid,running,sensors,points,debug,start_hz,stop_hz,f1_hz,f2_hz,q1,q2
```

`RTP`, `RTA`, and `RTW` are also emitted for relock progress, acceptance, and
warnings. They are 2B inputs and must be inventoried before adding a compatible
serialization adapter.

## Numerical and hardware boundary

The NanoVNA code uses `float`, fixed buffers, `freq_t`, sweep control, IF
bandwidth, and cooperative service timing. The Red Pitaya analysis uses `double`
internally and bounded vectors, keeps requested and DDS-effective frequency
separate, and acquires through one hardware-neutral source. NanoVNA bandwidth
numbers and delays are not transferred. The Red Pitaya `WINDOW_SHIFT`, ready
flag, DDS settling, timeout, raw signed I/Q, and zero-incident validation remain
inside the existing raw-IQ path and its adapter.

## Golden-data gaps

Current replay tests generate deterministic captured complex traces and cover a
valid resonance, two candidates, no acceptable resonance, cancellation, and an
RTM-like five-point frame. They do not yet use a checked-in real NanoVNA or Red
Pitaya capture. Before threshold tuning or Milestone 2B, add representative
captured data for noisy curvature, nearby candidates, weak coupling, rapid
shift, degraded fit, temporary loss, successful relock, and failed relock, with
expected records and explicit tolerances.
