"""Exercise settings UI, persistent rotations and MCU reset without phone actions."""
from pathlib import Path
import argparse,json,re,time,subprocess
import serial
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stlink-serial');ap.add_argument('--port',default='COM3');ap.add_argument('--output',type=Path,default=ROOT/'.tmp/settings/board_settings')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True);results=[]
    with serial.Serial(args.port,115200,timeout=.1) as port,(args.output/'serial.log').open('w',encoding='utf8') as log:
        def read(seconds):
            end=time.monotonic()+seconds;text=''
            while time.monotonic()<end:
                chunk=port.read(4096).decode('utf8','replace');text+=chunk;log.write(chunk)
            log.flush();return text
        def send(cmd):
            log.write('\n>>> '+cmd+'\n');log.flush();port.write((cmd+'\r\n').encode());return read(.25)
        def state():
            send('status');text=read(.5);send('settings status');text+=read(.5)
            # Include the bytes drained by each command as well.
            return text
        def capture():
            text=send('status')+read(.4)+send('settings status')+read(.4)
            status=re.findall(r'^\[status\].*$',text,re.M);display=re.findall(r'^\[display\].*$',text,re.M)
            settings=re.findall(r'^\[settings\] current=.*$',text,re.M)
            assert status and display and settings,text
            assert 'fw=1.2.1-privacy ' in status[-1] and 'LCD=READY error=0' in status[-1],text
            assert re.search(r'call=(?:0|4)(?:\r?$)',text,re.M),text
            assert 'stale=00 stack_low=00 event_drop=0 log_drop=0 rx_drop=0' in text,text
            values=lambda s:{k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',s)}
            return values(status[-1])|values(display[-1])|values(settings[-1])
        def check(name,page,focus=0,row=None):
            d=capture();assert d['page']==page and d['focus']==focus,(name,d)
            if row is not None:assert d['row']==row,(name,d)
            results.append({'name':name,'passed':True,'state':d});print(name,'PASS',flush=True);return d
        def reset():
            capture() # Refuse to reset an active/incoming call.
            cmd=['C:/Program Files/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin/STM32_Programmer_CLI.exe','-c','port=SWD',*(['sn='+args.stlink_serial] if args.stlink_serial else []),'mode=UR','reset=HWrst','-rst']
            p=subprocess.run(cmd,capture_output=True);assert p.returncode==0,p.stdout+p.stderr
            with (args.output/'reset_native.log').open('ab') as f:f.write(p.stdout+p.stderr)
            read(4)
        send('key home');check('clock root',3)
        for page in (11,12,14,16,3):send('key right');check('right cycle '+str(page),page)
        for page in (16,14,12,11,3):send('key left');check('left cycle '+str(page),page)
        send('page settings');send('key down');check('settings bar',16,1)
        send('key select');check('footer opens settings',17,row=0)
        send('key select');check('direction list',18,row=0)
        current=0
        for target in (1,2,3,0):
            while current<target:send('key down');current+=1
            while current>target:send('key up');current-=1
            send('key select');deadline=time.monotonic()+10
            while True:
                d=capture()
                if d['state']==0 and d['saved']==target*90 and d['current']==target*90:break
                assert time.monotonic()<deadline,d
            results.append({'name':'save '+str(target*90),'passed':True,'state':d});print('save',target*90,'PASS',flush=True)
            sequence=d['seq'];send('key select');read(1);assert capture()['seq']==sequence # deduplicate
            reset();d=check('reboot restores '+str(target*90),3)
            assert d['current']==target*90 and d['saved']==target*90 and d['seq']==sequence,d
            send('page settings');send('key select');send('key select');check('current direction selection',18,row=target)
        send('key down');send('key down');send('key down');send('key down');check('direction bar boundary',18,1)
        send('key up');check('direction content restored',18,row=3)
        send('key back');check('return preserves setting row',17,row=0)
        send('key down');send('key select');check('calibration confirmation',19,row=0)
        send('key down');check('confirmation bar',19,1)
        send('key right');send('key right');send('key select');check('cancel confirmation restores row',17,row=1)
        send('key home');check('finish at clock',3)
        (args.output/'results.json').write_text(json.dumps(results,indent=2),encoding='utf8')
    return 0
if __name__=='__main__':raise SystemExit(main())
