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
    // new-champions: any combat unit may own a construct (champions, their summons, monsters).
    // Tech constructs (turret/trap/pylon/skitter) replace the owner's oldest of the same recipe at its OwnerLimit.
    static ACireConstruct* SpawnFor(AActor* Source, const FCireConstructSpec& Spec, FVector GroundCenter, FRotator Heading, const FString& Name);
    static bool ValidateSpec(const FCireConstructSpec& Spec, FString* Error = nullptr);
    static bool ValidatePlacement(ACireHero* Source, const FCireConstructSpec& Spec, FVector& GroundCenter, FRotator Heading, FString* Error = nullptr);
    static bool ValidatePlacementFor(AActor* Source, const FCireConstructSpec& Spec, FVector& GroundCenter, FRotator Heading, FString* Error = nullptr);
    static ACireConstruct* FindBlockingConstruct(AActor* Mover, FVector Destination);
    static ACireConstruct* FindBlockingConstruct(AActor* Mover, AActor* Destination);
    static void ClearAll(UWorld* World);
    static void ClearForActor(AActor* Actor);
    bool CanObserve(const AActor* Observer) const;
    bool CanBeDamagedBy(AActor* Source) const;
    bool IsWall() const { return ConstructSpec.Kind == ECireConstructKind::Wall; }
    bool IsProtection() const { return ConstructSpec.Kind == ECireConstructKind::Protection; }
    bool IsTech() const { return ConstructSpec.IsTech(); }
    bool BlocksProjectilesFrom(AActor* Source) const;
    bool BlocksMovementOf(AActor* Mover) const;
    FString GetDisplayName() const { return AbilityName; }
    AActor* GetSourceActor() const;
    float GetAge() const { return Age; }

    UPROPERTY(ReplicatedUsing=OnRep_Appearance) FCireConstructSpec ConstructSpec;
    UPROPERTY(Replicated) float Health = 250.f;
    UPROPERTY(Replicated) float MaxHealth = 250.f;
    UPROPERTY(Replicated) int32 OriginTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 OriginPhase = INDEX_NONE;
    UPROPERTY(Replicated) FString AbilityName;
    // new-champions: a monster-owned construct is hostile to the champions of its lane.
    UPROPERTY(Replicated) bool bMonsterOwned = false;
    // new-champions: turrets fire faster while overcharged (server time).
    UPROPERTY(Replicated) float OverchargedUntil = 0.f;
    UPROPERTY(Replicated) uint8 ShotSerial = 0;          // turret bolts fired (client recoil pulse)
    // fix/summons: server time the construct expires (summons-bar duration timer on every client).
    UPROPERTY(Replicated) float ExpiresServerTime = 0.f;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> CollisionBox;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> BodyMesh;
    // new-champions: tech construct presentation (energy core, crystal, ring); hidden for walls.
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> CoreMesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> CrownMesh;
    // items-v2: absorb shield from the owner's Artificer's Heartforge (server only).
    float ItemShield = 0.f;
    // Server-only tech state (CireTechConstructs::TickConstruct).
    float TechTimer = 0.f;
    bool bTriggered = false;
    TWeakObjectPtr<AActor> SeekTarget;
    // Client-only presentation state.
    uint8 SeenShotSerial = 0;
    float Recoil = 0.f;
private:
    UPROPERTY() TObjectPtr<AActor> SourceUnit;
    UPROPERTY() TObjectPtr<AActor> Presentation;
    TSet<TWeakObjectPtr<ACharacter>> IgnoringCharacters;
    float Age = 0;
    float VisualTime = 0;
    UFUNCTION() void OnRep_Appearance();
    void RefreshMovementExceptions();
    void UpdateTechVisual(float DeltaSeconds);
};

namespace CireConstructs
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunConstructSmoke(ACireGameMode* Mode);
#endif
}
