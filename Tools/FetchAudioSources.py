"""Download the free-licensed audio sources used by Cire's Team Survival.

Every source is CC0 (Freesound, Kenney) or CC-BY 4.0 (Kevin MacLeod / incompetech).
The Freesound files are the public HQ previews of CC0 uploads (no login needed).
Everything downloaded lands in Art/Downloads/Audio (git-ignored): Freesound/Kenney
sources, and the music MP3s in Art/Downloads/Audio/music. The game ships the imported
.uasset files; this script, Tools/DecodeAudioSources.py, Tools/ProcessAudio.py and
Tools/BuildAudioContent.py rebuild them byte-for-byte from the pinned sources below
(each record carries the sha256 of the file that was used).

Writes Art/Audio/AudioSources.json, the machine-readable provenance record that
Art/Audio/PROVENANCE.md summarises.

Usage:  python Tools/FetchAudioSources.py [--force] [--provenance]  (--provenance: only regenerate PROVENANCE.md)
Run with the engine's bundled Python (bare "python" may be a Store alias):
  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/FetchAudioSources.py
"""
import hashlib
import html
import json
import re
import sys
import time
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOWNLOADS = ROOT / "Art" / "Downloads" / "Audio"
MUSIC = ROOT / "Art" / "Downloads" / "Audio" / "music"
MANIFEST = ROOT / "Art" / "Audio" / "AudioSources.json"
UA = {"User-Agent": "Mozilla/5.0 (CiresTeamSurvival audio fetch)"}
CC0 = "CC0-1.0"
CC0_URL = "https://creativecommons.org/publicdomain/zero/1.0/"
CCBY = "CC-BY-4.0"
CCBY_URL = "https://creativecommons.org/licenses/by/4.0/"

