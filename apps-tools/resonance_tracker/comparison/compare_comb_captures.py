"""Compare per-frequency noise and periodic ripple in two complete captures."""
import argparse
import csv
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def load(root):
    with (root / 'raw.csv').open() as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) != 6144 or any(r['valid'] != '1' for r in rows):
        raise ValueError('Comparison requires complete valid captures')
    return rows


def compare(old, new):
    runs = [load(old), load(new)]
    metrics = ('inc_i','inc_q','ref_i','ref_q','inc_magnitude','ref_magnitude','ratio_magnitude')
    records = []
    all_data = []
    for root, rows in zip((old, new), runs):
        with (root / 'analysis/repeatability.csv').open() as stream:
            periodic = {(int(r['window_shift']),r['metric']):r for r in csv.DictReader(stream)}
        data = np.empty((3,128,16,7))
        time = np.empty((3,128,16))
        seen = set()
        for row in rows:
            s,i,p = (int(row[k]) for k in ('window_shift','frequency_index','reading_index'))
            if (s,i,p) in seen or s not in (18,19,20) or not 0<=i<128 or not 0<=p<16:
                raise ValueError('Duplicate/unexpected sample')
            seen.add((s,i,p))
            iq = [float(row[k]) for k in metrics[:4]]
            inc,ref=complex(*iq[:2]),complex(*iq[2:])
            data[s-18,i,p] = iq+[abs(inc),abs(ref),abs(ref/inc)]
            time[s-18,i,p]=float(row['timestamp_s'])
        all_data.append(data)
        for s in range(3):
            for m,name in enumerate(metrics):
                sd = np.median(data[s,:,:,m].std(axis=1,ddof=1))
                mean=data[s,:,:,m].mean()
                a100=a200=float('nan')
                if m<4:
                    frequency=float(periodic[s+18,name]['near100_frequency_hz'])
                    t=time[s].ravel()
                    y=data[s,:,:,m].ravel()
                    b=np.column_stack([fn(2*np.pi*h*frequency*t) for h in (1,2) for fn in (np.sin,np.cos)])
                    # Remove both DDS-index and pass offsets in the balanced
                    # 128x16 design. Remaining ripple is a time-domain component.
                    def center(v):
                        shape=(128,16)+v.shape[1:]
                        a=v.reshape(shape)
                        return (a-a.mean(axis=0,keepdims=True)-a.mean(axis=1,keepdims=True)+a.mean(axis=(0,1),keepdims=True)).reshape(v.shape)
                    coef=np.linalg.lstsq(center(b),center(y),rcond=None)[0]
                    a100=np.hypot(*coef[:2]); a200=np.hypot(*coef[2:])
                records.append([root.name,s+18,name,mean,sd,a100,a200])
    out=new/'analysis'
    with (out/'comparison.csv').open('w',newline='') as stream:
        w=csv.writer(stream)
        w.writerow(['capture','shift','metric','grand_mean','median_per_frequency_sd_sample',
                    'time100_peak_amplitude_after_index_and_pass_offsets','time200_peak_amplitude_after_index_and_pass_offsets'])
        w.writerows(records)
    fig,axes=plt.subplots(3,2,figsize=(13,10))
    for s in range(3):
        ax=axes[s,0]
        for data,label in zip(all_data,('Earlier capture','Battery capture')):
            ax.plot(data[s,:,:,:4].std(axis=1,ddof=1).mean(axis=0),'.-',label=label)
        ax.set_xticks(range(4),metrics[:4]);ax.set_yscale('log')
        ax.set_title(f'Shift {s+18}: mean per-frequency sample SD');ax.set_ylabel('raw units');ax.legend()
        ax=axes[s,1]
        for data,label in zip(all_data,('Earlier capture','Battery capture')):
            response=data[s,:,:,2].mean(axis=1)
            ax.plot(response-response.mean(),label=label)
        ax.set_title(f'Shift {s+18}: reflected-I frequency means')
        ax.set_xlabel('DDS increment offset');ax.set_ylabel('centered raw units');ax.legend()
    fig.tight_layout();fig.savefig(out/'battery_comparison.png',dpi=160)
    for s in (18,19,20):
        print('SHIFT',s)
        for metric in metrics:
            oldrow=next(r for r in records if r[0]==old.name and r[1]==s and r[2]==metric)
            newrow=next(r for r in records if r[0]==new.name and r[1]==s and r[2]==metric)
            print(metric,'SD',round(oldrow[4],6),'->',round(newrow[4],6),'reduction',round(oldrow[4]/newrow[4],2),
                  '100Hz_amp',round(oldrow[5],3),'->',round(newrow[5],3))
    for s in range(3):
        battery=all_data[1]
        first=battery[s,0,:,2].mean();others=battery[s,1:,:,2].mean()
        print('Battery shift',s+18,'ref_i_index0_minus_other_mean',first-others)
    print('Battery reflected-I cross-shift correlations',np.corrcoef(all_data[1][:,:,:,2].mean(axis=2)))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('earlier',type=Path);p.add_argument('battery',type=Path)
    a=p.parse_args();compare(a.earlier,a.battery)
