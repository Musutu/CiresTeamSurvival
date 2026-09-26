"""UE 5.8: build /Game/Art/Materials/M_CireHero_PBR, the champion-HQ master material (feat/champion-hq).

Inputs (texture parameters, same names as the Tripo PBR set so the Bridge textures drop straight in):
  BaseColorTex  sRGB colour               NormalTex  tangent normal (TC_Normalmap)
  RoughnessTex  linear, R                 MetallicTex linear, R
  MaskTex       linear, optional: R = skin (subsurface), G = emissive, B = unused (default black texture)
Graph:
  colour     = saturate(desaturate(BaseColorTex, -Vibrance)) * Brightness * ColorTint
  roughness  = lerp(RoughnessMin, RoughnessMax, RoughnessTex.r), metal parts pulled toward MetalRoughness
  metallic   = MetallicTex.r * MetallicScale
  normal     = FlattenNormal(NormalTex, 1 - NormalStrength)
  subsurface = shading model Subsurface; opacity = MaskTex.r * SkinScatter, colour = SkinScatterColor
  emissive   = BaseColorTex * MaskTex.g * EmissiveColor * EmissiveIntensity
               + RimColor * fresnel(RimExponent) * RimStrength          (thin readable silhouette rim)
Run: UnrealEditor-Cmd <project> -run=pythonscript -script=<abs>/Tools/BuildHeroMaterial.py -unattended -nullrhi
Marker: CIRE_HERO_MATERIAL_PASS / _FAIL.
"""
import traceback
import unreal

TARGET_DIR, TARGET_NAME = "/Game/Art/Materials", "M_CireHero_PBR"
BLACK = "/Game/Art/Materials/T_CireHero_LinearBlack.T_CireHero_LinearBlack"
LGREY = "/Game/Art/Materials/T_CireHero_LinearGrey.T_CireHero_LinearGrey"
FLAT = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"
GREY = "/Engine/EngineResources/DefaultTexture.DefaultTexture"

lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
P = unreal.MaterialProperty


def linear_defaults():
    """8x8 linear (non-sRGB) defaults: the Linear Color samplers reject the engine's sRGB Black/Default textures."""
    from pathlib import Path
    src = Path(unreal.Paths.project_dir()) / "Art" / "ChampionHQ" / "Textures"
    for name in ("T_CireHero_LinearBlack", "T_CireHero_LinearGrey"):
        path = TARGET_DIR + "/" + name
        if not lib.does_asset_exist(path):
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str((src / (name + ".png")).resolve()))
            task.set_editor_property("destination_path", TARGET_DIR)
            task.set_editor_property("automated", True)
            task.set_editor_property("save", True)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        tex = unreal.load_asset(path)
        tex.set_editor_property("srgb", False)
        lib.save_loaded_asset(tex, False)


