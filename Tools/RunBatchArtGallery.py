"""Render ready champion bindings with exact native body/animation assertions.

Defaults to every ready profile, requiring Lancer and Summoner. --profiles accepts
an explicit comma-separated subset. --plan is read-only and never launches UE.
Two profiles share each page, with idle/walk/windup/release/recovered captures.
Native checks and PNG verification are structural QA; images still need review.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
STATES = {"idle_front", "walk_angled", "attack_windup", "attack_release", "recovered_idle"}
MOBILITY_STATES = {"walking_slow", "airborne", "roll_mid"}
FAILURE = re.compile(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:")


def fingerprint(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def canonical_asset(path: str) -> str:
    package = path.split(".")[0]
    if not re.fullmatch(r"/Game/[A-Za-z0-9_/-]+", package):
        raise ValueError("Unsafe or non-game asset path: " + path)
    return package + "." + package.rsplit("/", 1)[1]


def binding_plan(root: Path, profiles: str | None, mobility: bool = False) -> dict:
    path = root / "Content/Data/ChampionArtBindings.json"
    document = json.loads(path.read_text(encoding="utf-8-sig"))
    if document.get("schemaVersion") != 1 or not isinstance(document.get("bindings"), list):
        raise ValueError("Expected ChampionArtBindings schemaVersion 1")
    ready = {}
    for row in document["bindings"]:
        if row.get("status") not in ("ready", "custom_ready"):
            continue
        name = row.get("profileId", "")
        if not re.fullmatch(r"[a-z][a-z0-9_]{0,63}", name) or name in ready:
            raise ValueError("Invalid or duplicate ready profile ID: " + name)
        ready[name] = row
    required = profiles.split(",") if profiles else ["lancer", "summoner"]
    if any(not item or item not in ready for item in required) or len(required) != len(set(required)):
        raise ValueError("Every requested profile must have one ready binding; required: " + ", ".join(required))
    selected = {name: ready[name] for name in required} if profiles else ready
    if not 1 <= len(selected) <= 32:
        raise ValueError("Gallery supports 1..32 ready profiles")
    for name, row in selected.items():
        for key in (("mesh",) if row.get("status")=="custom_ready" else ("mesh", "locomotion", "attack")):
            asset = canonical_asset(row[key])
            package = asset.split(".")[0]
            if not (root / "Content" / (package[len("/Game/"):] + ".uasset")).is_file():
                raise ValueError(f"Saved {key} package is missing for {name}: {asset}")
    pages = (len(selected) + 1) // 2
    states=STATES|MOBILITY_STATES if mobility else STATES
    return dict(profiles=selected, pages=pages, states=sorted(states), mobility=mobility, expectedCaptures=pages*len(states), bindingFile=str(path), bindingSha256=fingerprint(path))


def validate_outputs(directory: Path, plan: dict) -> tuple[list, list, dict]:
    errors, captures = [], []
    manifest = json.loads((directory / "manifest.json").read_text(encoding="utf-8"))
    if manifest.get("passed") is not True or manifest.get("visualReviewAccepted") is not False:
        errors.append("Native manifest did not pass structural checks or incorrectly claimed visual approval")
    expected = plan["profiles"]
    requested_states=set(plan.get("states",STATES))
    actual = {row["profileId"]: row for row in manifest.get("profiles", [])}
    if actual.keys() != expected.keys():
        errors.append("Native manifest profile set differs from the planned ready bindings")
    states = {name: set() for name in expected}
    for row in manifest.get("captures", []):
        path = Path(row["file"]).resolve()
        if path.parent != directory.resolve():
            errors.append("Capture path escaped the native gallery directory")
            continue
        with path.open("rb") as stream:
            header = stream.read(24)
        valid_png = len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n"
        size = struct.unpack(">II", header[16:24]) if valid_png else (0, 0)
        if size != (1920, 1080) or path.stat().st_size <= 10000:
            errors.append("Invalid or unexpectedly small rendered PNG: " + path.name)
        pose_ids = {pose.get("profileId") for pose in row.get("poses", [])}
        if pose_ids != set(row.get("profiles", [])):
            errors.append("Capture is missing pose assertions for one or more profiles")
        for pose in row.get("poses", []):
            name = pose.get("profileId")
            if name not in expected or row.get("state") not in requested_states or pose.get("valid") is not True:
                errors.append("Invalid native pose record: " + str(name))
                continue
            if row["state"] in states[name]:
                errors.append("Duplicate profile state capture: " + name + "/" + row["state"])
            states[name].add(row["state"])
            if canonical_asset(pose.get("actualMesh", "")).lower() != canonical_asset(expected[name]["mesh"]).lower() or not pose.get("skeleton"):
                errors.append("Rendered mesh does not equal its selected binding: " + name)
        captures.append(dict(path=str(path), profiles=row.get("profiles", []), state=row.get("state"), width=size[0], height=size[1], bytes=path.stat().st_size))
    if len(captures) != plan["expectedCaptures"] or len(list(directory.glob("*.png"))) != plan["expectedCaptures"]:
        errors.append("Capture count differs from the requested states per profile page")
    for name, observed in states.items():
        if observed != requested_states:
            errors.append("Missing idle/walk/attack states for " + name)
    return captures, errors, manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--project", type=Path, default=ROOT / "CiresTeamSurvival.uproject")
    parser.add_argument("--editor", type=Path, default=Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"))
    parser.add_argument("--profiles", help="Comma-separated explicit subset, e.g. lancer,summoner")
    parser.add_argument("--mobility", action="store_true", help="Also capture walking speed, airborne and mid-roll presentation")
    parser.add_argument("--plan", action="store_true", help="Check bindings and package presence without launching or writing anything")
    args = parser.parse_args()
    root = args.project.resolve().parent
    try:
        plan = binding_plan(root, args.profiles, args.mobility)
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps(dict(passed=False, launched=False, failure=str(error)), indent=2))
        return 1
    if args.plan:
        print(json.dumps(dict(launched=False, **plan), indent=2)); return 0
    if not args.editor.is_file() or not args.project.is_file():
        parser.error("Project or editor executable is missing")
    folder = root / "Saved/BatchArtGalleryChecks" / datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    folder.mkdir(parents=True)
    log = folder / "gallery.log"
    command = [str(args.editor.resolve()), str(args.project.resolve()), "/Game/Maps/Citadel", "-game", "-CireBatchArtGallery", "-CireTripoChampions",
               "-RenderOffscreen", "-ForceRes", "-ResX=1920", "-ResY=1080", "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
               "-ExecCmds=t.MaxFPS 60", f"-abslog={log}"]
    if args.mobility:
        command.append("-CireMobilityArtGallery")
    if args.profiles:
        command.append("-CireBatchArtProfiles=" + args.profiles)
    started = time.monotonic(); failure = ""; timeout = 110 + plan["pages"]*(len(plan["states"])*3+5)
    with (folder / "console.log").open("wb") as stream:
        child = subprocess.Popen(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        try:
            code = child.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            child.terminate()
            try:
                code = child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                child.kill(); code = child.wait(timeout=5)
            failure = f"Batch art process exceeded {timeout} seconds"
    text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
    errors = [line for line in text.splitlines() if FAILURE.search(line)]
    match = re.search(r"CIRE_BATCH_ART_GALLERY_PASS profiles=(\d+) captures=(\d+) checks=(\d+) directory=(.+)", text)
    captures, manifest = [], {}
    if match:
        try:
            captures, output_errors, manifest = validate_outputs(Path(match.group(4).strip()), plan); errors.extend(output_errors)
            if int(match.group(1)) != len(plan["profiles"]) or int(match.group(2)) != plan["expectedCaptures"]:
                errors.append("Native PASS counts differ from the planned profiles and captures")
        except (OSError, ValueError, TypeError, KeyError) as error:
            errors.append("Native output verification failed: " + str(error))
    try:
        if fingerprint(Path(plan["bindingFile"])) != plan["bindingSha256"]:
            errors.append("Runtime bindings changed during capture; results are not reproducible")
    except OSError as error:
        errors.append("Runtime bindings could not be rechecked: " + str(error))
    passed = code == 0 and match is not None and not errors and not failure
    report = dict(passed=passed, exitCode=code, seconds=round(time.monotonic()-started, 2), failure=failure, errors=errors,
                  log=str(log), plan=plan, captures=captures, checks=int(match.group(3)) if match else None,
                  visualReviewAccepted=False, visualReviewRequired=True, stagedAnimationPresentation=True)
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
