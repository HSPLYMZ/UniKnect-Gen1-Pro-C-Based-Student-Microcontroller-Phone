"""Read status during user-operated calls. Never send call/end/navigation keys."""
from pathlib import Path
import argparse
import json
import re
import time
import serial


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port',default='COM3')
    ap.add_argument('--seconds',type=float,default=35)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--stop-file',type=Path,help='stop read-only capture when this file appears')
    args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    transitions=[];last=None;page=None;display={};phone={};start=time.monotonic()
    with serial.Serial(args.port,115200,timeout=.1) as port, (args.output/'serial.log').open('w',encoding='utf8') as log:
        pending=b'';next_status=start
        while time.monotonic()-start<args.seconds and not (args.stop_file and args.stop_file.exists()):
            now=time.monotonic()
            if now>=next_status:
                port.write(b'status\r\n');log.write('\n>>> status\n');next_status=now+2
            chunk=port.read(4096);pending+=chunk;log.write(chunk.decode('utf8','replace'));log.flush()
            while b'\n' in pending:
                line,pending=pending.split(b'\n',1);s=line.decode('utf8','replace').strip()
                fields={k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)',s)}
                if s.startswith('[display] profile='):display=fields
                elif s.startswith('[status] '):page=fields.get('page')
                elif s.startswith('[phone] '):
                    phone=fields;key=(phone.get('call'),page,display.get('focus'),display.get('nav'))
                    if key!=last:
                        event={'elapsed_seconds':round(time.monotonic()-start,3),'call':key[0],
                               'page':page,'focus':key[2],'nav':key[3],'network':phone}
                        transitions.append(event);last=key
                        print(event,flush=True)
                        (args.output/'transitions.json').write_text(json.dumps(transitions,indent=2),encoding='utf8')
        port.write(b'tasks\r\n');end=time.monotonic()+1
        while time.monotonic()<end:log.write(port.read(4096).decode('utf8','replace'))
    (args.output/'summary.json').write_text(json.dumps({'seconds':round(time.monotonic()-start,3),
        'commands_sent':['status','tasks'],'phone_commands_sent':False,'transitions':transitions,
        'last_phone':phone,'visual_and_audio_validation':'require_user_confirmation'},indent=2),encoding='utf8')


if __name__=='__main__':main()
