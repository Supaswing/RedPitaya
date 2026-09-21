# Raw I/Q interface discovery

## Authority

The raw coherent measurement contract is implemented in the separate repository
`C:/Users/bud/sensor-interface-software/InterfaceVna/`. The Red Pitaya tree
contains no corresponding HDL or generated register source. The relevant
symbols are `rp_vna_measure_iq`, `set_frequency_and_start_generator`,
`start_iq_measurement`, `wait_for_measurement_ready`, and
`read_iq_registers` in `rp_vna_interface.c`.

## Register contract

The VNA register block is mapped at physical address `0x40700000` with a
`0x1000` byte span. All registers are 32-bit and byte-aligned:

| Offset | Name | Access | Meaning |
| --- | --- | --- | --- |
| `0x00` | CONTROL | R/W | `0x0f` restart, `0x0d` run, `0x0c` stop |
| `0x04` | PHASE_INC | R/W | 32-bit DDS phase increment |
| `0x08` | PHASE_OFFSET | R/W | phase offset, default `0` |
| `0x0c` | PERIOD_COUNT | R/W | meaning not yet confirmed; held at default `0` |
| `0x10` | AMPLITUDE | R/W | generator amplitude, default `0x0800` |
| `0x14` | VNA_CONTROL | W | `0x01` starts one I/Q window |
| `0x18` | WINDOW_SHIFT | R/W | integration length exponent, default `17` |
| `0x1c` | I_INC | R | signed 32-bit incident I result |
| `0x20` | Q_INC | R | signed 32-bit incident Q result |
| `0x24` | I_REF | R | signed 32-bit reflected I result |
| `0x28` | Q_REF | R | signed 32-bit reflected Q result |
| `0x2c` | STATUS | R/W | bit 0 ready; write `0` to clear |

The result registers are accumulator/window outputs in raw FPGA units. The
external repository does not define volts, ADC counts, an accumulator count,
or a normalization scale. The tracker therefore preserves them as raw signed
integer units and computes magnitude and phase from those integers only.

The user-confirmed integration length is `2^WINDOW_SHIFT` samples at 125 MHz.
The app exposes the raw shift over the initial safe range 0-20, verifies register readback,
and reports the corresponding sample count and integration time. Changing it
invalidates the old sample and resets rolling statistics before acquisition
continues. Because overflow status is unavailable, large shifts must be checked
on hardware for accumulator saturation. Shift 20 is about 8.39 ms; larger
values are not exposed because they exceed the current 10 ms measurement
timeout before allowing for FPGA-ready latency.

## Sequencing and validity

Opening maps `/dev/mem`, writes the window, amplitude, phase offset, period
count, and clears status. Each point writes a rounded DDS phase increment for a
125,000,000 Hz FPGA clock, writes restart then run, busy-waits the configured
settling time (100 us by default), clears status, starts one measurement window,
and polls bit 0 until ready or the 10 ms timeout. A ready result is read as four
32-bit registers and status is cleared afterward. Timeout also clears status.

The four result reads are consecutive but the contract provides no hardware
sequence, snapshot latch, busy bit, overflow bit, or atomic-read guarantee.
The current external implementation treats ready as sufficient. The app reads
all four values immediately after ready and never publishes a sample if ready
or any register access fails.

`I_INC/Q_INC` and `I_REF/Q_REF` are the coherent incident/reference pair at the
selected frequency. They do not identify logical sensors; future sensor
selection is an independent hardware/control concern.

The app's `RT_SEQUENCE` is a monotonic software counter for complete valid
acquisitions; it is not a hardware sequence number and may advance by more than
one between browser updates. `RT_OVERFLOW` is always false only because no
overflow register exists; the UI displays this limitation as `UNAVAILABLE` in
the contract status. `RT_VALID` means the complete four-register read completed
after ready.

## Frequency and phase conventions

Requested frequency is an unsigned integer in Hz. The effective frequency is
not returned by hardware; the app reports the DDS-quantized value obtained by
rounding `frequency * 2^32 / 125000000` and converting that increment back to
Hz. Phase is `atan2(Q, I)` in degrees in the UI. The external gamma helper uses
`(I_REF + j Q_REF) / (I_INC + j Q_INC)` and therefore establishes the expected
positive-Q convention.

## Limits and open questions

The external code exposes a 100 us settling default and 10 ms measurement
timeout, but no measured maximum update rate. The app publishes telemetry at
50 ms by default and acquisition remains in the backend thread.

- `TODO(user)`: define accumulator width, overflow behavior, and
  atomicity/latching requirements in the FPGA design.
- `TODO(user)`: define `PERIOD_COUNT`; the app leaves it at zero until its
  behavior and safe range are known.
- `TODO(user)`: confirm target bitstream/version and whether `0x40700000` is
  reserved exclusively for this VNA block on STEMlab 125-14 Gen 2.
- `TODO(user)`: provide a hardware test result for fixed-frequency monotonic
  updates and a controlled frequency change.
