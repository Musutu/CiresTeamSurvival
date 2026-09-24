"""Download the CC0 source assets used by the themed PvP arenas (Docs/Arenas.md).

Only plain HTTPS downloads from Poly Haven (https://polyhaven.com, CC0 1.0) are
used; nothing downloaded is executed. Raw files land in Saved/ArenaSources (not
committed; re-downloadable, MD5-verified). The committed manifest
Art/Arenas/SourceManifest.json records every URL, size, hash, author and the arena
that uses it, and feeds Art/Arenas/PROVENANCE.md (written by
Tools/WriteArenaProvenance.py together with the Fab and original-content records).

The named Fab packs Eric owns (Iceland / Moab Desert Collections, European
Hornbeam, Underwater World, Big Star Station) are Unreal-format only, so they are
listed in Docs/FAB-ADD-TO-PROJECT.md and picked up by Arenas.json slot candidates
once added; these CC0 sources make every arena complete without them.

Usage:  python Tools/FetchArenaAssets.py
"""
from __future__ import annotations

import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CACHE = ROOT / "Saved/ArenaSources"
MANIFEST = ROOT / "Art/Arenas/SourceManifest.json"
API = "https://api.polyhaven.com"
LICENSE = "CC0 1.0 Universal (Poly Haven asset license: https://polyhaven.com/license)"
AGENT = {"User-Agent": "CiresTeamSurvival-ArenaFetch/1.0 (asset import; contact via repo)"}

# id -> (resolution, arena, purpose)
MODELS = {
    "rusted_spade_01": ("1k", "fields", "farm tool leaning on a bale"),
    "coast_rocks_05": ("1k", "iceland", "shoreline rock cluster"),
    "coast_land_rocks_02": ("1k", "iceland", "low lava outcrops"),
    "sand_rocks_small_01": ("1k", "iceland, underwater", "pebble scatter"),
    "namaqualand_boulder_03": ("1k", "moab", "fallen sandstone blocks"),
    "namaqualand_boulder_05": ("1k", "moab", "sandstone boulder"),
    "wild_rooibos_bush": ("1k", "moab", "desert scrub"),
    "dead_tree_trunk": ("1k", "moab, hornbeam", "fallen trunks"),
    "root_cluster_01": ("1k", "hornbeam", "root bank at the glade edge"),
    "tree_stump_02": ("1k", "hornbeam", "cut stumps"),
    "shrub_03": ("1k", "hornbeam", "saplings"),
    "grass_medium_02": ("1k", "hornbeam, fields", "grass tufts"),
    "treasure_chest": ("1k", "underwater", "sunken chest"),
    "cannon_01": ("1k", "underwater", "sunken cannon"),
    "marble_bust_01": ("1k", "underwater", "drowned statuary"),
    "lion_head": ("1k", "underwater", "fountain lion head"),
    "bronze_whale_statue": ("1k", "underwater", "bronze whale on a plinth"),
    "lambis_shell": ("1k", "underwater", "conch shells on the seabed"),
    "ship_pinnace": ("1k", "underwater", "the wreck beyond the ruins"),
    "portable_generator": ("1k", "station", "deck generator"),
    "metal_tool_chest": ("1k", "station", "tool chests"),
    "propane_tank": ("1k", "station", "gas bottles"),
    "industrial_storage_cart": ("1k", "station", "cargo cart"),
    "overhead_crane": ("1k", "station", "gantry crane"),
    "metal_jerrycan": ("1k", "station", "fuel cans"),
}

# Trees also come as FBX: the glTF leaf primitive (about a million triangles of leaf cards) is dropped
# by the Interchange glTF importer, the legacy FBX importer keeps it. id -> (arena, purpose)
TREES_FBX = {
    "island_tree_01": ("fields, hornbeam", "broadleaf trees (FBX with leaf cards and alpha)"),
    "tree_small_02": ("fields, hornbeam", "young trees (FBX with leaf cards and alpha)"),
}

# id -> (arena, purpose) ; 2k JPG diffuse / GL normal / ARM (AO, roughness, metal)
TEXTURES = {
    "withered_grass": ("fields", "harvested stubble ground"),
    "farm_soil": ("fields", "cart tracks and tilled strips"),
    "stacked_stone_wall": ("fields", "dry-stone walls and the well"),
    "rock_ground": ("iceland", "black lava gravel"),
    "dark_rock_02": ("iceland", "basalt columns and lava rock"),
    "mossy_rock": ("iceland", "moss-covered lava"),
    "coast_sand_01": ("iceland", "black-sand beach (tinted)"),
    "red_laterite_soil_stones": ("moab", "canyon floor"),
    "sandstone_cracks": ("moab", "sandstone arches, hoodoos and walls (tinted)"),
    "red_sand": ("moab", "drift sand"),
    "forest_floor": ("hornbeam", "leaf-litter paths"),
    "bark_willow": ("hornbeam", "hornbeam trunks (tinted grey)"),
    "damp_sand": ("underwater", "seabed sand"),
    "coral_stone_wall": ("underwater", "ruin masonry"),
    "coral_fort_wall_01": ("underwater", "encrusted columns"),
    "coral_gravel": ("underwater", "rubble"),
    "metal_plate": ("station", "tread-plate deck"),
    "metal_plate_02": ("station", "bulkhead panels"),
    "blue_metal_plate": ("station", "container panels"),
    "hangar_concrete_floor": ("station", "hangar floor"),
    "painted_metal_shutter": ("station", "shutters and cladding"),
}
TEXTURE_MAPS = {"Diffuse": "diff", "nor_gl": "nor_gl", "arm": "arm"}
# id -> (resolution, arena, purpose)
HDRIS = {
    "plains_sunset": ("4k", "fields", "golden-hour sky over dry plains"),
    "kloofendal_overcast_puresky": ("4k", "iceland", "overcast North Atlantic sky"),
    "kloofendal_43d_clear_puresky": ("4k", "moab", "harsh clear desert sky"),
    "epping_forest_01": ("4k", "hornbeam", "broadleaf woodland surround"),
}


