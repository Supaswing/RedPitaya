"""Check the matrix analyzer against the preserved first NanoVNA capture."""

from pathlib import Path

from analyze_nanovna_matrix import analyze_raw, complex_stats


def main():
    root = Path(__file__).parent
    raw = root / "output" / "setupA_nanovna_20260928_123520" / "raw.csv"
    rows = analyze_raw(raw, 192, 16_000, 100)
    five_sensor_1 = next(row for row in rows if row["tracker_points"] == 5 and row["sensor_id"] == 1)
    assert five_sensor_1["tracking_frames"] == 106
    assert abs(five_sensor_1["frequency_noise_rms_population_hz"] - 4131.6134219) < 0.001
    assert five_sensor_1["fixed_gamma_samples"] == 0
    stats = complex_stats([1 + 0j, 3 + 0j])
    assert stats["mean_gamma_magnitude"] == 2
    assert stats["amplitude_noise_rms_population"] == 1
    assert stats["relative_amplitude_noise_rms"] == 0.5
    assert stats["phase_noise_rms_population_deg"] == 0
    print("PASS: NanoVNA matrix analyzer reference and complex-noise calculations")


if __name__ == "__main__":
    main()
