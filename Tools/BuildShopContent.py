"""progression-shop: build the shop/item UI content (icons + sounds) and import it as assets.

1. Tools/BuildItemIcons.py renders the original item icons to Content/UI/Items/src/*.png.
2. This script synthesizes the original shop sounds to Content/UI/Shop/src/*.wav (sine/noise
   partials; no third-party audio).
3. An isolated staging project (Saved/ItemContentBuilder, a copy of Tools/ContentBuilder's
   settings) imports them offscreen as /Game/UI/Items/T_Item_<id> (UI textures) and
   /Game/UI/Shop/S_* (sound waves); the .uasset files are copied into Content/UI/Items and
   Content/UI/Shop. Nothing else in Content is touched.

Run with the engine's Python:  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/BuildShopContent.py
"""
from pathlib import Path
import math
import os
import random
import shutil
import struct
import subprocess
import wave

ROOT = Path(__file__).resolve().parent.parent
STAGING = ROOT / "Saved/ItemContentBuilder"
ICON_SRC = ROOT / "Content/UI/Items/src"
SOUND_SRC = ROOT / "Content/UI/Shop/src"
EDITOR = "F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
PYTHON = "F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"
RATE = 44100


def _write(path, samples):
    path.parent.mkdir(parents=True, exist_ok=True)
    peak = max(1e-6, max(abs(s) for s in samples))
    with wave.open(str(path), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s / peak * .8)) * 32767)) for s in samples))


def _tone(buf, freqs, start, length, decay=6.0, attack=.004, gain=1.0, glide=0.0):
    first = int(start * RATE)
    for i in range(int(length * RATE)):
        t = i / RATE
        env = min(1.0, t / attack) * math.exp(-decay * t)
        v = 0.0
        for f, a in freqs:
            ff = f * (1 + glide * t)
            v += a * math.sin(2 * math.pi * ff * t)
        if 0 <= first + i < len(buf):
            buf[first + i] += v * env * gain


def _noise(buf, start, length, decay, gain, seed, lowpass=.2):
    rnd = random.Random(seed)
    first = int(start * RATE)
    last = 0.0
    for i in range(int(length * RATE)):
        t = i / RATE
        last += (rnd.uniform(-1, 1) - last) * lowpass
        if 0 <= first + i < len(buf):
            buf[first + i] += last * math.exp(-decay * t) * gain


def coin(buf, start, pitch=1.0, gain=1.0):
    # A struck coin: bright inharmonic partials with a quick shimmer.
    _tone(buf, [(2637 * pitch, 1), (3951 * pitch, .6), (5274 * pitch, .35), (7040 * pitch, .2)], start, .45, decay=11, gain=gain)
    _noise(buf, start, .03, 90, .25 * gain, int(start * 1000 + pitch * 100), .6)


def synthesize():
    def sound(name, seconds, fill):
        buf = [0.0] * int(seconds * RATE)
        fill(buf)
        _write(SOUND_SRC / f"{name}.wav", buf)

    sound("S_ShopBuy", .7, lambda b: (coin(b, 0, 1.0), coin(b, .07, 1.19, .8), _tone(b, [(523, .5), (784, .3)], .0, .5, decay=7, gain=.5)))
    sound("S_ShopSell", .9, lambda b: [coin(b, k * .06, 0.9 + (k % 3) * .12, .9 - k * .1) for k in range(6)])
    sound("S_ShopError", .45, lambda b: (_tone(b, [(155, 1), (310, .5), (465, .3)], 0, .18, decay=8, attack=.003),
                                         _tone(b, [(147, 1), (294, .5), (441, .3)], .2, .22, decay=8, attack=.003)))
    sound("S_ShopUndo", .5, lambda b: (_tone(b, [(900, 1), (1350, .3)], 0, .35, decay=6, glide=-1.2), _noise(b, 0, .25, 12, .15, 5)))
    sound("S_ShopOpen", .8, lambda b: (_noise(b, 0, .2, 18, .4, 9, .08), _tone(b, [(392, .6), (587, .4), (784, .25)], .05, .7, decay=4.5)))
    sound("S_ShopTab", .12, lambda b: _tone(b, [(1800, 1), (2700, .3)], 0, .1, decay=45, attack=.001))
    sound("S_LootPickup", 1.2, lambda b: [(_tone(b, [(f, 1), (f * 2, .3)], k * .08, .8, decay=5, gain=.8), coin(b, k * .08 + .02, 1.3 + k * .1, .35))
                                          for k, f in enumerate((659, 784, 988, 1319))])
    sound("S_TeleportChannel", 1.6, lambda b: (_tone(b, [(330, .6), (495, .4), (660, .3)], 0, 1.5, decay=1.2, attack=.3, glide=.35),
                                               _noise(b, 0, 1.4, 1.5, .12, 11, .05)))
    sound("S_TeleportArrive", 1.4, lambda b: (_noise(b, 0, .4, 7, .5, 13, .15), _tone(b, [(784, 1), (1175, .5), (1568, .3)], .05, 1.2, decay=3.2)))
    print("sounds synthesized", flush=True)


