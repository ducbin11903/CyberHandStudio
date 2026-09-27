#!/usr/bin/env python3
"""
CyberHand firmware simulator — speaks the same CLI and log format as the
STM32H562 bench firmware (FW_H5 V1.7.0), so CyberHand Studio can be tested
without hardware.

Linux/macOS:  python3 fw_sim.py            -> prints a /dev/pts/N path, open it in the app
Windows:      install com0com (virtual COM pair, e.g. COM20<->COM21), then
              python fw_sim.py --port COM21  -> open COM20 in the app (needs pyserial)

Options: --joints 6 to emulate the proposed 6-joint log (q1..q6, t1..t6, i1..i6).
"""
import argparse
import math
import os
import random
import sys
import time

CNT90 = 7000.0   # 28000 cnt/rev output (7 PPR x4 x 1:1000), joint 1:1


def deg2cnt(d):
    return d * CNT90 / 90.0


class Motor:
    def __init__(self):
        self.pos = 0.0          # counts (after homing: 0 = extended stop)
        self.stop = -random.uniform(1000, 3000)  # mechanical stop, unknown before homing
        self.speed = 0.0        # counts/s
        self.out = 0.0          # applied duty %
        self.tgt = 0.0
        self.i_ma = 0.0
        self.stalled = False

    def step(self, dt, duty):
        self.out = duty
        vmax = 7000.0           # counts/s at 100 % duty (15 RPM output)
        tau = 0.08
        target_speed = duty / 100.0 * vmax
        self.speed += (target_speed - self.speed) * min(1.0, dt / tau)
        newpos = self.pos + self.speed * dt
        hi = self.stop + 7500   # flexion stop
        self.stalled = False
        if newpos <= self.stop:
            newpos = self.stop
            self.stalled = abs(duty) > 1
            self.speed = 0
        elif newpos >= hi:
            newpos = hi
            self.stalled = abs(duty) > 1
            self.speed = 0
        self.pos = newpos
        load = 900.0 if self.stalled else 90.0 + abs(self.speed) * 0.05
        # OCM behind an RC filter reads about duty x real current
        real = load * min(1.0, abs(duty) / 100.0 * 1.6) if abs(duty) > 0.5 else 0.0
        self.i_ma += (real - self.i_ma) * min(1.0, dt / 0.03)


