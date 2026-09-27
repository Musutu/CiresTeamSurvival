"""Build the shadow-portal content in /Game/Arenas/Portal (Docs/Arenas.md "Shadow portals").

Run with regular Python: launches an unattended UnrealEditor-Cmd Python commandlet against this project; inside Unreal
it imports the per-arena portal views and builds the two portal materials. Inputs:
  Art/Arenas/PortalViews/<arena id>.png   square captures of each arena (Tools/RunArenaGallery.py --portal-views)
Writes only under /Game/Arenas/Portal:
  T_PortalView_<arena id>   the destination view shown inside each portal
  M_ArenaPortal             the rift disc: the view with a slow inward swirl, a dark vignette and a swirling shadow rim
                            tinted per arena (parameters View, Tint, Accent, Open, Flash, ViewGain, RimGain)
  M_ArenaPortalMote         unlit additive specks for the themed motes (Color, Intensity)

Usage: python Tools/BuildArenaPortalContent.py
"""
from __future__ import annotations

import os
import stat
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PKG = "/Game/Arenas/Portal"
VIEWS = ROOT / "Art/Arenas/PortalViews"

# Shared by both custom nodes: polar coordinates with a wobbling edge.
# Shared by both custom nodes. The Plane lies on its back and is stood upright facing +X (pitch -90), so its U runs down
# the ring and its V runs to the viewer's left: img is the view's own (left-to-right, top-to-bottom) coordinate.
POLAR = """float2 img=float2(1.0-UV.y,UV.x);
float2 p=(img-0.5)*2.0; p.y=-p.y;
float r=length(p); float a=atan2(p.y,p.x);
float wob=0.035*sin(a*5.0+T*1.7)+0.022*sin(a*9.0-T*2.3)+0.012*sin(a*17.0+T*3.1);
float rr=r+wob*smoothstep(0.45,1.0,r);
float n=0.5+0.5*sin(a*7.0+T*2.6-rr*16.0);
float n2=0.5+0.5*sin(a*13.0-T*3.4+rr*23.0);
float n3=0.5+0.5*sin(a*3.0-T*1.1);
"""

EMISSIVE = POLAR + """
float sw=(1.0-saturate(r))*(1.0-saturate(r))*0.35+0.12*sin(T*0.37)*(1.0-saturate(r));
float cs=cos(sw), sn=sin(sw);
float2 q=p*(0.80+0.03*sin(T*0.8));
q=float2(q.x*cs-q.y*sn, q.x*sn+q.y*cs);
q+=0.012*float2(sin(T*1.3+p.y*7.0), cos(T*1.1+p.x*7.0))*smoothstep(0.2,0.8,r);
float2 vuv=saturate(float2(0.5+q.x*0.5, 0.5-q.y*0.5));
float3 view=Texture2DSample(Tex,TexSampler,vuv).rgb*ViewGain;
float3 dark=float3(0.006,0.002,0.012)+Tint*0.01;
float inner=smoothstep(0.80,0.42,rr);
float3 col=lerp(dark,view,inner);
float smoke=smoothstep(0.50,0.86,rr)*(0.7+0.3*n3);
col=lerp(col,dark,saturate(smoke));
float band=smoothstep(0.60,0.80,rr)*smoothstep(1.0,0.86,rr);
float fil=pow(n*n2,4.0)*2.0+pow(0.5+0.5*sin(a*19.0+T*4.0-rr*30.0),16.0)*1.2;
col+=Tint*band*fil*RimGain;
float lip=exp(-pow((rr-0.79)*16.0,2.0));
col+=Tint*lip*(0.55+0.45*n)*RimGain;
col+=float3(0.20,0.05,0.35)*band*0.25*(1.0-n2);
col+=Tint*Flash*2.0*saturate(1.0-rr);
return col;"""

OPACITY = POLAR + """
float o=smoothstep(1.0,0.86,rr-0.06*n);
return o*saturate(Open);"""


