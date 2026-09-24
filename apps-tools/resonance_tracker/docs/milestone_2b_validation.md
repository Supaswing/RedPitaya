# Milestone 2B completion and review (2026-09-24)

## Recovery behavior

Three consecutive poor tracking frames trigger `RELOCKING`. Each lost logical
resonance gets an 11-point local complex scan, then a 21-point expanded scan
if needed. A fit needs model explained fraction >= 0.20, linewidth within
0.67–1.50 times the baseline FWHM, and a center away from the scan edges.
At most 32 windows are acquired per lost resonance, within a two-second worker
deadline. A successful scan recenters the existing template and clears that
sensor's loss counter. A failed scan requests a full baseline using the saved
baseline configuration; valid completion automatically restarts tracking.
Cancellation checks run before every coherent point. An acquisition failure
enters `ERROR` with its reason.

## Host checks

MSYS2 UCRT GCC 16.1.0 built and ran `instrument_state_test`,
`tracker_engine_test`, `resonance_analysis_test`, and `iq_statistics_test`.
Replay tests cover a small commanded shift in both 3- and 5-point modes,
three-frame loss, first-scan relock, expanded-scan relock, bounded rejection,
cancellation after four points, and relocking one lost sensor while the second
remains valid. State tests cover local success, full-baseline fallback,
successful restart, failed fallback, and stale completion rejection.

The review also fixed the earlier narrow-resonance test failure: a coarse
side-lobe candidate with zero complex-model support was being retained despite
its Q falling in range. The original assertion now passes. No test was
weakened. Tracking quality now rejects nonfinite or negative frequency SE.

## Pending target checks and known limits

The target app build, browser console, acquisition and web rates, repeated
start/stop, frequency-change freshness, relock duration, and RF behavior have
not been checked here because the Red Pitaya target SDK and hardware are not
available in this workspace. NanoVNA event logs confirm examples of local
acceptance and full-baseline fallback, but lack their relock complex input
points; exact numerical golden comparison remains pending. The existing
`fitCurvatureFallback` helper is not called by baseline acceptance and needs a
captured weak/non-model case before integration. The FPGA still exposes one
incident/reference pair, so the two sensor IDs denote resonances rather than
separate physical input channels.
