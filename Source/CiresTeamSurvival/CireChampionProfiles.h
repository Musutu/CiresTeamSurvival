#pragma once
#include "CoreMinimal.h"
#include "Rules/CiresRules.h"
class ACireGameMode;
class ACireHero;
namespace CireChampionProfiles
{
    // Stable compatibility mapping; JSON order does not redefine old draft keys.
    const TCHAR* LegacyProfileId(int32 Choice);
    Cires::SkillDraftRole DraftRole(const ACireHero* Hero);
    // Describes authoritative target selection, including self fallbacks.
    FString SkillTargeting(const FString& Id);
#if !UE_BUILD_SHIPPING
    bool RunSmoke(ACireGameMode* Mode);
#endif
}
