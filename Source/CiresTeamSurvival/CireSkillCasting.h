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
    // casting-rules (Playtest 6): shared aim resolution for barriers, constructs and summons. Placement ignores clipping:
    // an aim up to 25% past Range is pulled back into range, then snapped onto the navmesh / ground. Only an aim outside
    // the caster's realm (or far out of range) is refused, with a notice.
    CIRESTEAMSURVIVAL_API bool PlacementAim(ACireHero* Hero, FVector& Aim, float Range);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunCastSmoke(ACireGameMode* Mode);
#endif
}
