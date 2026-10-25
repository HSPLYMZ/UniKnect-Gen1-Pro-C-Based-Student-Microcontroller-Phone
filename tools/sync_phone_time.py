"""Set the firmware's Beijing software clock over the explicit ST-LINK VCP.

Without --port this only prints a command. It never probes a port or resets a board.
"""
import argparse
from datetime import datetime, timezone, timedelta
import sys
import time

BEIJING = timezone(timedelta(hours=8))


def clock_command(now=None):
    now = now or datetime.now(timezone.utc)
    if now.tzinfo is None:
        raise ValueError('Provide a timezone-aware datetime')
    return 'clock ' + now.astimezone(BEIJING).strftime('%Y-%m-%d %H:%M:%S')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--port', help='ST-LINK console, e.g. COM3; omitted = print only')
    args = ap.parse_args()
    if not args.port:
        print(clock_command())
        return 0
    import serial
    try:
        with serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=1) as link:
            link.dtr = False
            link.rts = False
            link.port = args.port
            link.open()
            command = clock_command()
            link.write((command + '\r\n').encode('ascii'))
            link.flush()
            print(command)
            deadline = time.monotonic() + 2
            while time.monotonic() < deadline:
                chunk = link.read(4096)
                if chunk:
                    sys.stdout.write(chunk.decode('utf-8', 'replace'))
    except (OSError, serial.SerialException) as exc:
        print(f'Clock sync failed: {exc}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
