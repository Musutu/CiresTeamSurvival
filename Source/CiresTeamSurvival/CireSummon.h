#pragma once
#include "CoreMinimal.h"
#include "CireGame.h"
#include "CireSkillTuning.h"
#include "CireSummon.generated.h"

UENUM()
enum class ECireSummonCommand : uint8 { Follow, Move, Attack, Hold };

// A temporary allied combat unit. It is deliberately absent from the player
// roster, never revives, and never counts as a team elimination or life loss.
UCLASS()
class CIRESTEAMSURVIVAL_API ACireSummon : public ACireHero
{
    GENERATED_BODY()
public:
    ACireSummon();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void FellOutOfWorld(const UDamageType& DamageType) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    static TArray<ACireSummon*> SpawnGroup(ACireHero* Source, const FCireSummonSpec& Spec, AActor* Target, FVector Point);
    static bool ValidateSpec(const FCireSummonSpec& Spec);
    static void ClearAll(UWorld* World);
    static void ClearForActor(AActor* Actor);
    bool Command(ECireSummonCommand NewCommand, FVector Point, AActor* NewTarget = nullptr);
    ACireHero* GetOwnerHero() const { return OwnerHero; }
    // fix/summons: autonomous engagement (Docs/Pets.md "Summons and constructs"). Every fighting summon
    // keeps an ordered target, then assists the owner's target, then defends the owner, itself and the
    // owner's other summons, then engages any hostile near itself or the owner (never a neutral pack).
    // Hold fights only what is in reach; Move ignores fights until it arrives. Server only.
    virtual AActor* ChooseFightTarget();
    /** Extra reach against the current target's body (capsule radius), so big units can be hit. */
    float TargetReachBonus() const;
    /** Ability DB id that created this unit (HUD icon / tooltip); empty falls back by kind. */
    UPROPERTY(Replicated) FName SourceSkill;
    /** Engagement radii (cm): assist the owner's target / defend the owner / guard around owner and self. */
    static constexpr float AssistRange = 2000.f;
    static constexpr float DefendRadius = 1100.f;
    static constexpr float GuardRadius = 900.f;
    UPROPERTY(Replicated) TObjectPtr<ACireHero> OwnerHero;
    UPROPERTY(Replicated) FCireSummonSpec SummonSpec;
    UPROPERTY(Replicated) bool bCommandable = false;
    UPROPERTY(Replicated) ECireSummonCommand CurrentCommand = ECireSummonCommand::Follow;
    UPROPERTY(Replicated) FVector_NetQuantize MoveDestination;
    UPROPERTY(Replicated) float ExpiresServerTime = 0;
    UPROPERTY(Replicated) int32 OriginPhase = INDEX_NONE;
private:
    float Age = 0;
    TWeakObjectPtr<AActor> LastAttacker; // fix/summons: retaliation memory (server)
    float LastAttackedAt = -100.f;
};

namespace CireSummons
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSummonSmoke(ACireGameMode* Mode);
    /** fix/summons: every fighting summon/construct deals damage within N s; targetability, threat, scaling, summons bar. */
    CIRESTEAMSURVIVAL_API bool RunEngagementSmoke(ACireGameMode* Mode);
#endif
}
