#!/usr/bin/env python3
"""
STEP assembly of the OpenGrasp-Lite / CyberHand hand -> CyberHand Studio 3D model.

    pip install cadquery-ocp numpy
    python step_to_rig.py HAND_Assembly_with_screws.STEP out_dir [--deflection 0.2]

Writes out_dir/rig.json (links, colours, joints) and out_dir/hand.bin (triangle meshes).
In the app: Visual -> Mô hình 3D -> Mở thư mục model... and pick out_dir.

The part -> link rules below follow the part names of the mechanics repo
(finger_C: palm_connector, lower_phalanx_B, Finger_tip_sensor_nail_silicon, pins;
thumb_C_linkage: thumb_base_linkage, first/second phalange). Joint points come from
the pins' cylinder axes, so moving a pin in CAD moves the joint.

Finger (paper 2408.02293, fig. 2b): four-bar A (MCP pin, motor) - B (PIP pin) -
C (pin in the fingertip) - D (pin in palm_connector). AB = proximal phalanx (input),
BC = fingertip (coupler, DIP fixed), CD = spring link (follower), AD = ground.
Thumb: T1 (rotation) turns thumb_base about its pin; T2 (flexion) is the same four-bar on the thumb, with
D = hook of the spring extension on thumb_base.
"""
import argparse
import json
import struct
import sys

import numpy as np
from OCP.BRep import BRep_Tool
from OCP.BRepAdaptor import BRepAdaptor_Surface
from OCP.BRepMesh import BRepMesh_IncrementalMesh
from OCP.collections import Sequence_TDF_Label as LabelSeq
from OCP.GeomAbs import GeomAbs_Cylinder
from OCP.STEPCAFControl import STEPCAFControl_Reader
from OCP.TCollection import TCollection_ExtendedString
from OCP.TDataStd import TDataStd_Name
from OCP.TDF import TDF_Label
from OCP.TDocStd import TDocStd_Document
from OCP.TopAbs import TopAbs_FACE, TopAbs_REVERSED
from OCP.TopExp import TopExp_Explorer
from OCP.TopLoc import TopLoc_Location
from OCP.TopoDS import TopoDS
from OCP.XCAFDoc import XCAFDoc_DocumentTool, XCAFDoc_ShapeTool

# ---------------------------------------------------------------------------
# which parts to drop, colour classes

SKIP = ("screw", "Insert M", "Mainboard", "Motorboard", "CAPAE", "CAPC", "QFN", "SON95", "TPSM",
        "TB9051", "Packages", "SOTFL", "4363330", "rigid_link")
COLORS = {
    "printed": "#D9D9D9",
    "palm": "#C9C9C9",
    "tip": "#E2D8D0",
    "metal": "#8E9297",
    "pcb": "#4E6E5A",
    "motor": "#A7A9AD",
}


def color_class(name, link):
    n = name.lower()
    if n.startswith(("pin", "spur_gear", "worm_gear", "torsion_spring", "linear_spring", "spring_extension", "neodym")):
        return "metal"
    if n.startswith("hpcb_dc_motor") or n.startswith("encoder_ring"):
        return "motor"
    if n.startswith("encoder_pcb") or n.startswith("hall"):
        return "pcb"
    if "finger_tip" in n or "second_phalange" in n:
        return "tip"
    return "palm" if link == "palm" else "printed"


# ---------------------------------------------------------------------------
# STEP reading

def label_name(lab):
    n = TDataStd_Name()
    return n.Get().ToExtString() if lab.FindAttribute(TDataStd_Name.GetID_s(), n) else "?"


def read_step(path):
    doc = TDocStd_Document(TCollection_ExtendedString("doc"))
    r = STEPCAFControl_Reader()
    r.SetNameMode(True)
    if r.ReadFile(path) != 1:
        sys.exit("cannot read " + path)
    r.Transfer(doc)
    tool = XCAFDoc_DocumentTool.ShapeTool_s(doc.Main())
    out = []

    def walk(lab, loc, path):
        real = lab
        if XCAFDoc_ShapeTool.IsReference_s(lab):
            ref = TDF_Label()
            XCAFDoc_ShapeTool.GetReferredShape_s(lab, ref)
            real = ref
            loc = loc.Multiplied(XCAFDoc_ShapeTool.GetLocation_s(lab))
        nm, inst = label_name(real), label_name(lab)
        if XCAFDoc_ShapeTool.IsAssembly_s(real):
            comps = LabelSeq()
            XCAFDoc_ShapeTool.GetComponents_s(real, comps)
            for i in range(1, comps.Length() + 1):
                walk(comps.Value(i), loc, path + [f"{inst}:{nm}"])
            return
        out.append(dict(name=nm, path=path, loc=loc, shape=XCAFDoc_ShapeTool.GetShape_s(real)))

    roots = LabelSeq()
    tool.GetFreeShapes(roots)
    for i in range(1, roots.Length() + 1):
        walk(roots.Value(i), TopLoc_Location(), [])
    return out


