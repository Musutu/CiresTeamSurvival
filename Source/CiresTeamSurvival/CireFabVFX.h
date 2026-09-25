#pragma once
// fab-integration: optional Niagara overlay from the purchased Fab VFX packs (Docs/FAB-PURCHASED.md).
//
// Content/Data/FabVFX.json maps a damage school + presentation role to an ordered list of Niagara
// system object paths inside the locally installed packs. The packs are licensed and never committed,
// so every entry is optional: a path is used only when its package exists on disk, and when nothing
// resolves the caller keeps the procedural presentation (ACireSpellVisual / CireAuraVisuals), which
// is always drawn anyway. A clean clone without the packs therefore builds, runs and tests unchanged.
//
// cire.FabVFX 0 (or -CireNoFabVFX) turns the overlay off for A/B captures.
#include "CoreMinimal.h"
#include "CireAbilityShapes.h"

class UNiagaraSystem;
class UNiagaraComponent;
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
    // kits-complete: a skill's own overlay ("abilities.<id>.<role>"); Skill may be the ability id or its display name.
    CIRESTEAMSURVIVAL_API const FEntry* FindAbility(FName Skill, ERole Role);
    // First candidate whose package exists and loads as a Niagara system; cached. Never logs for missing packs.
    CIRESTEAMSURVIVAL_API UNiagaraSystem* Resolve(const FEntry* Entry);
    CIRESTEAMSURVIVAL_API UNiagaraSystem* ResolveSchool(ECireSchool School, ERole Role, float* OutScale = nullptr);

    // Spawners (nullptr when the overlay is off or the pack is missing).
    CIRESTEAMSURVIVAL_API UNiagaraComponent* SpawnAttached(UNiagaraSystem* System, USceneComponent* Parent, FVector Offset, float Scale, bool bAutoDestroy);
    CIRESTEAMSURVIVAL_API UNiagaraComponent* SpawnAt(UWorld* World, UNiagaraSystem* System, FVector Location, FRotator Rotation, float Scale);
    // Tints the common Lord Enot / UrtanoVFX / SoftTofu colour user parameters when present.
    CIRESTEAMSURVIVAL_API void ApplyTint(UNiagaraComponent* Component, FLinearColor Tint);

    // Diagnostics: how many school/role slots resolve right now (0 on a clean clone).
    struct FCoverage { int32 Configured = 0, Resolved = 0; TArray<FString> Missing; };
    CIRESTEAMSURVIVAL_API FCoverage Coverage();

#if !UE_BUILD_SHIPPING
    // Data validity + graceful fallback (missing packs resolve to nullptr without errors). Logs
    // CIRE_FAB_VFX_TESTS_PASS / CIRE_FAB_VFX_TESTS_FAIL.
    CIRESTEAMSURVIVAL_API bool RunTests(UWorld* World);
#endif
}
