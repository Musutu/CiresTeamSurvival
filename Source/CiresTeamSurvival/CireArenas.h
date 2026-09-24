#pragma once
// Randomised PvP arenas (Docs/Arenas.md).
//
// Content/Data/Arenas.json holds a pool of themed arena maps that all share one footprint far from the
// two PvE realms. Every arena phase the server picks one at random (never the previous one), replicates
// the choice through ACireGameState::ArenaIndex, and every peer builds that arena's actors locally during
// the prep minute (hidden), shows them for the fight and destroys them in recovery. Nothing here is
// replicated beyond the index: geometry is deterministic on every peer, like ACireWorld.
//
// Blockers carry an invisible box/cylinder collision proxy that matches their authored footprint exactly,
// so the data-only fairness checks (symmetry, paths, spawn clearance) describe the runtime collision.
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "CireArenas.generated.h"

class ACireGameMode;
class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;
class UInstancedStaticMeshComponent;

namespace CireArenas
{
    enum class EFit : uint8 { Footprint, Uniform, None };
    enum class EShape : uint8 { Box, Round };

    /** A reusable art slot: candidate assets (first that loads wins) and a guaranteed engine-shape fallback. */
    struct FSlot
    {
        FName Id;
        TArray<FString> Candidates;       // object paths; "a|b|c" places several parts that share one pivot
        FString Fallback;                 // engine basic shape used when no candidate loads
        FString FallbackMaterial;         // material for the fallback shape
        TMap<int32, FString> Materials;   // per-section overrides applied to whichever mesh resolves
        FVector Footprint = FVector(100); // authored size (X depth, Y width, Z height) in cm at scale 1
        EFit Fit = EFit::Footprint;
        EShape Shape = EShape::Box;
        FVector Offset = FVector::ZeroVector;
        float Yaw = 0.f;                  // mesh-local yaw correction
        bool bShadow = true;
        bool bEssential = false;          // an arena whose essential slot resolves to no real asset leaves the rotation
        float CullDistance = 0.f;         // 0 = never culled
        float Spin = 0.f;                 // degrees per second around local X (windmill sails)
        bool bWPO = false;                // material animates vertices (wind): keep WPO evaluation on
        bool bHidden = false;             // collision-only (e.g. the legs of an arch whose span you walk under)
        float WPODistance = 5000.f;       // beyond this the wind animation stops (cheaper shadows)
    };

    struct FPiece
    {
        FName Slot;
        FVector Location = FVector::ZeroVector; // arena-local cm (X toward Dusk, Y across), Z added to ground
        float Yaw = 0.f;
        FVector Scale = FVector::OneVector;
        bool bBlocker = false;                  // collision + line-of-sight
    };

    struct FScatter
    {
        FName Slot;
        FBox2D Region = FBox2D(ForceInit);
        int32 Count = 0;
        int32 Seed = 1;
        FVector2D ScaleRange = FVector2D(1, 1);
        float ZJitter = 0.f;
        float Z = 0.f;                          // base height (motes drift in a volume above the floor)
        bool bOutsideBounds = false;            // skip points inside the playable bounds (+margin)
        float BoundsMargin = 0.f;
        float Clearance = 0.f;                  // keep this far from blockers and spawns
        TArray<FBox2D> Exclude;
        bool bRandomYaw = true;
        bool bTilt = false;                     // small random lean (stalks, kelp)
    };

    struct FPointLight { FVector Location = FVector::ZeroVector; FLinearColor Color = FLinearColor::White; float Intensity = 5000, Radius = 1000; bool bShadows = false; };

