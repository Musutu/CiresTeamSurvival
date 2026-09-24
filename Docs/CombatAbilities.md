# Combat and Astra integration — September 23, 2026

## Playable implementation

Use `PlayTripoPreview.cmd`. Select Warden, Ranger, Scholar or Lancer with the four draft cards. Left-click a hostile unit and press Space to toggle basic attacks. Warden has a sword/shield melee attack; Ranger fires arrows; Lancer throws lances; Scholar fires a small arcane projectile. Lancer temporarily uses the Ranger skeletal body.

Attacks play an articulated prototype clip over running locomotion. The default animation lasts 0.65 seconds and releases at 0.25 seconds, shortening proportionally if attack speed requires it. The server validates range, phase, hostility and line of sight again at release. Targeted projectiles track the chosen living target, resolve once on arrival, and cancel when the phase or target becomes invalid. They do not collide with intervening units after launch. Free-aim projectile skillshots are not implemented; the new line skill uses a geometric ground area.

Basic attacks have independent sequential rolls: 5% miss, then 5% dodge if the attack did not miss. Therefore the default hit rate is 90.25%. Ranged miss is 40% uphill, giving a 57% final hit rate after dodge. Height uses capsule feet, with a 10 cm tolerance. Melee retains its baseline chance. Active spell damage is not subjected to these basic-attack rolls. Avoidance produces MISS/DODGE feedback but no health loss or damage-meter credit.

## Ground abilities

Five imported abilities are in the randomized active-skill pool:

| Ability | Shape | Behavior |
|---|---|---|
| Venom Ground | Circle | Poison while inside |
| Cinder Cone | Cone | Delayed burst |
| Grave Line | Line | Delayed burst |
| Ashen Ward | Square | Periodic damage |
| Blight Sigil | Concave custom polygon | Poison while inside |

The new abilities aim at the selected hostile unit's ground position, or 500 cm forward without one. Cones and lines originate at the caster and point toward that position. No cursor placement preview has been implemented. Mana, energy, cooldown, range, ground and blocking geometry are checked before a cast is accepted. Amber boundaries warn before activation; the active color then appears. All damage goes through normal server combat telemetry, so floating numbers, personal combat text and meters agree.

Poison counts appear in player, party, target and focus frames and selected nameplates. Entering a poisonous area adds that area's membership; leaving removes it on the next simulation tick, before another damage pulse. Overlapping areas remain independent. Death, phase changes and teleport hooks clear affected memberships. Areas use a feet-height tolerance to exclude other floors. Custom polygons may be concave but must be simple, bounded and non-self-intersecting.

## Astra workflow

Source application: `F:/Astra-Ability Creator`, guide `UNREAL-5.8.3.md`. Choose Creation studio → Code → Unreal → 5.8.3 → 3D, configure a ground area and generate the implementation. Exports include validated gameplay JSON, cosmetic choreography data, a material builder and optional native data-asset header. Existing Godot abilities and the live library were preserved. The running 47261 service still uses its previous loaded backend and needs an ordinary restart after saving in the application; its active session was not interrupted.

From this game directory:

```powershell
& 'F:/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe' Tools/ImportAstraAbilities.py 'F:/Astra-Ability Creator/work/cire-ue583/AstraAbilities.json'
```

The importer validates before writing, merges IDs and backs up existing data. Restart the game to reload `Content/Data/AstraAbilities.json`. The five starter IDs are already in the draft pool; additional new IDs also need registration in `Rules/CiresRules.cpp` before they can be offered. The imported JSON is staged as game content for packaging, which still requires a separate packaged-build check.

## Presentation limits and next work

The new weapons and attack clips are original engineering prototypes, not production animation or finished AA art. No additional Tripo credits were used. Original Tripo meshes, materials and locomotion assets were protected during generation. Remaining work includes proper grip/hand IK, animation polish, hit reactions, audio, weapon trails and authored Niagara effects.

Unreal currently plays the ground geometry and gameplay fields from Astra. It does not yet replay Astra's complete modeled-mesh choreography or arbitrary mechanics. Five existing saved abilities were exported as read-only references under Astra's `work/cire-ue583/existing-references`; that does not mean their full behavior is ported. Older spells still retain temporary debug effects. Production-quality dark-fantasy VFX are the next art/integration pass.

## Checks

`Tools/RunCombatChecks.py` runs deterministic attack/area tests, the existing telemetry suite and nine rendered captures. It requires explicit engine PASS markers and full 1920×1080 PNGs. `Tools/RunInterfaceSmoke.py --tripo-champions` verifies the existing two-client privacy, targeting, art, chat and phase flow. See `Validation.md` for executed results and their limits.
