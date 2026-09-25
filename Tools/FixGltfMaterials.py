"""Give glTF-imported props project-owned PBR masters that render in uncooked -game runs (world-dressing).

Problem: the Interchange glTF importer parents every material instance to the engine plugin masters in
/InterchangeAssets/gltf/. Those masters lack the InstancedStaticMeshes and Nanite usage flags. The editor sets
missing usage flags on the fly, but an uncooked `-game` run (Play.cmd, every gallery and smoke run) cannot,
so it draws the Default Material: every Poly Haven prop in the town, arenas and dressing rendered flat grey.
Engine content must not be modified, so this tool builds project masters with the right usage flags and the same
parameter names (BaseColorTexture, NormalTexture, MetallicRoughnessTexture, BaseColorFactor, MetallicFactor,
RoughnessFactor, NormalScale, AlphaCutoff) and re-parents the instances. Texture assignments are kept.

  /Game/Free/Materials/M_FreePBR             opaque (Default_Opaque, ClearCoat)
  /Game/Free/Materials/M_FreePBRMasked       masked, two-sided (Default_Mask)
  /Game/Free/Materials/M_FreePBRTranslucent  translucent, two-sided (Default_Blend, Transmission: bottles, glass)

The same missing flag hid the Tripo town/landmark art (market stalls, shrine, houses, well, cart): their shared master
/Game/TripoModels/Materials/M_Tripo_PBR_Master lacked InstancedStaticMeshes (and Nanite for the shrine). `--usage-masters`
sets those usage flags on the listed project masters and saves them (no graph change).

Usage: python Tools/FixGltfMaterials.py [--folders /Game/Free,/Game/Environment/Town/Props,/Game/Arenas/Props]
       python Tools/FixGltfMaterials.py --usage-masters /Game/TripoModels/Materials/M_Tripo_PBR_Master
"""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PKG = "/Game/Free/Materials"
DEFAULT_FOLDERS = "/Game/Free,/Game/Environment/Town/Props,/Game/Arenas/Props"


def set_usage(u, paths):
    lib = u.EditorAssetLibrary
    for path in paths:
        mat = lib.load_asset(path)
        assert isinstance(mat, u.Material), path
        for usage in (u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, u.MaterialUsage.MATUSAGE_NANITE):
            u.MaterialEditingLibrary.set_base_material_usage(mat, usage, True)
        u.MaterialEditingLibrary.recompile_material(mat)
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False), path
    u.log(f"CIRE_GLTF_MATERIALS_PASS usage_masters={len(paths)}")


