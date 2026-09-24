"""Decode the downloaded Ogg Vorbis sources to 16-bit PCM WAV using Unreal's own importer.

The bundled engine Python has no codec libraries, so this script imports each Ogg into a
throw-away package inside the isolated Tools/ContentBuilder project, exports the imported
SoundWave with Unreal's WAV exporter and deletes the package again. Nothing touches the
game's Content folder. Output: Art/Downloads/Audio/decoded/<provider>/<name>.wav
(git-ignored); Tools/ProcessAudio.py turns those into the shipped WAVs.

Run with the engine Python (it relaunches itself inside UnrealEditor-Cmd):
  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/DecodeAudioSources.py
"""
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGING = ROOT / "Tools" / "ContentBuilder"
DOWNLOADS = ROOT / "Art" / "Downloads" / "Audio"
DECODED = DOWNLOADS / "decoded"
PACKAGE = "/Game/_CireAudioDecode"

KENNEY_FILES = {
    "impact-sounds": ["footstep_concrete_00%d" % i for i in range(5)] + ["footstep_grass_00%d" % i for i in range(5)]
    + ["footstep_carpet_00%d" % i for i in range(5)] + ["impactPlate_light_00%d" % i for i in range(5)]
    + ["impactPlate_medium_00%d" % i for i in range(5)] + ["impactMining_00%d" % i for i in range(5)]
    + ["impactPunch_heavy_00%d" % i for i in range(5)] + ["footstep_wood_00%d" % i for i in range(5)],
    "rpg-audio": ["cloth1", "cloth2", "cloth3", "cloth4", "creak1", "creak2", "creak3", "doorOpen_1", "doorOpen_2",
                  "doorClose_1", "doorClose_2", "doorClose_3", "doorClose_4", "handleCoins", "handleCoins2",
                  "handleSmallLeather", "handleSmallLeather2", "beltHandle1", "beltHandle2", "metalLatch", "chop"],
    "interface-sounds": ["click_001", "click_002", "click_003", "select_001", "select_002", "back_001",
                         "confirmation_002", "drop_002"],
    "ui-audio": ["rollover1", "rollover2", "rollover3", "rollover4", "rollover5", "rollover6", "click1", "click3"],
}


def sources():
    # aura-vfx: CIRE_AUDIO_ONLY=key1,key2 decodes just those Freesound sources (no Kenney packs needed).
    only = {k for k in os.environ.get("CIRE_AUDIO_ONLY", "").split(",") if k}
    for ogg in sorted((DOWNLOADS / "freesound").glob("*.ogg")):
        if not only or ogg.stem in only:
            yield "freesound", ogg
    if only:
        return
    for pack, names in KENNEY_FILES.items():
        folder = DOWNLOADS / "kenney" / pack
        for name in names:
            found = list(folder.rglob(name + ".ogg"))
            if not found:
                raise FileNotFoundError("%s/%s.ogg" % (pack, name))
            yield "kenney", found[0]


def run_inside_unreal():
    import unreal
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    library = unreal.EditorAssetLibrary
    if library.does_directory_exist(PACKAGE):
        library.delete_directory(PACKAGE)
    count = 0
    for provider, path in sources():
        out = DECODED / provider / (path.stem + ".wav")
        out.parent.mkdir(parents=True, exist_ok=True)
        name = "D_" + "".join(c if c.isalnum() else "_" for c in path.stem)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(path))
        task.set_editor_property("destination_path", PACKAGE)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tools.import_asset_tasks([task])
        wave = library.load_asset("%s/%s" % (PACKAGE, name))
        if not wave:
            raise RuntimeError("import failed: %s" % path)
        export = unreal.AssetExportTask()
        export.set_editor_property("object", wave)
        export.set_editor_property("exporter", unreal.SoundExporterWAV())
        export.set_editor_property("filename", str(out))
        export.set_editor_property("automated", True)
        export.set_editor_property("prompt", False)
        export.set_editor_property("replace_identical", True)
        if not unreal.Exporter.run_asset_export_task(export) or not out.exists():
            raise RuntimeError("export failed: %s" % path)
        count += 1
    library.delete_directory(PACKAGE)
    unreal.log("CIRE_AUDIO_DECODE_PASS files=%d" % count)


def launch():
    log = STAGING / "Saved" / "Logs" / "AudioDecode.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(STAGING / "ContentBuilder.uproject"),
               "-unattended", "-nullrhi", "-nosplash", "-nosound", "-nop4", "-run=pythonscript",
               "-script=%s" % Path(__file__).resolve(), "-stdout", "-FullStdOutLogOutput", "-abslog=%s" % log]
    with (log.parent / "AudioDecode-console.log").open("w", encoding="utf-8") as console:
        result = subprocess.run(command, stdout=console, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text("utf-8", "replace") if log.exists() else ""
    if result.returncode or "CIRE_AUDIO_DECODE_PASS" not in text:
        raise SystemExit("decode failed (exit %s); see %s" % (result.returncode, log))
    print([l for l in text.splitlines() if "CIRE_AUDIO_DECODE_PASS" in l][-1])


if __name__ == "__main__":
    try:
        import unreal  # noqa: F401
        run_inside_unreal()
    except ImportError:
        launch()
