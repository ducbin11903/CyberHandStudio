#!/usr/bin/env python3
"""
CyberHand firmware simulator — speaks the same CLI and log format as the
STM32H562 bench firmware (FW_H5 V1.7.0), so CyberHand Studio can be tested
without hardware.

Linux/macOS:  python3 fw_sim.py            -> prints a /dev/pts/N path, open it in the app
Windows:      install com0com (virtual COM pair, e.g. COM20<->COM21), then
              python fw_sim.py --port COM21  -> open COM20 in the app (needs pyserial)

Options: --joints 6 to emulate the proposed 6-joint log (q1..q6, t1..t6, i1..i6).
         --g474 U6|U7 to emulate one MCU of the MCB board (fw 0.7-g474): 3 motors,
         U6 = I, T1, T2 and U7 = M, R, P (motor 1/2/3), commands m / e d c [key] / D,
         g <joint 1-6|key> <deg> (joints of the other MCU are ignored without a reply),
         log = 10 channels of the selected motor + qN/tN/iN for the 3 joints.
         fw 0.9 board sensors (SHT40 + VBAT on U7): log temp/rh/vbat, `sense`, OTEMP / VBAT
         protection (all motors off, flt 16 / 32, `e` refused); test commands simtemp <C>,
         simvbat <V>, simsht off|on; start values --temp / --vbat.
         --g474 U6 --bridge: fw 0.8-g474 with the UART link, this MCU has the USB port and
         bridges the other one (keys = all 6, link=U7, text of U7 prefixed "[U7] ",
         @U7 <line> raw, `link on|off` to simulate losing the link, log + lk:1).
Two ports (app mode "2 cổng"): run two sims, --g474 U6 and --g474 U7.
WiFi (app mode "WiFi"): --tcp 8888 serves the chosen firmware (any of the options above) over TCP,
         one client like the ESP32-S2 bridge; the app connects to 127.0.0.1:8888.
CAN (app mode "CAN", adapter "Virtual (UDP)"): --can-udp 7470 runs both MCUs as CAN nodes
         (fw 0.11-g474, protocol CAN v1 in docs/PROTOCOL.md): TEXT 0x280+n, STATUS 0x300+n at
         `canlog <hz>`, POS_ALL, E-STOP, HOST_HB watchdog; sim command `simdrop <n>` skips STATUS seq.
"""
import argparse
import math
import os
import random
import struct
import sys
import time

CNT90 = 7000.0   # 28000 cnt/rev output (7 PPR x4 x 1:1000), joint 1:1
FW_VERSION = "0.5"