def run(u, folders):
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    edit = u.MaterialEditingLibrary

    def node(mat, kind, **props):
        n = edit.create_material_expression(mat, getattr(u, "MaterialExpression" + kind))
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def custom(mat, code, inputs, out=u.CustomMaterialOutputType.CMOT_FLOAT3):
        ins = []
        for name, _, _ in inputs:
            ci = u.CustomInput()
            ci.set_editor_property("input_name", name)
            ins.append(ci)
        n = node(mat, "Custom", code=code, output_type=out, inputs=ins)
        for name, src, pin in inputs:
            assert edit.connect_material_expressions(src, pin, n, name), name
        return n

    white = u.load_object(None, "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture")
    flat = u.load_object(None, "/Engine/EngineMaterials/DefaultNormal.DefaultNormal")
    if not lib.does_directory_exist(PKG):
        lib.make_directory(PKG)
    # A linear (non-sRGB) white default for the metallic/roughness slot: glTF MR maps are linear data.
    lin_path = f"{PKG}/T_FreeLinearWhite"
    if not lib.does_asset_exist(lin_path):
        import struct, zlib
        png = ROOT / "Saved/FreeLinearWhite.png"
        raw = b"".join(bytes([0] + [255] * 12) for _ in range(4))
        chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
        png.write_bytes(bytes([137, 80, 78, 71, 13, 10, 26, 10]) + chunk(b"IHDR", struct.pack(">IIBBBBB", 4, 4, 8, 2, 0, 0, 0)) +
                        chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
        task = u.AssetImportTask()
        for k, v in (("filename", str(png)), ("destination_path", PKG), ("destination_name", "T_FreeLinearWhite"),
                     ("automated", True), ("replace_existing", True), ("save", True)):
            task.set_editor_property(k, v)
        tools.import_asset_tasks([task])
        t = lib.load_asset(lin_path)
        t.set_editor_property("srgb", False)
        lib.save_loaded_asset(t, only_if_is_dirty=False)
    linear_white = lib.load_asset(lin_path)

    def master(name, blend):
        path = f"{PKG}/{name}"
        mat = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, PKG, u.Material, u.MaterialFactoryNew())
        edit.delete_all_material_expressions(mat)
        for usage in (u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, u.MaterialUsage.MATUSAGE_NANITE,
                      u.MaterialUsage.MATUSAGE_STATIC_LIGHTING, u.MaterialUsage.MATUSAGE_SKELETAL_MESH):
            edit.set_base_material_usage(mat, usage, True)
        if blend != "opaque":
            mat.set_editor_property("two_sided", True)
            mat.set_editor_property("blend_mode", u.BlendMode.BLEND_MASKED if blend == "masked" else u.BlendMode.BLEND_TRANSLUCENT)
        if blend == "translucent":
            mat.set_editor_property("translucency_lighting_mode", u.TranslucencyLightingMode.TLM_SURFACE)
        uv = node(mat, "TextureCoordinate")
        bc = node(mat, "TextureSampleParameter2D", parameter_name="BaseColorTexture", texture=white)
        nm = node(mat, "TextureSampleParameter2D", parameter_name="NormalTexture", texture=flat, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        mr = node(mat, "TextureSampleParameter2D", parameter_name="MetallicRoughnessTexture", texture=linear_white, sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        for s in (bc, nm, mr):
            edit.connect_material_expressions(uv, "", s, "UVs")
        fac = node(mat, "VectorParameter", parameter_name="BaseColorFactor", default_value=u.LinearColor(1, 1, 1, 1))
        met = node(mat, "ScalarParameter", parameter_name="MetallicFactor", default_value=1.0)
        rou = node(mat, "ScalarParameter", parameter_name="RoughnessFactor", default_value=1.0)
        nsc = node(mat, "ScalarParameter", parameter_name="NormalScale", default_value=1.0)
        base = custom(mat, "return T.rgb*F.rgb;", [("T", bc, "RGB"), ("F", fac, "")])
        edit.connect_material_property(base, "", u.MaterialProperty.MP_BASE_COLOR)
        edit.connect_material_property(custom(mat, "return saturate(M.b*F);", [("M", mr, "RGB"), ("F", met, "")], u.CustomMaterialOutputType.CMOT_FLOAT1), "", u.MaterialProperty.MP_METALLIC)
        edit.connect_material_property(custom(mat, "return saturate(M.g*F);", [("M", mr, "RGB"), ("F", rou, "")], u.CustomMaterialOutputType.CMOT_FLOAT1), "", u.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(custom(mat, "return normalize(float3(N.xy*S,max(N.z,0.05)));", [("N", nm, "RGB"), ("S", nsc, "")]), "", u.MaterialProperty.MP_NORMAL)
        if blend == "masked":
            cut = node(mat, "ScalarParameter", parameter_name="AlphaCutoff", default_value=0.5)
            edit.connect_material_property(custom(mat, "return (T.a*F.a)>C?1:0;", [("T", bc, "RGBA"), ("F", fac, "RGBA"), ("C", cut, "")], u.CustomMaterialOutputType.CMOT_FLOAT1), "", u.MaterialProperty.MP_OPACITY_MASK)
        if blend == "translucent":
            edit.connect_material_property(custom(mat, "return saturate(max(T.a*F.a,0.35));", [("T", bc, "RGBA"), ("F", fac, "RGBA")], u.CustomMaterialOutputType.CMOT_FLOAT1), "", u.MaterialProperty.MP_OPACITY)
        edit.layout_material_expressions(mat)
        edit.recompile_material(mat)
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False), name
        return mat

    masters = {"opaque": master("M_FreePBR", "opaque"), "masked": master("M_FreePBRMasked", "masked"),
               "translucent": master("M_FreePBRTranslucent", "translucent")}
    counts = {}
    for folder in folders:
        for path in lib.list_assets(folder, recursive=True):
            asset = lib.load_asset(path)
            if not isinstance(asset, u.MaterialInstanceConstant):
                continue
            parent = asset.get_editor_property("parent")
            pname = parent.get_path_name() if parent else ""
            if not pname.startswith("/InterchangeAssets/"):
                continue
            kind = "masked" if "_Mask" in pname else "translucent" if ("_Blend" in pname or "Transmission" in pname) else "opaque"
            # read the factors before re-parenting (they live on the instance, but defaults come from the old parent)
            factor = edit.get_material_instance_vector_parameter_value(asset, "BaseColorFactor")
            metal = edit.get_material_instance_scalar_parameter_value(asset, "MetallicFactor")
            rough = edit.get_material_instance_scalar_parameter_value(asset, "RoughnessFactor")
            edit.set_material_instance_parent(asset, masters[kind])
            edit.set_material_instance_vector_parameter_value(asset, "BaseColorFactor", factor)
            edit.set_material_instance_scalar_parameter_value(asset, "MetallicFactor", metal)
            edit.set_material_instance_scalar_parameter_value(asset, "RoughnessFactor", rough)
            lib.save_loaded_asset(asset, only_if_is_dirty=False)
            counts[kind] = counts.get(kind, 0) + 1
    u.log(f"CIRE_GLTF_MATERIALS_PASS reparented={counts}")


def launch(args):
    import stat
    folders = args[args.index("--folders") + 1] if "--folders" in args else DEFAULT_FOLDERS
    masters = args[args.index("--usage-masters") + 1] if "--usage-masters" in args else ""
    for master in filter(None, masters.split(",")):
        disk = ROOT / "Content" / (master.replace("/Game/", "") + ".uasset")
        disk.chmod(disk.stat().st_mode | stat.S_IWRITE)
    for folder in ([] if masters else folders.split(",")):
        disk = ROOT / "Content" / folder.replace("/Game/", "")
        for path in disk.rglob("*.uasset"):
            path.chmod(path.stat().st_mode | stat.S_IWRITE)
    log = ROOT / "Saved/Logs/FixGltfMaterials.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, CIRE_GLTF_FOLDERS=folders, CIRE_USAGE_MASTERS=masters)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"),
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-run=pythonscript",
               f"-script={Path(__file__).resolve()}", f"-abslog={log}"]
    result = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT, env=env,
                            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text(encoding="utf-8", errors="replace")
    ok = result.returncode == 0 and "CIRE_GLTF_MATERIALS_PASS" in text
    for line in text.splitlines():
        if "CIRE_GLTF_MATERIALS_PASS" in line or ("Error" in line and "Python" in line):
            print(line)
    print(f"exit={result.returncode} pass={ok} log={log}")
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        sys.exit(launch(sys.argv[1:]))
    else:
        if os.environ.get("CIRE_USAGE_MASTERS"):
            set_usage(unreal, [m for m in os.environ["CIRE_USAGE_MASTERS"].split(",") if m])
        else:
            run(unreal, [f for f in os.environ.get("CIRE_GLTF_FOLDERS", DEFAULT_FOLDERS).split(",") if f])
