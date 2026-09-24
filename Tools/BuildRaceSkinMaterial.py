"""Build /Game/Art/Materials/M_CireMonsterSkin: the race-palette + rank-colour skin for Tripo monster bodies.

monster-races (Docs/Races.md). The skin is a copy of the Tripo PBR master (/Game/TripoModels/Materials/M_Tripo_PBR_Master,
so the four texture parameters and their sampler types match every Tripo body) with a recolour graph in front of
Base Color and a rank glow on Emissive:

  lum        = luminance(BaseColorTex)
  body       = lerp(base, lum * RaceTint * 2, RaceTintStrength)                 race palette (hue replacement, keeps detail)
  armour     = saturate(MetallicTex * ArmorMaskGain)                             metal parts of the Tripo PBR set
  body       = lerp(body, lum * RaceAccent * 2, RaceAccentStrength * armour)    race accent on the armour
  body       = lerp(body, lum * RankColor * 2.4, RankArmor * armour)            rank colour on the armour
  body       = lerp(body, lum * RankColor * 2.4, RankBody)                      small whole-body rank tint
  emissive   = (RimColor * fresnel^3 * RimStrength + TrimColor * armour * RankGlow * 0.3) * GlowBoost

CireRaces::ApplySkin creates one MID per body slot from it, copies the body's textures and sets the parameters.
Usage: python Tools/BuildRaceSkinMaterial.py   (runs UnrealEditor-Cmd on this project; only the process it starts is stopped)
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
MASTER = "/Game/TripoModels/Materials/M_Tripo_PBR_Master"
TARGET_DIR, TARGET_NAME = "/Game/Art/Materials", "M_CireMonsterSkin"


def build(unreal):
    lib = unreal.EditorAssetLibrary
    mel = unreal.MaterialEditingLibrary
    target = f"{TARGET_DIR}/{TARGET_NAME}"
    if lib.does_asset_exist(target):
        lib.delete_asset(target)
    if not lib.duplicate_asset(MASTER, target):
        raise RuntimeError("could not duplicate " + MASTER)
    mat = lib.load_asset(target)
    P = unreal.MaterialProperty
    base = mel.get_material_property_input_node(mat, P.MP_BASE_COLOR)
    base_out = mel.get_material_property_input_node_output_name(mat, P.MP_BASE_COLOR) if hasattr(mel, "get_material_property_input_node_output_name") else ""
    metal = mel.get_material_property_input_node(mat, P.MP_METALLIC)
    metal_out = mel.get_material_property_input_node_output_name(mat, P.MP_METALLIC) if hasattr(mel, "get_material_property_input_node_output_name") else ""
    if base is None or metal is None:
        raise RuntimeError("master material has no base colour / metallic inputs")
    unreal.log_warning(f"CIRE_RACE_SKIN base={base.get_name()}:{base_out!r} metal={metal.get_name()}:{metal_out!r}")
    x0, y0 = -900, -300
    nodes = []

    def node(cls, x, y, **props):
        n = mel.create_material_expression(mat, cls, x, y)
        for k, v in props.items():
            n.set_editor_property(k, v)
        nodes.append(n)
        return n

    def vec(name, value, x, y):
        return node(unreal.MaterialExpressionVectorParameter, x, y, parameter_name=name, default_value=unreal.LinearColor(*value))

    def scal(name, value, x, y):
        return node(unreal.MaterialExpressionScalarParameter, x, y, parameter_name=name, default_value=value)

    def mul(a, b, x, y, a_out="", b_out="", const_b=None):
        n = node(unreal.MaterialExpressionMultiply, x, y)
        mel.connect_material_expressions(a, a_out, n, "A")
        if b is not None:
            mel.connect_material_expressions(b, b_out, n, "B")
        else:
            n.set_editor_property("const_b", const_b)
        return n

    def lerp(a, b, alpha, x, y, a_out=""):
        n = node(unreal.MaterialExpressionLinearInterpolate, x, y)
        mel.connect_material_expressions(a, a_out, n, "A")
        mel.connect_material_expressions(b, "", n, "B")
        mel.connect_material_expressions(alpha, "", n, "Alpha")
        return n

    def add(a, b, x, y):
        n = node(unreal.MaterialExpressionAdd, x, y)
        mel.connect_material_expressions(a, "", n, "A")
        mel.connect_material_expressions(b, "", n, "B")
        return n

    weights = node(unreal.MaterialExpressionConstant3Vector, x0, y0 - 200, constant=unreal.LinearColor(0.299, 0.587, 0.114, 0))
    lum = node(unreal.MaterialExpressionDotProduct, x0 + 200, y0 - 200)
    mel.connect_material_expressions(base, base_out, lum, "A")
    mel.connect_material_expressions(weights, "", lum, "B")

    race_tint, race_strength = vec("RaceTint", (1, 1, 1, 1), x0, y0), scal("RaceTintStrength", 0.0, x0, y0 + 80)
    race_accent, accent_strength = vec("RaceAccent", (1, 1, 1, 1), x0, y0 + 160), scal("RaceAccentStrength", 0.0, x0, y0 + 240)
    rank_color, rank_armor = vec("RankColor", (0.82, 0.8, 0.74, 1), x0, y0 + 320), scal("RankArmor", 0.0, x0, y0 + 400)
    rank_body, rank_glow = scal("RankBody", 0.0, x0, y0 + 480), scal("RankGlow", 0.0, x0, y0 + 560)
    trim_color, rim_color = vec("TrimColor", (0.82, 0.8, 0.74, 1), x0, y0 + 640), vec("RimColor", (0, 0, 0, 1), x0, y0 + 720)
    rim_strength, armor_gain = scal("RimStrength", 0.0, x0, y0 + 800), scal("ArmorMaskGain", 2.0, x0, y0 + 880)
    glow_boost = scal("GlowBoost", 3.0, x0, y0 + 960)

    # race recolour
    race_col = mul(mul(lum, race_tint, x0 + 400, y0), None, x0 + 560, y0, const_b=2.0)
    body = lerp(base, race_col, race_strength, x0 + 760, y0, a_out=base_out)
    # armour mask from the metallic map
    armour = node(unreal.MaterialExpressionSaturate, x0 + 560, y0 + 300)
    mel.connect_material_expressions(mul(metal, armor_gain, x0 + 400, y0 + 300, a_out=metal_out), "", armour, "")
    accent_col = mul(mul(lum, race_accent, x0 + 400, y0 + 150), None, x0 + 560, y0 + 150, const_b=2.0)
    body = lerp(body, accent_col, mul(accent_strength, armour, x0 + 760, y0 + 200), x0 + 960, y0 + 100)
    rank_col = mul(mul(lum, rank_color, x0 + 400, y0 + 400), None, x0 + 560, y0 + 400, const_b=2.4)
    body = lerp(body, rank_col, mul(rank_armor, armour, x0 + 760, y0 + 380), x0 + 1160, y0 + 200)
    body = lerp(body, rank_col, rank_body, x0 + 1360, y0 + 250)
    mel.connect_material_property(body, "", P.MP_BASE_COLOR)
    # emissive: fresnel rim + armour trim glow
    fresnel = node(unreal.MaterialExpressionFresnel, x0 + 400, y0 + 700, exponent=3.0, base_reflect_fraction=0.0)
    rim = mul(mul(rim_color, fresnel, x0 + 560, y0 + 700), rim_strength, x0 + 760, y0 + 700)
    trim = mul(mul(mul(trim_color, armour, x0 + 760, y0 + 820), rank_glow, x0 + 960, y0 + 820), None, x0 + 1160, y0 + 820, const_b=0.3)
    emissive = mul(add(rim, trim, x0 + 1360, y0 + 760), glow_boost, x0 + 1560, y0 + 760)
    mel.connect_material_property(emissive, "", P.MP_EMISSIVE_COLOR)
    mel.set_base_material_usage(mat, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH, True)
    mel.recompile_material(mat)
    if not lib.save_loaded_asset(mat, only_if_is_dirty=False):
        raise RuntimeError("save failed")
    names = mel.get_vector_parameter_names(mat) + mel.get_scalar_parameter_names(mat)
    unreal.log_warning("CIRE_RACE_SKIN_PARAMS " + ",".join(str(n) for n in names))
    unreal.log_warning("CIRE_RACE_SKIN_PASS")


if __name__ == "__main__":
    try:
        import unreal  # noqa: F401
    except ImportError:
        log = ROOT / "Saved/Logs/RaceSkinMaterial.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(ROOT / "CiresTeamSurvival.uproject"), "-unattended", "-nullrhi",
                   "-nosplash", "-nosound", "-nop4", "-run=pythonscript", f"-script={Path(__file__).resolve()}", f"-abslog={log}"]
        creation = getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0
        code = subprocess.run(command, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, creationflags=creation).returncode
        text = log.read_text(encoding="utf-8", errors="replace") if log.exists() else ""
        for line in text.splitlines():
            if "CIRE_RACE_SKIN" in line or "Error" in line and "Python" in line:
                print(line.strip())
        ok = code == 0 and "CIRE_RACE_SKIN_PASS" in text
        print("RACE SKIN MATERIAL", "PASS" if ok else f"FAIL (exit {code}) see {log}")
        sys.exit(0 if ok else 1)
    else:
        import unreal
        build(unreal)
