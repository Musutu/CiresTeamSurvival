# Cire's Team Survival

**A playable UE 5.8.3 development prototype of the requested 5v5 survival / portal-arena game.**

This development prototype includes Tripo character art, an expanded town-defense battlefield, configurable combat effects, and playable survival/arena systems. Architecture, animation, effects and balance still need production polish. It is not a finished AA game or verified 1:1 Enfo recreation. The original `CireTeamSurvival` project in Documents and synced `sources/` files were not modified.

## Play

1. Double-click `PlayTripoPreview.cmd` for the imported human champions, prototype equipment and attack animation. This uses the installed Unreal Engine at `F:/UE_5.8` and the compiled editor module. `Play.cmd` retains the mannequin comparison view.
2. Choose a champion from the four-page roster with the mouse or keys 1–6; arrows/PageUp/PageDown change pages. **Iron Warden**, **Ash Ranger**, **Veil Scholar**, **Lancer**, and **Rift Summoner** have complete implemented thematic test kits. Additional profiles have their authored stats, roles and basic attacks but some bespoke skills remain planned. Normal matches start without learned skills.
3. Your team has four bots; the opposing team has five. Push toward the breach, defend against waves, and choose whether to fight challenge packs on the flanks.

You can also open `CiresTeamSurvival.uproject` and press Play. The native world builder creates the battlefield when play starts, so the editor map itself is intentionally sparse. A solo match waits for your champion choice before its timer starts.

| Control | Action |
|---|---|
| W / A / S / D | Move relative to camera |
| Hold right mouse + move mouse | Rotate camera |
| Tab | Cycle nearby enemies |
| F | Cycle nearby allies |
| Space | Toggle basic attacks on your target |
| 1–6 | Use acquired skills; 1–4 choose an open augment |
| Q | Use the learned ultimate |
| B | Town shop |
| R | Return to town during the 60-second intermission |
| H | Show/hide controls |
| Enter / Esc | Open/send chat / cancel chat |
| Tab while chatting | Switch party / everyone chat |
| F9 | Options: controls, interface, video, audio, developer tools |
| F10 | Edit/lock the HUD layout |
| Alt+F4 | Exit |

Basic attacks and targeted skills require range and line of sight. Champions start with their basic attack. A completed build has **six regular abilities, one passive, and one ultimate**, in addition to that basic attack. Every three levels offers four choices. Offers contain one or two passives until one is selected, then none; if only the passive slot remains, all four choices are passives. The complete draft policy passed the native rules suite.

Ranger fires homing arrows, Lancer throws homing lances, Warden uses a sword and shield, and Scholar fires arcane bolts. Basic attacks wind up before release and ranged damage resolves on arrival. Miss and dodge are separate rolls: 5% miss followed by 5% dodge, with ranged miss increased to 40% when the target's feet are more than 10 cm higher. Avoided attacks display MISS/DODGE above the victim and in personal combat text. Five new Astra-authored abilities provide circle, cone, line, square and custom ground effects, including poison that stops on exit. See `Docs/CombatAbilities.md` for controls, authoring and remaining presentation work.

Every cycle contains **3 cleared PvE waves**, **60 seconds of prep**, an arena lasting at most **90 seconds**, and **15 seconds of recovery** back at town. The three-wave cycle and 15-second recovery are confirmed settings. PvE has no five-minute countdown: the next wave waits for both teams to clear or leak all wave enemies, with a short breather between waves. Optional challenge packs never block cycle completion. Arena timeout compares surviving players, then remaining health fractions; equal results draw. Victory grants capped team power/loot benefits. Teams start with **100 defense lives**. A normal unit reaching town costs **1 life**, and a boss costs **10**. Zero lives means the team is overrun. For initial playtesting, the final wave of each cycle includes a boss. The last surviving base wins. The result screen has a Play Again button.

## Interface

The interface follows the supplied WoW / League hybrid mockup: player and clickable party frames, focus target, a tactical minimap, square ability buttons, party/all chat, damage and healing meters, and configurable scrolling combat text. Press **F10** to move/resize panels and lock the layout; **F9** changes display settings. Preferences save locally in `Saved/Config/CireUI.ini`. The combat log is optional and separate from chat. Meters use effective server-applied damage/healing, excluding overkill, overheal, and passive regeneration.

**F9 → Interface → Tooltips / status → Tooltip size (%)** adjusts tooltip text and frame from 60–140% in 5% steps, with a compact 80% default. The setting saves automatically and applies to cursor, fixed and radial placement. Descriptions wrap, height fits the content, and tooltips stay within the viewport. Fixed placement retains its saved position and base width; height is content-driven.

Damage and healing display **both above the affected target and in personal scrolling combat text**. Incoming and outgoing text have separate lanes with ability names; healing is green, outgoing damage gold, and incoming damage red. F9 includes independent toggles and font sizes for the two displays. F10 moves the scrolling-text area. See `Docs/CombatFeedback.md` for behavior and delivery details.

