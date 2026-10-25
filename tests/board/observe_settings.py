"""Read-only observation while a person calibrates and power-cycles the board."""
from pathlib import Path
import argparse,json,re,time
import serial
ROOT=Path(__file__).resolve().parents[2]
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--port',default='COM3')
    ap.add_argument('--seconds',type=float,default=180);ap.add_argument('--output',type=Path,default=ROOT/'.tmp/settings/manual_observation')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True);samples=[];disconnects=0;port=None;pending=''
    with (args.output/'serial.log').open('w',encoding='utf8') as log:
        end=time.monotonic()+args.seconds;next_command=0
        while time.monotonic()<end:
            try:
                if port is None:
                    port=serial.Serial(args.port,115200,timeout=.1);next_command=0;pending='';log.write('\n[host] port connected\n');log.flush()
                if time.monotonic()>=next_command:
                    port.write(b'status\r\nsettings status\r\n');next_command=time.monotonic()+2
                text=port.read(4096).decode('utf8','replace');log.write(text);log.flush();pending+=text
                while '\n' in pending:
                    line,pending=pending.split('\n',1);line=line.strip()
                    if line.startswith(('[settings]','[status]','[io]','[health]','[phone]')):
                        samples.append({'host_monotonic':round(time.monotonic(),3),'line':line})
            except (serial.SerialException,OSError):
                disconnects+=1
                if port is not None:
                    try:port.close()
                    except OSError:pass
                port=None;log.write('\n[host] disconnected/reconnect pending\n');log.flush();time.sleep(.5)
        if port is not None:port.close()
    result={'samples':samples,'disconnect_events':disconnects,'commands':['status','settings status'],'visual_and_full_power_off':'requires_user_feedback'}
    (args.output/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    print('Read-only observation complete:',len(samples),'records; disconnect/retry events:',disconnects)
    return 0
if __name__=='__main__':raise SystemExit(main())