# key: (freesound id, username, what it is used for)
FREESOUND = {
    "wind_rolling": (210220, "ERR0", "gate wind bed"),
    "wind_howl": (638434, "The_Isot_is_Back", "gate/castle cold wind layer"),
    "crows": (26959, "a1234", "gate crow one-shots"),
    "chains": (235534, "simosco", "gate chain rattles"),
    "gate_creak": (456763, "WavJunction.com", "gate creaks"),
    "market_festival": (703644, "hobonski", "market crowd bed (medieval festival)"),
    "market_square": (648483, "nicoproson", "market/square murmur layer"),
    "merchant_calls": (406578, "kyles", "distant merchant calls"),
    "ox_cart": (437078, "craigsmith", "market cart wheels"),
    "dogs_distant": (820267, "IENBA", "residential dog barks"),
    "dog_corgi": (581478, "rvandemark", "residential distant dog"),
    "wind_chimes": (437337, "giddster", "residential wind chimes"),
    "hearth": (836535, "SilverIllusionist", "residential hearth crackle"),
    "night_crickets_owl": (203598, "raoul_slayer", "night bed (crickets, tawny owl)"),
    "fountain": (36086, "cognito perceptu", "square fountain emitter"),
    "fountain_small": (676173, "Nox_Sound", "square fountain bed layer"),
    "pigeons": (154865, "pawsound", "square pigeons"),
    "bell_short": (479985, "craigsmith", "prep bell / square tolls"),
    "bell_funeral": (329324, "aoristos", "distant church bell (medieval church, Coupiac)"),
    "flag": (154794, "felix.blume", "castle banners flapping"),
    "blacksmith": (703642, "hobonski", "castle distant blacksmith (medieval festival)"),
    "anvil": (321889, "Duasun", "castle anvil strikes"),
    "guards_march": (480671, "craigsmith", "castle guards marching"),
    "knight_walk": (770083, "Vrymaa", "plate footsteps (knights walk/run)"),
    "boots_pavement": (777673, "YannSauvin", "leather boot footsteps on stone"),
    "steps_dirt": (264469, "aglinder", "footsteps on dirt/gravel"),
    "chainmail_grass": (384901, "Ali_6868", "chainmail footstep, grass"),
    "chainmail_gravel": (384890, "Ali_6868", "chainmail footstep, gravel"),
    "armor_chinks": (707667, "lukabea", "armor rattle layer"),
    "leather_creak": (536187, "IENBA", "leather creak layer"),
    "hooves_cobble": (204961, "skitchscharff", "centaur hooves on cobbles"),
    "hooves_2": (571312, "maciejadach", "centaur hooves on cobbles (alt)"),
    "horses_dirt": (437108, "craigsmith", "centaur hooves on dirt"),
    "shimmer": (632344, "adh.dreaming", "whisp faint shimmer"),
    "monster_stomp": (812538, "Yoyamen1212", "bear / beast heavy steps"),
    "bass_stomp": (334228, "oscaraudiogeek", "bear / golem low thump layer"),
    "monster_gravel": (712066, "AudioPapkin", "beast steps on dirt"),
    "fire_loop": (813328, "NickTayloe", "brazier / lamp crackle emitter"),
    "well": (649240, "Fedor_Ogon", "well winch and water emitter"),
    "war_horn": (539956, "adharca", "wave horn"),
    "war_horn_distant": (512490, "DeVern", "distant wave horn"),
    "war_drums": (209546, "Sclolex", "arena war drums"),
    "roar": (489901, "NicknameLarry", "Pack Leader roar"),
    "roar_growl": (366837, "Jofae", "Pack Leader growl"),
    "teleport": (466829, "Breviceps", "teleport arrive"),
    "teleport_hum": (670641, "SnowFightStudios", "teleport channel loop"),
    "sword_draw": (581594, "SamsterBirdies", "aggro-taken warning"),
    "fanfare": (350428, "bevibeldesign", "level-up fanfare"),
    "coins": (223343, "jalastram", "buy / sell coins"),
    # aura-vfx: buff / aura / empowered-attack sounds (Content/Audio/Auras, see Docs/BuffVisuals.md)
    "aura_heartbeat": (276578, "MickBoere", "Blood Rage / Blood Frenzy heartbeat loop"),
    "aura_snarl": (204611, "pikachu09", "rage and frenzy snarl"),
    "aura_blood_splat": (406582, "_stubb", "empowered blood splash on hit"),
    "aura_swing": (471097, "spycrah", "empowered swipe whoosh"),
    "aura_ice_break": (329744, "humanoide9000", "frost shatter on hit"),
    "aura_ice_crack": (262635, "j_p_higgins", "Frost Bind / Frost Weapon crackle"),
    "aura_chime": (660493, "lukabea", "holy chime (Sanctuary, Blessing)"),
    "aura_choir": (444491, "Breviceps", "Bastion of Dawn choir swell"),
    "aura_sparkle": (462095, "LilMati", "holy glint on hit, stars"),
    "aura_clang": (588278, "marchelonia", "Shield Wall / Iron Guard clang"),
    "aura_bubbles": (645909, "monosfera", "poison / mana bubbles"),
    "aura_flame": (592572, "magnuswaker", "Siege Fury flame burst, ember hit"),
    "aura_forcefield": (702772, "LilMati", "guards and aegis shields"),
    "aura_hourglass": (825859, "Vrymaa", "Borrowed Time sand"),
    "aura_gong": (415200, "Yin_Yang_Jake007", "Toll of the Grave"),
    "aura_warcry": (771323, "mellotrix9898", "War Cry / Rallied shout"),
    "aura_whoosh": (60013, "qubodup", "Scatter wind rush"),
    # arenas: per-arena ambience beds and one-shots (Docs/Arenas.md)
    "arena_wheat_wind": (240914, "bdvictor", "Sunlit Fields: wind through the wheat"),
    "arena_field": (447810, "florianreichelt", "Sunlit Fields: open field ambience"),
    "arena_skylark": (244357, "squashy555", "Sunlit Fields: skylark song"),
    "arena_sea": (450752, "florianreichelt", "Black Shore: surf on the beach"),
    "arena_gulls": (353416, "squashy555", "Black Shore: gull calls"),
    "arena_desert_wind": (112296, "slugzilla", "Redrock Canyon: dry desert wind"),
    "arena_canyon_wind": (135447, "ciccarelli", "Redrock Canyon: wind in the canyon"),
    "arena_woodland": (160893, "ecfike", "Hornbeam Glade: spring woodland"),
    "arena_dawn_chorus": (573080, "squashy555", "Hornbeam Glade: bird chorus"),
    "arena_underwater": (504641, "Fission9", "Drowned Sanctum: underwater ambience"),
    "arena_deep_sea": (193822, "jhumbucker", "Drowned Sanctum: deep-sea drone"),
    "arena_ship_hum": (221570, "AlaskaRobotics", "Star Station: spacecraft hum"),
    "arena_station_drone": (686237, "db3005", "Star Station: station drone"),
}

