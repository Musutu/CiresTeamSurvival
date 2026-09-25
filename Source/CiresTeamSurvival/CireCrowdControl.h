#pragma once
// Crowd control, timed casts and the new execute skills (server authority; state replicated
// through CireBuffs records and ACireHero cast fields). Maths: Cires::CC in Rules/CiresRules.h.
//
//   Stun      no casting, attacking or moving (heroes and monsters). Bosses are immune.
//   Silence   no casting (passives still work). Bosses are immune.
//   Interrupt cancels the current cast; optional lockout of that spell's school.
//   Slow      existing SlowUntil fields (movement), "slowed" visual.
//   Heal cut  healing received ("heal_cut") and/or done ("heal_cut_done") reduced by X%.
//   Armor break  armor multiplied by 0.5 ("armor_broken"), heroes (items) and monsters (archetype).
//   PvP diminishing returns for stun and silence: full, half, quarter, then immune for 18s.
//
// Abilities apply their Abilities.json "effects" automatically when their hits land
// (OnAbilityHit); Shadow Step's void rift stuns inside the inner circle and slows the ring.
#include "CoreMinimal.h"

class AActor;
class ACireHero;
class ACireMonster;
class ACireGameMode;

namespace CireCrowdControl
{
    // ---- queries (valid on clients too; they read replicated buff records) ----
    CIRESTEAMSURVIVAL_API bool IsStunned(const AActor* Unit);
    CIRESTEAMSURVIVAL_API bool IsSilenced(const AActor* Unit);
    CIRESTEAMSURVIVAL_API float HealingReceivedCut(const AActor* Unit);   // 0..1
    CIRESTEAMSURVIVAL_API float HealingDoneCut(const AActor* Unit);       // 0..1
    CIRESTEAMSURVIVAL_API float HealingMultiplier(const AActor* Source, const AActor* Target);
    CIRESTEAMSURVIVAL_API float ArmorMultiplier(const AActor* Unit);      // 0.5 while armor is broken
    CIRESTEAMSURVIVAL_API bool IsCasting(const ACireHero* Hero);
    CIRESTEAMSURVIVAL_API float CastProgress(const ACireHero* Hero);      // 0..1, -1 when not casting

    // ---- server: apply control (returns the applied duration after DR / immunity) ----
    CIRESTEAMSURVIVAL_API float Stun(AActor* Target, float Seconds, AActor* Source);
    CIRESTEAMSURVIVAL_API float Silence(AActor* Target, float Seconds, AActor* Source);
    CIRESTEAMSURVIVAL_API float Slow(AActor* Target, float Seconds, AActor* Source);
    CIRESTEAMSURVIVAL_API void HealCut(AActor* Target, float Fraction, float Seconds, AActor* Source, bool bHealingDone = false);
    CIRESTEAMSURVIVAL_API void ArmorBreak(AActor* Target, float Seconds, AActor* Source);
    CIRESTEAMSURVIVAL_API bool Interrupt(AActor* Target, AActor* Source, float LockoutSeconds);

    // ---- casting (ACireHero::Cast / Tick hooks) ----
    /** True when handled: blocked by CC/lockout/another cast, or a timed cast was started. */
    CIRESTEAMSURVIVAL_API bool GateCast(ACireHero* Hero, int32 Slot, const FString& Id);
    CIRESTEAMSURVIVAL_API void CancelCast(ACireHero* Hero, const FString& Reason);
    CIRESTEAMSURVIVAL_API void TickHero(ACireHero* Hero, float DeltaSeconds);
    /** Fixtures: finish the current timed cast immediately (no-op when not casting). */
    CIRESTEAMSURVIVAL_API bool CompleteCastNow(ACireHero* Hero);
    /** True while the monster is stunned (its AI tick is skipped). */
    CIRESTEAMSURVIVAL_API bool TickMonster(ACireMonster* Monster, float DeltaSeconds);

    // ---- ability hooks ----
    CIRESTEAMSURVIVAL_API void OnAbilityHit(AActor* Source, AActor* Target, const FString& AbilityName);
    /** Executioner: turns a ready basic attack into an execute (monsters) / 30% max HP (champions). */
    CIRESTEAMSURVIVAL_API float ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API bool HandlesSkill(const FString& Id);                  // decimating_strike
    CIRESTEAMSURVIVAL_API bool CastSkill(ACireHero* Hero, int32 Slot, const FString& Id);
    CIRESTEAMSURVIVAL_API FString Description(const FString& Id);
    /** Void rift from the ability's Abilities.json void zones, centred on Center. Returns units hit. */
    CIRESTEAMSURVIVAL_API int32 VoidBurst(ACireHero* Source, FVector Center, const FString& AbilityId);
    CIRESTEAMSURVIVAL_API bool IsExecutionerReady(const ACireHero* Hero);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
