#pragma once
#include "CoreMinimal.h"
class ACireHero;
class ACireMonster;
class ACireGameMode;
namespace CireThreat {
void Damage(ACireMonster* Monster, ACireHero* Source, float EffectiveDamage);
void Healing(ACireHero* Source, ACireHero* Target, float EffectiveHealing);
void Engage(ACireMonster* Monster, ACireHero* Hero);
void Taunt(ACireMonster* Monster, ACireHero* Hero, float Seconds);
ACireHero* Select(ACireMonster* Monster);
void Clear(ACireMonster* Monster);
void Remove(ACireHero* Hero);
#if !UE_BUILD_SHIPPING
bool RunSmoke(ACireGameMode* Mode);
#endif
}
