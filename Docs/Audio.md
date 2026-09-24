# Audio

Music, district ambience, footsteps and game-event sounds. Everything is data-driven
(`Content/Data/Audio*.json`) and runs on the client; a dedicated server never creates it.
All shipped audio is CC0 or CC BY 4.0: see [Art/Audio/PROVENANCE.md](../Art/Audio/PROVENANCE.md).

## Player controls: Options (F9) > Audio

| Control | Default | Effect |
|---|---|---|
| Mute all game sound | off | silences every bus (music, ambience, SFX, footsteps, UI) |
| Play music | on | fades the score out/in; resumes with the current state |
| Heavy footstep camera shake | **off** | slight camera bump on your own heavy footfalls (Bear, Behemoth, Ether Golem) |
| Master / Music / Combat effects / Ambience / Interface | .85 / .60 / .85 / .80 / .70 | linear; each bus = master x bus |

The panel also shows the live score state and track title, and the CC BY music credits.
The settings live in the local profile (`Saved/Config/CireUI.ini`, keys `MusicVolume`, `AmbienceVolume`,
`bMusicEnabled`, `bFootstepCameraShake` next to the existing `MasterVolume`/`SFXVolume`/`UIVolume`/`bMuteAudio`).
Older profiles without the keys keep the defaults. No schema bump was needed.

## Architecture

| File | Role |
|---|---|
| `CireAudio.h/.cpp` | public API, cue data, `UCireAudioSubsystem` (per-world tick: bus volumes, music, ambience, footsteps, event detection, camera shake) |
| `CireMusic.h/.cpp` | `FCireMusicDirector` (pure state machine) + `FCireMusicPlayer` (two crossfading slots + stinger slot) |
| `CireAmbience.h/.cpp` | district beds, random one-shots, nearest prop emitters, combat ducking |
| `CireFootsteps.h/.cpp` | armour classes, contact detection, surface trace, step budget |
| `CireAudioTests.cpp` | native smoke (`CireAudio::RunAudioSmoke`) and the offline render probe |

Shared-file hooks are small and marked `// audio:`: `CireUISettings` (fields), `CireOptions.cpp` (click cue, hover
cue, Audio tab), `CireHUDWow.cpp` (`PlayWowSound` and banner sounds route through cues first; the old synthesized
tones remain the fallback), `CireCombatExpansionProbe.cpp` (runs the audio smoke).

### Mix

Created by `Tools/BuildAudioContent.py` under `/Game/Audio/Mix`:

* **Sound classes** `SCL_Master` > `SCL_Music`, `SCL_SFX` (> `SCL_Footsteps`), `SCL_Ambience`, `SCL_UI`, `SCL_Voice`.
  Options volumes are applied at runtime with a sound-mix class override (`CireAudio::BusGain`), so cue volumes never
  multiply the sliders themselves. Legacy sounds with no class (`/Game/Audio/CireCombat`) keep their own multiplier.
* **Submixes** `SMX_Music`, `SMX_SFX`, `SMX_Ambience`, `SMX_UI`, `SMX_Voice`: every wave is routed explicitly, so each bus can be
  recorded or metered.
* **Concurrency**: footsteps 14 (stop quietest), ambience one-shots 6, prop emitters 8 (stop farthest), UI 3, events 8, music 3.
  Footsteps are also capped at 36 per second by a token bucket and limited to the 24 nearest bodies within 28 m.
* **Attenuation**: `ATT_Footstep` (1.5 m inner, 24 m falloff), `ATT_Prop` (2.5 m / 28 m), `ATT_Ambient` (6 m / 52 m),
  `ATT_Large` (12 m / 120 m, horns and roars). All use natural-sound falloff, distance low-pass and reverb send.
* **Reverb**: `REV_StoneStreets` (1.45 s decay, dense early reflections), activated for the whole map. Music and UI classes
  do not send to it.

## Music (`AudioMusic.json`)

| State | When | Tracks (Kevin MacLeod, CC BY 4.0) |
|---|---|---|
| town | warm-up, breathers between waves, prep, recovery | The Pyre / Oppressive Gloom |
| combat | an advancing lane wave, or a challenge pack fighting near you; held 6 s after the last threat | Five Armies / Crusade |
| boss | a lane boss alive in your realm, or a Pack Leader engaged near you; held 9 s | Killers / Black Vortex |
| arena | PvP arena phase | Death and Axes |
| finish | match over: one stinger, then silence | Hero Theme (victory) / Greta Sting (defeat) |

