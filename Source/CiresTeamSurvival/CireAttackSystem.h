#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireCombatEvents.h"
#include "CireAttackSystem.generated.h"

class ACireHero;
class UStaticMeshComponent;
UCLASS()
class CIRESTEAMSURVIVAL_API ACireTargetProjectile : public AActor {
    GENERATED_BODY()
public:
    ACireTargetProjectile();
    virtual void BeginPlay() override;
    virtual void Tick(float Delta) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer,const AActor* ViewTarget,const FVector& SrcLocation) const override;
    static ACireTargetProjectile* Launch(ACireHero* Source,AActor* Target,float Damage,ECireHitOutcome Outcome);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
    UPROPERTY(Replicated) TObjectPtr<ACireHero> Attacker;
    UPROPERTY(Replicated) TObjectPtr<AActor> Victim;
    UPROPERTY(Replicated) int32 Style=1;
    UPROPERTY(Replicated) int32 TeamId=-1;
    UPROPERTY(Replicated) int32 LaunchedPhase=-1;
    float Amount=0;
    ECireHitOutcome Result=ECireHitOutcome::Hit;
    float Age=0;
    int32 AppliedStyle=-1;
};
namespace CireAttacks {
    float FeetZ(const AActor* Actor);
    float MissChance(const AActor* Source,const AActor* Target,bool bRanged);
    ECireHitOutcome Roll(const AActor* Source,const AActor* Target,bool bRanged);
    void Resolve(AActor* Source,AActor* Target,float Damage,ECireHitOutcome Result,const FString& Name);
    void Release(ACireHero* Source,AActor* Target,float Damage);
}
