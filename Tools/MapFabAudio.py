"""Map the purchased Fab audio packs onto AudioCues.json cues (Docs/Audio.md "Fab audio packs").

The packs are licensed and never committed. This tool only writes object PATHS (no audio) to
Art/Audio/FabAudioMap.json; Tools/BuildAudioEvents.py copies them into each cue's "pack" list, and the game
plays them only when the pack is installed (CireAudio falls back to the shipped sounds otherwise).

Sources, per pack folder (first found wins):
  1. the installed pack: <main checkout>/Content/<PackFolder> (where "Add to Project" puts it);
  2. the Epic launcher vault manifest (C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/FabLibrary/<listing>),
     so the map can be prepared while a download is still in progress.
SoundWaves are used (not the packs' SoundCues) so our own random pick, pitch/volume jitter, voice limits,
bus and attenuation apply.

  F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe Tools/MapFabAudio.py [--installed-only] [--report]
"""
from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
import zlib
from collections import OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "Art" / "Audio" / "FabAudioMap.json"
VAULT = Path("C:/ProgramData/Epic/EpicGamesLauncher/VaultCache/FabLibrary")

PACKS = OrderedDict([
    ("ShieldBlocksDeflects", {"name": "Shield Blocks and Deflects Sound Effects", "vault": "Shield_Blocks_and_Deflects_Sound_Effects-e9f24538",
                              "use": "shield block (30% physical block on shield tanks), deflect, guard break"}),
    ("Professional_Gunshots", {"name": "Professional Gunshots | 105 AAA Handcrafted Weapon SFX", "vault": "Professional_Gunshots___105_AAA_Handcrafted_Weapon_SFX_for_Games-f0ab1244",
                               "use": "gun champions only: Gunblade pistol, Witch Slayer blunderbuss, Collect the Bounty"}),
    ("Magic_Spell_SFX_Pack_Vol1", {"name": "Magic Spell Sound Effects Pack Vol 1 (300 spells)", "vault": "Magic_Spell_Sound_Effects_Pack_Vol_1-8e59e01f",
                                   "use": "spells by element: cast / channel loop / projectile loop / impact / heal; buffs, CC"}),
    ("Combat_Sounds_-_Lite", {"name": "Combat Sounds Pack - Lite Edition (150 combat SFX)", "vault": "Combat_Sounds_Pack_-_Lite_Edition-8288f4c3",
                              "use": "weapon swings/impacts by type, bow/crossbow, flesh/armour layers, crits, body falls, explosions"}),
    ("Fantasy_UI_SFX_Pack", {"name": "Complete Fantasy UI Sound Effects Pack", "vault": "Fantasy_UI_Sound_Effects_Pack-f914d641",
                             "use": "every menu (Dark Fantasy set), shop, Skill Shop, loot, errors, ready, banner stings, polymorph pop (Cute set)"}),
])

SB = ("ShieldBlocksDeflects", r"wav/")
PG = ("Professional_Gunshots", r"Sound_Waves/[^/]+/Wav/")
MS = ("Magic_Spell_SFX_Pack_Vol1", r"[^/]+/wav/")
CS = ("Combat_Sounds_-_Lite", r"[^/]+/wav/")
UI = ("Fantasy_UI_SFX_Pack", r"WAVs/[^/]+/")


def spell(elem: str, part: str) -> str:
    """Magic pack spells are numbered N-V: variant 1 (or none) = the cast/charge, variants 2+ = the hit."""
    return rf"{elem}_Magic_Spell_\d+(-1)?_wav" if part == "cast" else rf"{elem}_Magic_Spell_\d+-[2-9]_wav"


def num(prefix: str, lo: int, hi: int) -> str:
    return prefix + r"_(" + "|".join(str(i) for i in range(lo, hi + 1)) + r")(-\d+)?_wav"


