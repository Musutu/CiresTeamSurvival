"""Import the medieval town content into /Game/Environment/Town.

Run with the regular Python to launch an unattended UnrealEditor-Cmd commandlet
against this project; inside Unreal it performs the import. Inputs:
  Saved/TownSources             (Tools/FetchTownAssets.py, Poly Haven CC0)
  Art/Environment/Town/Meshes   (Tools/BuildTownMeshes.py, original geometry)
Outputs only under /Game/Environment/Town and writes
Art/Environment/Town/ImportReport.json. Nothing outside that folder is modified.

Usage: python Tools/ImportTownContent.py [--only textures,materials,meshes,props,sky]
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PKG = "/Game/Environment/Town"
SRC = ROOT / "Saved/TownSources"
REPORT = ROOT / "Art/Environment/Town/ImportReport.json"

# Surface material instances: slot -> (texture id, tiling metres per repeat, tint, roughness mul, normal strength, macro)
SURFACES = {  # slot: (texture, metres per repeat, tint, roughness mul, normal strength, macro variation, saturation)
    "Plaster":   ("white_rough_plaster", 2.2, (0.74, 0.70, 0.63), 1.0, 1.0, 0.45, 0.7),
    "Timber":    ("rough_wood", 1.2, (0.40, 0.32, 0.26), 1.0, 1.0, 0.2, 0.7),
    "Planks":    ("weathered_planks", 1.8, (0.58, 0.52, 0.46), 1.0, 1.0, 0.3, 0.6),
    "Stone":     ("castle_wall_slates", 2.2, (0.78, 0.76, 0.74), 1.0, 1.1, 0.35, 0.45),
    "Castle":    ("stone_wall_04", 2.6, (0.80, 0.80, 0.82), 1.0, 1.1, 0.4, 0.5),
    "RoofSlate": ("roof_slates_02", 2.0, (0.52, 0.55, 0.60), 1.0, 1.2, 0.35, 0.6),
    "RoofClay":  ("clay_roof_tiles_02", 2.0, (0.62, 0.50, 0.45), 1.0, 1.2, 0.35, 0.65),
    "Thatch":    ("thatch_roof_angled", 2.6, (0.58, 0.52, 0.43), 1.0, 1.0, 0.3, 0.7),
    "Iron":      ("rust_coarse_01", 1.0, (0.30, 0.28, 0.27), 1.0, 0.8, 0.1, 0.5),
    "Canvas":    ("fabric_pattern_07", 1.4, (0.78, 0.70, 0.66), 1.0, 0.8, 0.2, 0.8),
    "CanvasAlt": ("fabric_pattern_05", 1.4, (0.72, 0.72, 0.76), 1.0, 0.8, 0.2, 0.8),
    "Sack":      ("fabric_pattern_05", 0.8, (0.45, 0.38, 0.30), 1.1, 0.8, 0.0, 0.3),
    "BannerCloth": ("fabric_pattern_05", 1.2, (0.42, 0.06, 0.05), 1.0, 0.6, 0.0, 1.0),
    "LogEnd":    ("rough_wood", 0.6, (0.55, 0.42, 0.30), 1.0, 1.0, 0.0, 0.8),
    "Bark":      ("rough_wood", 0.9, (0.26, 0.22, 0.19), 1.0, 1.2, 0.3, 0.5),
}
# World-aligned (triplanar) instances for scaled engine-cube geometry in ACireWorld.
WORLD = {
    "Cobble":    ("cobblestone_floor_08", 2.2, (0.50, 0.49, 0.48), 0.9, 1.2, 0.45, 0.45),
    "Plaza":     ("cobblestone_large_01", 3.2, (0.52, 0.50, 0.48), 1.0, 1.1, 0.45, 0.45),
    "Flagstone": ("monastery_stone_floor", 3.0, (0.58, 0.57, 0.56), 1.0, 1.0, 0.35, 0.45),
    "Ground":    ("brown_mud_leaves_01", 4.0, (0.55, 0.52, 0.47), 1.0, 1.0, 0.5, 0.6),
    "Field":     ("rocky_trail", 5.0, (0.52, 0.50, 0.47), 1.0, 1.0, 0.55, 0.55),
    "CastleW":   ("stone_wall_04", 3.0, (0.70, 0.70, 0.72), 1.0, 1.0, 0.45, 0.5),
    "StoneW":    ("castle_wall_slates", 2.4, (0.70, 0.69, 0.68), 1.0, 1.0, 0.4, 0.45),
    "CliffW":    ("rock_face_03", 9.0, (0.50, 0.47, 0.45), 1.0, 1.4, 0.6, 0.45),
    "PlanksW":   ("weathered_planks", 2.0, (0.60, 0.53, 0.46), 1.0, 1.0, 0.3, 0.6),
    "TimberW":   ("rough_wood", 1.4, (0.40, 0.31, 0.24), 1.0, 1.0, 0.2, 0.6),
}
FLATS = {  # slot -> (base colour, roughness, metallic, emissive colour, emissive strength, flicker)
    "GlassLit":  ((0.05, 0.03, 0.01), 0.35, 0.0, (1.0, 0.52, 0.18), 7.0, 0.08),
    "GlassDark": ((0.012, 0.014, 0.018), 0.18, 0.0, (0, 0, 0), 0.0, 0.0),
    "Flame":     ((0.2, 0.08, 0.02), 0.6, 0.0, (1.0, 0.45, 0.12), 26.0, 0.18),
    "Water":     ((0.012, 0.022, 0.025), 0.06, 0.0, (0, 0, 0), 0.0, 0.0),
    "Ember":     ((0.1, 0.02, 0.0), 0.8, 0.0, (1.0, 0.18, 0.05), 5.0, 0.25),
}
NANITE_MIN_TRIS = 1500
PROPS_NO_COLLISION = {"island_tree_02", "shrub_02", "shrub_04", "fern_02", "food_apple_01", "kite_shield", "wooden_axe",
                      "jug_01", "ceramic_pot", "Lantern_01", "wooden_lantern_01", "dead_tree_trunk_02"}


# ------------------------------------------------------------------ Unreal side
def run(u, only):
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    edit = u.MaterialEditingLibrary
    report = {"textures": {}, "materials": {}, "meshes": {}, "props": {}, "sky": {}}

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
    tex_ids = sorted({v[0] for v in list(SURFACES.values()) + list(WORLD.values())})
    textures = {}
    for tid in tex_ids:
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
                    tex.set_editor_property("flip_green_channel", False)  # materials treat these as OpenGL normals
                else:
                    tex.set_editor_property("srgb", False)
                    tex.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_MASKS)
                tex.set_editor_property("lod_group", u.TextureGroup.TEXTUREGROUP_WORLD)
                lib.save_loaded_asset(tex, only_if_is_dirty=False)
            maps[kind] = lib.load_asset(path)
            assert maps[kind], path
        textures[tid] = maps
        report["textures"][tid] = [m.get_path_name() for m in maps.values()]

    # ------------------------------------------------------------ materials
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
        for name, _ in inputs:
            ci = u.CustomInput()
            ci.set_editor_property("input_name", name)
            ins.append(ci)
        n = node(mat, "Custom", code=code, output_type=out, inputs=ins)
        for name, src in inputs:
            assert edit.connect_material_expressions(src, "", n, name), name
        return n

    def scalar(mat, name, value):
        return node(mat, "ScalarParameter", parameter_name=name, default_value=value)

    def vector(mat, name, value):
        return node(mat, "VectorParameter", parameter_name=name, default_value=u.LinearColor(*value, 1.0))

    def finish(mat, path_list):
        edit.layout_material_expressions(mat)
        edit.recompile_material(mat)
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False)
        path_list.append(mat.get_path_name())

    masters = {}
    created = []
    any_tex = textures[tex_ids[0]]
    if "materials" in only or not lib.does_asset_exist(f"{PKG}/Materials/M_TownSurface"):
        # UV based surface: mesh UVs are metres, Tiling = repeats per metre.
        mat = new_material("M_TownSurface")
        uv = node(mat, "TextureCoordinate")
        tiling = scalar(mat, "Tiling", 0.5)
        tb = node(mat, "TextureObjectParameter", parameter_name="BaseColorMap", texture=any_tex["D"])
        tn = node(mat, "TextureObjectParameter", parameter_name="NormalMap", texture=any_tex["N"])
        ta = node(mat, "TextureObjectParameter", parameter_name="ARMMap", texture=any_tex["ARM"])
        tint = vector(mat, "Tint", (1, 1, 1))
        macro = scalar(mat, "MacroVariation", 0.3)
        nstr = scalar(mat, "NormalStrength", 1.0)
        rmul = scalar(mat, "RoughnessMul", 1.0)
        sat = scalar(mat, "Saturation", 1.0)
        base = custom(mat, """float2 t=UV*Tiling;
