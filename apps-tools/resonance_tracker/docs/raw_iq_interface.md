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
same ready-qualified window. It counts DDS periods over an interval that also
includes settling/control latency, not only the `2^WINDOW_SHIFT` integration
samples. Consequently,
`2^WINDOW_SHIFT * effective_frequency / 125000000` is a lower bound rather
than an exact expected value. ARM/Linux scheduling jitter can vary the count
between otherwise identical measurements.

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
every ready-qualified sample, reads back the programmed DDS phase increment,
checks that `PERIOD_COUNT` is at least the integration-only lower bound and
that its implied duration fits within the measured acquisition-call duration,
changes the frequency, and returns to the original frequency. A successful run
ends with `PASS`; preserve its complete output as the hardware validation
record.

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
-- Configuring done (0.1s)
-- Generating done (0.1s)
-- Build files have been written to: /root/RedPitaya/apps-tools/resonance_tracker/build-hardware-test
[ 33%] Building CXX object CMakeFiles/raw_iq_hardware_test.dir/tests/raw_iq_hardware_test.cpp.o
[ 66%] Building CXX object CMakeFiles/raw_iq_hardware_test.dir/src/raw_iq_acquisition.cpp.o
[100%] Linking CXX executable resonance_tracker/raw_iq_hardware_test
[100%] Built target raw_iq_hardware_test
sequence,stage,requested_hz,effective_hz,period_count,minimum_integration_periods,period_count_duration_us,measurement_call_duration_us,inc_i,inc_q,ref_i,ref_q
1,fixed_a,32000000,32000000,43799,33554.4,1368.72,1502.33,346009,-722999,944986,1956688
2,fixed_a,32000000,32000000,40958,33554.4,1279.94,1292.94,321516,-722186,933314,1958401
3,fixed_a,32000000,32000000,40721,33554.4,1272.53,1283.21,315985,-723957,930451,1957874
4,fixed_a,32000000,32000000,40597,33554.4,1268.66,1278.89,322112,-724293,933233,1957450
5,fixed_a,32000000,32000000,40033,33554.4,1251.03,1262.04,340163,-727375,943059,1954549
6,fixed_a,32000000,32000000,39639,33554.4,1238.72,1252.43,338229,-728288,948167,1951985
7,fixed_a,32000000,32000000,41820,33554.4,1306.88,1325.35,337242,-727117,954764,1950106
8,fixed_a,32000000,32000000,41706,33554.4,1303.31,1319.88,341429,-724944,957222,1950824
9,fixed_a,32000000,32000000,41161,33554.4,1286.28,1300.07,331285,-722425,939356,1957275
10,fixed_a,32000000,32000000,46614,33554.4,1456.69,1474,309908,-727338,942762,1951065
11,changed_b,34000000,34000000,43595,35651.6,1282.21,1294.91,-629529,222033,2055727,-721784
12,returned_a,32000000,32000000,40031,33554.4,1250.97,1262.59,340562,-726966,943255,1954764
PASS: 12 ready-qualified acquisitions completed in order with DDS readback; fixed-frequency PERIOD_COUNT range=39639..46614, changed-frequency PERIOD_COUNT=43595