# ================================================================== Unreal side
def run(u):
    lib = u.EditorAssetLibrary
    tools = u.AssetToolsHelpers.get_asset_tools()
    edit = u.MaterialEditingLibrary
    made = []

    if not lib.does_directory_exist(PKG):
        lib.make_directory(PKG)

    # ------------------------------------------------------------ views
    views = sorted(VIEWS.glob("*.png")) if VIEWS.exists() else []
    for png in views:
        name = f"T_PortalView_{png.stem}"
        task = u.AssetImportTask()
        task.set_editor_property("filename", str(png))
        task.set_editor_property("destination_path", PKG)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tools.import_asset_tasks([task])
        tex = lib.load_asset(f"{PKG}/{name}")
        assert tex, name
        tex.set_editor_property("srgb", True)
        tex.set_editor_property("lod_group", u.TextureGroup.TEXTUREGROUP_WORLD)
        tex.set_editor_property("max_texture_size", 512)
        tex.set_editor_property("address_x", u.TextureAddress.TA_CLAMP)
        tex.set_editor_property("address_y", u.TextureAddress.TA_CLAMP)
        lib.save_loaded_asset(tex, only_if_is_dirty=False)
        made.append(tex.get_path_name())

    # ------------------------------------------------------------ helpers
    def new_material(name):
        path = f"{PKG}/{name}"
        mat = lib.load_asset(path) if lib.does_asset_exist(path) else tools.create_asset(name, PKG, u.Material, u.MaterialFactoryNew())
        edit.delete_all_material_expressions(mat)
        edit.set_base_material_usage(mat, u.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, True)
        return mat

    def node(mat, kind, **props):
        n = edit.create_material_expression(mat, getattr(u, "MaterialExpression" + kind))
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def custom(mat, code, inputs, out):
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

    def prop(mat, pin, src):
        assert edit.connect_material_property(src, "", getattr(u.MaterialProperty, pin)), pin

    def finish(mat):
        edit.layout_material_expressions(mat)
        edit.recompile_material(mat)
        assert lib.save_loaded_asset(mat, only_if_is_dirty=False)
        made.append(mat.get_path_name())

    default_view = lib.load_asset(f"{PKG}/T_PortalView_{views[0].stem}") if views else lib.load_asset("/Engine/EngineResources/Black")

    # ------------------------------------------------------------ the rift disc
    mat = new_material("M_ArenaPortal")
    mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", u.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("two_sided", True)
    uv, t = node(mat, "TextureCoordinate"), node(mat, "Time")
    tex = node(mat, "TextureObjectParameter", parameter_name="View", texture=default_view)
    tint, accent = vector(mat, "Tint", (0.55, 0.3, 1.0)), vector(mat, "Accent", (1, 1, 1))
    open_, flash = scalar(mat, "Open", 1.0), scalar(mat, "Flash", 0.0)
    gain, rim = scalar(mat, "ViewGain", 1.0), scalar(mat, "RimGain", 1.0)
    common = [("UV", uv), ("T", t)]
    emi = custom(mat, EMISSIVE, common + [("Tex", tex), ("Tint", tint), ("Accent", accent), ("Flash", flash), ("ViewGain", gain), ("RimGain", rim)],
                 u.CustomMaterialOutputType.CMOT_FLOAT3)
    opa = custom(mat, OPACITY, common + [("Open", open_)], u.CustomMaterialOutputType.CMOT_FLOAT1)
    prop(mat, "MP_EMISSIVE_COLOR", emi)
    prop(mat, "MP_OPACITY", opa)
    finish(mat)

    # ------------------------------------------------------------ motes
    mat = new_material("M_ArenaPortalMote")
    mat.set_editor_property("shading_model", u.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property("blend_mode", u.BlendMode.BLEND_ADDITIVE)
    col, inten, fres = vector(mat, "Color", (1, 1, 1)), scalar(mat, "Intensity", 5.0), node(mat, "Fresnel")
    emi = custom(mat, "return C*I*(0.55+0.45*(1.0-F));", [("C", col), ("I", inten), ("F", fres)], u.CustomMaterialOutputType.CMOT_FLOAT3)
    prop(mat, "MP_EMISSIVE_COLOR", emi)
    finish(mat)

    u.log(f"CIRE_ARENA_PORTAL_CONTENT_PASS views={len(views)} assets={len(made)}")


# ================================================================== launcher
def launch():
    folder = ROOT / "Content/Arenas/Portal"
    if folder.exists():
        for path in folder.rglob("*"):
            if path.is_file():
                path.chmod(path.stat().st_mode | stat.S_IWRITE)
    log = ROOT / "Saved/Logs/ArenaPortalContent.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"),
               "-unattended", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding", "-run=pythonscript",
               f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
    env = dict(os.environ, UE_SKIP_UBT_SDK_SETUP="1")
    with log.with_name("ArenaPortalContent-console.log").open("w", encoding="utf-8") as stream:
        result = subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, env=env,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text(encoding="utf-8", errors="replace")
    ok = result.returncode == 0 and "CIRE_ARENA_PORTAL_CONTENT_PASS" in text and "Failed to compile Material" not in text
    errors = [l for l in text.splitlines() if ("Error" in l and ("Python" in l or "Traceback" in l)) or "Failed to compile" in l or "[SM5]" in l or "[SM6]" in l]
    print(f"exit={result.returncode} pass={ok} log={log}")
    for line in errors[:60]:
        print(line)
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        sys.exit(launch())
    else:
        run(unreal)
