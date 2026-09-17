# Red Pitaya Resonance Tracker — Codex Working Guide

## Mission

Build a self-contained Red Pitaya web application for a two-sensor resonance
tracker. The application will use the FPGA raw coherent I/Q measurement already
added to this repository, run search/fitting/tracking on the ARM CPU, and expose
configuration, telemetry, and diagnostics in the browser.

Create the application at:

```text
apps-tools/resonance_tracker/
```

Use `apps-tools/impedance_analyzer/` as a reference for Red Pitaya application
lifecycle, build integration, parameter/signal transport, WebSocket use, and UI
patterns. Do not reuse its impedance-measurement algorithm or calibration model.

## Mandatory first step: inspect before editing

This guide does not know the exact names or layout of the locally added FPGA
raw-IQ registers. Do not guess them.

Before creating or modifying code:

1. Read the repository-level and nearest directory-level `AGENTS.md` files.
2. Inspect `git status`, the current branch, and the diff against its upstream or
   merge base. Preserve all existing user changes.
3. Locate the raw-IQ FPGA modifications and trace their complete data path:
   HDL/register definition -> address map -> low-level C API -> current caller or
   test code.
4. Inspect the actual `apps-tools/impedance_analyzer` implementation and the
   repository's current web-app examples. The checked-out source is authoritative;
   do not assume an API from older Red Pitaya documentation.
5. Write a short discovery note in
   `apps-tools/resonance_tracker/docs/raw_iq_interface.md` that records:
   - source files and relevant symbols;
   - register names, addresses/offsets, widths, signedness, scaling, and units;
   - accumulator/sample count and overflow behavior;
   - ready/busy/valid/sequence semantics;
   - reset, trigger, and frequency-change sequencing;
   - atomicity requirements when reading I and Q;
   - expected I/Q convention and phase/sign convention;
   - maximum safe update rate and known limitations;
   - any unanswered question marked explicitly as `TODO(user)`.

Useful discovery commands include `rg`, `git diff`, `git log -p`, and inspection
of generated register headers. Avoid broad generated/build directories.

Stop and ask the user if the raw-IQ interface cannot be identified unambiguously,
or if using it would require changing the FPGA register contract.

## Target architecture

Keep hardware access, tracking, transport, and presentation separate:

```text
Browser UI
  -> Red Pitaya parameter/signal transport
  -> thin web-app adapter
  -> tracker engine
  -> raw-IQ acquisition adapter
  -> existing FPGA register/API interface
```

Suggested source organization (adapt it to the conventions actually present in
the checked-out Red Pitaya tree):

```text
apps-tools/resonance_tracker/
  Makefile
  index.html
  info/
  css/
  js/
  src/
    main.cpp                 # Red Pitaya app lifecycle and thin adapter only
    raw_iq_acquisition.*     # FPGA/API access, scaling, coherent snapshots
    tracker_engine.*         # state machine and orchestration
    resonance_fit.*          # fitting/model code
    tracking_quality.*       # residuals, SE, loss/relock criteria
    telemetry_snapshot.*     # immutable UI-facing snapshot
  docs/
    raw_iq_interface.md
    architecture.md
```

Do not put DSP, fitting, or tracker state-machine logic in JavaScript. Do not put
HTML/WebSocket concerns in the tracker engine. `main.cpp` should remain a thin
Red Pitaya-specific adapter.

## Measurement constraints

- Hardware: Red Pitaya STEMlab 125-14 Gen 2 unless repository evidence says this
  branch targets another board.
- ADC/DAC clock: nominally 125 MS/s; obtain the effective clock and frequency
  tuning rules from the existing implementation rather than hard-coding
  assumptions.
- Primary operating region: approximately 30-34 MHz.
- The reflected signal is measured while the excitation/reference is coherent.
- The FPGA now provides raw coherent I and Q. Preserve raw integer values for
  diagnostics and convert to normalized/scaled values in one documented layer.
- The product goal is resonance variation/perturbation detection, not
  metrology-grade absolute impedance.
