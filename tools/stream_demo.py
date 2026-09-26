#!/usr/bin/env python3
"""
Send joint frames to CyberHand Studio's Stream mode over UDP.

  python stream_demo.py                   # smooth wave on all 6 joints, 50 Hz, percent
  python stream_demo.py --csv file.csv    # replay a trajectory CSV (degrees) in real time
  python stream_demo.py --host 192.168.1.20 --port 7400 --rate 30

In the app: Stream mode -> Start. Unit "% hành trình" for the wave, "Độ" for --csv.
Tick "Mirror Enabled" to forward the frames to the hand.
"""
import argparse
import csv
import json
import math
import socket
import time


def wave(t):
    return [50 - 50 * math.cos(2 * math.pi * 0.2 * t - i * 0.7) for i in range(6)]


def load_csv(path):
    keys = ["T2", "T1", "I", "M", "R", "P"]
    rows = []
    with open(path, newline="") as f:
        lines = [l for l in f if l.strip() and not l.startswith("#")]
    for r in csv.DictReader(lines):
        t = float(r.get("t") or r.get("time") or 0)
        q = [float(r[k]) if r.get(k) not in (None, "") else None for k in keys]
        rows.append((t, q))
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=7400)
    ap.add_argument("--rate", type=float, default=50)
    ap.add_argument("--csv")
    a = ap.parse_args()
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rows = load_csv(a.csv) if a.csv else None
    t0 = time.time()
    print("sending to %s:%d at %g Hz (Ctrl+C to stop)" % (a.host, a.port, a.rate))
    while True:
        t = time.time() - t0
        if rows:
            dur = rows[-1][0]
            tt = t % dur if dur > 0 else 0
            q = next((r[1] for r in rows if r[0] >= tt), rows[-1][1])
            frame = {"q": q}
        else:
            frame = {"q": [round(v, 1) for v in wave(t)]}
        sock.sendto(json.dumps(frame).encode(), (a.host, a.port))
        time.sleep(1.0 / a.rate)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
