"""Build the game's audio content: mix hierarchy + every imported sound.

Runs inside the isolated Tools/ContentBuilder project (no game module needed) and copies the
results into Content/Audio/{Mix,Music,Ambience,Footsteps,SFX,UI}. The legacy
Content/Audio/CireCombat folder is never touched.

  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/BuildAudioContent.py

Creates
  /Game/Audio/Mix/SCL_*   sound classes  Master > Music, SFX (> Footsteps), Ambience, UI, Voice
  /Game/Audio/Mix/SMX_*   submixes       one per bus, so each can be recorded / metered
  /Game/Audio/Mix/SC_*    concurrency    footsteps 14, ambience one-shots 6, emitters 8, UI 3, events 8, music 3
  /Game/Audio/Mix/ATT_*   attenuation    Footstep, Prop, Ambient, Large
  /Game/Audio/Mix/REV_StoneStreets       reverb for narrow stone streets (activated at runtime)
  /Game/Audio/<Category>/<Name>          SoundWaves from Art/Downloads/Audio/{processed,music}
Inputs come from Tools/FetchAudioSources.py -> DecodeAudioSources.py -> ProcessAudio.py.
"""
import json
import os
import shutil
import stat
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
STAGING = ROOT / "Tools" / "ContentBuilder"
PROCESSED = ROOT / "Art" / "Downloads" / "Audio" / "processed"
MUSIC = ROOT / "Art" / "Downloads" / "Audio" / "music"
REPORT = ROOT / "Art" / "Audio" / "ProcessReport.json"
FOLDERS = ("Mix", "Music", "Ambience", "Footsteps", "SFX", "UI")
MIX = "/Game/Audio/Mix"

LOOPING_MUSIC = {"MUS_ThePyre", "MUS_OppressiveGloom", "MUS_FiveArmies", "MUS_Crusade", "MUS_Killers",
                 "MUS_BlackVortex", "MUS_DeathandAxes"}


