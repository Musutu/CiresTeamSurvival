"""Sweep every body's clips for backwards elbows and reversed hands (CireRigAudit, Docs/RigAudit.md).

    python Tools/RunRigAudit.py [--only CTS_ChampHQ_,Monsters/Drowned] [--summary]

Starts the game with -CireRigAudit: every skeletal body referenced by Content/Data/*.json is evaluated over every clip on
its skeleton (12 samples per clip); arms are measured with CireRigAudit::MeasurePose (signed elbow flexion: negative =
hyperextended; hand twist vs. the bind pose). Output: Saved/RigAudit/<stamp>.json, plus a summary table on stdout
grouped by category (champion / npc / monster). Exit 0 when the sweep finished (defects are reported, not failed).
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def summarize(path: Path) -> str:
    data = json.loads(path.read_text(encoding="utf-8"))
    lines = []
    for cat in ("champion", "npc", "monster", "explicit"):
        rows = [b for b in data["bodies"] if b["category"] == cat]
        bad = [b for b in rows if b["hyperSamples"]]
        lines.append(f"== {cat}: {len(rows)} bodies, {len(bad)} with backwards/folded elbows (twist is reported, not failed)")
        for b in sorted(bad, key=lambda r: -r["hyperSamples"]):
            name = b["body"].split(".")[-1]
            lines.append(f"  {name:44s} clips={b['clips']:3d} hyper={b['hyperSamples']:4d} twist={b['twistSamples']:4d} "
                         f"worstElbow={b['worstElbow']:7.1f} ({b['worstElbowClip']}) worstTwist={b['worstTwist']:7.1f} "
                         f"bind={b['bindElbowL']:.1f}/{b['bindElbowR']:.1f}")
            for c in sorted(b["badClips"], key=lambda c: -c["hyperSamples"])[:6]:
                if c["hyperSamples"]:
                    lines.append(f"      {c['clip'].split('.')[-1]:52s} samples={c['hyperSamples']:3d} worstElbow={c['worstElbow']:7.1f}")
    return "\n".join(lines)


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--only", default="")
    p.add_argument("--meshes", default="", help="explicit skeletal mesh object paths (comma separated), category 'explicit'")
    p.add_argument("--summary", type=Path, default=None, help="only summarize an existing Saved/RigAudit json")
    p.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    p.add_argument("--timeout", type=int, default=3600)
    a = p.parse_args()
    if a.summary:
        print(summarize(a.summary))
        return 0
    log = ROOT / "Saved/Logs/RigAudit.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    cmd = [str(a.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireRigAudit", "-CireTripoChampions",
           "-nullrhi", "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if a.only:
        cmd.append(f"-CireRigAuditOnly={a.only}")
    if a.meshes:
        cmd.append(f"-CireRigAuditMeshes={a.meshes}")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation,
                             env={**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"})
    try:
        child.wait(timeout=a.timeout)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    m = re.search(r"CIRE_RIG_AUDIT_DONE .*file=(.+)", text)
    if not m:
        print("RIG AUDIT FAIL (no CIRE_RIG_AUDIT_DONE)")
        return 1
    print(m.group(0).strip())
    print(summarize(Path(m.group(1).strip())))
    for line in text.splitlines():
        if "CIRE_RIG_AUDIT_BIND" in line:
            print(line[line.index("CIRE_RIG_AUDIT_BIND"):])
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
