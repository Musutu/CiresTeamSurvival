"""progression-shop: run the item/shop/loot/teleport checks.

  --only data     validate Content/Data/Items.json + LootTables.json offline (no Unreal)
  --only native   -CireProgressionProbe: in-engine smoke (items, stats, consumables, actives,
                  teleport, loot chests, fair distribution, pack gating, NPC prep pause)
  --only network  dedicated server + remote client (-CireShopNetServer/-CireShopNetClient):
                  server-enforced shop, replicated inventory/gold/feedback, undo, teleport
  --only gallery  offscreen 1920x1080 captures (-CireShopGallery) of the shop, Skill Shop, purchase/feedback moments,
                  stats window, loot chest and teleport button; then the shop screens again at 1600x900
  (default: all)

Only child processes started here are ever stopped. Reports: Saved/ProgressionChecks/<stamp>/report.json
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
FAILURE = re.compile(r"CIRE_\S*(?:FAIL|ERROR)|Fatal error:|Assertion failed:|Ensure condition failed:")
EDITOR = Path("F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe")


# AutoSDK is off on this machine, so every editor boot otherwise runs "Build.bat -Mode=ValidatePlatforms"
# and blocks on Build.bat's machine-wide lock file while any other worktree compiles. Probes only target Win64.
EDITOR_ENV = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1"}


def kill_tree(child) -> None:
    """Kill the child's whole process tree so a Build.bat spawned by the editor cannot outlive it."""
    if os.name == "nt" and child.poll() is None:
        subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL, check=False)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace") if path.exists() else ""


def stop(child):
    if child is None or child.poll() is not None:
        return
    kill_tree(child)
    child.terminate()
    try:
        child.wait(timeout=5)
    except subprocess.TimeoutExpired:
        child.kill()
        child.wait(timeout=5)


def validate_data() -> dict:
    errors = []
    items = json.loads((ROOT / "Content/Data/Items.json").read_text(encoding="utf-8"))
    loot = json.loads((ROOT / "Content/Data/LootTables.json").read_text(encoding="utf-8"))
    by_id = {item["id"]: item for item in items["items"]}
    if len(by_id) != len(items["items"]):
        errors.append("duplicate item ids")
    total = {}

    def cost(item_id, seen=()):
        if item_id in total:
            return total[item_id]
        if item_id in seen:
            raise ValueError(f"recipe cycle at {item_id}")
        item = by_id[item_id]
        value = item["cost"] + sum(cost(c, seen + (item_id,)) for c in item.get("components", []))
        total[item_id] = value
        return value
    for item in items["items"]:
        for component in item.get("components", []):
            if component not in by_id:
                errors.append(f"{item['id']}: unknown component {component}")
        try:
            cost(item["id"])
        except (ValueError, KeyError) as error:
            errors.append(str(error))
        if not (ROOT / f"Content/UI/Items/src/T_Item_{item['id']}.png").exists():
            errors.append(f"{item['id']}: icon source missing")
        if not (ROOT / f"Content/UI/Items/T_Item_{item['id']}.uasset").exists():
            errors.append(f"{item['id']}: icon asset missing")
    for role, groups in items["recommended"].items():
        for group, ids in groups.items():
            errors += [f"recommended {role}/{group}: unknown {i}" for i in ids if i not in by_id]
    for name, table in loot["tables"].items():
        for entry in table["entries"]:
            errors += [f"loot {name}: unknown {i}" for i in entry.get("pool", []) if i not in by_id]
    for source in loot["sources"].values():
        errors += [f"loot source table {s['table']} missing" for s in source if s["table"] not in loot["tables"]]
    tiers = {}
    for item in items["items"]:
        tiers[item["tier"]] = tiers.get(item["tier"], 0) + 1
    legendary = [total[i] for i, item in by_id.items() if item["tier"] == "legendary" and item.get("purchasable", True)]
    summary = dict(items=len(by_id), tiers=tiers, actives=sum(1 for i in items["items"] if "use" in i and not i.get("belt") and not i.get("instant")),
                   uniques=sum(1 for i in items["items"] if i.get("unique")), legendaryCost=[min(legendary), max(legendary)],
                   tables=len(loot["tables"]), sounds=sorted(p.stem for p in (ROOT / "Content/UI/Shop").glob("S_*.uasset")))
    if len(summary["sounds"]) < 9:
        errors.append("shop sounds missing")
    return {"passed": not errors, "errors": errors, "summary": summary}


