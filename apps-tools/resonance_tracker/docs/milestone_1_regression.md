# Milestone 1 regression baseline

Before Milestone 2A changes, the user confirmed the deployed application could
start, stop, publish raw coherent I/Q, update statistics, change
`WINDOW_SHIFT`, and display `PERIOD_COUNT`. The source path was inspected before
editing and the register acquisition implementation was retained rather than
recreated.

Milestone 2A keeps these existing public controls and telemetry fields. Raw I/Q
still runs through the same `RawIqAcquisition::measure` call and rolling
statistics implementation. The only lifecycle change is that its numeric state
is now `RAW_IQ` in the authoritative state model.

Target checks still required after deploying this revision:

- repeat start/stop several times;
- verify fixed-frequency sequence monotonicity and update rate;
- change frequency and confirm no old-frequency sample is published as valid;
- change `WINDOW_SHIFT` and confirm statistics reset and readback is correct;
- confirm browser console and Nginx/backend logs remain clear;
- run a 30-34 MHz baseline and inspect candidate/fit results;
- cancel baseline during overview and refinement;
- acquire and cancel five-point diagnostics.