- Support two logical sensors, with each sensor independently hideable/disableable
  in the UI.
- Browser activity must not control or block measurement timing.

Do not change FPGA logic, register addresses, bit widths, or generated register
files as part of the initial web application unless the user explicitly approves
that scope.

## Timing and concurrency rules

Treat acquisition/tracking and browser telemetry as different rates:

- acquisition/DSP: fastest stable rate supported by the existing IQ interface;
- tracking estimator: driven by valid coherent measurements;
- web telemetry: initially 10-20 Hz, configurable later if justified;
- plots: decimate/bound history so the browser cannot create unbounded memory use.

Publish UI data through a coherent snapshot. A browser update must not observe a
partially updated pair of sensors or mismatched I/Q/sequence values. Follow the
locking/threading conventions already used by this Red Pitaya application tree.
Do not add a new concurrency framework without need.

After changing excitation frequency, obey the discovered FPGA/DDS settling and
accumulator reset/valid sequence. Never label stale I/Q as belonging to the new
frequency.

## Internal data model

Define typed internal structures rather than passing UART/CSV strings internally.
At minimum, plan for these concepts:

- `RawIqSample`: sequence, sensor/channel, frequency, raw I, raw Q, integration
  or sample count, validity/status, timestamp if available.
- `TrackerConfig`: enabled sensors, frequency/search ranges, points, averaging,
  tracking bandwidth, relock settings, and web update rate.
- `SensorEstimate`: frequency, Q, FWHM, standard error, complex response, and
  validity.
- `TrackingQuality`: normalized residual, template gain, requested shift in Hz,
  frequency SE, loss counter, and tracker state.
- `TelemetrySnapshot`: one coherent publication object for all UI-visible fields.

Preserve compatibility at the conceptual/data level with the existing diagnostic
records, where applicable:

```text
RTB, RTB_INF, RTB_REF, RTB_RES, RTB_MODEL, RTB_PT,
RTM, RTD, RTS, RTQ
```

UART/CSV output may remain as a serialization adapter, but the browser must not
parse these text lines. Expose structured Red Pitaya parameters/signals instead.

The useful `RTQ` fields are:

```text
sequence, id, normalized_residual, template_gain,
requested_shift_hz, se_hz, loss_counter
```

Tracker state should be decided by the backend, for example `STOPPED`,
`SEARCHING`, `TRACKING`, `DEGRADED`, `RELOCKING`, and `ERROR`. JavaScript only
renders it.

## Delivery plan

Work in small, buildable milestones. Do not start by porting the entire tracker.

### Milestone 0 — discovery and design

- Complete `docs/raw_iq_interface.md`.
- Complete a concise `docs/architecture.md` showing threads/control flow,
  ownership, data rates, and the parameter/signal contract.
- Identify exactly which impedance-analyzer files/patterns will be reused and
  which algorithmic parts will not.
- Report uncertainties before implementing around them.

### Milestone 1 — minimal raw-IQ web app

- Create a buildable `resonance_tracker` app registered consistently with sibling
  apps.
- Implement start/stop and safe initialization/cleanup.
- Read coherent raw I/Q from the existing FPGA interface.
- Show connection state, run state, sequence/update rate, excitation frequency,
  raw I, raw Q, magnitude, and phase.
- Provide a bounded short history plot or diagnostic table.
- No full search/tracking algorithm yet.

Milestone 1 acceptance criteria:

- Builds with the repository's documented app build flow.
- Loads without browser console errors.
- Start/stop can be repeated without hanging or leaking the acquisition resource.
- Frequency changes do not publish stale I/Q under the new frequency.
- UI refresh remains decoupled from measurement timing.
- Invalid/not-ready/overflow conditions are visible rather than converted into
  plausible measurements.

### Milestone 2 — replayable tracker core

- Add a hardware-independent tracker API.
- Add a replay/test source capable of feeding captured frequency/I/Q samples.
- Integrate existing search, complex model, fitting, quality, and relock logic
  from the user's implementation where present; do not re-invent it from memory.
