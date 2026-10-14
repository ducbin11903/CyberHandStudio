#!/usr/bin/env python3
"""
CI helper: version.json for the in-app updater (published next to CyberHandStudio-win64.zip on the
"latest" release) and the release notes.

  python tools/make_version_json.py --zip CyberHandStudio-win64.zip --build 61 --commit <sha> \
      --out version.json --notes notes.md
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess


def project_version(root):
    with open(os.path.join(root, "CMakeLists.txt"), encoding="utf-8") as f:
        m = re.search(r"project\(\s*CyberHandStudio\s+VERSION\s+([0-9.]+)", f.read())
    return m.group(1) if m else "0.0.0"


def git_log(root, n):
    out = subprocess.run(["git", "log", "-n", str(n), "--format=%H%x1f%s"], cwd=root,
                         capture_output=True, text=True, encoding="utf-8", check=False).stdout
    log = []
    for line in out.splitlines():
        if "\x1f" in line:
            sha, subject = line.split("\x1f", 1)
            log.append({"commit": sha, "subject": subject})
    return log


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--zip", required=True)
    ap.add_argument("--build", type=int, required=True)
    ap.add_argument("--commit", required=True)
    ap.add_argument("--out", default="version.json")
    ap.add_argument("--notes", help="also write release notes (markdown) here")
    ap.add_argument("--log", type=int, default=40, help="commits listed (app shows those newer than its own)")
    a = ap.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    h = hashlib.sha256()
    with open(a.zip, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    info = {
        "version": project_version(root),
        "build": a.build,
        "commit": a.commit,
        "date": datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "zip": os.path.basename(a.zip),
        "sha256": h.hexdigest(),
        "size": os.path.getsize(a.zip),
        "log": git_log(root, a.log),
    }
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(info, f, indent=1)
    print("%s: %s build %d %s, %d bytes" % (a.out, info["version"], a.build, a.commit[:7], info["size"]))

    if a.notes:
        lines = ["CyberHand Studio %s · build %d · %s" % (info["version"], a.build, a.commit[:7]), "",
                 "Tải zip, giải nén, chạy CyberHandStudio.exe. Bản đã cài: nút Cập nhật (thanh trái, dưới cùng) tự tải và cài.",
                 "", "Thay đổi gần đây:", ""]
        lines += ["- %s %s" % (e["commit"][:7], e["subject"]) for e in info["log"][:15]]
        with open(a.notes, "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
