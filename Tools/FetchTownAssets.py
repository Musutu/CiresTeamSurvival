"""Download the CC0 source assets used by the medieval town layout.

Only plain HTTPS downloads from Poly Haven (https://polyhaven.com, CC0 1.0) are
used. Nothing downloaded is executed. Raw files land in Saved/TownSources (not
committed; re-downloadable). A committed manifest with every URL, size and MD5
is written to Art/Environment/Town/SourceManifest.json, which also feeds
Art/Environment/Town/PROVENANCE.md.

Usage:  python Tools/FetchTownAssets.py            (skips files already present
                                                     with a matching MD5)
"""
from __future__ import annotations

import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "Saved/TownSources"
MANIFEST = ROOT / "Art/Environment/Town/SourceManifest.json"
API = "https://api.polyhaven.com"
LICENSE = "CC0 1.0 Universal (Poly Haven asset license: https://polyhaven.com/license)"
AGENT = {"User-Agent": "CiresTeamSurvival-TownFetch/1.0 (asset import; contact via repo)"}

# id -> (resolution, purpose)
MODELS = {
    "Barrel_01": ("1k", "market / street barrels"),
    "wine_barrel_01": ("1k", "tavern and cellar barrels"),
    "wooden_barrels_01": ("1k", "stacked barrel cluster"),
    "wooden_crate_01": ("1k", "market crates"),
    "wooden_crate_02": ("1k", "market crates, variant"),
    "wooden_bucket_01": ("1k", "well and yard bucket"),
    "wicker_basket_01": ("1k", "market produce basket"),
    "wicker_basket_02": ("1k", "market produce basket, variant"),
    "Lantern_01": ("1k", "hanging / post lantern head"),
    "wooden_lantern_01": ("1k", "stall and doorway lantern"),
    "large_castle_door": ("2k", "castle gate and house doors"),
    "modular_fort_01": ("2k", "stone fortification kit: town wall, castle curtain, towers"),
    "gothic_statue": ("1k", "castle / square statuary"),
    "stone_fire_pit": ("1k", "watch fires and braziers"),
    "WoodenTable_01": ("1k", "market stall counters"),
    "wooden_stool_02": ("1k", "stall and tavern seating"),
    "dead_tree_trunk_02": ("1k", "dead trees outside the walls"),
    "tree_stump_01": ("1k", "stumps, field clutter"),
    "rock_moss_set_01": ("1k", "rocks outside the walls"),
    "rock_moss_set_02": ("1k", "rocks outside the walls, variant"),
    "boulder_01": ("1k", "large boulder"),
    "shrub_02": ("1k", "garden shrubs"),
    "shrub_04": ("1k", "garden shrubs, variant"),
    "fern_02": ("1k", "alley greenery"),
    "kite_shield": ("1k", "wall heraldry"),
    "wooden_axe": ("1k", "woodpile clutter"),
    "jug_01": ("1k", "stall wares"),
    "ceramic_pot": ("1k", "stall wares"),
    "food_apple_01": ("1k", "produce"),
    "wooden_ladder": ("1k", "yard clutter"),
    "island_tree_02": ("1k", "gnarled courtyard tree (Nanite)"),
}

# id -> purpose ; 2k JPG diffuse / GL normal / ARM (AO, roughness, metal)
TEXTURES = {
    "cobblestone_floor_08": "march road cobbles",
    "cobblestone_large_01": "market and square paving",
    "monastery_stone_floor": "castle bailey flagstones",
    "brown_mud_leaves_01": "town ground between streets",
    "rocky_trail": "fields outside the walls",
    "white_rough_plaster": "house plaster infill",
    "rough_wood": "timber framing / beams",
    "weathered_planks": "planks, shutters, stalls",
    "roof_slates_02": "slate roofs",
    "clay_roof_tiles_02": "clay tile roofs",
    "thatch_roof_angled": "thatched roofs",
    "stone_wall_04": "castle masonry (dressed grey blocks)",
    "castle_wall_slates": "town wall, plinth and trim rubble stone",
    "rock_face_03": "cliff face of the Sundering Wall between the realms",
    "rust_coarse_01": "wrought iron",
    "fabric_pattern_07": "market awning canvas (red check)",
    "fabric_pattern_05": "market awning canvas (alt)",
}
COLOUR_VARIANT = {"fabric_pattern_07": "col_1", "fabric_pattern_05": "col_01"}
TEXTURE_MAPS = {"Diffuse": "diff", "nor_gl": "nor_gl", "arm": "arm"}
HDRIS = {"belfast_sunset_puresky": ("4k", "dusk sky dome and sky light")}


