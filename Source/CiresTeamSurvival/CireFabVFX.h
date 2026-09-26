#pragma once
// fab-integration: optional Niagara overlay from the purchased Fab VFX packs (Docs/FAB-PURCHASED.md).
//
// Content/Data/FabVFX.json maps a damage school + presentation role to an ordered list of Niagara
// system object paths inside the locally installed packs. The packs are licensed and never committed,
// so every entry is optional: a path is used only when its package exists on disk, and when nothing
// resolves the caller keeps the procedural presentation (ACireSpellVisual / CireAuraVisuals), which
// is always drawn anyway. A clean clone without the packs therefore builds, runs and tests unchanged.
//
// fab-coverage: "abilities.<skill id>.<role>" gives one ability its own signature system ahead of the school set,
// and a candidate may be a Cascade UParticleSystem (Kakky FX Variety Pack) as well as a Niagara system: both are
// UFXSystemAssets and spawn as UFXSystemComponents.
//
// cire.FabVFX 0 (or -CireNoFabVFX) turns the overlay off for A/B captures.
#include "CoreMinimal.h"
#include "CireAbilityShapes.h"

class UFXSystemAsset;
class UFXSystemComponent;
class USceneComponent;
class UWorld;

namespace CireFabVFX
{
    enum class ERole : uint8 { Cast, Projectile, Impact, Area, Aura, Count };
    CIRESTEAMSURVIVAL_API FString RoleName(ERole Role);

    struct FEntry
    {
        TArray<FString> Candidates;  // object paths, first existing one wins
        float Scale = 1.f;           // uniform scale applied on spawn
        FLinearColor Tint = FLinearColor(0, 0, 0, 0); // A>0: pushed to common colour user parameters
    };

    CIRESTEAMSURVIVAL_API bool Enabled();
    // Reloads Content/Data/FabVFX.json (tests, hot data edits).
    CIRESTEAMSURVIVAL_API void Reload();
    // The configured entry for a school/role, or nullptr when the data has none.
    CIRESTEAMSURVIVAL_API const FEntry* Find(ECireSchool School, ERole Role);
    CIRESTEAMSURVIVAL_API const FEntry* FindBuff(const FString& Key);
    // fab-coverage: the ability's own entry for this role ("abilities.<id>.<role>"), or nullptr.
    CIRESTEAMSURVIVAL_API const FEntry* FindAbility(FName Skill, ERole Role);
    // The ability's own entry when it resolves locally, else the school set (Find). Used by every spell presentation.
    CIRESTEAMSURVIVAL_API const FEntry* FindFor(FName Skill, ECireSchool School, ERole Role);
    // First candidate whose package exists and loads as a Niagara or Cascade system; cached. Never logs for missing packs.
    CIRESTEAMSURVIVAL_API UFXSystemAsset* Resolve(const FEntry* Entry);
    CIRESTEAMSURVIVAL_API UFXSystemAsset* ResolveSchool(ECireSchool School, ERole Role, float* OutScale = nullptr);

    // Spawners (nullptr when the overlay is off or the pack is missing).
    CIRESTEAMSURVIVAL_API UFXSystemComponent* SpawnAttached(UFXSystemAsset* System, USceneComponent* Parent, FVector Offset, float Scale, bool bAutoDestroy);
    CIRESTEAMSURVIVAL_API UFXSystemComponent* SpawnAt(UWorld* World, UFXSystemAsset* System, FVector Location, FRotator Rotation, float Scale);
    // Tints the common Lord Enot / UrtanoVFX / SoftTofu colour user parameters when present.
    CIRESTEAMSURVIVAL_API void ApplyTint(UFXSystemComponent* Component, FLinearColor Tint);
    // Stops emitting and lets the live particles finish, then destroys the component (Niagara or Cascade).
    CIRESTEAMSURVIVAL_API void Release(UFXSystemComponent* Component);

    // telegraphs (2026-09-26): Fab ground-effect overlays (the "area" role under a live ACireAreaEffect).
    // Vendor area systems are authored at their own size and footprint (the holy set carries a diamond / square frame,
    // some throw world-space shards or smoke that ignore the component scale), so a ground overlay is CURATED:
    //   - it only decorates CIRCLE zones (lines, cones and polygons keep the procedural telegraph alone), never pylon fields;
    //   - the system must have a measured footprint in FabVFX.json "groundRadius" (cm at scale 1, from
    //     Tools/RunSpellGallery.py --fab-ground) and must not be listed in "groundExcluded" (path -> reason);
    //   - it is scaled so that footprint sits inside the true radius (GroundFitFraction) and dimmed with the slider.
    CIRESTEAMSURVIVAL_API bool IsGroundOverlay(const UFXSystemAsset* System, FString* Why = nullptr);
    // First candidate of the entry that resolves AND is a curated ground overlay (nullptr, with the reason, otherwise).
    CIRESTEAMSURVIVAL_API UFXSystemAsset* ResolveGround(const FEntry* Entry, FString* Why = nullptr);
    // Measured XY footprint radius of a system spawned at scale 1 (cm); 0 when not measured.
    CIRESTEAMSURVIVAL_API float NativeGroundRadius(const UFXSystemAsset* System);
    // Current XY reach of a live component's bounds from its origin (cm), for the gallery measurement pass.
    CIRESTEAMSURVIVAL_API float MeasureReach(const UFXSystemComponent* Component);
    // Fraction of the true radius a ground overlay may reach (the procedural rim stays the outermost line).
    constexpr float GroundFitFraction = .92f;
    // Multiplies the RGB of every exposed LinearColor user parameter (and float "Emissive"/"Intensity"/"Brightness"
    // parameters) by Brightness (Niagara only; Cascade systems expose none and are not used as ground overlays).
    // Returns how many parameters were scaled.
    CIRESTEAMSURVIVAL_API int32 DimColors(UFXSystemComponent* Component, float Brightness);

    // Diagnostics: how many school/role/buff/ability slots resolve right now (0 on a clean clone).
    struct FCoverage { int32 Configured = 0, Resolved = 0, Abilities = 0, AbilitySlots = 0, AbilitySlotsResolved = 0, Cascade = 0; TArray<FString> Missing; };
    CIRESTEAMSURVIVAL_API FCoverage Coverage();

#if !UE_BUILD_SHIPPING
    // Data validity + graceful fallback (missing packs resolve to nullptr without errors). Logs
    // CIRE_FAB_VFX_TESTS_PASS / CIRE_FAB_VFX_TESTS_FAIL.
    CIRESTEAMSURVIVAL_API bool RunTests(UWorld* World);
#endif
}
