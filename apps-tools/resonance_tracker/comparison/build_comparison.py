"""Build Setup A comparison CSV, noise ratios, and figures from saved captures."""

import csv
import json
import math
import statistics
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D


ROOT = Path(__file__).parent
OUTPUT = ROOT / "output"
NANO = OUTPUT / "setupA_nanovna_20260928_123520"
RP_PREFIX = OUTPUT / "setupA_redpitaya_static_shift20_avg3_5pt3pt_20260928_1230"


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def complex_summary(samples):
    """Summarize a repeated normalized complex ratio at one tracker offset."""
    magnitudes = [abs(value) for value in samples]
    mean_ratio = sum(samples) / len(samples)
    center_phase = math.atan2(mean_ratio.imag, mean_ratio.real)
    phase_residuals = [math.atan2(math.sin(math.atan2(z.imag, z.real) - center_phase),
                                  math.cos(math.atan2(z.imag, z.real) - center_phase))
                       for z in samples]
    return {
        "mean_ratio_magnitude": statistics.mean(magnitudes),
        "amplitude_noise_rms_population": statistics.pstdev(magnitudes) if len(samples) > 1 else "",
        "amplitude_noise_sd_sample": statistics.stdev(magnitudes) if len(samples) > 1 else "",
        "relative_amplitude_noise_rms": (statistics.pstdev(magnitudes) / statistics.mean(magnitudes)
                                          if len(samples) > 1 else ""),
        "mean_phase_deg": math.degrees(center_phase),
        "phase_noise_rms_population_deg": (math.degrees(statistics.pstdev(phase_residuals))
                                           if len(samples) > 1 else ""),
        "phase_noise_sd_sample_deg": (math.degrees(statistics.stdev(phase_residuals))
                                      if len(samples) > 1 else ""),
    }