# cue -> [(pack, stem regex)]. Stems are full-matched against SoundWave names in the pack's wave folders.
RULES: "OrderedDict[str, list]" = OrderedDict([
    # ---- shield (every block is a distinct clang) ----
    ("combat.block", [(SB, r"SH_Block_Deflect_[A-H]0\d")]),
    ("combat.deflect", [(SB, r"SH_Block_Deflect_[I-Q]0\d")]),
    ("weapon.shield.impact", [(SB, r"SH_Guard_Break_\d+")]),
    ("weapon.shield.bash", [(CS, r"Shield_Metal_2_\d+")]),
    ("ability.guard", [(CS, r"Shield_Metal_1_\d+")]),
    # ---- guns (gun champions only) ----
    ("weapon.pistol.shot", [(PG, r"S_Pistol__\d+_"), (PG, r"S_Pistol_B__\d+_")]),
    ("weapon.pistol.heavy", [(PG, r"S_Heavy_Gun_Short__\d+_"), (PG, r"S_Sniper__\d+_")]),
    ("weapon.blunderbuss.shot", [(PG, r"S_Shotgun_A__\d+_"), (PG, r"S_Shotgun_D__\d+_")]),
    ("weapon.pistol.cock", [(CS, r"Weapon_Foley_1_\d+")]),
    ("weapon.bullet.impact", [(CS, r"Generic_Hit_[45]_\d+")]),
    # ---- melee weapons ----
    ("weapon.sword.swing", [(CS, r"Whoosh_Metal_[12]_\d+")]),
    ("weapon.sword.impact", [(CS, r"Stab_[12]_\d+")]),
    ("weapon.gunblade.swing", [(CS, r"Whoosh_Metal_[12]_\d+")]),
    ("weapon.gunblade.impact", [(CS, r"Stab_[12]_\d+"), (CS, r"Metal_Weapon_Clash_2_\d+")]),
    ("weapon.dagger.swing", [(CS, r"Whoosh_[12]_\d+")]),
    ("weapon.dagger.impact", [(CS, r"Stab_[34]_\d+")]),
    ("weapon.axe.swing", [(CS, r"Whoosh_[34]_\d+")]),
    ("weapon.axe.impact", [(CS, r"Bone_[12]_\d+"), (CS, r"Generic_Hit_1_\d+")]),
    ("weapon.axe.throw", [(CS, r"Whoosh_Metal_3_\d+")]),
    ("weapon.mace.swing", [(CS, r"Whoosh_[45]_\d+")]),
    ("weapon.mace.impact", [(CS, r"Punch_[45]_\d+"), (CS, r"Generic_Hit_3_\d+")]),
    ("weapon.spear.thrust", [(CS, r"Whoosh_Metal_3_\d+"), (CS, r"Whoosh_2_\d+")]),
    ("weapon.spear.impact", [(CS, r"Stab_[1-4]_\d+")]),
    ("weapon.spear.throw", [(CS, r"Arrow_Shot_2_\d+")]),
    ("weapon.claws.swipe", [(CS, r"Whoosh_[123]_\d+")]),
    ("weapon.claws.impact", [(CS, r"Blood___Gore_[12]_\d+")]),
    ("weapon.staff.swing", [(CS, r"Whoosh_5_\d+")]),
    ("weapon.staff.impact", [(CS, r"Wood_Weapon_Handle_1_\d+")]),
    ("weapon.glaive.swing", [(CS, r"Whoosh_Metal_[12]_\d+")]),
    ("weapon.glaive.throw", [(CS, r"Whoosh_Metal_[12]_\d+")]),
    ("weapon.glaive.impact", [(CS, r"Metal_Weapon_Clash_2_\d+")]),
    # ---- bow / crossbow: draw, release, impact ----
    ("weapon.bow.draw", [(CS, r"Bow_Draw_[12]_\d+")]),
    ("weapon.bow.release", [(CS, r"Arrow_Shot_1_\d+")]),
    ("weapon.bow.impact", [(CS, r"Arrow_Hit_1_\d+")]),
    ("weapon.crossbow.draw", [(CS, r"Arrow_Handle_1_\d+")]),
    ("weapon.crossbow.release", [(CS, r"Arrow_Shot_2_\d+")]),
    ("weapon.crossbow.impact", [(CS, r"Arrow_Hit_1_\d+")]),
    # ---- body layers, crits, reactions, deaths ----
    ("impact.armor", [(CS, r"Armor_Foley_1_\d+"), (CS, r"Metal_Weapon_Clash_1_\d+")]),
    ("impact.flesh", [(CS, r"Punch_[123]_\d+")]),
    ("impact.wood", [(CS, r"Shield_Wood_[12]_\d+")]),
    ("impact.stone", [(MS, r"Debris_\d+_wav")]),
    ("combat.crit", [(CS, r"Bone_[12]_\d+"), (CS, r"Blood___Gore_[12]_\d+")]),
    ("combat.dodge", [(CS, r"Whoosh_[1-5]_\d+")]),
    ("combat.miss", [(CS, r"Whoosh_[1-5]_\d+")]),
    ("hit.player", [(CS, r"Generic_Hit_[56]_\d+"), (CS, r"Punch_[123]_\d+")]),
    ("hit.player_heavy", [(CS, r"Bone_[12]_\d+")]),
    ("death.humanoid", [(CS, r"Body_Fall_[12]_\d+")]),
    ("death.hero", [(CS, r"Body_Fall_[12]_\d+")]),
    ("spell.explosion", [(CS, r"Large_Explosion_\d+-\d+"), (CS, r"Medium_Explosion_(9|10|Realistic_2)_\d+")]),
    ("spell.explosion_small", [(CS, r"Small_Explosion_(Realistic_)?\d+_\d+"), (CS, r"Medium_Explosion_[48]_\d+")]),
    ("aggro_taken", [(CS, r"Draw_Weapon_Metal_[12]_\d+")]),
    # ---- spells by element ----
    ("spell.physical.cast", [(MS, r"Magic_Cast_Whoosh_[1-3]-\d_wav")]),
    ("spell.physical.impact", [(MS, num("Special_Hit", 1, 3))]),
    ("spell.physical.heal", [(MS, num("Positive_Magic_Effect", 1, 3))]),
    ("spell.fire.cast", [(MS, spell("Fire", "cast"))]),
    ("spell.fire.impact", [(MS, spell("Fire", "hit")), (MS, r"Fire_Burn_1-\d_wav")]),
    ("spell.fire.channel", [(MS, r"Fire_Loop_1_wav")]),
    ("spell.fire.projectile", [(MS, r"Fire_Loop_1_wav")]),
    ("spell.fire.heal", [(MS, num("Positive_Magic_Effect", 4, 5))]),
    ("spell.frost.cast", [(MS, spell("Frost", "cast"))]),
    ("spell.frost.impact", [(MS, spell("Frost", "hit")), (MS, r"Freeze_1-\d_wav")]),
    ("spell.frost.channel", [(MS, r"Frost_Loop_1_wav")]),
    ("spell.frost.projectile", [(MS, r"Frost_Loop_1_wav")]),
    ("spell.frost.heal", [(MS, num("Positive_Magic_Effect", 6, 6))]),
    ("spell.nature.cast", [(MS, spell("Nature", "cast"))]),
    ("spell.nature.impact", [(MS, spell("Nature", "hit"))]),
    ("spell.nature.channel", [(MS, r"Nature_Loop_1_wav")]),
    ("spell.nature.projectile", [(MS, r"Nature_Loop_2-\d_wav")]),
    ("spell.nature.heal", [(MS, num("Positive_Magic_Effect", 8, 9))]),
    ("spell.shadow.cast", [(MS, spell("Dark", "cast"))]),
    ("spell.shadow.impact", [(MS, spell("Dark", "hit")), (MS, num("Negative_Magic_Effect", 9, 10))]),
    ("spell.shadow.channel", [(MS, r"Dark_Loop_1_wav")]),
    ("spell.shadow.projectile", [(MS, r"Dark_Loop_1_wav")]),
    ("spell.shadow.heal", [(MS, num("Positive_Magic_Effect", 10, 10))]),
    ("spell.arcane.cast", [(MS, spell("Arcane", "cast"))]),
    ("spell.arcane.impact", [(MS, spell("Arcane", "hit"))]),
    ("spell.arcane.channel", [(MS, r"Arcane_Loop_1_wav")]),
    ("spell.arcane.projectile", [(MS, r"General_Magic_Loop_3-\d_wav")]),
    ("spell.arcane.heal", [(MS, num("Positive_Magic_Effect", 2, 3))]),
    ("spell.holy.cast", [(MS, num("Positive_Magic_Effect", 1, 1)), (MS, num("Positive_Magic_Effect", 7, 7)), (MS, r"Magic_Cast_Whoosh_[45]-\d_wav")]),
    ("spell.holy.impact", [(MS, num("Special_Hit", 2, 2)), (MS, num("Positive_Magic_Effect", 5, 5))]),
    ("spell.holy.channel", [(MS, r"General_Magic_Loop_1_wav")]),
    ("spell.holy.projectile", [(MS, r"General_Magic_Loop_2_wav")]),
    ("spell.holy.heal", [(MS, num("Positive_Magic_Effect", 1, 3)), (MS, num("Positive_Magic_Effect", 8, 8))]),
    ("spell.earth.cast", [(MS, r"Magic_Cast_Whoosh_[67]-\d_wav")]),
    ("spell.earth.impact", [(MS, r"Debris_\d+_wav"), (MS, num("Special_Hit", 3, 3))]),
    ("spell.earth.heal", [(MS, num("Positive_Magic_Effect", 4, 4))]),
    ("spell.water.cast", [(MS, spell("Water", "cast"))]),
    ("spell.water.impact", [(MS, spell("Water", "hit"))]),
    ("spell.water.channel", [(MS, r"Water_Loop_1_wav")]),
    ("spell.water.projectile", [(MS, r"Water_Loop_1_wav")]),
    ("spell.water.heal", [(MS, num("Positive_Magic_Effect", 9, 10))]),
    ("spell.lightning.cast", [(MS, spell("Lightning", "cast"))]),
    ("spell.lightning.impact", [(MS, spell("Lightning", "hit"))]),
    ("spell.lightning.channel", [(MS, r"Lightning_Loop_1_wav")]),
    ("spell.lightning.projectile", [(MS, r"Lightning_Loop_1_wav")]),
    ("spell.lightning.heal", [(MS, num("Positive_Magic_Effect", 6, 7))]),
    ("weapon.arrow.flight", [(MS, r"Wind_Loop_1_wav")]),
    ("ability.summon", [(MS, r"General_Magic_Loop_4_wav"), (MS, num("Positive_Magic_Effect", 5, 5))]),
    ("ability.barrier", [(MS, num("Positive_Magic_Effect", 3, 3))]),
    ("combat.interrupt", [(MS, num("Negative_Magic_Effect", 4, 4))]),
    ("combat.resist", [(MS, num("Negative_Magic_Effect", 5, 5))]),
    # ---- buffs / auras / crowd control ----
    ("aura_apply", [(MS, num("Positive_Magic_Effect", 1, 3))]),
    ("aura_heal", [(MS, num("Positive_Magic_Effect", 8, 9))]),
    ("buff.expire", [(MS, num("Negative_Magic_Effect", 1, 1))]),
    ("debuff.expire", [(MS, num("Positive_Magic_Effect", 7, 7))]),
    ("debuff.stunned.start", [(MS, num("Negative_Magic_Effect", 2, 2)), (MS, num("Special_Hit", 1, 1))]),
    ("npc.silence.start", [(MS, num("Negative_Magic_Effect", 3, 3))]),
    ("npc.root.start", [(MS, num("Negative_Magic_Effect", 6, 6))]),
    ("debuff.slowed.start", [(MS, r"Freeze_1-\d_wav")]),
    ("debuff.poisoned.start", [(MS, num("Negative_Magic_Effect", 8, 8))]),
    ("cc.polymorph.start", [(UI, r"WAV_Fantasy_UI_Cute_Feedback_Bubble_0\d_mono"), (UI, r"WAV_Fantasy_UI_Cute_Star_Burst_0\d_stereo")]),
    ("cc.polymorph.sparkle", [(UI, r"WAV_Fantasy_UI_Cute_Star_0\d_mono")]),
    ("cc.polymorph.end", [(UI, r"WAV_Fantasy_UI_Cute_Star_Bounce_0\d_mono")]),
    ("cc.stun.end", [(MS, num("Positive_Magic_Effect", 7, 7))]),
    ("cc.silence.end", [(MS, num("Positive_Magic_Effect", 7, 7))]),
    # ---- UI (Dark Fantasy set, one kit across every menu) ----
    ("ui_click", [(UI, r"WAV_Fantasy_UI_Dark_Generic_Click_0\d_mono"), (UI, r"WAV_Fantasy_UI_Dark_Select_0\d_mono")]),
    ("ui_hover", [(UI, r"WAV_Fantasy_UI_Dark_Hover_0[124]_mono")]),
    ("ui_skill_hover", [(UI, r"WAV_Fantasy_UI_Dark_Hover_0[35]_(mono|stereo)")]),
    ("ui_open", [(UI, r"WAV_Fantasy_UI_Dark_Inventory_Window_Open_stereo")]),
    ("ui_close", [(UI, r"WAV_Fantasy_UI_Dark_Inventory_Window_Close_stereo")]),
    ("ui_confirm", [(UI, r"WAV_Fantasy_UI_Gen_Confirm_mono")]),
    ("ui_tab", [(UI, r"WAV_Fantasy_UI_Dark_Toggle_0\d[AB]_mono")]),
    ("ui_undo", [(UI, r"WAV_Fantasy_UI_Dark_Skill_Unequip_v\d_mono")]),
    ("ui_ready", [(UI, r"WAV_Fantasy_UI_Dark_Success_0\d_stereo")]),
    ("ui_ready_bell", [(UI, r"WAV_Fantasy_UI_Dark_Bell_0\d_mono")]),
    ("ui_unready", [(UI, r"WAV_Fantasy_UI_Dark_Exit_01_Subtle_mono")]),
    ("ui_error", [(UI, r"WAV_Fantasy_UI_Dark_Error_0[12]_mono")]),
    ("ui_error_mana", [(UI, r"WAV_Fantasy_UI_Dark_Skill_Tree_Error_mono")]),
    ("ui_error_gold", [(UI, r"WAV_Fantasy_UI_Dark_Item_Error_mono")]),
    ("ui_target", [(UI, r"WAV_Fantasy_UI_Dark_Ping_0\d_(mono|stereo)")]),
    ("ui_threat", [(UI, r"WAV_Fantasy_UI_Dark_Alert_0[234]_mono")]),
    ("ui_aggro_lost", [(UI, r"WAV_Fantasy_UI_Dark_Discard_Thud_Bass_0\d_mono")]),
    ("ui_skill_buy", [(UI, r"WAV_Fantasy_UI_Dark_Skill_Attribute_Upgrade_v\d_stereo")]),
    ("ui_skill_offer", [(UI, r"WAV_Fantasy_UI_Dark_Skill_Window_Open_stereo")]),
    ("ui_skill_learned", [(UI, r"WAV_Fantasy_UI_Dark_Skill_Equip_v\d_(mono|stereo)")]),
    ("ui_buy_confirm", [(UI, r"WAV_Fantasy_UI_Dark_Item_Upgrade_Success_v\d_stereo")]),
    ("ui_sell_confirm", [(UI, r"WAV_Fantasy_UI_Dark_Item_Discard_mono")]),
    ("loot_pickup", [(UI, r"WAV_Fantasy_UI_Gen_Reward_Obtained_Small_0\d_mono")]),
    ("loot_common", [(UI, r"WAV_Fantasy_UI_Gen_Reward_Obtained_Small_0\d_mono")]),
    ("loot_magic", [(UI, r"WAV_Fantasy_UI_Gen_Item_Obtained_stereo")]),
    ("loot_rare", [(UI, r"WAV_Fantasy_UI_Gen_Reward_Obtained_stereo")]),
    ("loot_epic", [(UI, r"WAV_Fantasy_UI_Dark_Notification_Epic_01_stereo")]),
    ("sting.levelup", [(UI, r"WAV_Fantasy_UI_Dark_Skill_Attribute_Add_mono")]),
    ("sting.prep", [(UI, r"WAV_Fantasy_UI_Dark_Stinger_Wobble_01_stereo")]),
    ("sting.wave", [(UI, r"WAV_Fantasy_UI_Dark_Stinger_Bram_Low_0\d_stereo")]),
    ("sting.arena", [(UI, r"WAV_Fantasy_UI_Dark_Stinger_Bram_0\d_stereo")]),
    ("sting.boss", [(UI, r"WAV_Fantasy_UI_Dark_Boom_Pad_Long_0\d_stereo")]),
    ("sting.challenge", [(UI, r"WAV_Fantasy_UI_Dark_Notification_Impact_0\d_mono")]),
    ("sting.cleared", [(UI, r"WAV_Fantasy_UI_Dark_Notification_Tonal_0\d_mono")]),
    ("sting.death", [(UI, r"WAV_Fantasy_UI_Dark_Stinger_Low_0\d_stereo")]),
])

