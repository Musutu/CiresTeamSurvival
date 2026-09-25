# Content/UI/Draft licences and provenance

## Backgrounds/T_DraftBg_*.uasset (champion-select scenes)

Generated for Eric via ChatGPT (OpenAI), 2026-09-25, using Eric's own champion-select
target image as the style reference. One scene per champion family; variants of one body
share a scene (ether_golem, paladin, troll_berserker). Source PNGs (1672x941):
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

## M_DraftCutout.uasset

Authored in this project by `Tools/BuildDraftSelectContent.py` (UI material that
composites the live 3D champion over the scene).

## Portraits/, Sounds/

See the tools that generate them (`Tools/RunDraftPortraits.py`, `Tools/BuildDraftSounds.py`).
