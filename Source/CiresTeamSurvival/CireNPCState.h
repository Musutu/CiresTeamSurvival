#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CireNPCArchetypes.h"
#include "CireNPCState.generated.h"

class ACireHero;
class ACireMonster;
class UStaticMeshComponent;

// Why a monster's target changed. Published with every aggro change.
UENUM(BlueprintType)
enum class ECireAggroReason : uint8
{
    Acquired,     // first target after being idle / pulled
    Pulled,       // someone exceeded 110% (melee) / 130% (ranged) of the current target's threat
    Taunted,      // a champion taunt forced the target
    TauntExpired, // forced target ended and threat picked a different champion
    TargetLost,   // target died, left range or despawned; next highest took over
    Reset         // leash, phase change or death cleared all threat
};

// One replicated threat-table row. Rows are sorted by threat, highest first.
USTRUCT(BlueprintType)
struct CIRESTEAMSURVIVAL_API FCireThreatEntry
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<ACireHero> Hero = nullptr;
    UPROPERTY() float Threat = 0.f;
};

USTRUCT(BlueprintType)
struct CIRESTEAMSURVIVAL_API FCireAggroState
{
    GENERATED_BODY()
    UPROPERTY() TObjectPtr<ACireHero> Target = nullptr;
    UPROPERTY() TObjectPtr<ACireHero> Previous = nullptr;
    UPROPERTY() ECireAggroReason Reason = ECireAggroReason::Reset;
    UPROPERTY() uint16 Serial = 0;
};

// Broadcast on the server when the change happens and on clients when the
// replicated aggro state arrives. Monster/heroes may be null on clients that
// do not have the actor relevant.
struct FCireAggroEvent
{
    TWeakObjectPtr<ACireMonster> Monster;
    TWeakObjectPtr<ACireHero> NewTarget;
    TWeakObjectPtr<ACireHero> OldTarget;
    ECireAggroReason Reason = ECireAggroReason::Reset;
};
DECLARE_MULTICAST_DELEGATE_OneParam(FCireAggroChanged, const FCireAggroEvent&);

// UI-facing ability description (static data; no replication needed).
struct FCireNPCAbilityInfo
{
    FName Id;
    FString Name;
    FString Description;
    FString TypeLabel;      // Attack / Cast / Cast (interruptible) / Telegraph / Buff / Taunt / Enrage / Movement
    ECireNPCAbilityKind Kind = ECireNPCAbilityKind::Melee;
    float Cooldown = 0.f;
    float CastTime = 0.f;
    bool bInterruptible = false;
    bool bBasic = false;
};

struct FCireNPCCastInfo
{
    bool bCasting = false;
    FName AbilityId;
    FString Name;
    float Progress = 0.f;   // 0..1
    float Remaining = 0.f;  // seconds
    bool bInterruptible = false;
};

// Status flag bits replicated in UCireNPCState::StatusFlags.
namespace CireNPCStatus
{
    constexpr uint8 Enraged=1<<0, Rallied=1<<1, ShieldWall=1<<2, Guarded=1<<3, Provoking=1<<4, Charging=1<<5;
}

// Per-monster role/boss/threat state. Created as a default subobject of every
// ACireMonster; replicated so clients can draw role icons, boss frames, cast
// bars, ability insight and the threat meter without extra RPCs.
UCLASS()
class CIRESTEAMSURVIVAL_API UCireNPCState : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireNPCState();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ---- replicated ----
    UPROPERTY(ReplicatedUsing=OnRep_Archetype) FName ArchetypeId;
    UPROPERTY(Replicated) ECireNPCRole Role = ECireNPCRole::Bruiser;
    UPROPERTY(Replicated) ECireNPCClass Classification = ECireNPCClass::Normal;
    UPROPERTY(Replicated) uint8 StatusFlags = 0;
    UPROPERTY(Replicated) FName CastAbilityId;
    UPROPERTY(Replicated) bool bCastInterruptible = false;
    UPROPERTY(Replicated) TArray<FCireThreatEntry> ThreatTable;
    UPROPERTY(ReplicatedUsing=OnRep_Aggro) FCireAggroState Aggro;
    // monster-races: rank colour, palette reskin and this monster's drawn skills (CireRaces.h).
    UPROPERTY(ReplicatedUsing=OnRep_Look) uint8 Rank = 0;          // ECireNPCRank
    UPROPERTY(ReplicatedUsing=OnRep_Look) uint8 PaletteIndex = 0;  // race palette variant
    UPROPERTY(Replicated) uint8 SkillTier = 0;                     // 0 none, 1..3 = I..III
    UPROPERTY(Replicated) bool bLoadoutSet = false;                // false = every authored ability (legacy/tests)
    UPROPERTY(Replicated) TArray<FName> Loadout;                   // active non-basic abilities when bLoadoutSet
    bool IsAbilityActive(FName AbilityId) const;

    // ---- read API (valid on server and clients) ----
    const FCireNPCArchetype* Archetype() const;
    bool HasStatus(uint8 Flag) const { return (StatusFlags&Flag)!=0; }
    TArray<FCireNPCAbilityInfo> Abilities() const;
    FCireNPCCastInfo CastInfo() const;
    // Raw threat relative to the current target (target = 100).
    float ThreatPercent(const ACireHero* Hero) const;
    // Progress toward pulling aggro (100 = pulls): uses 110% in melee range, 130% outside.
    float PullPercent(const ACireHero* Hero) const;
    float ThreatOf(const ACireHero* Hero) const;
    static FCireAggroChanged& OnAggroChanged();
    static FString DescribeAggro(const FCireAggroEvent& Event);   // "Ember 2 gained aggro on Gravemaw"
    static FString DescribeFocus(const ACireMonster* Monster);   // "Gravemaw is focusing Ember 2"

    // ---- server runtime (not replicated) ----
    TMap<FName,float> ReadyAt;
    TMap<TWeakObjectPtr<ACireHero>,float> LastThreatAt;
    TMap<TWeakObjectPtr<ACireHero>,float> ProvokedUntil;
    TWeakObjectPtr<ACireMonster> Guardian;
    float GuardUntil=0, GuardFraction=0;
    float RallyUntil=0, RallyBonus=0;
    float ShieldWallUntil=0, ShieldWallReduction=0;
    float EnrageBonus=0;
    bool bEnraged=false;
    bool bForcedLastSelect=false;
    float ThreatPublishAt=0;
    float KiteUntil=0, KiteReadyAt=0;
    FVector DashTarget=FVector::ZeroVector;
    float DashUntil=0, DashSpeed=0;
    FName DashAbility;
    void ResetRuntime();
    void SetAggro(ACireHero* NewTarget,ACireHero* OldTarget,ECireAggroReason Reason);
    void PublishThreat(bool bForce);
    void RefreshStatusFlags(float Now);

    // Cosmetic body; applied on clients from OnRep and on non-dedicated servers.
    void ApplyVisuals();
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> VisualParts;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> StaticBody;
    FName AppliedVisualArchetype;
private:
    UFUNCTION() void OnRep_Archetype();
    UFUNCTION() void OnRep_Aggro();
    UFUNCTION() void OnRep_Look(); // monster-races
};
