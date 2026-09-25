"""Write Content/Data/RaceMeshes.tripo.json (race-unit Tripo bodies) from Saved/TripoRacesIntegration.json.

Usage: python Tools/WriteRaceMeshes.py
SPEC below is the authored part per unit: target height (cm, before the archetype's own scale from Races.json),
basic-attack clip, baked weapon and the prop bones to drop because the mesh already carries that weapon/claw.
meshScale = heightCm / rawHeightCm (raw Tripo bodies are ~98 cm tall; scale is never baked, see Docs/MonsterArt.md).
Every Tripo body faces +Y, so yaw -90 turns it to actor +X. Units not integrated yet are left out (they keep their
fallback body). Also merges each unit's abilityClips into Content/Data/MonsterArt.json.
"""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent.parent
REPORT = ROOT / "Saved" / "TripoRacesIntegration.json"
OUT = ROOT / "Content" / "Data" / "RaceMeshes.tripo.json"
ART = ROOT / "Content" / "Data" / "MonsterArt.json"

# unit: (heightCm, attack clip, bakedWeapon or None, dropPropBones, abilityClips)
SPEC = {
    # The Drowned Deep
    "abyssal_stalker": (185, "slash", "hooked bone claws (both hands)", ["hand_r"], {}),
    "deepspawn_thrall": (200, "slash", "coral club (right arm) and crab claw (left arm)", ["hand_r"], {}),
    "coralshell_guardian": (185, "slash", None, [], {}),
    "tidecaller": (190, "cast_a_spell", "conch-and-driftwood staff (right hand)", ["hand_r"], {}),
    "barbspitter": (178, "cast_a_spell", "whalebone harpoon slung on the back; spits its spines", ["hand_l"], {}),
    "mind_leech": (165, "cast_a_spell", None, ["hand_r"], {}),
    "drowned_prophet": (200, "cast_a_spell", "eldritch-eye staff (right hand)", ["hand_r"], {}),
    "maw_of_the_deep": (230, "slash", "crab claws (both arms)", ["hand_r"],
                        {"drowned_tentacle_sweep": "tentacle_sweep", "drowned_tsunami_slam": "tentacle_sweep",
                         "drowned_leviathan_rage": "war_cry", "drowned_devour": "slash"}),
}
EXTRA_WINDOWS = {"tentacle_sweep": {"start": 0.3, "contact": 1.7, "end": 3.4, "recoverRate": 1.4}}


def main():
    report = json.loads(REPORT.read_text(encoding="utf-8"))["units"]
    out = {"schemaVersion": 1, "source": "tripo",
           "description": ("Tripo race-unit bodies (tripo-races agent) overlaying Content/Data/Races.json units by priority "
                           "(RaceMeshes.tripo.json > NPCMeshes.tripo.json > RaceMeshes.free.json > fallback archetype). "
                           "meshScale brings the raw ~98 cm Tripo body to heightCm BEFORE the archetype's scale; yaw -90; "
                           "Tripo 'UE5 Mannequin' 61-bone rig; clips are the Tripo preset library (+ text-to-motion where "
                           "named). Written by Tools/WriteRaceMeshes.py from Saved/TripoRacesIntegration.json; prompts, "
                           "task ids and credits in Art/TripoRaces.json."),
           "archetypes": {}}
    art_text = ART.read_text(encoding="utf-8")
    art = json.loads(art_text)
    for unit, (height, attack, baked, drops, ability_clips) in SPEC.items():
        r = report.get(unit)
        if not r or not r.get("skeletal"):
            continue
        clips = r["animations"]
        if attack not in clips:
            attack = "slash" if "slash" in clips else next(iter(clips))
        variant = r["mesh"].rsplit("/", 1)[1].split(".")[0].replace("CTS_Race_", "")
        entry = {
            "variant": variant, "mesh": r["mesh"], "skeleton": r["skeleton"], "material": r["material"],
            "race": r["race"], "rawHeightCm": r["rawHeightCm"], "heightCm": height,
            "meshScale": round(height / r["rawHeightCm"], 4), "yaw": -90, "bakedWeapon": baked,
            "dropPropBones": drops,
            "animations": {
                "idle": clips["idle"], "walk": clips.get("walk"), "run": clips.get("run"), "attack": clips[attack],
                "attackAlt": clips.get("chop"), "hit": clips.get("hit_to_body_01"), "death": clips.get("fall"),
                "all": dict(sorted(clips.items())),
            },
        }
        entry["animations"] = {k: v for k, v in entry["animations"].items() if v is not None}
        out["archetypes"][unit] = entry
        if ability_clips and unit not in art.get("archetypes", {}):
            # Text insertion keeps MonsterArt.json's hand-aligned layout (a json.dump would reformat the whole file).
            marker = "\n  },\n  \"bodies\""
            line = ",\n    %s: { \"abilityClips\": %s }" % (json.dumps(unit), json.dumps(ability_clips))
            art_text = art_text.replace(marker, line + marker, 1)
    for name, window in EXTRA_WINDOWS.items():
        if name not in art.get("clips", {}):
            anchor = art_text.index("\n", art_text.index('"ground_slam":'))
            art_text = art_text[:anchor] + "\n    %s: %s," % (json.dumps(name), json.dumps(window)) + art_text[anchor:]
    json.loads(art_text)  # still valid JSON
    OUT.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")
    ART.write_text(art_text, encoding="utf-8")
    print("RaceMeshes units:", len(out["archetypes"]))


if __name__ == "__main__":
    main()
