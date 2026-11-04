#!/usr/bin/env python3
"""Report repeatability/drift separately from absolute accuracy. Uses stable rows only."""
import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


def summarize(rows, bin_seconds=60):
    def number(r,k):
        try:
            v=float(r[k]);return v if math.isfinite(v) else None
        except (KeyError,ValueError,TypeError):return None
    def stats(values):
        return dict(n=len(values),mean=statistics.mean(values),min=min(values),max=max(values),
                    peak_to_peak=max(values)-min(values),stdev=statistics.pstdev(values),
                    max_abs_deviation_from_mean=max(abs(v-statistics.mean(values)) for v in values)) if values else None
    # An initial stable fragment has no observed start of recovery.
    if rows and rows[0].get('stable')=='True':
        first_id=rows[0].get('placement')
        rows=[dict(r,recovery_s='') if r.get('placement')==first_id else r for r in rows]
    stable=[r for r in rows if r.get('stable')=='True' and r.get('valid')=='True' and r.get('calibrating')!='True']
    start=min((number(r,'elapsed_s') for r in rows if number(r,'elapsed_s') is not None),default=0)
    bins=defaultdict(list);placements=defaultdict(list);marked=defaultdict(list);label=None
    # Marks remain active until the next mark. Use distinct labels for distinct
    # fixture returns, and a "moving" mark outside fixture measurements.
    for r in rows:
        if r.get('mark'):label=r['mark']
        if not (r.get('stable')=='True' and r.get('valid')=='True' and r.get('calibrating')!='True'):continue
        t=number(r,'elapsed_s')
        if t is not None:bins[int((t-start)//bin_seconds)].append(r)
        placements[r.get('placement','')].append(r)
        if label and label!='moving':marked[label].append(r)
    def block(rs):
        result={k:stats([number(r,k) for r in rs if number(r,k) is not None]) for k in ['pitch','slope','temperature','magnitude','noise','drift','recovery_s']}
        roll=[number(r,'roll') for r in rs if number(r,'roll') is not None]
        if roll:
            center=math.degrees(math.atan2(sum(math.sin(math.radians(x)) for x in roll),sum(math.cos(math.radians(x)) for x in roll)))
            deviations=[(x-center+180)%360-180 for x in roll]
            result['roll']=dict(n=len(roll),circular_mean=center,
                                peak_to_peak=max(deviations)-min(deviations),
                                deviation_stdev=statistics.pstdev(deviations))
        else:result['roll']=None
        return result
    groups={k:block(v) for k,v in marked.items()}
    means=[g['slope']['mean'] for g in groups.values() if g['slope']]
    minute_means=[block(v)['slope']['mean'] for _,v in sorted(bins.items()) if block(v)['slope']]
    recovery={}
    for k,v in placements.items():
        vals=[number(r,'recovery_s') for r in v if number(r,'recovery_s') is not None]
        if vals:recovery[k]=vals[0]
    return dict(total_rows=len(rows),stable_rows=len(stable),
                simulated=any(r.get('simulated')=='True' for r in rows),
                sensor_modes=sorted(set(r.get('sensor_mode','unknown') for r in rows)),
                saved_calibration_rows=sum(r.get('saved')=='True' for r in rows),
                duration_s=max((number(r,'elapsed_s') or 0 for r in rows),default=0)-start,
                angle_units='degrees',absolute_error='NOT MEASURED: requires independent angle reference',
                notes=['Only compare marked returns on the same fixture and direction.',
                       'Roll uses circular mean and wrapped deviations; do not combine different fixture orientations.',
                       'Temperature, when available, is a sensor internal reading, not calibrated ambient temperature; BNO086 firmware reports it unavailable.',
                       'Static-bin drift is meaningful only if the device remained on the same unchanged fixture.'],
                overall=block(stable),time_bins={str(k):block(v) for k,v in sorted(bins.items())},
                static_bin_mean_range_deg=max(minute_means)-min(minute_means) if len(minute_means)>=2 else None,
                marked_returns=groups,
                marked_slope_mean_range_deg=max(means)-min(means) if len(means)>=2 else None,
                marked_slope_max_deviation_from_first_deg=max(abs(m-means[0]) for m in means) if len(means)>=2 else None,
                automatic_stability_episodes={k:block(v) for k,v in placements.items()},
                recovery_seconds_by_episode=recovery)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('csv');p.add_argument('--output');p.add_argument('--bin-seconds',type=float,default=60)
    args=p.parse_args()
    if args.bin_seconds<=0:p.error('--bin-seconds must be positive')
    with open(args.csv,newline='') as f:report=summarize(list(csv.DictReader(f)),args.bin_seconds)
    text=json.dumps(report,indent=2,ensure_ascii=False)
    if args.output:Path(args.output).write_text(text+'\n')
    print(text)