class Firmware:
    def __init__(self, joints, write):
        self.write = write
        self.n = joints
        self.motors = [Motor() for _ in range(joints)]
        self.en = 0
        self.flt = 0
        self.mode = 0            # 0 open loop, 1 PID, 2 homing
        self.duty = 0.0          # open-loop setpoint
        self.limit = 100.0
        self.kp, self.ki, self.kd = 0.002, 0.0, 0.00002
        self.log_hz = 5.0
        self.vel_dps = 0.0
        self.home = None         # dict while homing
        self.buf = b""
        self.last_log = 0.0
        self.write("USBX: OK")
        self.write("ID: CyberHand-H562 sim fw 0.4 joints=%d cnt90=%d" % (joints, CNT90))

    # --- CLI -----------------------------------------------------------
    def feed(self, data):
        self.buf += data
        while True:
            idx = min([i for i in (self.buf.find(b"\n"), self.buf.find(b"\r")) if i >= 0], default=-1)
            if idx < 0:
                break
            line = self.buf[:idx].decode(errors="replace").strip()
            self.buf = self.buf[idx + 1:]
            if line:
                self.command(line)

    def ok(self, msg):
        self.write("OK " + msg)

    def err(self, msg):
        self.write("ERR " + msg)

    def command(self, line):
        parts = line.split()
        c, args = parts[0], parts[1:]

        def num(i, default=None):
            try:
                return float(args[i])
            except (IndexError, ValueError):
                return default

        if c == "h":
            for l in HELP.strip("\n").split("\n"):
                self.write(l)
        elif c == "e":
            self.en = 1
            self.duty = 0
            self.ok("driver enabled")
        elif c == "d":
            self.en = 0
            self.mode = 0
            self.duty = 0
            self.home = None
            self.ok("driver disabled")
        elif c == "x":
            v = num(0)
            if v is None or not 0 <= v <= 100:
                return self.err("x <0..100>")
            self.limit = v
            self.ok("limit %d%%" % v)
        elif c == "o":
            v = num(0)
            if v is None:
                return self.err("o <pct>")
            self.mode = 0
            self.duty = max(-100, min(100, v))
            self.ok("duty %d%%" % self.duty)
        elif c in "+-" and len(c) == 1:
            self.mode = 0
            self.duty = max(-100, min(100, self.duty + (5 if c == "+" else -5)))
            self.ok("duty %d%%" % self.duty)
        elif c == "r":
            self.mode = 0
            self.duty = -self.duty
            self.ok("duty %d%%" % self.duty)
        elif c == "0":
            self.mode = 0
            self.duty = 0
            self.ok("duty 0")
        elif c == "g":
            if len(args) >= 2 and self.n > 1:
                j, d = int(num(0, 1)) - 1, num(1)
            else:
                j, d = 0, num(0)
            if d is None or not 0 <= j < self.n:
                return self.err("g <deg>")
            self.vel_dps = 0
            self.mode = 1
            self.motors[j].tgt = deg2cnt(max(0, min(90, d)))
            self.ok("goto %.1f deg" % d)
        elif c == "G":
            vals = [num(i) for i in range(self.n)]
            if any(v is None for v in vals):
                return self.err("G q1..q%d" % self.n)
            self.vel_dps = 0
            self.mode = 1
            for m, d in zip(self.motors, vals):
                m.tgt = deg2cnt(max(0, min(90, d)))
            self.ok("goto all")
        elif c == "p":
            v = num(0)
            if v is None:
                return self.err("p <cnt>")
            self.vel_dps = 0
            self.mode = 1
            self.motors[0].tgt = v
            self.ok("pos %d" % v)
        elif c == "t":
            v = num(0)
            if v is None:
                return self.err("t <cnt>")
            self.vel_dps = 0
            self.mode = 1
            self.motors[0].tgt = self.motors[0].pos + v
            self.ok("step %d" % v)
        elif c == "v":
            v = num(0)
            if v is None:
                return self.err("v <deg/s>")
            self.mode = 1
            self.vel_dps = v
            for m in self.motors:
                m.tgt = m.pos
            self.ok("vel %.1f deg/s" % v)
        elif c == "k":
            if len(args) < 3:
                return self.err("k kp ki kd")
            self.kp, self.ki, self.kd = num(0), num(1), num(2)
            self.ok("pid %g %g %g" % (self.kp, self.ki, self.kd))
        elif c == "H":
            pct = num(0, 30)
            ma = num(1, 250)
            if not self.en:
                return self.err("driver disabled")
            self.mode = 2
            self.home = {"pct": pct, "ma": ma, "t0": time.time()}
            self.ok("homing %d%% %dmA" % (pct, ma))
        elif c == "z":
            for m in self.motors:
                m.stop -= m.pos
                m.pos = 0
                m.tgt = 0
            self.ok("zero")
        elif c == "c":
            self.flt = 0
            self.ok("faults cleared")
        elif c == "s":
            self.log_line()
        elif c == "l":
            v = num(0)
            if v is None or not 0 <= v <= 100:
                return self.err("l <0..100>")
            self.log_hz = v
            self.ok("log %d Hz" % v)
        elif c == "i":
            self.write("ID: CyberHand-H562 sim fw 0.4 joints=%d cnt90=%d" % (self.n, CNT90))
        else:
            self.err("unknown command '%s' (h for help)" % c)

    # --- control loop --------------------------------------------------
    no_vm = False

    def step(self, dt):
        for m in self.motors:
            duty = 0.0
            if self.en and not (self.flt & 2):
                if self.mode == 0:
                    duty = self.duty
                elif self.mode == 1:
                    if self.vel_dps:
                        m.tgt += deg2cnt(self.vel_dps) * dt
                        m.tgt = max(0, min(deg2cnt(90), m.tgt))
                    e = m.tgt - m.pos
                    duty = max(-self.limit, min(self.limit, e * 0.35))
                elif self.mode == 2 and self.home:
                    duty = -abs(self.home["pct"])
            if self.no_vm:
                m.out = duty
                m.i_ma = 0.0
                m.speed = 0.0
                m.pos = float(random.choice([0, 1]))   # encoder sitting on an edge
            else:
                m.step(dt, duty)
            if m.i_ma > 800:
                self.flt |= 2
        if self.en == 0:
            self.flt &= ~2
        if self.mode == 2 and self.home:
            m = self.motors[0]
            if m.stalled and m.i_ma > self.home["ma"]:
                for mm in self.motors:
                    mm.stop -= mm.pos
                    mm.pos = 0
                    mm.tgt = 0
                self.mode = 1
                self.home = None
                self.write("HOME: done")
            elif time.time() - self.home["t0"] > 10:
                self.mode = 0
                self.home = None
                self.write("HOME: timeout")
        now = time.time()
        if self.log_hz > 0 and now - self.last_log >= 1.0 / self.log_hz:
            self.last_log = now
            self.log_line()

    def log_line(self):
        m = self.motors[0]
        diag = 1 if (self.en and not self.no_vm) else 0
        if self.n == 1:
            self.write("en:%d,duty:%d,out:%.1f,i_mA:%d,flt:%d,diag:%d,pos:%d,tgt:%d,vel:%d,mode:%d" % (
                self.en, self.duty, m.out, m.i_ma, self.flt, diag, m.pos,
                m.tgt if self.mode == 1 else 0,
                (random.choice([-150, 0, 150]) if self.no_vm else m.speed), self.mode))
        else:
            q = ",".join("q%d:%d" % (i + 1, mm.pos) for i, mm in enumerate(self.motors))
            t = ",".join("t%d:%d" % (i + 1, mm.tgt) for i, mm in enumerate(self.motors))
            c = ",".join("i%d:%d" % (i + 1, mm.i_ma) for i, mm in enumerate(self.motors))
            self.write("%s,%s,%s,en:%d,flt:%d,mode:%d" % (q, t, c, self.en, self.flt, self.mode))