def run_inside_unreal():
    import unreal
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    library = unreal.EditorAssetLibrary
    problems = []

    def setp(obj, name, value):
        """Set a property; `name` may list alternates ("a|b") because Python names drift between versions."""
        error = None
        for candidate in name.split("|"):
            try:
                obj.set_editor_property(candidate, value)
                return True
            except Exception as e:
                error = e
        problems.append("%s.%s: %s" % (type(obj).__name__, name, error))
        return False

    def make(name, cls, factory, path=MIX):
        full = "%s/%s" % (path, name)
        if library.does_asset_exist(full):
            return library.load_asset(full)
        return tools.create_asset(name, path, cls, factory)

    # ---- submixes -------------------------------------------------------------------
    submixes = {}
    for bus in ("Music", "SFX", "Ambience", "UI", "Voice"):
        submixes[bus] = make("SMX_" + bus, unreal.SoundSubmix, unreal.SoundSubmixFactory())

    # ---- sound classes ----------------------------------------------------------------
    master = make("SCL_Master", unreal.SoundClass, unreal.SoundClassFactory())
    classes = {"Master": master}
    for bus in ("Music", "SFX", "Footsteps", "Ambience", "UI", "Voice"):
        classes[bus] = make("SCL_" + bus, unreal.SoundClass, unreal.SoundClassFactory())

    def configure_class(name, parent, submix, reverb, reverb_2d, ui=False, music=False):
        cls = classes[name]
        props = cls.get_editor_property("properties")
        setp(props, "volume", 1.0)
        setp(props, "reverb", reverb)
        setp(props, "default2d_reverb_send_amount|default2_d_reverb_send_amount|default_2d_reverb_send_amount", reverb_2d)
        setp(props, "is_ui_sound", ui)
        setp(props, "is_music", music)
        setp(props, "default_submix", submixes[submix])
        setp(cls, "properties", props)
        setp(cls, "parent_class", parent)
    configure_class("Music", master, "Music", False, 0.0, music=True)
    configure_class("SFX", master, "SFX", True, 0.2)
    configure_class("Footsteps", classes["SFX"], "SFX", True, 0.0)
    configure_class("Ambience", master, "Ambience", True, 0.15)
    configure_class("UI", master, "UI", False, 0.0, ui=True)
    configure_class("Voice", master, "Voice", True, 0.1)
    setp(master, "child_classes", [classes[b] for b in ("Music", "SFX", "Ambience", "UI", "Voice")])
    setp(classes["SFX"], "child_classes", [classes["Footsteps"]])

    # ---- concurrency --------------------------------------------------------------------
    def concurrency(name, count, rule, retrigger=0.0, owner=False):
        asset = make(name, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
        settings = asset.get_editor_property("concurrency")
        setp(settings, "max_count", count)
        setp(settings, "limit_to_owner", owner)
        setp(settings, "resolution_rule", rule)
        setp(settings, "retrigger_time", retrigger)
        setp(asset, "concurrency", settings)
        return asset
    R = unreal.MaxConcurrentResolutionRule
    conc = {
        "Footsteps": concurrency("SC_Footsteps", 14, R.STOP_QUIETEST, .03),
        "AmbienceOneShot": concurrency("SC_AmbienceOneShots", 6, R.STOP_OLDEST),
        "Emitters": concurrency("SC_Emitters", 8, R.STOP_FARTHEST_THEN_OLDEST),
        "UI": concurrency("SC_UI", 3, R.STOP_OLDEST, .03),
        "Events": concurrency("SC_Events", 8, R.STOP_OLDEST, .05),
        "Music": concurrency("SC_Music", 3, R.STOP_OLDEST),
    }

    # ---- attenuation presets ---------------------------------------------------------------
    def attenuation(name, inner, falloff, reverb_min, reverb_max, lpf_far=2500.0):
        asset = make(name, unreal.SoundAttenuation, unreal.SoundAttenuationFactory())
        a = asset.get_editor_property("attenuation")
        setp(a, "attenuate", True)
        setp(a, "spatialize", True)
        setp(a, "distance_algorithm", unreal.AttenuationDistanceModel.NATURAL_SOUND)
        setp(a, "attenuation_shape", unreal.AttenuationShape.SPHERE)
        setp(a, "attenuation_shape_extents", unreal.Vector(inner, 0, 0))
        setp(a, "falloff_distance", falloff)
        setp(a, "attenuate_with_lpf", True)
        setp(a, "lpf_radius_min", inner)
        setp(a, "lpf_radius_max", inner + falloff)
        setp(a, "lpf_frequency_at_min", 20000.0)
        setp(a, "lpf_frequency_at_max", lpf_far)
        setp(a, "enable_reverb_send", True)
        setp(a, "reverb_wet_level_min", reverb_min)
        setp(a, "reverb_wet_level_max", reverb_max)
        setp(a, "reverb_distance_min", inner)
        setp(a, "reverb_distance_max", inner + falloff)
        setp(a, "non_spatialized_radius_start", 60.0)
        setp(a, "non_spatialized_radius_end", 20.0)
        setp(asset, "attenuation", a)
        return asset
    att = {
        "Footstep": attenuation("ATT_Footstep", 150, 2400, .15, .45, 3500),
        "Prop": attenuation("ATT_Prop", 250, 2800, .2, .55, 3000),
        "Ambient": attenuation("ATT_Ambient", 600, 5200, .3, .7, 2200),
        "Large": attenuation("ATT_Large", 1200, 12000, .3, .8, 3000),
    }

    # ---- reverb ---------------------------------------------------------------------------------
    rev = make("REV_StoneStreets", unreal.ReverbEffect, unreal.ReverbEffectFactory())
    for key, value in {"density": .85, "diffusion": .7, "gain": .32, "gain_hf": .78, "decay_time": 1.45,
                       "decay_hf_ratio": .72, "reflections_gain": .35, "reflections_delay": .012,
                       "late_gain": 1.1, "late_delay": .02, "air_absorption_gain_hf": .994}.items():
        setp(rev, key, value)

    for asset in list(submixes.values()) + list(classes.values()) + list(conc.values()) + list(att.values()) + [rev]:
        library.save_loaded_asset(asset, only_if_is_dirty=False)

    # ---- sounds -----------------------------------------------------------------------------------
    report = json.loads(REPORT.read_text("utf-8"))
    imports = [(PROCESSED / o["category"] / (o["name"] + ".wav"), o["category"], o["name"], o) for o in report["outputs"]]
    imports += [(p, "Music", p.stem, {"loop": p.stem in LOOPING_MUSIC}) for p in sorted(MUSIC.glob("MUS_*.mp3"))]
    count = 0
    for path, category, name, meta in imports:
        if not path.exists():
            raise RuntimeError("missing source " + str(path))
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(path))
        task.set_editor_property("destination_path", "/Game/Audio/" + category)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tools.import_asset_tasks([task])
        wave = library.load_asset("/Game/Audio/%s/%s" % (category, name))
        if not wave:
            raise RuntimeError("import failed " + str(path))
        looping = bool(meta.get("loop"))
        setp(wave, "looping", looping)
        if looping:
            setp(wave, "virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
        if category == "Music":
            setp(wave, "sound_class_object", classes["Music"])
            setp(wave, "concurrency_set", [conc["Music"]])
        elif category == "UI":
            setp(wave, "sound_class_object", classes["UI"])
            setp(wave, "concurrency_set", [conc["UI"]])
        elif category == "Footsteps":
            setp(wave, "sound_class_object", classes["Footsteps"])
            setp(wave, "attenuation_settings", att["Footstep"])
            setp(wave, "concurrency_set", [conc["Footsteps"]])
        elif category == "Ambience":
            setp(wave, "sound_class_object", classes["Ambience"])
            if name.startswith("EMT_"):
                setp(wave, "attenuation_settings", att["Prop"])
                setp(wave, "concurrency_set", [conc["Emitters"]])
            elif name.startswith("AMB_") and not looping:
                setp(wave, "attenuation_settings", att["Ambient"])
                setp(wave, "concurrency_set", [conc["AmbienceOneShot"]])
        else:  # SFX events: 2D unless the runtime passes a location (then ATT_Large applies)
            setp(wave, "sound_class_object", classes["SFX"])
            setp(wave, "concurrency_set", [conc["Events"]])
            if meta.get("positional"):
                setp(wave, "attenuation_settings", att["Large"])
        # Route explicitly as well as through the class default, so each bus can be recorded/metered.
        bus = {"Music": "Music", "UI": "UI", "Footsteps": "SFX", "Ambience": "Ambience"}.get(category, "SFX")
        setp(wave, "sound_submix_object", submixes[bus])
        library.save_loaded_asset(wave, only_if_is_dirty=False)
        count += 1
    for p in problems:
        unreal.log_warning("CIRE_AUDIO_PROPERTY " + p)
    unreal.log("CIRE_AUDIO_CONTENT_PASS sounds=%d classes=%d submixes=%d problems=%d" % (count, len(classes), len(submixes), len(problems)))


def remove_tree(path):
    """rmtree that also clears read-only flags (Git LFS 'lockable' checkouts are read-only)."""
    def clear(func, target, _):
        os.chmod(target, stat.S_IWRITE)
        func(target)
    if Path(path).exists():
        shutil.rmtree(path, onerror=clear)


def launch():
    log = STAGING / "Saved" / "Logs" / "AudioContent.log"
    log.parent.mkdir(parents=True, exist_ok=True)
    for folder in FOLDERS:  # rebuild cleanly so renamed/removed sounds do not linger
        remove_tree(STAGING / "Content" / "Audio" / folder)
    command = ["F:/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe", str(STAGING / "ContentBuilder.uproject"),
               "-unattended", "-nullrhi", "-nosplash", "-nosound", "-nop4", "-run=pythonscript",
               "-script=%s" % Path(__file__).resolve(), "-stdout", "-FullStdOutLogOutput", "-abslog=%s" % log]
    with (log.parent / "AudioContent-console.log").open("w", encoding="utf-8") as console:
        result = subprocess.run(command, stdout=console, stderr=subprocess.STDOUT,
                                creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    text = log.read_text("utf-8", "replace") if log.exists() else ""
    if result.returncode or "CIRE_AUDIO_CONTENT_PASS" not in text:
        raise SystemExit("audio content build failed (exit %s); see %s" % (result.returncode, log))
    for line in text.splitlines():
        if "CIRE_AUDIO_PROPERTY" in line or "CIRE_AUDIO_CONTENT_PASS" in line:
            print(line.split("LogPython: ")[-1])
    for folder in FOLDERS:
        source, destination = STAGING / "Content" / "Audio" / folder, ROOT / "Content" / "Audio" / folder
        remove_tree(destination)
        shutil.copytree(source, destination)
        remove_tree(source)  # the staging copy is not versioned; Content/Audio is the source of truth
    print("copied", ", ".join(FOLDERS), "into Content/Audio")


if __name__ == "__main__":
    try:
        import unreal  # noqa: F401
        run_inside_unreal()
    except ImportError:
        launch()
