"""Read board health; optical and telephone acceptance require separate evidence."""
from pathlib import Path
import argparse
import json
import re
import time
import serial

ROOT = Path(__file__).resolve().parents[2]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='COM3')
    ap.add_argument('--output', type=Path, default=ROOT/'.tmp/reviewfix_board_20261025')
    ap.add_argument('--expected-version', default='1.2.1-privacy')
    ap.add_argument('--wait-ready', type=float, default=5)
    ap.add_argument('--require-network', action='store_true')
    ap.add_argument('--restore-home', action='store_true', help='only after confirming no active call')
    args = ap.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, 115200, timeout=.1) as port, (args.output/'final_status.log').open('w', encoding='utf8') as log:
        def command(text):
            log.write('\n>>> '+text+'\n');log.flush()
            port.write((text+'\r\n').encode())

        def read(seconds):
            end = time.monotonic()+seconds
            chunks = []
            while time.monotonic() < end:
                s = port.read(4096).decode('utf8', 'replace')
                chunks.append(s);log.write(s)
            log.flush()
            return ''.join(chunks)

        deadline = time.monotonic()+args.wait_ready
        while True:
            command('status');command('tasks');s = read(2)
            phone_lines = re.findall(r'^\[phone\].*$', s, re.M)
            status_lines = re.findall(r'^\[status\].*$', s, re.M)
            if phone_lines and status_lines:
                phone = {k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)', phone_lines[-1])}
                assert phone.get('call') in (0,4), ('Active/incoming call: no automatic navigation',phone)
                assert f'fw={args.expected_version} ' in status_lines[-1], status_lines[-1]
                network_ready = all(phone.get(k)==1 for k in ('network','registered','LTE','signal_valid'))
                if 'LCD=READY error=0' in status_lines[-1] and (network_ready or not args.require_network):
                    break
            if time.monotonic() >= deadline:
                raise AssertionError(('Board/network did not become ready',s))
            print('Waiting for board/network readiness...', flush=True)
        if args.restore_home:
            command('lcd bench off');command('key home');read(.5)
            command('status');command('tasks');s = read(2)
        print(s)
        assert f'fw={args.expected_version} ' in s and 'LCD=READY error=0' in s
        assert 'profile=14 bench=0' in s and 'phone_only=1 tasks=6' in s
        if args.restore_home:
            assert re.search(r'\[status\].* page=3 ', s) and 'focus=0' in s
        assert '[health] stale=00 stack_low=00 event_drop=0 log_drop=0 rx_drop=0' in s
        assert '[rotation] mode=LOCK requested=0 actual=0' in s and 'sd=DISABLED' in s
        phone_line = re.findall(r'^\[phone\].*$', s, re.M)[-1]
        phone = {k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',phone_line)}
        assert phone['demo']==0 and phone['call'] in (0,4)
        if args.require_network:
            assert all(phone.get(k)==1 for k in ('network','registered','LTE','signal_valid'))
        assert '[clock] Beijing ' in s
        rows = re.findall(r'^(control|input|display|console|health|voice) (\d+) (\d+)/(\d+) (\d+) (\d+) (OK|LATE)',s,re.M)
        assert len(rows)==6 and all(row[-1]=='OK' and int(row[2])>=64 for row in rows)
        result = {'passed':True,'expected_version':args.expected_version,'active_tasks':rows,
                  'network':phone,'no_call_command_issued':True,
                  'visual_validation':'pending','telephone_validation':'not_tested_by_this_script'}
        (args.output/'final_status.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
