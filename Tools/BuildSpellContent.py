"""Original Cire native spell materials and deterministic synthesized combat audio.

Run with engine Python while Unreal project processes are stopped. This uses the
isolated ContentBuilder project, checks a success marker, then copies only its
named new assets. No existing imported character or source material is modified.
All waveforms are generated here; there are no downloaded recordings or samples.
"""
from pathlib import Path
import array
import json
import math
import os
import random
import shutil
import subprocess
import wave

ROOT = Path(__file__).resolve().parent.parent
STAGING = ROOT / "Tools/ContentBuilder"
SOURCES = ROOT / "Art/Generated/CombatAudio01"
NAMES = ["Swing", "Bow", "Lance", "Impact", "MagicCast", "MagicImpact", "Heal", "Ward", "Critical"]


def synthesize():
    SOURCES.mkdir(parents=True, exist_ok=True)
    report = []
    rate = 48000
    for index, name in enumerate(NAMES):
        rng = random.Random(81073 + index)
        duration = {"Swing": .30, "Bow": .40, "Lance": .35, "Impact": .29, "MagicCast": .56,
                    "MagicImpact": .42, "Heal": .80, "Ward": .70, "Critical": .35}[name]
        count = round(rate * duration)
        samples, low, mid, phase = [], 0.0, 0.0, 0.0
        for i in range(count):
            t = i / rate
            u = t / duration
            white = rng.uniform(-1, 1)
            low += .025 * (white - low)
            mid += .18 * (white - mid)
            high = white - mid
            if name in ("Swing", "Lance"):
                envelope = math.sin(math.pi * u) ** (2.8 if name == "Swing" else 1.7)
                phase += (400 + 2100 * math.sin(math.pi * u)) * 2 * math.pi / rate
                value = envelope * (mid * 2.4 + high * .11 + math.sin(phase) * .04)
            elif name == "Bow":
                # A plucked inharmonic string plus the air release, not a gunshot.
                value = math.exp(-t * 14) * (.34 * math.sin(2*math.pi*176*t) + .18*math.sin(2*math.pi*359*t)
                                             + .09*math.sin(2*math.pi*715*t))
                value += mid * math.exp(-t*28) * 1.1 + high * math.exp(-t*100) * .35
            elif name == "Impact":
                value = .45 * math.sin(2*math.pi*(95*t-55*t*t)) * math.exp(-t*21)
                value += mid * 1.3 * math.exp(-t*23) + high * .22 * math.exp(-t*90)
                value += .09 * math.sin(2*math.pi*1283*t) * math.exp(-t*36)
            elif name in ("MagicCast", "MagicImpact"):
                impact = name == "MagicImpact"
                phase += (110 + 450*(1-u if impact else u)) * 2*math.pi/rate
                envelope = math.exp(-t*9) if impact else math.sin(math.pi*u)**1.3
                value = envelope * (mid * 1.4 + low*2 + math.sin(phase)*.2 + math.sin(phase*1.503)*.08)
                if impact:
                    value += high * .3 * math.exp(-t*55)
            else:
                # Bell-like resonances with independent decay, airy onset and
                # non-harmonic upper modes. Consonant lift identifies support.
                base = {"Heal": 436, "Ward": 218, "Critical": 872}[name]
                value = sum(gain * math.sin(2*math.pi*base*ratio*t)*math.exp(-t*decay)
                            for ratio,gain,decay in ((1,.3,4),(1.501,.18,5),(2.013,.12,8),(3.17,.06,12)))
                value += mid * .45 * math.exp(-t*25)
            edge = min(1.0, t/.006, (duration-t)/.035)
            samples.append(value * max(0, edge))
        peak = max(abs(x) for x in samples)
        gain = .78 / max(peak, .001)
        pcm = array.array("h", [round(max(-.9, min(.9, s*gain))*32767) for s in samples])
        with wave.open(str(SOURCES / f"S_{name}.wav"), "wb") as output:
            output.setnchannels(1)
            output.setsampwidth(2)
            output.setframerate(rate)
            output.writeframes(pcm.tobytes())
        rms = math.sqrt(sum((s/32767)**2 for s in pcm)/len(pcm))
        assert .02 < rms < .6 and max(abs(s) for s in pcm) < 32767 and abs(pcm[0]) < 4 and abs(pcm[-1]) < 100
        report.append(dict(name=name, frames=len(pcm), sampleRate=rate, duration=duration, rms=rms,
                           peak=max(abs(s) for s in pcm)/32767, originalSynthesis=True))
    (SOURCES / "manifest.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    (SOURCES / "README.md").write_text(
        "# Cire Combat Audio 01\n\nNine original deterministic synthesized mono combat sounds, authored by BuildSpellContent.py. "
        "48 kHz, 16-bit PCM, softened onset/tail, normalized peak below -2 dBFS. No external samples.\n\n"
        "Spatial attenuation is configured on each local presentation audio component (160 cm inner radius, "
        "1800 cm falloff). Shared concurrency limits combat presentation to 16 voices. Confirmed-hit audio "
        "is only emitted by authoritative combat events; misses and dodges have visual text without impact sound. "
        "These are original gameplay cues, pending a final sound-design/mixing pass.\n", encoding="utf-8")
    return report


