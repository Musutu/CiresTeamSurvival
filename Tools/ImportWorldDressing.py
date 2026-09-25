"""Import the world-dressing content into /Game/Free/Dressing (world-dressing).

Run with the regular Python to launch an unattended UnrealEditor-Cmd commandlet against this project; inside
Unreal it performs the import. Inputs:
  Saved/DressingSources               (Tools/FetchWorldDressing.py: Poly Haven + ambientCG, CC0)
  Art/Environment/Dressing/Meshes     (Tools/BuildDressingMeshes.py, original geometry)
Outputs only under /Game/Free/Dressing and writes Art/Environment/Dressing/ImportReport.json.
Town material instances (/Game/Environment/Town/Materials) are referenced, never modified.

Usage: python Tools/ImportWorldDressing.py [--only textures,materials,meshes,props]
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PKG = "/Game/Free/Dressing"
TOWN = "/Game/Environment/Town/Materials"
SRC = ROOT / "Saved/DressingSources"
ART = ROOT / "Art/Environment/Dressing"
REPORT = ART / "ImportReport.json"
NANITE_MIN_TRIS = 1500

# ambientCG texture sets: id -> maps to import (suffix in the file name -> role)
TEXTURES = {
    "LeafSet017": {"Color": "D", "Opacity": "A", "NormalGL": "N"},
    "Leaking003": {"Color": "D", "Opacity": "A"},
    "ScatteredLeaves007": {"Color": "D", "Opacity": "A", "NormalGL": "N"},
}
# OBJ material slot -> existing town instance (reused as-is)
TOWN_SLOTS = {k: f"{TOWN}/MI_Town_{k}.MI_Town_{k}" for k in
              ("Iron", "Planks", "Timber", "Canvas", "CanvasAlt", "Sack", "BannerCloth", "Bark", "Thatch", "Flame", "Stone")}
# Poly Haven props without collision (small clutter, plants): everything else gets per-poly collision.
PROPS_NO_COLLISION = {"CheeseBox_01", "wooden_bowl_02", "carved_wooden_plate", "wooden_cutting_board", "brass_goblets",
                      "brass_pot_01", "ceramic_vase_01", "ceramic_vase_03", "antique_ceramic_vase_01", "yellow_onion",
                      "sweet_potato", "food_pears_asian_01", "food_pomegranate_01", "wine_bottles_01", "wooden_broom",
                      "sledgehammer_01", "hatchet", "handsaw_wood", "wooden_axe_02", "wooden_hammer_01", "ornate_war_hammer",
                      "ornate_medieval_mace", "antique_estoc", "lantern_chandelier_01", "planter_pot_clay", "nettle_plant",
                      "weed_plant_02", "shrub_sorrel_01", "dry_branches_medium_01", "street_rat", "wooden_bucket_02",
                      "folding_wooden_stool", "wooden_stool_01", "wooden_ladder_02", "planter_box_01", "planter_box_02",
                      "planter_box_03"}
MESH_COLLISION = {"SM_Dress_WeaponRack", "SM_Dress_HayPile"}
# Nanite does not draw translucent sections (glass): keep these on the regular mesh path.
NO_NANITE = {"wine_bottles_01", "lantern_chandelier_01"}


def run(u, only):
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    edit = u.MaterialEditingLibrary
    report = {"textures": {}, "materials": [], "meshes": {}, "props": {}}

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
    tex = {}
    for tid, maps in TEXTURES.items():
        dest = f"{PKG}/Textures/{tid}"
        tex[tid] = {}
        for suffix, role in maps.items():
            name = f"T_{tid}_{role}"
            path = f"{dest}/{name}"
            if "textures" in only or not lib.does_asset_exist(path):
                ensure(dest)
                source = next((SRC / "AmbientCG" / tid).glob(f"{tid}_*_{suffix}.jpg"))
                import_file(source, dest, name)
                t = lib.load_asset(path)
                assert t, path
                if role == "D":
                    t.set_editor_property("srgb", True)
                elif role == "N":
                    t.set_editor_property("srgb", False)
                    t.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_NORMALMAP)
                    t.set_editor_property("flip_green_channel", True)  # OpenGL source -> Unreal (DirectX) convention
                else:
                    t.set_editor_property("srgb", False)
                    t.set_editor_property("compression_settings", u.TextureCompressionSettings.TC_GRAYSCALE)
                t.set_editor_property("lod_group", u.TextureGroup.TEXTUREGROUP_WORLD)
                lib.save_loaded_asset(t, only_if_is_dirty=False)
            tex[tid][role] = lib.load_asset(path)
        report["textures"][tid] = [t.get_path_name() for t in tex[tid].values()]

    # ------------------------------------------------------------ materials
    def new_material(name):
        path = f"{PKG}/Materials/{name}"
        ensure(f"{PKG}/Materials")
        mat = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, f"{PKG}/Materials", u.Material, u.MaterialFactoryNew())
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

    def finish(mat):
        edit.layout_material_expressions(mat)
        edit.recompile_material(mat)
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False)
        report["materials"].append(mat.get_path_name())

    F1 = u.CustomMaterialOutputType.CMOT_FLOAT1
    ivy = tex["LeafSet017"]
    if "materials" in only or not lib.does_asset_exist(f"{PKG}/Materials/M_DressCard"):
        # Masked two-sided cards (ivy leaves, leaf litter, grime streaks). Grime dithers its opacity.
        mat = new_material("M_DressCard")
        mat.set_editor_property("blend_mode", u.BlendMode.BLEND_MASKED)
        mat.set_editor_property("two_sided", True)
        mat.set_editor_property("dithered_lod_transition", True)
        uv = node(mat, "TextureCoordinate")
        col = node(mat, "TextureSampleParameter2D", parameter_name="ColorMap", texture=ivy["D"])
        opa = node(mat, "TextureSampleParameter2D", parameter_name="OpacityMap", texture=ivy["A"], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        nrm = node(mat, "TextureSampleParameter2D", parameter_name="NormalMap", texture=ivy["N"], sampler_type=u.MaterialSamplerType.SAMPLERTYPE_NORMAL)
        for s in (col, opa, nrm):
            edit.connect_material_expressions(uv, "", s, "UVs")
        tint = vector(mat, "Tint", (1, 1, 1))
        rough = scalar(mat, "Roughness", .7)
        nstr = scalar(mat, "NormalStrength", 1.0)
        strength = scalar(mat, "OpacityStrength", 1.0)
        dither = scalar(mat, "Dither", 0.0)
        radial = scalar(mat, "RadialFade", 0.0)
        pos = node(mat, "WorldPosition")
        base = custom(mat, "float v=frac(sin(dot(floor(P.xy/180),float2(12.9898,78.233)))*43758.5453);return C.rgb*T*lerp(.8,1.15,v);",
                      [("C", col), ("T", tint), ("P", pos)])
        n2 = custom(mat, "return normalize(float3(N.xy*S,max(N.z,.05)));", [("N", nrm), ("S", nstr)])
        # interleaved-gradient screen dither (soft grime edges without translucency)
        mask = custom(mat, "float a=saturate(A.r*S);float2 px=Parameters.SvPosition.xy;"
                      "float2 c=UV-0.5;float r=length(c)*2+0.18*sin(atan2(c.y,c.x)*5+UV.x*3);a*=lerp(1,saturate((1-r)*2.5),R);"
                      "float z=frac(52.9829189*frac(dot(px,float2(0.06711056,0.00583715))));return D>0.5?(a>z?1:0):(a>0.5?1:0);",
                      [("A", opa), ("S", strength), ("D", dither), ("UV", uv), ("R", radial)], F1)
        edit.connect_material_property(base, "", u.MaterialProperty.MP_BASE_COLOR)
        edit.connect_material_property(n2, "", u.MaterialProperty.MP_NORMAL)
        edit.connect_material_property(rough, "", u.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(mask, "", u.MaterialProperty.MP_OPACITY_MASK)
        finish(mat)

        # Standing water: near-black, mirror-smooth, slow ripples; the edge darkens the cobbles around it.
        mat = new_material("M_DressPuddle")
        pos = node(mat, "WorldPosition")
        t = node(mat, "Time")
        col = vector(mat, "WaterColor", (.012, .014, .016))
        rough = scalar(mat, "Roughness", .03)
        ripple = scalar(mat, "Ripple", .06)
        n = custom(mat, """float2 p=P.xy*0.035;
