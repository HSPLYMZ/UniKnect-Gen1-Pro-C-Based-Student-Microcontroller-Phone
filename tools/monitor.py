"""Read the board console (USART3 via the ST-LINK VCP) and print it.

Usage:
    .venv/Scripts/python.exe tools/monitor.py            # COM3, 8 s
    .venv/Scripts/python.exe tools/monitor.py -t 30      # 30 s
    .venv/Scripts/python.exe tools/monitor.py -p COM7    # other port
    .venv/Scripts/python.exe tools/monitor.py -s "cmd"   # send a line first
"""
import argparse
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("-p", "--port", default="COM3")
    ap.add_argument("-b", "--baud", type=int, default=115200)
    ap.add_argument("-t", "--seconds", type=float, default=8.0)
    ap.add_argument("-s", "--send", default=None, help="line to send after opening")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except Exception as exc:  # noqa: BLE001 - report and exit
        print(f"open {args.port} failed: {exc}", file=sys.stderr)
        return 1

    if args.send:
        ser.write((args.send + "\r\n").encode())

    deadline = time.time() + args.seconds
    buf = b""
    try:
        while time.time() < deadline:
            chunk = ser.read(4096)
            if chunk:
                buf += chunk
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()

    print(f"\n--- {len(buf)} bytes in {args.seconds:.0f}s ---")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