def import_in_editor(unreal):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    library = unreal.EditorAssetLibrary
    tasks = []
    for png in sorted(ICON_SRC.glob("*.png")):
        task = unreal.AssetImportTask()
        task.filename = str(png)
        task.destination_path = "/Game/UI/Items"
        task.destination_name = png.stem
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    for wav in sorted(SOUND_SRC.glob("*.wav")):
        task = unreal.AssetImportTask()
        task.filename = str(wav)
        task.destination_path = "/Game/UI/Shop"
        task.destination_name = wav.stem
        task.automated = True
        task.replace_existing = True
        task.save = True
        tasks.append(task)
    tools.import_asset_tasks(tasks)
    for png in ICON_SRC.glob("*.png"):
        texture = library.load_asset("/Game/UI/Items/" + png.stem)
        if not texture:
            raise RuntimeError("Icon import failed: " + png.stem)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("srgb", True)
        library.save_loaded_asset(texture, only_if_is_dirty=False)
    for wav in SOUND_SRC.glob("*.wav"):
        if not library.does_asset_exist("/Game/UI/Shop/" + wav.stem):
            raise RuntimeError("Sound import failed: " + wav.stem)
    unreal.log("CIRE_SHOP_CONTENT_PASS")


def run_in_editor(unreal):
    try:
        import_in_editor(unreal)
    except Exception as error:  # report and still quit so the process never hangs
        unreal.log_error("CIRE_SHOP_CONTENT_FAIL " + str(error))
    unreal.SystemLibrary.quit_editor()


def main():
    subprocess.run([PYTHON, str(ROOT / "Tools/BuildItemIcons.py")], check=True)
    synthesize()
    # Isolated staging project: never the game project, never another tool's builder.
    (STAGING / "Config").mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "Tools/ContentBuilder/ContentBuilder.uproject", STAGING / "ItemContentBuilder.uproject")
    shutil.copy2(ROOT / "Tools/ContentBuilder/Config/DefaultEngine.ini", STAGING / "Config/DefaultEngine.ini")
    for sub in ("Content/UI/Items", "Content/UI/Shop"):
        shutil.rmtree(STAGING / sub, ignore_errors=True)
    logs = STAGING / "Saved/Logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / "ShopContent.log"
    command = [EDITOR, str(STAGING / "ItemContentBuilder.uproject"), "-unattended", "-RenderOffscreen", "-nosplash", "-nosound",
               "-nop4", "-NoLiveCoding", f"-ExecutePythonScript={Path(__file__).resolve()}", f"-abslog={log}"]
    with (logs / "ShopContent-console.log").open("w", encoding="utf-8") as output:
        subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=3600,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if "CIRE_SHOP_CONTENT_PASS" not in log.read_text(encoding="utf-8", errors="replace"):
        raise SystemExit(f"Shop content import failed: {log}")
    copied = 0
    for sub in ("Items", "Shop"):
        source = STAGING / "Content/UI" / sub
        destination = ROOT / "Content/UI" / sub
        destination.mkdir(parents=True, exist_ok=True)
        for asset in source.glob("*.uasset"):
            target = destination / asset.name
            if target.exists():
                os.chmod(target, 0o666)
            shutil.copy2(asset, target)
            copied += 1
    print(f"CIRE_SHOP_CONTENT_COPIED assets={copied}")


if __name__ == "__main__":
    try:
        import unreal  # noqa: F401  (inside the editor)
    except ImportError:
        main()
    else:
        run_in_editor(unreal)