class Motor:
    def __init__(self):
        self.pos = 0.0          # counts (after homing: 0 = extended stop)
        self.stop = -random.uniform(1000, 3000)  # mechanical stop, unknown before homing
        self.speed = 0.0        # counts/s
        self.out = 0.0          # applied duty %
        self.tgt = 0.0
        self.i_ma = 0.0
        self.i_rep = 0.0        # reported current after the `a <hz>` averaging
        self.over_ms = 0.0
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
        self.cnt90 = CNT90       # `j <cnt>`
        self.itrip = 500.0       # `I <mA> <ms> <resp> <deg>`
        self.itrip_ms = 300.0
        self.iresp = 0           # 0 trip, 1 retract, 2 hold, 3 warn
        self.retract_deg = 10.0
        self.i_hz = 1000.0       # `a <hz>` current averaging
        self.homed = False       # zero set by `z` or homing (CAN STATUS bit)
        self.buf = b""
        self.last_log = 0.0
        self.write("USBX: OK")
        self.write(self.id_line())

    def id_line(self):
        return "ID: CyberHand-H562 sim fw %s joints=%d cnt90=%d" % (FW_VERSION, self.n, self.cnt90)

    def deg2cnt(self, d):
        return d * self.cnt90 / 90.0

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
            self.motors[j].tgt = self.deg2cnt(max(0, min(90, d)))
            self.ok("goto %.1f deg" % d)
        elif c == "G":
            vals = [num(i) for i in range(self.n)]
            if any(v is None for v in vals):
                return self.err("G q1..q%d" % self.n)
            self.vel_dps = 0
            self.mode = 1
            for m, d in zip(self.motors, vals):
                m.tgt = self.deg2cnt(max(0, min(90, d)))
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
            self.homed = True
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
            self.write(self.id_line())
        elif c == "j":
            v = num(0)
            if v is None:
                return self.ok("cnt90=%d" % self.cnt90)
            if not 100 <= v <= 1000000:
                return self.err("j <100..1000000>")
            self.cnt90 = float(int(v))
            self.ok("cnt90=%d" % self.cnt90)
        elif c == "I":
            if not args:
                return self.ok("trip i>%d mA %d ms resp=%d retract=%.1f deg" % (self.itrip, self.itrip_ms, self.iresp, self.retract_deg))
            ma, ms, resp, deg = num(0), num(1, self.itrip_ms), num(2, self.iresp), num(3, self.retract_deg)
            if ma is None or ma <= 0:
                return self.err("I <mA> [ms] [resp 0 trip|1 retract|2 hold|3 warn] [deg]")
            self.itrip, self.itrip_ms, self.iresp, self.retract_deg = ma, max(1, ms), int(max(0, min(3, resp))), max(0.5, deg)
            self.ok("trip i>%d mA %d ms resp=%d retract=%.1f deg" % (self.itrip, self.itrip_ms, self.iresp, self.retract_deg))
        elif c == "a":
            v = num(0)
            if v is None or not 1 <= v <= 1000:
                return self.err("a <1..1000>")
            self.i_hz = v
            self.ok("i avg %d Hz" % v)
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
                        m.tgt += self.deg2cnt(self.vel_dps) * dt
                        m.tgt = max(0, min(self.deg2cnt(90), m.tgt))
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
            # reported current: mean over 1/i_hz s
            m.i_rep += (m.i_ma - m.i_rep) * min(1.0, dt * self.i_hz)
            # over-current protection, same rules as the firmware
            if self.en and m.i_ma > self.itrip:
                m.over_ms += dt * 1000.0
                if m.over_ms >= self.itrip_ms:
                    m.over_ms = 0.0
                    self.over_current(m)
            else:
                m.over_ms = 0.0
        if self.mode == 2 and self.home:
            m = self.motors[0]
            if m.stalled and m.i_ma > self.home["ma"]:
                for mm in self.motors:
                    mm.stop -= mm.pos
                    mm.pos = 0
                    mm.tgt = 0
                self.mode = 1
                self.home = None
                self.homed = True
                self.write("HOME: done")
            elif time.time() - self.home["t0"] > 10:
                self.mode = 0
                self.home = None
                self.write("HOME: timeout")
        now = time.time()
        if self.log_hz > 0 and now - self.last_log >= 1.0 / self.log_hz:
            self.last_log = now
            self.log_line()

    def over_current(self, m):
        i = int(m.i_ma)
        if self.iresp == 0:
            self.en = 0
            self.mode = 0
            self.duty = 0
            self.flt |= 2
            self.write("OVERI: trip i=%d mA" % i)
        elif self.iresp == 1:
            self.mode = 1
            self.vel_dps = 0
            self.home = None
            m.tgt = max(0.0, m.pos - self.deg2cnt(self.retract_deg))
            self.write("OVERI: retract i=%d mA to %d cnt" % (i, m.tgt))
        elif self.iresp == 2:
            self.mode = 1
            self.vel_dps = 0
            self.home = None
            m.tgt = m.pos
            self.write("OVERI: hold i=%d mA at %d cnt" % (i, m.tgt))
        else:
            self.write("OVERI: warn i=%d mA" % i)

    def log_line(self):
        m = self.motors[0]
        diag = 1 if (self.en and not self.no_vm) else 0
        if self.n == 1:
            self.write("en:%d,duty:%d,out:%.1f,i_mA:%d,flt:%d,diag:%d,pos:%d,tgt:%d,vel:%d,mode:%d" % (
                self.en, self.duty, m.out, m.i_rep, self.flt, diag, m.pos,
                m.tgt if self.mode == 1 else 0,
                (random.choice([-150, 0, 150]) if self.no_vm else m.speed), self.mode))
        else:
            q = ",".join("q%d:%d" % (i + 1, mm.pos) for i, mm in enumerate(self.motors))
            t = ",".join("t%d:%d" % (i + 1, mm.tgt) for i, mm in enumerate(self.motors))
            c = ",".join("i%d:%d" % (i + 1, mm.i_rep) for i, mm in enumerate(self.motors))
            self.write("%s,%s,%s,en:%d,flt:%d,mode:%d" % (q, t, c, self.en, self.flt, self.mode))


# ---------------------------------------------------------------------------
# MCB fw 0.9: board sensors on U7 (SHT40 + VBAT divider), protection on every MCU

T_OFF, T_ON, V_OFF, V_ON = 70.0, 60.0, 6.40, 6.80