    struct FLighting
    {
        float SunPitch = -35.f, SunYaw = 90.f, SunIntensity = 6.f, SunSourceAngle = .6f;
        FLinearColor SunColor = FLinearColor(1, .95f, .88f);
        bool bLightShafts = false; float ShaftBloomScale = .2f, ShaftThreshold = 8.f;
        float VolumetricScattering = 1.f;
        FString SkyMaterial;                    // IsSky dome material (unlit, samples by camera direction)
        FLinearColor SkyTint = FLinearColor::White; float SkyBrightness = 1.f, SkyYaw = 0.f;
        FLinearColor SkyHaze = FLinearColor(1, 1, 1); float SkyHazeStrength = 0.f; // blends the dome's horizon into the fog
        float SkyLightIntensity = 1.f; FLinearColor SkyLightColor = FLinearColor::White;
        float FogDensity = .01f, FogFalloff = .2f, FogStart = 0.f, FogHeight = 0.f, FogMaxOpacity = 1.f;
        FLinearColor FogColor = FLinearColor(.4f, .45f, .5f);
        bool bVolumetricFog = true; float VolumetricDistribution = .5f, VolumetricExtinction = 1.f;
        FLinearColor VolumetricAlbedo = FLinearColor::White;
        float ExposureBias = 0.f, Saturation = 1.f, Contrast = 1.f, Temperature = 6500.f, Bloom = .675f, Vignette = .4f;
        FLinearColor Gain = FLinearColor::White, ShadowTint = FLinearColor::White;
        FString LightFunction; float LightFunctionScale = 1000.f;
        TArray<FPointLight> Lights;
    };

    struct FArena
    {
        FName Id;
        FString Name, Theme, Subtitle;
        bool bEnabled = true, bFallbackOnly = false;
        float Weight = 1.f;
        FVector2D HalfExtents = FVector2D(2600, 2000);
        TArray<FVector2D> Spawns[2];
        FName Ground, Underlay;                 // world-aligned surface slots (materials)
        float GroundSize = 40000.f;             // visual ground square edge (cm)
        FString BoundaryWall;                   // optional visible boundary treatment note (pieces do the art)
        TArray<FPiece> Pieces;
        TArray<FScatter> Scatter;
        FLighting Lighting;
        FName Ambience;                         // AudioAmbience.json district id
        FString Music;                          // AudioMusic track override for the arena state
        FLinearColor MinimapGround = FLinearColor(.08f, .07f, .05f), MinimapBlocker = FLinearColor(.5f, .45f, .35f), MinimapAccent = FLinearColor(.86f, .66f, .3f);
        TArray<FName> Essential;                // resolved from slots flagged essential
    };

    struct FPool
    {
        bool bValid = false;
        FVector Origin = FVector(0, 60000, 0);
        TMap<FName, FSlot> Slots;
        TArray<FArena> Arenas;                  // JSON order, then the built-in legacy court (fallback only)
        TArray<FString> Errors;
        int32 FallbackIndex = INDEX_NONE;
    };

    /** The arena pool (loaded once; bReload re-reads Content/Data/Arenas.json). */
    CIRESTEAMSURVIVAL_API const FPool& Pool(bool bReload = false);
    CIRESTEAMSURVIVAL_API const FArena* Get(int32 Index);
    CIRESTEAMSURVIVAL_API FVector Origin();
    /** Validation errors for one arena (empty = valid). Data-only: symmetry, bounds, spawns, paths, slots. */
    CIRESTEAMSURVIVAL_API TArray<FString> Validate(const FArena& Arena, const FPool& Pool);
    /** Arena indices the server may pick: enabled, valid and with their essential assets present. */
    CIRESTEAMSURVIVAL_API TArray<int32> Rotation();
    /** Random pick from the rotation, never equal to Previous when another arena exists; falls back to the legacy court. */
    CIRESTEAMSURVIVAL_API int32 PickNext(int32 Previous, FRandomStream* Stream = nullptr);

    // ---- geometry helpers (all world space, for the arena shown by ArenaIndex) ----
    CIRESTEAMSURVIVAL_API FVector SpawnLocation(int32 Index, int32 Team, int32 Slot, float Z = 110.f);
    CIRESTEAMSURVIVAL_API FVector Center(const UWorld* World);
    CIRESTEAMSURVIVAL_API FVector2D HalfExtents(const UWorld* World);
    /** True when a world point lies within the current arena's playable bounds (minus Margin). */
    CIRESTEAMSURVIVAL_API bool InBounds(const UWorld* World, const FVector& Point, float Margin = 0.f);
    /** The arena whose index the game state currently carries (valid on clients too). */
    CIRESTEAMSURVIVAL_API const FArena* Current(const UWorld* World);
    CIRESTEAMSURVIVAL_API int32 CurrentIndex(const UWorld* World);
    CIRESTEAMSURVIVAL_API FString DisplayName(int32 Index);
    /** Ambience district / music track for the arena being fought in (NAME_None / "" when not in an arena). */
    CIRESTEAMSURVIVAL_API FName ActiveAmbience(const UWorld* World);
    CIRESTEAMSURVIVAL_API FString ActiveMusic(const UWorld* World);