float3 c=Texture2DSample(Tex,TexSampler,t).rgb;
float m=dot(Texture2DSample(Tex,TexSampler,t*0.137+0.31).rgb,float3(.3,.59,.11));
c*=lerp(1.0,saturate(0.5+m*1.5),Macro);
c=lerp(dot(c,float3(.3,.59,.11)).xxx,c,Sat);
return c*Tint;""", [("UV", uv), ("Tiling", tiling), ("Tex", tb), ("Tint", tint), ("Macro", macro), ("Sat", sat)])
        nrm = custom(mat, """float2 t=UV*Tiling; float2 xy=Texture2DSample(Tex,TexSampler,t).rg*2-1;
xy*=Strength; xy.y=-xy.y; // OpenGL source -> Unreal tangent space
return normalize(float3(xy,sqrt(saturate(1-dot(xy,xy)))+0.001));""", [("UV", uv), ("Tiling", tiling), ("Tex", tn), ("Strength", nstr)])
        arm = custom(mat, "return Texture2DSample(Tex,TexSampler,UV*Tiling).rgb;", [("UV", uv), ("Tiling", tiling), ("Tex", ta)])
        rough = custom(mat, "return saturate(A.g*R);", [("A", arm), ("R", rmul)], u.CustomMaterialOutputType.CMOT_FLOAT1)
        ao = custom(mat, "return A.r;", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1)
        metal = custom(mat, "return A.b;", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1)
        edit.connect_material_property(base, "", u.MaterialProperty.MP_BASE_COLOR)
        edit.connect_material_property(nrm, "", u.MaterialProperty.MP_NORMAL)
        edit.connect_material_property(rough, "", u.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(ao, "", u.MaterialProperty.MP_AMBIENT_OCCLUSION)
        edit.connect_material_property(metal, "", u.MaterialProperty.MP_METALLIC)
        finish(mat, created)

        # World aligned triplanar: for non-uniformly scaled engine cubes.
        mat = new_material("M_TownWorld")
        mat.set_editor_property("tangent_space_normal", False)
        pos = node(mat, "WorldPosition")
        nrmws = node(mat, "VertexNormalWS")
        size = scalar(mat, "WorldSizeCm", 300.0)
        tb = node(mat, "TextureObjectParameter", parameter_name="BaseColorMap", texture=any_tex["D"])
        tn = node(mat, "TextureObjectParameter", parameter_name="NormalMap", texture=any_tex["N"])
        ta = node(mat, "TextureObjectParameter", parameter_name="ARMMap", texture=any_tex["ARM"])
        tint = vector(mat, "Tint", (1, 1, 1))
        macro = scalar(mat, "MacroVariation", 0.3)
        nstr = scalar(mat, "NormalStrength", 1.0)
        rmul = scalar(mat, "RoughnessMul", 1.0)
        sat = scalar(mat, "Saturation", 1.0)
        prelude = """float3 Nn=normalize(N); float3 w=pow(abs(Nn),4); w/=dot(w,1);