def build(unreal):
    synthesize()
    editor, library = unreal.MaterialEditingLibrary, unreal.EditorAssetLibrary
    assets = unreal.AssetToolsHelpers.get_asset_tools()

    def make(name, folder, cls, factory):
        path = f"{folder}/{name}"
        return library.load_asset(path) if library.does_asset_exist(path) else assets.create_asset(name, folder, cls, factory)

    material = make("M_SpellGlow", "/Game/Art/Effects/CireSpell", unreal.Material, unreal.MaterialFactoryNew())
    editor.delete_all_material_expressions(material)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("disable_depth_test", False)
    vertex = editor.create_material_expression(material, unreal.MaterialExpressionVertexColor, -320, 0)
    assert editor.connect_material_property(vertex, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    assert editor.connect_material_property(vertex, "A", unreal.MaterialProperty.MP_OPACITY)
    editor.layout_material_expressions(material)
    editor.recompile_material(material)
    assert library.save_loaded_asset(material, only_if_is_dirty=False)

    stone = make("M_Runestone", "/Game/Art/Effects/CireSpell", unreal.Material, unreal.MaterialFactoryNew())
    editor.delete_all_material_expressions(stone)
    stone.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    stone.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    base = editor.create_material_expression(stone, unreal.MaterialExpressionConstant3Vector, -250, 0)
    base.set_editor_property("constant", unreal.LinearColor(.065, .073, .082, 1))
    rough = editor.create_material_expression(stone, unreal.MaterialExpressionConstant, -250, 120)
    rough.set_editor_property("r", .82)
    metal = editor.create_material_expression(stone, unreal.MaterialExpressionConstant, -250, 230)
    metal.set_editor_property("r", .16)
    assert editor.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)
    assert editor.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    assert editor.connect_material_property(metal, "", unreal.MaterialProperty.MP_METALLIC)
    editor.layout_material_expressions(stone)
    editor.recompile_material(stone)
    assert library.save_loaded_asset(stone, only_if_is_dirty=False)

    concurrency = make("SC_Combat", "/Game/Audio/CireCombat", unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
    settings = concurrency.get_editor_property("concurrency")
    settings.set_editor_property("max_count", 16)
    settings.set_editor_property("limit_to_owner", False)
    settings.set_editor_property("resolution_rule", unreal.MaxConcurrentResolutionRule.STOP_FARTHEST_THEN_OLDEST)
    settings.set_editor_property("retrigger_time", .008)
    concurrency.set_editor_property("concurrency", settings)
    assert library.save_loaded_asset(concurrency, only_if_is_dirty=False)

    for name in NAMES:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(SOURCES / f"S_{name}.wav"))
        task.set_editor_property("destination_path", "/Game/Audio/CireCombat")
        task.set_editor_property("destination_name", f"S_{name}")
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        assets.import_asset_tasks([task])
        wave_asset = library.load_asset(f"/Game/Audio/CireCombat/S_{name}")
        if not wave_asset:
            raise RuntimeError(f"Missing imported waveform: {name}")
        wave_asset.set_editor_property("looping", False)
        assert library.save_loaded_asset(wave_asset, only_if_is_dirty=False)
    unreal.log("CIRE_SPELL_CONTENT_PASS materials=2 original_sounds=9 shared_voices=16")


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        log = STAGING / "Saved/Logs/SpellContent.log"
        console = STAGING / "Saved/Logs/SpellContent-console.log"
        log.parent.mkdir(parents=True, exist_ok=True)
        command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(STAGING / "ContentBuilder.uproject"),
                   "-unattended", "-nullrhi", "-nosplash", "-nosound", "-nop4", "-run=pythonscript",
                   f"-script={Path(__file__).resolve()}", "-stdout", "-FullStdOutLogOutput", f"-abslog={log}"]
        with console.open("w", encoding="utf-8") as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        contents = log.read_text(encoding="utf-8", errors="replace")
        if result.returncode or "CIRE_SPELL_CONTENT_PASS" not in contents:
            raise RuntimeError(f"Spell generation failed: {log}")
        for relative in ("Art/Effects/CireSpell", "Audio/CireCombat"):
            source, destination = STAGING / "Content" / relative, ROOT / "Content" / relative
            destination.mkdir(parents=True, exist_ok=True)
            for path in source.glob("*.uasset"):
                shutil.copy2(path, destination / path.name)
        print("CIRE_SPELL_CONTENT_COPIED materials=2 sounds=9 concurrency=1")
    else:
        build(unreal)