float a=sin(p.x*3.1+T*1.3)+sin(p.y*2.7-T*1.1)+sin((p.x+p.y)*4.3+T*1.7);
float b=cos(p.x*2.3-T*0.9)+cos(p.y*3.7+T*1.4)+cos((p.x-p.y)*3.9-T*1.2);
return normalize(float3(a*R,b*R,1));""", [("P", pos), ("T", t), ("R", ripple)])
        edit.connect_material_property(col, "", u.MaterialProperty.MP_BASE_COLOR)
        edit.connect_material_property(rough, "", u.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(n, "", u.MaterialProperty.MP_NORMAL)
        finish(mat)

        # Chimney smoke: translucent unlit cards, rising noise, soft edges, fading with height.
        mat = new_material("M_DressSmoke")
        mat.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
        mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
        mat.set_editor_property("two_sided", True)
        uv = node(mat, "TextureCoordinate")
        t = node(mat, "Time")
        pos = node(mat, "ObjectPositionWS")
        colour = vector(mat, "SmokeColor", (.16, .15, .14))
        density = scalar(mat, "Density", .45)
        speed = scalar(mat, "RiseSpeed", .06)
        noise = """float h(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
"""
        op = custom(mat, """float2 uv=UV; float seed=frac(O.x*0.0013+O.y*0.0017)*10;
float2 p=float2(uv.x*3,uv.y*6-T*S*6)+seed;
float n=0,amp=0.5;
[unroll] for(int i=0;i<4;i++){float2 f=frac(p),q=floor(p);f=f*f*(3-2*f);
 float a=frac(sin(dot(q,float2(127.1,311.7)))*43758.5453),b=frac(sin(dot(q+float2(1,0),float2(127.1,311.7)))*43758.5453);
 float c=frac(sin(dot(q+float2(0,1),float2(127.1,311.7)))*43758.5453),d=frac(sin(dot(q+float2(1,1),float2(127.1,311.7)))*43758.5453);
 n+=amp*lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);p*=2.03;amp*=0.5;}