- Add deterministic tests for state transitions and loss/relock behavior.

### Milestone 3 — two-sensor dashboard

- Add sensor 1/2 enable and visibility controls.
- Show frequency, Q, frequency SE, quality, and state per sensor.
- Add trajectory and diagnostic complex-I/Q views.
- Add RTM point analysis, RTD statistics/histograms, and RTQ diagnostics only
  after the core data contracts are stable.

## UI contract for the first implementation

Prefer the parameter and signal conventions used by current sibling apps. Choose
final names only after inspecting those conventions, then document them in
`docs/architecture.md`.

Initial controls:

- run/stop;
- excitation frequency in Hz;
- integration/averaging setting only if safely supported by the current FPGA API;
- web telemetry rate;
- sensor/channel selector only if the hardware interface already exposes it.

Initial telemetry:

- backend connection and error state;
- acquisition state and monotonic sequence;
- requested and effective frequency;
- raw I and Q integers;
- scaled/normalized I and Q, if scaling is known;
- magnitude and phase with documented calculation;
- valid/busy/overflow flags;
- acquisition and web publication rates.

Do not expose a control that the backend cannot apply and verify. Disable or omit
unsupported controls instead of making them cosmetic.

## Numerical and RF requirements

- Preserve raw integer data until a named conversion function.
- Use sufficiently wide intermediate types for accumulator values and magnitude
  calculations; avoid signed overflow and unintended integer truncation.
- Use `hypot`/`atan2` or equivalent for magnitude/phase.
- Define whether phase is degrees or radians and keep it consistent across API,
  logs, and UI.
- Document I/Q scaling. If absolute scaling is unknown, call the values
  normalized or ADC/accumulator units rather than volts.
- Track requested versus realizable DDS/NCO frequency separately when tuning is
  quantized.
- Any averaging must state whether it averages raw accumulations, complex I/Q,
  magnitude, or fitted estimates. Prefer coherent complex averaging where valid.
- Represent invalid data explicitly; do not use zero as an implicit error value.

## Coding and repository rules

- Match the language standard, formatting, naming, and build conventions already
  used by the nearest maintained Red Pitaya web apps.
- Reuse repository APIs and utilities before introducing dependencies.
- Keep changes scoped to the new app and the smallest necessary build/registration
  files.
- Never overwrite or discard unrelated working-tree changes.
- Do not edit vendored/generated files unless that is the established build path.
- Avoid large mechanical formatting changes in borrowed code.
- Add comments for hardware sequencing, units, ownership, and non-obvious timing;
  do not narrate straightforward code.
- Keep configuration centralized. Do not scatter 125 MHz, 30 MHz, register masks,
  or update intervals as magic numbers.
- Do not silently copy code with incompatible licensing. Retain required headers.

## Validation

For every milestone:

1. Run the narrowest relevant unit tests and app build.
2. Run formatting/static checks used by this repository for modified files.
3. If cross-compilation or target hardware is unavailable, state exactly what was
   and was not validated; do not claim runtime verification.
4. When hardware is available, capture a short test showing monotonically updated
   I/Q at a fixed frequency, then a controlled frequency change with correct
   reset/settling behavior.
5. Check browser console and backend logs for errors.
6. Summarize modified files, data-contract decisions, tests, and remaining
   hardware questions.

Do not weaken or delete an existing test simply to make a change pass.

## Definition of done for the initial Codex task

The first Codex task is complete when Milestones 0 and 1 are implemented and
validated as far as the available environment permits. It is not complete if it
only copies the impedance analyzer UI, produces mock I/Q without clearly marking
it as simulated, or leaves the hardware interface undocumented.

Before proceeding to the full resonance tracker, present the user with:

- the documented raw-IQ contract;
- the implemented parameter/signal names;
- the achieved acquisition and UI rates;
- target-hardware checks still required;
- any decision needed for two-sensor multiplexing or simultaneous acquisition.
