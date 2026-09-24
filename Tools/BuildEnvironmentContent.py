"""Build original environment materials in the isolated content-builder project.

Uses the three saved image_gen albedos with world-space mapping, shader-derived
microrelief and roughness. No third-party assets or original game packages edited.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
STAGING = ROOT / "Tools/ContentBuilder"
PACKAGE = "/Game/Art/Environment"
TEXTURE_NAMES = ("T_BasaltRoad", "T_AshenMasonry", "T_WornEarth")


def build(u):
    assets = u.AssetToolsHelpers.get_asset_tools()
    lib = u.EditorAssetLibrary
    edit = u.MaterialEditingLibrary
    lib.make_directory(PACKAGE + "/Textures")
    lib.make_directory(PACKAGE + "/Materials")
    textures = {}
    for name in TEXTURE_NAMES:
        source_image = ROOT / "Art/Environment/Textures01" / (name + ".png")
        assert source_image.is_file(), f"Missing authored source image: {source_image}"
        task = u.AssetImportTask()
        task.set_editor_property("filename", str(source_image))
        task.set_editor_property("destination_path", PACKAGE + "/Textures")
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        assets.import_asset_tasks([task])
        tex = lib.load_asset(PACKAGE + "/Textures/" + name)
        assert tex, name
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("address_x", u.TextureAddress.TA_WRAP)
        tex.set_editor_property("address_y", u.TextureAddress.TA_WRAP)
        assert lib.save_loaded_asset(tex, only_if_is_dirty=False)
        textures[name] = tex

    report = []
    material_sources = []
    for name, tex_name, scale, tint in (
        ("M_BasaltRoad", "T_BasaltRoad", 260, (1.05, 1.03, .99)),
        ("M_AshenMasonry", "T_AshenMasonry", 320, (.84, .89, .94)),
        ("M_WornEarth", "T_WornEarth", 400, (.96, .96, .96)),
        ("M_OldTimber", None, 120, (.067, .035, .015)),
        ("M_RoofSlate", None, 100, (.031, .045, .061)),
        ("M_WindowGlow", None, 100, (.32, .16, .047)),
    ):
        path = PACKAGE + "/Materials/" + name
        mat = lib.load_asset(path) if lib.does_asset_exist(path) else assets.create_asset(name, PACKAGE + "/Materials", u.Material, u.MaterialFactoryNew())
        assert mat
        edit.delete_all_material_expressions(mat)
        edit.set_base_material_usage(mat, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
        mat.set_editor_property("tangent_space_normal", False)

        def node(kind, **props):
            n = edit.create_material_expression(mat, getattr(u, "MaterialExpression" + kind))
            for key, value in props.items():
                n.set_editor_property(key, value)
            return n

        def custom(code, inputs, kind=u.CustomMaterialOutputType.CMOT_FLOAT3):
            ins = []
            for input_name, source in inputs:
                ci = u.CustomInput()
                ci.set_editor_property("input_name", input_name)
                ins.append(ci)
            n = node("Custom", code=code, output_type=kind, inputs=ins)
            for input_name, source in inputs:
                assert edit.connect_material_expressions(source, "", n, input_name), input_name
            return n

        pos = node("WorldPosition")
        normal = node("VertexNormalWS")
        if tex_name:
            tex = node("TextureObject", texture=textures[tex_name])
            base = custom(f"""float3 n=abs(normalize(N)); n=pow(n,8); n/=max(dot(n,float3(1,1,1)),0.0001);
float3 p=P/{float(scale)};
float3 a=Texture2DSample(Tex,TexSampler,p.yz).rgb;
float3 b=Texture2DSample(Tex,TexSampler,p.xz).rgb;
float3 c=Texture2DSample(Tex,TexSampler,p.xy).rgb;
return (a*n.x+b*n.y+c*n.z)*float3({tint[0]},{tint[1]},{tint[2]});""", [("Tex", tex), ("P", pos), ("N", normal)])
        else:
            # Restrained code-native grain for secondary materials, all sampled
            # in world centimetres to prevent stretching on long modules.
            detail = "float v=.82+.12*sin(P.x*.09+sin(P.y*.17)*2)+.06*sin(P.y*.13+P.z*.12);"
            if name == "M_OldTimber":
                detail = "float v=.65+.25*sin(P.z*.04+sin(P.x*.085)*1.6)+.1*sin(P.x*.8);"
            if name == "M_RoofSlate":
                detail = "float row=floor(P.z/24); float seam=step(.07,frac(P.z/24))*step(.035,frac(P.x/48+row*.5)); float v=lerp(.40,.85+.15*sin(row*2.31),seam);"
            base = custom(detail + f"return float3({tint[0]},{tint[1]},{tint[2]})*v;", [("P", pos)])
        assert edit.connect_material_property(base, "", u.MaterialProperty.MP_BASE_COLOR)
        rough = custom("return clamp(.94-dot(C,float3(.2126,.7152,.0722))*.22,.62,.98);", [("C", base)], u.CustomMaterialOutputType.CMOT_FLOAT1)
        assert edit.connect_material_property(rough, "", u.MaterialProperty.MP_ROUGHNESS)
        bump = custom("""float3 n=normalize(N); float h=dot(C,float3(.2126,.7152,.0722))*1.4;
float3 r1=cross(ddy(P),n),r2=cross(n,ddx(P)); float det=dot(ddx(P),r1);
float3 grad=sign(det)*(ddx(h)*r1+ddy(h)*r2);
return normalize(max(abs(det),.00001)*n-grad);""", [("C", base), ("P", pos), ("N", normal)])
        assert edit.connect_material_property(bump, "", u.MaterialProperty.MP_NORMAL)
        if name == "M_WindowGlow":
            glow = node("Constant3Vector", constant=u.LinearColor(1.8, .67, .16, 1))
            assert edit.connect_material_property(glow, "", u.MaterialProperty.MP_EMISSIVE_COLOR)
        edit.layout_material_expressions(mat)
        edit.recompile_material(mat)
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False), path
        assert edit.has_material_usage(mat, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
        report.append(path)
        material_sources.append({"material": path, "albedo": tex_name, "worldRepeatCm": scale if tex_name else None, "tint": tint})
    (STAGING / "Saved/EnvironmentContent.json").write_text(json.dumps({"materials": report, "textures": list(textures), "materialSources": material_sources, "normalMethod": "shader-derived albedo microrelief; not a captured normal map"}, indent=2))
    u.log(f"CIRE_ENVIRONMENT_CONTENT_PASS textures={len(textures)} materials={len(report)}")


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        log = STAGING / "Saved/Logs/EnvironmentContent.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(STAGING / "ContentBuilder.uproject"),
                   "-unattended", "-nullrhi", "-nosplash", "-nosound", "-nop4", "-run=pythonscript",
                   f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
        with log.with_name("EnvironmentContent-console.log").open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        contents = log.read_text(encoding="utf-8", errors="replace")
        if result.returncode or "CIRE_ENVIRONMENT_CONTENT_PASS" not in contents or "Failed to compile Material" in contents:
            raise RuntimeError(f"Environment material build failed: {log}")
        source, dest = STAGING / "Content/Art/Environment", ROOT / "Content/Art/Environment"
        for package in source.rglob("*.uasset"):
            target = dest / package.relative_to(source)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(package, target)
        shutil.copy2(STAGING / "Saved/EnvironmentContent.json", ROOT / "Saved/EnvironmentContent.json")
        print(f"CIRE_ENVIRONMENT_CONTENT_COPIED textures={len(TEXTURE_NAMES)} materials=6")
    else:
        build(unreal)
