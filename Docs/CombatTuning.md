# Combat authoring for Unreal 5.8.3

`Content/Data/CombatTuning.json` is the authoritative combat configuration. Units
are centimetres, seconds, and absolute damage/health. It contains `globals`,
`skillshots`, `constructs`, `summons`, and optional `roleSkills`. Ground areas retain the separate,
compatible `AstraAbilities.json` format. A recipe ID chooses an existing native
handler; adding an arbitrary ID does not register a new learned skill.

## Edit in Astra

Select **Unreal / 5.8.3 / 3D** in Creation studio → Code. **Combat tuning** opens
typed editors for a skillshot, wall/protection, summon, or global combat values.
Keep the runtime ID equal to the game recipe you intend to change. Global values
are opt-in: check only the fields that the package should update. Save the
definition, Generate implementation, review, then use the generated result.
Download `CombatTuning.patch.json` from the ability package. Normal ability save
and revision handling preserves unrelated fields; built-in generation needs no
paid provider. Existing Godot abilities retain their own engine profile.

From the game directory:

```powershell
python Tools/ImportCombatTuning.py PATH/CombatTuning.patch.json --check
python Tools/ImportCombatTuning.py PATH/CombatTuning.patch.json
```

The importer validates the complete merged result before writing, preserves
unmentioned IDs and global values, and creates a timestamped `.bak` next to the
target. It atomically replaces the JSON file after the backup. `--check` makes no
changes. A full `CireCombatTuning` file may initialize an empty target; a patch
requires an existing full file. Restore a backup by copying it over the target.
Restart the game to load changes, or invoke `CireSkillTuning::Reload` from native
development tooling. Running actors keep their copied spec; the reload affects
subsequent spawns. Invalid native loads preserve the last valid configuration.

## Values and semantics

- Champion attributes are native rules, not this file (`Rules/CiresRules.h`): each STR gives 10 health,
  0.1 armor and 0.1 spell ward (25 health before 25 September 2026), each INT 30 mana, each AGI 1% attack
  speed. A flat base of 15 x starting STR keeps level-1 health unchanged; see Progression.md
  "Strength scaling". NPC damage below was not retuned for it.
- Globals: crit chance `.05`, crit multiplier `1.5`; tank damage threat `5`, other
  damage threat `1`, effective healing threat `.4` distributed among engaged foes.
  NPC damage is fixed by type: basic `18`, bruiser `24`, caster `14`, ranged `16`,
  boss `40`, challenge `24`. Health grows from base `800` by `80` per wave; boss
  multiplier `6`, challenge base `1200`. Type health factors are `1`, `1.5`, `.8`,
  `.9`. Runtime wave indexing is owned by the encounter spawner.
- Skillshots: speed `50–10000`, radius `1–200`, range `50–5000`, lifetime
  `.05–30`, warning `0–10`, damage `0–10000`, hit limit `1–32`, reflection limit
  `0–8`. World, player, monster, protection, and summoned wall collisions each
  accept `ignore`, `stop`, `pierce`, or `reflect`. Ignore makes no hit; stop ends
  travel; pierce hits and continues; reflect redirects within the reflection cap.
  Default repeat-target hits are disabled. Warning time precedes damaging travel.
- Constructs: wall or protection, health `1–100000`, lifetime `.1–120`, width
  `20–2000`, depth `10–1000`, height `20–2000`; independent movement/projectile
  blocking and destructibility flags. Friendly projectiles pass friendly
  constructs. Movement blocking uses solid collision and also blocks friendlies;
  `blockFriendly=false` is reserved authoring data, not a team-aware physics pass.
  Protection response defines its interaction with incoming projectiles.
- Summons: count `1–3`, health `1–100000`, damage `0–10000`, duration `.1–300`,
  explicit mana/energy costs, cooldown and cast range. Commandability, leash
  `100–5000`, movement speed `50–1500`, attack range `50–2000`, and visual
  archetype `0–3` are authored independently. `oathbound_guardian` creates one
  commandable ally; `spectral_pack` creates three temporary AI allies.

Base weapon shot damage comes from the owner's weapon calculation. NPC ranged
shots use the encounter's fixed archetype damage. Their JSON damage defaults
match the initial global values; the runtime launch damage takes precedence.
Shot, construct and summon specs include explicit mana/energy costs, cooldown
and cast range; the native casting helper enforces these values. Existing rank
scaling remains in the native skill handlers. The visual
timeline never determines collision, damage, or authority.

## Files and checks

`CireSkillTuning.h` exposes reflected specs and `Get`, `FindSkillshot`,
`FindConstruct`, `FindSummon`, `FindRoleSkill`, transactional `ParseJson` and `Reload`.
`Tools/CombatTuningSchema.json` mirrors Astra `app/unreal-combat.mjs` for the
existing recipe types and adds native role-skill fields for the standalone
Python importer; edit bounds consistently with the native parser. Role-skill
editing currently uses JSON rather than an Astra form; see `Docs/RoleDrafts.md`.
Unknown fields, duplicate IDs, nonfinite numbers, unsafe IDs and oversized data
are rejected. Data size is capped at 256 KB; recipe counts are 64/32/32/32.

Run `python Tools/TestImportCombatTuning.py` for merge, backup and rejection
checks. Astra `runtime/node.exe --test tests/*.test.mjs` includes API schema,
generation/review/apply, sparse-global edits, summoning bounds, and preserved
Godot exports. `CireSkillTuning::RunValidationSmoke` is the opt-in native check;
these authoring tests do not establish Unreal rendering or multiplayer behavior.
