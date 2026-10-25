"""Read-only EC200U USB AT queries with explicit per-command verdicts."""
import argparse
from dataclasses import dataclass
import json
import sys
import time
import serial

DEFAULT_CMDS = ['AT', 'ATI', 'AT+CGMR', 'AT+CPIN?', 'AT+CSQ',
                'AT+QENG="servingcell"', 'AT+QPCMV=?', 'AT+QPCMV?',
                'AT+QDAI?', 'AT+CLVL?', 'AT+QRXGAIN?', 'AT+QCFG="usbnet"']


@dataclass
class QueryResult:
    command: str
    status: str
    response: str
    accepted: bool


def terminal_status(data):
    # Only a complete, independent final-result line terminates the query.
    for line in data.replace(b'\r\n', b'\n').split(b'\n')[:-1]:
        line = line.strip()
        if line == b'OK':
            return 'ok'
        if line in (b'ERROR', b'FAIL') or line.startswith((b'+CME ERROR:', b'+CMS ERROR:')):
            return 'error'
    return None


def query(port, baud, cmds, wait=0.6, timeout=3.0, allow_error=()):
    results = []
    with serial.Serial(port, baud, timeout=0.2, write_timeout=2) as ser:
        for cmd in cmds:
            ser.reset_input_buffer()
            ser.write((cmd + '\r\n').encode())
            ser.flush()
            deadline = time.monotonic() + timeout
            buf, status = b'', None
            while time.monotonic() < deadline and status is None:
                buf += ser.read(256)
                status = terminal_status(buf)
            status = status or 'timeout'
            response = buf.decode('utf-8', 'replace').strip()
            accepted = status == 'ok' or (status == 'error' and cmd in allow_error)
            result = QueryResult(cmd, status, response, accepted)
            results.append(result)
            print(f'>>> {cmd} [{status}, {"accepted" if accepted else "failed"}]')
            print(response)
            time.sleep(wait)
    return results


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', default='COM27')
    ap.add_argument('--baud', type=int, default=115200)
    ap.add_argument('--cmd', action='append', help='replacement command; may repeat')
    ap.add_argument('--allow-error', action='append', default=[], metavar='COMMAND',
                    help='explicitly accept ERROR for this capability query')
    ap.add_argument('--json', dest='json_path', help='save structured results')
    args = ap.parse_args(argv)
    try:
        results = query(args.port, args.baud, args.cmd or DEFAULT_CMDS, allow_error=args.allow_error)
        if args.json_path:
            from pathlib import Path
            Path(args.json_path).write_text(json.dumps([r.__dict__ for r in results],
                                                       ensure_ascii=False, indent=2), encoding='utf-8')
        return 0 if all(r.accepted for r in results) else 1
    except (serial.SerialException, OSError) as exc:
        print(f'probe failed: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
