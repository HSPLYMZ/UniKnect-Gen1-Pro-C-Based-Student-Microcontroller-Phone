"""Check real signal snapshots, manual refresh coalescing and background freeze.

No call, hangup, network configuration or simulated signal command is sent.
Visual layout and RF measurement accuracy require separate evidence.
"""
from pathlib import Path
import argparse
import json
import re
import time
import serial

ROOT=Path(__file__).resolve().parents[2]


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',default='COM3')
    ap.add_argument('--expected-version',default='1.2.1-privacy')
    ap.add_argument('--operator',default='中国广电')
    ap.add_argument('--output',type=Path,default=ROOT/'.tmp/signal/final_snapshot')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    with serial.Serial(args.port,115200,timeout=.1) as port,(args.output/'serial.log').open('w',encoding='utf8') as log:
        def send(text):
            log.write('\n>>> '+text+'\n');log.flush();port.write((text+'\r\n').encode())

        def read(seconds):
            end=time.monotonic()+seconds;buf=''
            while time.monotonic()<end:
                text=port.read(4096).decode('utf8','replace');buf+=text;log.write(text)
            log.flush();return buf

        def snapshot():
            send('signal status');text=read(1)
            match=re.search(r'\[signal\] state=(\d+) request=(\d+) completed=(\d+) valid=(\d+) errors=(\d+) present=(\d+) utc=(\d+) scroll=(\d+)',text)
            assert match,text
            value=dict(zip(['state','request','completed','valid','errors','present','utc','scroll'],map(int,match.groups())))
            value['metrics']=re.search(r'^\[signal\] RSRP=.*',text,re.M).group(0).strip()
            value['operator']=re.search(r'^\[signal\] operator=(.*)',text,re.M).group(1).strip()
            return value

        send('status');text=read(1)
        assert f'fw={args.expected_version} ' in text,text
        assert re.search(r'call=(0|4)(?:\r?$)',text,re.M),text
        send('page signalinfo');read(7);first=snapshot()
        assert first['state']==4 and first['valid']==127 and first['errors']==0 and first['request']==0,first
        assert first['operator']==args.operator,first
        background=read(35)
        assert '+QENG: "servingcell"' in background,'No background serving-cell response observed during freeze check'
        frozen=snapshot();assert first==frozen,(first,frozen)
        for _ in range(3):send('signal refresh')
        read(7);manual=snapshot()
        expected=(first['completed']+1)&0xffffffff or 1
        assert manual['completed']==expected and manual['errors']==0 and manual['request']==0 and manual['utc']>first['utc'],manual
        send('key home');read(1);send('status');send('tasks');final=read(2)
        assert 'stale=00 stack_low=00 event_drop=0 log_drop=0 rx_drop=0' in final,final
        assert 'page=3 ' in final and 'profile=14 bench=0 focus=0' in final,final
        result={'first':first,'unchanged_after_35_seconds':frozen,'coalesced_manual_refresh':manual,'passed':True,
                'background_serving_query_observed':True,
                'rf_accuracy':'not_calibrated','visual_validation':'separate_user_feedback'}
        (args.output/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('Real signal fields / 35s freeze / coalesced refresh / health: PASS')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
