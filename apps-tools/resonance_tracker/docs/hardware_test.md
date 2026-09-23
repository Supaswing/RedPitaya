# Raw-IQ target hardware test

This test is intentionally built and run on the Red Pitaya. It accesses the VNA
register block through `/dev/mem`; do not run it concurrently with the web-app
backend.

## Deploy from the Windows checkout

The target repository is expected at
`/root/RedPitaya/apps-tools/resonance_tracker`. Copy only the test and its
production acquisition dependency with:

```powershell
cd C:\Users\bud\RedPitaya\apps-tools\resonance_tracker
.\deploy_hardware_test.ps1
```

Override the SSH destination or target path when needed:

```powershell
.\deploy_hardware_test.ps1 -Target root@rp-f0f8d5.local `
  -RemoteAppDirectory /root/RedPitaya/apps-tools/resonance_tracker
```

The deployment helper copies files but does not build, stop services, load a
bitstream, or run the hardware test.

## Run on the target

1. Load the normal resonance-tracker bitstream if it is not already active.
2. Stop the `resonance_tracker` application from Bazaar. The script refuses to
   continue when `fuser` reports another `/dev/mem` user.
3. Build and run:

```sh
cd /root/RedPitaya/apps-tools/resonance_tracker
sh hardware_test.sh 2>&1 | tee hardware_test.log
```

The defaults exercise ten windows at 32 MHz, one at 34 MHz, and one after
returning to 32 MHz, all with `WINDOW_SHIFT=17`. The test verifies DDS register
readback and treats the integration-only period count as a lower bound because
the hardware counter also covers settling/control latency. Custom values are
accepted as:

```sh
sh hardware_test.sh frequency_a_hz frequency_b_hz fixed_samples window_shift
```

The build is serial and isolated in `build-hardware-test`; it does not install
or replace the web application. A passing run ends with `PASS`. Copy
`hardware_test.log` back to the development checkout and attach its contents to
`docs/raw_iq_interface.md` as the hardware validation record.

## DDS-LSB and integration-noise experiment

The noise experiment measures the nearest DDS setting to 30 MHz and the next
phase increment (a separation of `125 MHz / 2^32`, approximately 0.0291 Hz).
It collects 128 estimates by default at every `WINDOW_SHIFT` from 16 through
20. It also forms coherent two-acquisition I/Q averages at shifts 16 through
19 and compares their standard deviations with the next native window:
`2x16 vs 17`, `2x17 vs 18`, `2x18 vs 19`, and `2x19 vs 20`.
The two DDS settings alternate acquisition order (`A-B`, then `B-A`) so a
settling or ordering bias cannot masquerade as a one-increment response.

Run it while the web application is stopped:

```sh
cd /root/RedPitaya/apps-tools/resonance_tracker
sh noise_test.sh 2>&1 | tee noise_test.log
```

An optional first argument changes the number of estimates. The optional
second argument changes the raw CSV path:

```sh
sh noise_test.sh 256 my_noise_samples.csv 2>&1 | tee noise_test.log
```

`noise_test.log` contains per-metric means, sample standard deviations, and
the averaged/native standard-deviation ratios. `raw_iq_noise_samples.csv`
contains every underlying acquisition, including phase increment, period
count, and the four I/Q values. Preserve both files for offline analysis.
