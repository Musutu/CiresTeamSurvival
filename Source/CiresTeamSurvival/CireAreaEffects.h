#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireAreaEffects.generated.h"

class ACireHero;
class ACireGameMode;
class UProceduralMeshComponent;

UENUM()
enum class ECireAreaShape : uint8 { Circle, Cone, Line, Square, Custom };

// Coordinates are centimetres in the area's local XY plane. The cone/line extend
// along +X from the origin; circle/square are centred. Custom polygons are simple
// (no intersecting edges), may be concave, and contain at most 32 vertices.
USTRUCT()
struct CIRESTEAMSURVIVAL_API FCireAreaSpec
{
    GENERATED_BODY()
    UPROPERTY() ECireAreaShape Shape = ECireAreaShape::Circle;
    UPROPERTY() float Radius = 280.f;
    UPROPERTY() float Length = 650.f;
    UPROPERTY() float Width = 220.f;
    UPROPERTY() float ConeAngleDegrees = 75.f;
    UPROPERTY() TArray<FVector2D> CustomPolygon;
    UPROPERTY() float WarningSeconds = .65f;
    UPROPERTY() float DurationSeconds = 5.f;
    UPROPERTY() float TickInterval = .5f;
    UPROPERTY() float DamagePerSecond = 20.f;
    UPROPERTY() float BurstDamage = 60.f;
    // Applied to feet, not character centres, so a floor above is excluded.
    UPROPERTY() float VerticalTolerance = 70.f;
    UPROPERTY() FLinearColor Color = FLinearColor(.3f, .9f, .12f, .28f);
    UPROPERTY() FString AbilityName = TEXT("Poison Field");
    UPROPERTY() bool bPersistent = true;
    UPROPERTY() bool bPoison = true;
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACireAreaEffect : public AActor
{
    GENERATED_BODY()
public:
    ACireAreaEffect();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

    // Invalid specs/clients/noncombat calls fail without spawning an actor.
    static ACireAreaEffect* Spawn(AActor* Source, const FCireAreaSpec& Spec, FVector GroundCenter, FRotator Heading);
    static bool ValidateSpec(const FCireAreaSpec& Spec, FString* Error = nullptr);
    static bool ContainsPoint(const FCireAreaSpec& Spec, FVector Center, FRotator Heading, FVector Point);
    static TArray<FVector2D> BoundaryPoints(const FCireAreaSpec& Spec);
    static void ClearAll(UWorld* World);
    // Call on death/teleport: remove only this actor's memberships; destroy areas
    // they own. Other poison areas keep their own independent memberships.
    static void ClearForActor(AActor* Actor);
    bool CanObserve(const AActor* Observer) const;
    bool IsActive() const { return bActive; }
    int32 GetOccupantCount() const { return Occupants.Num(); }
    bool HasOccupant(AActor* Actor) const { return Occupants.Contains(Actor); }
    // ability-vfx: a following ACireSpellVisual paints the animated ground; the flat mesh stays as the fallback.
    bool bPresentationOwnsGround = false;

    UPROPERTY(ReplicatedUsing=OnRep_Appearance) FCireAreaSpec AreaSpec;
    UPROPERTY(ReplicatedUsing=OnRep_Appearance) bool bActive = false;
    UPROPERTY(Replicated) int32 OriginTeam = INDEX_NONE;
    UPROPERTY(Replicated) int32 OriginPhase = INDEX_NONE;
    UPROPERTY(Replicated) float StartServerTime = 0.f;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> GroundMesh;

private:
    UPROPERTY() TObjectPtr<AActor> SourceActor;
    TMap<TWeakObjectPtr<AActor>, float> Occupants;
    float Age = 0.f;
    float TickElapsed = 0.f;
    bool bBurstApplied = false;
    UFUNCTION() void OnRep_Appearance();
    void RebuildVisual();
    void RefreshOccupants(float ActiveDelta);
    void RemoveOccupant(AActor* Actor);
    void ClearOccupants();
    void DealAccumulatedDamage();
};

namespace CireAreaEffects
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunGeometrySmoke();
    CIRESTEAMSURVIVAL_API bool RunLifecycleSmoke(ACireGameMode* Mode);
#endif
}

