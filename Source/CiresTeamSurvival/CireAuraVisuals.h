#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "CireAuraVisuals.generated.h"

class UProceduralMeshComponent;
class UPointLightComponent;
class UMaterialInstanceDynamic;
class ACireGameMode;

// aura-vfx: data-driven signature visuals for buffs, debuffs, auras and stances.
// Everything here is local cosmetic presentation driven by replicated state; see
// Docs/BuffVisuals.md. Nothing in this file changes gameplay, collision or AI.

UENUM()
enum class ECireAuraShape : uint8
{
    Ring, Ripple, Crystals, Pool, Cracks,          // ground
    Swirl, Shell, Plates, Aegis, Flames, Motes, Drips, Chains, Halo, // body
    Hands, Weapon, Glyph, Tether, Tower
};

struct FCireAuraLayer
{
    ECireAuraShape Shape=ECireAuraShape::Ring;
    FName Style;
    float Size=1,Speed=1,Height=1,Alpha=1;
    int32 Count=6;
    bool bBurstOnly=false;
    bool bSecondary=false; // use the palette's secondary colour as the main tint
};
struct FCireAuraAttack
{
    bool bValid=false;
    FName Swipe;   // blood, frost, holy, ember, gold, steel, rhythm, venom, arcane
    FName OnHit;   // splash, shatter, glint, sparks, ripple, none
    FLinearColor Primary=FLinearColor::White,Core=FLinearColor::White;
};
struct FCireAuraDef
{
    FName Id;
    FString Name,Kind,School,Source;
    FLinearColor Primary=FLinearColor::White,Secondary=FLinearColor::White,Core=FLinearColor::White;
    int32 Priority=50;
    bool bAllegianceRim=true;
    TArray<FCireAuraLayer> Layers;
    float Base=1,PerStack=0,Expiring=1.5f,Burst=.45f,Fade=.4f,Light=0;
    int32 MaxStacks=1;
    FCireAuraAttack Attack;
    FString SoundStart,SoundLoop,SoundEnd,SoundHit;
};
struct FCireAuraLimits
{
    int32 MaxUnits=32,MaxLayersPerUnit=5,MaxLights=6,MaxStrikes=24,MaxVerticesPerUnit=9000;
    float FullDetailCm=2800,ReducedDetailCm=5500,CullCm=9000;
    FLinearColor Friendly=FLinearColor(.2f,.85f,1.f),Hostile=FLinearColor(1.f,.2f,.1f);
};

namespace CireAuraData
{
    CIRESTEAMSURVIVAL_API bool Reload(FString& Error);
    CIRESTEAMSURVIVAL_API bool Parse(const FString& Text,TMap<FName,FCireAuraDef>& Out,FCireAuraLimits& OutLimits,FString& Error,TMap<FName,FName>* OutItemBuffs=nullptr);
    CIRESTEAMSURVIVAL_API const FCireAuraDef* Find(FName Id);
    CIRESTEAMSURVIVAL_API const FCireAuraLimits& Limits();
    CIRESTEAMSURVIVAL_API const TMap<FName,FCireAuraDef>& All();
    /** Replicated item timed-buff id (UCireInventory::Buffs) -> visual id, from "itemBuffs" in the JSON. */
    CIRESTEAMSURVIVAL_API const TMap<FName,FName>& ItemBuffs();
}

// One visible effect on a unit, from the start burst through the fade-out.
struct FCireAuraInstance
{
    FName Id;
    float StartServer=0,EndServer=0;   // replicated clock
    float BornLocal=0,FadeLocal=-1;    // local presentation clock
    int32 Stacks=1;
    bool bSeen=false;
    TWeakObjectPtr<AActor> Link;
};