float3 p=P/Size; float2 ux=float2(p.y,-p.z), uy=float2(p.x,-p.z), uz=float2(p.x,p.y);
"""
        base = custom(mat, prelude + """float3 c=Texture2DSample(Tex,TexSampler,ux).rgb*w.x+Texture2DSample(Tex,TexSampler,uy).rgb*w.y+Texture2DSample(Tex,TexSampler,uz).rgb*w.z;
float m=dot(Texture2DSample(Tex,TexSampler,uz*0.113+0.37).rgb,float3(.3,.59,.11));
c*=lerp(1.0,saturate(0.5+m*1.5),Macro);
c=lerp(dot(c,float3(.3,.59,.11)).xxx,c,Sat);
return c*Tint;""", [("P", pos), ("N", nrmws), ("Size", size), ("Tex", tb), ("Tint", tint), ("Macro", macro), ("Sat", sat)])
        nrm = custom(mat, prelude + """float2 nx=Texture2DSample(Tex,TexSampler,ux).rg*2-1;
float2 ny=Texture2DSample(Tex,TexSampler,uy).rg*2-1;
float2 nz=Texture2DSample(Tex,TexSampler,uz).rg*2-1;
// OpenGL normals: perturbation = x*dP/du - y*dP/dv for each projection plane
float3 px=nx.x*float3(0,1,0)-nx.y*float3(0,0,-1);
float3 py=ny.x*float3(1,0,0)-ny.y*float3(0,0,-1);
float3 pz=nz.x*float3(1,0,0)-nz.y*float3(0,1,0);
return normalize(Nn+(px*w.x+py*w.y+pz*w.z)*Strength);""", [("P", pos), ("N", nrmws), ("Size", size), ("Tex", tn), ("Strength", nstr)])
        arm = custom(mat, prelude + "return Texture2DSample(Tex,TexSampler,ux).rgb*w.x+Texture2DSample(Tex,TexSampler,uy).rgb*w.y+Texture2DSample(Tex,TexSampler,uz).rgb*w.z;",
                     [("P", pos), ("N", nrmws), ("Size", size), ("Tex", ta)])
        rough = custom(mat, "return saturate(A.g*R);", [("A", arm), ("R", rmul)], u.CustomMaterialOutputType.CMOT_FLOAT1)
        ao = custom(mat, "return A.r;", [("A", arm)], u.CustomMaterialOutputType.CMOT_FLOAT1)
        edit.connect_material_property(base, "", u.MaterialProperty.MP_BASE_COLOR)
        edit.connect_material_property(nrm, "", u.MaterialProperty.MP_NORMAL)
        edit.connect_material_property(rough, "", u.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(ao, "", u.MaterialProperty.MP_AMBIENT_OCCLUSION)
        finish(mat, created)

        # Flat / emissive (window glow, flames, water, dark glass).
        mat = new_material("M_TownFlat")
        bc = vector(mat, "BaseColor", (0.1, 0.1, 0.1))
        ro = scalar(mat, "Roughness", 0.5)
        me = scalar(mat, "Metallic", 0.0)
        ec = vector(mat, "EmissiveColor", (0, 0, 0))
        es = scalar(mat, "EmissiveStrength", 0.0)
        fl = scalar(mat, "Flicker", 0.0)
        t = node(mat, "Time")
        pos = node(mat, "WorldPosition")
        emi = custom(mat, "float f=1+Fl*(sin(T*7.3+P.x*0.013)*0.6+sin(T*13.1+P.y*0.021)*0.4);return C*S*f;",
                     [("C", ec), ("S", es), ("Fl", fl), ("T", t), ("P", pos)])
        edit.connect_material_property(bc, "", u.MaterialProperty.MP_BASE_COLOR)
        edit.connect_material_property(ro, "", u.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(me, "", u.MaterialProperty.MP_METALLIC)
        edit.connect_material_property(emi, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
        finish(mat, created)
    report["materials"]["masters"] = created or ["(kept)"]
    for name in ("M_TownSurface", "M_TownWorld", "M_TownFlat"):
        masters[name] = lib.load_asset(f"{PKG}/Materials/{name}")
        assert masters[name], name

    def instance(name, parent):
        path = f"{PKG}/Materials/{name}"
        mi = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, f"{PKG}/Materials", u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi, parent)
        return mi

    instances = {}
    for slot, (tid, metres, tint, rmul, nstr, macro, satv) in SURFACES.items():
        mi = instance(f"MI_Town_{slot}", masters["M_TownSurface"])
        maps = textures[tid]
        edit.set_material_instance_texture_parameter_value(mi, "BaseColorMap", maps["D"])
        edit.set_material_instance_texture_parameter_value(mi, "NormalMap", maps["N"])
        edit.set_material_instance_texture_parameter_value(mi, "ARMMap", maps["ARM"])
        edit.set_material_instance_scalar_parameter_value(mi, "Tiling", 1.0 / metres)
        edit.set_material_instance_scalar_parameter_value(mi, "NormalStrength", nstr)
        edit.set_material_instance_scalar_parameter_value(mi, "RoughnessMul", rmul)
        edit.set_material_instance_scalar_parameter_value(mi, "MacroVariation", macro)
        edit.set_material_instance_scalar_parameter_value(mi, "Saturation", satv)
        edit.set_material_instance_vector_parameter_value(mi, "Tint", u.LinearColor(*tint, 1))
        lib.save_loaded_asset(mi, only_if_is_dirty=False)
        instances[slot] = mi
    for slot, (tid, metres, tint, rmul, nstr, macro, satv) in WORLD.items():
        mi = instance(f"MI_TownW_{slot}", masters["M_TownWorld"])
        maps = textures[tid]
        edit.set_material_instance_texture_parameter_value(mi, "BaseColorMap", maps["D"])
        edit.set_material_instance_texture_parameter_value(mi, "NormalMap", maps["N"])
        edit.set_material_instance_texture_parameter_value(mi, "ARMMap", maps["ARM"])
        edit.set_material_instance_scalar_parameter_value(mi, "WorldSizeCm", metres * 100)
        edit.set_material_instance_scalar_parameter_value(mi, "NormalStrength", nstr)
        edit.set_material_instance_scalar_parameter_value(mi, "RoughnessMul", rmul)
        edit.set_material_instance_scalar_parameter_value(mi, "MacroVariation", macro)
        edit.set_material_instance_scalar_parameter_value(mi, "Saturation", satv)
        edit.set_material_instance_vector_parameter_value(mi, "Tint", u.LinearColor(*tint, 1))
        lib.save_loaded_asset(mi, only_if_is_dirty=False)
    for slot, (bc, ro, me, ec, es, fl) in FLATS.items():
        mi = instance(f"MI_Town_{slot}", masters["M_TownFlat"])
        edit.set_material_instance_vector_parameter_value(mi, "BaseColor", u.LinearColor(*bc, 1))
        edit.set_material_instance_scalar_parameter_value(mi, "Roughness", ro)
        edit.set_material_instance_scalar_parameter_value(mi, "Metallic", me)
        edit.set_material_instance_vector_parameter_value(mi, "EmissiveColor", u.LinearColor(*ec, 1))
        edit.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", es)
        edit.set_material_instance_scalar_parameter_value(mi, "Flicker", fl)
        lib.save_loaded_asset(mi, only_if_is_dirty=False)
        instances[slot] = mi
    report["materials"]["instances"] = sorted(list(SURFACES) + [f"W_{k}" for k in WORLD] + list(FLATS))

    # ------------------------------------------------------------ sky
    if "sky" in only or not lib.does_asset_exist(f"{PKG}/Sky/M_TownSky"):
        ensure(f"{PKG}/Sky")
        hdr = next((SRC / "HDRI").glob("*.hdr"))
        import_file(hdr, f"{PKG}/Sky", "T_TownSkyCube")
        cube = lib.load_asset(f"{PKG}/Sky/T_TownSkyCube")
        assert cube, "sky cube"
        mat = new_material("M_TownSky", "Sky")
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("is_sky", True)
        cam = node(mat, "CameraVectorWS")
        samp = node(mat, "TextureSampleParameterCube", parameter_name="Sky", texture=cube)
        dirn = custom(mat, "return -C;", [("C", cam)])
        edit.connect_material_expressions(dirn, "", samp, "UVs")
        bright = scalar(mat, "Brightness", 1.0)
        tintv = vector(mat, "SkyTint", (1, 1, 1))
        emi = custom(mat, "return S.rgb*B*T;", [("S", samp), ("B", bright), ("T", tintv)])
        edit.connect_material_property(emi, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
        finish(mat, created)
        report["sky"] = {"cube": cube.get_path_name(), "material": mat.get_path_name(), "source": hdr.name}

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

    def finalize_mesh(mesh, collision, nanite_ok=True):
        if collision:
            body = mesh.get_editor_property("body_setup")
            if body:
                body.set_editor_property("collision_trace_flag", u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        tris = mesh.get_num_triangles(0)
        if nanite_ok and tris >= NANITE_MIN_TRIS:
            ns = mesh.get_editor_property("nanite_settings")
            ns.set_editor_property("enabled", True)
            mesh.set_editor_property("nanite_settings", ns)
        return tris

    if "meshes" in only or not lib.does_asset_exist(f"{PKG}/Meshes/SM_Castle_Keep"):
        ensure(f"{PKG}/Meshes")
        meta = json.loads((ROOT / "Art/Environment/Town/Meshes/TownMeshes.json").read_text())
        for row in meta["meshes"]:
            name = row["name"]
            import_file(ROOT / "Art/Environment/Town/Meshes" / f"{name}.obj", f"{PKG}/Meshes", name, mesh_options())
            mesh = lib.load_asset(f"{PKG}/Meshes/{name}")
            assert isinstance(mesh, u.StaticMesh), name
            mats = mesh.get_editor_property("static_materials")
            for i, slot in enumerate(mats):
                key = str(slot.get_editor_property("imported_material_slot_name"))
                mi = instances.get(key)
                if not mi:
                    raise RuntimeError(f"{name}: unknown material slot {key}")
                mesh.set_material(i, mi)
            tris = finalize_mesh(mesh, collision=True)
            b = mesh.get_bounds()
            assert lib.save_loaded_asset(mesh, only_if_is_dirty=False), name
            report["meshes"][name] = {"triangles": tris, "extent": [round(b.box_extent.x), round(b.box_extent.y), round(b.box_extent.z)],
                                      "nanite": tris >= NANITE_MIN_TRIS}

    # ------------------------------------------------------------ Poly Haven models
    importing = "props" in only or not lib.does_directory_exist(f"{PKG}/Props/Barrel_01")
    manifest = json.loads((ROOT / "Art/Environment/Town/SourceManifest.json").read_text())
    for asset in manifest["assets"]:
            if asset["kind"] != "model":
                continue
            aid = asset["id"]
            dest = f"{PKG}/Props/{aid}"
            paths = []
            if importing:
                ensure(dest)
                paths = import_file(ROOT / asset["entry"], dest)
            meshes = []
            for p in lib.list_assets(dest, recursive=True):
                a = lib.load_asset(p)
                if isinstance(a, u.StaticMesh):
                    tris = finalize_mesh(a, collision=aid not in PROPS_NO_COLLISION) if importing else a.get_num_triangles(0)
                    b = a.get_bounds()
                    if importing:
                        lib.save_loaded_asset(a, only_if_is_dirty=False)
                    meshes.append({"asset": a.get_path_name(), "triangles": tris,
                                   "extent": [round(b.box_extent.x), round(b.box_extent.y), round(b.box_extent.z)],
                                   "origin": [round(b.origin.x), round(b.origin.y), round(b.origin.z)]})
            report["props"][aid] = {"imported": len(paths), "meshes": meshes}
            u.log(f"CIRE_TOWN_PROP {aid} meshes={len(meshes)}")

    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    u.log(f"CIRE_TOWN_IMPORT_PASS textures={len(report['textures'])} meshes={len(report['meshes'])} props={len(report['props'])}")


# ------------------------------------------------------------------ launcher
def launch(args):
    # LFS 'lockable' checkouts are read-only; the importer must be able to overwrite its own packages.
    import stat
    for path in (ROOT / "Content/Environment/Town").rglob("*"):
        if path.is_file():
            path.chmod(path.stat().st_mode | stat.S_IWRITE)
    log = ROOT / "Saved/Logs/TownImport.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    only = args[args.index("--only") + 1] if "--only" in args else ""
    env = dict(os.environ, CIRE_TOWN_ONLY=only)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"),
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-run=pythonscript",
               f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
    with log.with_name("TownImport-console.log").open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text(encoding="utf-8", errors="replace")
    ok = result.returncode == 0 and "CIRE_TOWN_IMPORT_PASS" in text
    errors = [l for l in text.splitlines() if "Error" in l and ("Python" in l or "Traceback" in l or "LogPython" in l)]
    print(f"exit={result.returncode} pass={ok} log={log}")
    for line in errors[:40]:
        print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        sys.exit(launch(sys.argv[1:]))
    else:
        run(unreal, set(filter(None, os.environ.get("CIRE_TOWN_ONLY", "").split(","))))
