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
    # The Blightwood
    "vinelasher": (185, "slash", "thorn-vine whip arms", ["hand_r"], {}),
    "sapling_brute": (200, "slash", "splintered stump club (right arm)", ["hand_r"], {}),
    "barkhide_warden": (190, "slash", None, [], {}),
    "rotbloom_shaman": (185, "cast_a_spell", "fungus branch staff (right hand)", ["hand_r"], {}),
    "thornspitter": (180, "cast_a_spell", "thorn quivers on the back; spits its thorns", ["hand_l"], {}),
    "sporeling": (95, "slash", None, ["hand_r"], {}),
    "withered_matron": (210, "cast_a_spell", "clawed branch fingers", ["hand_r"], {"blight_withering_wrath": "war_cry", "blight_matron_call": "war_cry"}),
    "elder_oakheart": (240, "slash", "root-fist arms", ["hand_r"],
                       {"blight_root_eruption": "ground_slam", "blight_oakheart_stomp": "ground_slam", "blight_crushing_bough": "slash",
                        "blight_heartwood_fury": "war_cry"}),
    # The Ironhide Warband
    "ironhide_grunt": (190, "slash", None, [], {}),
    "redmoon_ravager": (205, "slash", None, [], {}),
    "ironhide_bulwark": (200, "slash", None, [], {}),
    "blood_hexer": (185, "cast_a_spell", "shrunken-head totem staff (right hand)", ["hand_r"], {}),
    "redmoon_axethrower": (195, "axe_throw", "throwing axes on a bandolier (thrown axes are projectiles)", ["hand_l", "hand_r"],
                           {"ironhide_axe_barrage": "axe_throw", "ironhide_hamstring_axe": "axe_throw", "ironhide_spinning_axe": "axe_throw"}),
    "ironhide_drummer": (180, "cast_a_spell", "war drum on the back and two bone mallets", ["hand_r"],
                         {"ironhide_war_drums": "war_cry", "ironhide_battle_hymn": "war_cry", "ironhide_deafening_boom": "war_cry"}),
    "ironhide_warchief": (200, "slash", "colossal cleaver (right hand) and skull banner on the back", ["hand_r", "spine_03"],
                          {"ironhide_warchief_roar": "war_cry", "ironhide_call_the_clans": "war_cry", "ironhide_blood_fury": "war_cry",
                           "ironhide_skull_cleave": "slash"}),
    "ironhide_juggernaut": (240, "slash", "iron-bound tree-trunk club (right hand)", ["hand_r"],
                            {"ironhide_earthsplitter": "ground_slam", "ironhide_trunk_sweep": "slash", "ironhide_juggernaut_rage": "war_cry"}),
    # The Drakkari Brood
    "drakkari_whelpguard": (185, "slash", None, [], {}),
    "drakkari_scalebreaker": (205, "slash", None, [], {}),
    "drakkari_scaleguard": (195, "slash", None, [], {}),
    "drakkari_flamecaller": (190, "cast_a_spell", "caged-coal iron staff (right hand)", ["hand_r"], {}),
    "drakkari_wingshot": (185, "attack_bow", None, [], {}),
    "ember_whelp": (90, "slash", "claws", ["hand_r"], {}),
    "drakkari_broodmother": (205, "cast_a_spell", "dragon-egg staff (right hand)", ["hand_r"],
                             {"drakkari_broodmother_wrath": "war_cry", "drakkari_hatch_the_brood": "war_cry"}),
    "drakkari_ashwing": (240, "slash", "claws", ["hand_r"],
                         {"drakkari_inferno_breath": "fire_breath", "drakkari_ash_fall": "fire_breath", "drakkari_ashwing_stomp": "ground_slam",
                          "drakkari_ashwing_fury": "war_cry", "drakkari_ashwing_tail": "slash"}),
    # The Stoneborn
    "rune_sentinel": (190, "slash", "stone blade forearm (right arm)", ["hand_r"], {}),
    "granite_crusher": (205, "slash", "granite fists", ["hand_r"], {}),
    "bastion_golem": (200, "slash", "stone slab shield fused to the left forearm", ["hand_l", "hand_r"], {}),
    "deepforge_runesmith": (170, "cast_a_spell", "forge-hammer staff (right hand)", ["hand_r"], {}),
    "stoneborn_forgelord": (200, "slash", "glowing forge hammer (right hand)", ["hand_r", "spine_03"],
                            {"stoneborn_forge_fury": "war_cry", "stoneborn_forge_sentinels": "war_cry", "stoneborn_slag_eruption": "ground_slam",
                             "stoneborn_anvil_cleave": "slash", "stoneborn_runic_shockwave": "ground_slam"}),
    "stoneborn_colossus": (240, "slash", "boulder fists", ["hand_r"],
                           {"stoneborn_quake": "ground_slam", "stoneborn_crush": "ground_slam", "stoneborn_colossus_rage": "war_cry",
                            "stoneborn_boulder_toss": "slash"}),
    # The Feral Kin (dire_wolf and bristleback keep the CC0 quadrupeds in RaceMeshes.free.json)
    "werebear_mauler": (220, "slash", "claws", ["hand_r"], {"feral_rage": "war_cry"}),
    "wild_outrider": (195, "attack_bow", None, [], {}),
    "feral_ursoth": (210, "slash", "claws; antler mantle and tree on the back", ["hand_r", "spine_03"],
                     {"feral_elder_roar": "war_cry", "feral_call_of_the_wild": "war_cry", "feral_elder_fury": "war_cry",
                      "feral_rending_maul": "slash", "feral_ursoth_charge": "ground_slam"}),
    "feral_mammoth": (240, "slash", "stone-capped tusks", ["hand_r"],
                      {"feral_mammoth_stomp": "ground_slam", "feral_earthquake": "ground_slam", "feral_mammoth_trample": "ground_slam",
                       "feral_tusk_sweep": "slash", "feral_mammoth_rage": "war_cry"}),
    "tusked_behemoth": (205, "slash", None, [], {}),
    "feral_shaman": (185, "cast_a_spell", "bone-and-antler staff (right hand)", ["hand_r"], {}),
    # The Fallen Order
    "fallen_squire": (180, "slash", None, [], {}),
    "dread_knight": (200, "slash", None, [], {}),
    "oathbreaker_templar": (200, "slash", None, [], {}),
    "blighted_chaplain": (180, "cast_a_spell", "brass censer (right hand)", ["hand_r"], {}),
    "fallen_inquisitor_crossbow": (185, "attack_crossbow", "repeating crossbow (right hand)", ["hand_l", "hand_r"], {}),
    "flagellant": (170, "slash", "barbed flails (both hands)", ["hand_r"], {}),
    "fallen_high_inquisitor": (205, "cast_a_spell", "sun-relic staff (right hand)", ["hand_r"],
                               {"fallen_inquisitor_zeal": "war_cry", "fallen_summon_flagellants": "war_cry"}),
    "fallen_crusader": (240, "slash", "greatsword (right hand)", ["hand_r"],
                        {"fallen_judgment_slam": "ground_slam", "fallen_crusader_cleave": "slash", "fallen_crusader_wrath": "war_cry",
                         "fallen_crusade": "war_cry"}),
    # The Voidborn
    "rift_stalker": (190, "slash", "scythe finger-blades", ["hand_r"], {}),
    "void_ravager": (210, "slash", "mandible claws", ["hand_r"], {}),
    "null_warden": (200, "slash", None, [], {}),
    "rift_weaver": (195, "cast_a_spell", None, ["hand_r"], {}),
    "rift_gazer": (175, "cast_a_spell", None, ["hand_l"], {}),
    "voidling": (90, "slash", "claws", ["hand_r"], {}),
    "voidborn_herald": (205, "cast_a_spell", "rift-shard staff (right hand)", ["hand_r"],
                        {"void_herald_ascension": "war_cry", "void_open_the_rift": "war_cry"}),
    "voidborn_devourer": (245, "slash", "claws", ["hand_r"],
                          {"void_titan_slam": "ground_slam", "void_singularity": "ground_slam", "void_devourer_hunger": "war_cry",
                           "void_breath": "fire_breath", "void_devour": "slash"}),
    # The Aetheri Remnant (art-collector-3d, Art/TripoArt3D.json)
    "aetheri_phaseblade": (185, "slash", "energy blades on both wrist gauntlets", ["hand_r"], {}),
    "aetheri_warframe": (225, "slash", "oversized gauntlet fists", ["hand_r"], {}),
    "aetheri_bulwark": (210, "slash", "hard-light tower shield (left arm)", ["hand_l"], {}),
    "aetheri_engineer": (185, "cast_a_spell", None, ["hand_r"], {}),
    "aetheri_lancer": (190, "cast_a_spell", "photon lance-rifle (right hand)", ["hand_r"], {}),
    "skitter_drone": (110, "slash", "claw arms", ["hand_r"], {}),
    "aetheri_hierarch": (235, "cast_a_spell", "warp staff (right hand)", ["hand_r"], {}),
    "aetheri_colossus": (260, "slash", "clawed war-engine hands", ["hand_r"], {}),
}
EXTRA_WINDOWS = {"tentacle_sweep": {"start": 0.3, "contact": 1.7, "end": 3.4, "recoverRate": 1.4},
                 "axe_throw": {"start": 0.2, "contact": 1.2, "end": 2.6, "recoverRate": 1.4},
                 "fire_breath": {"start": 0.3, "contact": 1.5, "end": 3.5, "recoverRate": 1.3}}


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
    if OUT.exists():  # keep bodies integrated by earlier batches (their Saved report may live in another worktree)
        out["archetypes"].update(json.loads(OUT.read_text(encoding="utf-8")).get("archetypes", {}))
    art_text = ART.read_text(encoding="utf-8")
    art = json.loads(art_text)
    for unit, (height, attack, baked, drops, ability_clips) in SPEC.items():
        r = report.get(unit)
        if not r or not r.get("skeletal"):
            continue
        # The Bridge mangles some library clip names (e.g. "Huge_w_rlord..."); committed (read-only) clips keep the
        # raw name, so alias them to their role here.
        clips = {("war_cry" if k.startswith("Huge_") and "rlord" in k else k): v for k, v in r["animations"].items()}
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