class BoardSensor:
    def __init__(self, temp=25.0, vbat=7.99):
        self.t_set = temp
        self.rh_set = 45.0
        self.v_set = vbat
        self.sht = True
        self.i2c_err = 0
        self._err_s = 0.0
        self.temp = temp
        self.rh = self.rh_set
        self.vbat = vbat
        self.load_ma = 0.0

    def step(self, dt):
        self.temp = self.t_set + random.uniform(-0.05, 0.05)
        self.rh = max(0.0, min(100.0, self.rh_set + random.uniform(-0.2, 0.2)))
        if self.v_set >= 3.0:
            sag = self.load_ma / 1000.0 * 0.15          # ~0.15 ohm pack + wiring
            self.vbat = max(0.0, self.v_set - sag + random.uniform(-0.004, 0.004))
        else:
            self.vbat = self.v_set                      # < 3 V: no battery
        if not self.sht:
            self._err_s += dt
            if self._err_s >= 1.0:
                self._err_s = 0.0
                self.i2c_err += 1

    def fields(self):
        f = []
        if self.sht:
            f += ["temp:%.1f" % self.temp, "rh:%.1f" % self.rh]
        f.append("vbat:%.2f" % self.vbat)
        return ",".join(f)


class Protect:
    """one MCU: its motors off on board over-temperature / low battery"""
    def __init__(self, g):
        self.g = g
        self.hot = False
        self.low = False
        self.low_ms = 0.0

    def step(self, dt, sensor):
        if sensor is None:          # no data (link to U7 down)
            return
        if sensor.sht:
            t = sensor.temp
            if not self.hot and t >= T_OFF:
                self.hot = True
                self.trip(16)
                self.g.write("OTEMP: board %.1f C >= %.1f C (SHT40 on U7), all motors off (flt 16)" % (t, T_OFF))
            elif self.hot and t < T_ON:
                self.hot = False
                self.g.write("OTEMP: cleared (< %.1f C), send e to run again" % T_ON)
        v = sensor.vbat
        if v < 3.0:                 # no battery: not checked
            self.low_ms = 0.0
        elif not self.low:
            if v < V_OFF:
                self.low_ms += dt * 1000.0
                if self.low_ms >= 2000.0:
                    self.low = True
                    self.trip(32)
                    self.g.write("VBAT: battery %.2f V < %.2f V, all motors off (flt 32)" % (v, V_OFF))
            else:
                self.low_ms = 0.0
        elif v >= V_ON:
            self.low = False
            self.low_ms = 0.0
            self.g.write("VBAT: ok (%.2f V), send e to run again" % v)

    def trip(self, bit):
        for ch in self.g.ch:
            ch.en = 0
            ch.mode = 0
            ch.duty = 0
            ch.flt |= bit

    def refusal(self):
        if self.hot:
            return "ERR board over-temperature, motors stay off ('sense')"
        if self.low:
            return "ERR board battery low, motors stay off ('sense')"
        return None


def sense_line(sensor, prot):
    if sensor is None:
        return "ERR no sensor data (SHT40 + VBAT are on U7, link to U7 is down)"
    tv = ("T=%.1f C RH=%.1f %%" % (sensor.temp, sensor.rh)) if sensor.sht else "T=n/a C RH=n/a %"
    s = "OK sense U7 %s VBAT=%.2f V | off at T>=%.1f C (on <%.1f) VBAT<%.2f V (on >=%.2f) | hot=%d low=%d i2c_err=%d" % (
        tv, sensor.vbat, T_OFF, T_ON, V_OFF, V_ON, 1 if prot.hot else 0, 1 if prot.low else 0, sensor.i2c_err)
    if not sensor.sht:
        s += " (T invalid)"
    return s


# MCB G474: one MCU, 3 motors (fw 0.7-g474)

G474_KEYS = {"U6": ["I", "T1", "T2"], "U7": ["M", "R", "P"]}
APP_ORDER = ["T2", "T1", "I", "M", "R", "P"]   # joint numbers 1..6 used by g <j> and qN/tN/iN


