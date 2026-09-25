"""progression-shop: fetch and import the CC0 critters that Polymorph turns monsters into.

Critters: Chicken and Frog (Quaternius, CC0 1.0, animated, via poly.pizza), plus the Pig that
Tools/FetchFreeCreatures.py already imported (/Game/Free/Creatures/Pig, shown as a piglet).
Every page is checked before download exactly like Tools/FetchFreeCreatures.py (licence "CC0 1.0",
animated, creator Quaternius, ResourceID equal to the file). Raw GLBs go to Saved/CreatureSources
(not committed). The skinned-node transform is normalized first (the glTF "scale 100" fix, see
ImportFreeCreatures.normalized_glb), then each model is imported into /Game/Free/Critters/<Model>
with its own flat colour materials, and duplicate clip exports are removed. Nothing else in Content
is touched. Writes Art/Creatures/Free/Critters.json (mesh + clip paths) and PROVENANCE_Critters.md.

Usage: python Tools/ImportPolymorphCritters.py
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "Tools"))
SRC = ROOT / "Saved/CreatureSources"
ART = ROOT / "Art/Creatures/Free"
PKG = "/Game/Free/Critters"
# name -> (poly.pizza ResourceID, search term)
CRITTERS = {
    "Chicken": ("a0001762-9352-48c3-9abd-be91e42db114", "Chicken"),
    "Frog": ("08416495-b056-4ed7-b23b-5a10e82277a0", "Frog"),
}


def fetch():
    import FetchFreeCreatures as F
    records = []
    for name, (resource, term) in CRITTERS.items():
        public_id = F.page_for(resource, term)
        info = F.verify(public_id, resource)
        url = f"https://static.poly.pizza/{resource}.glb"
        data = F.get(url)
        if data[:4] != b"glTF":
            raise RuntimeError(f"{url} is not a binary glTF")
        SRC.mkdir(parents=True, exist_ok=True)
        (SRC / f"{name}.glb").write_bytes(data)
        records.append({"name": name, "title": info["title"], "page": info["page"], "url": url, "licence": info["licence"],
                        "creator": info["creator"], "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        print(f"{name}: {info['page']} CC0 1.0 verified")
    return records


def run(u):
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    out = {}
    for name in CRITTERS:
        dest = f"{PKG}/{name}"
        if lib.does_directory_exist(dest):
            lib.delete_directory(dest)
        task = u.AssetImportTask()
        for k, v in (("filename", str(SRC / "Normalized" / f"{name}.glb")), ("destination_path", dest), ("automated", True),
                     ("replace_existing", True), ("save", True)):
            task.set_editor_property(k, v)
        tools.import_asset_tasks([task])
        mesh, anims = None, {}
        for p in lib.list_assets(dest, recursive=True):
            a = lib.load_asset(p)
            if isinstance(a, u.SkeletalMesh):
                mesh = a
            elif isinstance(a, u.AnimSequence):
                anims[a.get_name()] = a
        assert mesh, name
        clips = {}
        for clip, a in sorted(anims.items(), key=lambda kv: (0 if "Armature" in kv[0] else 1, len(kv[0]))):
            key = clip[len(name):] if clip.startswith(name) else clip
            for prefix in ("AnimalArmature_", "Armature_"):
                while key.startswith(prefix):
                    key = key[len(prefix):]
            key = key.strip("_")
            if key in clips:
                lib.delete_asset(a.get_path_name())
            else:
                clips[key] = a.get_path_name()
        box = mesh.get_bounds().box_extent
        out[name] = {"mesh": mesh.get_path_name(), "clips": clips, "height": round(box.z * 2, 1), "length": round(max(box.x, box.y) * 2, 1)}
        lib.save_directory(dest, only_if_is_dirty=False, recursive=True)
        u.log(f"CIRE_CRITTER {name} mesh={mesh.get_path_name()} clips={sorted(clips)} height={out[name]['height']}")
    (ART / "Critters.unreal.json").write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    u.log("CIRE_CRITTERS_IMPORT_PASS")


def launch():
    import ImportFreeCreatures as I
    records = fetch()
    for name in CRITTERS:
        I.normalized_glb(name)
    log = ROOT / "Saved/Logs/PolymorphCritters.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"),
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-run=pythonscript",
               f"-script={Path(__file__).resolve()}", f"-abslog={log}"]
    env = dict(os.environ, UE_SKIP_UBT_SDK_SETUP="1")  # skip the AutoSDK UBT query (stalls when UBT is busy)
    result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, env=env, timeout=1200,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    # The editor falls back to <name>_2.log when the log file is still locked: read the newest one.
    newest = max(log.parent.glob("PolymorphCritters*.log"), key=lambda f: f.stat().st_mtime)
    text = newest.read_text(encoding="utf-8", errors="replace")
    for line in text.splitlines():
        if "CIRE_CRITTER" in line or ("Error" in line and "Python" in line):
            print(line.split("LogPython: ")[-1])
    if "CIRE_CRITTERS_IMPORT_PASS" not in text:
        print(f"import failed exit={result.returncode} log={log}")
        return 1
    imported = json.loads((ART / "Critters.unreal.json").read_text(encoding="utf-8"))
    (ART / "Critters.unreal.json").unlink()
    manifest = {"source": "poly.pizza (Quaternius, CC0 1.0)", "fetched": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                "assets": [dict(r, **imported[r["name"]]) for r in records]}
    (ART / "Critters.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    lines = ["# Polymorph critter provenance (progression-shop)", "",
             "Polymorph turns a monster into a Chicken, a Piglet or a Frog. The Piglet reuses the Pig in",
             "`/Game/Free/Creatures/Pig` (see PROVENANCE.md). The models below are by **Quaternius** (https://quaternius.com),",
             "released under **CC0 1.0 Universal** (public domain; commercial use, modification and redistribution allowed).",
             "`Tools/ImportPolymorphCritters.py` checked each poly.pizza page before downloading (licence `CC0 1.0`, animated,",
             "creator Quaternius, ResourceID equal to the file). Imported to `/Game/Free/Critters/<Model>`.", "",
             f"Fetched: {manifest['fetched']}", "", "| Model | Author | Page | File | SHA-256 | License |", "| --- | --- | --- | --- | --- | --- |"]
    for a in manifest["assets"]:
        lines.append(f"| {a['name']} | Quaternius | {a['page']} | {a['url']} | `{a['sha256'][:16]}...` | CC0 1.0 |")
    (ART / "PROVENANCE_Critters.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print("CIRE_CRITTERS_PASS")
    return 0


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        sys.exit(launch())
    else:
        run(unreal)
