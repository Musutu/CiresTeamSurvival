#pragma once
#include "CoreMinimal.h"
class ACireHero;
class ACireGameMode;

namespace CireSkillCasting
{
    CIRESTEAMSURVIVAL_API bool Handles(const FString& Id);
    // Validates the learned slot, resources, cooldown, realm and aim again even
    // when called outside Hero::Cast. A failed spawn never charges resources.
    CIRESTEAMSURVIVAL_API bool Cast(ACireHero* Hero, int32 Slot, const FString& Id);
    CIRESTEAMSURVIVAL_API FString Name(const FString& Id);
    CIRESTEAMSURVIVAL_API FString Description(const FString& Id);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunCastSmoke(ACireGameMode* Mode);
#endif
}