class G474:
    VERSION = "0.9-g474"

    def __init__(self, mcu, write, banner=True, sensor=None):
        self.write = write
        self.mcu = mcu
        # the sensor is on U7; a standalone U6 (2-port sim) stands in for the values it gets over the link
        self.sensor = sensor if sensor is not None else BoardSensor()
        self.owns_sensor = True
        self.keys = G474_KEYS[mcu]
        self.sel = 0
        self.log_hz = 5.0
        self.last_log = 0.0
        self.buf = b""
        self._mute = True
        self.ch = []
        for _ in self.keys:
            fw = Firmware(1, self._chan_write)
            fw.log_hz = 0
            self.ch.append(fw)
        self.protect = Protect(self)
        self._mute = False
        if banner:
            self.write("USBX: OK")
            self.write(self.id_line())

    @property
    def no_vm(self):
        return self.ch[0].no_vm

    @no_vm.setter
    def no_vm(self, v):
        for c in self.ch:
            c.no_vm = v

    def _chan_write(self, s):
        if not self._mute:
            self.write(s)

    def id_line(self):
        return "ID: CyberHand-G474 fw %s joints=3 cnt90=%d mcu=%s motor=%d(%s) keys=%s" % (
            self.VERSION, self.ch[self.sel].cnt90, self.mcu, self.sel + 1, self.keys[self.sel], ",".join(self.keys))

    def board(self):
        """sensor values this MCU has (None = none)"""
        return self.sensor

    def sim_board(self, c, args):
        """simtemp <C> / simvbat <V> / simsht off|on (test commands)"""
        b = self.sensor
        try:
            if c == "simtemp":
                b.t_set = float(args[0])
                return self.ok("simtemp %.1f C" % b.t_set)
            if c == "simvbat":
                b.v_set = float(args[0])
                return self.ok("simvbat %.2f V" % b.v_set)
            b.sht = args[0].lower() in ("on", "1")
            return self.ok("simsht %s" % ("on" if b.sht else "off"))
        except (IndexError, ValueError):
            return self.err("%s <value>" % c)

    def ok(self, msg):
        self.write("OK " + msg)

    def err(self, msg):
        self.write("ERR " + msg)

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

    def motor_of(self, tok):
        """motor index on this MCU from a key or 1..3, else None"""
        for k, key in enumerate(self.keys):
            if tok.upper() == key:
                return k
        if tok.isdigit() and 1 <= int(tok) <= 3:
            return int(tok) - 1
        return None

    def joint_of(self, tok):
        """g <j|key>: (motor index or None, known joint)"""
        key = None
        if tok.isdigit() and 1 <= int(tok) <= 6:
            key = APP_ORDER[int(tok) - 1]
        elif tok.upper() in APP_ORDER:
            key = tok.upper()
        if key is None:
            return None, False
        return (self.keys.index(key) if key in self.keys else None), True

    def goto(self, k, deg):
        c = self.ch[k]
        self._mute = True
        c.command("g %s" % deg)
        self._mute = False
        self.ok("pos %d cnt %s" % (c.motors[0].tgt, self.keys[k]))

    def command(self, line):
        parts = line.split()
        c, args = parts[0], parts[1:]
        if c == "i":
            self.write(self.id_line())
        elif c == "h":
            for l in HELP_G474.strip("\n").split("\n"):
                self.write(l)
        elif c == "l":
            try:
                v = float(args[0])
            except (IndexError, ValueError):
                return self.err("l <0..100>")
            self.log_hz = max(0.0, min(100.0, v))
            self.ok("log %d Hz" % self.log_hz)
        elif c == "m":
            if not args:
                return self.ok("m %d %s" % (self.sel + 1, self.keys[self.sel]))
            k = self.motor_of(args[0])
            if k is None:
                return self.err("m %s: not on %s (keys=%s)" % (args[0], self.mcu, ",".join(self.keys)))
            self.sel = k
            self.ok("m %d %s" % (k + 1, self.keys[k]))
        elif c == "g" and len(args) >= 2:
            k, known = self.joint_of(args[0])
            if not known:
                return self.err("g <joint 1-6|key> <deg>")
            if k is None:
                return  # joint of the other MCU: ignored, no reply
            self.goto(k, args[1])
        elif c == "g":
            if not args:
                return self.err("g <deg>")
            self.goto(self.sel, args[0])
        elif c in ("e", "d", "c"):
            targets = range(3)
            if args:
                k = self.motor_of(args[0])
                if k is None:
                    return self.err("%s %s: not on %s" % (c, args[0], self.mcu))
                targets = [k]
            if c == "e":
                refuse = self.protect.refusal()
                if refuse:
                    return self.write(refuse)
            self._mute = True
            for k in targets:
                self.ch[k].command(c)
                if c == "e":
                    self.ch[k].flt &= ~(16 | 32)
            self._mute = False
            self.ok("%s %s" % (c, self.keys[targets[0]] if args else "all"))
        elif c == "sense":
            self.write(sense_line(self.board(), self.protect))
        elif c in ("simtemp", "simvbat", "simsht"):
            self.sim_board(c, args)
        elif c == "D":
            self._mute = True
            for ch in self.ch:
                ch.command("d")
            self._mute = False
            self.ok("D all off")
        elif c == "s":
            self.log_line()
        elif c == "G":
            self.err("G not supported, use g <joint> <deg>")
        elif c in ("o", "+", "-", "r", "0", "p", "t", "v", "k", "x", "H", "z", "j", "I", "a"):
            self.ch[self.sel].command(line)
        else:
            self.err("unknown command '%s' (h for help)" % c)

    def step(self, dt):
        for ch in self.ch:
            ch.step(dt)
        if self.owns_sensor:
            self.sensor.load_ma = sum(ch.motors[0].i_rep for ch in self.ch)
            self.sensor.step(dt)
        self.protect.step(dt, self.board())
        now = time.time()
        if self.log_hz > 0 and now - self.last_log >= 1.0 / self.log_hz:
            self.last_log = now
            self.log_line()

    def head(self):
        """10 channels of the selected motor"""
        c = self.ch[self.sel]
        m = c.motors[0]
        diag = 1 if (c.en and not c.no_vm) else 0
        return "en:%d,duty:%d,out:%.1f,i_mA:%d,flt:%d,diag:%d,pos:%d,tgt:%d,vel:%d,mode:%d" % (
            c.en, c.duty, m.out, m.i_rep, c.flt, diag, m.pos, m.tgt if c.mode == 1 else 0, m.speed, c.mode)

    def tail(self):
        """qN,tN,iN of the 3 joints"""
        t = []
        for key, ch in zip(self.keys, self.ch):
            n = APP_ORDER.index(key) + 1
            mm = ch.motors[0]
            t.append("q%d:%d,t%d:%d,i%d:%d" % (n, mm.pos, n, mm.tgt if ch.mode == 1 else mm.pos, n, mm.i_rep))
        return ",".join(t)

    def board_fields(self):
        b = self.board()
        return ("," + b.fields()) if b is not None else ""

    def log_line(self):
        self.write(self.head() + "," + self.tail() + self.board_fields())


