# Raw I/Q interface discovery

## Authority

The raw coherent measurement contract is implemented in the separate repository
`C:/Users/bud/sensor-interface-software/InterfaceVna/`. The Red Pitaya tree
contains no corresponding HDL or generated register source. The relevant
symbols are `rp_vna_measure_iq`, `set_frequency_and_start_generator`,
`start_iq_measurement`, `wait_for_measurement_ready`, and
`read_iq_registers` in `rp_vna_interface.c`. The user confirmed that this block
is integrated into the Red Pitaya FPGA core for the STEMlab 125-14 Gen 2 at the
address below.

## Register contract

The VNA register block is mapped at physical address `0x40700000` with a
`0x1000` byte span. All registers are 32-bit and byte-aligned:

| Offset | Name | Access | Meaning |
| --- | --- | --- | --- |
| `0x00` | CONTROL | R/W | `0x0f` restart, `0x0d` run, `0x0c` stop |
| `0x04` | PHASE_INC | R/W | 32-bit DDS phase increment |
| `0x08` | PHASE_OFFSET | R/W | phase offset, default `0` |
| `0x0c` | PERIOD_COUNT | R | completed DDS periods in the latest integration window |
| `0x10` | AMPLITUDE | R/W | generator amplitude, default `0x0800` |
| `0x14` | VNA_CONTROL | W | `0x01` starts one I/Q window |
| `0x18` | WINDOW_SHIFT | R/W | integration length exponent, default `17` |
| `0x1c` | I_INC | R | signed 32-bit incident I result |
| `0x20` | Q_INC | R | signed 32-bit incident Q result |
| `0x24` | I_REF | R | signed 32-bit reflected I result |
| `0x28` | Q_REF | R | signed 32-bit reflected Q result |
| `0x2c` | STATUS | R/W | bit 0 ready; write `0` to clear |

The FPGA uses 52-bit accumulators internally. At completion it divides each
accumulator by `2^WINDOW_SHIFT` and exports a signed 14-bit result through each
32-bit I/Q register. The application sign-extends those values into `int32_t`,
preserves them as raw ADC-domain units, and computes magnitude and phase from
those integers only. No absolute volts calibration is defined.

The user-confirmed integration length is `2^WINDOW_SHIFT` samples at 125 MHz.
The app exposes the raw shift over the initial safe range 0-20, verifies register readback,
and reports the corresponding sample count and integration time. Changing it
invalidates the old sample and resets rolling statistics before acquisition
continues. The FPGA provides no overflow detection or overflow status. Shift 20
is about 8.39 ms; larger
values are not exposed because they exceed the current 10 ms measurement
timeout before allowing for FPGA-ready latency.

## Sequencing and validity

Opening maps `/dev/mem`, writes the window, amplitude and phase offset, and
clears status. It does not write `PERIOD_COUNT`. Each point writes a rounded DDS phase increment for a
125,000,000 Hz FPGA clock, writes restart then run, busy-waits the configured
settling time (100 us by default), clears status, starts one measurement window,
and polls bit 0 until ready or the 10 ms timeout. A ready result is read as four
32-bit I/Q registers plus `PERIOD_COUNT`, and status is cleared afterward.
Timeout also clears status.

The result reads are consecutive but the contract provides no hardware
sequence, documented snapshot latch, busy bit, overflow bit, or atomic-read guarantee.
The current external implementation treats ready as sufficient. The app reads
all result values immediately after ready and never publishes a sample if ready
or any register access fails.

`I_INC/Q_INC` and `I_REF/Q_REF` are the coherent incident/reference pair at the
selected frequency. They do not identify logical sensors; future sensor
selection is an independent hardware/control concern.

The app's `RT_SEQUENCE` is a monotonic software counter for complete valid
acquisitions; it is not a hardware sequence number and may advance by more than
one between browser updates. `RT_OVERFLOW` is always false only because no
overflow register exists; the UI displays this limitation as `UNAVAILABLE` in
the contract status. `RT_VALID` means the complete I/Q and period-count read
completed after ready.

`RT_PERIOD_COUNT` publishes the hardware value read at offset `0x0c` for the
same ready-qualified window. At a fixed frequency it is expected to remain
stable; changing frequency should change it approximately as
`2^WINDOW_SHIFT * effective_frequency / 125000000`.

## Frequency and phase conventions

Requested frequency is an unsigned integer in Hz. The effective frequency is
not returned by hardware; the app reports the DDS-quantized value obtained by
rounding `frequency * 2^32 / 125000000` and converting that increment back to
Hz. Phase is `atan2(Q, I)` in degrees in the UI. The external gamma helper uses
`(I_REF + j Q_REF) / (I_INC + j Q_INC)` and therefore establishes the expected
positive-Q convention.

## Target hardware test

After loading the normal application bitstream, run from the target checkout:

```sh
cd /root/RedPitaya/apps-tools/resonance_tracker
sh hardware_test.sh
```

Optional arguments are `frequency_a_hz frequency_b_hz fixed_samples
window_shift`. Defaults are `32000000 34000000 10 17`. The test prints CSV for
every ready-qualified sample, checks that fixed-frequency `PERIOD_COUNT` stays
within two counts, verifies it against the expected DDS periods, changes the
frequency, and returns to the original frequency. The changed period count is
hardware evidence that the post-change I/Q window used the new DDS setting. A
successful run ends with `PASS`; preserve its complete output as the hardware
validation record.

## Limits and open questions

The external code exposes a 100 us settling default and 10 ms measurement
timeout, but no measured maximum update rate. The app publishes telemetry at
50 ms by default and acquisition remains in the backend thread.

- `TODO(user)`: confirm whether ready latches all four I/Q outputs and
  `PERIOD_COUNT` into one atomic snapshot, or document the required safe read
  order while the generator continues running.
- `TODO(hardware)`: attach the output of `sh hardware_test.sh` to this document
  after running it on the target. The test is implemented but cannot access
  `/dev/mem` in the Windows development environment.
