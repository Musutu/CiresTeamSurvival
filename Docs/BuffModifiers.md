# Buff / debuff presentation: modifier registry, rows, callouts, CC and cast bars

wow-ui pass, 25 September 2026. Presentation only: gameplay stays in the skills and states
that apply effects (CireBuffs, NPC status flags, Shield/Taunt/Slow/Poison fields).

## Modifier summary registry

`Content/Data/BuffModifiers.json` has one row per effect id (the same ids as
`BuffVisuals.json` and `CireBuffs::Apply`). Rows can also live in
`Content/Data/Abilities.json` under a top-level `"buffModifiers"` object (same row format).

```json
"healing_cut": {"type": "curse", "control": "healcut",
                "mods": [{"stat": "Healing", "value": -50, "unit": "%"}],
                "line": "Healing received is halved."}
```

| Field | Meaning |
| --- | --- |
| `type` | `magic`, `poison`, `curse`, `disease`, `physical`, `none`: the icon border colour and dispel hint. |
| `control` | `stun`, `silence`, `root`, `healcut`, `fear`, `disarm`, `taunt`, `slow`, `none`: CC badge on frames and nameplates, a centre callout for hard CC (stun/silence/healcut/fear/disarm/root) and a screen-edge effect while you are stunned, silenced, feared or disarmed. |
| `mods` | `{stat, value, unit, duration?}`. `value` is signed from the holder's view; 0 means label only. Stats with overhead arrows: `ATK`/`Damage Dealt`, `DEF`/`Armor`, `Move` (SPD), `Haste` (AS), `Healing` (HEAL). |
| `line` | One short sentence (tooltip and callout). |
| `callout` | `false` hides the gain callout. Passives and auras never call out. |
| `kind` | Optional override of the BuffVisuals kind (buff/debuff/stance/aura/passive). |

The registry is merged with BuffVisuals (name, school, kind). Symbols are formatted as
`DEF +40%`, `Healing −50%` (true minus sign), `Armor −20% (10s)` for a part shorter than the
effect, `HP −12/s`, and `Stunned 1.5s` / `Silenced 3s` for control.

**API** (`CireEffects.h`): `CireEffects::Find(Id)`, `Symbols(Info, Remaining)`,
`FormatMod`, `FormatControl`, `Gather(Unit, ServerNow, Out, LocalHero)` (records plus derived
state, sorted CC → debuffs (yours first) → buffs → passives), `HardControl(Unit, Now,
&Remaining)`. To add an effect, apply it with `CireBuffs::Apply(Unit, Id, Seconds, Source)`,
add its BuffVisuals row and a registry row; the native test fails when any `KnownIds()` or
BuffVisuals id has no registry row.

## Where it shows

- **Buff/debuff rows** on the player, party, target, focus and boss frames: coloured borders
  (gold buff, blue magic, green poison, purple curse, amber disease, red physical), a darkening
  duration sweep, stack counts, a blinking last 3 seconds; debuffs you applied get a thicker
  white-edged border. Tooltips: name, `DEF +40%  ·  8s left`, the line, the dispel type.
- **Overhead status chips** above every visible unit's nameplate (and above your own head):
  hard CC as a larger chip with a duration ring (STUN, SILENCE, ROOT, HEAL-CUT, FEAR, DISARM),
  stat changes as arrow chips (ATK ▲, DEF ▼, SPD ▼, AS ▲, HEAL ▼), other effects as coloured
  dots with their name on hover. At most 4 plus `+N`; units beyond 17m show CC only; a pop when
  an effect is (re)applied; realm privacy follows the nameplates. Options: all units / enemies
  only / off.
- **Callouts**: when you gain a notable effect, a small banner with its icon, name and symbols
  (gold for buffs, red/purple for debuffs); hard CC is a large centre word ("STUNNED") plus a
  coloured screen edge and a small "Stunned 1.6s" plate while it lasts. `FCireCalloutQueue`
  throttles them: 0.9s minimum spacing, 6s per-id cooldown, at most 3 queued, CC jumps the queue.
- **CC badges** on the target/focus portrait and boss frames.

## Cast bars

`CireCasts::Get(Unit, ServerNow)` returns the current cast of a monster (from
`UCireNPCState::CastInfo`) or a hero (from a registered provider) and detects a cast that ended
before its end time: `Interrupted`, or `Silenced` when the unit is silenced at that moment. Bars
are gold when interruptible, grey with a shield icon when not, green for heals; a stopped cast
flashes INTERRUPTED (red) or SILENCED (purple) for about a second. Shown on the player (above the
action bars), target/focus frames, boss frames and enemy nameplates.

Heroes have no replicated cast fields yet. Gameplay that adds hero cast times registers a reader
once (for example at module start-up):

```cpp
CireCasts::RegisterHeroProvider([](const ACireHero& Hero, float Now, FCireCastView& Out)
{
    if (Hero.CastEndsAt <= Now) return false;            // whatever the new replicated fields are
    Out.bCasting = true; Out.Name = ACireHero::SkillName(Hero.CastingSkill);
    Out.Remaining = Hero.CastEndsAt - Now; Out.Duration = Hero.CastEndsAt - Hero.CastStartedAt;
    Out.Progress = 1.f - Out.Remaining / FMath::Max(.01f, Out.Duration);
    Out.bInterruptible = true; Out.bHeal = /* heal skill */ false;
    return true;
});
```

## Tests

`CireEffects::RunSmoke` (part of `CireOptions::RunSettingsSmoke`, i.e. the native suite): a
registry row for every KnownIds and BuffVisuals id, symbol formatting (DEF +20%, Healing −50%,
Armor −50% (10s), Stunned 1.5s, …), bad rows rejected, callout-queue throttling, and the cast
tracker's interrupted / silenced / completed states. `Tools/RunWowUIGallery.py` stages 24-28
capture a buff callout, the STUNNED callout, buff rows with a tooltip, cast bars (player heal,
interrupted, silenced, uninterruptible boss) and overhead chips at gameplay distance.
