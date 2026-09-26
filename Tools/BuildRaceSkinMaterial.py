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
    # monster-rig: the skin is referenced by saved instances, so an unattended delete is refused; an existing skin
    # is patched in place (only the sway graph is added) instead of rebuilt from the master.
    patch = lib.does_asset_exist(target) and not lib.delete_asset(target)
    if not patch and not lib.duplicate_asset(MASTER, target):
        raise RuntimeError("could not duplicate " + MASTER)
    mat = lib.load_asset(target)
    P = unreal.MaterialProperty
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

    if patch and mel.get_material_property_input_node(mat, P.MP_WORLD_POSITION_OFFSET) is not None:
        unreal.log_warning("CIRE_RACE_SKIN sway already present; recompiling only")
        mel.recompile_material(mat)
        lib.save_loaded_asset(mat, only_if_is_dirty=False)
        unreal.log_warning("CIRE_RACE_SKIN_PASS")
        return
    if patch:
        emissive = mel.get_material_property_input_node(mat, P.MP_EMISSIVE_COLOR)
    else:
        base = mel.get_material_property_input_node(mat, P.MP_BASE_COLOR)
        base_out = mel.get_material_property_input_node_output_name(mat, P.MP_BASE_COLOR) if hasattr(mel, "get_material_property_input_node_output_name") else ""
        metal = mel.get_material_property_input_node(mat, P.MP_METALLIC)
        metal_out = mel.get_material_property_input_node_output_name(mat, P.MP_METALLIC) if hasattr(mel, "get_material_property_input_node_output_name") else ""
        if base is None or metal is None:
            raise RuntimeError("master material has no base colour / metallic inputs")
        unreal.log_warning(f"CIRE_RACE_SKIN base={base.get_name()}:{base_out!r} metal={metal.get_name()}:{metal_out!r}")

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
    # monster-rig: skin sway (tentacles/vines Tripo's humanoid rig gave no bones), world position offset from the
    # pre-skinned position; SwayDebug paints the mask green for tuning (cire.Monsters.SwayDebug 1).
    sway_inputs = [
        ("P", node(unreal.MaterialExpressionPreSkinnedPosition, x0 + 1000, y0 + 1100)),
        ("T", node(unreal.MaterialExpressionTime, x0 + 1000, y0 + 1160)),
        ("Seed", node(unreal.MaterialExpressionObjectPositionWS, x0 + 1000, y0 + 1220)),
        ("CA", vec("SwayCenterA", (0, 0, 0, 0), x0 + 800, y0 + 1100)),
        ("RA", vec("SwayRadiiA", (1, 1, 1, 0), x0 + 800, y0 + 1180)),
        ("BA", vec("SwayBandA", (1, 0, 0, 0), x0 + 800, y0 + 1260)),
        ("CB", vec("SwayCenterB", (0, 0, 0, 0), x0 + 800, y0 + 1340)),
        ("RB", vec("SwayRadiiB", (1, 1, 1, 0), x0 + 800, y0 + 1420)),
        ("BB", vec("SwayBandB", (1, 0, 0, 0), x0 + 800, y0 + 1500)),
        ("Speed", scal("SwaySpeed", 1.6, x0 + 800, y0 + 1580)),
        ("Wave", scal("SwayWave", 1.0, x0 + 800, y0 + 1660)),
    ]
    mask_code = """
float3 p = P.xyz;
float3 cs[2] = {CA.xyz, CB.xyz};
float3 rs[2] = {max(RA.xyz, 0.1), max(RB.xyz, 0.1)};
float3 bs[2] = {BA.xyz, BB.xyz};
float3 o = 0;
float mask = 0;
float t = T * Speed;
float ph0 = dot(p, float3(0.13, 0.09, 0.21)) * Wave + dot(Seed.xyz, float3(0.011, 0.017, 0));
[unroll] for (int i = 0; i < 2; i++)
{
    float3 d = (p - cs[i]) / rs[i];
    float m = saturate((1 - dot(d, d)) * 4);
    float span = bs[i].x - bs[i].y;
    float tip = saturate((bs[i].x - p.z) / (abs(span) < 0.01 ? 0.01 : span));
    m *= tip * tip * step(0.001, bs[i].z);
    float ph = ph0 + i * 1.7;
    o += float3(sin(t + ph), cos(t * 0.83 + ph * 1.37), 0.3 * sin(t * 1.9 + ph * 0.7)) * bs[i].z * m;
    mask = max(mask, m);
}
"""

    def custom(code, x, y):
        n = node(unreal.MaterialExpressionCustom, x, y, code=code, output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,
                 description="CireSway")
        items = []
        for name, _ in sway_inputs:
            item = unreal.CustomInput()
            item.set_editor_property("input_name", name)
            items.append(item)
        n.set_editor_property("inputs", items)
        for name, src in sway_inputs:
            mel.connect_material_expressions(src, "", n, name)
        return n

    wpo = custom(mask_code + "return o;", x0 + 1300, y0 + 1200)
    mel.connect_material_property(wpo, "", P.MP_WORLD_POSITION_OFFSET)
    # PreSkinnedPosition is vertex-shader only: the debug mask is computed per vertex and interpolated.
    sway_mask = custom(mask_code + "return float3(0.1, 1.0, 0.35) * mask * 4;", x0 + 1300, y0 + 1400)
    interp = node(unreal.MaterialExpressionVertexInterpolator, x0 + 1450, y0 + 1400)
    mel.connect_material_expressions(sway_mask, "", interp, "")
    emissive = add(emissive, mul(interp,scal("SwayDebug", 0.0, x0 + 1300, y0 + 1500), x0 + 1500, y0 + 1400), x0 + 1760, y0 + 900)
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
