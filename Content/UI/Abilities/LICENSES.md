# Ability icon licenses

## Painted icons

Ability (all 39 pool skills and all 63 champion-kit skills), passive, ultimate, trait, status-effect (`T_status_<name>`) and role (`T_role_<name>`)
icons listed in `Art/Icons/ChatGPT/Abilities/` are painted icons **generated for Eric via ChatGPT
(OpenAI), 2026-09-25**. They belong to Eric (the project owner) under OpenAI's terms for generated
output; no third-party icon pack, traced game art or licensed asset is used. The full source sheets
are in `Art/Icons/ChatGPT/abilities_*.png` and `ui_*.png`, sliced to 256x256 by
`Tools/SliceIconSheet.py`.

## New-champion kits (art-2d)

The 40 kit icons of Gunblade, Witch Slayer, Huntress, Aetheri Artificer and Aetheri Warden (all actives,
passives, ultimates and Construct skills) and the redrawn `sanctuary` (healing spring), `protection_dome`
(Aegis Dome: crystal hex barrier) and `mass_aegis` (winged shield ring) icons are painted icons
**generated for Eric via ChatGPT (OpenAI), 2026-09-25**, from the sheets
`Art/Icons/ChatGPT/abilities_{gunblade,witch_slayer,huntress,aetheri_artificer,aetheri_warden}.png`
(the Aetheri sheets' 9th tile is an unused emblem).

## Dodge-roll skills and Ashfang companion (art-2d)

The 20 dodge-roll skill icons (`riposte_roll` ... `evasive_stance`; one family: a hooded figure mid-roll inside a
school-coloured tumbling arc) and the Huntress companion icons `sabercat_maul` (Ashfang: Maul) and `sabercat_roar`
(Ashfang: Dread Roar) are painted icons **generated for Eric via ChatGPT (OpenAI), 2026-09-25**, from
`Art/Icons/ChatGPT/abilities_roll1.png`, `abilities_roll2.png` and `abilities_roll3_pet.png` (2x2).

## Procedural icons

Any future `T_<id>` without a painted PNG falls back to original procedural art from `Tools/BuildAbilityIcons.py`
(no licensed sources).

Rebuild and import both: `python Tools/RunAbilityIcons.py --pylib <dir with Pillow>`
(`--procedural` ignores the painted overrides).