def main():
    nano_summary = read_csv(NANO / "tracking_summary.csv")
    nano_raw = read_csv(NANO / "raw.csv")
    rp_summary = read_csv(Path(str(RP_PREFIX) + ".summary.csv"))
    rp_frames = read_csv(Path(str(RP_PREFIX) + ".csv"))
    setup = read_csv(ROOT / "setup_A_metadata.csv")[0]
    rows = []
    for source, platform in ((nano_summary, "NanoVNA"), (rp_summary, "Red Pitaya")):
        for record in source:
            points = int(record["tracker_points"])
            sensor = int(record["sensor_id"])
            if platform == "NanoVNA":
                count = int(record["count"])
                missing = int(record["missing_sequences"])
                rate = float(record["complete_two_sensor_frame_rate_hz"])
                rate_basis = "timestamped complete RTD intervals"
                acquisition = "100 Hz tracking bandwidth; 16 kHz IF"
                source_level = "400 mV RMS into 50 ohm at DDS; 200 mV RMS at antenna (user report)"
            else:
                count = int(record["matched_telemetry_frames"])
                first = int(record["first_sequence"])
                last = int(record["last_sequence"])
                missing = last - first + 1 - count
                rate = (float(record["observed_matched_telemetry_rate_hz"])
                        if missing == 0 else (last - first + 1) / 20.0)
                rate_basis = ("timestamped consecutive WebSocket frames"
                              if missing == 0 else "backend sequence growth over requested 20 s")
                acquisition = "WINDOW_SHIFT=20; 3 coarse/refine baseline averages"
                source_level = "800 mV peak into 50 ohm (configured; user report)"
            rows.append({
                "platform": platform,
                "setup_id": "A (physical match pending)",
                "sensor_id": sensor,
                "tracker_points": points,
                "acquisition_setting": acquisition,
                "observed_complete_frames": count,
                "missing_backend_sequences": missing,
                "noise_sample_coverage": count / (count + missing),
                "mean_frequency_hz": record["mean_frequency_hz"],
                "frequency_sd_sample_hz": record["frequency_sd_sample_hz"],
                "df_rms_population_hz": record["df_rms_population_hz"],
                "mean_internal_se_hz": record["mean_internal_se_hz"],
                "mean_normalized_residual": ("" if platform == "NanoVNA"
                                             else record["mean_normalized_residual"]),
                "mean_template_gain": ("" if platform == "NanoVNA"
                                       else record["mean_template_gain"]),
                "source_amplitude": source_level,
                "display_state": setup["display_state"] if platform == "NanoVNA" else "not applicable",
                "calibration_state": setup["nanovna_calibration_state"] if platform == "NanoVNA" else "raw ratio",
                "calibration_plane": setup["nanovna_calibration_plane"] if platform == "NanoVNA" else "unverified",
                "measured_source_amplitude": "",
                "incident_amplitude": "",
                "reflected_amplitude": "",
                "incident_adc_percent_fs": "",
                "reflected_adc_percent_fs": "",
                "tx_gain_attenuation": "",
                "rx_gain_attenuation": "",
                "sensor_contrast_abs_delta_gamma": "",
                "sigma_re_gamma": "",
                "sigma_im_gamma": "",
                "phase_noise_deg": "",
                "frame_rate_hz": rate,
                "frame_rate_basis": rate_basis,
                "signal_delta_f_hz": "",
                "snr_f": "",
            })
    with (OUTPUT / "setupA_comparison_summary.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    ratios = []
    for sensor in (1, 2):
        rp_noise = next(float(r["df_rms_population_hz"]) for r in rp_summary
                        if int(r["tracker_points"]) == 5 and int(r["sensor_id"]) == sensor)
        nano_noise = next(float(r["df_rms_population_hz"]) for r in nano_summary
                          if int(r["tracker_points"]) == 5 and int(r["sensor_id"]) == sensor)
        ratios.append({"sensor_id": sensor, "tracker_points": 5,
                       "nanovna_df_rms_hz": nano_noise, "redpitaya_df_rms_hz": rp_noise,
                       "noise_ratio_nanovna_over_redpitaya": nano_noise / rp_noise,
                       "interpretation": "descriptive; source level and display state differ or are unverified"})
    with (OUTPUT / "setupA_frequency_noise_ratio.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=ratios[0].keys())
        writer.writeheader()
        writer.writerows(ratios)

    # Offset zero is the moving tracker center, so its scatter includes frequency drift.
    rp_points = read_csv(Path(str(RP_PREFIX) + ".points.csv"))
    complex_rows = []
    for points in (5, 3):
        for sensor in (1, 2):
            selected = [r for r in rp_points if int(r["tracker_points"]) == points
                        and int(r["sensor_id"]) == sensor and int(r["offset"]) == 0]
            expected = next(int(r["matched_telemetry_frames"]) for r in rp_summary
                            if int(r["tracker_points"]) == points and int(r["sensor_id"]) == sensor)
            if len(selected) != expected or len({r["sequence"] for r in selected}) != expected:
                raise ValueError(f"Incomplete Red Pitaya center-point set: {points}-point sensor {sensor}")
            values = [complex(float(r["gamma_re"]), float(r["gamma_im"])) for r in selected]
            coverage = next(r["noise_sample_coverage"] for r in rows
                            if r["platform"] == "Red Pitaya" and r["tracker_points"] == points
                            and r["sensor_id"] == sensor)
            complex_rows.append({"platform": "Red Pitaya", "run": "shift20_avg3", "sensor_id": sensor,
                                 "tracker_points": points, "offset": 0, "count": len(values),
                                 "complete_frame_coverage": coverage,
                                 "ratio_convention": "reflected / incident raw coherent I/Q",
                                 **complex_summary(values)})
    debug = read_csv(NANO / "raw.csv")
    for sensor in (1, 2):
        records = [r["record"].split(",") for r in debug
                   if r["phase"] == "complex_debug_frame" and r["record"].startswith("RTM,")]
        selected = [f for f in records if int(f[2]) == sensor and int(f[3]) == 0]
        if len(selected) != 1:
            raise ValueError(f"Expected one NanoVNA center debug point: sensor {sensor}")
        value = complex(float(selected[0][5]), float(selected[0][6]))
        complex_rows.append({"platform": "NanoVNA", "run": "20260928_123520", "sensor_id": sensor,
                             "tracker_points": 5, "offset": 0, "count": 1,
                             "complete_frame_coverage": "",
                             "ratio_convention": "raw measurement / reference (pre-calibration)",
                             **complex_summary([value])})
    with (OUTPUT / "setupA_complex_amplitude_phase.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=complex_rows[0].keys())
        writer.writeheader()
        writer.writerows(complex_rows)

    colors = {"NanoVNA": "#a54134", "Red Pitaya": "#187d75"}
    fig, axes = plt.subplots(2, 2, figsize=(9.3, 5.7), sharex="col", sharey="row")
    for column, points in enumerate((5, 3)):
        for row_index, sensor in enumerate((1, 2)):
            ax = axes[row_index, column]
            for platform in ("NanoVNA", "Red Pitaya"):
                if platform == "NanoVNA":
                    samples = [r for r in nano_raw if r["phase"] == f"tracking_{points}pt"
                               and r["record"].startswith("RTD,")]
                    t = [float(r["elapsed_s"]) for r in samples]
                    index = 2 if sensor == 1 else 5
                    f = [float(r["record"].split(",")[index]) for r in samples]
                else:
                    samples = [r for r in rp_frames if int(r["tracker_points"]) == points
                               and int(r["sensor_id"]) == sensor]
                    t = [float(r["telemetry_timestamp_s"]) for r in samples]
                    f = [float(r["frequency_hz"]) for r in samples]
                t0 = t[0]
                mean = sum(f) / len(f)
                ax.plot([value - t0 for value in t], [(value - mean) / 1000 for value in f],
                        ".", ms=2.8, alpha=0.72, color=colors[platform], label=platform)
            ax.axhline(0, color="#777777", lw=0.6)
            ax.grid(alpha=0.2)
            ax.set_title(f"Sensor {sensor}, {points}-point")
            if row_index == 1:
                ax.set_xlabel("Time from mode start (s)")
            if column == 0:
                ax.set_ylabel("f - run mean (kHz)")
    axes[0, 0].legend(loc="upper right", fontsize=8)
    fig.suptitle("Static frequency estimates; unfiltered observed frames")
    fig.tight_layout()
    fig.savefig(OUTPUT / "setupA_frequency_traces.png", dpi=170)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(9.3, 3.8), sharey=True)
    for sensor, ax in zip((1, 2), axes):
        for row in rows:
            if row["sensor_id"] != sensor:
                continue
            platform = row["platform"]
            marker = "o" if row["tracker_points"] == 5 else "s"
            partial = row["noise_sample_coverage"] < 1.0
            ax.scatter(float(row["frame_rate_hz"]), float(row["df_rms_population_hz"]) / 1000,
                       marker=marker, s=70, color=colors[platform],
                       facecolors="none" if partial else colors[platform], linewidths=1.8)
        ax.set_title(f"Sensor {sensor}")
        ax.set_xlabel("Complete two-sensor backend frames/s")
        ax.grid(alpha=0.2)
    axes[0].set_ylabel("Observed dF RMS (kHz)")
    fig.suptitle("Frequency noise versus complete-frame rate")
    legend = [Line2D([0], [0], marker="o" if points == 5 else "s", linestyle="none",
                     markerfacecolor="none" if platform == "Red Pitaya" and points == 3
                     else colors[platform], markeredgecolor=colors[platform], markersize=8,
                     label=f"{platform}, {points}-point" +
                     (" (sampled noise)" if platform == "Red Pitaya" and points == 3 else ""))
              for platform in ("NanoVNA", "Red Pitaya") for points in (5, 3)]
    fig.legend(handles=legend, loc="lower center", ncol=2, frameon=False, fontsize=8)
    fig.tight_layout(rect=(0, 0.14, 1, 1))
    fig.savefig(OUTPUT / "setupA_noise_rate.png", dpi=170)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(8.0, 3.7))
    x = [0, 1]
    rp_noise = [next(float(r["df_rms_population_hz"]) / 1000 for r in rp_summary
                     if int(r["tracker_points"]) == 5 and int(r["sensor_id"]) == sensor)
                for sensor in (1, 2)]
    nano_noise = [next(float(r["df_rms_population_hz"]) / 1000 for r in nano_summary
                       if int(r["tracker_points"]) == 5 and int(r["sensor_id"]) == sensor)
                  for sensor in (1, 2)]
    ax.bar([v - 0.18 for v in x], rp_noise, width=0.36, color=colors["Red Pitaya"], label="Red Pitaya")
    ax.bar([v + 0.18 for v in x], nano_noise, width=0.36, color=colors["NanoVNA"], label="NanoVNA")
    ax.set_xticks(x, ["Sensor 1", "Sensor 2"])
    ax.set_ylabel("Five-point frequency dF RMS (kHz)")
    ax.set_title("Frequency noise from complete two-sensor captures")
    ax.grid(axis="y", alpha=0.2)
    ax.legend(frameon=False, ncol=3, loc="upper center", bbox_to_anchor=(0.5, -0.12), fontsize=8)
    fig.tight_layout()
    fig.savefig(OUTPUT / "setupA_frequency_noise.png", dpi=170, bbox_inches="tight")
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7.0, 3.4))
    noise_ratios = [next(r["noise_ratio_nanovna_over_redpitaya"] for r in ratios
                         if r["sensor_id"] == sensor) for sensor in (1, 2)]
    ax.bar(x, noise_ratios, width=0.55, color=colors["NanoVNA"])
    ax.axhline(1, color="#555555", linewidth=0.8)
    ax.set_xticks(x, ["Sensor 1", "Sensor 2"])
    ax.set_ylabel("NanoVNA / Red Pitaya dF RMS")
    ax.set_title("Five-point frequency-noise ratio (dimensionless)")
    ax.grid(axis="y", alpha=0.2)
    fig.tight_layout()
    fig.savefig(OUTPUT / "setupA_frequency_noise_ratio.png", dpi=170)
    plt.close(fig)

    fig, axes = plt.subplots(1, 2, figsize=(8.0, 3.5))
    stable = [r for r in complex_rows if r["platform"] == "Red Pitaya" and r["tracker_points"] == 5]
    axes[0].bar([0, 1], [r["relative_amplitude_noise_rms"] * 100 for r in stable],
                color=colors["Red Pitaya"])
    axes[0].set_ylabel("RMS of |Γ| / mean |Γ| (%)")
    axes[0].set_title("Amplitude-ratio noise")
    axes[1].bar([0, 1], [r["phase_noise_rms_population_deg"] for r in stable],
                color=colors["Red Pitaya"])
    axes[1].set_ylabel("Circular phase RMS (degrees)")
    axes[1].set_title("Phase noise")
    for ax in axes:
        ax.set_xticks([0, 1], ["Sensor 1", "Sensor 2"])
        ax.grid(axis="y", alpha=0.2)
    fig.suptitle("Red Pitaya five-point center response; NanoVNA repeatability unavailable")
    fig.tight_layout()
    fig.savefig(OUTPUT / "setupA_amplitude_phase_noise.png", dpi=170)
    plt.close(fig)

    print(json.dumps({"rows": len(rows), "comparison_csv": str(OUTPUT / "setupA_comparison_summary.csv"),
                      "figures": ["setupA_frequency_traces.png", "setupA_noise_rate.png",
                                  "setupA_frequency_noise.png", "setupA_frequency_noise_ratio.png",
                                  "setupA_amplitude_phase_noise.png"]}))


if __name__ == "__main__":
    main()