/** Per-unit renderer. Driven by UCireAuraSubsystem; never ticks on its own. */
UCLASS(Transient)
class CIRESTEAMSURVIVAL_API UCireAuraComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireAuraComponent();
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    /** Reads replicated state into Instances (start/refresh/fade). */
    void Synchronize(float ServerNow,float LocalNow);
    /** Builds geometry. MaxLayers==0 hides everything. Returns vertices produced. */
    int32 Render(float LocalNow,float AnimTime,float ServerNow,int32 Detail,int32 MaxLayers,bool bLight,float IntensityScale,const FVector& CameraLocation,const FRotator& CameraRotation);
    void HideAll();
    void ReleaseMeshes();
    bool HasVisibleWork() const;
    TArray<FCireAuraInstance> Instances;
    /** Attack modifier currently granted by the highest-priority active effect. */
    const FCireAuraDef* AttackModifier(float ServerNow) const;
    int32 CountLayers() const { return LastLayers; }
    int32 CountVertices() const { return LastVertices; }
    bool IsLightOn() const;
    bool AreMeshesCollisionFree() const;
    /** Gallery/tests: pretend every current effect started this many seconds ago. */
    void AgeForPreview(float Seconds);
    bool bRenderedThisFrame=false;
    /** The local player's own unit: overhead marks are skipped (the HUD buff bar covers them and they would sit in the camera's sightline). */
    bool bLocalView=false;
    /** Attached loop cues (heartbeat, fire, bubbles) per effect id; stopped on fade, hide, cull and unregister. */
    void UpdateLoops(bool bAllowed,int32& Budget,float Volume);
    void StopLoops();
    TSet<FName> LoopIds; // effects whose loop is active (kept even without an audio device, for tests)
    TMap<FName,TWeakObjectPtr<class UAudioComponent>> LoopAudio;
    /** fab-integration: optional Niagara signature per active effect from the Fab State/VFX packs (Content/Data/FabVFX.json
     *  "buffs"). Purely additive over the procedural layers; nothing spawns when the packs are not installed. */
    void UpdateFabAuras(bool bAllowed,int32& Budget,float Intensity);
    TMap<FName,TWeakObjectPtr<class UNiagaraComponent>> FabAuras;
    // Attack bookkeeping (subsystem).
    uint32 LastAttackSerial=0;
    bool bAttackPrimed=false;
    float PendingStrikeServer=0;
    bool bStrikePending=false;
    float LastStrikeLocal=-10;
    FVector PendingAim=FVector::ZeroVector;
    float Score=0;
    int32 FrameSkip=0;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Core;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Soft;
    UPROPERTY(Transient) TObjectPtr<UPointLightComponent> Light;
private:
    void EnsureMeshes();
    void Want(TArray<FCireAuraInstance>& Desired,FName Id,float Start,float End,int32 Stacks,AActor* Link) const;
    int32 LastLayers=0,LastVertices=0;
    TArray<FVector> CV,SV;TArray<int32> CI,SI;TArray<FLinearColor> CC,SC;TArray<FVector2D> SUV;TArray<FVector> CN;
};

/** Short-lived attack-modifier presentation: swipe arcs, projectile trails and on-hit bursts. */
UCLASS(NotBlueprintable,Transient)
class CIRESTEAMSURVIVAL_API ACireAuraStrike : public AActor
{
    GENERATED_BODY()
public:
    ACireAuraStrike();
    virtual void Tick(float Delta) override;
    enum class EMode : uint8 { Swipe, Hit, Trail, Muzzle };
    void Configure(EMode InMode,const FCireAuraAttack& Attack,FVector From,FVector To,float InScale,float Mirror=1.f);
    void FollowProjectile(AActor* Projectile,const FCireAuraAttack& Attack,float InScale);
    void SetPreviewAge(float Seconds);
    /** Gallery/tests: freeze a trail along these world points (oldest first). */
    void SetPreviewTrail(const TArray<FVector>& WorldPoints);
    int32 VertexCount() const { return LastVertices; }
    bool IsCollisionFree() const;
    EMode Mode=EMode::Swipe;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Core;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> Soft;
private:
    void Rebuild();
    FCireAuraAttack Style;
    FVector From,To;
    float Age=0,Duration=.4f,Scale=1,Mirror=1;
    int32 OriginPhase=INDEX_NONE,LastVertices=0;
    bool bPreview=false;
    TWeakObjectPtr<AActor> Followed;
    TArray<FVector> Trail;
    TArray<FVector> CV,SV;TArray<int32> CI,SI;TArray<FLinearColor> CC,SC;TArray<FVector2D> SUV;
};

