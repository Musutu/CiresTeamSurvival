"""Download the CC0 animated creatures that fill feral_kin / hollow / stoneborn units (world-dressing).

Source: Quaternius models (https://quaternius.com, CC0 1.0) hosted as single animated GLBs on poly.pizza.
For every model the script resolves its poly.pizza page from the search results, reads the page's embedded
state and refuses the model unless it says "Licence":"CC0 1.0", "Animated":true and creator Quaternius, and
the page's ResourceID equals the file we download. Plain HTTPS only; nothing downloaded is executed.
Raw GLBs land in Saved/CreatureSources (not committed; re-downloadable, SHA-256 recorded).
Writes Art/Creatures/Free/SourceManifest.json and Art/Creatures/Free/PROVENANCE.md.

Quaternius's own Google Drive links were quota-blocked for anonymous downloads on 2026-09-24; poly.pizza serves
the same models. Quaternius's newer QAL-licensed "Bestiary" pack is NOT used (no redistribution allowed).

Usage: python Tools/FetchFreeCreatures.py [--provenance-only]
"""
from __future__ import annotations

import hashlib
import json
import re
import sys
import time
import urllib.parse
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "Saved/CreatureSources"
ART = ROOT / "Art/Creatures/Free"
AGENT = {"User-Agent": "Mozilla/5.0 (CiresTeamSurvival asset fetch; contact via repo)"}

# name -> (poly.pizza ResourceID, search term, purpose)
CREATURES = {
    "Wolf": ("f1d12388-e39b-4157-b32a-646a1d089fc4", "Wolf", "feral_kin Dire Wolf, hollow Grave Hound"),
    "Stag": ("a9c69fbc-bf7c-4585-9a49-a82e0be1ac6b", "Stag", "feral_kin Antlered Shaman"),
    "Bull": ("5704ef69-2c27-4de8-a942-70a29458af21", "Bull", "feral_kin Tusked Behemoth"),
    "Pig": ("6277c120-ea5c-405d-9614-0d3d7e0b829d", "Pig", "feral_kin Bristleback Boar"),
    "Spider": ("4259fbdb-afb5-4d40-9108-363625dd6b6e", "Spider", "stoneborn Crystal Ballista (spider-legged construct)"),
}


def get(url: str) -> bytes:
    for attempt in range(4):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=120) as r:
                return r.read()
        except Exception as error:  # network hiccup: retry with backoff
            if attempt == 3:
                raise
            print(f"retry {url}: {error}")
            time.sleep(2 + attempt * 3)
    raise RuntimeError(url)


def page_for(resource: str, term: str) -> str:
    text = get(f"https://poly.pizza/search/{urllib.parse.quote(term)}").decode("utf-8", "replace")
    for m in re.finditer(r'"previewUrl":"([^"]+)","publicID":"([^"]+)"', text):
        if resource in m.group(1).replace("\\u002F", "/"):
            return m.group(2)
    raise RuntimeError(f"no poly.pizza page lists resource {resource} (search '{term}')")


def verify(public_id: str, resource: str) -> dict:
    url = f"https://poly.pizza/m/{public_id}"
    text = get(url).decode("utf-8", "replace")
    lic = re.search(r'"Licence":"([^"]+)"', text)
    res = re.search(r'"ResourceID":"([^"]+)"', text)
    ani = re.search(r'"Animated":(true|false)', text)
    title = re.search(r'"Title":"([^"]+)"', text)
    creator = "Quaternius" if re.search(r'"(?:Username|username)":"Quaternius"', text) else None
    info = {"page": url, "licence": lic and lic.group(1), "resource": res and res.group(1), "animated": ani and ani.group(1) == "true",
            "title": title and title.group(1), "creator": creator}
    if info["licence"] != "CC0 1.0" or info["resource"] != resource or not info["animated"] or creator != "Quaternius":
        raise RuntimeError(f"{url}: refusing, page says {info}")
    return info


def write_provenance() -> None:
    data = json.loads((ART / "SourceManifest.json").read_text(encoding="utf-8"))
    lines = ["# Free creature provenance (world-dressing)", "",
             "Animated creature bodies committed to the repository. Every model below is by **Quaternius**",
             "(https://quaternius.com) and released under **CC0 1.0 Universal** (public domain dedication,",
             "https://creativecommons.org/publicdomain/zero/1.0/): no attribution required, commercial use, modification",
             "and redistribution allowed. Credits are given anyway. `Tools/FetchFreeCreatures.py` checked each poly.pizza",
             "page before downloading: licence `CC0 1.0`, animated, creator Quaternius, and the page's ResourceID equal to",
             "the downloaded file. Nothing downloaded was executed.", "",
             "Not used: Quaternius's *Bestiary - Dungeon Monsters Kit* (Quaternius Asset License, no redistribution).", "",
             f"Fetched: {data['fetched']}", "",
             "| Model | Author | Used for | Page | File | SHA-256 | License |", "| --- | --- | --- | --- | --- | --- | --- |"]
    for a in data["assets"]:
        lines.append(f"| {a['name']} | Quaternius | {a['purpose']} | {a['page']} | {a['url']} | `{a['sha256'][:16]}...` | CC0 1.0 |")
    lines += ["", "## Derived content (original)", "",
              "* `/Game/Free/Creatures/<Model>/`: the imported skeletal meshes and clips; sockets (`head`, `pelvis`, `spine_03`,",
              "  `hand_l/r`, `foot_l/r`, `ball_l/r`) added by `Tools/ImportFreeCreatures.py` so aura, footstep and test code find",
              "  quadruped anatomy.",
              "* `T_<Model>_<Slot>_Fur*` textures and `MI_<Model>_<Slot>` instances: procedural fur/hide detail generated by",
              "  `Tools/ImportFreeCreatures.py` from each model's flat material colour, parented to the race skin material.", ""]
    (ART / "PROVENANCE.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    if "--provenance-only" in sys.argv:
        write_provenance()
        return 0
    records = []
    for name, (resource, term, purpose) in CREATURES.items():
        public_id = page_for(resource, term)
        info = verify(public_id, resource)
        url = f"https://static.poly.pizza/{resource}.glb"
        target = CACHE / f"{name}.glb"
        target.parent.mkdir(parents=True, exist_ok=True)
        data = get(url)
        if data[:4] != b"glTF":
            raise RuntimeError(f"{url} is not a binary glTF")
        target.write_bytes(data)
        records.append({"name": name, "title": info["title"], "purpose": purpose, "page": info["page"], "url": url,
                        "licence": info["licence"], "creator": info["creator"], "file": str(target.relative_to(ROOT)).replace("\\", "/"),
                        "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        print(f"{name}: {info['page']} {len(data) / 1e6:.2f} MB CC0 1.0 verified")
    ART.mkdir(parents=True, exist_ok=True)
    (ART / "SourceManifest.json").write_text(json.dumps({"source": "poly.pizza (Quaternius, CC0 1.0)",
                                                         "fetched": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                                                         "assets": records}, indent=2) + "\n", encoding="utf-8")
    write_provenance()
    print(f"CIRE_CREATURE_FETCH_PASS models={len(records)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
