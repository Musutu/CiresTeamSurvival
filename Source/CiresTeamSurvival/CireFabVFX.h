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
        FLinearColor Tint = FLinearColor(0, 0, 0, 0); // A>0: recolour (pack-usage: Recolor / ApplyEntryTint)
        float TintStrength = 1.f;    // pack-usage: 0..1, how far every exposed colour moves to the tint's hue / saturation
        int8 Anchor = -1;            // vfx-loop-fix: "anchor": -1 by system (groundAnchored list), 0 body, 1 ground
        float Lifetime = 0.f;        // vfx-loop-fix: "lifetime" seconds for this one-shot entry (0: the role's default)
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
    // Sets the generic "Color" / "Tint" user parameters when a system exposes one (legacy; the arena portals use it).
    CIRESTEAMSURVIVAL_API void ApplyTint(UFXSystemComponent* Component, FLinearColor Tint);
    // pack-usage: recolour variant. Every exposed LinearColor user parameter of a Niagara system (Lord Enot exposes one per
    // part: Color_Trail, Color_Sparks, Color_Smoke...) keeps its brightness (HSV value, HDR intensity) and alpha and moves its
    // hue and saturation towards Tint by Strength; near-white flashes (saturation < .12) stay white so the read is kept.
    // Returns the number of parameters changed (0 for Cascade or a system without colour parameters).
    CIRESTEAMSURVIVAL_API int32 Recolor(UFXSystemComponent* Component, FLinearColor Tint, float Strength = 1.f);
    // Applies the entry's tint (Recolor) when it has one; a no-op otherwise. Used by every data-driven spawn.
    CIRESTEAMSURVIVAL_API void ApplyEntryTint(UFXSystemComponent* Component, const FEntry& Entry);
    // The entry for a data key in the "abilities" table (hit.<layer>.<weapon>, kill.<class>, level_up...), or nullptr.
    CIRESTEAMSURVIVAL_API const FEntry* FindKey(const FString& Key, ERole Role);
    // Stops emitting and lets the live particles finish, then destroys the component (Niagara or Cascade).
    // vfx-loop-fix: a vendor system that keeps looping after Deactivate (infinite emitters that ignore the inactive state)
    // is hard-stopped ReleaseFadeSeconds() later, so no released effect (buff ended, cast visual gone) can outlive its owner.
    CIRESTEAMSURVIVAL_API void Release(UFXSystemComponent* Component);

    // vfx-loop-fix (Eric, playtest 2026-09-28: "a constant loop of fire eruption around the mid section ... should appear
    // from the ground"). Vendor systems are authored either to loop (auras, pillars, rings) or to burst once; the same asset
    // is often reused as a one-shot (a cast flare, an impact). Every one-shot spawn is therefore BOUNDED: it is released after
    // its role's lifetime (FabVFX.json "lifetime": cast / impact / levelUp seconds) and hard-stopped after the fade window,
    // whatever the asset does. Looping roles (projectile trails, zones, buff auras) end with their owner through Release.
    CIRESTEAMSURVIVAL_API float OneShotSeconds(ERole Role, const FEntry* Entry = nullptr);
    CIRESTEAMSURVIVAL_API float ReleaseFadeSeconds();
    CIRESTEAMSURVIVAL_API float LevelUpSeconds();
    // Releases the component after Seconds (0 or less: never) and hard-stops it ReleaseFadeSeconds() after that.
    CIRESTEAMSURVIVAL_API void Bound(UFXSystemComponent* Component, float Seconds);
    // Live bounded / released components still being tracked (tests).
    CIRESTEAMSURVIVAL_API int32 TrackedCount();
    // Advances the tracker (normally a core ticker; tests call it with a fake clock offset).
    CIRESTEAMSURVIVAL_API void TickTracked(double ExtraSeconds = 0);
    // Ground-anchored systems (FabVFX.json "groundAnchored": object path -> why): eruptions, pillars, rings and circles authored
    // to rise from the floor. On a unit they spawn at its feet (GroundUnder), never at the capsule centre (the midsection).
    // An entry may force it with "anchor": "ground" / "body".
    CIRESTEAMSURVIVAL_API bool IsGroundAnchored(const UFXSystemAsset* System, const FEntry* Entry = nullptr);
    CIRESTEAMSURVIVAL_API bool IsGroundAnchoredPath(const FString& ObjectPath);
    // The floor under a unit or point: a character's capsule bottom, else a downward trace (WorldStatic) from At, else At.
    CIRESTEAMSURVIVAL_API FVector GroundUnder(UWorld* World, FVector At, const AActor* Unit = nullptr);
    // Relative offset (in the root component's unscaled space) from a character's root to its feet (+ a 2 cm lift);
    // (0,0,-88) for anything without a capsule. Used by every Fab effect attached to a unit's root (auras, level-up).
    CIRESTEAMSURVIVAL_API FVector FeetOffset(const AActor* Unit);

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
    // vfx-loop-fix (CireVFXLoopTests.cpp): bounded one-shots, effects ending with their buff, ground-anchored placement.
    // Logs CIRE_VFX_LOOP_TESTS_PASS / _FAIL.
    CIRESTEAMSURVIVAL_API bool RunLoopTests(UWorld* World);
#endif
}