During PvE, prep, and recovery, opposing heroes and creatures are hidden and excluded from normal replication. Their minimap half stays concealed. The server only permits opposing-team damage during arena PvP. The minimap follows the expanded winding roads. Waypoints and Armored Escort scheduling are editable in `Content/Data/BattlefieldRoutes.json`; see `Docs/BattlefieldRoutes.md`.

The ultimate has a dedicated Q slot. Standalone developer tools under **F9 → Developer** include live tuning, AI-vs-AI or player-controlled balance scenarios, and local replay recording/playback. The Effects page's **Load champion test kit** replaces your match's learned skills with the selected core champion's full kit after overrides are enabled and applied. This is a deliberate testing action, not normal progression.

## Included systems

- Replicated heroes, monsters, team state, countdown, health, resources, progression, and skill offers; combat/purchases/rewards resolve on the server.
- Seventeen regular active skill prototypes, four passive prototypes, and four ultimate prototypes, including healing, guarding, taunting monsters/bots, slowing, ranged attacks, cleaves and five authored ground areas.
- STR +10 HP and +0.1 armor / spell ward (plus a flat base of 15 x starting STR, so level-1 HP is unchanged), INT +30 MP, AGI +1% attack speed per point; each level gives +2 primary and +1 to each other stat. Primary stat adds one basic-attack damage per point; the prototype has 12 weapon damage.
- Mana, regenerating energy, and pure cooldown reduction capped at 60%. Cooldown reduction does not increase damage.
- Three challenge tiers per battlefield, group-clear rewards, rare relic rolls and Greater Stat Tomes; town XP/stat/gear/CDR purchases.
- Three simple arena variants sharing one clear combat footprint. These are layout placeholders for future distinct arenas.
- Bot replacement preserves a disconnected champion and its progression. Joining later takes over an existing bot. This is not account-backed reconnect recovery.

`HostLAN.cmd` starts a listen server. `JoinLAN.cmd HOST_IP` connects another instance. The development prototype has no matchmaking, accounts, lobby, online backend, or full ten-human certification. Ordinary local network/firewall setup is external to this project.

## Build and assets

`Build.cmd` rebuilds the editor module. The installed Microsoft compiler, Windows SDK, and .NET SDK have been verified. `Tools/BuildContent.py` reproducibly builds the map/materials and copies the licensed local Unreal mannequin template assets. `Tools/RunNetworkSmoke.py` runs the bounded separate-server/client check. Native rules tests are under `Tests/`.

**Iron Warden, Ash Ranger, and Veil Scholar** were generated, retopologized, and rigged in Tripo. All three successfully arrived as Unreal skeletal meshes through the official **Tripo Bridge 1.0.5**, installed in this project's `Plugins/Tripo3DUEBridge`. The user dispatched the transfers from the connected Bridge asset picker. Content lives under `/Game/TripoModels`; full provenance and credit accounting are in `Art/TripoManifest.json`. Use **PlayTripoPreview.cmd** to play with these three models. Their repaired materials, human-scale proportions, facing, and idle/walk/jog poses passed rendered review; a dedicated server and two remote clients also passed art, targeting and realm-privacy checks. The regular Play.cmd retains mannequin art while combat/casting/death animation and deformation polish continue. Preview02 fixes the imported root-scale mismatch; Preview01 is rejected for gameplay.

**Batch01 adds fourteen new animated bodies across sixteen profiles**, including dedicated Lancer and Summoner art, seven human designs and six humanoid creature designs. All eighteen new Tripo exports are saved; Bear, Whisp, Centaur and the future dragon form still need custom motion before their bodies can be used in combat. Separate reload verification and a40-frame native gallery passed. See `Docs/Batch01-VisualReview.md` for reviewed images and the remaining grip, equipment, Orc identity and Troll mesh issues. These are playable prototypes, not final art approval.

Open **Window → Tripo Bridge** in Unreal, keep the editor outside Play mode, and connect Unreal in Tripo Studio's DCC Bridge menu. The official Bridge path works even though ordinary Chrome ZIP downloads returned `ERR_BLOCKED_BY_CLIENT`. `Tools/OpenTripoBridge.py` opens the installed vendor panel through Unreal's editor API and saves stable import batches for up to ten minutes. `Tools/InspectTripoAssets.py` reports mesh, material, texture, scale, and skeleton details without changing assets.

Fab is disabled in this new project because Windows Application Control blocks the installed Fab DLL. The engine's security policy was not changed. See `Docs/Environment.md` for the diagnosis and official repair options.

## What remains

See `Docs/Validation.md` for observed results and limits. `Docs/Design.md`, `Docs/EnfoParity.md`, `Docs/AssetPipeline.md`, and `Docs/ProductionRoadmap.md` define the next production gates: real art/rig integration, locomotion and attack animations, proper navigation/threat and damage schools, distinct arenas, a larger roster and randomized Single Draft, sound/VFX, packaged builds, ten-human full-match testing, performance/latency testing, and exact-version Enfo feature comparison.

The code intentionally keeps rules independent of Unreal to make invariants testable. The current native ability implementation is a prototype; adopting Unreal's Gameplay Ability System is a future engineering decision, not an already-completed feature.
