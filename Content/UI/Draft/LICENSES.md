# Content/UI/Draft licences and provenance

## Backgrounds/T_DraftBg_*.uasset (champion-select scenes)

Generated for Eric via ChatGPT (OpenAI), 2026-09-25, using Eric's own champion-select
target image as the style reference. One scene per champion family; since champ-select-hq every
variant has its own scene too (ether_golem is the Granite golem's, paladin the Righteous paladin's,
troll_berserker the melee berserker's). Source PNGs (1672x941):
`Art/DraftBackgrounds/<id>.png`, imported by `Tools/BuildDraftSelectContent.py`.

| id | scene |
|---|---|
| knight | ruined gothic fortress courtyard of a fallen holy order |
| ranger | ash-grey frozen forest at dusk |
| scholar | candlelit cloister archive |
| lancer | windswept dusk steppe with a ruined watchtower |
| summoner | shattered arcane rift sanctum |
| bear | primeval gravewood forest den |
| paladin | saint's reliquary chapel |
| dwarf_miner | deep dwarven mine forge |
| ether_golem | overgrown golem ruin with ether crystals |
| orc_chieftain | orc war camp at night |
| totemic_behemoth | savanna ancestral totem ground |
| drakish_footman | dragon-kin volcanic ridge |
| wizard | burned library under a storm |
| troll_berserker | jungle ziggurat under a blood-red moon |
| dryad | moonlit forest glade |
| whisp | lantern-lit twilight woodland path |
| evergrove_centaur | spring meadow grove with a stone circle |
| keeper_of_light | drowned city with a great lighthouse lantern |
| gunblade | gallows crossroads outside a burned village at dusk (new champions, art-2d) |
| witch_slayer | witch's clearing with a broken blue-fire ritual circle (new champions, art-2d) |
| huntress | moonlit forest ridge with sabercat tracks in the frost (new champions, art-2d) |
| aetheri | Aetheri workshop plaza of crystal spires and warp rings (Aetheri Artificer) |
| aetheri_warden | Aetheri sanctuary terrace with warding pylons and aurora |
| ether_golem_support | sunken moss-grown golem ruin with glowing oath-runes, waterfall and healing spring (Verdant) |
| ether_golem_bruiser | breached fortress wall burning with green fel-fire, broken portcullis and siege chains (Felfire) |
| paladin_holy | dawn-lit healers' sanctuary in a white-stone abbey with censers and a glowing relic (Holy) |
| troll_berserker_ranged | troll hunting gorge under a blood-red moon: rope bridge, axe-studded totems, throwing targets (Thrown) |

The four variant scenes above were **generated for Eric via ChatGPT (OpenAI), 2026-09-25** (champ-select-hq),
1672x941, prompted in the same painterly dark-fantasy splash style with an open centre foreground for the champion.

The five new-champion scenes (gunblade, witch_slayer, huntress, aetheri, aetheri_warden) were
**generated for Eric via ChatGPT (OpenAI), 2026-09-25**, using the ranger scene above as the style
reference, 1672x941 like the rest (aetheri_warden was darkened with a gamma curve and vignette to sit in the same value range). Which profile shows which scene is in `Content/Data/DraftBackgrounds.json`.

## M_DraftCutout.uasset

Authored in this project by `Tools/BuildDraftSelectContent.py` (UI material that
composites the live 3D champion over the scene).

## Portraits/, Sounds/

See the tools that generate them (`Tools/RunDraftPortraits.py`, `Tools/BuildDraftSounds.py`).

All 27 roster portraits (`T_Portrait_<profile>`) are stylised paintings of the champions' mesh renders (same
design, armour and colours; head-and-shoulders bust, warm key / cool rim light, dark vignette; variants told
apart by backlight colour or props): **generated for Eric via ChatGPT (OpenAI), 2026-09-25**. Sheets:
`Art/DraftPortraits/portraits_pale_sheet.png` (the six first pale ones), `portraits_sheet_a.png`, `portraits_sheet_b.png`
and `portraits_sheet_c.png` (its 4th tile is an unused alternative Huntress); 512 px tiles:
`Art/DraftPortraits/Painted/<profile>.png`, which `Tools/RunDraftPortraits.py` applies over fresh renders
(`--rendered` keeps the raw renders). Import only: `python Tools/RunDraftPortraits.py --import-dir Art/DraftPortraits/Painted`.
