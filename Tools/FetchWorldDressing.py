"""Download the CC0 sources that dress the town and arenas with life (world-dressing).

Plain HTTPS downloads only; nothing downloaded is executed and archives are only read for image files.
  * Poly Haven (https://polyhaven.com, CC0 1.0): market goods, yard tools, furniture, planters, weeds, a rat.
  * ambientCG (https://ambientcg.com, CC0 1.0): an ivy leaf atlas and a wall-moisture streak decal.
Raw files land in Saved/DressingSources (not committed; re-downloadable, Poly Haven MD5s verified).
The committed manifest Art/Environment/Dressing/SourceManifest.json lists every URL, size and hash and
feeds Art/Environment/Dressing/PROVENANCE.md.

Usage:  python Tools/FetchWorldDressing.py [--provenance-only]
"""
from __future__ import annotations

import hashlib
import json
import sys
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "Saved/DressingSources"
ART = ROOT / "Art/Environment/Dressing"
MANIFEST = ART / "SourceManifest.json"
PROVENANCE = ART / "PROVENANCE.md"
PH_API = "https://api.polyhaven.com"
AGENT = {"User-Agent": "CiresTeamSurvival-WorldDressing/1.0 (asset import; contact via repo)"}
PH_LICENSE = "CC0 1.0 Universal (https://polyhaven.com/license)"
ACG_LICENSE = "CC0 1.0 Universal (https://docs.ambientcg.com/license/)"

# Poly Haven id -> purpose (1k glTF; all real-world scale scans/models)
MODELS = {
    # market goods on stall counters and display shelves
    "CheeseBox_01": "cheese box, market goods",
    "wooden_bowl_02": "wooden bowl, stall wares",
    "carved_wooden_plate": "carved plate, stall wares",
    "wooden_cutting_board": "cutting board, stall wares",
    "brass_goblets": "goblets, stall wares / tavern tables",
    "brass_pot_01": "brass cooking pot, stall wares",
    "ceramic_vase_01": "earthenware vase, pottery stall",
    "ceramic_vase_03": "earthenware vase, pottery stall",
    "antique_ceramic_vase_01": "amphora-like jar, pottery stall",
    "yellow_onion": "onions, produce",
    "sweet_potato": "root vegetables, produce",
    "food_pears_asian_01": "pears, produce",
    "food_pomegranate_01": "pomegranates, produce",
    "wine_bottles_01": "wine bottles, tavern / wine stall",
    "wooden_display_shelves_01": "open display shelves for stalls",
    "wooden_bucket_02": "second bucket variant",
    # yards, workshops, doorsteps
    "spinning_wheel_01": "spinning wheel, residential doorstep",
    "wooden_broom": "broom leaning on walls",
    "sledgehammer_01": "sledgehammer, workshop clutter",
    "hatchet": "hatchet on the woodpile",
    "handsaw_wood": "saw, workshop clutter",
    "wooden_axe_02": "axe variant, woodpiles",
    "wooden_hammer_01": "mallet, workshop clutter",
    "folding_wooden_stool": "stool variant",
    "painted_wooden_bench": "bench along facades",
    "wooden_stool_01": "stool variant",
    "round_wooden_table_02": "round tavern table",
    "wooden_ladder_02": "ladder against walls",
    # castle
    "ornate_war_hammer": "weapon rack, castle",
    "ornate_medieval_mace": "weapon rack, castle",
    "antique_estoc": "weapon rack, castle",
    "lantern_chandelier_01": "hanging iron lantern ring, market hall",
    # plants
    "planter_box_01": "flower box",
    "planter_box_02": "flower box",
    "planter_box_03": "flower box",
    "planter_pot_clay": "clay flower pot",
    "nettle_plant": "nettles along walls",
    "weed_plant_02": "weeds between cobbles",
    "shrub_sorrel_01": "sorrel clumps",
    "dry_branches_medium_01": "dry branch litter",
    # vermin
    "street_rat": "rats in alleys (static)",
}

