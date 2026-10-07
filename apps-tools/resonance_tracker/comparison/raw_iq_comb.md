# DDS-unit I/Q experiment

Requested capture: shifts 18, 19, 20; 128 consecutive DDS increments starting
at the nearest setting to 30 MHz; 16 independent readings at each frequency.
There is no software averaging in the raw capture. Effective frequencies are
29,999,999.998835847 through 30,000,003.695022315 Hz (step 0.029103830457 Hz).
Integration windows are respectively 2.097152, 4.194304 and 8.388608 ms.
The integration-only total is about 30 seconds, plus settling and OS overhead.

Keep the physical setup fixed. Stop the resonance tracker from Bazaar before
running. The capture refuses to run if another process owns `/dev/mem`.
Do not unload/change the normal raw-IQ bitstream. No FPGA or web-app changes
are required. Existing raw-IQ acquisition code is compiled into an isolated
capture executable. It restarts the DDS and waits 100 us before each window,
as in the production adapter. DDS readback and ready completion are required.
No hardware overflow flag exists. The capture aborts on any invalid reading,
preserving the partial CSV and error log; analysis rejects incomplete scans.

From PowerShell in `C:\Users\bud\RedPitaya`, copy into an isolated device
directory (authentication is through your normal SSH password/key prompt):

```powershell
ssh root@rp-f0f8d5.local "mkdir -p /tmp/raw-iq-comb-20261006/src /tmp/raw-iq-comb-20261006/comparison"
scp apps-tools/resonance_tracker/src/raw_iq_acquisition.cpp apps-tools/resonance_tracker/src/raw_iq_acquisition.hpp root@rp-f0f8d5.local:/tmp/raw-iq-comb-20261006/src/
scp apps-tools/resonance_tracker/comparison/raw_iq_comb_capture.cpp apps-tools/resonance_tracker/comparison/run_raw_iq_comb.sh root@rp-f0f8d5.local:/tmp/raw-iq-comb-20261006/comparison/
ssh root@rp-f0f8d5.local "sh /tmp/raw-iq-comb-20261006/comparison/run_raw_iq_comb.sh"
```

The wrapper prints the output path and writes `raw.csv`, `settings.txt`, and
`capture.log` under `comparison/output/raw_iq_comb_<UTC timestamp>/` on the
device. Copy the output back before rebooting (`/tmp` is temporary):

```powershell
scp -r root@rp-f0f8d5.local:/tmp/raw-iq-comb-20261006/comparison/output apps-tools/resonance_tracker/comparison/
```

Each shift runs 16 full passes, alternating forward and reverse direction.
Every frequency gets 16 readings distributed over the shift's acquisition
interval. The per-frequency SD therefore includes drift across that interval,
not only short-term adjacent-window noise. The raw CSV preserves pass index,
frequency index, phase increment, time, call duration, period count, and all
four signed integer I/Q values. Failed rows are explicitly invalid.

Analysis (replace the timestamp in the filename with the captured one):

```powershell
& C:/Users/bud/Orthsens/sdsi_reader/.venv/Scripts/python.exe apps-tools/resonance_tracker/comparison/analyze_raw_iq_comb.py apps-tools/resonance_tracker/comparison/output/raw_iq_comb_20261005_152416/raw.csv
```

Outputs include per-frequency means, sample SD, mean SE, and CPU sample I/Q
covariance; frequency-pattern plots with SE bars; noise plots; and detrended
spectral screening. Magnitudes and reflected/incident complex ratios are
computed per reading before averaging. Raw units have no volts calibration;
ratios have no absolute S11 calibration. All readings are retained.

The spectral screen searches 2–64 cycles per scan after removing a linear
frequency trend. Its bootstrap compares the largest spectral peak with
resampled per-frequency errors; it corrects for 27 metric/shift series.
This assumes independent errors and may be misleading under correlated
interference or drift. Compare raw pass data and patterns across shifts before
claiming a comb or its cause. A single spectral peak is not proof of a comb.
The 3.696 Hz span investigates fine DDS structure at the requested operating
point; it cannot find or establish proximity to resonance. Physical setup
and proximity to resonance remain unverified until recorded by the operator.

Validation on the Windows host: synthetic 8-unit pattern detection, noise-only
series screening, statistics, and incomplete-scan rejection passed. Hardware
capture and target C++ compilation remain pending; SSH authentication failed
and the operator elected to run the prepared script. WSL was available but had
no `g++` compiler. No measured result has been reported yet.
