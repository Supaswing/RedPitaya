"""Synthetic analysis checks only; never report these as device measurements."""
import csv
from pathlib import Path
import tempfile

import numpy as np

from analyze_raw_iq_comb import analyze


def main():
    with tempfile.TemporaryDirectory(prefix="synthetic_comb_") as directory:
        root = Path(directory)
        raw = root / "synthetic.csv"
        rng = np.random.default_rng(1234)
        fields = ["window_shift", "frequency_index", "reading_index", "phase_increment",
                  "frequency_hz", "timestamp_s", "valid", "inc_i", "inc_q", "ref_i", "ref_q"]
        base = round(30000000 * 2**32 / 125000000)
        with raw.open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow(fields)
            for s in range(18, 21):
                for f in range(128):
                    for r in range(16):
                        comb = 100 * np.cos(2 * np.pi * f / 8)
                        writer.writerow([s, f, r, base + f, (base + f) * 125000000 / 2**32,
                                         r + f * 0.001, 1, 10000 + comb + rng.normal(),
                                         2000 + rng.normal(), 5000 + rng.normal(),
                                         1000 + rng.normal()])
        analyze(raw, root / "analysis")
        with (root / "analysis/periodicity_screen.csv").open() as stream:
            peaks = list(csv.DictReader(stream))
        for row in peaks:
            if row["metric"] == "inc_i":
                assert float(row["period_dds_units"]) == 8
                assert abs(float(row["peak_amplitude"]) - 100) < 1
                assert float(row["p_bonferroni_27_series"]) < 0.05
            if row["metric"] == "ref_i":
                assert float(row["p_bonferroni_27_series"]) > 0.05
        with (root / "analysis/frequency_statistics.csv").open() as stream:
            stats = list(csv.DictReader(stream))
        assert len(stats) == 384
        assert all(row["count"] == "16" for row in stats)
        assert all(abs(float(row["inc_i_mean_se"]) * 4 - float(row["inc_i_sd_sample"])) < 1e-12
                   for row in stats)
        lines = raw.read_text().splitlines()
        raw.write_text("\n".join(lines[:-1]) + "\n")
        try:
            analyze(raw, root / "invalid")
        except ValueError:
            pass
        else:
            raise AssertionError("Incomplete scan accepted")
        print("PASS: synthetic 8-unit comb, noise-only series, statistics, incomplete rejection")


if __name__ == "__main__":
    main()
