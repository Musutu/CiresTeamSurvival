"""champion-hq: skin / emissive masks for the HQ bodies (M_CireHero_PBR "MaskTex": R = skin, G = emissive).

Plain Python orchestrator (Pillow; --pylib DIR if Pillow is not installed):
  1. an editor commandlet exports each integrated body's base colour + metallic textures to
     Saved/ChampionHQ/Textures/<key>/ (this file, run inside Unreal with CIRE_HQ_MASK_STAGE=export);
  2. Pillow derives the mask at 1024 px:
       skin     = warm, moderately saturated, non-metallic texels (hue 0-0.11, sat 0.15-0.62, value 0.25-0.97)
       emissive = texels near the champion's EmissiveColor hue that are saturated and bright ("look" in
                  Art/ChampionHQ/TripoChampionHQ.json; no EmissiveColor = no emissive mask)
     both blurred slightly so the masks have soft edges;
  3. a second commandlet imports <export>_mask into the body's Textures folder (CIRE_HQ_MASK_STAGE=import);
  Tools/RunChampionHQIntegration.py then binds it (IntegrateChampionHQ picks up *_mask).
Usage: python Tools/ChampionHQMaskTextures.py [--only ranger,scholar] [--pylib Saved/pylib]
"""
from __future__ import annotations

import colorsys
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LOG = ROOT / "Art" / "ChampionHQ" / "TripoChampionHQ.json"
WORK = ROOT / "Saved" / "ChampionHQ" / "Textures"
DEST = "/Game/Tripo/ChampionsHQ"


def in_unreal() -> bool:
    try:
        import unreal  # noqa: F401
        return True
    except ImportError:
        return False


def unreal_stage(stage: str) -> None:
    import unreal
    exports = json.loads(LOG.read_text(encoding="utf-8"))["exports"]
    only = [s for s in os.environ.get("CIRE_CHAMPION_HQ_ONLY", "").split(",") if s]
    lib = unreal.EditorAssetLibrary
    for key, entry in exports.items():
        if only and key not in only:
            continue
        folder = "%s/%s/Textures" % (DEST, entry["folder"])
        if not lib.does_directory_exist(folder):
            continue
        out = WORK / key
        out.mkdir(parents=True, exist_ok=True)
        if stage == "export":
            for path in lib.list_assets(folder, recursive=False, include_folder=False):
                tex = unreal.load_asset(str(path).split(".")[0])
                if not isinstance(tex, unreal.Texture2D):
                    continue
                low = tex.get_name().lower()
                kind = "metallic" if low.endswith("_metallic") else "basecolor" if (low.endswith("_basecolor") or (tex.get_editor_property("srgb") and "_mask" not in low)) else None
                if not kind:
                    continue
                task = unreal.AssetExportTask()
                task.set_editor_property("object", tex)
                task.set_editor_property("filename", str(out / (kind + ".png")))
                task.set_editor_property("automated", True)
                task.set_editor_property("replace_identical", True)
                task.set_editor_property("prompt", False)
                task.set_editor_property("exporter", unreal.TextureExporterPNG())
                ok = unreal.Exporter.run_asset_export_task(task)
                unreal.log("CIRE_HQ_MASK export %s %s %s" % (key, kind, ok))
        elif stage == "import":
            mask = out / "mask.png"
            if not mask.exists():
                continue
            name = entry["export"] + "_mask"
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str(mask))
            task.set_editor_property("destination_path", folder)
            task.set_editor_property("destination_name", name)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("automated", True)
            task.set_editor_property("save", False)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
            tex = unreal.load_asset(folder + "/" + name)
            tex.set_editor_property("srgb", False)
            tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
            lib.save_loaded_asset(tex, False)
            unreal.log("CIRE_HQ_MASK import %s" % name)
    unreal.log("CIRE_HQ_MASK_STAGE_DONE " + stage)


def build_masks(only: list[str]) -> None:
    from PIL import Image, ImageFilter
    exports = json.loads(LOG.read_text(encoding="utf-8"))["exports"]
    for key, entry in exports.items():
        if only and key not in only:
            continue
        src = WORK / key / "basecolor.png"
        if not src.exists():
            continue
        base = Image.open(src).convert("RGB").resize((1024, 1024), Image.LANCZOS)
        metal_path = WORK / key / "metallic.png"
        metal = Image.open(metal_path).convert("L").resize((1024, 1024)) if metal_path.exists() else None
        emis = entry.get("look", {}).get("EmissiveColor")
        eh = colorsys.rgb_to_hsv(*emis[:3])[0] if emis else None
        px = base.load(); mp = metal.load() if metal else None
        mask = Image.new("RGB", base.size, (0, 0, 0)); out = mask.load()
        skin_n = emis_n = 0
        for y in range(base.size[1]):
            for x in range(base.size[0]):
                r, g, b = px[x, y]
                h, s, v = colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)
                metallic = (mp[x, y] / 255) if mp else 0
                skin = 255 if (h <= .11 or h >= .97) and .15 <= s <= .62 and .25 <= v <= .97 and r > g > b and metallic < .35 else 0
                glow = 0
                if eh is not None and s > .45 and v > .55:
                    d = min(abs(h - eh), 1 - abs(h - eh))
                    if d < .06:
                        glow = int(255 * min(1, (v - .55) / .35) * (1 - d / .06))
                out[x, y] = (skin, glow, 0)
                skin_n += skin > 0; emis_n += glow > 0
        mask = mask.filter(ImageFilter.GaussianBlur(1.2))
        mask.save(WORK / key / "mask.png")
        print("mask", key, "skin=%.1f%%" % (100 * skin_n / 1024 / 1024), "emissive=%.1f%%" % (100 * emis_n / 1024 / 1024))


def run_editor(stage: str, only: str) -> None:
    env = {**os.environ, "UE_SKIP_UBT_SDK_SETUP": "1", "CIRE_HQ_MASK_STAGE": stage, "CIRE_CHAMPION_HQ_ONLY": only}
    cmd = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"), "-run=pythonscript",
           "-script=" + str(Path(__file__).resolve()), "-unattended", "-nullrhi", "-nosplash", "-NoLiveCoding",
           "-abslog=" + str(ROOT / "Saved" / "Logs" / ("ChampionHQMask-%s.log" % stage))]
    subprocess.run(cmd, env=env, timeout=1800, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__" or in_unreal():
    if in_unreal():
        unreal_stage(os.environ.get("CIRE_HQ_MASK_STAGE", "export"))
    else:
        import argparse
        parser = argparse.ArgumentParser()
        parser.add_argument("--only", default="")
        parser.add_argument("--pylib")
        args = parser.parse_args()
        if args.pylib:
            sys.path.insert(0, args.pylib)
        run_editor("export", args.only)
        build_masks([s for s in args.only.split(",") if s])
        run_editor("import", args.only)
        print("done; rerun Tools/RunChampionHQIntegration.py to bind the masks")
