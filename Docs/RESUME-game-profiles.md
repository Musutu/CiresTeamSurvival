# RESUME: game-profiles (feat/game-profiles)

Eric (2026-09-28): "so all of these editor abilities can save the state once set and be a profile or a game mode or type?"
Answer shipped here: every F8 editor saves **named profiles**, and a **game type** (WavePresets.json row) names the profile
each editor plays with.

## How Eric uses it
1. F8 > any editor page (Match, Spawn/stats, Effects, Economy, Packs, Movement). Set values, then use the **PROFILE bar**
   at the bottom of the Options window: `[DOMAIN] [name] [LOAD] [SAVE AS] [RENAME] [DELETE]`.
   - SAVE AS: type a name, Enter. Saved to `Content/Data/Profiles/<Domain>/<name>.json`.
   - Click the name to cycle the saved profiles, LOAD makes it live. `>` marks the live one.
   - **Default** = the page's own data file (unchanged; the page's SAVE button still writes it). LOAD Default re-reads it.
   - Packs page: click the `PACKS <>` tag to switch to **SPACING** (boss sizes, capsules, separation = UnitSpacing.json).
   - Match / Spawn / Effects share one profile (FCireDeveloperSettings); LOAD turns the F8 overrides on, Default turns them off.
2. F8 > Waves > MODES & SCALE > **PROFILES BUNDLE**: pick a preset (Standard / Hero TD / Hybrid / custom) in PRESETS, then
   click each slot (LAYOUT, ABILITY TUNING, HERO KITS, ECONOMY, PACKS, MOVEMENT, MATCH, SPACING, WORLD EDITS) to cycle its
   profile; each click is saved to WavePresets.json at once.
   **SAVE CURRENT AS GAME TYPE**: type a name; creates a preset from the wave draft plus the profile live in every editor.
3. Host picks the game type in champion select (GAME TYPE). The list now shows a third line per type: what it bundles
   ("Economy: Rich | Packs: Hard" or "Every editor on Default").

## Design (one pattern everywhere)
- `CireProfiles` (CireProfiles.h/.cpp): store = `Content/Data/Profiles/<Domain>/<name>.json`
  `{"schemaVersion":1,"domain","name","values":{...}}`; List (Default first) / SaveValues / LoadValues / Rename / Delete;
  per-domain Capture / ApplyValues / ApplyDefault; Load = Default + the profile's values (partial profiles never inherit
  the previous profile). Active profile per domain tracked process-wide.
  Domains: Match (CireDeveloper settings), Economy (kill gold + Skill Shop rules + Vendors.json shop pricing), Packs
  (JunglePacks stats + per-tier health/damage), Movement (MovementTuning.json), Spacing (UnitSpacing.json).
- Game type bundle: `FCireWavePreset::Bundle` (TMap key -> name) + `AllowTuning` kept through saves (they were dropped by
  SAVE OVER before; CapturePreset now inherits kitProfile / bundle / allowTuning from the preset it saves over).
  Keys: layout, tuningProfile, kitProfile (existing field), economyProfile, packProfile, movementProfile, matchProfile,
  spacingProfile, worldEdit. Missing/empty/"Default" = Default.
- Applied in ONE place: `CireGameProfiles::ApplyGameType(Mode, Preset, bAtInit)` called from `CireWaveDirector::Initialize`
  (match init; preset null = every editor Default) and `SelectPreset` (host pick). It runs the Ability Tuner preset hook,
  the layout, the five data profiles and the world edit set, and logs `CIRE_GAME_TYPE_APPLIED ...`.
  Kits: CireKitEditor already reads kitProfile from the replicated WavePreset.
  A layout restart re-running init with the same game type keeps hand-loaded F8 profiles.
- Clients: `ACireGameState::WavePreset` is now `ReplicatedUsing=OnRep_WavePreset`; the client applies the same data
  profiles from its own files (`CIRE_GAME_TYPE_CLIENT ...`). Match profiles are standalone-only (F8 overrides) and are
  reported as skipped in multiplayer.
