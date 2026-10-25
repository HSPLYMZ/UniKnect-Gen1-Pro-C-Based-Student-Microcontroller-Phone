"""Measure real LCD frame completions. Fixtures affect only the display snapshot.

No calls, SMS, or demo commands are sent to the phone model. The modem task
continues normally; phone-only builds disable SD. Restore the stable transport.
"""
import argparse
import json
from pathlib import Path
import re
import time

import serial

ROOT = Path(__file__).resolve().parents[1]


def evaluate_samples(samples, bench_id, rotation, profile_id, seconds, min_fps):
    matching = [s for s in samples if s.get('bench')==bench_id and
                s.get('rotation')==rotation and s.get('profile')==profile_id]
    coverage = sum(s.get('window',0) for s in matching)
    continuous = bool(matching) and all(4500 <= s.get('window',0) <= 7000 for s in matching)
    continuous = continuous and all(0 < b['received_ms']-a['received_ms'] <= 7500
                                   for a,b in zip(matching,matching[1:]))
    passed = coverage >= seconds*1000 and continuous and all(
        s.get('fps10',0)>=min_fps*10 and s.get('rolling10',0)>=min_fps*10 and
        s.get('timeout',1)==0 and s.get('dmaerr',1)==0 for s in matching)
    return matching, coverage, continuous, passed


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='COM3')
    ap.add_argument('--profile', default='dma3', choices=['gpio', 'fast'] +
                    [f'{mode}{n}' for mode in ('poll', 'dma') for n in range(8)])
    ap.add_argument('--seconds', type=float, default=60)
    ap.add_argument('--min-fps', type=float, default=40, help='required completed-frame floor, including rolling windows')
    ap.add_argument('--angles', type=int, nargs='+', default=[0], choices=[0, 90, 180, 270])
    ap.add_argument('--scenes', nargs='+', default=['carousel', 'list', 'scroll'], choices=['carousel', 'list', 'scroll', 'color', 'signal', 'settings'])
    ap.add_argument('--restore-profile', default='dma3')
    ap.add_argument('--restore-angle',type=int,default=0,choices=[0,90,180,270],help='temporary orientation to restore; does not write saved settings')
    ap.add_argument('--output', type=Path, default=ROOT/'.tmp/wououi/board')
    args = ap.parse_args()
    if args.min_fps <= 0:
        ap.error('--min-fps must be positive')
    if args.seconds < 6:
        ap.error('--seconds must be at least 6 to capture a complete 5 second window')
    args.output.mkdir(parents=True, exist_ok=True)
    results = []
    with serial.Serial(args.port, 115200, timeout=0.1) as port, (args.output/'serial.log').open('w', encoding='utf-8') as log:
        pending = b''

        def send(command):
            log.write(f'\n>>> {command}\n'); log.flush()
            port.write((command+'\r\n').encode())

        def receive(seconds, samples=None):
            nonlocal pending
            end = time.monotonic()+seconds
            while time.monotonic() < end:
                data = port.read(4096)
                if not data:
                    continue
                log.write(data.decode('utf-8', 'replace')); log.flush()
                pending += data
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    line = line.decode('utf-8', 'replace').strip()
                    if samples is not None and line.startswith('[fps]'):
                        sample={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',line)}
                        sample['received_ms']=round(time.monotonic()*1000)
                        samples.append(sample)

        try:
            send('lcd bench off'); receive(1)
            send(f'lcd profile {args.profile}'); receive(5)
            for angle in args.angles:
                send(f'rotate {angle}'); receive(3)
                for scene in args.scenes:
                    send('lcd bench off'); receive(1)
                    send(f'lcd bench {scene}')
                    # Each log covers 5 seconds and its rolling minimum can
                    # look back another 5. Exclude both setup-tainted windows.
                    receive(11)
                    samples = []
                    bench_id = ['off', 'carousel', 'list', 'scroll', 'color', 'signal', 'settings'].index(scene)
                    profile_id = ['gpio', 'fast'] + [f'{mode}{n}' for mode in ('poll', 'dma') for n in range(7, -1, -1)]
                    expected_profile = profile_id.index(args.profile)
                    capture_start=time.monotonic()
                    while True:
                        receive(1,samples)
                        matching,coverage_ms,continuous,passed=evaluate_samples(
                            samples,bench_id,angle//90,expected_profile,args.seconds,args.min_fps)
                        elapsed=time.monotonic()-capture_start
                        if (elapsed>=args.seconds and coverage_ms>=args.seconds*1000) or elapsed>=args.seconds+10:
                            break
                    result = {'profile': args.profile, 'angle': angle, 'scene': scene,'required_fps':args.min_fps,
                              'windows': matching, 'passed': passed,
                              'visual_validation': 'pending',
                              'capture_seconds': args.seconds, 'coverage_ms': coverage_ms,
                              'measured_wall_seconds':round(elapsed,3),'continuous_windows':continuous,
                              'target_40fps_met':all(min(s['fps10'],s.get('rolling10',0))>=400 for s in matching) if matching else False,
                              'minimum_fps': min((min(s['fps10'], s.get('rolling10', 0))/10 for s in matching), default=0),
                              'maximum_gap_ms': max((s['gap'] for s in matching), default=0)}
                    results.append(result)
                    (args.output/'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
                    print(f'{args.profile} {angle} {scene}: min={result["minimum_fps"]:.1f} FPS gap={result["maximum_gap_ms"]}ms {"PASS" if passed else "FAIL"}', flush=True)
            send('status'); receive(2)
        finally:
            send('lcd bench off'); receive(1)
            send(f'lcd profile {args.restore_profile}'); receive(5)
            send(f'rotate {args.restore_angle}'); receive(2)
    return 0 if results and all(r['passed'] for r in results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