# Loudness trims for pack members (linear, applied instead of nothing). Filled from the probe's levels / by ear.
DEFAULT_TUNING = OrderedDict([
    ("weapon.pistol.shot", {"packVolume": .8}), ("weapon.pistol.heavy", {"packVolume": .8}), ("weapon.blunderbuss.shot", {"packVolume": .75}),
    ("spell.explosion", {"packVolume": .8}),
    ("spell.fire.projectile", {"packVolume": .8, "packPitch": [1.15, 1.25]}), ("spell.frost.projectile", {"packVolume": .8, "packPitch": [1.15, 1.25]}),
    ("spell.shadow.projectile", {"packVolume": .8, "packPitch": [1.15, 1.25]}), ("spell.water.projectile", {"packVolume": .8, "packPitch": [1.15, 1.25]}),
    ("spell.lightning.projectile", {"packVolume": .8, "packPitch": [1.15, 1.25]}), ("weapon.arrow.flight", {"packVolume": .6, "packPitch": [1.6, 1.8]}),
    ("combat.block", {"packVolume": 1.3, "packPitch": [.96, 1.04]}), ("combat.deflect", {"packVolume": 1.2, "packPitch": [.96, 1.06]}),
    ("ui_hover", {"packVolume": .7}), ("cc.polymorph.start", {"packVolume": 1.4}),
])