def get_json(url: str):
    with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=60) as r:
        return json.load(r)


def md5_of(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(url: str, target: Path, md5: str | None) -> dict:
    target.parent.mkdir(parents=True, exist_ok=True)
    if not (target.exists() and md5 and md5_of(target) == md5):
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
        if md5 and md5_of(target) != md5:
            raise RuntimeError(f"MD5 mismatch for {url}")
    return {"url": url, "file": str(target.relative_to(ROOT)).replace("\\", "/"), "bytes": target.stat().st_size, "md5": md5}


def author(asset_id: str) -> str:
    info = get_json(f"{API}/info/{asset_id}")
    return ", ".join(info.get("authors", {}).keys()), info.get("name", asset_id)


def main() -> int:
    records = []
    for asset_id, (res, purpose) in MODELS.items():
        files = get_json(f"{API}/files/{asset_id}")["gltf"][res]["gltf"]
        folder = CACHE / "Models" / asset_id
        entry = fetch(files["url"], folder / Path(files["url"]).name, files.get("md5"))
        parts = [entry] + [fetch(v["url"], folder / rel, v.get("md5")) for rel, v in files["include"].items()]
        by, name = author(asset_id)
        records.append({"kind": "model", "id": asset_id, "name": name, "authors": by, "resolution": res, "purpose": purpose,
                        "page": f"https://polyhaven.com/a/{asset_id}", "license": LICENSE, "entry": entry["file"], "files": parts})
        print(f"model {asset_id}: {sum(p['bytes'] for p in parts)/1e6:.1f} MB")
    for asset_id, purpose in TEXTURES.items():
        files = get_json(f"{API}/files/{asset_id}")
        parts = []
        for key, suffix in TEXTURE_MAPS.items():
            if key == "Diffuse" and key not in files:  # fabric assets ship colour variants instead
                key = COLOUR_VARIANT.get(asset_id) or sorted(k for k in files if k.startswith("col"))[0]
            src = files[key]["2k"]["jpg"]
            parts.append(fetch(src["url"], CACHE / "Textures" / asset_id / f"{asset_id}_{suffix}_2k.jpg", src.get("md5")))
        by, name = author(asset_id)
        records.append({"kind": "texture", "id": asset_id, "name": name, "authors": by, "resolution": "2k", "purpose": purpose,
                        "page": f"https://polyhaven.com/a/{asset_id}", "license": LICENSE, "files": parts})
        print(f"texture {asset_id}")
    for asset_id, (res, purpose) in HDRIS.items():
        src = get_json(f"{API}/files/{asset_id}")["hdri"][res]["hdr"]
        part = fetch(src["url"], CACHE / "HDRI" / f"{asset_id}_{res}.hdr", src.get("md5"))
        by, name = author(asset_id)
        records.append({"kind": "hdri", "id": asset_id, "name": name, "authors": by, "resolution": res, "purpose": purpose,
                        "page": f"https://polyhaven.com/a/{asset_id}", "license": LICENSE, "files": [part]})
        print(f"hdri {asset_id}")
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps({"source": "Poly Haven public API (api.polyhaven.com)", "license": LICENSE,
                                    "fetched": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "assets": records}, indent=2) + "\n",
                        encoding="utf-8")
    print(f"CIRE_TOWN_FETCH_PASS assets={len(records)} manifest={MANIFEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
