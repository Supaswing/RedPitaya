"""Build Setup A comparison CSV and two figures from preserved capture summaries."""

import csv
import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


ROOT = Path(__file__).parent
OUTPUT = ROOT / "output"
NANO = OUTPUT / "setupA_nanovna_20260928_123520"
RP_PREFIX = OUTPUT / "setupA_redpitaya_static_shift20_avg3_5pt3pt_20260928_1230"


def read_csv(path):
    with path.open(newline="", encoding="utf-8") as source:
        return list(csv.DictReader(source))


def main():
    nano_summary = read_csv(NANO / "tracking_summary.csv")
    nano_raw = read_csv(NANO / "raw.csv")
    rp_summary = read_csv(Path(str(RP_PREFIX) + ".summary.csv"))
    rp_frames = read_csv(Path(str(RP_PREFIX) + ".csv"))
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
                source_level = ""
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

    fig, axes = plt.subplots(1, 2, figsize=(9.3, 3.7), sharey=True)
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
            ax.annotate(f"{platform}, {row['tracker_points']} pt" + ("*" if partial else ""),
                        (float(row["frame_rate_hz"]), float(row["df_rms_population_hz"]) / 1000),
                        xytext=(4, 5), textcoords="offset points", fontsize=7)
        ax.set_title(f"Sensor {sensor}")
        ax.set_xlabel("Complete two-sensor backend frames/s")
        ax.grid(alpha=0.2)
    axes[0].set_ylabel("Observed dF RMS (kHz)")
    fig.suptitle("Frequency noise versus rate (* Red Pitaya 3-point noise is a WebSocket subset)")
    fig.tight_layout()
    fig.savefig(OUTPUT / "setupA_noise_rate.png", dpi=170)
    plt.close(fig)

    print(json.dumps({"rows": len(rows), "comparison_csv": str(OUTPUT / "setupA_comparison_summary.csv"),
                      "figures": ["setupA_frequency_traces.png", "setupA_noise_rate.png"]}))


if __name__ == "__main__":
    main()