def tessellate(shape, deflection):
    """triangles with per-face smooth normals (faces keep their own vertices -> sharp edges)"""
    BRepMesh_IncrementalMesh(shape, deflection, False, 0.4, True)
    P, N, I, cyl = [], [], [], []
    off = 0
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        f = TopoDS.Face(exp.Current())
        loc = TopLoc_Location()
        tr = BRep_Tool.Triangulation_s(f, loc)
        if tr is not None and tr.NbTriangles() > 0:
            T = loc.Transformation()
            n = tr.NbNodes()
            pts = np.empty((n, 3))
            for i in range(1, n + 1):
                p = tr.Node(i).Transformed(T)
                pts[i - 1] = (p.X(), p.Y(), p.Z())
            idx = np.empty((tr.NbTriangles(), 3), dtype=np.int64)
            for i in range(1, tr.NbTriangles() + 1):
                a, b, c = tr.Triangle(i).Get()
                idx[i - 1] = (a - 1, b - 1, c - 1)
            if f.Orientation() == TopAbs_REVERSED:
                idx = idx[:, [0, 2, 1]]
            fn = np.cross(pts[idx[:, 1]] - pts[idx[:, 0]], pts[idx[:, 2]] - pts[idx[:, 0]])
            vn = np.zeros_like(pts)
            for k in range(3):
                np.add.at(vn, idx[:, k], fn)
            ln = np.linalg.norm(vn, axis=1)
            ln[ln == 0] = 1
            P.append(pts)
            N.append(vn / ln[:, None])
            I.append(idx + off)
            off += n
        s = BRepAdaptor_Surface(f)
        if s.GetType() == GeomAbs_Cylinder:
            ax = s.Cylinder().Axis()
            lp, d = ax.Location(), ax.Direction()
            cyl.append((lp.X(), lp.Y(), lp.Z(), d.X(), d.Y(), d.Z(), s.Cylinder().Radius()))
        exp.Next()
    if not P:
        return None
    return np.vstack(P), np.vstack(N), np.vstack(I), np.array(cyl).reshape(-1, 7)


def matrix(loc):
    T = loc.Transformation()
    return np.array([[T.Value(r, c) for c in range(1, 5)] for r in range(1, 4)])


# ---------------------------------------------------------------------------
# rig

FINGERS = ("I", "M", "R", "P")


