"""vendors: render the merchant review gallery (Source/CiresTeamSurvival/CireVendorGallery.cpp).

Offscreen 1920x1080 captures land in Saved/Vendors/Gallery/<stamp>/ (or --out):
  * per merchant body: the 4-view turnaround (front / right / back / left, reference pose) plus face and back of
    head, then idle and greet frames with both hands from the front and each hand from its side;
  * each stall from the customer's side, a wide shot of all three, the nameplate + interact prompt, the shop on
    every merchant tab.
--meshes a,b   only the mesh check for these asset paths (e.g. a fresh unrigged Tripo import before rigging).
--yaw N        mesh yaw so the body faces +X (Tripo exports face +Y: -90, the default).
Usage: python Tools/RunVendorGallery.py [--meshes /Game/TripoModels/X/X.X] [--out DIR]
Captures still need a visual review (Janus faces, hands, fused parts); the tool only proves they were written.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--meshes", default="")
    parser.add_argument("--yaw", type=float, default=-90.0)
    parser.add_argument("--out", default="")
    parser.add_argument("--timeout", type=int, default=1300)
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    args = parser.parse_args()
    log = ROOT / "Saved/Logs/VendorGallery.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = [str(args.editor), str(ROOT / "CiresTeamSurvival.uproject"), "/Game/Maps/Citadel", "-game", "-CireVendorGallery",
               "-RenderOffscreen", "-ForceRes", "-windowed", "-ResX=1920", "-ResY=1080", f"-CireVendorGalleryYaw={args.yaw}",
               "-nosound", "-unattended", "-nop4", "-NoLiveCoding", "-nosplash", f"-abslog={log}"]
    if args.meshes:
        command.append(f"-CireVendorGalleryMeshes={args.meshes}")
    if args.out:
        command.append(f"-CireVendorGalleryOut={Path(args.out).resolve()}")
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    child = subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)
    try:
        code = child.wait(timeout=args.timeout)
    except subprocess.TimeoutExpired:
        kill_tree(child)
        code = -1
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    for line in text.splitlines():
        if re.search(r"CIRE_VENDOR_GALLERY_(PASS|FAIL|CHECK_FAIL|READY|NOCLIP)|CIRE_VENDORS_SPAWNED|Vendors: |Fatal error", line):
            print(line.strip())
    passed = code == 0 and "CIRE_VENDOR_GALLERY_PASS" in text
    print("CIRE_VENDOR_GALLERY_RUN", "PASS" if passed else "FAIL", "exit", code)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
