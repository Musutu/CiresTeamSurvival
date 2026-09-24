#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CireBuffs.generated.h"

// aura-vfx: authoritative "which named effect is on this unit" record.
//
// Gameplay rules (damage reduction, taunt, slow...) keep their existing fields
// (ShieldUntil, TauntUntil, SlowUntil, NPC status flags). This component only
// records the *identity* of the effect that produced them so every client can
// draw a signature visual for THAT skill. It is replicated with the owning unit,
// so realm relevancy (IsNetRelevantFor) already hides opponents' records.
// Item actives and future skills call CireBuffs::Apply with their own id and add
// a row to Content/Data/BuffVisuals.json; no presentation code is required.

USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireBuffEntry
{
    GENERATED_BODY()
    UPROPERTY() FName Id;
    UPROPERTY() float StartTime=0;      // server world seconds
    UPROPERTY() float EndTime=0;        // server world seconds; <=StartTime means "until removed"
    UPROPERTY() uint8 Stacks=1;
    UPROPERTY() int8 SourceTeam=-1;
    UPROPERTY() TObjectPtr<AActor> Source=nullptr; // optional link (taunter, provoker); may be null on clients
    int32 Phase=INDEX_NONE;             // server only: records never survive a phase change
};

UCLASS(ClassGroup=(Cire))
class CIRESTEAMSURVIVAL_API UCireBuffState : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireBuffState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Tick) override;
    UPROPERTY(Replicated) TArray<FCireBuffEntry> Buffs;
    /** Server: drop expired, phase-stale or dead-owner records. Returns the number removed. */
    int32 Prune(float Now,int32 Phase,bool bOwnerAlive);
    const FCireBuffEntry* Find(FName Id) const;
};

namespace CireBuffs
{
    constexpr int32 MaxEntriesPerUnit=12;
    /** Server only. Refreshes (max end time) an existing record with the same id. */
    CIRESTEAMSURVIVAL_API bool Apply(AActor* Target,FName Id,float DurationSeconds,AActor* Source=nullptr,int32 Stacks=1);
    CIRESTEAMSURVIVAL_API bool Remove(AActor* Target,FName Id);
    CIRESTEAMSURVIVAL_API void ClearAll(AActor* Target);
    CIRESTEAMSURVIVAL_API UCireBuffState* Get(const AActor* Unit);
    /** Active on this machine's clock (server time on clients). */
    CIRESTEAMSURVIVAL_API bool IsActive(const AActor* Unit,FName Id);
    CIRESTEAMSURVIVAL_API float ServerNow(const UWorld* World);
    /**
     * Every buff/debuff/aura id the game can currently produce, whether recorded by
     * Apply or derived from existing replicated state (NPC flags, slow, poison...).
     * Native tests require a BuffVisuals.json row for each.
     */
    CIRESTEAMSURVIVAL_API const TArray<FName>& KnownIds();
}