float edge=smoothstep(0,0.3,uv.x)*smoothstep(1,0.7,uv.x);
float fade=smoothstep(0,0.08,uv.y)*pow(1-uv.y,1.6);
return saturate((n-0.28)*1.8)*edge*fade*D;""", [("UV", uv), ("T", t), ("S", speed), ("D", density), ("O", pos)], F1)
        emi = custom(mat, "return C*(0.75+0.5*UV.y);", [("C", colour), ("UV", uv)])
        edit.connect_material_property(emi, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
        edit.connect_material_property(op, "", u.MaterialProperty.MP_OPACITY)
        finish(mat)

        # Crows: glossy black; UV0.x = wing lever, UV0.y = phase. Flocks orbit their instance pivot.
        mat = new_material("M_DressCrow")
        mat.set_editor_property("two_sided", True)
        uv = node(mat, "TextureCoordinate")
        t = node(mat, "Time")
        pos = node(mat, "WorldPosition")
        piv = node(mat, "ObjectPositionWS")
        col = vector(mat, "Plumage", (.018, .018, .024))
        rough = scalar(mat, "Roughness", .42)
        orbit = scalar(mat, "OrbitSpeed", 0.0)
        flap = scalar(mat, "FlapAmplitude", 0.0)
        rate = scalar(mat, "FlapRate", 7.0)
        wpo = custom(mat, """float3 l=P-O; float a=T*Orbit+frac(O.x*0.0007+O.y*0.0009)*6.2832;
