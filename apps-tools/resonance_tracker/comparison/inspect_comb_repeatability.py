"""Compare DDS-index structure with time-domain structure in a saved capture.

These are descriptive fitted models, not a test of hardware causality. The
frequency is selected from the entire capture. Forward-only coefficients are
also evaluated on reverse passes to check amplitude/phase reproducibility.
Pass centering removes offsets; original raw data are never modified.
"""
import csv
from pathlib import Path
import argparse
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main(path):
    with path.open() as f:
        rows = list(csv.DictReader(f))
    out = path.parent / 'analysis'
    out.mkdir(exist_ok=True)
    fig, axes = plt.subplots(3, 2, figsize=(14, 11))
    corrected_fig, corrected_axes = plt.subplots(3, 2, figsize=(14, 10))
    results = []
    for s in (18, 19, 20):
        selected = [r for r in rows if int(r['window_shift']) == s]
        a = np.empty((16, 128, 4))
        t = np.empty((16, 128))
        for r in selected:
            p, i = int(r['reading_index']), int(r['frequency_index'])
            a[p, i] = [float(r[m]) for m in ('inc_i', 'inc_q', 'ref_i', 'ref_q')]
            t[p, i] = float(r['timestamp_s'])
        centered = a - a.mean(axis=1, keepdims=True)
        for m, name in enumerate(('inc_i', 'inc_q', 'ref_i', 'ref_q')):
            forward = centered[::2, :, m].mean(axis=0)
            reverse = centered[1::2, :, m].mean(axis=0)
            corr = np.corrcoef(forward, reverse)[0, 1]
            loo = [np.corrcoef(centered[p, :, m], np.delete(centered[:, :, m], p, axis=0).mean(axis=0))[0, 1]
                   for p in range(16)]
            index = np.array([int(r['frequency_index']) for r in selected])
            time = np.array([float(r['timestamp_s']) for r in selected])
            y = np.array([float(r[name]) for r in selected])
            passes = np.array([int(r['reading_index']) for r in selected])
            # Remove each pass mean before comparing periodic regressors.
            def remove_pass_mean(v):
                v = v.copy()
                for p in range(16):
                    v[passes == p] -= v[passes == p].mean(axis=0)
                return v
            y = remove_pass_mean(y)
            frequencies = np.arange(1, 401, 0.1)
            scores = []
            for f in frequencies:
                basis = remove_pass_mean(np.column_stack((np.sin(2*np.pi*f*time), np.cos(2*np.pi*f*time))))
                fit = basis @ np.linalg.lstsq(basis, y, rcond=None)[0]
                scores.append(float(fit @ fit / (y @ y)))
            best = int(np.argmax(scores))
            # Refine the coarse maximum and check the 100/200 Hz neighborhoods:
            # a 0.1 Hz grid loses phase coherence over a long acquisition.
            candidates = np.unique(np.concatenate([np.arange(center-.15, center+.15, .001)
                                                     for center in (frequencies[best], 100.1, 200.2)]))
            best_f, best_score = frequencies[best], scores[best]
            near100 = (0., 0.)
            for f in candidates:
                basis = remove_pass_mean(np.column_stack((np.sin(2*np.pi*f*time), np.cos(2*np.pi*f*time))))
                fit = basis @ np.linalg.lstsq(basis, y, rcond=None)[0]
                score = float(fit @ fit / (y @ y))
                if 99.9 < f < 100.3 and score > near100[1]:
                    near100 = (f, score)
                if score > best_score:
                    best_f, best_score = f, score
            fundamental = near100[0]
            basis = remove_pass_mean(np.column_stack([fn(2*np.pi*h*fundamental*time)
                                                      for h in (1, 2) for fn in (np.sin, np.cos)]))
            harmonic_fit = basis @ np.linalg.lstsq(basis, y, rcond=None)[0]
            harmonic_r2 = float(harmonic_fit @ harmonic_fit / (y @ y))
            forward_mask = passes % 2 == 0
            forward_coefficients = np.linalg.lstsq(basis[forward_mask], y[forward_mask], rcond=None)[0]
            reverse_mask = ~forward_mask
            reverse_r2 = 1 - np.sum((y[reverse_mask] - basis[reverse_mask] @ forward_coefficients)**2) / np.sum(y[reverse_mask]**2)
            remainder = y - harmonic_fit
            original_mean = np.array([y[index == f].mean() for f in range(128)])
            remaining_mean = np.array([remainder[index == f].mean() for f in range(128)])
            mean_rms_ratio = np.linalg.norm(remaining_mean) / np.linalg.norm(original_mean)
            results.append([s, name, corr, np.median(loo), best_f, best_score, *near100,
                            harmonic_r2, mean_rms_ratio, reverse_r2])
            if m == 0:
                ax = corrected_axes[s-18, 0]
                ax.plot(original_mean, label='measured frequency mean')
                ax.plot(remaining_mean, label='after fitted 100 + 200 Hz removal')
                ax.set_title(f'Shift {s}: incident I frequency pattern')
                ax.set_xlabel('DDS increment offset'); ax.set_ylabel('centered raw units'); ax.legend()
                ax = corrected_axes[s-18, 1]
                short = time < time[0] + .1
                ax.plot(time[short]-time[0], y[short], '.-', label='measured')
                ax.plot(time[short]-time[0], harmonic_fit[short], '.-', label='100 + 200 Hz fit')
                ax.set_title(f'Shift {s}: first 100 ms, incident I')
                ax.set_xlabel('elapsed seconds'); ax.legend()
            print(s, name, 'forward_reverse_corr', round(corr, 4), 'median_pass_corr', round(np.median(loo), 4),
                  'best_time_hz', round(best_f, 3), 'time_R2', round(best_score, 4),
                  'near100_hz_R2', tuple(round(v, 4) for v in near100),
                  '100+200_R2', round(harmonic_r2,4), 'remaining_frequency_mean_rms_ratio',round(mean_rms_ratio,4))
        ax = axes[s-18, 0]
        ax.plot(centered[::2, :, 0].mean(axis=0), label='forward')
        ax.plot(centered[1::2, :, 0].mean(axis=0), label='reverse')
        ax.set_title(f'Shift {s}: incident I, centered forward/reverse means')
        ax.set_xlabel('DDS increment offset'); ax.set_ylabel('raw units'); ax.legend()
        ax = axes[s-18, 1]
        for p in (0, 1, 2, 3):
            ax.plot(np.arange(128), centered[p, :, 0], lw=0.7, label=f'pass {p+1}')
        ax.set_title(f'Shift {s}: individual incident-I passes in DDS order')
        ax.set_xlabel('DDS increment offset'); ax.legend()
        print('shift', s, 'median_interval_ms', np.median(np.diff([float(r['timestamp_s']) for r in selected]))*1000)
    fig.tight_layout(); fig.savefig(out/'repeatability.png', dpi=160)
    corrected_fig.tight_layout(); corrected_fig.savefig(out/'time_interference.png', dpi=160)
    with (out/'repeatability.csv').open('w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['window_shift','metric','forward_reverse_corr','median_leave_one_pass_out_corr',
                    'best_time_sine_hz_1_to_400','time_sine_explained_fraction',
                    'near100_frequency_hz','near100_explained_fraction',
                    'fundamental_plus_second_harmonic_explained_fraction','remaining_frequency_mean_rms_ratio',
                    'reverse_R2_using_forward_coefficients'])
        w.writerows(results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('raw_csv', type=Path)
    main(parser.parse_args().raw_csv)