def build(parts, deflection):
    meshes = {}  # name -> tessellation (shared by instances)
    items = []
    for p in parts:
        full = "/".join(p["path"][1:] + [p["name"]])
        if any(s in full for s in SKIP):
            continue
        if p["name"] not in meshes:
            meshes[p["name"]] = tessellate(p["shape"], deflection)
        m = meshes[p["name"]]
        if m is None:
            continue
        P, N, I, C = m
        M = matrix(p["loc"])
        R, t = M[:, :3], M[:, 3]
        Cw = C.copy()
        if len(C):
            Cw[:, :3] = C[:, :3] @ R.T + t
            Cw[:, 3:6] = C[:, 3:6] @ R.T
        top = p["path"][1] if len(p["path"]) > 1 else ""
        items.append(dict(name=p["name"], top=top, path=p["path"], P=P @ R.T + t, N=N @ R.T, I=I, cyl=Cw))

    # fingers: order along X (index next to the thumb)
    finger_tops = sorted({it["top"] for it in items if "finger_C" in it["top"]},
                         key=lambda tp: np.mean([it["P"][:, 0].mean() for it in items if it["top"] == tp]))
    thumb_x = np.mean([it["P"][:, 0].mean() for it in items if "thumb_C" in it["top"]])
    first_x = np.mean([it["P"][:, 0].mean() for it in items if it["top"] == finger_tops[0]])
    if abs(first_x - thumb_x) > abs(np.mean([it["P"][:, 0].mean() for it in items if it["top"] == finger_tops[-1]]) - thumb_x):
        finger_tops.reverse()
    fmap = {tp: FINGERS[i] for i, tp in enumerate(finger_tops[:4])}

    def pin_axis(it):
        C = it["cyl"]
        i = int(np.argmax(C[:, 6]))
        d = C[i, 3:6] / np.linalg.norm(C[i, 3:6])
        a = C[i, :3]
        c = it["P"].mean(0)
        return a + np.dot(c - a, d) * d, d

    joints = {f: {} for f in FINGERS + ("T1", "T2")}
    link_of = []
    for it in items:
        n, top = it["name"], it["top"]
        link = "palm"
        if top in fmap:
            f = fmap[top]
            in_motor = "finger_motor_with_encoder" in "/".join(it["path"])
            if n in ("lower_phalanx_B", "spur_gear_m0.5_20_5mm_Standard", "neodym_magnet_4x1mm", "pin_3mmx19", "torsion_spring_20deg_Standard"):
                link = f + "_pp"
            elif n in ("Finger_tip_sensor_nail_silicon", "pin_1_5mmx17"):
                link = f + "_tip"
            elif n in ("spring_extension_Standard", "linear_spring_finger_Standard"):
                link = f + "_cd"
            elif in_motor:
                link = None  # motors sit inside the closed palm
            if n == "pin_3mmx18":
                joints[f]["A"] = pin_axis(it)
            elif n == "pin_3mmx19":
                joints[f]["B"] = pin_axis(it)
            elif n == "pin_1_5mmx17":
                joints[f]["C"] = pin_axis(it)
            elif n == "pin_1_5mmx19":
                joints[f]["D"] = pin_axis(it)
        elif "thumb_C" in top:
            c = it["P"].mean(0)
            in_motor = "finger_motor_with_encoder" in "/".join(it["path"])
            if n.startswith("pin_3mm"):
                ctr, d = pin_axis(it)
                if "A" not in joints["T2"] and abs(d[2]) > 0.9:
                    joints["T2"]["A"] = (ctr, d)
                    link = "palm"
                elif abs(d[2]) > 0.9:
                    link = "palm"
                else:
                    # T1 pin (on the base) or IP pin (on the proximal phalange): decided below
                    link = "T_base"
                    it["_pin"] = (ctr, d)
            elif n == "pin_1_5mmx17":
                joints["T1"]["C"] = pin_axis(it)
                link = "T_tip"
            elif n == "thumb_connector_Standard":
                link = "palm"
            elif n in ("first_phalange_B_linkage_Standard", "first_phalange_cover_B_linkage_Standard", "neodym_magnet_4x1mm", "torsion_spring_20deg_Standard") or in_motor:
                link = "T_pp"
            elif n == "second_phalange_C_linkage_Standard":
                link = "T_tip"
            elif n in ("spring_extension_thumb_Standard", "linear_spring_thumb_n_Standard"):
                link = "T_cd"
            else:
                link = "T_base"  # thumb_base, gears, hall sensors
            if n == "hall_sensor_A3144" and "A" in joints["T2"] and np.linalg.norm(c - joints["T2"]["A"][0]) < 12:
                link = "palm"
        elif "finger_motor_with_encoder" in "/".join(it["path"]) or "finger_motor_with_encoder" in top:
            link = None  # thumb abduction motor inside the palm
        link_of.append(link)

    # thumb pins with a flexion-direction axis: the one nearer the T2 axis is T1 (A), the other the IP joint (B)
    tp = [(k, it["_pin"]) for k, it in enumerate(items) if "_pin" in it]
    a2 = joints["T2"]["A"][0]
    tp.sort(key=lambda kv: np.linalg.norm(kv[1][0] - a2))
    joints["T1"]["A"] = tp[0][1]
    link_of[tp[0][0]] = "T_base"
    joints["T1"]["B"] = tp[1][1]
    link_of[tp[1][0]] = "T_pp"
    # thumb ground pivot D: hook of the spring extension on thumb_base (small bore on the flexion axis direction)
    d1 = joints["T1"]["A"][1]
    base = [it for it in items if it["name"] == "thumb_base_linkage_Standard"][0]
    ext = [it for it in items if it["name"] == "spring_extension_thumb_Standard"][0]
    lowend = ext["P"][np.argmin(np.linalg.norm(ext["P"] - joints["T1"]["A"][0], axis=1))]
    C = base["cyl"]
    dd = C[:, 3:6] / np.linalg.norm(C[:, 3:6], axis=1)[:, None]
    cand = [(np.linalg.norm(C[i, :3] - lowend), i) for i in range(len(C)) if abs(dd[i] @ d1) > 0.99 and C[i, 6] < 1.0]
    joints["T1"]["D"] = (C[min(cand)[1], :3], d1)

    return items, link_of, joints


def flex_sign(J, tip_dir):
    """+1 if a positive rotation about the pin axis moves B toward tip_dir"""
    A, d = J["A"]
    B = J["B"][0]
    v = np.cross(d, B - A)
    return 1.0 if v @ tip_dir > 0 else -1.0


