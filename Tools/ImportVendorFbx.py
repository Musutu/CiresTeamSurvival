"""UE 5.8 (editor python, vendors): import Tripo FBX exports (Studio 'Export' > FBX, Blender preset) without the DCC Bridge.

The Bridge 'Send To Unreal' is unreliable when several worktree editors run; the Studio export zip is equivalent:
<name>.zip -> tripo_convert_<id>.fbx + tripo_convert_<id>.fbm/ (colour, normal, roughness, metallic).
Unzip it to Saved/TripoExports/<Folder>/ and list the jobs in CIRE_VENDOR_FBX as
  "<fbx path>|<destination /Game path>|<asset name>|<skeletal 0/1>;..."
Skeletal jobs import the mesh, its skeleton and every animation take (the rig's UE5-Mannequin preset clips)
into <dest> and <dest>/Animations named <asset>_<take>; static jobs (the pre-rig 4-view check) import one mesh.
Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/ImportVendorFbx.py -unattended
Marker: CIRE_VENDOR_FBX_IMPORT PASS / FAIL.
"""
import os
import re
import traceback
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def tidy_clips(dest, name):
    """Clips land as <name>_<name><take>_001 next to the mesh: move them to Animations/<name>_<take>."""
    clips = []
    for path in [str(p).split(".")[0] for p in EAL.list_assets(dest, recursive=True, include_folder=False)]:
        asset = EAL.load_asset(path)
        if not isinstance(asset, unreal.AnimSequence):
            continue
        take = asset.get_name()
        while take.startswith(name):
            take = take[len(name):].lstrip("_")
        take = re.sub(r"(_\d{3})+$", "", take.replace("Armature|", "").replace("|", "_"))
        new_name = "%s_%s" % (name, take)
        if new_name != asset.get_name() or not path.startswith(dest + "/Animations/"):
            AT.rename_assets([unreal.AssetRenameData(asset, dest + "/Animations", new_name)])
        clips.append(new_name)
    return clips


def tune_textures(dest):
    """The FBX importer brings the Tripo normal map in as an sRGB colour texture (hard dark patches on the faces):
    normal -> TC_Normalmap linear, roughness / metallic -> linear masks, colour stays sRGB; character LOD group, 4K cap,
    no mips (see below)."""
    for path in [str(p).split(".")[0] for p in EAL.list_assets(dest, recursive=True, include_folder=False)]:
        tex = EAL.load_asset(path)
        if not isinstance(tex, unreal.Texture2D):
            continue
        low = tex.get_name().lower()
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
        tex.set_editor_property("max_texture_size", 4096)
        # Tripo atlases are hundreds of tiny UV islands with no padding: every mip below the top bleeds the dark gutter
        # into the islands (blotchy faces). Keep the full-resolution top mip only, always resident.
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("never_stream", True)
        if "normal" in low:
            tex.set_editor_property("flip_green_channel", False)
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            tex.set_editor_property("srgb", False)
        elif "roughness" in low or "metallic" in low:
            tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            tex.set_editor_property("srgb", False)
        else:
            tex.set_editor_property("srgb", True)
        EAL.save_loaded_asset(tex, only_if_is_dirty=False)
    # The Tripo normal map does not match the FBX tangent frame (sharp dark patches along the UV islands, either
    # green convention): the legacy Phong instance renders it at weight 0 (smooth, correct shading).
    for path in [str(p).split(".")[0] for p in EAL.list_assets(dest, recursive=True, include_folder=False)]:
        mi = EAL.load_asset(path)
        if isinstance(mi, unreal.MaterialInstanceConstant):
            unreal.MaterialEditingLibrary.set_material_instance_scalar_parameter_value(mi, "NormalMapWeight", 0.0)
            unreal.MaterialEditingLibrary.update_material_instance(mi)
            EAL.save_loaded_asset(mi, only_if_is_dirty=False)


def import_job(fbx, dest, name, skeletal):
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_textures", True)
    options.set_editor_property("import_materials", True)
    options.set_editor_property("import_as_skeletal", skeletal)
    options.set_editor_property("import_animations", skeletal)
    options.set_editor_property("automated_import_should_detect_type", False)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH)
    if skeletal:
        sk = options.get_editor_property("skeletal_mesh_import_data")
        sk.set_editor_property("import_morph_targets", False)
        sk.set_editor_property("convert_scene", True)
        anim = options.get_editor_property("anim_sequence_import_data")
        anim.set_editor_property("import_bone_tracks", True)
        anim.set_editor_property("animation_length", unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    else:
        st = options.get_editor_property("static_mesh_import_data")
        st.set_editor_property("combine_meshes", True)
        st.set_editor_property("convert_scene", True)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx)
    task.set_editor_property("destination_path", dest)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("options", options)
    AT.import_asset_tasks([task])
    imported = list(task.get_editor_property("imported_object_paths") or [])
    clips = tidy_clips(dest, name)
    tune_textures(dest)
    for path in [str(p).split(".")[0] for p in EAL.list_assets(dest, recursive=True, include_folder=False)]:
        a = EAL.load_asset(path)
        if isinstance(a, unreal.Texture2D):
            a.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_CHARACTER)
            a.set_editor_property("max_texture_size", 4096)
        EAL.save_asset(path, only_if_is_dirty=False)
    unreal.log("CIRE_VENDOR_FBX_JOB %s imported=%d clips=%s" % (name, len(imported), ",".join(sorted(clips))))
    return bool(imported)


ok = True
try:
    for job in [j for j in os.environ.get("CIRE_VENDOR_FBX_TIDY", "").split(";") if j.strip()]:
        dest, name = job.split("|")
        tune_textures(dest)
        unreal.log("CIRE_VENDOR_FBX_TIDY %s clips=%s" % (name, ",".join(sorted(tidy_clips(dest, name)))))
        for path in [str(p).split(".")[0] for p in EAL.list_assets(dest, recursive=True, include_folder=False)]:
            EAL.save_asset(path, only_if_is_dirty=True)
    for job in [j for j in os.environ.get("CIRE_VENDOR_FBX", "").split(";") if j.strip()]:
        fbx, dest, name, skeletal = job.split("|")
        ok = import_job(fbx, dest, name, skeletal == "1") and ok
except Exception:
    ok = False
    unreal.log_error(traceback.format_exc())
unreal.log("CIRE_VENDOR_FBX_IMPORT %s" % ("PASS" if ok else "FAIL"))