    /** Blocker footprints for the minimap and path tests: oriented boxes (round = circle radius in Extent.X). */
    struct FFootprint { FVector2D Center = FVector2D::ZeroVector, Extent = FVector2D::ZeroVector; float Yaw = 0.f; bool bRound = false; float Height = 0.f; };
    CIRESTEAMSURVIVAL_API TArray<FFootprint> Footprints(const FArena& Arena, const FPool& Pool);
    /** 2D grid walkability test for a capsule of Radius: every spawn reaches every enemy spawn. */
    CIRESTEAMSURVIVAL_API bool SpawnsConnected(const FArena& Arena, const FPool& Pool, float Radius, FString* OutError = nullptr, float* OutReachableFraction = nullptr);

    // ---- runtime ----
    /** Server, prep start: pick the next arena (never the previous one) and replicate it so peers prebuild it hidden. */
    CIRESTEAMSURVIVAL_API void ServerPrepare(ACireGameMode* Mode);
    /** Server, arena start: make sure an arena was picked this cycle and is built before anyone is teleported. */
    CIRESTEAMSURVIVAL_API void ServerBegin(ACireGameMode* Mode);
    /** Build/show/clear the local arena to match the game state now (server calls this before teleporting). */
    CIRESTEAMSURVIVAL_API void Sync(UWorld* World);
    /** Development: build a specific arena and show it (galleries, tests). Index < 0 clears. */
    CIRESTEAMSURVIVAL_API void Force(UWorld* World, int32 Index, bool bVisible);
    CIRESTEAMSURVIVAL_API void ReleaseForce(UWorld* World);
    CIRESTEAMSURVIVAL_API class ACireArenaStage* Stage(const UWorld* World);

#if !UE_BUILD_SHIPPING
    /** Native checks: data validation, symmetry, paths, spawn clearance, selection, build and cleanup. */
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}

/** The built arena: all components live on this one actor, which is destroyed on cleanup. */
UCLASS(NotPlaceable, Transient)
class CIRESTEAMSURVIVAL_API ACireArenaStage : public AActor
{
    GENERATED_BODY()
public:
    ACireArenaStage();
    virtual void Tick(float DeltaSeconds) override;
    int32 ArenaIndex = INDEX_NONE;
    bool bShown = false;
    bool bVisuals = true;             // false on dedicated servers: collision only
    int32 BlockerCount = 0, InstanceCount = 0, FallbackSlots = 0;
    TArray<FName> MissingSlots;
    void BuildArena(int32 Index, bool bWithVisuals);
    void SetShown(bool bShow);
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Spinners;
    TArray<float> SpinRates;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UActorComponent>> Lighting;
    void BuildLighting();
    void ClearLighting();
};

/** Watches the replicated phase/arena index on every peer and keeps the local stage in step. */
UCLASS()
class CIRESTEAMSURVIVAL_API UCireArenaSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCireArenaSubsystem, STATGROUP_Tickables); }
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Deinitialize() override;
    void Apply(int32 Index, bool bShow);
    void Clear();
    ACireArenaStage* GetStage() const { return Stage.Get(); }
    int32 ForcedIndex = INDEX_NONE;
    bool bForced = false, bForcedShow = false;
    int32 BuildCount = 0, ClearCount = 0;
private:
    TWeakObjectPtr<ACireArenaStage> Stage;
    bool bTownHidden = false;
    TArray<TWeakObjectPtr<class USceneComponent>> HiddenTown;
    TArray<TWeakObjectPtr<AActor>> HiddenTownActors; // the town world is hidden too: the IsSky dome cannot occlude it
    void HideTown(bool bHide);
};
