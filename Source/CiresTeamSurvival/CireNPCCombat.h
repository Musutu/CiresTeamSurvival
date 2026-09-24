#pragma once
#include "CoreMinimal.h"
class ACireMonster;
class ACireGameMode;
namespace CireNPCCombat {
void Configure(ACireMonster* Monster,int32 Kind,int32 Wave,bool bBoss=false,int32 ChallengeTier=0,int32 Round=1);
void Tick(ACireMonster* Monster,float Delta);
// Cancels current windup and owned combat actors; preserves threat.
void Interrupt(ACireMonster* Monster);
#if !UE_BUILD_SHIPPING
bool RunSmoke(ACireGameMode* Mode);
#endif
}
