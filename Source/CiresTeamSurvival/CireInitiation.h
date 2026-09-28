#pragma once
// initiation (Playtest 6, section L): team-fight engage support.
//
//  * Set-up: every Initiation-group spell (Content/Data/AbilitiesExpansion.json, recipe "setup") marks the enemies it
//    catches with the "xp_setup" debuff for a few seconds. While Set-up, the initiator's whole team deals +15% damage
//    to them, +25% with area follow-ups (ground zones, novas, circles). Readable: the debuff shows its icon / overhead
//    glyph and a "SET UP!" floating callout (ECireHitOutcome::SetUp) the moment it lands.
//  * Blink Dagger (Items.json "blink_dagger", use kind "blink"): instant blink toward the cursor, up to Radius cm. Taking
//    damage from an enemy champion disrupts it for the item's "duration" seconds (visible "xp_blink_locked" debuff).
// Server-authoritative; clients see replicated buffs, positions and combat events.
#include "CoreMinimal.h"

class AActor;
class ACireHero;
class ACireGameMode;

namespace CireInitiation
{
    /** Live-tunable numbers (Content/Data/Initiation.json; console cire.ReloadInitiation). */
    struct FTuning
    {
        float SetUpSeconds = 3.5f, TeamDamageBonus = .15f, AreaDamageBonus = .25f, CalloutInterval = 1.f;
        float BlinkRange = 1200.f, BlinkCooldown = 14.f, BlinkLockout = 3.f, BlinkMinDistance = 150.f;
    };
    CIRESTEAMSURVIVAL_API const FTuning& Tuning();
    CIRESTEAMSURVIVAL_API bool ReloadTuning(FString* Error = nullptr);
    CIRESTEAMSURVIVAL_API extern const FName SetUpId;
    CIRESTEAMSURVIVAL_API extern const FName BlinkLockedId;
    /** Marks Target as Set-up by Source's spell (refreshes; the callout fires at most once per second per target). */
    CIRESTEAMSURVIVAL_API void ApplySetUp(ACireHero* Source, AActor* Target, const FString& AbilityName);
    /** Set-up amplification: +15% from the initiator's team, +25% for area damage. */
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount);
    /** Champion damage on a Blink Dagger carrier disrupts the dagger. */
    CIRESTEAMSURVIVAL_API void OnDamageDealt(AActor* Source, AActor* Target, float Applied);

    // ---- Blink Dagger ----
    CIRESTEAMSURVIVAL_API bool CarriesBlink(const ACireHero* Hero);
    /** False (with the reason) while the dagger is disrupted by champion damage. */
    CIRESTEAMSURVIVAL_API bool CanBlink(const ACireHero* Hero, FString& Why);
    /** The cursor point sent with the item key (UCireInventory::ServerUseAt); consumed by the next blink. */
    CIRESTEAMSURVIVAL_API void SetUseAim(ACireHero* Hero, FVector Aim);
    /** Blink toward the pending aim (else forward / toward the target), up to Range cm; VFX + sound in and out. */
    CIRESTEAMSURVIVAL_API bool Blink(ACireHero* Hero, float Range, FString& Message);
    CIRESTEAMSURVIVAL_API const TArray<FName>& BuffIds();
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
