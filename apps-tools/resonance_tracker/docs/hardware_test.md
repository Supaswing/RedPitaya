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
returning to 32 MHz, all with `WINDOW_SHIFT=17`. Custom values are accepted as:

```sh
sh hardware_test.sh frequency_a_hz frequency_b_hz fixed_samples window_shift
```

The build is serial and isolated in `build-hardware-test`; it does not install
or replace the web application. A passing run ends with `PASS`. Copy
`hardware_test.log` back to the development checkout and attach its contents to
`docs/raw_iq_interface.md` as the hardware validation record.