def get_json(url: str):
    for attempt in range(4):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=60) as r:
                return json.load(r)
        except Exception as error:
            if attempt == 3:
                raise
            print(f"retry {url}: {error}")
            time.sleep(2 + attempt * 3)


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
                with urllib.request.urlopen(urllib.request.Request(url, headers=AGENT), timeout=300) as r, tmp.open("wb") as out:
                    while chunk := r.read(1 << 20):
                        out.write(chunk)
                tmp.replace(target)
                break
            except Exception as error:
                if attempt == 3:
                    raise
                print(f"retry {url}: {error}")
                time.sleep(2 + attempt * 3)
        if md5 and md5_of(target) != md5:
            raise RuntimeError(f"MD5 mismatch for {url}")
    return {"url": url, "file": str(target.relative_to(ROOT)).replace("\\", "/"), "bytes": target.stat().st_size, "md5": md5}


def info(asset_id: str):
    data = get_json(f"{API}/info/{asset_id}")
    return ", ".join(data.get("authors", {}).keys()), data.get("name", asset_id)


def main() -> int:
    only = set(sys.argv[sys.argv.index("--only") + 1].split(",")) if "--only" in sys.argv else None
    records = []
    if MANIFEST.exists() and only:
        records = [r for r in json.loads(MANIFEST.read_text(encoding="utf-8"))["assets"] if r["id"] not in only]
    for asset_id, (res, arena, purpose) in MODELS.items():
        if only and asset_id not in only:
            continue
        files = get_json(f"{API}/files/{asset_id}")["gltf"][res]["gltf"]
        folder = CACHE / "Models" / asset_id
        entry = fetch(files["url"], folder / Path(files["url"]).name, files.get("md5"))
        parts = [entry] + [fetch(v["url"], folder / rel, v.get("md5")) for rel, v in files["include"].items()]
        by, name = info(asset_id)
        records.append({"kind": "model", "id": asset_id, "name": name, "authors": by, "resolution": res, "arena": arena,
                        "purpose": purpose, "page": f"https://polyhaven.com/a/{asset_id}", "license": LICENSE,
                        "entry": entry["file"], "files": parts})
        print(f"model {asset_id}: {sum(p['bytes'] for p in parts) / 1e6:.1f} MB")
    for asset_id, (arena, purpose) in TREES_FBX.items():
        if only and asset_id not in only:
            continue
        files = get_json(f"{API}/files/{asset_id}")["fbx"]["1k"]["fbx"]
        folder = CACHE / "TreesFBX" / asset_id
        entry = fetch(files["url"], folder / Path(files["url"]).name, files.get("md5"))
        parts = [entry] + [fetch(v["url"], folder / rel, v.get("md5")) for rel, v in files["include"].items()]
        by, name = info(asset_id)
        records.append({"kind": "model-fbx", "id": asset_id, "name": name, "authors": by, "resolution": "1k", "arena": arena,
                        "purpose": purpose, "page": f"https://polyhaven.com/a/{asset_id}", "license": LICENSE,
                        "entry": entry["file"], "files": parts})
        print(f"tree fbx {asset_id}: {sum(p['bytes'] for p in parts) / 1e6:.1f} MB")
    for asset_id, (arena, purpose) in TEXTURES.items():
        if only and asset_id not in only:
            continue
        files = get_json(f"{API}/files/{asset_id}")
        parts = []
        for key, suffix in TEXTURE_MAPS.items():
            if key not in files and key == "Diffuse":
                key = sorted(k for k in files if k.lower().startswith(("diff", "col")))[0]
            src = files[key]["2k"]["jpg"]
            parts.append(fetch(src["url"], CACHE / "Textures" / asset_id / f"{asset_id}_{suffix}_2k.jpg", src.get("md5")))
        by, name = info(asset_id)
        records.append({"kind": "texture", "id": asset_id, "name": name, "authors": by, "resolution": "2k", "arena": arena,
                        "purpose": purpose, "page": f"https://polyhaven.com/a/{asset_id}", "license": LICENSE, "files": parts})
        print(f"texture {asset_id}")
    for asset_id, (res, arena, purpose) in HDRIS.items():
        if only and asset_id not in only:
            continue
        src = get_json(f"{API}/files/{asset_id}")["hdri"][res]["hdr"]
        part = fetch(src["url"], CACHE / "HDRI" / f"{asset_id}_{res}.hdr", src.get("md5"))
        by, name = info(asset_id)
        records.append({"kind": "hdri", "id": asset_id, "name": name, "authors": by, "resolution": res, "arena": arena,
                        "purpose": purpose, "page": f"https://polyhaven.com/a/{asset_id}", "license": LICENSE, "files": [part]})
        print(f"hdri {asset_id}")
    MANIFEST.parent.mkdir(parents=True, exist_ok=True)
    MANIFEST.write_text(json.dumps({"source": "Poly Haven public API (api.polyhaven.com)", "license": LICENSE,
                                    "fetched": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "assets": records}, indent=2) + "\n",
                        encoding="utf-8")
    print(f"CIRE_ARENA_FETCH_PASS assets={len(records)} manifest={MANIFEST}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