float c=cos(a),s=sin(a); float3 r=float3(l.x*c-l.y*s,l.x*s+l.y*c,l.z);
float bob=sin(T*0.7+UV.y*6.2832)*25*step(0.0001,Orbit);
float f=sin(T*Rate*(0.85+0.3*UV.y)+UV.y*6.2832); float glide=saturate(sin(T*0.9+UV.y*12.0)*2+0.3);
float lift=f*Flap*pow(UV.x,1.5)*glide;
return (r-l)+float3(0,0,lift+bob);""", [("P", pos), ("O", piv), ("T", t), ("Orbit", orbit), ("UV", uv), ("Flap", flap), ("Rate", rate)])
        edit.connect_material_property(col, "", u.MaterialProperty.MP_BASE_COLOR)
        edit.connect_material_property(rough, "", u.MaterialProperty.MP_ROUGHNESS)
        edit.connect_material_property(wpo, "", u.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        finish(mat)

    masters = {n: lib.load_asset(f"{PKG}/Materials/{n}") for n in ("M_DressCard", "M_DressPuddle", "M_DressSmoke", "M_DressCrow")}
    town_surface = lib.load_asset(f"{TOWN}/M_TownSurface")

    def instance(name, parent, scalars=None, vectors=None, textures=None):
        path = f"{PKG}/Materials/{name}"
        mi = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, f"{PKG}/Materials", u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi, parent)
        for k, v in (scalars or {}).items():
            edit.set_material_instance_scalar_parameter_value(mi, k, v)
        for k, v in (vectors or {}).items():
            edit.set_material_instance_vector_parameter_value(mi, k, u.LinearColor(*v, 1))
        for k, v in (textures or {}).items():
            edit.set_material_instance_texture_parameter_value(mi, k, v)
        lib.save_loaded_asset(mi, only_if_is_dirty=False)
        report["materials"].append(mi.get_path_name())
        return mi

    lit = tex["ScatteredLeaves007"]
    grime = tex["Leaking003"]
    fabric = lib.load_asset("/Game/Environment/Town/Textures/fabric_pattern_05/T_fabric_pattern_05_D")
    inst = {
        "Ivy": instance("MI_Dress_Ivy", masters["M_DressCard"], {"Roughness": .55, "NormalStrength": 1.0},
                        {"Tint": (.62, .72, .52)}, {"ColorMap": ivy["D"], "OpacityMap": ivy["A"], "NormalMap": ivy["N"]}),
        "Litter": instance("MI_Dress_Litter", masters["M_DressCard"], {"Roughness": .85, "NormalStrength": .8, "OpacityStrength": 1.3, "RadialFade": 1, "Dither": 1},
                           {"Tint": (.55, .5, .45)}, {"ColorMap": lit["D"], "OpacityMap": lit["A"], "NormalMap": lit["N"]}),
        "Grime": instance("MI_Dress_Grime", masters["M_DressCard"], {"Roughness": .9, "NormalStrength": 0, "OpacityStrength": .75, "Dither": 1},
                          {"Tint": (.22, .2, .17)}, {"ColorMap": grime["D"], "OpacityMap": grime["A"], "NormalMap": ivy["N"]}),
        "Puddle": instance("MI_Dress_Puddle", masters["M_DressPuddle"], {"Roughness": .07, "Ripple": .04}, {"WaterColor": (.055, .047, .038)}),
        "Smoke": instance("MI_Dress_Smoke", masters["M_DressSmoke"]),
        "Crow": instance("MI_Dress_Crow", masters["M_DressCrow"]),
        "CrowFlight": instance("MI_Dress_CrowFlight", masters["M_DressCrow"], {"OrbitSpeed": .22, "FlapAmplitude": 16, "FlapRate": 8}),
    }
    linen = lib.load_asset(f"{PKG}/Materials/MI_Dress_Linen") if lib.does_asset_exist(f"{PKG}/Materials/MI_Dress_Linen") else None
    if linen is None or "materials" in only:
        linen = lib.load_asset(f"{TOWN}/MI_Town_CanvasAlt")
        path = f"{PKG}/Materials/MI_Dress_Linen"
        mi = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset("MI_Dress_Linen", f"{PKG}/Materials", u.MaterialInstanceConstant, u.MaterialInstanceConstantFactoryNew())
        edit.set_material_instance_parent(mi, town_surface)
        for p in ("BaseColorMap", "NormalMap", "ARMMap"):
            edit.set_material_instance_texture_parameter_value(mi, p, linen.get_texture_parameter_value(p))
        edit.set_material_instance_scalar_parameter_value(mi, "Tiling", 1.2)
        edit.set_material_instance_scalar_parameter_value(mi, "Saturation", .15)
        edit.set_material_instance_scalar_parameter_value(mi, "MacroVariation", .1)
        edit.set_material_instance_vector_parameter_value(mi, "Tint", u.LinearColor(1.25, 1.2, 1.1, 1))
        lib.save_loaded_asset(mi, only_if_is_dirty=False)
        linen = mi
    inst["Cloth"] = linen
    for k, path in TOWN_SLOTS.items():
        inst[k] = lib.load_asset(path)
        assert inst[k], path

    # ------------------------------------------------------------ original OBJ kit
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

    meta = json.loads((ART / "Meshes/DressingMeshes.json").read_text())
    for row in meta["meshes"]:
        name = row["name"]
        path = f"{PKG}/Meshes/{name}"
        if "meshes" in only or not lib.does_asset_exist(path):
            ensure(f"{PKG}/Meshes")
            import_file(ART / "Meshes" / f"{name}.obj", f"{PKG}/Meshes", name, mesh_options())
            mesh = lib.load_asset(path)
            assert isinstance(mesh, u.StaticMesh), name
            for i, slot in enumerate(mesh.get_editor_property("static_materials")):
                key = str(slot.get_editor_property("imported_material_slot_name"))
                if key not in inst:
                    raise RuntimeError(f"{name}: unknown material slot {key}")
                mesh.set_material(i, inst[key])
            body = mesh.get_editor_property("body_setup")
            if body and name in MESH_COLLISION:
                body.set_editor_property("collision_trace_flag", u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            lib.save_loaded_asset(mesh, only_if_is_dirty=False)
        mesh = lib.load_asset(path)
        b = mesh.get_bounds()
        report["meshes"][name] = {"asset": mesh.get_path_name(), "triangles": mesh.get_num_triangles(0),
                                  "extent": [round(b.box_extent.x, 1), round(b.box_extent.y, 1), round(b.box_extent.z, 1)],
                                  "origin": [round(b.origin.x, 1), round(b.origin.y, 1), round(b.origin.z, 1)]}

    # ------------------------------------------------------------ Poly Haven models
    manifest = json.loads((ART / "SourceManifest.json").read_text())
    for asset in manifest["assets"]:
        if asset["kind"] != "model":
            continue
        aid = asset["id"]
        dest = f"{PKG}/Props/{aid}"
        importing = "props" in only or not lib.does_directory_exist(dest) or not lib.list_assets(dest, recursive=True)
        if importing:
            ensure(dest)
            import_file(ROOT / asset["entry"], dest)
        meshes = []
        for p in lib.list_assets(dest, recursive=True):
            a = lib.load_asset(p)
            if not isinstance(a, u.StaticMesh):
                continue
            tris = a.get_num_triangles(0)
            if importing:
                body = a.get_editor_property("body_setup")
                if body and aid not in PROPS_NO_COLLISION:
                    body.set_editor_property("collision_trace_flag", u.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
                lib.save_loaded_asset(a, only_if_is_dirty=False)
            ns = a.get_editor_property("nanite_settings")
            want = tris >= NANITE_MIN_TRIS and aid not in NO_NANITE
            if ns.get_editor_property("enabled") != want:
                ns.set_editor_property("enabled", want)
                a.set_editor_property("nanite_settings", ns)
                lib.save_loaded_asset(a, only_if_is_dirty=False)
            b = a.get_bounds()
            bb = a.get_bounding_box()
            meshes.append({"asset": a.get_path_name(), "triangles": tris,
                           "extent": [round(b.box_extent.x, 1), round(b.box_extent.y, 1), round(b.box_extent.z, 1)],
                           "origin": [round(b.origin.x, 1), round(b.origin.y, 1), round(b.origin.z, 1)],
                           "minZ": round(bb.min.z, 1)})
        report["props"][aid] = {"meshes": meshes}
        u.log(f"CIRE_DRESSING_PROP {aid} meshes={len(meshes)}")

    REPORT.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    u.log(f"CIRE_DRESSING_IMPORT_PASS textures={len(report['textures'])} meshes={len(report['meshes'])} props={len(report['props'])}")


# ------------------------------------------------------------------ launcher
def launch(args):
    import stat
    for path in (ROOT / "Content/Free/Dressing").rglob("*"):
        if path.is_file():
            path.chmod(path.stat().st_mode | stat.S_IWRITE)
    log = ROOT / "Saved/Logs/DressingImport.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    only = args[args.index("--only") + 1] if "--only" in args else ""
    env = dict(os.environ, CIRE_DRESSING_ONLY=only)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"),
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-run=pythonscript",
               f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
    with log.with_name("DressingImport-console.log").open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text(encoding="utf-8", errors="replace")
    ok = result.returncode == 0 and "CIRE_DRESSING_IMPORT_PASS" in text
    errors = [l for l in text.splitlines() if "Error" in l and ("Python" in l or "Traceback" in l or "LogPython" in l)]
    print(f"exit={result.returncode} pass={ok} log={log}")
    for line in errors[:40]:
        print(line)
    if ok and (not only or "props" in only):
        # Interchange parents glTF materials to engine masters without ISM/Nanite usage: re-parent (FixGltfMaterials.py).
        ok = subprocess.run([sys.executable, str(ROOT / "Tools/FixGltfMaterials.py"), "--folders", "/Game/Free"]).returncode == 0
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        sys.exit(launch(sys.argv[1:]))
    else:
        run(unreal, set(filter(None, os.environ.get("CIRE_DRESSING_ONLY", "").split(","))))
