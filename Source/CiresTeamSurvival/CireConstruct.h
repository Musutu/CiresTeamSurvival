#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireSkillTuning.h"
#include "CireConstruct.generated.h"

class ACireHero;
class ACireGameMode;
class ACharacter;
class UBoxComponent;
class UStaticMeshComponent;

UCLASS()
class CIRESTEAMSURVIVAL_API ACireConstruct : public AActor
{
    GENERATED_BODY()
public:
    ACireConstruct();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual float TakeDamage(float Amount, const FDamageEvent& Event, AController* Instigator, AActor* Causer) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& Src) const override;

    // Width runs along local Y; depth runs along the casting direction (+X).
    // Ground placement is validated and snapped to a supporting world surface.
    static ACireConstruct* Spawn(ACireHero* Source, const FCireConstructSpec& Spec, FVector GroundCenter, FRotator Heading, const FString& Name);
    static bool ValidateSpec(const FCireConstructSpec& Spec, FString* Error = nullptr);
    static bool ValidatePlacement(ACireHero* Source, const FCireConstructSpec& Spec, FVector& GroundCenter, FRotator Heading, FString* Error = nullptr);
    static ACireConstruct* FindBlockingConstruct(AActor* Mover, FVector Destination);
    static ACireConstruct* FindBlockingConstruct(AActor* Mover, AActor* Destination);
    static void ClearAll(UWorld* World);
    static void ClearForActor(AActor* Actor);
    bool CanObserve(const AActor* Observer) const;
    bool CanBeDamagedBy(AActor* Source) const;
    bool IsWall() const { return ConstructSpec.Kind == ECireConstructKind::Wall; }
    bool IsProtection() const { return ConstructSpec.Kind == ECireConstructKind::Protection; }
    bool BlocksProjectilesFrom(AActor* Source) const;
    bool BlocksMovementOf(AActor* Mover) const;
    FString GetDisplayName() const { return AbilityName; }
    AActor* GetSourceActor() const;

    UPROPERTY(ReplicatedUsing=OnRep_Appearance) FCireConstructSpec ConstructSpec;
    UPROPERTY(Replicated) float Health = 250.f;
    UPROPERTY(Replicated) float MaxHealth = 250.f;
    UPROPERTY(Replicated) int32 OriginTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 OriginPhase = INDEX_NONE;
    UPROPERTY(Replicated) FString AbilityName;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> CollisionBox;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BodyMesh;
private:
    UPROPERTY() TObjectPtr<ACireHero> SourceHero;
    UPROPERTY() TObjectPtr<AActor> Presentation;
    TSet<TWeakObjectPtr<ACharacter>> IgnoringCharacters;
    float Age = 0;
    UFUNCTION() void OnRep_Appearance();
    void RefreshMovementExceptions();
};

namespace CireConstructs
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunConstructSmoke(ACireGameMode* Mode);
#endif
}