KENNEY = {
    "kenney_rpg_audio": "rpg-audio",
    "kenney_impact_sounds": "impact-sounds",
    "kenney_interface_sounds": "interface-sounds",
    "kenney_ui_audio": "ui-audio",
}

# Kevin MacLeod (incompetech.com), CC-BY 4.0. file stem -> (title, use)
MUSIC_TRACKS = {
    "MUS_ThePyre": ("The Pyre", "town / prep (night)"),
    "MUS_OppressiveGloom": ("Oppressive Gloom", "town / recovery alternate"),
    "MUS_FiveArmies": ("Five Armies", "combat waves"),
    "MUS_Crusade": ("Crusade", "combat waves alternate"),
    "MUS_Killers": ("Killers", "Pack Leader / lane boss"),
    "MUS_BlackVortex": ("Black Vortex", "boss alternate"),
    "MUS_DeathandAxes": ("Death and Axes", "arena PvP"),
    "MUS_HeroTheme": ("Hero Theme", "victory stinger"),
    "MUS_GretaSting": ("Greta Sting", "defeat stinger"),
}


def get(url, binary=False, tries=3):
    for attempt in range(tries):
        try:
            data = urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=90).read()
            return data if binary else data.decode("utf-8", "replace")
        except Exception:
            if attempt == tries - 1:
                raise
            time.sleep(2 + attempt * 3)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def freesound(key, sound_id, user, use, force):
    page = "https://freesound.org/people/%s/sounds/%d/" % (urllib.parse.quote(user), sound_id)
    text = get(page)
    title = re.search(r'data-title="([^"]*)"', text)
    lic = re.search(r'href="(https?://creativecommons.org/[^"]+)"', text)
    ogg = re.search(r'data-ogg="([^"]*)"', text)
    if not ogg or not lic:
        raise RuntimeError("could not parse freesound page " + page)
    lic_url = lic.group(1)
    if "publicdomain/zero" not in lic_url:
        raise RuntimeError("%s is not CC0 (%s); refusing" % (page, lic_url))
    url = ogg.group(1).replace("-lq.ogg", "-hq.ogg")
    out = DOWNLOADS / "freesound" / ("%s.ogg" % key)
    out.parent.mkdir(parents=True, exist_ok=True)
    if force or not out.exists():
        out.write_bytes(get(url, binary=True))
    return {
        "key": key, "provider": "Freesound", "id": sound_id, "author": user,
        "title": html.unescape(title.group(1)) if title else "", "page": page,
        "download": url, "license": CC0, "licenseUrl": CC0_URL, "use": use,
        "file": out.relative_to(ROOT).as_posix(), "sha256": sha256(out),
        "note": "Freesound public HQ preview (Ogg Vorbis) of the CC0 upload.",
    }


def kenney(key, slug, force):
    page = "https://kenney.nl/assets/" + slug
    text = get(page)
    zip_url = re.search(r'https://kenney\.nl/media/pages/assets/[^"]*\.zip', text).group(0)
    out = DOWNLOADS / "kenney" / (slug + ".zip")
    out.parent.mkdir(parents=True, exist_ok=True)
    if force or not out.exists():
        out.write_bytes(get(zip_url, binary=True))
    folder = out.with_suffix("")
    with zipfile.ZipFile(out) as z:
        z.extractall(folder)
    lic_text = next(folder.rglob("License.txt")).read_text("utf-8", "replace")
    if "CC0" not in lic_text and "Creative Commons Zero" not in lic_text:
        raise RuntimeError("Kenney pack %s license text is not CC0" % slug)
    return {
        "key": key, "provider": "Kenney", "author": "Kenney (www.kenney.nl)", "title": slug,
        "page": page, "download": zip_url, "license": CC0, "licenseUrl": CC0_URL,
        "file": out.relative_to(ROOT).as_posix(), "sha256": sha256(out),
    }


def music(stem, title, use, force):
    url = "https://incompetech.com/music/royalty-free/mp3-royaltyfree/%s.mp3" % urllib.parse.quote(title)
    out = MUSIC / (stem + ".mp3")
    out.parent.mkdir(parents=True, exist_ok=True)
    if force or not out.exists():
        out.write_bytes(get(url, binary=True))
    return {
        "key": stem, "provider": "incompetech", "author": "Kevin MacLeod", "title": title,
        "page": "https://incompetech.com/music/royalty-free/music.html", "download": url,
        "license": CCBY, "licenseUrl": CCBY_URL, "use": use,
        "attribution": '"%s" Kevin MacLeod (incompetech.com)\nLicensed under Creative Commons: By Attribution 4.0 License\nhttp://creativecommons.org/licenses/by/4.0/' % title,
        "file": out.relative_to(ROOT).as_posix(), "sha256": sha256(out),
    }


