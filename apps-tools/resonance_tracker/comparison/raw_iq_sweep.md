# Record an I/Q frequency sweep

Run `run_raw_iq_sweep.sh` on the Red Pitaya as root, with the existing raw-IQ
bitstream loaded. Stop the resonance tracker application in Bazaar first and
keep it stopped until capture finishes. The script checks `/dev/mem` ownership
and locks against a second instance of this sweep script. Requires `g++`,
`fuser`, `flock`, and `sha256sum`, as supplied by the target development setup.

From a checkout on the device:

```sh
cd /root/RedPitaya/apps-tools/resonance_tracker/comparison
sh run_raw_iq_sweep.sh
```

Defaults are **33–37 MHz**, **401 points** (10 kHz spacing), **one sweep**, and
**window shift 20**. Both endpoints are included. Each point is one independent
ready-qualified window; there is no software averaging. Shift 20 integrates
1,048,576 samples, nominally 8.388608 ms. The integration-only time for the
default sweep is about 3.36 seconds; settling, register access, Linux scheduling,
and file writes add time. Compilation happens before the timed capture.

Arguments are positional, in Hz:

```sh
# start_hz stop_hz points sweeps window_shift [output_parent]
sh run_raw_iq_sweep.sh 33000000 37000000 801 20 20
```

Frequencies must be integers from 1 through 62,500,000 Hz, with stop greater
than start. Points must be 2–1,000,000 and spacing at least 1 Hz. Sweeps must be
1–1,000,000; shift must be 0–20. The grid rounds to integer requested Hz;
the CSV separately reports the DDS-quantized effective frequency. Repeated
sweeps alternate forward/reverse direction, keeping the same frequency grid.
`sweep_index` and `frequency_index` are zero-based.

Each run creates a unique directory under `comparison/output/` containing:

- `raw.csv`: every raw incident/reflected I/Q window, requested/effective Hz,
  DDS increment, integration count, hardware period count, sequence, sweep and
  frequency indices, monotonic completion time, call duration, validity and error.
- `settings.txt`: UTC start time, configured settings, source hashes and commit
  if available. Physical setup and bitstream version start as `unrecorded`;
  fill these in for the actual experiment.
- `capture.log` and `build.log`: capture progress/errors and compiler output.
- `status.txt`: `complete` only after every requested reading succeeds. An
  interrupted/failed run retains its partial files and is never marked complete.
- `raw_iq_sweep_capture`: the executable built from the current acquisition code.

The existing acquisition adapter is compiled unchanged. Every measurement uses
DDS readback, restart/run, 100 us settling, status reset, one integration, and
ready qualification with a 10 ms timeout. I/Q values are signed raw register
units, without volts or absolute S11 calibration. Incident and reflected pairs
come from the same window. No hardware overflow flag or hardware sequence is
available. Sequence is a software counter of attempted readings; a failed row
has `valid=0` and blank measurement values, then capture aborts. Ctrl+C requests
cancellation after the current acquisition; already-written rows are retained.
The adapter's `close()` releases the mapping; it does not switch off the DDS.

For a Windows checkout, copy the four source/script files and run through SSH:

```powershell
ssh root@rp-f0f8d5.local "mkdir -p /tmp/raw-iq-sweep/src /tmp/raw-iq-sweep/comparison"
scp apps-tools/resonance_tracker/src/raw_iq_acquisition.cpp apps-tools/resonance_tracker/src/raw_iq_acquisition.hpp root@rp-f0f8d5.local:/tmp/raw-iq-sweep/src/
scp apps-tools/resonance_tracker/comparison/raw_iq_sweep_capture.cpp apps-tools/resonance_tracker/comparison/run_raw_iq_sweep.sh root@rp-f0f8d5.local:/tmp/raw-iq-sweep/comparison/
ssh -t root@rp-f0f8d5.local "sh /tmp/raw-iq-sweep/comparison/run_raw_iq_sweep.sh"
scp -r root@rp-f0f8d5.local:/tmp/raw-iq-sweep/comparison/output/. apps-tools/resonance_tracker/comparison/output/
```

Copy files back before rebooting when using `/tmp`. Shell syntax and help are
checked on the Windows host; target C++ compilation and actual hardware capture
remain to be validated on the Red Pitaya.
