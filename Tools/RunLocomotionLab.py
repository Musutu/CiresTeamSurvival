"""movement-feel: run the locomotion lab (Source/CiresTeamSurvival/CireLocomotionLab.cpp) and build contact sheets.

Every subject (real drafted champion / configured monster) runs a scripted course at a fixed 60 Hz step: idle, start,
stop, run, 90 degree turn, reversal, strafe, backpedal, turn in place (smooth and snapped), a 15 degree hill and a
cornered path. The fixture logs per-segment foot-slide, yaw, pose-jerk and frozen-pose metrics and captures slow-motion
frame windows. This tool writes Saved/LocomotionLab/<stamp>-<tag>/{metrics.json, summary.md, sheets/<subject>.png}.

  python Tools/RunLocomotionLab.py                      # after (current presentation)
  python Tools/RunLocomotionLab.py --before             # -CireLocoFeel=0: previous presentation
  python Tools/RunLocomotionLab.py --subjects hero:lancer,monster:dire_wolf --no-capture
  python Tools/RunLocomotionLab.py --compare <before-dir> <after-dir>
  python Tools/RunLocomotionLab.py --net --local <standalone-dir>   # dedicated server + remote client (simulated proxies)

Contact sheets need Pillow (installed with: python -m pip install --target Saved/pylib pillow).
Only the editor process started here is stopped.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Saved" / "pylib"))
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}
KEY = ["footSlide", "footSlideRatio", "turnDrift", "yawRateMax", "yawSnaps", "visualLagDeg", "jerkMax", "pops", "frozenFrames", "groundErrMean"]


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def sheets(directory: Path) -> list[Path]:
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        print("CIRE_LOCO_LAB_SHEETS skipped (Pillow missing)")
        return []
    out = []
    frames = directory / "frames"
    (directory / "sheets").mkdir(exist_ok=True)
    for subject in sorted(p for p in frames.iterdir() if p.is_dir()) if frames.exists() else []:
        rows: dict[str, list[Path]] = {}
        for f in sorted(subject.glob("*.png")):
            m = re.match(r"(\d+_[a-z0-9_]+?)_(\d{3})\.png$", f.name)
            if m:
                rows.setdefault(m.group(1), []).append(f)
        if not rows:
            continue
        cell_w, cell_h = 320, 180
        cols = max(len(v) for v in rows.values())
        cols = min(cols, 16)
        sheet = Image.new("RGB", (160 + cols * cell_w, len(rows) * cell_h), (18, 18, 22))
        draw = ImageDraw.Draw(sheet)
        for r, (name, files) in enumerate(sorted(rows.items())):
            draw.text((8, r * cell_h + 8), name, fill=(240, 220, 170))
            # Wide windows (hill, corners) are subsampled to fit the row.
            step = max(1, (len(files) + cols - 1) // cols)
            for c, f in enumerate(files[::step][:cols]):
                im = Image.open(f).convert("RGB").resize((cell_w, cell_h))
                sheet.paste(im, (160 + c * cell_w, r * cell_h))
        path = directory / "sheets" / (subject.name + ".png")
        sheet.save(path)
        out.append(path)
    return out


def stance_drift(rows: list, contacts: list, height: float) -> tuple:
    """World drift of every contact across each stance (h within 2% of the body height of its segment minimum, at
    least 4 frames): distance between the first and last stance frame over the stance time. Returns (cm/s, stances)."""
    tol = max(2.0, .02 * height)
    num = den = 0.0
    count = 0
    for c in contacts:
        hs = [float(r[f"c{c}h"]) for r in rows]
        hmin = min(hs)
        run: list = []
        for i, h in enumerate(hs + [1e9]):
            if h < hmin + tol:
                run.append(i)
                continue
            if len(run) >= 4:
                a, b = rows[run[0]], rows[run[-1]]
                num += ((float(b[f"c{c}x"]) - float(a[f"c{c}x"])) ** 2 + (float(b[f"c{c}y"]) - float(a[f"c{c}y"])) ** 2) ** .5
                den += float(b["t"]) - float(a["t"])
                count += 1
            run = []
    return (num / den if den > 0 else None), count


def skate(directory: Path, data: dict) -> None:
    """Foot slide per segment from the samples: stance drift (cm/s) in the steady part (after 0.4 s) and its ratio to
    the body speed; turnDrift = stance drift while standing (turns in place)."""
    import csv
    for subject in data["subjects"]:
        path = directory / "samples" / (subject["subject"].replace(":", "_") + ".csv")
        if not path.exists():
            continue
        rows = list(csv.DictReader(path.open(encoding="utf-8")))
        contacts = sorted({int(k[1:-1]) for k in rows[0] if k.startswith("c") and k.endswith("h")}) if rows else []
        by_seg: dict[str, list] = {}
        for r in rows:
            if r["skip"] == "0":
                by_seg.setdefault(r["seg"], []).append(r)
        for seg in subject["segments"]:
            rs = by_seg.get(seg["segment"], [])
            if len(rs) < 10:
                continue
            t0 = float(rs[0]["t"])
            steady = [r for r in rs if float(r["t"]) - t0 >= .4] if seg["segment"] not in ("start", "stop", "stop2") else rs
            if len(steady) < 10:
                continue
            speed = sum((float(r["vx"]) ** 2 + float(r["vy"]) ** 2) ** .5 for r in steady) / len(steady)
            value, count = stance_drift(steady, contacts, subject["height"])
            if value is None:
                continue
            if speed > 40:
                seg["footSlide"] = round(value, 1)
                seg["footSlideRatio"] = round(value / speed, 3)
                seg["stances"] = count
            else:
                seg["turnDrift"] = round(value, 1)


def summary(directory: Path) -> str:
    data = json.loads((directory / "metrics.json").read_text(encoding="utf-8"))
    skate(directory, data)
    (directory / "metrics.json").write_text(json.dumps(data, indent=1), encoding="utf-8")
    lines = [f"# Locomotion lab {directory.name} (tag {data['tag']}, locoFeel {data['locoFeel']})", ""]
    lines.append("| subject | segment | speed | " + " | ".join(KEY) + " |")
    lines.append("|---|---|---|" + "---|" * len(KEY))
    for s in data["subjects"]:
        for seg in s["segments"]:
            lines.append(f"| {s['subject']} | {seg['segment']} | {seg['speed']} | " + " | ".join(str(seg.get(k, '')) for k in KEY) + " |")
    lines.append("")
    for s in data["subjects"]:
        for c in s.get("clips", []):
            lines.append(f"- {s['subject']}: " + ", ".join(f"{k}={v}" for k, v in c.items()))
    text = "\n".join(lines) + "\n"
    (directory / "summary.md").write_text(text, encoding="utf-8")
    return text


def smoothness(directory: Path) -> dict:
    """Whole-run smoothness per subject (any lab or client sample set): visual yaw snaps (>15 deg in a frame), p99 yaw
    acceleration, p99/max mesh acceleration (cm/s^2, from the rendered mesh position), and foot slide while moving."""
    import csv
    out = {}
    for path in sorted((directory / "samples").glob("*.csv")):
        rows = [r for r in csv.DictReader(path.open(encoding="utf-8"))]
        if len(rows) < 30 or "mx" not in rows[0]:
            continue
        snaps, yaw_acc, mesh_acc = 0, [], []
        for a, b, c in zip(rows, rows[1:], rows[2:]):
            if "1" in (a["skip"], b["skip"], c["skip"]):
                continue
            t0, t1, t2 = float(a["t"]), float(b["t"]), float(c["t"])
            if t1 <= t0 or t2 <= t1:
                continue
            d1 = ((float(c["visYaw"]) - float(b["visYaw"]) + 180) % 360) - 180
            d0 = ((float(b["visYaw"]) - float(a["visYaw"]) + 180) % 360) - 180
            snaps += abs(d1) >= 15
            dt = (t2 - t0) / 2
            yaw_acc.append(abs(d1 / (t2 - t1) - d0 / (t1 - t0)) / dt)
            acc = [((float(c[k]) - float(b[k])) / (t2 - t1) - (float(b[k]) - float(a[k])) / (t1 - t0)) / dt for k in ("mx", "my", "mz")]
            mesh_acc.append((acc[0] ** 2 + acc[1] ** 2 + acc[2] ** 2) ** .5)
        pct = lambda v, q: sorted(v)[min(len(v) - 1, int(q * len(v)))] if v else 0
        contacts = sorted({int(k[1:-1]) for k in rows[0] if k.startswith("c") and k.endswith("h")})
        moving = [r for r in rows if r["skip"] == "0" and (float(r["vx"]) ** 2 + float(r["vy"]) ** 2) ** .5 > 60]
        speed = sum((float(r["vx"]) ** 2 + float(r["vy"]) ** 2) ** .5 for r in moving) / max(1, len(moving))
        slide, _ = stance_drift(moving, contacts, 180.0) if moving else (None, 0)
        out[path.stem] = {"frames": len(rows), "yawSnaps": snaps, "yawAccP99": round(pct(yaw_acc, .99)), "meshAccP99": round(pct(mesh_acc, .99)),
                          "meshAccMax": round(max(mesh_acc, default=0)), "footSlideRatio": round(slide / speed, 3) if slide and speed > 40 else None}
    return out


def compare(before: Path, after: Path) -> str:
    b = {s["subject"]: {g["segment"]: g for g in s["segments"]} for s in json.loads((before / "metrics.json").read_text())["subjects"]}
    a = {s["subject"]: {g["segment"]: g for g in s["segments"]} for s in json.loads((after / "metrics.json").read_text())["subjects"]}
    lines = [f"# Before {before.name} -> after {after.name}", "", "| subject | segment | " + " | ".join(KEY) + " |", "|---|---|" + "---|" * len(KEY)]
    for subject in a:
        for seg, g in a[subject].items():
            o = b.get(subject, {}).get(seg)
            if not o:
                continue
            lines.append(f"| {subject} | {seg} | " + " | ".join(f"{o.get(k, '')} -> {g.get(k, '')}" for k in KEY) + " |")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--subjects", default="")
    parser.add_argument("--before", action="store_true", help="previous presentation (-CireLocoFeel=0)")
    parser.add_argument("--tag", default="")
    parser.add_argument("--no-capture", action="store_true")
    parser.add_argument("--compare", nargs=2, type=Path)
    parser.add_argument("--net", action="store_true", help="dedicated server runs the course, a remote client samples proxies")
    parser.add_argument("--local", type=Path, help="standalone lab directory to compare the client against")
    parser.add_argument("--port", type=int, default=8311)
    parser.add_argument("--lag", type=int, default=0, help="client packet lag in ms (NetEmulation.PktLag)")
    parser.add_argument("--timeout", type=int, default=2400)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    args = parser.parse_args()
    if args.compare:
        text = compare(*args.compare)
        (args.compare[1] / "compare.md").write_text(text, encoding="utf-8")
        print(text)
        return 0
    if args.net:
        return run_net(args)
    tag = args.tag or ("before" if args.before else "after")
    log = ROOT / f"Saved/Logs/LocomotionLab-{tag}.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireLocomotionLab",
               "-CireTripoChampions", "-UseFixedTimeStep", "-FPS=60", "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1280", "-ResY=720",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}", f"-CireLocoLabTag={tag}",
               f"-CireLocoFeel={0 if args.before else 1}"]
    if args.subjects:
        command.append(f"-CireLocoLabSubjects={args.subjects}")
    if args.no_capture:
        command.append("-CireLocoLabNoCapture")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        child.kill()
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    directory = None
    for line in text.splitlines():
        if re.search(r"CIRE_LOCO_LAB_(PASS|FAIL|CHECK_FAIL|SUBJECT|START)|Fatal error|Ensure condition failed", line):
            print(line.strip())
        m = re.search(r"CIRE_LOCO_LAB_(?:PASS|FAIL) .*directory=(.+)$", line)
        if m:
            directory = Path(m.group(1).strip())
    passed = code == 0 and "CIRE_LOCO_LAB_PASS" in text
    if directory and (directory / "metrics.json").exists():
        print(summary(directory))
        for sheet in sheets(directory):
            print("CIRE_LOCO_LAB_SHEET", sheet)
        print("CIRE_LOCO_LAB_DIRECTORY", directory)
    print("LOCOMOTION LAB", "PASS" if passed else f"FAIL (exit {code})")
    return 0 if passed else 1


def run_net(args) -> int:
    logs = ROOT / "Saved/Logs"
    logs.mkdir(parents=True, exist_ok=True)
    server_log, client_log = logs / "LocomotionLab-net-server.log", logs / "LocomotionLab-net-client.log"
    feel = f"-CireLocoFeel={0 if args.before else 1}"
    common = ["-nullrhi", "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-ExecCmds=t.MaxFPS 60", "-CireTripoChampions", feel]
    server = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-server", f"-port={args.port}", "-CireLocomotionLab",
              "-CireLocoLabNet", f"-CireLocoLabTag=net-server", f"-abslog={server_log}", *common]
    if args.subjects:
        server.append(f"-CireLocoLabSubjects={args.subjects}")
    client_cmds = "t.MaxFPS 60" + (f",NetEmulation.PktLag {args.lag}" if args.lag else "")
    client = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), f"127.0.0.1:{args.port}", "-game", "-CireLocoLabClient", f"-abslog={client_log}",
              *[c for c in common if not c.startswith("-ExecCmds")], f"-ExecCmds={client_cmds}"]
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    srv = subprocess.Popen(server, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    import time
    time.sleep(25)
    cli = subprocess.Popen(client, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    try:
        code = cli.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        code = -1
    for child in (cli, srv):
        try:
            child.wait(timeout=30)
        except subprocess.TimeoutExpired:
            kill_tree(child)
            child.kill()
    text = client_log.read_text(encoding="utf-8", errors="replace") if client_log.exists() else ""
    directory = None
    for line in text.splitlines():
        if re.search(r"CIRE_LOCO_LAB_(PASS|FAIL|CLIENT_SUBJECT|CLIENT_START)|Fatal error", line):
            print(line.strip())
        m = re.search(r"CIRE_LOCO_LAB_(?:PASS|FAIL) .*directory=(.+)$", line)
        if m:
            directory = Path(m.group(1).strip())
    if not directory:
        print("LOCOMOTION NET FAIL (no client result)")
        return 1
    remote = smoothness(directory)
    local = smoothness(args.local) if args.local else {}
    lines = ["| subject | " + " | ".join(f"{k} local -> proxy" for k in ("yawSnaps", "yawAccP99", "meshAccP99", "meshAccMax", "footSlideRatio")) + " |", "|---|" + "---|" * 5]
    for name, r in remote.items():
        o = local.get(name, {})
        lines.append(f"| {name} | " + " | ".join(f"{o.get(k, '')} -> {r[k]}" for k in ("yawSnaps", "yawAccP99", "meshAccP99", "meshAccMax", "footSlideRatio")) + " |")
    table = "\n".join(lines) + "\n"
    (directory / "net-compare.md").write_text(table, encoding="utf-8")
    print(table)
    print("CIRE_LOCO_LAB_DIRECTORY", directory)
    passed = "CIRE_LOCO_LAB_PASS" in text and len(remote) > 0
    print("LOCOMOTION NET", "PASS" if passed else f"FAIL (exit {code})")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