def build():
    linear_defaults()
    target = TARGET_DIR + "/" + TARGET_NAME
    if lib.does_asset_exist(target):
        mat = lib.load_asset(target)
        mel.delete_all_material_expressions(mat)
    else:
        mat = unreal.AssetToolsHelpers.get_asset_tools().create_asset(TARGET_NAME, TARGET_DIR, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    mat.set_editor_property("two_sided", False)
    for flag in ("used_with_skeletal_mesh", "used_with_static_lighting", "used_with_nanite"):
        try:
            mat.set_editor_property(flag, True)
        except Exception:
            pass

    def node(cls, x, y, **props):
        n = mel.create_material_expression(mat, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        return n

    def tex(name, x, y, default, sampler):
        return node(unreal.MaterialExpressionTextureSampleParameter2D, x, y, parameter_name=name,
                    texture=unreal.load_asset(default), sampler_type=sampler)

    def scal(name, value, x, y):
        return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    def vec(name, value, x, y):
        return node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*value))

    def op(cls, a, b, x, y, a_out="", b_out=""):
        n = node(cls, x, y)
        mel.connect_material_expressions(a, a_out, n, "A")
        mel.connect_material_expressions(b, b_out, n, "B")
        return n

    def lerp(a, b, alpha, x, y, alpha_out=""):
        n = node(unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, "", n, "A")
        mel.connect_material_expressions(b, "", n, "B")
        mel.connect_material_expressions(alpha, alpha_out, n, "Alpha")
        return n

    S = unreal.MaterialSamplerType
    base = tex("BaseColorTex", -1400, -400, GREY, S.SAMPLERTYPE_COLOR)
    normal = tex("NormalTex", -1400, 0, FLAT, S.SAMPLERTYPE_NORMAL)
    rough = tex("RoughnessTex", -1400, 300, LGREY, S.SAMPLERTYPE_LINEAR_COLOR)
    metal = tex("MetallicTex", -1400, 600, BLACK, S.SAMPLERTYPE_LINEAR_COLOR)
    mask = tex("MaskTex", -1400, 900, BLACK, S.SAMPLERTYPE_LINEAR_COLOR)

    # Colour: vibrance = lerp(luminance, colour, 1 + Vibrance) (extrapolates away from grey), brightness, tint.
    vib = scal("Vibrance", 0.25, -1100, -250)
    one_plus = node(unreal.MaterialExpressionAdd, -950, -250, const_b=1.0)
    mel.connect_material_expressions(vib, "", one_plus, "A")
    lumw = node(unreal.MaterialExpressionConstant3Vector, -1100, -150, constant=unreal.LinearColor(0.2126, 0.7152, 0.0722, 1))
    lum = node(unreal.MaterialExpressionDotProduct, -950, -150)
    mel.connect_material_expressions(base, "RGB", lum, "A")
    mel.connect_material_expressions(lumw, "", lum, "B")
    sat_l = node(unreal.MaterialExpressionLinearInterpolate, -800, -400)
    mel.connect_material_expressions(lum, "", sat_l, "A")
    mel.connect_material_expressions(base, "RGB", sat_l, "B")
    mel.connect_material_expressions(one_plus, "", sat_l, "Alpha")
    sat = node(unreal.MaterialExpressionSaturate, -650, -400)
    mel.connect_material_expressions(sat_l, "", sat, "")
    bright = scal("Brightness", 1.0, -650, -300)
    tint = vec("ColorTint", (1, 1, 1, 1), -650, -200)
    c1 = op(unreal.MaterialExpressionMultiply, sat, bright, -500, -400)
    colour = op(unreal.MaterialExpressionMultiply, c1, tint, -350, -400)
    mel.connect_material_property(colour, "", P.MP_BASE_COLOR)

    # Metallic.
    mscale = scal("MetallicScale", 1.0, -1100, 650)
    metallic = op(unreal.MaterialExpressionMultiply, metal, mscale, -900, 600, "R")
    mel.connect_material_property(metallic, "", P.MP_METALLIC)

    # Roughness: remap, then metal pulled toward a polished value (crisper highlights on armour).
    rmin = scal("RoughnessMin", 0.18, -1100, 300)
    rmax = scal("RoughnessMax", 0.92, -1100, 380)
    r1 = lerp(rmin, rmax, rough, -900, 320, "R")
    mrough = scal("MetalRoughness", 0.32, -900, 450)
    mpull = scal("MetalPolish", 0.35, -900, 520)
    mw = op(unreal.MaterialExpressionMultiply, metallic, mpull, -750, 520)
    r2 = lerp(r1, mrough, mw, -600, 350)
    mel.connect_material_property(r2, "", P.MP_ROUGHNESS)
    spec = scal("Specular", 0.5, -600, 480)
    mel.connect_material_property(spec, "", P.MP_SPECULAR)

    # Normal strength.
    nstr = scal("NormalStrength", 1.0, -1100, 120)
    one_minus = node(unreal.MaterialExpressionOneMinus, -950, 120)
    mel.connect_material_expressions(nstr, "", one_minus, "")
    flat = node(unreal.MaterialExpressionMaterialFunctionCall, -800, 0,
                material_function=unreal.load_asset("/Engine/Functions/Engine_MaterialFunctions01/Texturing/FlattenNormal.FlattenNormal"))
    mel.connect_material_expressions(normal, "RGB", flat, "Normal")
    mel.connect_material_expressions(one_minus, "", flat, "Flatness")
    mel.connect_material_property(flat, "", P.MP_NORMAL)

    # Subsurface on skin only.
    sss = scal("SkinScatter", 0.6, -1100, 950)
    sop = op(unreal.MaterialExpressionMultiply, mask, sss, -900, 900, "R")
    # Subsurface opacity: 1 = opaque (no scatter), 0 = full scatter, so skin gets 1 - mask * SkinScatter.
    sinv = node(unreal.MaterialExpressionOneMinus, -750, 900)
    mel.connect_material_expressions(sop, "", sinv, "")
    mel.connect_material_property(sinv, "", P.MP_OPACITY)
    scol = vec("SkinScatterColor", (0.85, 0.28, 0.18, 1), -900, 1000)
    mel.connect_material_property(scol, "", P.MP_SUBSURFACE_COLOR)

    # Emissive accents (masked base colour) + silhouette rim.
    ecol = vec("EmissiveColor", (1, 1, 1, 1), -1100, 1150)
    eint = scal("EmissiveIntensity", 0.0, -1100, 1250)
    e1 = op(unreal.MaterialExpressionMultiply, base, mask, -900, 1150, "RGB", "G")
    e2 = op(unreal.MaterialExpressionMultiply, e1, ecol, -750, 1150)
    e3 = op(unreal.MaterialExpressionMultiply, e2, eint, -600, 1150)
    fres = node(unreal.MaterialExpressionFresnel, -900, 1350)
    rexp = scal("RimExponent", 4.0, -1100, 1350)
    mel.connect_material_expressions(rexp, "", fres, "ExponentIn")
    rcol = vec("RimColor", (1, 1, 1, 1), -900, 1450)
    rstr = scal("RimStrength", 0.0, -900, 1550)
    r_a = op(unreal.MaterialExpressionMultiply, fres, rcol, -750, 1350)
    r_b = op(unreal.MaterialExpressionMultiply, r_a, rstr, -600, 1350)
    emis = op(unreal.MaterialExpressionAdd, e3, r_b, -450, 1250)
    mel.connect_material_property(emis, "", P.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    lib.save_loaded_asset(mat, False)
    unreal.log("CIRE_HERO_MATERIAL_PASS " + target)


try:
    build()
except Exception:
    unreal.log_error("CIRE_HERO_MATERIAL_FAIL " + traceback.format_exc())
