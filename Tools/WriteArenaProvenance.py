"""Write Art/Arenas/PROVENANCE.md: source, author and licence of every asset the PvP arenas use.

Inputs (all committed): Art/Arenas/SourceManifest.json (Poly Haven CC0 downloads),
Art/Arenas/Meshes/ArenaMeshes.json (original kit), Art/Audio/AudioSources.json (arena_* keys),
Content/Data/Arenas.json (which slots each arena uses, including reused town assets and the
Fab pack candidates that are still waiting for "Add to Project").

Usage: python Tools/WriteArenaProvenance.py
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Art/Arenas/PROVENANCE.md"

FAB_PACKS = [
    # (name, author, listing, license, target arena, slot candidates waiting for it)
    ("Moab Desert Collections", "Quixel Megascans", "https://www.fab.com/library/assets/2c0a2fe4-1ab9-4c49-8055-9aa053c46457",
     "Fab Standard License (UE-only Megascans collection, in Eric's library)", "Redrock Canyon", "/Game/Fab/Arenas/Moab/..."),
    ("Iceland Collections", "Quixel Megascans", "https://www.fab.com/library/assets/765f63b0-1994-493e-af8d-33ad1c2dc10c",
     "Fab Standard License (UE-only Megascans collection, in Eric's library)", "The Black Shore", "/Game/Fab/Arenas/Iceland/..."),
    ("European Hornbeam", "Quixel Megascans", "https://www.fab.com/listings/c6f917b6-ffcb-4b86-9d9f-5274ba7f6a8e",
     "Fab Standard License (free, UE format only, in Eric's library)", "Hornbeam Glade", "/Game/Fab/Arenas/Hornbeam/..."),
    ("Underwater World / 70 Assets", "PackDev", "https://www.fab.com/listings/e2cdf1ba-c517-4b8e-b5ce-0828bbc44eab",
     "Fab Standard License (owned by Eric, UE format)", "The Drowned Sanctum", "/Game/Fab/Arenas/Underwater/..."),
    ("Big Star Station", "Akairo", "https://www.fab.com/listings/9cff72cf-bd72-4f4b-bab0-ec556a25e37d",
     "Fab Standard License (owned by Eric, UE format)", "Star Station Hangar", "/Game/Fab/Arenas/StarStation/..."),
]


def main() -> int:
    manifest = json.loads((ROOT / "Art/Arenas/SourceManifest.json").read_text(encoding="utf-8"))
    kit = json.loads((ROOT / "Art/Arenas/Meshes/ArenaMeshes.json").read_text(encoding="utf-8"))
    arenas = json.loads((ROOT / "Content/Data/Arenas.json").read_text(encoding="utf-8"))
    audio_path = ROOT / "Art/Audio/AudioSources.json"
    audio = json.loads(audio_path.read_text(encoding="utf-8"))["sources"] if audio_path.exists() else []
    used_paths = {c for s in arenas["slots"].values() for cand in s["candidates"] for c in cand.split("|")}
    used_paths |= {s["fallbackMaterial"] for s in arenas["slots"].values()}
    lines = [
        "# Arena asset provenance", "",
        "Every asset used by the six themed PvP arenas (`Content/Data/Arenas.json`, `Docs/Arenas.md`), with its source,",
        "author and licence. Third-party content is **CC0 1.0** (Poly Haven, Freesound) or **CC BY 4.0** (Kevin MacLeod",
        "music, credited in game). Nothing downloaded was executed. Raw downloads live in `Saved/ArenaSources` and",
        "`Art/Downloads/Audio` (not committed; the fetch scripts restore them and verify hashes).",
        "", "Regenerate with `Tools/WriteArenaProvenance.py`.", "",
        "## Poly Haven (CC0 1.0) - `Tools/FetchArenaAssets.py`", "",
        f"Fetched {manifest['fetched']} from the public API. Licence: {manifest['license']}.", "",
        "| Kind | Asset | Authors | Res | Arena | Used for | Source |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for a in sorted(manifest["assets"], key=lambda r: (r["kind"], r["id"])):
        lines.append(f"| {a['kind']} | {a['name']} (`{a['id']}`) | {a['authors']} | {a['resolution']} | {a['arena']} | {a['purpose']} | {a['page']} |")
    lines += ["", "## Original meshes (this project) - `Tools/BuildArenaMeshes.py`", "",
              "Authored procedurally for Cire's Team Survival; no third-party geometry. Imported to `/Game/Arenas/Meshes`",
              "and textured with the CC0 materials above through the arena master materials in `/Game/Arenas/Materials`",
              "(also original: `M_ArenaSurface`, `M_ArenaWorld`, `M_ArenaBlend`, `M_ArenaFlat`, `M_ArenaWheat`, `M_ArenaKelp`,",
              "`M_ArenaFoliage`, `M_ArenaMotes`, `M_ArenaShaft`, `M_ArenaCaustics`, `M_ArenaWater`, `M_ArenaHazard`, and the sky",
              "masters `M_ArenaSky` / `M_ArenaSkyProcedural`).", "",
              "| Mesh | Triangles | Size (cm) | Material slots |", "| --- | --- | --- | --- |"]
    for m in kit["meshes"]:
        lines.append(f"| `{m['name']}` | {m['triangles']} | {' x '.join(str(round(v)) for v in m['size'])} | {', '.join(m['materials'])} |")
    town = sorted(p for p in used_paths if p.startswith("/Game/Environment/Town/"))
    lines += ["", "## Reused town assets (already in the project)", "",
              "Covered by `Art/Environment/Town/PROVENANCE.md` (Poly Haven CC0 props and textures, original town meshes):", ""]
    lines += [f"- `{p}`" for p in town]
    lines += ["", "## Audio - Freesound CC0 (licence verified by `Tools/FetchAudioSources.py`)", "",
              "| Key | Title | Author | Page | Used for |", "| --- | --- | --- | --- | --- |"]
    for r in audio:
        if r.get("key", "").startswith("arena_"):
            lines.append(f"| {r['key']} | {r['title'].replace('|', '/')} | {r['author']} | {r['page']} | {r.get('use', '')} |")
    lines += ["", "Arena music overrides reuse the existing Kevin MacLeod (incompetech.com) tracks, CC BY 4.0, credited in",
              "Options > Audio and `Art/Audio/PROVENANCE.md`.", "",
              "## Fab packs from Eric's library (not yet in the project)", "",
              "These are Unreal-format only, so they cannot be website-downloaded; they need **Add to Project** in the Epic",
              "Games Launcher (see `Docs/FAB-ADD-TO-PROJECT.md`, section Arenas). The arena slots already list their expected",
              "paths first, so the arenas switch to them automatically once added; until then the CC0/original fallbacks above are used.", "",
              "| Pack | Author | Licence | Arena | Listing |", "| --- | --- | --- | --- | --- |"]
    for name, author, url, lic, arena, _ in FAB_PACKS:
        lines.append(f"| {name} | {author} | {lic} | {arena} | {url} |")
    lines.append("")
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
