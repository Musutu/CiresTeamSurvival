#pragma once
#include "CoreMinimal.h"
class ACireHero;
class ACireMonster;
class ACireGameMode;
// WoW-style threat. Rules (tunable in NPCArchetypes.json "threat"):
// - damage threat = effective damage x hero role multiplier (tank 5x, others 1x);
// - effective healing threat = heal x HealingThreatMultiplier, split across engaged monsters;
// - a monster keeps its current target until a challenger exceeds 110% of that
//   target's threat inside melee range or 130% outside it;
// - taunt raises the taunter to the top threat and forces the target for its duration;
// - threat is never lost to time or distance: only unit death (hero or monster) or an
//   explicit ability (Transfer/Scale: misdirect, fade, "drop X% threat") removes it.
//   Exceptions: armored escorts ignore threat; the arena/recovery phase change clears all.
// Every target change is published through UCireNPCState::OnAggroChanged().
namespace CireThreat {
void Damage(ACireMonster* Monster, ACireHero* Source, float EffectiveDamage);
void Healing(ACireHero* Source, ACireHero* Target, float EffectiveHealing);
void Engage(ACireMonster* Monster, ACireHero* Hero);
void Taunt(ACireMonster* Monster, ACireHero* Hero, float Seconds);
// Adds raw threat without the damage role multiplier (abilities, fixtures).
void AddRaw(ACireMonster* Monster, ACireHero* Hero, float Amount);
// Moves Fraction (0..1) of From's threat to To on this monster.
void Transfer(ACireMonster* Monster, ACireHero* From, ACireHero* To, float Fraction);
// Multiplies a hero's threat on this monster (e.g. 0.5 for a fade).
void Scale(ACireMonster* Monster, ACireHero* Hero, float Multiplier);
// rules-conformance: an ability that explicitly drops/reduces threat: multiplies the hero's threat on every
// monster (0 drops it entirely, 0.5 = "reduce threat by 50%"). Ability DB field "threatScale" (applied on cast).
void ScaleAll(ACireHero* Hero, float Multiplier);
// Pull threshold multiplier a challenger needs against the current target.
float PullRatio(const ACireMonster* Monster, const ACireHero* Challenger);
// Idle decay and replicated table refresh; called from the NPC tick.
void Tick(ACireMonster* Monster, float DeltaSeconds);
ACireHero* Select(ACireMonster* Monster);
void Clear(ACireMonster* Monster);
void Remove(ACireHero* Hero);
#if !UE_BUILD_SHIPPING
bool RunSmoke(ACireGameMode* Mode);
bool RunRulesSmoke(ACireGameMode* Mode);
#endif
}
