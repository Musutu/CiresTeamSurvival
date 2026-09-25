#pragma once

#include "CoreMinimal.h"
#include "Engine/DamageEvents.h"
#include "CireSpellPresentation.h"
#include "CireCombatEvents.generated.h"

class AActor;
class ACireHero;
class ACireGameMode;

UENUM()
enum class ECireHitOutcome : uint8 { Hit, Miss, Dodge, Block, Resist }; // scaling-kits: shield BLOCK, aura RESIST

// Actor references are used only while preparing recipients on the server, then
// removed from the wire payload. Killing blows never depend on an actor NetGUID.
USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireCombatEvent
{
    GENERATED_BODY()

    UPROPERTY() TObjectPtr<AActor> Source = nullptr;
    UPROPERTY() TObjectPtr<AActor> Target = nullptr;
    UPROPERTY() FString SourceName;
    UPROPERTY() FString TargetName;
    UPROPERTY() FString AbilityName;
    UPROPERTY() FName SourceId = NAME_None;
    UPROPERTY() FName TargetId = NAME_None;
    UPROPERTY() float Amount = 0;
    UPROPERTY() float ServerTime = 0;
    UPROPERTY() float TimeSeconds = 0; // Set to local receive time by the controller.
    UPROPERTY() bool bHealing = false;
    UPROPERTY() bool bCritical = false;
    UPROPERTY() ECireHitOutcome Outcome = ECireHitOutcome::Hit;
    UPROPERTY() bool bLocalSource = false;
    UPROPERTY() bool bLocalTarget = false;
    UPROPERTY() uint32 Sequence = 0; // Monotonic receive order, unchanged by buffer pruning.
    UPROPERTY() FVector Location = FVector::ZeroVector;
    UPROPERTY() int32 SourceTeam = INDEX_NONE;
    UPROPERTY() int32 TargetTeam = INDEX_NONE;
};

// Local damage dispatch carries attribution through authoritative TakeDamage without
// temporarily mutating the attacker or relying on an unreliable visual-effect RPC.
struct FCireDamageEvent : public FDamageEvent
{
    static constexpr int32 CireClassID = 0x43495245;
    FString AbilityName;
    bool bCritical = false;

    explicit FCireDamageEvent(const FString& InAbilityName,bool InCritical=false) : AbilityName(InAbilityName),bCritical(InCritical) {}
    virtual int32 GetTypeID() const override { return CireClassID; }
    virtual bool IsOfType(int32 InID) const override { return InID == CireClassID || FDamageEvent::IsOfType(InID); }
};

namespace CireCombat
{
    CIRESTEAMSURVIVAL_API float ApplyDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName,bool bCritical=false);
    CIRESTEAMSURVIVAL_API float ApplyStrike(AActor* Source,AActor* Target,float Amount,const FString& AbilityName,bool bCanCrit=true);
    CIRESTEAMSURVIVAL_API bool IsAlive(const AActor* Actor);
    CIRESTEAMSURVIVAL_API bool AreHostile(AActor* Source,AActor* Target);
    CIRESTEAMSURVIVAL_API int32 TeamOf(const AActor* Actor);
    CIRESTEAMSURVIVAL_API void PlayCue(AActor* Source,AActor* Target,FName SkillId,FVector From,FVector To,ECireSpellCue Cue,float Scale=1.f,bool bSound=true);
    CIRESTEAMSURVIVAL_API float ApplyHealing(ACireHero* Source, ACireHero* Target, float Amount, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API void BroadcastDamage(AActor* Source, AActor* Target, float AppliedAmount, const FDamageEvent& DamageEvent);
    CIRESTEAMSURVIVAL_API void BroadcastHealing(ACireHero* Source, ACireHero* Target, float AppliedAmount, const FString& AbilityName);
    CIRESTEAMSURVIVAL_API void BroadcastAvoidance(AActor* Source, AActor* Target, ECireHitOutcome Outcome, const FString& AbilityName, float PreventedAmount = 0.f);
    /** Floating text / log word for an outcome ("Miss", "Dodge", "Block", "Resist"). */
    CIRESTEAMSURVIVAL_API FString OutcomeText(ECireHitOutcome Outcome);
    CIRESTEAMSURVIVAL_API void AppendReceivedEvent(TArray<FCireCombatEvent>& Buffer, uint32& Sequence, const FCireCombatEvent& Event, float Now);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunTelemetrySmoke(ACireGameMode* Mode);
#endif
}