- Layout: `CireGameProfiles::RuntimeLayoutPath()` (used by `CireLanePath::LoadActive`) = the game type's
  `Content/Data/MapLayouts/<name>.json`, else MapLayout.json. Host pick -> `RestartOnLayout` (before wave 1); at init ->
  live route reload. MapLayout.json / MapLayouts are only read.
- World edits: `CireGameProfiles::RegisterWorldEdit(Apply, List)` is the tiny interface for feat/world-editor. Until
  that module registers, a `worldEdit` key is kept and logged `CIRE_GAME_TYPE_WORLD_EDIT_PENDING`.

## Done
- Profile store + 5 domains + PROFILE bar on Match/Spawn/Effects/Economy/Packs(+Spacing)/Movement pages.
- Bundle keys in WavePresets.json (parse + write + round trip), one apply point, client OnRep apply.
- MODES & SCALE bundle editor + SAVE CURRENT AS GAME TYPE; picker summary line + tooltip.
- Native tests `CireProfiles::RunTests` (66 checks): store round trip, every domain capture/save/Default/load, a game type
  applying each domain profile, missing profile -> Default, none -> all Default, bundle JSON round trip.
- Network probe: server picks runtime game type `netprobe_profiles` (profiles in Saved/GameProfilesNetProbe);
  `CIRE_NET_SERVER_PROFILES_PASS` and `CIRE_NET_CLIENT_PROFILES PASS` verify boss gold x12.5, pack health x1.35,
  roll energy 31, melee reach bonus 23 live on server and client.
- Review capture: console `cire.ProfilesCapture` (screenshots the bar + bundle view to Saved/ProfilesCapture, then quits).

## Not done / next
- VFX tuning (VFXTuning.json) not made a profile domain (not in the preset key list; player-side option already scales it).
- Pricing folded into the Economy profile (no separate pricingProfile key).
- No Spacing sliders exist in F8 (console `cire.Spacing`); the bar saves/loads what is live.
- World edit application waits for feat/world-editor to call `CireGameProfiles::RegisterWorldEdit`.

## Assumptions / questions for Eric
- One Match profile covers the Match, Spawn/stats and Effects pages (they share one settings block / ini).
- Match profiles only apply in a development standalone game (that is where F8 overrides exist at all).
- "Default" is never a file: it is always today's data file, so nothing changes for existing data.
- Renaming/deleting a profile does not rewrite game types that name it (they fall back to Default and log it).
- Economy profile includes shop pricing (vendor discount / out-of-town surcharge).

## Shared-file edits (small, additive)
- CireWaves.h: `FCireWavePreset::Bundle`, `AllowTuning`; `RegisterRuntimePreset`.
- CireWaveData.cpp: parse/write bundle keys + allowTuning, operator==, CapturePreset inherits bundle, runtime presets.
- CireWaves.cpp: Initialize + SelectPreset call `CireGameProfiles::ApplyGameType` (replaces the direct tuner call; tuner
  still runs inside), probe pick hook.
- CireGame.h: `WavePreset` ReplicatedUsing=OnRep_WavePreset (+ UFUNCTION decl; body in CireProfiles.cpp).
- CireLanePathLayout.cpp: LoadActive reads `CireGameProfiles::RuntimeLayoutPath()`.
- CireHUD.h: `DrawProfileFooter`, `DrawGameTypeBundle` declarations. CireHUD.cpp: Esc cancels the name box.
- CireDeveloperHUD.cpp: one call `DrawProfileFooter(X,Y+457)`.
- CireWaveEditor.cpp: PROFILES BUNDLE toggle wraps the scale & damage rows; `DrawGameTypeBundle`.
- CireRosterHUD.cpp: GAME TYPE list row 56 px with the bundle summary line + tooltip.
- CireChat.cpp (InputChar), CireController.cpp (keyboard ownership + client probe check), CireCombatExpansionProbe.cpp (tests).
- Docs/Waves.md: bundle paragraph.
- New files: CireProfiles.h/.cpp, CireProfilesTests.cpp, CireProfileBar.h/.cpp, Docs/RESUME-game-profiles.md.

## Gate logs
See the bottom section (updated at each run).
