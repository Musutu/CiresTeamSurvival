#pragma once
#include "CoreMinimal.h"
class ACireGameMode;
namespace CireCombatExpansion {
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool Run(ACireGameMode* Mode);
#endif
}