# ambientCG id -> (resolution zip, purpose)
AMBIENTCG = {
    "LeafSet017": ("2K-JPG", "ivy leaf atlas (colour + opacity + normal) for ivy cards on walls"),
    "Leaking003": ("1K-JPG", "moisture / grime streak under windows and eaves"),
    "ScatteredLeaves007": ("1K-JPG", "fallen-leaf litter patches (colour + opacity)"),
}


def get_json(url: str):
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=60) as r:
        return json.load(r)


def digest(path: Path, algo: str = "md5") -> str:
    h = hashlib.new(algo)
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url: str, target: Path, md5: str | None) -> dict:
    target.parent.mkdir(parents=True, exist_ok=True)
    if not (target.exists() and (md5 is None or digest(target) == md5)):
        for attempt in range(4):
            try:
                tmp = target.with_suffix(target.suffix + ".part")
                with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=180) as r, tmp.open("wb") as out:
                    while chunk := r.read(1 << 20):
                        out.write(chunk)
                tmp.replace(target)
                break
            except Exception as error:  # network hiccup: retry with backoff
                if attempt == 3:
                    raise
                print(f"retry {url}: {error}")
                time.sleep(2 + attempt * 3)
        if md5 and digest(target) != md5:
            raise RuntimeError(f"MD5 mismatch for {url}")
    return {"url": url, "file": str(target.relative_to(ROOT)).replace("\\", "/"), "bytes": target.stat().st_size,
            "md5": md5, "sha256": digest(target, "sha256")}