States rotate through their tracks on each entry and crossfade over 3 s. The director is pure and unit-tested
(`FCireMusicDirector::Update`).

## Ambience (`AudioAmbience.json`)

The district comes from `CireEnvironmentProps::DistrictAt` (TownLayout.json); outside the town it is `outskirts`,
in the PvP arena `arena`. Beds are keyed by sound, so a layer shared by two districts carries over without restarting.
A night layer (crickets, tawny owl) plays everywhere. During combat, boss and arena music the ambience ducks to 45%.

| District | Beds | One-shots |
|---|---|---|
| breach / gate | wind, cold howl | crows, gate chains, gate creaks |
| market | medieval-festival crowd, square murmur | distant merchant calls, ox-cart wheels |
| residential (Cooper's Lanes) | hearth crackle, wind chimes, light wind | dogs, doors, wood chopping |
| square | fountain, pigeons, murmur | church bell phrases, distant funeral bell |
| approach / castle | banners flapping, wind | anvil strikes, blacksmith, guards marching |
| arena | cold wind | crows |

**Prop emitters**: the nearest 8 town props within 26 m play positional loops: `fire_pit`, `brazier`, `lamp`, `wall_lantern`
(fire crackle), `tavern` (hearth), `fountain` (water), `well` (winch and bucket every 14-26 s). They are found from the town
instanced meshes (component tag = slot id), so props added by the town work are picked up automatically. The current
layout has no `fountain` slot, so the square's fountain is a bed layer until one is placed.

## Footsteps (`AudioFootsteps.json`)

Contact sources, in order:

1. **Creature gait phase**: Bear and Centaur use `UCireCreatureArt::MotionPhase()`; a step fires at the two diagonal contacts per cycle (phase 0 and PI).
2. **Foot bones**: humanoids (Tripo and mannequin) use `foot_l`/`foot_r`; a step fires when a lifted foot drops back to its planted height.
   If a moving body produces no contact for 1.5 s (for example, an animation that doesn't lift the feet), that body falls back to:
3. **Speed cadence**: stride grows from `strideWalk` to `strideRun` between 250 and 600 cm/s.

| Class | Sound | Used by |
|---|---|---|
| plate | stone/chainmail boot + armour rattle | knight, paladin_righteous, dwarf_miner (lower pitch), hollow_shieldbearer, ironbound_bruiser |
| plate_heavy | pitched-down plate + low thump + heavy rattle | orc_chieftain, hollow_siegebreaker |
| mail (semi-armoured) | leather boot + light rattle + creak | lancer, paladin_holy, drakish_footman, hollow_infantry |
| leather | leather boot + creak | ranger, troll berserkers, barbed_hunter |
| cloth | soft step + cloth rustle | scholar, summoner, wizard, keeper_of_light, blight_caster |
| bark | soft, low step + wood creak | dryad |
| bear | heavy stomp + bass thump, camera shake 1.4 cm | bear |
| behemoth | heavier, lower bear, camera shake 2.2 cm | totemic_behemoth |
| beast | monster steps + thump | gravemaw_pack_leader |
| golem | stone impact + thump, camera shake 1.6 cm | ether golems (pitch varies by form) |
| hooves | two clops per contact (70 ms apart) | evergrove_centaur |
| whisp | faint shimmer, sparse | whisp |

Every step picks a random, non-repeating member of each layer, with random pitch and volume
(15% jitter) and a louder level when running. The surface is traced under the foot: town meshes and the
route road count as stone; ground materials named grass/dirt/mud as dirt; otherwise the Breach Fields are dirt
and the town is stone. Your own steps play 15% quieter.

## Event cues (`AudioCues.json`)

| Cue | Trigger |
|---|---|
| `ui_click`, `ui_hover` | Options/Developer buttons (hover only while Options is open) |
| `level_up` | level-up burst (replaces the synthesized chime) |
| `aggro_taken` | aggro-gained alert (sword draw) |
| `banner_wave` / `banner_boss` / `banner_challenge` | war horn / distant horn on wave, boss and challenge banners |
| `banner_prep` / `banner_cleared` / `banner_recovery` | church bell tolls |
| `banner_arena` | war drums |
| `coins_buy` | automatically when your gold goes down; the shop can call it too (a same-frame repeat is dropped) |
| `coins_sell`, `loot_pickup` | for the shop / loot code to call |
| `teleport_channel` | loop on your body for the last 3.5 s of prep before the arena transfer |
| `teleport_arrive` | when your body jumps more than 15 m (arena transfer, recall, recovery) |
| `pack_leader_roar` / `pack_leader_growl` | a Pack Leader or lane boss first engages or appears / starts each cast |
| `aura_apply`, `aura_heal` | generic buff sounds for the aura/VFX work |

### API for other systems

```cpp
#include "CireAudio.h"
CireAudio::PlayCue(this, TEXT("coins_sell"), Location);           // positional one-shot
CireAudio::PlayCue2D(this, TEXT("loot_pickup"));                  // 2D one-shot
UAudioComponent* Hum = CireAudio::PlayAttached(TEXT("teleport_channel"), Mesh); // loop, caller stops it
CireAudio::SpawnCueAtLocation(this, TEXT("emt_fire"), Location);  // returns the component (loops)
```

All calls are safe on a server (no-op), when muted, or with an unknown id (returns false/nullptr). To add a sound:
add a cue to `AudioCues.json` that points at an existing `Folder/Name` under `/Game/Audio`. For new source audio, add it to
`Tools/FetchAudioSources.py` (CC0/CC-BY only) and a line in `Tools/ProcessAudio.py`, then rebuild (below) so it gets the
right class, submix, attenuation and concurrency.

## Asset pipeline

```
Tools/FetchAudioSources.py    download (Freesound CC0 previews, Kenney CC0, incompetech CC BY) -> Art/Downloads/Audio (git-ignored)
                              writes Art/Audio/AudioSources.json (URL, author, licence, sha256) and PROVENANCE.md
Tools/DecodeAudioSources.py   Ogg -> 16-bit WAV through Unreal's importer/exporter (isolated ContentBuilder project)
Tools/ProcessAudio.py         stdlib DSP: 48 kHz, seamless loops, onset-sliced footsteps, mono for positional sounds, levels
                              -> Art/Audio/ProcessReport.json
Tools/BuildAudioContent.py    mix assets + imports into ContentBuilder, then copies to Content/Audio/{Mix,Music,Ambience,Footsteps,SFX,UI}
```

The `.uasset` files are the shipped result. Raw downloads and intermediate WAVs are not versioned because the
pipeline rebuilds them from the pinned sources.

## Verification

* **Native smoke**: `CireAudio::RunAudioSmoke` runs inside `Tools/RunExpansionChecks.py --only native`. It checks settings
  persistence (volumes, toggles, clamping, old profiles, reset), bus gains, that every cue/bed/track/footstep sound exists,
  that every roster profile and NPC archetype has an explicit armour class, music transitions (holds, prep/arena/recovery,
  one stinger per finish), footstep cadence against speed for every class, gait-phase contacts, and the step budget.
* **Offline render**: `Tools/RunAudioChecks.py` launches `-CireAudioProbe`. The probe mutes every submix output, including
  the main one, so nothing reaches the speakers. It lifts the unfocused-window volume, which BaseEngine.ini sets to 0, and
  records the Music, Ambience and SFX submixes to WAV while it plays every music state, six districts, all footstep classes
  and nine event cues. The script writes `levels.json`/`levels.md` and fails on any silent segment. Run it with `au.NeverDisableSubmixes 1`,
  as the script does; otherwise idle submixes drop frames and the timeline drifts.
* **Soak**: `-CireAudioSoak=<seconds>` plays a normal session, logs `CIRE_AUDIO_SOAK_DONE` and `CIRE_AUDIO_STATS`
  (footsteps by contact source, dropped steps, one-shots, district, music state), then quits. For example, add
  `-CireBalanceLab=Player -CireBalanceBots=4 -CireBalanceWave=3`.

## Known limits

* Mixing levels were set by measurement, not by ear. Relative levels (footsteps vs music vs ambience) need a listening pass.
* Freesound sources are the public 128-192 kbps previews, not the original uploads (downloading those needs a
  Freesound account). They are fine for ambience and one-shots; replace any that sound thin.
* Some field recordings may carry faint modern background noise (distant traffic under dogs or crows). The processing
  slices around the events, but check by ear.
* The Bear/Centaur phase contacts and the humanoid foot-bone contacts are verified by logic and a live soak.
  The feel on camera still needs a play session.
