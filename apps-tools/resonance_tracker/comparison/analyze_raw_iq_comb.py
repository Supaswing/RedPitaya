"""Analyze 128 DDS-unit frequencies x 16 windows x shifts 18/19/20.

Raw values remain ADC/accumulator units; the ratio is uncalibrated. Sample SD
uses N-1. Spectral peaks are descriptive evidence of DDS-index periodicity,
not proof of physical resonance. Bootstrap screening assumes independent
measurement errors and does not account for correlated temporal interference.
"""
import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

METRICS = ("inc_i", "inc_q", "ref_i", "ref_q", "inc_magnitude",
           "ref_magnitude", "ratio_re", "ratio_im", "ratio_magnitude")


def analyze(raw_path, output):
    with raw_path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 6144 or any(row["valid"] != "1" for row in rows):
        raise ValueError("Incomplete/invalid capture: require all 6,144 valid readings")
    data = np.empty((3, 128, 16, len(METRICS)))
    times = np.empty((3, 128, 16))
    frequencies = np.empty((3, 128))
    seen = set()
    base = round(30000000 * 2**32 / 125000000)
    for row in rows:
        shift, index, reading = (int(row[key]) for key in
                                 ("window_shift", "frequency_index", "reading_index"))
        key = shift, index, reading
        if shift not in (18, 19, 20) or not 0 <= index < 128 or not 0 <= reading < 16 or key in seen:
            raise ValueError(f"Unexpected or duplicate group: {key}")
        seen.add(key)
        increment = int(row["phase_increment"])
        frequency = float(row["frequency_hz"])
        if increment != base + index or abs(frequency - increment * 125000000 / 2**32) > 1e-7:
            raise ValueError(f"DDS frequency mismatch: {key}")
        iq = [float(row[name]) for name in METRICS[:4]]
        inc, ref = complex(*iq[:2]), complex(*iq[2:])
        if abs(inc) == 0:
            raise ValueError(f"Zero incident denominator: {key}")
        ratio = ref / inc
        data[shift - 18, index, reading] = iq + [abs(inc), abs(ref), ratio.real, ratio.imag, abs(ratio)]
        times[shift - 18, index, reading] = float(row["timestamp_s"])
        frequencies[shift - 18, index] = frequency
    if not np.isfinite(data).all():
        raise ValueError("Nonfinite measurement")
    output.mkdir(parents=True, exist_ok=True)
    means, sd = data.mean(axis=2), data.std(axis=2, ddof=1)
    fields = ["window_shift", "frequency_index", "frequency_hz", "count"]
    fields += [f"{name}_{suffix}" for name in METRICS for suffix in ("mean", "sd_sample", "mean_se")]
    fields += ["inc_iq_cov_sample", "ref_iq_cov_sample"]
    with (output / "frequency_statistics.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(fields)
        for s in range(3):
            for f in range(128):
                stats = [v for m in range(len(METRICS)) for v in
                         (means[s, f, m], sd[s, f, m], sd[s, f, m] / 4)]
                cov = [np.cov(data[s, f, :, a], data[s, f, :, a + 1], ddof=1)[0, 1] for a in (0, 2)]
                writer.writerow([s + 18, f, frequencies[s, f], 16] + stats + cov)
    rng = np.random.default_rng(1801920)
    x = np.arange(128)
    trend_basis = np.column_stack((np.ones(128), x - x.mean()))

    def detrend(values):
        return values - trend_basis @ np.linalg.lstsq(trend_basis, values, rcond=None)[0]

    peaks = []
    for s in range(3):
        for m, name in enumerate(METRICS):
            residual = detrend(means[s, :, m])
            amplitudes = 2 * abs(np.fft.rfft(residual)) / 128
            amplitudes[-1] /= 2  # Nyquist bin has only one component.
            peak_bin = 2 + int(np.argmax(amplitudes[2:]))
            # Maximum over the full searched spectrum for every null draw.
            errors = data[s, :, :, m] - means[s, :, m, None]
            bootstrap = np.empty((128, 1000))
            for f in range(128):
                bootstrap[f] = errors[f, rng.integers(0, 16, size=(1000, 16))].mean(axis=1)
            null = 2 * abs(np.fft.rfft(detrend(bootstrap), axis=0)) / 128
            null[-1] /= 2
            p = (1 + np.count_nonzero(null[2:].max(axis=0) >= amplitudes[peak_bin])) / 1001
            peaks.append([s + 18, name, np.median(sd[s, :, m]),
                          np.ptp(means[s, :, m]), peak_bin, 128 / peak_bin,
                          128 / peak_bin * 125000000 / 2**32,
                          amplitudes[peak_bin], p, min(1, p * 3 * len(METRICS))])
    with (output / "periodicity_screen.csv").open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["window_shift", "metric", "median_sd_sample", "mean_peak_to_peak",
                         "peak_cycles_per_scan", "period_dds_units", "period_hz", "peak_amplitude",
                         "bootstrap_p_max_spectrum", "p_bonferroni_27_series"])
        writer.writerows(peaks)
    for selected, filename in ((METRICS[:4], "raw_iq.png"), (METRICS[4:], "amplitude_ratio.png")):
        fig, axes = plt.subplots(len(selected), 3, figsize=(15, 3 * len(selected)), squeeze=False)
        for r, name in enumerate(selected):
            m = METRICS.index(name)
            for s in range(3):
                ax = axes[r, s]
                ax.errorbar(x, means[s, :, m], yerr=sd[s, :, m] / 4, lw=0.8, elinewidth=0.5)
                ax.set_title(f"shift {s + 18}: {name}")
                ax.set_xlabel("DDS increment offset from nearest 30 MHz (one unit = 0.029104 Hz)")
                ax.set_ylabel("uncalibrated ratio" if name.startswith("ratio") else "raw units")
                ax.grid(alpha=0.25)
        fig.suptitle("Frequency means; error bars = sample SD / sqrt(16)")
        fig.tight_layout()
        fig.savefig(output / filename, dpi=160)
        plt.close(fig)
    fig, axes = plt.subplots(3, 1, figsize=(12, 9))
    for s, ax in enumerate(axes):
        for m, name in enumerate(METRICS[:4]):
            ax.plot(x, sd[s, :, m], label=name)
        ax.set_title(f"shift {s + 18}: per-frequency sample SD (16 readings)")
        ax.set_ylabel("raw units")
        ax.set_xlabel("DDS increment offset")
        ax.legend()
    fig.tight_layout()
    fig.savefig(output / "noise.png", dpi=160)
    plt.close(fig)
    text = [f"Validated {len(rows)} readings: 3 shifts x 128 frequencies x 16 readings.",
            f"Frequency range: {frequencies.min():.9f} to {frequencies.max():.9f} Hz.",
            "Means and SD use individual windows; magnitude is computed per reading before averaging.",
            "I/Q covariance is CPU sample covariance; ratio = reflected / incident, uncalibrated.",
            "Spectral screening removes a linear frequency trend, then searches bins 2..64.",
            "Bootstrap p values screen the maximum spectrum; Bonferroni covers 27 series.",
            "Correlated errors/drift can invalidate p values. A peak alone does not establish a comb.",
            "Compare periods and phases across shifts/repeats before interpreting hardware causality.",
            "This 3.696 Hz span cannot locate a resonance or measure its width."]
    for s in range(3):
        text.append(f"Shift {s + 18}: integration {2**(s+18)/125000000*1000:.6f} ms; "
                    f"observed span {np.ptp(times[s]):.3f} s.")
    (output / "analysis.txt").write_text("\n".join(text) + "\n", encoding="utf-8")
    print("\n".join(text))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("raw_csv", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    analyze(args.raw_csv, args.output or args.raw_csv.parent / "analysis")
