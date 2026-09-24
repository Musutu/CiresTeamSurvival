"""Import the themed-arena content into /Game/Arenas (Docs/Arenas.md).

Run with regular Python: launches an unattended UnrealEditor-Cmd Python commandlet
against this project; inside Unreal it performs the import. Inputs:
  Saved/ArenaSources          (Tools/FetchArenaAssets.py, Poly Haven CC0)
  Art/Arenas/Meshes           (Tools/BuildArenaMeshes.py, original geometry)
  /Game/Environment/Town/Textures (existing CC0 town textures, reused read-only)
Writes only under /Game/Arenas and Art/Arenas/ImportReport.json.

Usage: python Tools/ImportArenaContent.py [--only textures,materials,sky,meshes,props]
"""
from __future__ import annotations

import json
import os
import stat
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PKG = "/Game/Arenas"
TOWN_TEX = "/Game/Environment/Town/Textures"
SRC = ROOT / "Saved/ArenaSources"
REPORT = ROOT / "Art/Arenas/ImportReport.json"

# ---- surface instances on UV-mapped meshes (metre UVs): slot -> (texture, metres/repeat, tint, rough mul, normal, macro, saturation)
SURFACES = {
    "Hay": ("thatch_roof_angled", 1.1, (1.45, 1.16, 0.64), 1.0, 1.2, 0.25, 1.1),
    "HayEnd": ("thatch_roof_angled", 0.7, (1.30, 1.02, 0.55), 1.0, 1.3, 0.15, 1.05),
    "Straw": ("thatch_roof_angled", 0.8, (1.45, 1.18, 0.68), 1.0, 1.1, 0.2, 1.05),
    "Ears": ("thatch_roof_angled", 0.5, (1.50, 1.08, 0.52), 1.0, 1.2, 0.15, 1.15),
    "Twine": ("fabric_pattern_05", 0.4, (0.40, 0.30, 0.20), 1.0, 0.6, 0.0, 0.4),
    "Timber": ("rough_wood", 1.2, (0.52, 0.42, 0.33), 1.0, 1.0, 0.2, 0.7),
    "Planks": ("weathered_planks", 1.8, (0.70, 0.60, 0.50), 1.0, 1.0, 0.3, 0.6),
    "DarkWood": ("weathered_planks", 1.6, (0.30, 0.24, 0.20), 1.0, 1.0, 0.3, 0.5),
    "Thatch": ("thatch_roof_angled", 2.4, (0.80, 0.68, 0.48), 1.0, 1.0, 0.3, 0.8),
    "Sack": ("fabric_pattern_05", 0.8, (0.55, 0.45, 0.32), 1.1, 0.8, 0.0, 0.35),
    "CoatCloth": ("fabric_pattern_07", 1.0, (0.45, 0.30, 0.25), 1.0, 0.8, 0.1, 0.5),
    "SailCloth": ("fabric_pattern_05", 1.6, (0.86, 0.82, 0.72), 1.0, 0.7, 0.2, 0.3),
    "Iron": ("rust_coarse_01", 1.0, (0.30, 0.28, 0.27), 1.0, 0.8, 0.1, 0.5),
    "RuinStone": ("stacked_stone_wall", 2.4, (0.95, 0.88, 0.78), 1.0, 1.1, 0.35, 0.7),
    "MillStone": ("stacked_stone_wall", 2.8, (1.10, 1.05, 0.95), 1.0, 1.0, 0.35, 0.5),
    "Basalt": ("dark_rock_02", 1.6, (0.50, 0.52, 0.56), 1.0, 1.3, 0.3, 0.6),
    "Bark": ("bark_willow", 1.4, (0.62, 0.60, 0.56), 1.0, 1.2, 0.25, 0.45),
    "LogEnd": ("rough_wood", 0.6, (0.60, 0.46, 0.32), 1.0, 1.0, 0.0, 0.8),
    "ContainerPaint": ("blue_metal_plate", 2.5, (0.55, 0.75, 0.95), 1.0, 1.0, 0.2, 1.0),
    "ContainerRust": ("painted_metal_shutter", 1.5, (0.95, 0.45, 0.20), 1.0, 1.0, 0.2, 1.0),
    "SteelDark": ("metal_plate_02", 1.2, (0.35, 0.37, 0.40), 1.0, 1.0, 0.2, 0.5),
    "Hull": ("metal_plate_02", 2.0, (0.80, 0.82, 0.86), 1.0, 1.0, 0.3, 0.4),
    "CratePanel": ("painted_metal_shutter", 0.8, (0.70, 0.72, 0.75), 1.0, 1.0, 0.1, 0.6),
}
# ---- world-aligned triplanar instances (ground planes and scaled shapes): slot -> (texture, metres, tint, rough, normal, macro, sat)
WORLD = {
    "Stubble": ("withered_grass", 2.6, (1.12, 0.88, 0.55), 1.0, 1.1, 0.55, 1.1),
    "Track": ("withered_grass", 2.0, (0.82, 0.66, 0.46), 1.0, 1.2, 0.5, 0.8),
    "FieldStone": ("rock_face_03", 1.2, (0.78, 0.72, 0.64), 1.0, 1.2, 0.3, 0.5),
    "BlackGravel": ("rock_ground", 2.4, (0.40, 0.40, 0.42), 1.0, 1.2, 0.5, 0.4),
    "BlackSand": ("coast_sand_01", 3.5, (0.20, 0.20, 0.21), 0.85, 1.3, 0.6, 0.25),
    "RedSoil": ("red_laterite_soil_stones", 3.0, (1.08, 0.78, 0.60), 1.0, 1.0, 0.5, 1.0),
    "SandstoneCap": ("sandstone_cracks", 2.5, (0.62, 0.38, 0.26), 1.0, 1.2, 0.35, 1.0),
    "ForestFloor": ("forest_floor", 3.2, (0.78, 0.74, 0.64), 1.0, 1.0, 0.55, 0.95),
    "RootEarth": ("farm_soil", 1.6, (0.62, 0.50, 0.40), 1.0, 1.2, 0.35, 0.8),
    "Seabed": ("damp_sand", 3.0, (0.72, 0.80, 0.82), 0.9, 1.0, 0.5, 0.55),
    "Deck": ("metal_plate", 1.6, (0.55, 0.58, 0.62), 0.9, 1.0, 0.25, 0.4),
    "HangarFloor": ("hangar_concrete_floor", 4.0, (0.55, 0.57, 0.60), 1.0, 1.0, 0.4, 0.4),
}
# ---- two-layer blend (triplanar base + top layer on up-facing surfaces, optional strata bands)
# slot -> (base tex, base m, base tint, top tex, top m, top tint, top amount, sharpness, strata strength, strata cm, strata colour)
BLEND = {
    "Lava": ("dark_rock_02", 2.2, (0.28, 0.29, 0.31), "mossy_rock", 1.4, (0.24, 0.30, 0.17), 0.12, 7.0, 0.0, 100.0, (1, 1, 1)),
    "Sandstone": ("sandstone_cracks", 2.4, (0.90, 0.48, 0.31), "red_sand", 3.0, (1.1, 0.72, 0.5), 0.18, 7.0, 0.55, 140.0, (0.62, 0.34, 0.24)),
    "ForestRock": ("rock_face_03", 2.4, (0.55, 0.56, 0.52), "mossy_rock", 1.4, (0.62, 0.80, 0.45), 0.32, 5.0, 0.0, 100.0, (1, 1, 1)),
    "Ruin": ("coral_fort_wall_01", 2.0, (0.78, 0.82, 0.80), "coral_gravel", 1.6, (0.55, 0.75, 0.55), 0.32, 5.0, 0.0, 100.0, (1, 1, 1)),
    "RuinBlock": ("coral_stone_wall", 2.2, (0.75, 0.80, 0.78), "coral_gravel", 1.6, (0.55, 0.72, 0.55), 0.35, 5.0, 0.0, 100.0, (1, 1, 1)),
    "SeaRock": ("rock_face_03", 2.6, (0.50, 0.58, 0.60), "coral_gravel", 1.5, (0.75, 0.62, 0.55), 0.4, 4.0, 0.0, 100.0, (1, 1, 1)),
}
# ---- flat / emissive: slot -> (base colour, roughness, metallic, emissive colour, emissive strength, flicker)
FLATS = {
    "Emissive": ((0.02, 0.05, 0.06), 0.4, 0.0, (0.35, 0.85, 1.0), 12.0, 0.0),
    "EmissiveAmber": ((0.05, 0.03, 0.01), 0.4, 0.0, (1.0, 0.55, 0.15), 10.0, 0.0),
    "EngineGlow": ((0.05, 0.02, 0.01), 0.4, 0.0, (1.0, 0.45, 0.15), 30.0, 0.05),
    "Glass": ((0.01, 0.015, 0.02), 0.05, 0.6, (0.02, 0.05, 0.08), 1.0, 0.0),
    "Coral": ((0.85, 0.35, 0.22), 0.7, 0.0, (0.0, 0.0, 0.0), 0.0, 0.0),
    "CoralViolet": ((0.45, 0.22, 0.60), 0.7, 0.0, (0.0, 0.0, 0.0), 0.0, 0.0),
    "CoralGold": ((0.90, 0.62, 0.20), 0.7, 0.0, (0.0, 0.0, 0.0), 0.0, 0.0),
    "Mote": ((0.0, 0.0, 0.0), 1.0, 0.0, (0.6, 0.9, 1.0), 2.0, 0.0),
    "Water": ((0.005, 0.012, 0.015), 0.04, 0.0, (0, 0, 0), 0.0, 0.0),
    "PortalRing": ((0.02, 0.02, 0.02), 0.4, 0.0, (1.0, 0.72, 0.30), 6.0, 0.08),
}
# ---- HDRI sky instances: name -> (hdri id, brightness, tint)
SKIES = {
    "Fields": ("plains_sunset", 1.0, (1.05, 0.97, 0.88)),
    "Iceland": ("kloofendal_overcast_puresky", 0.9, (0.92, 0.96, 1.02)),
    "Moab": ("kloofendal_43d_clear_puresky", 1.0, (1.0, 0.97, 0.94)),
    "Hornbeam": ("epping_forest_01", 0.9, (0.95, 1.0, 0.92)),
}
NANITE_MIN_TRIS = 1500
UV_SLOTS = set(SURFACES) | {"Wheat", "Kelp", "Mote", "Shaft", "Hazard"}


