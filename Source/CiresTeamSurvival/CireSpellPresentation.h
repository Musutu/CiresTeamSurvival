#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireAbilityShapes.h" // ability-vfx
#include "CireSpellPresentation.generated.h"

class UProceduralMeshComponent;
class UAudioComponent;
class ACireAreaEffect;
class UPointLightComponent;
class UNiagaraComponent; // fab-integration
struct FCireSpellMesh; // ability-vfx
struct FCireSoftMesh;  // ability-vfx

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
    virtual void EndPlay(const EEndPlayReason::Type Reason) override; // ability-vfx
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
    // ability-vfx: flat ground layer (telegraphs, shock rings, splash/scorch) in actor-local space.
    UPROPERTY(VisibleAnywhere) TObjectPtr<UProceduralMeshComponent> GroundMesh;
    enum class EMode : uint8 { Legacy, AreaFollow, Projectile, Lane, CasterFlare, SelfShock, TargetMark, Chain, Gather, Impact, VoidZone, Channel };
    EMode GetMode() const { return Mode; }
    // Hidden (no geometry, light or sound) until the caster's clip reaches its release frame.
    void SetStartDelay(float Seconds);
    float GetStartDelay() const { return StartDelay; }
    int32 GroundVertexCount() const { return GroundVertices.Num(); }
    const TArray<FVector>& GetGroundVertices() const { return GroundVertices; }
    bool LightGranted() const { return bLightGranted; }
    FBox2D GroundFillBounds() const { return LastFill; }
    FVector2D GroundArrowTip() const { return LastArrowTip; }
    int32 GroundChevrons() const { return LastChevrons; }
    bool IsHostileTelegraph() const { return bHostile; }
    float TelegraphLength() const { return LaneLength; }
    float TelegraphWidth() const { return LaneWidth; }
    bool IsFadingOut() const { return FadeOutAt>=0; }
    const FCireHitShape& GetShape() const { return Shape; }
    FVector2D VoidRadiiDrawn() const { return LastVoidRadii; }
    int32 VoidIconsDrawn() const { return LastVoidIcons; }
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
    // ability-vfx: new presentation modes (CireSpellVisualModes.cpp).
    EMode Mode = EMode::Legacy;
    FCireHitShape Shape;
    float StartDelay = 0, LaneLength = 0, LaneWidth = 0, GroundZ = -88.f, AreaActiveAge = -1, FadeOutAt = -1, ReleasedAge = -1;
    bool bChainHop = false, bHarmlessArea = false, bShapeResolved = false;
    FVector2D LastVoidRadii = FVector2D::ZeroVector; int32 LastVoidIcons = 0; FVector HopFrom = FVector::ZeroVector;
    bool bHostile = false, bSoundPending = false, bGroundProbed = false, bAreaPersistent = true, bShakeDone = false;
    TWeakObjectPtr<AActor> CastSource;
    FVector LaneOrigin = FVector::ZeroVector, LaneDirection = FVector::ForwardVector;
    FCireAreaSpec CachedArea;
    FBox2D LastFill = FBox2D(ForceInit);
    FVector2D LastArrowTip = FVector2D::ZeroVector;
    int32 LastChevrons = 0;
    TArray<FVector> GroundVertices;
    TArray<int32> GroundIndices;
    TArray<FLinearColor> GroundColors;
    void ClassifyCue();
    void ProbeGround();
    // fab-integration: optional Niagara overlay from the Fab VFX packs (CireFabVFX); procedural art always stays.
    TWeakObjectPtr<UNiagaraComponent> FabFX;
    bool bFabTried = false;
    void UpdateFabVFX();
public:
    bool HasFabVFX() const;
private:
    bool RebuildModes(FCireSpellMesh& M, FCireSoftMesh& Soft, float T, float Fade, float Expand);
    void RebuildGround(float T, float Fade);
    void DrawImpact(FCireSpellMesh& M, FCireSoftMesh& Soft, float T, float Fade);
    void DrawProjectile(FCireSpellMesh& M, FCireSoftMesh& Soft);
    void DrawAreaParticles(FCireSpellMesh& M, FCireSoftMesh& Soft, float Burst);
    bool TickModes(float DeltaSeconds);
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
    // ability-vfx: presentation delay applied to a cue so it appears on the caster clip's release frame.
    // ability-vfx: true when a visible follower paints this area's ground (its flat mesh is then not drawn).
    CIRESTEAMSURVIVAL_API bool IsAreaPresented(const ACireAreaEffect* Area);
    CIRESTEAMSURVIVAL_API float ReleaseDelay(UWorld* World, FName SkillId, ECireSpellCue Cue, FVector From);
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(UWorld* World);
#endif
}
