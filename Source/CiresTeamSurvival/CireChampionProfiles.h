#pragma once
#include "CoreMinimal.h"
#include "Rules/CiresRules.h"
class ACireGameMode;
class ACireHero;
struct FCireChampionProfile;
namespace CireChampionProfiles
{
    // Stable compatibility mapping; JSON order does not redefine old draft keys.
    const TCHAR* LegacyProfileId(int32 Choice);
    Cires::SkillDraftRole DraftRole(const ACireHero* Hero);
    // Hybrid roles beyond the primary bucket (e.g. Cinder Arcanist: DPS + Support).
    Cires::RoleMask SecondaryRoles(const ACireHero* Hero);
    // Profile-level role data for the draft screen (no hero required).
    Cires::SkillDraftRole PrimaryRole(const FCireChampionProfile& Profile);
    Cires::RoleMask ProfileRoleMask(const FCireChampionProfile& Profile);
    Cires::RoleMask RoleMaskFromNames(const TArray<FString>& Roles);
    // Teammate already bound to this profile (bots optional). Humans cannot
    // lock a champion a human teammate already locked; bots never block.
    const ACireHero* PickedByTeammate(const ACireHero* Hero,const FString& ProfileId,bool bHumansOnly);
    // Describes authoritative target selection, including self fallbacks.
    FString SkillTargeting(const FString& Id);
#if !UE_BUILD_SHIPPING
    bool RunSmoke(ACireGameMode* Mode);
#endif
}