def main_checkout() -> Path:
    out = subprocess.run(["git", "worktree", "list", "--porcelain"], cwd=ROOT, capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("worktree "):
            return Path(line[9:].strip())
    return ROOT


def manifest_files(vault_dir: str) -> list[str]:
    path = VAULT / vault_dir / "unreal-engine" / "manifest"
    if not path.exists():
        return []
    b = path.read_bytes()
    header = struct.unpack_from("<I", b, 4)[0]
    flags = b[4 + 12 + 20]
    data = b[header:]
    if flags & 1:
        data = zlib.decompress(data)
    return sorted({m.decode("latin1")[len("Content/"):] for m in re.findall(rb"Content/[\x20-\x7e]+?\.uasset", data)})


def pack_files(main: Path, folder: str, vault_dir: str, installed_only: bool) -> tuple[list[str], str]:
    local = main / "Content" / folder
    if local.is_dir():
        return sorted(str(p.relative_to(main / "Content")).replace("\\", "/") for p in local.rglob("*.uasset")), "installed"
    if installed_only:
        return [], "missing"
    files = manifest_files(vault_dir)
    return (files, "vault manifest") if files else ([], "missing")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--installed-only", action="store_true", help="map only packs present in the main checkout")
    ap.add_argument("--report", action="store_true", help="print every cue's members")
    args = ap.parse_args()
    main_tree = main_checkout()
    previous = json.loads(OUT.read_text("utf-8"), object_pairs_hook=OrderedDict) if OUT.exists() else OrderedDict()
    listing, packs = {}, OrderedDict()
    for folder, meta in PACKS.items():
        files, source = pack_files(main_tree, folder, meta["vault"], args.installed_only)
        listing[folder] = files
        packs[folder] = OrderedDict([("name", meta["name"]), ("use", meta["use"]), ("source", source), ("assets", len(files))])
    cues = OrderedDict()
    unmatched = []
    for cue, rules in RULES.items():
        members = []
        for (folder, subdir), stem in rules:
            rx = re.compile(re.escape(folder) + "/" + subdir + "(" + stem + r")\.uasset$")
            for f in listing.get(folder, []):
                m = rx.fullmatch(f)
                if m:
                    obj = "/Game/" + f[:-len(".uasset")]
                    if obj not in members:
                        members.append(obj)
        if members:
            cues[cue] = members
        elif any(listing.get(r[0][0]) for r in rules):
            unmatched.append(cue)
    tuning = previous.get("tuning") or DEFAULT_TUNING
    out = OrderedDict([
        ("_comment", "Fab audio pack -> cue map written by Tools/MapFabAudio.py. Object paths only: the packs are licensed and stay local "
                     "(gitignored, junctioned into worktrees by Tools/LinkFabContent.py). Tools/BuildAudioEvents.py copies these into "
                     "AudioCues.json 'pack' lists; missing packs fall back to the shipped sounds. 'tuning' trims pack loudness per cue."),
        ("packs", packs), ("tuning", tuning), ("cues", cues),
    ])
    OUT.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(out, indent=2, ensure_ascii=False) + "\n"
    OUT.write_text(text, "utf-8", newline="\n")
    for folder, p in packs.items():
        print(f"{folder:28s} {p['source']:15s} {p['assets']:5d} assets")
    print(f"mapped {len(cues)} cues, {sum(len(v) for v in cues.values())} members -> {OUT.relative_to(ROOT)}")
    if unmatched:
        print("NO MATCH (check the rule): " + ", ".join(unmatched))
    if args.report:
        for c, m in cues.items():
            print(f"{c}: {len(m)}  e.g. {m[0].rsplit('/', 1)[-1]}")
    return 1 if unmatched else 0


if __name__ == "__main__":
    sys.exit(main())