# ================================================================== Unreal side
def run(u, only):
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    edit = u.MaterialEditingLibrary
    report = {"textures": {}, "materials": [], "sky": {}, "meshes": {}, "props": {}}

    def ensure(path):
        if not lib.does_directory_exist(path):
            lib.make_directory(path)

    def import_file(filename, dest, name=None, options=None):
        task = u.AssetImportTask()
        task.set_editor_property("filename", str(filename))
        task.set_editor_property("destination_path", dest)
        if name:
            task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        if options is not None:
            task.set_editor_property("options", options)
        tools.import_asset_tasks([task])
        return list(task.get_editor_property("imported_object_paths"))

    # ------------------------------------------------------------ textures
    arena_tex = sorted({v[0] for v in list(SURFACES.values()) + list(WORLD.values())} |
                       {v[0] for v in BLEND.values()} | {v[3] for v in BLEND.values()})
    textures = {}
    for tid in arena_tex:
        town = f"{TOWN_TEX}/{tid}"
        if lib.does_asset_exist(f"{town}/T_{tid}_D"):
            textures[tid] = {k: lib.load_asset(f"{town}/T_{tid}_{k}") for k in ("D", "N", "ARM")}
            continue
        dest = f"{PKG}/Textures/{tid}"
        maps = {}
        for kind, suffix in (("D", "diff"), ("N", "nor_gl"), ("ARM", "arm")):
            name = f"T_{tid}_{kind}"
            path = f"{dest}/{name}"
            if "textures" in only or not lib.does_asset_exist(path):
                ensure(dest)
                import_file(SRC / "Textures" / tid / f"{tid}_{suffix}_2k.jpg", dest, name)
                tex = lib.load_asset(path)
                assert tex, path
                if kind == "D":
                    tex.set_editor_property("srgb", True)
                elif kind == "N":
                    tex.set_editor_property("srgb", False)
                    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_NORMALMAP)
                    tex.set_editor_property("flip_green_channel", False)
                else:
                    tex.set_editor_property("srgb", False)
                    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_MASKS)
                tex.set_editor_property("lod_group", u.TextureGroup.TEXTUREGROUP_WORLD)
                lib.save_loaded_asset(tex, only_if_is_dirty=False)
            maps[kind] = lib.load_asset(path)
            assert maps[kind], path
        textures[tid] = maps
        report["textures"][tid] = [m.get_path_name() for m in maps.values()]

    # ------------------------------------------------------------ material helpers
    def new_material(name, folder="Materials"):
        path = f"{PKG}/{folder}/{name}"
        ensure(f"{PKG}/{folder}")
        mat = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, f"{PKG}/{folder}", u.Material, u.MaterialFactoryNew())
        edit.delete_all_material_expressions(mat)
        for usage in (u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, u.MaterialUsage.MATUSAGE_NANITE, u.MaterialUsage.MATUSAGE_STATIC_LIGHTING):
            edit.set_base_material_usage(mat, usage, True)
        return mat

    def node(mat, kind, **props):
        n = edit.create_material_expression(mat, getattr(u, "MaterialExpression" + kind))
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def custom(mat, code, inputs, out=u.CustomMaterialOutputType.CMOT_FLOAT3):
        ins = []
        for item in inputs:
            ci = u.CustomInput()
            ci.set_editor_property("input_name", item[0])
            ins.append(ci)
        n = node(mat, "Custom", code=code, output_type=out, inputs=ins)
        for item in inputs:  # (name, source) or (name, source, output pin)
            name, src, pin = item[0], item[1], item[2] if len(item) > 2 else ""
            assert edit.connect_material_expressions(src, pin, n, name), name
        return n

    def scalar(mat, name, value):
        return node(mat, "ScalarParameter", parameter_name=name, default_value=value)

    def vector(mat, name, value):
        return node(mat, "VectorParameter", parameter_name=name, default_value=u.LinearColor(*value, 1.0))

    def finish(mat):
        edit.layout_material_expressions(mat)
        edit.recompile_material(mat)
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False)
        report["materials"].append(mat.get_path_name())

    def prop(mat, pin, src, out=""):
        assert edit.connect_material_property(src, out, getattr(u.MaterialProperty, pin)), pin

    any_tex = textures[arena_tex[0]]
    TRI = """float3 Nn=normalize(N); float3 w=pow(abs(Nn),4); w/=dot(w,1);
float3 p=P/Size; float2 ux=float2(p.y,-p.z), uy=float2(p.x,-p.z), uz=float2(p.x,p.y);
"""
    TRI_NORMAL = """float2 nx=Texture2DSample(Tex,TexSampler,ux).rg*2-1;
float2 ny=Texture2DSample(Tex,TexSampler,uy).rg*2-1;
float2 nz=Texture2DSample(Tex,TexSampler,uz).rg*2-1;
float3 px=nx.x*float3(0,1,0)-nx.y*float3(0,0,-1);
float3 py=ny.x*float3(1,0,0)-ny.y*float3(0,0,-1);
float3 pz=nz.x*float3(1,0,0)-nz.y*float3(0,1,0);
"""

    def build_masters():
        # ---- UV surface (metre UVs)
        mat = new_material("M_ArenaSurface")
        uv = node(mat, "TextureCoordinate")
        tiling = scalar(mat, "Tiling", 0.5)
        tb = node(mat, "TextureObjectParameter", parameter_name="BaseColorMap", texture=any_tex["D"])
        tn = node(mat, "TextureObjectParameter", parameter_name="NormalMap", texture=any_tex["N"])
        ta = node(mat, "TextureObjectParameter", parameter_name="ARMMap", texture=any_tex["ARM"])
        tint, macro, nstr = vector(mat, "Tint", (1, 1, 1)), scalar(mat, "MacroVariation", 0.3), scalar(mat, "NormalStrength", 1.0)
        rmul, sat = scalar(mat, "RoughnessMul", 1.0), scalar(mat, "Saturation", 1.0)
        pr = node(mat, "PerInstanceRandom")
        vary = scalar(mat, "InstanceVariation", 0.12)
        base = custom(mat, """float2 t=UV*Tiling;
float3 c=Texture2DSample(Tex,TexSampler,t).rgb;
float m=dot(Texture2DSample(Tex,TexSampler,t*0.137+0.31).rgb,float3(.3,.59,.11));
c*=lerp(1.0,saturate(0.5+m*1.5),Macro);
c=lerp(dot(c,float3(.3,.59,.11)).xxx,c,Sat);
c*=1.0+(R-0.5)*Vary;
return c*Tint;""", [("UV", uv), ("Tiling", tiling), ("Tex", tb), ("Tint", tint), ("Macro", macro), ("Sat", sat), ("R", pr), ("Vary", vary)])
        nrm = custom(mat, """float2 t=UV*Tiling; float2 xy=Texture2DSample(Tex,TexSampler,t).rg*2-1;
xy*=Strength; xy.y=-xy.y;
return normalize(float3(xy,sqrt(saturate(1-dot(xy,xy)))+0.001));""", [("UV", uv), ("Tiling", tiling), ("Tex", tn), ("Strength", nstr)])
        arm = custom(mat, "return Texture2DSample(Tex,TexSampler,UV*Tiling).rgb;", [("UV", uv), ("Tiling", tiling), ("Tex", ta)])
        prop(mat, "MP_BASE_COLOR", base)
        prop(mat, "MP_NORMAL", nrm)
        prop(mat, "MP_ROUGHNESS", custom(mat, "return saturate(A.g*R);", [("A", arm), ("R", rmul)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        prop(mat, "MP_AMBIENT_OCCLUSION", custom(mat, "return A.r;", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        prop(mat, "MP_METALLIC", custom(mat, "return A.b;", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        finish(mat)

        # ---- world triplanar with anti-tiling on the top projection (grounds): a rotated, rescaled second sample
        # blended by smooth world-space value noise, so large floors do not show the texture grid.
        mat = new_material("M_ArenaWorld")
        mat.set_editor_property("tangent_space_normal", False)
        pos, nws = node(mat, "WorldPosition"), node(mat, "VertexNormalWS")
        size = scalar(mat, "WorldSizeCm", 300.0)
        anti = scalar(mat, "AntiTile", 0.85)
        tb = node(mat, "TextureObjectParameter", parameter_name="BaseColorMap", texture=any_tex["D"])
        tn = node(mat, "TextureObjectParameter", parameter_name="NormalMap", texture=any_tex["N"])
        ta = node(mat, "TextureObjectParameter", parameter_name="ARMMap", texture=any_tex["ARM"])
        tint, macro, nstr = vector(mat, "Tint", (1, 1, 1)), scalar(mat, "MacroVariation", 0.3), scalar(mat, "NormalStrength", 1.0)
        rmul, sat = scalar(mat, "RoughnessMul", 1.0), scalar(mat, "Saturation", 1.0)
        pre = TRI + """float2 uz2=float2(uz.x*0.8-uz.y*0.6, uz.x*0.6+uz.y*0.8)*0.63+0.17;
float2 q=P.xy/(Size*3.7); float2 qi=floor(q), qf=frac(q); qf=qf*qf*(3-2*qf);
float n00=frac(sin(dot(qi,float2(127.1,311.7)))*43758.5453), n10=frac(sin(dot(qi+float2(1,0),float2(127.1,311.7)))*43758.5453);
float n01=frac(sin(dot(qi+float2(0,1),float2(127.1,311.7)))*43758.5453), n11=frac(sin(dot(qi+float2(1,1),float2(127.1,311.7)))*43758.5453);
float mt=saturate((lerp(lerp(n00,n10,qf.x),lerp(n01,n11,qf.x),qf.y)-0.3)*2.2)*Anti;
"""
        base = custom(mat, pre + """float3 top=lerp(Texture2DSample(Tex,TexSampler,uz).rgb, Texture2DSample(Tex,TexSampler,uz2).rgb, mt);
float3 c=Texture2DSample(Tex,TexSampler,ux).rgb*w.x+Texture2DSample(Tex,TexSampler,uy).rgb*w.y+top*w.z;
float m=dot(Texture2DSample(Tex,TexSampler,uz*0.113+0.37).rgb,float3(.3,.59,.11));
float m2=dot(Texture2DSample(Tex,TexSampler,uz*0.031+0.71).rgb,float3(.3,.59,.11));
c*=lerp(1.0,saturate(0.45+m*1.2)*saturate(0.55+m2*1.0),Macro);
c=lerp(dot(c,float3(.3,.59,.11)).xxx,c,Sat);
return c*Tint;""", [("P", pos), ("N", nws), ("Size", size), ("Tex", tb), ("Tint", tint), ("Macro", macro), ("Sat", sat), ("Anti", anti)])
        nrm = custom(mat, pre + """float2 nx=Texture2DSample(Tex,TexSampler,ux).rg*2-1;
float2 ny=Texture2DSample(Tex,TexSampler,uy).rg*2-1;
float2 na=Texture2DSample(Tex,TexSampler,uz).rg*2-1;
float2 nb=Texture2DSample(Tex,TexSampler,uz2).rg*2-1; nb=float2(nb.x*0.8+nb.y*0.6, -nb.x*0.6+nb.y*0.8);
float2 nz=lerp(na,nb,mt);
float3 px=nx.x*float3(0,1,0)-nx.y*float3(0,0,-1);
float3 py=ny.x*float3(1,0,0)-ny.y*float3(0,0,-1);
float3 pz=nz.x*float3(1,0,0)-nz.y*float3(0,1,0);
return normalize(Nn+(px*w.x+py*w.y+pz*w.z)*Strength);""", [("P", pos), ("N", nws), ("Size", size), ("Tex", tn), ("Strength", nstr), ("Anti", anti)])
        arm = custom(mat, pre + """float3 top=lerp(Texture2DSample(Tex,TexSampler,uz).rgb, Texture2DSample(Tex,TexSampler,uz2).rgb, mt);
return Texture2DSample(Tex,TexSampler,ux).rgb*w.x+Texture2DSample(Tex,TexSampler,uy).rgb*w.y+top*w.z;""",
                     [("P", pos), ("N", nws), ("Size", size), ("Tex", ta), ("Anti", anti)])
        prop(mat, "MP_BASE_COLOR", base)
        prop(mat, "MP_NORMAL", nrm)
        prop(mat, "MP_ROUGHNESS", custom(mat, "return saturate(A.g*R);", [("A", arm), ("R", rmul)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        prop(mat, "MP_AMBIENT_OCCLUSION", custom(mat, "return A.r;", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        finish(mat)

        # ---- two-layer blend: base triplanar + top layer on up-facing surfaces + optional strata bands
        mat = new_material("M_ArenaBlend")
        mat.set_editor_property("tangent_space_normal", False)
        pos, nws = node(mat, "WorldPosition"), node(mat, "VertexNormalWS")
        bs, ts = scalar(mat, "WorldSizeCm", 250.0), scalar(mat, "TopSizeCm", 200.0)
        bd = node(mat, "TextureObjectParameter", parameter_name="BaseColorMap", texture=any_tex["D"])
        bn = node(mat, "TextureObjectParameter", parameter_name="NormalMap", texture=any_tex["N"])
        ba = node(mat, "TextureObjectParameter", parameter_name="ARMMap", texture=any_tex["ARM"])
        td = node(mat, "TextureObjectParameter", parameter_name="TopColorMap", texture=any_tex["D"])
        tn2 = node(mat, "TextureObjectParameter", parameter_name="TopNormalMap", texture=any_tex["N"])
        ta2 = node(mat, "TextureObjectParameter", parameter_name="TopARMMap", texture=any_tex["ARM"])
        tint, ttint = vector(mat, "Tint", (1, 1, 1)), vector(mat, "TopTint", (1, 1, 1))
        amount, sharp = scalar(mat, "TopAmount", 0.4), scalar(mat, "TopSharpness", 5.0)
        sstr, scm, scol = scalar(mat, "StrataStrength", 0.0), scalar(mat, "StrataCm", 150.0), vector(mat, "StrataColor", (1, 1, 1))
        nstr = scalar(mat, "NormalStrength", 1.0)
        pr = node(mat, "PerInstanceRandom")
        mask = custom(mat, TRI + """float3 q=P/(Size*3.1);
float n=Texture2DSample(Tex,TexSampler,q.xy+q.z*0.37).g-0.5;
return saturate((Nn.z-(1-Amount))*Sharp+n*1.6);""", [("P", pos), ("N", nws), ("Size", bs), ("Tex", ba), ("Amount", amount), ("Sharp", sharp)],
                      u.CustomMaterialOutputType.CMOT_FLOAT1)
        def tri_sample(tex, sz):
            return custom(mat, TRI + "return Texture2DSample(Tex,TexSampler,ux).rgb*w.x+Texture2DSample(Tex,TexSampler,uy).rgb*w.y+Texture2DSample(Tex,TexSampler,uz).rgb*w.z;",
                          [("P", pos), ("N", nws), ("Size", sz), ("Tex", tex)])
        bcol, tcol, barm, tarm = tri_sample(bd, bs), tri_sample(td, ts), tri_sample(ba, bs), tri_sample(ta2, ts)
        base = custom(mat, """float band=0.5+0.5*sin(P.z/StrataCm*6.2831+sin(P.x*0.0013+P.y*0.0011)*2.0+R*6.2831);
float3 b=B*Tint*lerp(float3(1,1,1),StrataColor*1.25,StrataStrength*band*band);
return lerp(b,T*TopTint,M);""", [("B", bcol), ("T", tcol), ("Tint", tint), ("TopTint", ttint), ("M", mask), ("P", pos), ("StrataCm", scm),
                                  ("StrataColor", scol), ("StrataStrength", sstr), ("R", pr)])
        def tri_normal(tex, sz):
            return custom(mat, TRI + TRI_NORMAL + "return (px*w.x+py*w.y+pz*w.z)*Strength;", [("P", pos), ("N", nws), ("Size", sz), ("Tex", tex), ("Strength", nstr)])
        nb, nt = tri_normal(bn, bs), tri_normal(tn2, ts)
        nrm = custom(mat, "return normalize(normalize(N)+lerp(A,B,M));", [("N", nws), ("A", nb), ("B", nt), ("M", mask)])
        arm = custom(mat, "return lerp(A,B,M);", [("A", barm), ("B", tarm), ("M", mask)])
        prop(mat, "MP_BASE_COLOR", base)
        prop(mat, "MP_NORMAL", nrm)
        prop(mat, "MP_ROUGHNESS", custom(mat, "return saturate(A.g);", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        prop(mat, "MP_AMBIENT_OCCLUSION", custom(mat, "return A.r;", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        finish(mat)

        # ---- flat / emissive
        mat = new_material("M_ArenaFlat")
        bc, ro, me = vector(mat, "BaseColor", (0.1, 0.1, 0.1)), scalar(mat, "Roughness", 0.5), scalar(mat, "Metallic", 0.0)
        ec, es, fl = vector(mat, "EmissiveColor", (0, 0, 0)), scalar(mat, "EmissiveStrength", 0.0), scalar(mat, "Flicker", 0.0)
        t, pos = node(mat, "Time"), node(mat, "WorldPosition")
        emi = custom(mat, "float f=1+Fl*(sin(T*7.3+P.x*0.013)*0.6+sin(T*13.1+P.y*0.021)*0.4);return C*S*f;",
                     [("C", ec), ("S", es), ("Fl", fl), ("T", t), ("P", pos)])
        prop(mat, "MP_BASE_COLOR", bc)
        prop(mat, "MP_ROUGHNESS", ro)
        prop(mat, "MP_METALLIC", me)
        prop(mat, "MP_EMISSIVE_COLOR", emi)
        finish(mat)

        # ---- wheat: golden stalks and ears, backlit two-sided foliage, wind sway (V = normalised height, U > 1.5 = ear)
        mat = new_material("M_ArenaWheat")
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        uv, pos, pr, t = node(mat, "TextureCoordinate"), node(mat, "WorldPosition"), node(mat, "PerInstanceRandom"), node(mat, "Time")
        c_low, c_high, c_ear = vector(mat, "StalkLow", (0.42, 0.36, 0.14)), vector(mat, "StalkHigh", (0.86, 0.66, 0.28)), vector(mat, "EarColor", (0.95, 0.66, 0.24))
        sss = vector(mat, "Backlight", (0.9, 0.55, 0.12))
        amp, speed, freq = scalar(mat, "WindAmplitude", 9.0), scalar(mat, "WindSpeed", 1.6), scalar(mat, "WindFrequency", 0.0021)
        gust = scalar(mat, "GustStrength", 0.6)
        base = custom(mat, """float h=saturate(UV.y);
float3 c=lerp(Low,High,smoothstep(0.0,0.85,h));
if(UV.x>1.5){ c=Ear*(0.82+0.3*frac(sin(dot(floor(P.xy/3.0),float2(12.9898,78.233)))*43758.5453)); }
c*=0.86+0.28*R;
return c;""", [("UV", uv), ("Low", c_low), ("High", c_high), ("Ear", c_ear), ("P", pos), ("R", pr)])
        wpo = custom(mat, """float h=saturate(UV.y);
float2 dir=normalize(float2(0.8,0.6));
float ph=dot(P.xy,dir)*Freq*6.2831;
float g=0.5+0.5*sin(T*0.35+ph*0.23);
float s=sin(T*Speed+ph+R*2.0)*(1+Gust*g)+0.35*sin(T*Speed*2.3+ph*1.7);
float a=Amp*h*h*s;
return float3(dir*a, -abs(a)*0.25*h);""", [("UV", uv), ("P", pos), ("T", t), ("R", pr), ("Amp", amp), ("Speed", speed), ("Freq", freq), ("Gust", gust)])
        prop(mat, "MP_BASE_COLOR", base)
        prop(mat, "MP_SUBSURFACE_COLOR", sss)
        prop(mat, "MP_ROUGHNESS", scalar(mat, "Roughness", 0.62))
        prop(mat, "MP_WORLD_POSITION_OFFSET", wpo)
        prop(mat, "MP_AMBIENT_OCCLUSION", custom(mat, "return lerp(0.45,1.0,saturate(UV.y*1.4));", [("UV", uv)], u.CustomMaterialOutputType.CMOT_FLOAT1))
        finish(mat)

        # ---- kelp: slow underwater sway growing with height
        mat = new_material("M_ArenaKelp")
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        uv, pos, pr, t = node(mat, "TextureCoordinate"), node(mat, "WorldPosition"), node(mat, "PerInstanceRandom"), node(mat, "Time")
        low, high = vector(mat, "Low", (0.10, 0.12, 0.04)), vector(mat, "High", (0.30, 0.36, 0.10))
        base = custom(mat, "return lerp(Low,High,saturate(UV.y*1.3))*(0.8+0.4*R);", [("UV", uv), ("Low", low), ("High", high), ("R", pr)])
        wpo = custom(mat, """float h=saturate(UV.y);
float s=sin(T*0.7+R*6.0+P.x*0.002)+0.4*sin(T*1.3+P.y*0.003);
return float3(s*h*h*45.0, cos(T*0.55+R*4.0)*h*h*30.0, 0);""", [("UV", uv), ("P", pos), ("T", t), ("R", pr)])
        prop(mat, "MP_BASE_COLOR", base)
        prop(mat, "MP_SUBSURFACE_COLOR", vector(mat, "Backlight", (0.25, 0.4, 0.08)))
        prop(mat, "MP_ROUGHNESS", scalar(mat, "Roughness", 0.5))
        prop(mat, "MP_WORLD_POSITION_OFFSET", wpo)
        finish(mat)

        # ---- drifting motes (unlit additive, rise and wrap)
        mat = new_material("M_ArenaMotes")
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("blend_mode", u.BlendMode.BLEND_ADDITIVE)
        pos, pr, t = node(mat, "ObjectPositionWS"), node(mat, "PerInstanceRandom"), node(mat, "Time")
        col, inten, rise, height = vector(mat, "MoteColor", (0.6, 0.85, 1.0)), scalar(mat, "Intensity", 1.5), scalar(mat, "RiseSpeed", 12.0), scalar(mat, "WrapHeight", 900.0)
        wpo = custom(mat, """float r=R*37.0;
float z=frac((T*Rise+R*H)/H)*H - (P.z - floor(P.z/H)*H);
return float3(sin(T*0.31+r)*60.0, cos(T*0.27+r*1.3)*60.0, z);""", [("P", pos), ("R", pr), ("T", t), ("Rise", rise), ("H", height)])
        emi = custom(mat, "return C*I*(0.4+0.6*frac(R*91.7));", [("C", col), ("I", inten), ("R", pr)])
        prop(mat, "MP_EMISSIVE_COLOR", emi)
        prop(mat, "MP_WORLD_POSITION_OFFSET", wpo)
        finish(mat)

        # ---- fake light shafts (unlit additive, soft edges, fade up the cone)
        mat = new_material("M_ArenaShaft")
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("blend_mode", u.BlendMode.BLEND_ADDITIVE)
        mat.set_editor_property("two_sided", True)
        uv, cam, nws, t = node(mat, "TextureCoordinate"), node(mat, "CameraVectorWS"), node(mat, "VertexNormalWS"), node(mat, "Time")
        col, inten = vector(mat, "ShaftColor", (0.5, 0.85, 0.9)), scalar(mat, "Intensity", 0.35)
        emi = custom(mat, """float edge=pow(saturate(abs(dot(normalize(N),normalize(C)))),2.5);
float v=saturate(UV.y);
float fade=smoothstep(0.0,0.25,v)*(1-smoothstep(0.7,1.0,v));
float flick=0.8+0.2*sin(T*0.6+UV.x*9.0);
return Col*I*edge*fade*flick;""", [("N", nws), ("C", cam), ("UV", uv), ("T", t), ("Col", col), ("I", inten)])
        prop(mat, "MP_EMISSIVE_COLOR", emi)
        finish(mat)

        # ---- caustics light function (animated, world XY)
        mat = new_material("M_ArenaCaustics")
        mat.set_editor_property("material_domain", u.MaterialDomain.MD_LIGHT_FUNCTION)
        pos, t = node(mat, "WorldPosition"), node(mat, "Time")
        scale, speed, strength = scalar(mat, "CausticScale", 0.004), scalar(mat, "CausticSpeed", 0.25), scalar(mat, "CausticStrength", 1.4)
        emi = custom(mat, """float2 uv=P.xy*S;
float c=0;
for(int k=0;k<2;k++){
  float2 q=uv*(1.0+k*0.73)+float2(T*Sp*(1+k*0.5),-T*Sp*0.7);
  float2 i=floor(q), f=frac(q); float d=1.0;
  for(int y=-1;y<=1;y++) for(int x=-1;x<=1;x++){
    float2 g=float2(x,y); float2 h=frac(sin(float2(dot(i+g,float2(127.1,311.7)),dot(i+g,float2(269.5,183.3))))*43758.5453);
    h=0.5+0.5*sin(T*Sp*3.0+6.2831*h);
    d=min(d,length(g+h-f));
  }
  c+=pow(saturate(1.0-d),6.0);
}
return (0.55+c*St).xxx;""", [("P", pos), ("T", t), ("S", scale), ("Sp", speed), ("St", strength)])
        prop(mat, "MP_EMISSIVE_COLOR", emi)
        finish(mat)

        # ---- procedural sky (underwater gradient / starfield with planet)
        mat = new_material("M_ArenaSkyProcedural", "Sky")
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("is_sky", True)
        cam, t = node(mat, "CameraVectorWS"), node(mat, "Time")
        zen, hor, gnd = vector(mat, "Zenith", (0.1, 0.3, 0.35)), vector(mat, "Horizon", (0.02, 0.08, 0.12)), vector(mat, "Ground", (0.0, 0.02, 0.03))
        sund, sunc, suns = vector(mat, "SunDirection", (0.2, 0.1, 0.97)), vector(mat, "SunColor", (0.6, 0.9, 1.0)), scalar(mat, "SunSize", 0.12)
        stars, planetd, planetc, planets = scalar(mat, "Stars", 0.0), vector(mat, "PlanetDirection", (0.6, -0.5, 0.35)), vector(mat, "PlanetColor", (0.3, 0.45, 0.8)), scalar(mat, "PlanetSize", 0.0)
        neb, bright = vector(mat, "NebulaColor", (0.0, 0.0, 0.0)), scalar(mat, "Brightness", 1.0)
        rays = scalar(mat, "Rays", 0.0)
        emi = custom(mat, """float3 d=-normalize(C);
float h=d.z;
float3 col= h>0 ? lerp(Hor,Zen,pow(saturate(h),0.6)) : lerp(Hor,Gnd,saturate(-h*3.0));
float3 sd=normalize(SunDir);
float sa=saturate(dot(d,sd));
col+=SunCol*(pow(sa,1.0/max(SunSize,0.001))*0.5+pow(sa,64.0)*2.0);
if(Rays>0){ float ang=atan2(d.y,d.x); float r=0.5+0.5*sin(ang*23.0+T*0.2)*sin(ang*7.0-T*0.13); col+=SunCol*Rays*r*pow(saturate(h),3.0)*0.35; }
if(Stars>0){
  float3 g=floor(d*380.0); float n=frac(sin(dot(g,float3(12.9898,78.233,37.719)))*43758.5453);
  float tw=0.6+0.4*sin(T*2.0+n*40.0);
  col+=smoothstep(0.9965,1.0,n)*tw*Stars*saturate(h*4+0.6)*float3(0.9,0.95,1.0)*6.0;
  float3 q=d*3.0; float nb=frac(sin(dot(floor(q*2.0),float3(7.1,3.3,5.7)))*127.1);
  col+=Neb*pow(saturate(0.5+0.5*sin(q.x*2.1+q.y*1.3+sin(q.z*3.0))),3.0)*(0.5+0.5*nb);
}
if(PlanetSize>0){
  float3 pd=normalize(PlanetDir); float pa=acos(saturate(dot(d,pd)));
  float disc=1-smoothstep(PlanetSize*0.985,PlanetSize,pa);
  float3 side=normalize(cross(pd,float3(0,0,1))); float3 up=cross(side,pd);
  float2 lp=float2(dot(d-pd,side),dot(d-pd,up))/PlanetSize;
  float lit=saturate(0.25+dot(normalize(float3(lp,sqrt(saturate(1-dot(lp,lp))))),normalize(float3(-0.6,0.3,0.7))));
  float bands=0.85+0.15*sin(lp.y*18.0+sin(lp.x*6.0)*1.5);
  col=lerp(col,PlanetCol*lit*bands,disc);
  col+=PlanetCol*0.9*exp(-abs(pa-PlanetSize)*60.0)*0.6;
}
return col*B;""", [("C", cam), ("T", t), ("Zen", zen), ("Hor", hor), ("Gnd", gnd), ("SunDir", sund), ("SunCol", sunc), ("SunSize", suns),
                   ("Stars", stars), ("PlanetDir", planetd), ("PlanetCol", planetc), ("PlanetSize", planets), ("Neb", neb), ("B", bright), ("Rays", rays)])
        prop(mat, "MP_EMISSIVE_COLOR", emi)
        finish(mat)

        # ---- HDRI sky dome with yaw, tint and brightness
        mat = new_material("M_ArenaSky", "Sky")
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("is_sky", True)
        cam = node(mat, "CameraVectorWS")
        first_cube = None
        for sky, (hdri, _, _) in SKIES.items():
            p = f"{PKG}/Sky/T_Sky_{sky}"
            if lib.does_asset_exist(p):
                first_cube = lib.load_asset(p)
                break
        samp = node(mat, "TextureSampleParameterCube", parameter_name="Sky", texture=first_cube)
        yaw = scalar(mat, "SkyYawDegrees", 0.0)
        dirn = custom(mat, "float3 d=-C; float a=radians(Y); float c=cos(a), s=sin(a); return float3(d.x*c-d.y*s, d.x*s+d.y*c, d.z);", [("C", cam), ("Y", yaw)])
        edit.connect_material_expressions(dirn, "", samp, "UVs")
        bright, tintv = scalar(mat, "Brightness", 1.0), vector(mat, "SkyTint", (1, 1, 1))
        hazec, hazes = vector(mat, "HorizonHaze", (1, 1, 1)), scalar(mat, "HazeStrength", 0.0)
        emi = custom(mat, "float3 d=-normalize(C); float hz=exp(-abs(d.z)*12.0)*H; return lerp(S.rgb*B*T, HC*B, saturate(hz));",
                     [("S", samp), ("B", bright), ("T", tintv), ("C", cam), ("HC", hazec), ("H", hazes)])
        prop(mat, "MP_EMISSIVE_COLOR", emi)
        finish(mat)

        # ---- sea water (opaque, glossy, procedural swell normals)
        mat = new_material("M_ArenaWater")
        mat.set_editor_property("tangent_space_normal", False)
        pos, t = node(mat, "WorldPosition"), node(mat, "Time")
        deep, shallow = vector(mat, "Deep", (0.01, 0.025, 0.03)), vector(mat, "Foam", (0.6, 0.65, 0.66))
        nrm = custom(mat, """float2 p=P.xy;
float2 g=float2(0,0);
float2 dirs[4]={float2(1,0.2),float2(0.3,1),float2(-0.7,0.6),float2(0.9,-0.5)};
float fr[4]={0.0018,0.0031,0.0057,0.011};
for(int k=0;k<4;k++){ float2 d=normalize(dirs[k]); float ph=dot(p,d)*fr[k]*6.2831+T*(0.6+k*0.35); g+=d*cos(ph)*0.35/(1+k); }
return normalize(float3(-g.x,-g.y,1));""", [("P", pos), ("T", t)])
        prop(mat, "MP_BASE_COLOR", deep)
        prop(mat, "MP_NORMAL", nrm)
        prop(mat, "MP_ROUGHNESS", scalar(mat, "Roughness", 0.06))
        prop(mat, "MP_SPECULAR", scalar(mat, "Specular", 0.8))
        finish(mat)

        # ---- masked leaves (Nanite-safe replacement for the glTF BLEND leaf materials), gentle wind
        mat = new_material("M_ArenaFoliage")
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("blend_mode", u.BlendMode.BLEND_MASKED)
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
        uv, pos, pr, t = node(mat, "TextureCoordinate"), node(mat, "WorldPosition"), node(mat, "PerInstanceRandom"), node(mat, "Time")
        leaf = node(mat, "TextureSampleParameter2D", parameter_name="LeafMap", texture=any_tex["D"])
        edit.connect_material_expressions(uv, "", leaf, "UVs")
        # masks sampler with a mask-compressed default (the tree alpha maps are imported as TC_MASKS)
        alpha = node(mat, "TextureSampleParameter2D", parameter_name="AlphaMap", texture=any_tex["ARM"], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_MASKS)
        edit.connect_material_expressions(uv, "", alpha, "UVs")
        lnrm = node(mat, "TextureSampleParameter2D", parameter_name="LeafNormal", texture=any_tex["N"], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        edit.connect_material_expressions(uv, "", lnrm, "UVs")
        tint, sss, hue = vector(mat, "LeafTint", (1, 1, 1)), vector(mat, "Backlight", (0.35, 0.45, 0.12)), scalar(mat, "InstanceVariation", 0.18)
        base = custom(mat, "return L.rgb*Tint*(1.0+(R-0.5)*V);", [("L", leaf), ("Tint", tint), ("R", pr), ("V", hue)])
        mask = custom(mat, "return lerp(1.0, min(LA, AR), U);", [("LA", leaf, "A"), ("AR", alpha, "R"), ("U", scalar(mat, "UseAlpha", 1.0))], u.CustomMaterialOutputType.CMOT_FLOAT1)
        wind = custom(mat, "float s=sin(T*1.7+P.x*0.004+P.y*0.003+R*6.0)*0.6+sin(T*3.1+P.z*0.02)*0.4; return float3(s*A,s*A*0.6,0);",
                      [("T", t), ("P", pos), ("R", pr), ("A", scalar(mat, "WindAmplitude", 3.0))])
        prop(mat, "MP_BASE_COLOR", base)
        prop(mat, "MP_OPACITY_MASK", mask)
        prop(mat, "MP_NORMAL", lnrm, "RGB")
        prop(mat, "MP_SUBSURFACE_COLOR", sss)
        prop(mat, "MP_ROUGHNESS", scalar(mat, "Roughness", 0.78))
        prop(mat, "MP_SPECULAR", scalar(mat, "Specular", 0.22))
        prop(mat, "MP_WORLD_POSITION_OFFSET", wind)
        finish(mat)

        # ---- hazard stripes (UV metres)
        mat = new_material("M_ArenaHazard")
        uv = node(mat, "TextureCoordinate")
        a, b = vector(mat, "StripeA", (0.85, 0.62, 0.05)), vector(mat, "StripeB", (0.02, 0.02, 0.02))
        base = custom(mat, "float s=frac((UV.x+UV.y)*3.3); return s<0.5?A:B;", [("UV", uv), ("A", a), ("B", b)])
        prop(mat, "MP_BASE_COLOR", base)
        prop(mat, "MP_ROUGHNESS", scalar(mat, "Roughness", 0.45))
        prop(mat, "MP_METALLIC", scalar(mat, "Metallic", 0.3))
        finish(mat)

    # HDRI cubes first: the sky-dome master needs one to compile.
    ensure(f"{PKG}/Sky")
    for sky, (hdri, bright, tint) in SKIES.items():
        name = f"T_Sky_{sky}"
        if "sky" in only or not lib.does_asset_exist(f"{PKG}/Sky/{name}"):
            src = next((SRC / "HDRI").glob(f"{hdri}_*.hdr"))
            import_file(src, f"{PKG}/Sky", name)
        report["sky"][sky] = f"{PKG}/Sky/{name}"
    if "materials" in only or not lib.does_asset_exist(f"{PKG}/Materials/M_ArenaFoliage") or not lib.does_asset_exist(f"{PKG}/Sky/M_ArenaSky"):
        build_masters()

    masters = {n: lib.load_asset(f"{PKG}/Materials/{n}") for n in ("M_ArenaSurface", "M_ArenaWorld", "M_ArenaBlend", "M_ArenaFlat", "M_ArenaWheat",
                                                                   "M_ArenaKelp", "M_ArenaMotes", "M_ArenaShaft", "M_ArenaCaustics", "M_ArenaWater", "M_ArenaHazard",
                                                                   "M_ArenaFoliage")}
    for k, v in masters.items():
        assert v, k

    def instance(name, parent, folder="Materials"):
        ensure(f"{PKG}/{folder}")
        path = f"{PKG}/{folder}/{name}"
        mi = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, f"{PKG}/{folder}", u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi, parent)
        return mi

    def setp(mi, scalars=None, vectors=None, texs=None):
        for k, v in (scalars or {}).items():
            edit.set_material_instance_scalar_parameter_value(mi, k, float(v))
        for k, v in (vectors or {}).items():
            edit.set_material_instance_vector_parameter_value(mi, k, u.LinearColor(*v, 1))
        for k, v in (texs or {}).items():
            edit.set_material_instance_texture_parameter_value(mi, k, v)
        lib.save_loaded_asset(mi, only_if_is_dirty=False)

    instances = {}
    for slot, (tid, metres, tint, rmul, nstr, macro, satv) in SURFACES.items():
        mi = instance(f"MI_Arena_{slot}", masters["M_ArenaSurface"])
        mp = textures[tid]
        setp(mi, {"Tiling": 1.0 / metres, "NormalStrength": nstr, "RoughnessMul": rmul, "MacroVariation": macro, "Saturation": satv},
             {"Tint": tint}, {"BaseColorMap": mp["D"], "NormalMap": mp["N"], "ARMMap": mp["ARM"]})
        instances[slot] = mi
    for slot, (tid, metres, tint, rmul, nstr, macro, satv) in WORLD.items():
        mi = instance(f"MI_ArenaW_{slot}", masters["M_ArenaWorld"])
        mp = textures[tid]
        setp(mi, {"WorldSizeCm": metres * 100, "NormalStrength": nstr, "RoughnessMul": rmul, "MacroVariation": macro, "Saturation": satv},
             {"Tint": tint}, {"BaseColorMap": mp["D"], "NormalMap": mp["N"], "ARMMap": mp["ARM"]})
        instances[slot] = mi
    for slot, (bt, bm, btint, tt, tm, ttint, amount, sharp, sstr, scm, scol) in BLEND.items():
        mi = instance(f"MI_ArenaB_{slot}", masters["M_ArenaBlend"])
        b, t = textures[bt], textures[tt]
        setp(mi, {"WorldSizeCm": bm * 100, "TopSizeCm": tm * 100, "TopAmount": amount, "TopSharpness": sharp, "StrataStrength": sstr, "StrataCm": scm},
             {"Tint": btint, "TopTint": ttint, "StrataColor": scol},
             {"BaseColorMap": b["D"], "NormalMap": b["N"], "ARMMap": b["ARM"], "TopColorMap": t["D"], "TopNormalMap": t["N"], "TopARMMap": t["ARM"]})
        instances[slot] = mi
    for slot, (bc, ro, me, ec, es, fl) in FLATS.items():
        mi = instance(f"MI_ArenaF_{slot}", masters["M_ArenaFlat"])
        setp(mi, {"Roughness": ro, "Metallic": me, "EmissiveStrength": es, "Flicker": fl}, {"BaseColor": bc, "EmissiveColor": ec})
        instances[slot] = mi
    instances["Wheat"] = instance("MI_Arena_Wheat", masters["M_ArenaWheat"])
    lib.save_loaded_asset(instances["Wheat"], only_if_is_dirty=False)
    instances["Kelp"] = instance("MI_Arena_Kelp", masters["M_ArenaKelp"])
    lib.save_loaded_asset(instances["Kelp"], only_if_is_dirty=False)
    instances["Mote"] = instance("MI_Arena_Motes", masters["M_ArenaMotes"])
    setp(instances["Mote"], {"Intensity": 2.5, "RiseSpeed": 10.0, "WrapHeight": 900.0}, {"MoteColor": (0.55, 0.85, 0.9)})
    instances["Pollen"] = instance("MI_Arena_Pollen", masters["M_ArenaMotes"])
    setp(instances["Pollen"], {"Intensity": 1.1, "RiseSpeed": 3.0, "WrapHeight": 650.0}, {"MoteColor": (1.0, 0.78, 0.42)})
    instances["Shaft"] = instance("MI_Arena_Shaft", masters["M_ArenaShaft"])
    setp(instances["Shaft"], {"Intensity": 0.09}, {"ShaftColor": (0.45, 0.85, 0.9)})
    instances["Hazard"] = instance("MI_Arena_Hazard", masters["M_ArenaHazard"])
    lib.save_loaded_asset(instances["Hazard"], only_if_is_dirty=False)
    instances["Caustics"] = instance("MI_Arena_Caustics", masters["M_ArenaCaustics"])
    lib.save_loaded_asset(instances["Caustics"], only_if_is_dirty=False)
    instances["Dapple"] = instance("MI_Arena_Dapple", masters["M_ArenaCaustics"])
    setp(instances["Dapple"], {"CausticScale": 0.0011, "CausticSpeed": 0.05, "CausticStrength": 0.55})
    instances["SeaWater"] = instance("MI_Arena_Water", masters["M_ArenaWater"])
    setp(instances["SeaWater"], {"Roughness": 0.42, "Specular": 0.3}, {"Deep": (0.008, 0.014, 0.017)})
    report["instances"] = sorted(instances)

    # ------------------------------------------------------------ skies
    sky_master = lib.load_asset(f"{PKG}/Sky/M_ArenaSky")
    proc = lib.load_asset(f"{PKG}/Sky/M_ArenaSkyProcedural")
    for sky, (hdri, bright, tint) in SKIES.items():
        mi = instance(f"MI_Sky_{sky}", sky_master, "Sky")
        cube = lib.load_asset(f"{PKG}/Sky/T_Sky_{sky}")
        edit.set_material_instance_texture_parameter_value(mi, "Sky", cube)
        setp(mi, {"Brightness": bright}, {"SkyTint": tint})
    mi = instance("MI_Sky_Underwater", proc, "Sky")
    setp(mi, {"SunSize": 0.08, "Brightness": 1.0, "Rays": 1.0},
         {"Zenith": (0.10, 0.42, 0.46), "Horizon": (0.012, 0.075, 0.10), "Ground": (0.004, 0.02, 0.028), "SunDirection": (0.15, 0.1, 0.98), "SunColor": (0.45, 0.85, 0.9)})
    mi = instance("MI_Sky_Space", proc, "Sky")
    setp(mi, {"SunSize": 0.02, "Stars": 1.0, "PlanetSize": 0.42, "Brightness": 1.0},
         {"Zenith": (0.004, 0.006, 0.014), "Horizon": (0.012, 0.018, 0.04), "Ground": (0.002, 0.003, 0.008), "SunDirection": (-0.55, 0.6, 0.35),
          "SunColor": (1.0, 0.9, 0.75), "PlanetDirection": (0.75, -0.35, 0.18), "PlanetColor": (0.35, 0.55, 0.95), "NebulaColor": (0.05, 0.02, 0.09)})

    # ------------------------------------------------------------ generated meshes
    def mesh_options():
        opts = u.FbxImportUI()
        opts.set_editor_property("import_mesh", True)
        opts.set_editor_property("import_materials", False)
        opts.set_editor_property("import_textures", False)
        opts.set_editor_property("import_as_skeletal", False)
        opts.set_editor_property("mesh_type_to_import", u.FBXImportType.FBXIT_STATIC_MESH)
        data = opts.get_editor_property("static_mesh_import_data")
        data.set_editor_property("combine_meshes", True)
        data.set_editor_property("auto_generate_collision", False)
        data.set_editor_property("convert_scene", False)
        data.set_editor_property("convert_scene_unit", False)
        data.set_editor_property("normal_import_method", u.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS)
        return opts

    def nanite(mesh, enabled):
        ns = mesh.get_editor_property("nanite_settings")
        ns.set_editor_property("enabled", enabled)
        mesh.set_editor_property("nanite_settings", ns)

    no_nanite = {"SM_Arena_Mote", "SM_Arena_LightShaft", "SM_Arena_WindmillSails"}
    if "meshes" in only or not lib.does_asset_exist(f"{PKG}/Meshes/SM_Arena_WheatClump"):
        ensure(f"{PKG}/Meshes")
        meta = json.loads((ROOT / "Art/Arenas/Meshes/ArenaMeshes.json").read_text())
        wanted = set(os.environ.get("CIRE_ARENA_MESHES", "").split(",")) - {""}
        for row in meta["meshes"]:
            name = row["name"]
            if wanted and name not in wanted:
                continue
            import_file(ROOT / "Art/Arenas/Meshes" / f"{name}.obj", f"{PKG}/Meshes", name, mesh_options())
            mesh = lib.load_asset(f"{PKG}/Meshes/{name}")
            assert isinstance(mesh, u.StaticMesh), name
            for i, slot in enumerate(mesh.get_editor_property("static_materials")):
                key = str(slot.get_editor_property("imported_material_slot_name"))
                mi = instances.get(key)
                if not mi:
                    raise RuntimeError(f"{name}: unknown material slot {key}")
                mesh.set_material(i, mi)
            tris = mesh.get_num_triangles(0)
            nanite(mesh, name not in no_nanite and tris >= NANITE_MIN_TRIS)
            if name in ("SM_Arena_WheatClump", "SM_Arena_StubbleTuft", "SM_Arena_Kelp"):
                ns = mesh.get_editor_property("nanite_settings")
                ns.set_editor_property("shape_preservation", u.NaniteShapePreservation.PRESERVE_AREA)
                mesh.set_editor_property("nanite_settings", ns)
            b = mesh.get_bounds()
            assert lib.save_loaded_asset(mesh, only_if_is_dirty=False), name
            report["meshes"][name] = {"triangles": tris, "extent": [round(b.box_extent.x), round(b.box_extent.y), round(b.box_extent.z)]}

    # ------------------------------------------------------------ Poly Haven models
    manifest = json.loads((ROOT / "Art/Arenas/SourceManifest.json").read_text())
    for asset in manifest["assets"]:
        if asset["kind"] != "model":
            continue
        aid = asset["id"]
        dest = f"{PKG}/Props/{aid}"
        importing = "props" in only or not lib.does_directory_exist(dest)
        if importing:
            ensure(dest)
            import_file(ROOT / asset["entry"], dest)
        meshes = []
        for p in lib.list_assets(dest, recursive=True):
            a = lib.load_asset(p)
            if isinstance(a, u.StaticMesh):
                tris = a.get_num_triangles(0)
                if importing:
                    nanite(a, tris >= NANITE_MIN_TRIS)
                    lib.save_loaded_asset(a, only_if_is_dirty=False)
                b = a.get_bounds()
                meshes.append({"asset": a.get_path_name(), "triangles": tris,
                               "size": [round(b.box_extent.x * 2), round(b.box_extent.y * 2), round(b.box_extent.z * 2)]})
        report["props"][aid] = meshes
        u.log(f"CIRE_ARENA_PROP {aid} meshes={len(meshes)}")

    # Trees from FBX (legacy importer keeps the leaf cards): bark instances plus alpha-masked leaves, Nanite voxelized.
    for asset in manifest["assets"]:
        if asset["kind"] != "model-fbx":
            continue
        aid = asset["id"]
        dest = f"{PKG}/Trees/{aid}"
        mesh_path = f"{dest}/SM_{aid}"
        if "trees" in only or not lib.does_asset_exist(mesh_path):
            ensure(dest)
            u.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
            src_dir = ROOT / Path(asset["entry"]).parent
            texmap = {}
            for f in sorted((src_dir / "textures").iterdir()):
                stem = f.stem.replace(f"{aid}_", "").replace("_1k", "")
                name = f"T_{aid}_{stem}"
                import_file(f, dest, name)
                tex = lib.load_asset(f"{dest}/{name}")
                if "nor" in stem:
                    tex.set_editor_property("srgb", False)
                    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_NORMALMAP)
                elif any(k in stem for k in ("alpha", "rough", "arm", "ao")):
                    tex.set_editor_property("srgb", False)
                    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_MASKS)
                lib.save_loaded_asset(tex, only_if_is_dirty=False)
                texmap[stem] = tex
            opts = u.FbxImportUI()
            for k, v in {"import_mesh": True, "import_as_skeletal": False, "import_animations": False, "import_materials": False,
                         "import_textures": False, "mesh_type_to_import": u.FBXImportType.FBXIT_STATIC_MESH,
                         "automated_import_should_detect_type": False}.items():
                opts.set_editor_property(k, v)
            data = opts.get_editor_property("static_mesh_import_data")
            for k, v in {"combine_meshes": True, "generate_lightmap_u_vs": False, "auto_generate_collision": False,
                         "convert_scene": True, "convert_scene_unit": True}.items():
                data.set_editor_property(k, v)
            import_file(ROOT / asset["entry"], dest, f"SM_{aid}", opts)
            mesh = lib.load_asset(mesh_path)
            assert isinstance(mesh, u.StaticMesh), mesh_path
            def mi_for(kind):
                if kind == "leaves":
                    mi = instance(f"MI_Arena_Leaves_{aid}", masters["M_ArenaFoliage"], "Trees")
                    texs = {"LeafMap": texmap["leaves_diff"], "AlphaMap": texmap["leaves_alpha"]}
                    if "leaves_nor_gl" in texmap:
                        texs["LeafNormal"] = texmap["leaves_nor_gl"]
                    setp(mi, {"WindAmplitude": 2.0, "UseAlpha": 1.0, "InstanceVariation": 0.25}, {"LeafTint": (1.3, 1.55, 0.95), "Backlight": (0.40, 0.58, 0.13)}, texs)
                    return mi
                prefix = {"branch": "branch_", "branches": "branches_"}.get(kind, "")
                mi = instance(f"MI_Arena_Bark_{aid}_{kind}", masters["M_ArenaSurface"], "Trees")
                texs = {"BaseColorMap": texmap.get(prefix + "diff", texmap.get("diff"))}
                if texmap.get(prefix + "nor_gl") or texmap.get("nor_gl"):
                    texs["NormalMap"] = texmap.get(prefix + "nor_gl", texmap.get("nor_gl"))
                if texmap.get(prefix + "arm") or texmap.get("arm"):
                    texs["ARMMap"] = texmap.get(prefix + "arm", texmap.get("arm"))
                setp(mi, {"Tiling": 1.0, "MacroVariation": 0.0, "Saturation": 1.0, "RoughnessMul": 1.0}, {"Tint": (1, 1, 1)}, texs)
                return mi
            for i, slot in enumerate(mesh.get_editor_property("static_materials")):
                name = str(slot.get_editor_property("imported_material_slot_name")).lower()
                kind = "leaves" if "lea" in name else "branches" if "branches" in name else "branch" if "branch" in name else "trunk"
                mesh.set_material(i, mi_for(kind))
                report.setdefault("trees", {}).setdefault(aid, []).append([i, name, kind])
            nanite(mesh, True)
            ns = mesh.get_editor_property("nanite_settings")
            ns.set_editor_property("shape_preservation", u.NaniteShapePreservation.VOXELIZE)
            mesh.set_editor_property("nanite_settings", ns)
            lib.save_loaded_asset(mesh, only_if_is_dirty=False)
        mesh = lib.load_asset(mesh_path) if lib.does_asset_exist(mesh_path) else None
        if mesh:
            b = mesh.get_bounds()
            report.setdefault("treeMeshes", {})[aid] = {"asset": mesh.get_path_name(), "triangles": mesh.get_num_triangles(0),
                                                       "size": [round(b.box_extent.x * 2), round(b.box_extent.y * 2), round(b.box_extent.z * 2)]}

    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    u.log(f"CIRE_ARENA_IMPORT_PASS textures={len(report['textures'])} materials={len(report['materials'])} meshes={len(report['meshes'])} props={len(report['props'])}")


# ================================================================== launcher
def launch(args):
    for path in (ROOT / "Content/Arenas").rglob("*"):
        if path.is_file():
            path.chmod(path.stat().st_mode | stat.S_IWRITE)
    log = ROOT / "Saved/Logs/ArenaImport.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    only = args[args.index("--only") + 1] if "--only" in args else ""
    meshes = args[args.index("--meshes") + 1] if "--meshes" in args else ""
    env = dict(os.environ, CIRE_ARENA_ONLY=only, CIRE_ARENA_MESHES=meshes)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"),
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-run=pythonscript",
               f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
    with log.with_name("ArenaImport-console.log").open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text(encoding="utf-8", errors="replace")
    ok = result.returncode == 0 and "CIRE_ARENA_IMPORT_PASS" in text and "Failed to compile Material" not in text
    errors = [l for l in text.splitlines() if ("Error" in l and ("Python" in l or "Traceback" in l or "LogPython" in l)) or "Failed to compile" in l or "[SM5]" in l]
    print(f"exit={result.returncode} pass={ok} log={log}")
    for line in errors[:60]:
        print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        sys.exit(launch(sys.argv[1:]))
    else:
        run(unreal, set(filter(None, os.environ.get("CIRE_ARENA_ONLY", "").split(","))))
