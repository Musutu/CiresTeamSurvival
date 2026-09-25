"""progression-shop: import the Skill Shop art and display font.

* Scrolls + crests: Content/UI/Shop/Scrolls/src/*.png (cut from Eric's reference image by
  Tools/CutSkillScrolls.py) -> /Game/UI/Shop/Scrolls/<name>, UI textures WITH mipmaps so the
  scroll cards stay clean when drawn at a fraction of their source size.
* T_ShopGlow: a soft radial glow (white, alpha falloff) generated here, for the scroll light,
  sparks and seal flashes (the WowUI T_Glow is a framed panel glow, not a radial one).
* Display font: Content/UI/WowUI/Fonts/src/cinzel_Cinzel-wght.ttf (SIL OFL 1.1) ->
  /Game/UI/WowUI/Fonts/UIDisplay (the wide-spaced serif caps of the Skill Shop headings).

Runs the full editor offscreen in an isolated staging project (UFontFace import needs Slate),
then copies only those .uasset files into Content. Nothing else in Content is touched.

Run with the engine's Python:  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/BuildSkillShopArt.py
"""
from pathlib import Path
import math
import os
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parent.parent
STAGING = ROOT / "Saved/SkillShopArtBuilder"
SCROLL_SRC = ROOT / "Content/UI/Shop/Scrolls/src"
FONT = ROOT / "Content/UI/WowUI/Fonts/src/cinzel_Cinzel-wght.ttf"
EDITOR = "F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"


def write_glow(path, size=128):
    """Soft radial glow, premultiplied (RGB = alpha = (1 - r)^2.4, 0 at the edge). Pure Python PNG."""
    rows = []
    half = (size - 1) / 2
    for y in range(size):
        row = bytearray([0])
        for x in range(size):
            r = min(1.0, math.hypot(x - half, y - half) / half)
            a = int(round(255 * (1 - r) ** 2.4))
            row += bytes((a, a, a, a))  # premultiplied: additive canvas blending ignores alpha
        rows.append(bytes(row))
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)
    png = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]) + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    path.write_bytes(png)


def build(unreal):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    library = unreal.EditorAssetLibrary
    tasks = []

    def task(filename, path, name):
        t = unreal.AssetImportTask()
        t.filename = str(filename)
        t.destination_path = path
        t.destination_name = name
        t.automated = True
        t.replace_existing = True
        t.save = True
        tasks.append(t)

    for png in sorted(SCROLL_SRC.glob("*.png")):
        task(png, "/Game/UI/Shop/Scrolls", png.stem)
    task(FONT, "/Game/UI/WowUI/Fonts", "UIDisplay")
    tools.import_asset_tasks(tasks)
    for png in SCROLL_SRC.glob("*.png"):
        texture = library.load_asset("/Game/UI/Shop/Scrolls/" + png.stem)
        if not texture:
            raise RuntimeError("Scroll import failed: " + png.stem)
        texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        texture.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
        texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        texture.set_editor_property("never_stream", True)
        texture.set_editor_property("srgb", True)
        library.save_loaded_asset(texture, only_if_is_dirty=False)
    face = library.load_asset("/Game/UI/WowUI/Fonts/UIDisplay")
    if not face or not isinstance(face, unreal.FontFace):
        raise RuntimeError("Display font import failed")
    face.set_editor_property("hinting", unreal.FontHinting.AUTO_LIGHT)
    face.set_editor_property("loading_policy", unreal.FontLoadingPolicy.INLINE)
    library.save_loaded_asset(face, only_if_is_dirty=False)
    unreal.log("CIRE_SKILLSHOP_ART_PASS")


def main():
    write_glow(SCROLL_SRC / "T_ShopGlow.png")
    (STAGING / "Config").mkdir(parents=True, exist_ok=True)
    shutil.copy2(ROOT / "Tools/ContentBuilder/ContentBuilder.uproject", STAGING / "SkillShopArtBuilder.uproject")
    shutil.copy2(ROOT / "Tools/ContentBuilder/Config/DefaultEngine.ini", STAGING / "Config/DefaultEngine.ini")
    for sub in ("Content/UI/Shop/Scrolls", "Content/UI/WowUI/Fonts"):
        shutil.rmtree(STAGING / sub, ignore_errors=True)
    logs = STAGING / "Saved/Logs"
    logs.mkdir(parents=True, exist_ok=True)
    log = logs / "SkillShopArt.log"
    command = [EDITOR, str(STAGING / "SkillShopArtBuilder.uproject"), "-unattended", "-RenderOffscreen", "-nosplash", "-nosound",
               "-nop4", "-NoLiveCoding", f"-ExecutePythonScript={Path(__file__).resolve()}", f"-abslog={log}"]
    with (logs / "SkillShopArt-console.log").open("w", encoding="utf-8") as output:
        subprocess.run(command, stdout=output, stderr=subprocess.STDOUT, timeout=1800,
                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if "CIRE_SKILLSHOP_ART_PASS" not in log.read_text(encoding="utf-8", errors="replace"):
        raise SystemExit(f"Skill Shop art import failed: {log}")
    copies = [(STAGING / "Content/UI/Shop/Scrolls", ROOT / "Content/UI/Shop/Scrolls", "*.uasset"),
              (STAGING / "Content/UI/WowUI/Fonts", ROOT / "Content/UI/WowUI/Fonts", "UIDisplay.uasset")]
    for source, destination, pattern in copies:
        destination.mkdir(parents=True, exist_ok=True)
        for asset in source.glob(pattern):
            target = destination / asset.name
            if target.exists():
                os.chmod(target, 0o666)  # LFS lockable files are checked out read-only
            shutil.copy2(asset, target)
            print("CIRE_SKILLSHOP_ART_COPIED", target)


def run_in_editor(unreal):
    try:
        build(unreal)
    except Exception as error:  # report and still quit so the process never hangs
        unreal.log_error("CIRE_SKILLSHOP_ART_FAIL " + str(error))
    unreal.SystemLibrary.quit_editor()


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        main()
    else:
        run_in_editor(unreal)