def write(out_dir, items, link_of, joints, name):
    import os
    os.makedirs(out_dir, exist_ok=True)
    # merge by (link, colour class)
    groups = {}
    for it, link in zip(items, link_of):
        if link is None:
            continue
        key = (link, color_class(it["name"], link))
        groups.setdefault(key, []).append(it)
    link_names = ["palm"] + [f + s for f in FINGERS for s in ("_pp", "_tip", "_cd")] + ["T_base", "T_pp", "T_tip", "T_cd"]
    meshes = []
    blob = bytearray(b"CHMB" + struct.pack("<II", 1, len(groups)))
    allp = np.vstack([it["P"] for it in items])
    for (link, cls), its in sorted(groups.items(), key=lambda kv: (link_names.index(kv[0][0]), kv[0][1])):
        P = np.vstack([it["P"] for it in its]).astype(np.float32)
        N = np.vstack([it["N"] for it in its]).astype(np.float32)
        I, off = [], 0
        for it in its:
            I.append(it["I"] + off)
            off += len(it["P"])
        I = np.vstack(I).astype(np.uint32)
        blob += struct.pack("<II", len(P), I.size)
        blob += np.hstack([P, N]).astype("<f4").tobytes()
        blob += I.astype("<u4").tobytes()
        meshes.append(dict(link=link, color=COLORS[cls], cls=cls, triangles=int(len(I))))
    open(os.path.join(out_dir, "hand.bin"), "wb").write(bytes(blob))

    palm_dir = np.array([0.0, 1.0, 0.0])  # palm side (+Y in the mechanics repo)
    lo, hi = allp.min(0), allp.max(0)
    ctr = (lo + hi) / 2

    def jp(v):
        return [round(float(x), 4) for x in v]

    js = []
    for f in FINGERS:
        J = joints[f]
        s = flex_sign(J, palm_dir)
        js.append(dict(key=f, type="fourbar", axis=jp(J["A"][1] * s), A=jp(J["A"][0]), B=jp(J["B"][0]),
                       C=jp(J["C"][0]), D=jp(J["D"][0]), input=f + "_pp", coupler=f + "_tip", follower=f + "_cd"))
    # thumb: T2 positive = opposition (tip toward the fingers, +X), T1 positive = flexion toward the fingers (+Z)
    A2, d2 = joints["T2"]["A"]
    tip = np.vstack([it["P"] for it, l in zip(items, link_of) if l == "T_tip"]).mean(0)
    s2 = 1.0 if np.cross(d2, tip - A2)[0] > 0 else -1.0
    # app keys: T1 = thumb rotation (revolute base), T2 = thumb flexion (four-bar)
    js.append(dict(key="T1", type="revolute", axis=jp(d2 * s2), point=jp(A2), link="T_base", children=["T_pp", "T_tip", "T_cd"]))
    J = joints["T1"]
    s1 = 1.0 if np.cross(J["A"][1], J["B"][0] - J["A"][0])[2] > 0 else -1.0
    js.append(dict(key="T2", type="fourbar", parent="T1", axis=jp(J["A"][1] * s1), A=jp(J["A"][0]), B=jp(J["B"][0]),
                   C=jp(J["C"][0]), D=jp(J["D"][0]), input="T_pp", coupler="T_tip", follower="T_cd"))
    rig = dict(format="cyberhand-rig-1", name=name, units="mm", mesh="hand.bin",
               bounds=[jp(lo), jp(hi)],
               # app frame: fingers +Y, palm faces -Z, thumb on -X (model X, Z, -Y);
               # centred in X/Z, wrist standing on the app floor (y = -57)
               view=dict(eulerDeg=[-90, 0, 0], position=jp([-ctr[0], -lo[2] - 57.0, ctr[1]])),
               links=link_names, meshes=meshes, joints=js)
    json.dump(rig, open(os.path.join(out_dir, "rig.json"), "w"), indent=1)
    tris = sum(m["triangles"] for m in meshes)
    print(f"{len(meshes)} meshes, {tris} triangles, {len(blob) / 1e6:.1f} MB")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("step")
    ap.add_argument("out_dir")
    ap.add_argument("--deflection", type=float, default=0.2, help="mesh chord tolerance, mm")
    ap.add_argument("--name", default="OpenGrasp-Lite (HAND_Assembly_with_screws)")
    a = ap.parse_args()
    parts = read_step(a.step)
    items, link_of, joints = build(parts, a.deflection)
    for k, v in joints.items():
        print(k, {p: [round(float(x), 2) for x in v[p][0]] for p in v})
    write(a.out_dir, items, link_of, joints, a.name)


if __name__ == "__main__":
    main()
