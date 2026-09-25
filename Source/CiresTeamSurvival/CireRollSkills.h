#pragma once
// Dodge-roll synergy skills (20; data in Content/Data/Abilities.json, category/effectTags "roll").
// Every trigger fires per successful UCireMobility::StartRoll, so extra roll charges trigger again.
// Server authority; timed effects are CireBuffs records (replicated, shown by the buff UI).
// Numbers come from CireAbilityDB::EffectiveStats at the hero's Skill Shop level. Docs/Abilities.md "Dodge-roll skills".
#include "CoreMinimal.h"

class AActor;
class ACireHero;
class ACireMonster;
class ACireGameMode;

namespace CireRollSkills
{
    CIRESTEAMSURVIVAL_API bool Knows(const FString& Id);
    CIRESTEAMSURVIVAL_API bool IsPassive(const FString& Id);
    CIRESTEAMSURVIVAL_API bool IsActive(const FString& Id);
    CIRESTEAMSURVIVAL_API FString Name(const FString& Id);
    CIRESTEAMSURVIVAL_API FString Description(const FString& Id);
    CIRESTEAMSURVIVAL_API bool Cast(ACireHero* Hero, int32 Slot, const FString& Id);
    CIRESTEAMSURVIVAL_API const TArray<FString>& AllIds();
    CIRESTEAMSURVIVAL_API const TArray<FName>& BuffIds();

    // ---- hooks ----
    /** UCireMobility::StartRoll succeeded (per roll / per charge). */
    CIRESTEAMSURVIVAL_API void OnRoll(ACireHero* Hero, FVector Direction);
    /** A hit was dodged by the roll's i-frames (Riposte, Evasive Stance). */
    CIRESTEAMSURVIVAL_API void OnDodgedHit(ACireHero* Hero, AActor* Attacker);
    /** Blur: true when the incoming hit is dodged (already broadcast as a dodge). */
    CIRESTEAMSURVIVAL_API bool TryBlur(ACireHero* Hero, AActor* Attacker, const FString& AbilityName);
    /** Tumbler's Edge, Killer Instinct (sets *InOutCritical) and Momentum. */
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName, bool* InOutCritical = nullptr);
    CIRESTEAMSURVIVAL_API float MoveSpeedMultiplier(const ACireHero* Hero);
    /** Quickened Mind: consumes the charge and returns true when the next timed cast is instant. */
    CIRESTEAMSURVIVAL_API bool ConsumeInstantCast(ACireHero* Hero);
    CIRESTEAMSURVIVAL_API void OnKill(ACireHero* Killer);
    /** Mines, bot rolling and bot stances (called from CireCrowdControl::TickHero). */
    CIRESTEAMSURVIVAL_API void Tick(ACireHero* Hero, float DeltaSeconds);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