UCLASS()
class CIRESTEAMSURVIVAL_API UCireAuraSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Tick(float Delta) override;
    virtual TStatId GetStatId() const override;
    virtual bool IsTickableInEditor() const override { return false; }
    void Register(UCireAuraComponent* Component);
    void Unregister(UCireAuraComponent* Component);
    /** Called by spell presentation for monster melee cues (no source actor on the wire). */
    void NotifyAttackCue(FName SkillId,FVector From,FVector To);
    ACireAuraStrike* SpawnStrike(ACireAuraStrike::EMode Mode,const FCireAuraAttack& Attack,FVector From,FVector To,float Scale,float Mirror=1.f);
    /** Deterministic galleries/tests: fixes the animation clock. <0 restores real time. */
    void SetPreviewClock(float Seconds) { PreviewClock=Seconds; }
    /** Runs one presentation pass immediately. LocalOverride>=0 replaces the lifecycle clock (tests). */
    void UpdateNow(float LocalOverride=-1.f);
    /** Tests/galleries: observe as this hero and/or from this camera instead of the local controller. */
    TWeakObjectPtr<AActor> ObserverOverride;
    bool bCameraOverride=false;
    FVector CameraOverrideLocation=FVector::ZeroVector;
    FRotator CameraOverrideRotation=FRotator::ZeroRotator;
    float LastLocalNow=0;
    // Diagnostics for tests.
    int32 RenderedUnits=0,RenderedLayers=0,LitUnits=0,LiveStrikes=0,RegisteredCount=0,CulledUnits=0;
    int32 SpawnedStrikes=0;
    TArray<TWeakObjectPtr<UCireAuraComponent>> Components;
private:
    void HandleAttacks(UCireAuraComponent* Aura,float ServerNow,float LocalNow);
    void HandleProjectiles();
    void HandleCombatEvents(float LocalNow);
    float PreviewClock=-1;
    float LocalClock=0;
    uint32 LastEventSequence=0;
    struct FPendingHit { FString SourceName; FVector Near=FVector::ZeroVector; float Until=0; FCireAuraAttack Attack; float Scale=1; FString SoundHit; TWeakObjectPtr<AActor> Unit; };
    struct FRecentHit { FString SourceName; FVector Location=FVector::ZeroVector; float Time=0; };
    TArray<FPendingHit> PendingHits;
    TArray<FRecentHit> RecentHits;
    TArray<TWeakObjectPtr<ACireAuraStrike>> Strikes;
    bool MatchHit(const FString& SourceName,const FVector& Location,float LocalNow);
    void QueueHit(FPendingHit&& Pending,float LocalNow);
    TMap<TWeakObjectPtr<AActor>,TWeakObjectPtr<ACireAuraStrike>> Trails;
};

namespace CireAuraVisuals
{
    CIRESTEAMSURVIVAL_API UCireAuraComponent* Attach(AActor* Unit);
    CIRESTEAMSURVIVAL_API UCireAuraSubsystem* Get(const UWorld* World);
    /** Sound cue ids are data hooks for the audio pass; see Docs/BuffVisuals.md. */
    CIRESTEAMSURVIVAL_API void PlaySoundCue(const FString& CueId,AActor* Unit,const FVector* Location=nullptr);
    /** Attached loops playing (or wanted, when no audio device exists) across all units; capped by MaxLoops. */
    constexpr int32 MaxLoops=4;
    /** fab-integration: Niagara aura overlays alive at once (nearest/most important units first). */
    constexpr int32 MaxFabAuras=8;
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSmoke(ACireGameMode* Mode);
#endif
}
