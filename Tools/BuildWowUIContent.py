"""Import the WoW-style interface fonts and generate/import its original UI sounds.

Runs in the isolated Tools/ContentBuilder project (like the other content tools),
then copies the resulting .uasset files into Content/UI/WowUI.

Fonts: OFL-licensed TTFs under Content/UI/WowUI/Fonts/src (see Content/UI/WowUI/LICENSES.md).
Sounds: synthesized here from sine partials (original, no third-party audio).
"""
from pathlib import Path
import math
import os
import shutil
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parent.parent
STAGING = ROOT / "Tools/ContentBuilder"
FONT_SRC = ROOT / "Content/UI/WowUI/Fonts/src"
SOUND_SRC = ROOT / "Content/UI/WowUI/Sounds/src"
FONTS = {
    "UIHeading": "marcellus_Marcellus-Regular.ttf",
    "UIBody": "alegreyasans_AlegreyaSans-Medium.ttf",
    "UIBodyRegular": "alegreyasans_AlegreyaSans-Regular.ttf",
    "UIBold": "alegreyasans_AlegreyaSans-Bold.ttf",
    "UINumbers": "alegreyasans_AlegreyaSans-ExtraBold.ttf",
}
RATE = 44100


def _write(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    peak = max(1e-6, max(abs(s) for s in samples))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s / peak * .82)) * 32767)) for s in samples))


def _tone(freqs, start, length, total, decay=4.0, attack=.008, gain=1.0):
    buf = [0.0] * total
    first = int(start * RATE)
    for i in range(int(length * RATE)):
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-decay * t)
        v = sum(a * math.sin(2 * math.pi * f * t) for f, a in freqs)
        if first + i < total:
            buf[first + i] += v * env * gain
    return buf


def _mix(*layers):
    return [sum(v) for v in zip(*layers)]


def synthesize():
    # Level up: a rising major arpeggio with bell partials and a long shimmer tail.
    total = int(2.4 * RATE)
    notes = [523.25, 659.25, 783.99, 1046.5]
    layers = [_tone([(f, 1), (f * 2.0, .35), (f * 3.01, .18)], .0 + i * .11, 1.9, total, decay=2.3) for i, f in enumerate(notes)]
    layers.append(_tone([(2093.0, .25), (2637.0, .2), (3136.0, .15)], .42, 1.9, total, decay=1.6, attack=.2))
    _write(SOUND_SRC / "S_LevelUp.wav", _mix(*layers))
    # Aggro gained: two short descending brass-like warning pulses.
    total = int(.75 * RATE)
    _write(SOUND_SRC / "S_AggroGained.wav", _mix(
        _tone([(622.0, 1), (1244.0, .5), (1866.0, .25)], 0, .3, total, decay=9),
        _tone([(466.0, 1), (932.0, .5), (1398.0, .25)], .2, .45, total, decay=7)))
    # Threat warning: a soft single rising ping.
    total = int(.5 * RATE)
    _write(SOUND_SRC / "S_ThreatWarning.wav", _tone([(880.0, 1), (1320.0, .4)], 0, .45, total, decay=8))
    # Aggro lost (tank): a low falling knock.
    total = int(.6 * RATE)
    _write(SOUND_SRC / "S_AggroLost.wav", _mix(
        _tone([(392.0, 1), (784.0, .3)], 0, .3, total, decay=10),
        _tone([(294.0, 1), (588.0, .3)], .14, .45, total, decay=8)))
    # Target acquired: a short soft click.
    total = int(.12 * RATE)
    _write(SOUND_SRC / "S_TargetSelect.wav", _tone([(1760.0, .6), (2640.0, .3)], 0, .12, total, decay=40, attack=.002))


def build(unreal):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    library = unreal.EditorAssetLibrary
    tasks = []
    for name, file in FONTS.items():
        task = unreal.AssetImportTask()
        task.filename = str(FONT_SRC / file)
        task.destination_path = "/Game/UI/WowUI/Fonts"
        task.destination_name = name
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    for wav in sorted(SOUND_SRC.glob("*.wav")):
        task = unreal.AssetImportTask()
        task.filename = str(wav)
        task.destination_path = "/Game/UI/WowUI/Sounds"
        task.destination_name = wav.stem
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    for name in FONTS:
        path = "/Game/UI/WowUI/Fonts/" + name
        asset = library.load_asset(path)
        if not asset or not isinstance(asset, unreal.FontFace):
            raise RuntimeError("Font face import failed: " + path)
        asset.set_editor_property("hinting", unreal.FontHinting.AUTO)
        asset.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
        library.save_loaded_asset(asset, only_if_is_dirty=False)
    for wav in SOUND_SRC.glob("*.wav"):
        if not library.does_asset_exist("/Game/UI/WowUI/Sounds/" + wav.stem):
            raise RuntimeError("Sound import failed: " + wav.stem)
    unreal.log("CIRE_WOWUI_CONTENT_PASS")


def run_in_editor(unreal):
    try:
        build(unreal)
    except Exception as error:  # report and still quit so the process never hangs
        unreal.log_error("CIRE_WOWUI_CONTENT_FAIL " + str(error))
    unreal.SystemLibrary.quit_editor()


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        synthesize()
        logs = STAGING / "Saved/Logs"
        logs.mkdir(parents=True, exist_ok=True)
        log = logs / "WowUIContent.log"
        # UFontFace import needs a live Slate application, which -run=pythonscript lacks,
        # so this runs the full editor offscreen and quits from the script.
        command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe", str(STAGING / "ContentBuilder.uproject"),
                   "-unattended", "-RenderOffscreen", "-nosplash", "-nosound", "-nop4", "-NoLiveCoding",
                   f"-ExecutePythonScript={Path(__file__).resolve()}", f"-abslog={log}"]
        with (logs / "WowUIContent-console.log").open("w", encoding="utf-8") as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        if "CIRE_WOWUI_CONTENT_PASS" not in log.read_text(encoding="utf-8", errors="replace"):
            raise RuntimeError(f"WoW UI content generation failed: {log}")
        for sub in ("Fonts", "Sounds"):
            source = STAGING / "Content/UI/WowUI" / sub
            destination = ROOT / "Content/UI/WowUI" / sub
            destination.mkdir(parents=True, exist_ok=True)
            for asset in source.glob("*.uasset"):
                shutil.copy2(asset, destination / asset.name)
                print(f"CIRE_WOWUI_COPIED {destination / asset.name}")
    else:
        run_in_editor(unreal)