def result(logs, codes, markers, failure=""):
    texts = [read(path) for path in logs]
    errors = [line for text in texts for line in text.splitlines() if FAILURE.search(line)]
    return {
        "passed": not failure and not errors and all(code == 0 for code in codes) and all(m in t for m, t in zip(markers, texts)),
        "failure": failure or None, "exitCodes": codes, "logs": [str(p) for p in logs], "errors": errors,
        "evidence": [line.split("]", 2)[-1].strip() for text in texts for line in text.splitlines()
                     if "CIRE_" in line and any(w in line for w in ("PASS", "READY", "PREP", "SURVIVAL", "CAPTURE", "LOADED", "DROP", "DISTRIBUTED"))],
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--only", choices=("data", "native", "network", "gallery", "all"), default="all")
    parser.add_argument("--port", type=int, default=7791)
    parser.add_argument("--theme", help="ui-themes: render the gallery with this UI theme id")
    args = parser.parse_args()
    project = ROOT / "CiresTeamSurvival.uproject"
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
    output = ROOT / "Saved/ProgressionChecks" / stamp
    output.mkdir(parents=True, exist_ok=False)
    creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
    common = ["-unattended", "-nop4", "-NoLiveCoding", "-nosplash"]
    reports = {}

    def save(name, record):
        reports[name] = record
        (output / "report.json").write_text(json.dumps({"passed": all(r["passed"] for r in reports.values()), "results": reports}, indent=2) + "\n", encoding="utf-8")
        print(f"{name}: {'PASS' if record['passed'] else 'FAIL'}", flush=True)
        for line in record.get("evidence", []) + record.get("errors", []):
            print("  " + line, flush=True)
        if record.get("failure"):
            print("  " + record["failure"], flush=True)

    def launch(address, switches, log, extra):
        command = [str(EDITOR), str(project), address, *switches, f"-abslog={log}", *common, *extra]
        return subprocess.Popen(command, cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation, env=EDITOR_ENV)

    if args.only in ("data", "all"):
        save("data", validate_data())

    if args.only in ("native", "all"):
        log, child, failure = output / "native.log", None, ""
        try:
            child = launch("/Game/Maps/Citadel", ["-game", "-CireProgressionProbe"], log, ["-nullrhi", "-nosound"])
            child.wait(timeout=240)
        except subprocess.TimeoutExpired:
            failure = "native probe timed out"
        finally:
            stop(child)
        save("native", result([log], [child.returncode if child else None], ["CIRE_PROGRESSION_PROBE_PASS"], failure))

    if args.only in ("network", "all"):
        logs = [output / "server.log", output / "client.log"]
        children, failure = [], ""
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.bind(("127.0.0.1", args.port))
            children.append(launch("/Game/Maps/Citadel", ["-server", f"-port={args.port}", "-CireShopNetServer"], logs[0], ["-nullrhi", "-nosound"]))
            deadline = time.monotonic() + 240
            while "CIRE_SHOP_NET_SERVER_READY" not in read(logs[0]):
                if children[0].poll() is not None:
                    raise RuntimeError(f"server exited early: {children[0].returncode}")
                if time.monotonic() > deadline:
                    raise TimeoutError("server readiness timeout")
                time.sleep(.25)
            children.append(launch(f"127.0.0.1:{args.port}", ["-game", "-CireShopNetClient"], logs[1], ["-nullrhi", "-nosound"]))
            deadline = time.monotonic() + 260
            while any(child.poll() is None for child in children):
                if time.monotonic() > deadline:
                    raise TimeoutError("network probe timeout")
                if any(FAILURE.search(read(log)) for log in logs):
                    raise RuntimeError("network probe reported a failure")
                time.sleep(.25)
        except (OSError, RuntimeError, TimeoutError) as error:
            failure = str(error)
        finally:
            for child in reversed(children):
                stop(child)
        codes = [children[i].returncode if i < len(children) else None for i in range(2)]
        save("network", result(logs, codes, ["CIRE_SHOP_NET_SERVER_PASS", "CIRE_SHOP_NET_CLIENT_PASS"], failure))

    if args.only in ("gallery", "all"):
        # Full gallery at 1920x1080, then the shop screens (Skill Shop + Armory) again at 1600x900.
        for width, height, shop_only in ((1920, 1080, False), (1600, 900, True)):
            name = "gallery" if not shop_only else f"gallery_{width}x{height}"
            log, child, failure = output / f"{name}.log", None, ""
            try:
                switches = ["-game", "-CireShopGallery", "-RenderOffscreen", "-ForceRes", f"-ResX={width}", f"-ResY={height}", "-ExecCmds=t.MaxFPS 60"]
                if args.theme:
                    switches.append(f"-CireUITheme={args.theme}")
                if shop_only:
                    switches.insert(2, "-CireShopGalleryShopOnly")
                child = launch("/Game/Maps/Citadel", switches, log, ["-nosound"])
                child.wait(timeout=360)
            except subprocess.TimeoutExpired:
                failure = "gallery timed out"
            finally:
                stop(child)
            record = result([log], [child.returncode if child else None], ["CIRE_SHOP_GALLERY_PASS"], failure)
            match = re.search(r"CIRE_SHOP_GALLERY_PASS captures=(\d+) directory=(.+)", read(log))
            captures = []
            if match:
                for path in sorted(Path(match.group(2).strip()).glob("*.png")):
                    header = path.read_bytes()[:24]
                    size = struct.unpack(">II", header[16:24]) if header[:8] == bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]) else (0, 0)
                    captures.append({"path": str(path), "width": size[0], "height": size[1], "bytes": path.stat().st_size})
                    record["passed"] = record["passed"] and size == (width, height)
            record["captures"] = captures
            record["visualReviewAccepted"] = False
            save(name, record)

    print(f"Report: {output / 'report.json'}")
    return 0 if reports and all(r["passed"] for r in reports.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
