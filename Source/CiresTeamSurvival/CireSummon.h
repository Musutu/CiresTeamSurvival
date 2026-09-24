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
    UPROPERTY(Replicated) TObjectPtr<ACireHero> OwnerHero;
    UPROPERTY(Replicated) FCireSummonSpec SummonSpec;
    UPROPERTY(Replicated) bool bCommandable = false;
    UPROPERTY(Replicated) ECireSummonCommand CurrentCommand = ECireSummonCommand::Follow;
    UPROPERTY(Replicated) FVector_NetQuantize MoveDestination;
    UPROPERTY(Replicated) float ExpiresServerTime = 0;
    UPROPERTY(Replicated) int32 OriginPhase = INDEX_NONE;
private:
    float Age = 0;
};

namespace CireSummons
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSummonSmoke(ACireGameMode* Mode);
#endif
}
