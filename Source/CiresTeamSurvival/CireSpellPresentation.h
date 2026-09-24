#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireSpellPresentation.generated.h"

class UProceduralMeshComponent;
class UAudioComponent;
class ACireAreaEffect;
class UPointLightComponent;

UENUM()
enum class ECireSpellCue : uint8 { Cast, Launch, Impact, Critical, Projectile, Wall, Protection };

// Local-only presentation. Gameplay and recipient selection remain authoritative.
// Never use these meshes as projectile collision or ground-area hit geometry.
UCLASS(NotBlueprintable, Transient)
class CIRESTEAMSURVIVAL_API ACireSpellVisual : public AActor
{
    GENERATED_BODY()
public:
    ACireSpellVisual();
    virtual void Tick(float DeltaSeconds) override;
    void Configure(FName Id, FVector From, FVector To, ECireSpellCue InCue, float InScale, bool bSound);
    void Follow(ACireAreaEffect* Area);
    void FollowActor(AActor* Actor, FName Id, ECireSpellCue InCue, FVector Bounds);
    void SetPreviewAge(float Seconds);
    void SetTint(FLinearColor Color);
    int32 VertexCount() const { return LastVertexCount; }
    bool HasMaterial() const;
    bool GeometryValid() const;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> SoftMesh;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UPointLightComponent> Light;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UAudioComponent> Audio;
private:
    FName Skill;
    FVector Start, End;
    FLinearColor Tint;
    ECireSpellCue Cue = ECireSpellCue::Cast;
    int32 Family = 0;
    float Age = 0, Duration = 1, Size = 1;
    int32 LastVertexCount = 0;
    int32 OriginPhase = INDEX_NONE;
    bool bPreview = false, bFollowArea = false;
    bool bFollowActor = false;
    bool bLightGranted = false;
    FVector FollowBounds = FVector(18.f);
    TWeakObjectPtr<AActor> FollowedActor;
    TWeakObjectPtr<ACireAreaEffect> FollowedArea;
    TArray<FVector> ScratchVertices;
    TArray<int32> ScratchIndices;
    TArray<FLinearColor> ScratchColors;
    TArray<FVector> SoftVertices,TrailPoints;
    TArray<int32> SoftIndices;
    TArray<FLinearColor> SoftColors;
    TArray<FVector2D> SoftUVs;
    void Rebuild();
    void StartSound();
};

namespace CireSpellPresentation
{
    // Call after realm-filtered client delivery, or locally for an observable
    // replicated projectile. Cast/launch sounds announce attempts; only Impact
    // produces a successful-hit sound. Do not call Impact for misses/dodges.
    CIRESTEAMSURVIVAL_API ACireSpellVisual* Play(UWorld* World, FName SkillId, FVector From, FVector To,
        ECireSpellCue Cue = ECireSpellCue::Cast, float Scale = 1.f, bool bSound = true);
    CIRESTEAMSURVIVAL_API ACireSpellVisual* FollowArea(ACireAreaEffect* Area);
    CIRESTEAMSURVIVAL_API ACireSpellVisual* AttachProjectile(AActor* Projectile, FName SkillId, float Radius);
    CIRESTEAMSURVIVAL_API ACireSpellVisual* AttachConstruct(AActor* Construct, bool bProtection, FVector HalfExtents);
    CIRESTEAMSURVIVAL_API bool IsSupported(FName SkillId);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(UWorld* World);
#endif
}
