"""Unreal editor script: import draft portrait / ability icon PNGs as UI textures.

CIRE_UI_TEXTURE_DEST and CIRE_UI_TEXTURE_PREFIX override the destination
(default /Game/UI/Draft/Portraits, T_Portrait_); Tools/RunAbilityIcons.py uses
/Game/UI/Abilities with T_Ability_.

Run inside UnrealEditor-Cmd with -run=pythonscript (Tools/RunDraftPortraits.py does
this). Reads CIRE_DRAFT_PORTRAIT_DIR, imports every <profile_id>.png as
/Game/UI/Draft/Portraits/T_Portrait_<profile_id> (UI texture group, box-filtered mips for crisp downscaling, sRGB),
saves the packages and writes import-report.json next to the PNGs.
"""
import json
import os
import re

import unreal

SOURCE = os.environ.get("CIRE_DRAFT_PORTRAIT_DIR", "")
DESTINATION = os.environ.get("CIRE_UI_TEXTURE_DEST", "/Game/UI/Draft/Portraits")
PREFIX = os.environ.get("CIRE_UI_TEXTURE_PREFIX", "T_Portrait_")


def main():
    report = {"source": SOURCE, "destination": DESTINATION, "imported": [], "errors": []}
    if not SOURCE or not os.path.isdir(SOURCE):
        report["errors"].append("CIRE_DRAFT_PORTRAIT_DIR is not a directory")
    else:
        tasks = []
        for name in sorted(os.listdir(SOURCE)):
            match = re.fullmatch(r"([a-z][a-z0-9_]{0,63})\.png", name)
            if not match:
                continue
            task = unreal.AssetImportTask()
            task.filename = os.path.join(SOURCE, name)
            task.destination_path = DESTINATION
            task.destination_name = PREFIX + match.group(1)
            task.replace_existing = True
            task.automated = True
            task.save = False
            tasks.append((match.group(1), task))
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t for _, t in tasks])
        for profile, task in tasks:
            path = f"{DESTINATION}/{PREFIX}{profile}"
            texture = unreal.EditorAssetLibrary.load_asset(path)
            if not isinstance(texture, unreal.Texture2D):
                report["errors"].append("Import failed: " + profile)
                continue
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
            texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
            texture.set_editor_property("srgb", True)
            texture.set_editor_property("never_stream", True)
            if not unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False):
                report["errors"].append("Save failed: " + profile)
                continue
            report["imported"].append({"profile": profile, "asset": path,
                                       "width": texture.blueprint_get_size_x(), "height": texture.blueprint_get_size_y()})
    if SOURCE and os.path.isdir(SOURCE):
        with open(os.path.join(SOURCE, "import-report.json"), "w", encoding="utf-8") as stream:
            json.dump(report, stream, indent=2)
    unreal.log("CIRE_DRAFT_PORTRAIT_IMPORT_{} imported={} errors={}".format(
        "PASS" if not report["errors"] and report["imported"] else "FAIL", len(report["imported"]), len(report["errors"])))


main()
