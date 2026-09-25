#pragma once
// items-v2: ultimate upgrades. Carrying a path-defining unique with the "ultimateUpgrade" passive
// (Sigil of Apotheosis) makes every ultimate also fire its Abilities.json "ultimateUpgrade": one
// extra, data-driven effect (party aura, shield, heal, mana restore, cleanse, stun, silence,
// slow, armor break, damage, cooldown refund). Tools/UltimateUpgrades.py is the source table.
#include "CoreMinimal.h"

class ACireHero;
struct FCireAbilityDef;

namespace CireUltimateUpgrades
{
    /** Cast hook (server): honours the upgrade's delay; returns false when there is nothing to fire. */
    CIRESTEAMSURVIVAL_API bool Trigger(ACireHero* Hero, const FCireAbilityDef& Def);
    /** Fires every effect now around Center (upgrade center) and returns how many units it touched. */
    CIRESTEAMSURVIVAL_API int32 Apply(ACireHero* Hero, const FCireAbilityDef& Def, FVector Center);
    /** The upgrade's center: the aimed point or hostile target for "target" upgrades, else the caster. */
    CIRESTEAMSURVIVAL_API FVector CenterFor(const ACireHero* Hero, const FCireAbilityDef& Def);
    /** Primary attribute value (STR, AGI or INT, whichever is the hero's primary). */
    CIRESTEAMSURVIVAL_API float PrimaryValue(const ACireHero* Hero);
}