HELP = """
CyberHand H562 sim — commands (end with CR or LF):
  e / d          enable driver / disable driver
  x <pct>        |duty| limit 0..100 (open loop and PID)
  o <pct>        open loop duty;  + / - step 5%;  r reverse;  0 stop
  g <deg>        PID to joint angle (0 = extended)   [6 joints: g <j> <deg>, G q1..q6]
  p <cnt> / t <cnt>   PID to absolute counts / relative step
  v <deg/s>      constant joint speed, v 0 holds
  k kp ki kd     PID gains
  H [pct] [mA]   home: back off to the extension stop, stop on current/stall, set 0
  z  c  s        zero here, clear faults, print one log line
  l <hz>         log rate (<= 100 Hz)
  i  h           id line, this help
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="real/virtual serial port (needs pyserial); default: create a pty")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--joints", type=int, default=1, choices=[1, 6])
    ap.add_argument("--link", help="also create a symlink to the pty at this path")
    ap.add_argument("--no-vm", action="store_true", help="simulate missing motor supply: no motion, no current, DIAG low")
    a = ap.parse_args()

    if a.port:
        import serial  # pyserial
        ser = serial.Serial(a.port, a.baud, timeout=0)
        read = lambda: ser.read(4096)
        write_raw = lambda b: ser.write(b)
        print("fw_sim on %s" % a.port, flush=True)
    else:
        import pty
        import tty
        master, slave = pty.openpty()
        tty.setraw(slave)
        name = os.ttyname(slave)
        os.set_blocking(master, False)
        if a.link:
            try:
                os.remove(a.link)
            except FileNotFoundError:
                pass
            os.symlink(name, a.link)
        print(name, flush=True)

        def read():
            try:
                return os.read(master, 4096)
            except (BlockingIOError, OSError):
                return b""

        def write_raw(b):
            try:
                os.write(master, b)
            except OSError:
                pass

    fw = Firmware(a.joints, lambda s: write_raw((s + "\r\n").encode()))
    fw.no_vm = a.no_vm
    last = time.time()
    while True:
        data = read()
        if data:
            fw.feed(data)
        now = time.time()
        fw.step(now - last)
        last = now
        time.sleep(0.001)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
