#pragma once
// Class baseline traits by main role (Eric's rulings; maths in Rules/CiresRules Cires::Traits):
//   Support - Mending Strikes: 50% of damage dealt heals the lowest-%-health living party
//             member (the Support included); -20% damage to enemies; +10% attack speed.
//   Tank    - Natural Defense: every incoming damage instance is reduced by a flat 10 (floor 0),
//             applied LAST, after guard/Stone Skin/armor/spell ward, to basic and ability damage.
//   DPS     - Keen Edge: 10% base critical-strike chance.
// Server-authoritative hooks called from the single damage/stat pipeline. Summons, monsters
// and undrafted heroes have no class trait.
#include "CoreMinimal.h"
#include "Rules/CiresRules.h"

class AActor;
class ACireHero;
class ACireGameMode;

struct FCireClassTrait
{
    FString Id;        // icon id: /Game/UI/Abilities/T_<Id>
    FString Name;      // "Mending Strikes"
    FString Summary;   // one line for chips / stats window
    FString Tooltip;   // full passive-style tooltip
    FLinearColor Color = FLinearColor::White;
};

namespace CireClassTraits
{
    /** Main role of a drafted champion (summons/monsters -> Any, i.e. no trait). */
    CIRESTEAMSURVIVAL_API Cires::SkillDraftRole Role(const AActor* Actor);
    CIRESTEAMSURVIVAL_API FCireClassTrait Info(Cires::SkillDraftRole Role);
    CIRESTEAMSURVIVAL_API float CriticalChance(const ACireHero* Hero, float TuningBase);
    CIRESTEAMSURVIVAL_API float AttackSpeedBonus(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(const AActor* Source, float Amount);
    CIRESTEAMSURVIVAL_API float ModifyIncomingDamage(const ACireHero* Hero, float Amount);
    /** Support Mending Strikes: heals the lowest-health party member; shows as healing. */
    CIRESTEAMSURVIVAL_API float OnDamageDealt(AActor* Source, AActor* Target, float Applied);
    CIRESTEAMSURVIVAL_API ACireHero* MendingTarget(const ACireHero* Source);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