# MCB G474 fw 0.8: the MCU with USB bridges the other one over UART4

SINGLE = ("o", "+", "-", "r", "0", "p", "t", "v", "k", "x", "H", "z", "j", "I", "a")


class G474Bridge(G474):
    def __init__(self, mcu, write, sensor=None):
        self.peer_name = "U7" if mcu == "U6" else "U6"
        self.link_up = True
        self.sel_remote = False
        # the other MCU: everything it prints reaches USB as "[U7] ..."
        self.peer = G474(self.peer_name, self._peer_write, banner=False, sensor=sensor)
        self.peer.log_hz = 0
        super().__init__(mcu, write, sensor=self.peer.sensor)
        self.owns_sensor = False   # U7 measures, this MCU gets the values over the link

    def board(self):
        return self.sensor if self.link_up else None

    def _peer_write(self, s):
        if self.link_up:
            self.write("[%s] %s" % (self.peer_name, s))

    def all_keys(self):
        return self.keys + (self.peer.keys if self.link_up else [])

    def id_line(self):
        if self.sel_remote and self.link_up:
            p = self.peer
            cnt90, motor = p.ch[p.sel].cnt90, "%d(%s)" % (p.sel + 1, p.keys[p.sel])
        else:
            cnt90, motor = self.ch[self.sel].cnt90, "%d(%s)" % (self.sel + 1, self.keys[self.sel])
        return "ID: CyberHand-G474 fw %s joints=%d cnt90=%d mcu=%s motor=%s keys=%s link=%s" % (
            self.VERSION, len(self.all_keys()), cnt90, self.mcu, motor, ",".join(self.all_keys()),
            self.peer_name if self.link_up else "-")

    def set_link(self, up):
        if up == self.link_up:
            return self.ok("link %s %s" % (self.peer_name, "up" if up else "lost"))
        self.link_up = up
        if not up:
            # slave watchdog: motors off, flt LINK (8)
            for ch in self.peer.ch:
                ch.en = 0
                ch.mode = 0
                ch.duty = 0
                ch.flt |= 8
            self.sel_remote = False
        self.write(self.id_line())
        self.write("LINK: %s %s" % ("up" if up else "lost", self.peer_name))

    def owner(self, key):
        """'L' local, 'R' remote (link up), None unknown / unreachable"""
        if key in self.keys:
            return "L"
        if key in self.peer.keys:
            return "R" if self.link_up else None
        return None

    def command(self, line):
        parts = line.split()
        c, args = parts[0], parts[1:]
        if c.startswith("@"):
            if c[1:].upper() != self.peer_name or not args:
                return self.err("@%s <line>" % self.peer_name)
            if not self.link_up:
                return self.err("link %s down" % self.peer_name)
            return self.peer.command(" ".join(args))
        if c == "link":
            if not args:
                return self.ok("link %s %s" % (self.peer_name, "up" if self.link_up else "lost"))
            return self.set_link(args[0] in ("on", "auto", "up"))
        if c == "m" and args:
            key = args[0].upper()
            o = self.owner(key)
            if o == "R":
                self.sel_remote = True
                return self.peer.command("m " + key)
            if o is None and key in self.peer.keys:
                return self.err("m %s: link %s down" % (key, self.peer_name))
            self.sel_remote = False
            return G474.command(self, line)
        if c == "g" and len(args) >= 2:
            k, known = self.joint_of(args[0])
            if not known:
                return self.err("g <joint 1-6|key> <deg>")
            if k is not None:
                return self.goto(k, args[1])
            key = APP_ORDER[int(args[0]) - 1] if args[0].isdigit() else args[0].upper()
            if self.owner(key) == "R":
                return self.peer.command(line)
            return self.err("g %s: link %s down" % (args[0], self.peer_name))
        if c in ("e", "d", "c") and args:
            o = self.owner(args[0].upper())
            if o == "R":
                return self.peer.command(line)
            return G474.command(self, line)
        if c in ("e", "d", "c", "D") and not args:
            G474.command(self, line)
            if self.link_up:
                self.peer.command(line)
            return
        if c == "G":
            vals = args
            if len(vals) != 6:
                return self.err("G q1..q6")
            for n, v in enumerate(vals):
                key = APP_ORDER[n]
                o = self.owner(key)
                if o == "L":
                    self._mute = True
                    self.ch[self.keys.index(key)].command("g %s" % v)
                    self._mute = False
                elif o == "R":
                    self.peer._mute = True
                    self.peer.ch[self.peer.keys.index(key)].command("g %s" % v)
                    self.peer._mute = False
            return self.ok("G")
        if c == "h":
            G474.command(self, line)
            self.write("  @%s <line>     raw line to %s;  link on|off|auto" % (self.peer_name, self.peer_name))
            return
        if (c in SINGLE or (c == "g" and len(args) == 1)) and self.sel_remote:
            if not self.link_up:
                return self.err("link %s down" % self.peer_name)
            return self.peer.command(line)
        return G474.command(self, line)

    def step(self, dt):
        self.peer.step(dt)
        G474.step(self, dt)

    def log_line(self):
        head = self.peer.head() if (self.sel_remote and self.link_up) else self.head()
        tail = self.tail()
        if self.link_up:
            tail += "," + self.peer.tail()
        self.write("%s,%s%s,lk:%d" % (head, tail, self.board_fields(), 1 if self.link_up else 0))


