"""Observe real firmware state through VCP; never submit call/end/SMS commands."""
from pathlib import Path
import argparse,json,re,time,serial
r=Path(__file__).resolve().parents[2]
ap=argparse.ArgumentParser(description=__doc__)
ap.add_argument('--port',default='COM3')
ap.add_argument('--expected-version',default='1.2.1-privacy')
ap.add_argument('--output',type=Path,default=r/'.tmp/wououi/board_navigation')
args=ap.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=True)
cases=[]
with serial.Serial(args.port,115200,timeout=.1) as p,(out/'serial.log').open('w',encoding='utf8') as log:
 def command(c,wait=.12):
  log.write('\n>>> '+c+'\n');p.write((c+'\r\n').encode());time.sleep(wait)
 def read(seconds):
  end=time.monotonic()+seconds;s=''
  while time.monotonic()<end:
   chunk=p.read(4096).decode('utf8','replace');s+=chunk;log.write(chunk)
  log.flush();return s
 def check(name,page,focus=0,nav=None,history=None):
  command('status');s=read(.65)
  displays=re.findall(r'^\[display\] profile=.*$',s,re.M);statuses=re.findall(r'^\[status\].*$',s,re.M)
  if not displays or not statuses:raise AssertionError((name,s))
  d={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',displays[-1]+' '+statuses[-1])}
  assert d['page']==page and d['focus']==focus,(name,d)
  assert nav is None or d['nav']==nav,(name,d)
  assert history is None or d['history']==history,(name,d)
  assert 'LCD=READY error=0' in statuses[-1],statuses[-1]
  assert f'fw={args.expected_version} ' in statuses[-1],statuses[-1]
  assert re.search(r'\[phone\] demo=0 .* call=(?:0|4)(?:\r?$)',s,re.M),('unexpected call/demo',s)
  assert re.search(r'\[health\] stale=00 stack_low=00 event_drop=0 log_drop=0 rx_drop=0',s),s
  cases.append({'name':name,'passed':True,'state':d})
  print(name,'PASS',flush=True)
  return d
 try:
  read(1);command('status');preflight=read(.7)
  assert re.search(r'\[phone\] demo=0 .* call=(?:0|4)(?:\r?$)',preflight,re.M),('unexpected call/demo',preflight)
  command('lcd bench off');command('key home');check('clock root',3,history=0)
  for i,page in enumerate([11,12,14,16,3]):command('key right');check(f'right {i}',page,history=0)
  for i,page in enumerate([16,14,12,11,3]):command('key left');check(f'left {i}',page,history=0)
  for page in [3,11,12,14,16]:
   command({3:'page home',11:'page contact',12:'page sms',14:'page signal',16:'page settings'}[page]);command('key down');check(f'bar {page}',page,1,0)
   command('key left');check(f'left clamp {page}',page,1,0)
   for _ in range(3):command('key right')
   check(f'right clamp {page}',page,1,2)
   command('key up');check(f'restore content {page}',page,0)
  for entry,child in [('contact',13),('sms',6)]:
   command('page '+entry);command('key select');check(entry+' center child',child,history=1)
   command('key back');check(entry+' back',11 if entry=='contact' else 12,history=0)
   command('key down');command('key select');check(entry+' footer child',child,history=1)
   command('key down');check(entry+' child footer',child,1,0)
   command('key up');check(entry+' child restore',child)
   command('key down');command('key right');command('key select');check(entry+' home from child',3,history=0)
  command('page signal');command('key select');check('signal center child',15,history=1)
  for _ in range(40):command('key down',.04)
  d=check('signal bottom bar',15,1,0,1);assert d['signal_scroll']>0
  command('key up');check('signal restore reading',15,0,history=1)
  command('key select');check('signal content refresh',15,0,history=1)
  command('key back');check('signal back to entry',14,0,history=0)
  command('key down');command('key select');check('signal footer child',15,0,history=1)
  for _ in range(40):command('key down',.04)
  command('key right');command('key select');check('signal home from child',3,history=0)
  command('page sms');command('key back');check('top back to clock',3,history=0)
  command('key back');check('clock back stays',3,history=0)
  command('key down');command('key select');check('clock footer select no-op',3,1,0)
  command('key home');command('tasks');read(1)
 finally:
  command('lcd bench off');command('key home');read(1)
  (out/'results.json').write_text(json.dumps(cases,indent=2),encoding='utf8')
print('Validated',len(cases),'states; no real call or SMS command sent')
