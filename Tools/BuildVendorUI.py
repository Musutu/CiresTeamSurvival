"""UE 5.8 (editor python, vendors): import the merchant sign boards and emblems (Tools/ProcessVendorArt.py output).

Content/UI/Vendors/src/T_Vendor*.png -> /Game/UI/Vendors/<name>: UI textures with mips (they are drawn both on the
HUD at small sizes and on the in-world sign card), sRGB, alpha kept.
Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildVendorUI.py -unattended
Marker: CIRE_VENDOR_UI PASS / FAIL.
"""
from pathlib import Path
import unreal

SRC = Path(unreal.Paths.project_dir()).resolve() / "Content" / "UI" / "Vendors" / "src"
DEST = "/Game/UI/Vendors"
EAL = unreal.EditorAssetLibrary
tasks = []
for png in sorted(SRC.glob("T_Vendor*.png")):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", str(png))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", png.stem)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append(t)
unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
ok = bool(tasks)
for png in SRC.glob("T_Vendor*.png"):
    tex = EAL.load_asset(DEST + "/" + png.stem)
    if not tex:
        ok = False
        continue
    tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property("srgb", True)
    tex.set_editor_property("never_stream", True)
    EAL.save_loaded_asset(tex, only_if_is_dirty=False)
unreal.log("CIRE_VENDOR_UI %s textures=%d" % ("PASS" if ok else "FAIL", len(tasks)))
