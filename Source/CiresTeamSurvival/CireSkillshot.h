#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "CireSkillTuning.h"
#include "CireSkillshot.generated.h"

class ACireGameMode;
class UStaticMeshComponent;
class UPrimitiveComponent;

UCLASS()
class CIRESTEAMSURVIVAL_API ACireSkillshot : public AActor
{
    GENERATED_BODY()
public:
    ACireSkillshot();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& Src) const override;
    // Aim determines a fixed horizontal direction at cast time; no target tracking.
    static ACireSkillshot* Spawn(AActor* Source, const FCireSkillshotSpec& Spec, FVector AimPoint, const FString& Name);
    static bool ValidateSpec(const FCireSkillshotSpec& Spec, FString* Error = nullptr);
    static void ClearAll(UWorld* World);
    static void ClearForActor(AActor* Actor);
    bool CanObserve(const AActor* Observer) const;
    AActor* GetSourceActor() const { return SourceActor; }
    int32 GetHitCount() const { return HitCount; }
    int32 GetReflectionCount() const { return ReflectionCount; }

    UPROPERTY(ReplicatedUsing=OnRep_Appearance) FCireSkillshotSpec ShotSpec;
    UPROPERTY(Replicated) FVector_NetQuantize Velocity;
    UPROPERTY(Replicated) int32 OriginTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 OriginPhase = INDEX_NONE;
    UPROPERTY(Replicated) float StartServerTime = 0;
    UPROPERTY(Replicated) FString AbilityName;
    UPROPERTY(ReplicatedUsing=OnRep_Appearance) bool bReleased = false;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> FlightMesh;
private:
    UPROPERTY() TObjectPtr<AActor> SourceActor;
    UPROPERTY() TObjectPtr<AActor> Presentation;
    TMap<TWeakObjectPtr<AActor>, int32> HitGeneration;
    TSet<TWeakObjectPtr<UPrimitiveComponent>> PiercedWorldComponents;
    float Age = 0;
    float DistanceTravelled = 0;
    int32 HitCount = 0;
    int32 ReflectionCount = 0;
    UFUNCTION() void OnRep_Appearance();
    void Travel(float Distance);
};

namespace CireSkillshots
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSkillshotSmoke(ACireGameMode* Mode);
#endif
}
