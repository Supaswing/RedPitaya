# Milestone 1 regression baseline

Before Milestone 2A changes, the user confirmed the deployed application could
start, stop, publish raw coherent I/Q, update statistics, change
`WINDOW_SHIFT`, and display the hardware `PERIOD_COUNT`. The source path was inspected before
editing and the register acquisition implementation was retained rather than
recreated.

Milestone 2A keeps these existing public controls and telemetry fields. Raw I/Q
still runs through the same `RawIqAcquisition::measure` call and rolling
statistics implementation. The only lifecycle change is that its numeric state
is now `RAW_IQ` in the authoritative state model.

Target checks still required after deploying this revision:

- run `sh hardware_test.sh` to verify repeated ready-qualified fixed-frequency
  acquisitions and a controlled 32 MHz -> 34 MHz -> 32 MHz change using the
  hardware DDS-period count;
- repeat start/stop several times;
- change `WINDOW_SHIFT` and confirm statistics reset and readback is correct;
- confirm browser console and Nginx/backend logs remain clear;
- run a 30-34 MHz baseline and inspect candidate/fit results;
- cancel baseline during overview and refinement;
- acquire and cancel five-point diagnostics.