# ---------------------------------------------------------------------------
# MCB fw 0.11: CAN FD protocol v1 (docs/PROTOCOL.md "CAN v1") on a UDP virtual bus.
# One datagram per frame: "CF" u8 flags (b0 FD, b1 BRS, b2 EXT) u8 len u32 id (LE) data[len].

CAN_ESTOP, CAN_HB, CAN_POS_ALL, CAN_CMD, CAN_TEXT, CAN_STATUS, CAN_PING, CAN_PONG = (
    0x000, 0x080, 0x100, 0x200, 0x280, 0x300, 0x700, 0x780)
FD_LENS = (0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64)


def fd_pad(b):
    n = next(l for l in FD_LENS if l >= len(b))
    return bytes(b) + b"\0" * (n - len(b))


class CanNode:
    """one G474 on the bus: CLI over CMD_TEXT / TEXT, STATUS at `canlog` Hz, POS_ALL, E-STOP, HB watchdog"""

    def __init__(self, n, sensor, send):
        self.n = n
        self.send = send
        self.g = G474("U6" if n == 1 else "U7", self.text, banner=False, sensor=sensor)
        self.g.VERSION = "0.11-g474"
        self.g.log_hz = 0              # STATUS frames replace the text log
        self.g.owns_sensor = n == 2    # SHT40 + VBAT are on U7
        self.canlog = 50.0
        self.seq = 0
        self.drop = 0
        self.last_status = 0.0
        self.t0 = time.time()
        self.last_hb = None            # watchdog armed by the first HOST_HB

    def text(self, line):
        b = (line + "\n").encode()
        while b:
            self.send(CAN_TEXT + self.n, b[:64])
            b = b[64:]

    def command(self, line):
        parts = line.split()
        if not parts:
            return
        c, args = parts[0], parts[1:]
        if c == "canlog":
            if args:
                try:
                    self.canlog = max(0.0, min(100.0, float(args[0])))
                except ValueError:
                    return self.g.err("canlog <0..100>")
            return self.g.ok("canlog %d Hz" % self.canlog)
        if c == "l":                   # text log goes to USB only, not on CAN
            return self.g.ok("log %s Hz (USB only, CAN uses canlog)" % (args[0] if args else "0"))
        if c == "e" and any(ch.flt & 4 for ch in self.g.ch):   # E-STOP latched until `c`
            return self.g.err("E-STOP latched (flt 4), send c first")
        if c == "simdrop":             # sim only: the next STATUS skips <n> sequence numbers
            self.drop = int(args[0]) if args and args[0].isdigit() else 1
            return self.g.ok("simdrop %d" % self.drop)
        self.g.command(line)

    def motors_off(self, bit):
        for ch in self.g.ch:
            ch.en = 0
            ch.mode = 0
            ch.duty = 0
            ch.home = None
            ch.flt |= bit

    def estop(self):
        self.motors_off(4)
        self.text("ESTOP: all motors off (flt 4)")

    def heartbeat(self):
        self.last_hb = time.time()

    def pos_all(self, data):
        for j in range(min(6, len(data) // 2)):
            v = struct.unpack_from("<h", data, 2 * j)[0]
            key = APP_ORDER[j]
            if v == 0x7FFF or key not in self.g.keys:
                continue
            ch = self.g.ch[self.g.keys.index(key)]
            self.g._mute = True
            ch.command("g %.2f" % (v / 100.0))
            self.g._mute = False

    def status(self):
        d = bytearray(64)
        self.seq = (self.seq + 1 + self.drop) & 0xFFFF
        self.drop = 0
        struct.pack_into("<BBH", d, 0, 1, self.n, self.seq)
        for b, (key, ch) in enumerate(zip(self.g.keys, self.g.ch)):
            m = ch.motors[0]
            bits = ((1 if ch.en else 0) | (2 if ch.homed else 0) | ((ch.mode & 3) << 2)
                    | (0 if (ch.en and not ch.no_vm) else 0x10))
            struct.pack_into("<BBBbiih", d, 4 + 14 * b, APP_ORDER.index(key) + 1, bits, ch.flt & 0xFF,
                             int(max(-128, min(127, round(m.out)))), int(m.pos),
                             int(m.tgt if ch.mode == 1 else m.pos), int(max(-32768, min(32767, m.i_rep))))
        s = self.g.board()
        temp = int(round(s.temp * 10)) if (s and s.sht) else 0x7FFF
        rh = int(round(s.rh * 10)) if (s and s.sht) else 0xFFFF
        vbat = int(max(0, min(65535, round(s.vbat * 1000)))) if s else 0
        struct.pack_into("<hHH", d, 46, temp, rh, vbat)
        p = self.g.protect
        d[52] = (1 if p.hot else 0) | (2 if p.low else 0) | 4   # b2: UART link to the other MCU alive
        struct.pack_into("<I", d, 56, int((time.time() - self.t0) * 1000) & 0xFFFFFFFF)
        self.send(CAN_STATUS + self.n, bytes(d))

    def step(self, dt):
        now = time.time()
        if self.last_hb is not None and now - self.last_hb > 0.3:
            self.last_hb = None
            if any(ch.en or ch.mode for ch in self.g.ch):
                self.motors_off(8)
                self.text("LINK: host heartbeat lost > 300 ms, all motors off (flt 8)")
        self.g.step(dt)
        if self.canlog > 0 and now - self.last_status >= 1.0 / self.canlog:
            self.last_status = now
            self.status()


def run_can_udp(a):
    import socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((a.can_host, a.can_udp))
    sock.setblocking(False)
    peer = [None]                      # the app: address of the last datagram

    def send(fid, data, fd=True, brs=True):
        if peer[0] is None:
            return
        data = fd_pad(data) if fd else bytes(data[:8])
        pkt = b"CF" + struct.pack("<BBI", (1 if fd else 0) | (2 if brs else 0), len(data), fid) + data
        try:
            sock.sendto(pkt, peer[0])
        except OSError:
            pass

    sensor = BoardSensor(a.temp, a.vbat)
    nodes = [CanNode(1, sensor, send), CanNode(2, sensor, send)]
    for nd in nodes:
        nd.g.no_vm = a.no_vm
    print("CAN UDP %s:%d  node 1 = U6 (I T1 T2), node 2 = U7 (M R P), fw 0.11-g474" % (a.can_host, a.can_udp),
          flush=True)

    last = time.time()
    while True:
        while True:
            try:
                pkt, addr = sock.recvfrom(2048)
            except (BlockingIOError, OSError):
                break
            if len(pkt) < 8 or pkt[:2] != b"CF":
                continue
            peer[0] = addr
            flags, n, fid = struct.unpack_from("<BBI", pkt, 2)
            data = pkt[8:8 + n]
            fid &= 0x1FFFFFFF if flags & 4 else 0x7FF
            if fid == CAN_ESTOP:
                for nd in nodes:
                    nd.estop()
            elif fid == CAN_HB:
                for nd in nodes:
                    nd.heartbeat()
            elif fid == CAN_POS_ALL:
                for nd in nodes:
                    nd.pos_all(data)
            elif CAN_CMD <= fid <= CAN_CMD + 2:
                line = data.replace(b"\0", b"").decode(errors="replace").strip()
                for nd in nodes:
                    if fid == CAN_CMD or fid == CAN_CMD + nd.n:
                        nd.command(line)
            elif fid in (CAN_PING + 1, CAN_PING + 2):
                send(CAN_PONG + fid - CAN_PING, data, bool(flags & 1), bool(flags & 2))
        now = time.time()
        for nd in nodes:
            nd.step(now - last)
        last = now
        time.sleep(0.001)


HELP_G474 = """
CyberHand G474 sim (fw 0.7-g474) — this MCU drives 3 joints, see keys= in the ID line
  m <key|1..3>   select the motor for o + - r 0 p t v k x H z j I a s
  g <j> <deg>    joint 1-6 (T2 T1 I M R P) or key; joints of the other MCU are ignored
  g <deg>        selected motor
  e / d / c [key]   enable / disable / clear faults: all 3 motors, or one
  D              all motors off
  i  s  l <hz>  h   id line, status line, log rate, this help
  sense          board sensors (SHT40 + VBAT on U7) and protection thresholds
  simtemp <C>  simvbat <V>  simsht off|on   sim only: set the sensor values
"""


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
  a <hz>         current averaging: i_mA = mean over 1/hz s (1..1000)
  j [cnt]        counts at 90 deg (joint calibration), no arg = print
  I [mA ms r d]  over-current: trip level, hold time, reaction 0 trip / 1 retract d deg / 2 hold / 3 warn
  i  h           id line, this help
"""


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="real/virtual serial port (needs pyserial); default: create a pty")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--joints", type=int, default=1, choices=[1, 6])
    ap.add_argument("--link", help="also create a symlink to the pty at this path")
    ap.add_argument("--no-vm", action="store_true", help="simulate missing motor supply: no motion, no current, DIAG low")
    ap.add_argument("--g474", choices=["U6", "U7"], help="emulate one MCU of the MCB G474 board (3 motors)")
    ap.add_argument("--bridge", action="store_true", help="with --g474: fw 0.8, also drive the other MCU over the UART link")
    ap.add_argument("--temp", type=float, default=25.0, help="with --g474: board temperature at start, C")
    ap.add_argument("--vbat", type=float, default=7.99, help="with --g474: battery voltage at start, V (< 3 = no battery)")
    ap.add_argument("--can-udp", type=int, metavar="PORT",
                    help="MCB fw 0.11 on CAN FD v1: both MCUs (node 1 U6, node 2 U7) on a UDP virtual bus")
    ap.add_argument("--can-host", default="127.0.0.1", help="with --can-udp: address to listen on")
    ap.add_argument("--tcp", type=int, metavar="PORT",
                    help="serve this firmware over TCP like the MainBoard WiFi bridge (app: Kết nối -> WiFi, 127.0.0.1:PORT)")
    ap.add_argument("--tcp-host", default="127.0.0.1", help="with --tcp: address to listen on (0.0.0.0 = every interface)")
    a = ap.parse_args()

    if a.can_udp:
        return run_can_udp(a)

    if a.port:
        import serial  # pyserial
        ser = serial.Serial(a.port, a.baud, timeout=0)
        read = lambda: ser.read(4096)
        write_raw = lambda b: ser.write(b)
        print("fw_sim on %s" % a.port, flush=True)
    elif a.tcp:
        import socket
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((a.tcp_host, a.tcp))
        srv.listen(1)
        srv.setblocking(False)
        client = [None]   # one client, a new connection replaces the old one (like the ESP32 bridge)
        print("TCP %s:%d" % (a.tcp_host, a.tcp), flush=True)

        def read():
            try:
                c, _ = srv.accept()
                c.setblocking(False)
                c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                if client[0] is not None:
                    client[0].close()
                client[0] = c
            except (BlockingIOError, OSError):
                pass
            if client[0] is None:
                return b""
            try:
                d = client[0].recv(4096)
                if d == b"":
                    client[0].close()
                    client[0] = None
                return d
            except BlockingIOError:
                return b""
            except OSError:
                client[0] = None
                return b""

        def write_raw(b):
            if client[0] is None:
                return
            try:
                client[0].sendall(b)
            except (BlockingIOError, OSError):
                pass
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

    writer = lambda s: write_raw((s + "\r\n").encode())
    if a.g474:
        sensor = BoardSensor(a.temp, a.vbat)
        fw = G474Bridge(a.g474, writer, sensor=sensor) if a.bridge else G474(a.g474, writer, sensor=sensor)
    else:
        fw = Firmware(a.joints, writer)
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