def write_provenance() -> None:
    data = json.loads(MANIFEST.read_text(encoding="utf-8"))
    lines = ["# World dressing asset provenance (world-dressing)", "",
             "Third-party sources for the props, plants and decals that bring the town and arenas to life. Every asset",
             "below is **CC0 1.0 Universal** (public domain dedication): no attribution is required, commercial use,",
             "modification and redistribution are allowed. Credits are listed anyway.", "",
             "* **Poly Haven** (https://polyhaven.com/license): models fetched from `api.polyhaven.com` / `dl.polyhaven.org`.",
             "* **ambientCG** (https://docs.ambientcg.com/license/): texture sets fetched from `ambientcg.com/get`.", "",
             "Fetched with plain HTTPS by `Tools/FetchWorldDressing.py`; nothing downloaded was executed and zip archives were",
             "only read for their image files. Raw downloads live in `Saved/DressingSources` (not committed). Exact URLs, sizes",
             "and hashes: `Art/Environment/Dressing/SourceManifest.json`. Imported by `Tools/ImportWorldDressing.py` into",
             "`/Game/Free/Dressing`.", "",
             f"Fetched: {data['fetched']}", "",
             "| Kind | Asset | Authors | Res | Used for | Source page | License |",
             "| --- | --- | --- | --- | --- | --- | --- |"]
    for a in data["assets"]:
        lines.append(f"| {a['kind']} | {a['name']} (`{a['id']}`) | {a['authors']} | {a['resolution']} | {a['purpose']} | {a['page']} | CC0 1.0 |")
    lines += ["", "## Reused CC0 content already in the repository", "",
              "* `/Game/Arenas/Trees/island_tree_01`, `/Game/Arenas/Trees/tree_small_02` (leafy trees), `/Game/Arenas/Props/shrub_03`,",
              "  `grass_medium_02`, `root_cluster_01`, `tree_stump_02`, `treasure_chest`, `cannon_01` (static parts): Poly Haven CC0,",
              "  see `Art/Arenas/PROVENANCE.md`.",
              "* `/Game/Environment/Town/...`: Poly Haven CC0 and original town kit, see `Art/Environment/Town/PROVENANCE.md`.", "",
              "## Original (non-third-party) content", "",
              "* `Art/Environment/Dressing/Meshes/*.obj` -> `/Game/Free/Dressing/Meshes`: hanging shop signs, wall banners, laundry",
              "  lines, ivy cards, puddles, grime cards, leaf litter cards, chimney smoke plumes, perched and circling crows, wall",
              "  torches, a weapon rack and hay piles. Authored procedurally by `Tools/BuildDressingMeshes.py`",
              "  for this project (no third-party geometry).",
              "* `/Game/Free/Dressing/Materials`: masters `M_DressCard` (masked two-sided cards: ivy, leaves, grime),",
              "  `M_DressPuddle`, `M_DressSmoke` (translucent rising smoke), `M_DressCrow` (wing flap and flock orbit in the",
              "  vertex shader) and their instances, written by `Tools/ImportWorldDressing.py`.",
              "* `/Game/Free/Materials/M_FreePBR`, `M_FreePBRMasked`, `M_FreePBRTranslucent`, `T_FreeLinearWhite`: original",
              "  glTF-compatible PBR masters with instanced/Nanite usage, written by `Tools/FixGltfMaterials.py`. Every Poly Haven",
              "  material instance in `/Game/Free`, `/Game/Environment/Town/Props` and `/Game/Arenas/Props` is parented to them",
              "  (the engine glTF masters lack those usage flags, so uncooked -game runs drew them as the default grey material).",
              "  Their CC0 textures and credits are unchanged.", ""]
    PROVENANCE.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    if "--provenance-only" in sys.argv:
        write_provenance()
        return 0
    records = []
    for asset_id, purpose in MODELS.items():
        files = get_json(f"{PH_API}/files/{asset_id}")["gltf"]["1k"]["gltf"]
        folder = CACHE / "Models" / asset_id
        entry = fetch(files["url"], folder / Path(files["url"]).name, files.get("md5"))
        parts = [entry] + [fetch(v["url"], folder / rel, v.get("md5")) for rel, v in files["include"].items()]
        info = get_json(f"{PH_API}/info/{asset_id}")
        records.append({"kind": "model", "source": "Poly Haven", "id": asset_id, "name": info.get("name", asset_id),
                        "authors": ", ".join(info.get("authors", {}).keys()), "resolution": "1k", "purpose": purpose,
                        "page": f"https://polyhaven.com/a/{asset_id}", "license": PH_LICENSE, "entry": entry["file"], "files": parts})
        print(f"model {asset_id}: {sum(p['bytes'] for p in parts) / 1e6:.1f} MB")
    for asset_id, (res, purpose) in AMBIENTCG.items():
        url = f"https://ambientcg.com/get?file={asset_id}_{res}.zip"
        archive = fetch(url, CACHE / "AmbientCG" / f"{asset_id}_{res}.zip", None)
        out = CACHE / "AmbientCG" / asset_id
        out.mkdir(parents=True, exist_ok=True)
        extracted = []
        with zipfile.ZipFile(ROOT / archive["file"]) as z:
            for member in z.namelist():
                name = Path(member).name
                if Path(name).suffix.lower() in (".jpg", ".png") and "/" not in member.strip("/") and ".." not in member:
                    (out / name).write_bytes(z.read(member))
                    extracted.append(name)
        records.append({"kind": "texture", "source": "ambientCG", "id": asset_id, "name": asset_id, "authors": "Lennart Demes (ambientCG)",
                        "resolution": res, "purpose": purpose, "page": f"https://ambientcg.com/a/{asset_id}", "license": ACG_LICENSE,
                        "files": [archive], "extracted": sorted(extracted)})
        print(f"texture {asset_id}: {', '.join(sorted(extracted))}")
    ART.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps({"sources": ["Poly Haven public API", "ambientCG"], "license": "CC0 1.0 Universal",
                                    "fetched": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "assets": records}, indent=2) + "\n",
                        encoding="utf-8")
    write_provenance()
    print(f"CIRE_DRESSING_FETCH_PASS assets={len(records)} manifest={MANIFEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
