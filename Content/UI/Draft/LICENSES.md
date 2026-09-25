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
| gunblade | gallows crossroads outside a burned village at dusk (new champions, art-2d) |
| witch_slayer | witch's clearing with a broken blue-fire ritual circle (new champions, art-2d) |
| huntress | moonlit forest ridge with sabercat tracks in the frost (new champions, art-2d) |
| aetheri | Aetheri workshop plaza of crystal spires and warp rings (Aetheri Artificer) |
| aetheri_warden | Aetheri sanctuary terrace with warding pylons and aurora |

The five new-champion scenes (gunblade, witch_slayer, huntress, aetheri, aetheri_warden) were
**generated for Eric via ChatGPT (OpenAI), 2026-09-25**, using the ranger scene above as the style
reference, 1672x941 like the rest. Which profile shows which scene is in `Content/Data/DraftBackgrounds.json`.

## M_DraftCutout.uasset

Authored in this project by `Tools/BuildDraftSelectContent.py` (UI material that
composites the live 3D champion over the scene).

## Portraits/, Sounds/

See the tools that generate them (`Tools/RunDraftPortraits.py`, `Tools/BuildDraftSounds.py`).