def write_provenance(records):
    """Art/Audio/PROVENANCE.md: every source, its licence and which shipped sounds use it."""
    report_path = ROOT / "Art" / "Audio" / "ProcessReport.json"
    used = {}
    if report_path.exists():
        for out in json.loads(report_path.read_text("utf-8"))["outputs"]:
            for src in out["sources"]:
                used.setdefault(src.split(":", 1)[-1] if src.startswith("kenney:") else src, []).append(out["name"])
    kenney_used = sorted({n for k, v in used.items() if k not in FREESOUND for n in v})
    lines = [
        "# Audio provenance",
        "",
        "Every sound shipped in `Content/Audio/{Music,Ambience,Footsteps,SFX,UI}` comes from the sources below.",
        "All are free for commercial use and redistribution: **CC0 1.0** (no attribution required) or **CC BY 4.0**",
        "(attribution required - given in-game under Options > Audio > Music credits, and here).",
        "The machine-readable record with download URLs and SHA-256 hashes is `Art/Audio/AudioSources.json`;",
        "`Art/Audio/ProcessReport.json` lists every processed output, its source keys and measured levels.",
        "Rebuild: `Tools/FetchAudioSources.py` -> `Tools/DecodeAudioSources.py` -> `Tools/ProcessAudio.py` -> `Tools/BuildAudioContent.py`.",
        "Freesound files are the site's public HQ previews (Ogg Vorbis) of CC0 uploads; the licence of each upload was",
        "checked by the fetch script, which refuses anything that is not CC0.",
        "",
        "The pre-existing `Content/Audio/CireCombat` and `Content/UI/WowUI/Sounds` sounds are original synthesized work",
        "from earlier passes and are not covered here.",
        "",
        "## Music - Kevin MacLeod (incompetech.com), CC BY 4.0",
        "",
        "Required attribution (also shown in game):",
        "",
    ]
    for r in records:
        if r["provider"] == "incompetech":
            lines.append("> %s  " % r["attribution"].replace("\n", "  \n> "))
            lines.append("")
    lines += ["| Asset | Title | Used for | Download |", "|---|---|---|---|"]
    lines += ["| `Music/%s` | %s | %s | %s |" % (r["key"], r["title"], r["use"], r["download"]) for r in records if r["provider"] == "incompetech"]
    lines += ["", "## Sound effects and ambience - Freesound.org, CC0 1.0", "",
              "| Key | Title | Author | Page | Shipped as |", "|---|---|---|---|---|"]
    for r in records:
        if r["provider"] == "Freesound":
            lines.append("| %s | %s | %s | %s | %s |" % (r["key"], r["title"].replace("|", "/"), r["author"], r["page"], ", ".join(sorted(used.get(r["key"], []))) or "-"))
    lines += ["", "## Kenney (www.kenney.nl), CC0 1.0", "",
              "| Pack | Page | Download |", "|---|---|---|"]
    lines += ["| %s | %s | %s |" % (r["title"], r["page"], r["download"]) for r in records if r["provider"] == "Kenney"]
    lines += ["", "Shipped sounds built (fully or partly) from Kenney packs: " + ", ".join(kenney_used) + ".", ""]
    (ROOT / "Art" / "Audio" / "PROVENANCE.md").write_text("\n".join(lines), "utf-8")


def main():
    force = "--force" in sys.argv
    if "--provenance" in sys.argv:
        write_provenance(json.loads(MANIFEST.read_text("utf-8"))["sources"])
        print("wrote Art/Audio/PROVENANCE.md")
        return
    records = []
    for stem, (title, use) in MUSIC_TRACKS.items():
        records.append(music(stem, title, use, force)); print("music", stem)
    for key, slug in KENNEY.items():
        records.append(kenney(key, slug, force)); print("kenney", slug)
    for key, (sid, user, use) in FREESOUND.items():
        records.append(freesound(key, sid, user, use, force)); print("freesound", key)
        time.sleep(.4)
    MANIFEST.write_text(json.dumps({"schemaVersion": 1, "sources": records}, indent=2, ensure_ascii=False) + "\n", "utf-8")
    write_provenance(records)
    print("wrote", MANIFEST, len(records), "sources")


if __name__ == "__main__":
    main()
