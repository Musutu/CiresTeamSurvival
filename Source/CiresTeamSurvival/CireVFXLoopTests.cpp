// vfx-loop-fix: native tests for bounded Fab one-shots, effects ending with their buff, and ground-anchored placement.
// Eric (playtest 2026-09-28): "a constant loop of fire eruption around the mid section of the characters. This should
// appear from the ground." Called from CireFabVFX::RunTests (CireCombatExpansionProbe native gate).
#include "CireFabVFX.h"
#if !UE_BUILD_SHIPPING
#include "CireAuraVisuals.h"
#include "CireBuffs.h"
#include "CireGame.h"
#include "CireSpellPresentation.h"
#include "Components/CapsuleComponent.h"
#include "Engine/Engine.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "NiagaraSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "NiagaraComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireVFXLoop, Log, All);

namespace
{
const TCHAR* VfxLoopFlame1 = TEXT("/Game/Big_Pack_Magic_VFX/Pack_Effects_1/Fire_Magic/VFX_Niagara/NS_Fire_Magic_Flame1.NS_Fire_Magic_Flame1");
const TCHAR* VfxLoopBuff = TEXT("/Game/Big_Pack_Magic_VFX/Pack_Effects_1/Fire_Magic/VFX_Niagara/NS_Fire_Magic_Buff.NS_Fire_Magic_Buff");

// Ended: destroyed, being destroyed, or no longer active (a pooled one back in its pool).
bool VfxLoopEnded(const TWeakObjectPtr<UFXSystemComponent>& C)
{
    return !C.IsValid() || C->IsBeingDestroyed() || !C->IsActive();
}
}

bool CireFabVFX::RunLoopTests(UWorld* World)
{
    int32 Checks = 0, Failed = 0, Skipped = 0;
    auto Check = [&](bool bOk, const FString& Why) { ++Checks; if (!bOk) { ++Failed; UE_LOG(LogCireVFXLoop, Error, TEXT("CIRE_VFX_LOOP_CHECK_FAIL %s"), *Why); } };
    if (!World) { UE_LOG(LogCireVFXLoop, Error, TEXT("CIRE_VFX_LOOP_TESTS_FAIL no world")); return false; }
    // The world clock does not move inside a native test: the tracker is advanced on a fake clock, in small steps (a release
    // arms its kill relative to the moment it happens, like the real per-frame ticker).
    auto Pump = [](double Seconds) { for (double T = 0; T <= Seconds + .001; T += .1) TickTracked(T); };

    // ---- 1. Data: lifetimes, ground-anchored list, the two playtest systems -----------------------------------------
    Reload();
    Check(OneShotSeconds(ERole::Cast) > .2f && OneShotSeconds(ERole::Impact) > .2f && ReleaseFadeSeconds() > .2f && LevelUpSeconds() > .2f,
        TEXT("FabVFX.json lifetime: cast / impact / levelUp / releaseFade are positive"));
    Check(IsGroundAnchoredPath(VfxLoopFlame1), TEXT("NS_Fire_Magic_Flame1 (Cinder Cone cast: rings rising to a flame column) is ground-anchored"));
    Check(IsGroundAnchoredPath(VfxLoopBuff), TEXT("NS_Fire_Magic_Buff (Ashen Ward cast, fire buff aura: flame pillar) is ground-anchored"));
    {
        FEntry Body; Body.Candidates.Add(VfxLoopFlame1); Body.Anchor = 0;
        FEntry Ground; Ground.Anchor = 1;
        Check(!IsGroundAnchored(nullptr, &Body) && IsGroundAnchored(nullptr, &Ground), TEXT("an entry's \"anchor\" overrides the system list"));
        FEntry Timed; Timed.Lifetime = 4.5f;
        Check(FMath::IsNearlyEqual(OneShotSeconds(ERole::Cast, &Timed), 4.5f), TEXT("an entry's \"lifetime\" overrides the role default"));
    }

    // A headless probe has no view: Niagara's scalability would pre-cull every spawn. Pause culling for the fixtures.
    if (GEngine) GEngine->Exec(World, TEXT("fx.Niagara.Scalability.CullingMode 1"));
    ON_SCOPE_EXIT { if (GEngine) GEngine->Exec(World, TEXT("fx.Niagara.Scalability.CullingMode 0")); };
    // Fixtures: a far stage, a hero standing on nothing (its capsule bottom is the ground).
    const FVector Stage(-61000, 47000, 26000);
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT { for (AActor* A : Actors) if (IsValid(A)) A->Destroy(); };
    FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ACireHero* Hero = World->SpawnActor<ACireHero>(Stage, FRotator::ZeroRotator, P);
    if (Hero) { Actors.Add(Hero); Hero->TeamId = 0; Hero->Draft(0); Hero->SetActorTickEnabled(false); Hero->SetActorEnableCollision(false); Hero->GetCharacterMovement()->DisableMovement(); }
    Check(Hero != nullptr, TEXT("fixture hero spawns"));
    if (!Hero) { UE_LOG(LogCireVFXLoop, Display, TEXT("CIRE_VFX_LOOP_TESTS_FAIL checks=%d failed=%d"), Checks, Failed + 1); return false; }
    const float Feet = static_cast<float>(Hero->GetActorLocation().Z - Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    Check(FMath::IsNearlyEqual(static_cast<float>(GroundUnder(World, Hero->GetActorLocation(), Hero).Z), Feet + 2.f, .5f), TEXT("GroundUnder a character is its capsule bottom"));
    Check(FMath::IsNearlyEqual(static_cast<float>((Hero->GetActorTransform().TransformPosition(FeetOffset(Hero))).Z), Feet + 2.f, .5f), TEXT("FeetOffset puts a root attachment on the feet"));

    // ---- 2. Bounded one-shot: a looping pillar spawned as a one-shot still ends ------------------------------------
    FEntry Flame; Flame.Candidates.Add(VfxLoopFlame1);
    UFXSystemAsset* FlameSystem = Enabled() ? Resolve(&Flame) : nullptr;
    FEntry Pillar; Pillar.Candidates.Add(VfxLoopBuff);
    UFXSystemAsset* PillarSystem = Enabled() ? Resolve(&Pillar) : nullptr;
    // Niagara never creates components without a renderer (FApp::CanEverRender, the -nullrhi gate). There a registered
    // Niagara component carrying the system and forced active stands in for a vendor loop that never completes on its own.
    const bool bLive = FApp::CanEverRender();
    auto StandIn = [&](UFXSystemAsset* System, bool bAutoDestroy) -> UFXSystemComponent*
    {
        UNiagaraComponent* N = NewObject<UNiagaraComponent>(Hero);
        N->SetAsset(Cast<UNiagaraSystem>(System)); N->SetAutoActivate(false); N->SetAutoDestroy(bAutoDestroy);
        N->SetupAttachment(Hero->GetRootComponent()); N->SetRelativeLocation(FeetOffset(Hero)); N->RegisterComponent();
        N->SetActiveFlag(true);
        return N;
    };
    auto OneShot = [&](UFXSystemAsset* System) -> UFXSystemComponent*
    {
        if (bLive) return SpawnAttached(System, Hero->GetRootComponent(), FeetOffset(Hero), 1.f, true);
        UFXSystemComponent* C = StandIn(System, true); Bound(C, OneShotSeconds(ERole::Cast)); return C; // what SpawnAttached(auto-destroy) does
    };
    if (FlameSystem && PillarSystem)
    {
        TWeakObjectPtr<UFXSystemComponent> Shot = OneShot(FlameSystem);
        Check(Shot.IsValid(), TEXT("one-shot cast system spawns")); // a live system may still be compiling (activation deferred)
        Pump(OneShotSeconds(ERole::Cast) * .5);
        if (!bLive) Check(Shot.IsValid() && Shot->IsActive(), TEXT("a looping one-shot keeps playing during its lifetime"));
        Pump(OneShotSeconds(ERole::Cast) + ReleaseFadeSeconds() + .1);
        Check(VfxLoopEnded(Shot), TEXT("one-shot spawn ends after its lifetime + fade even if the asset loops (no endless eruption)"));

        if (bLive)
        {
            TWeakObjectPtr<UFXSystemComponent> Burst = SpawnAt(World, PillarSystem, Stage + FVector(400, 0, 0), FRotator::ZeroRotator, 1.f);
            Pump(OneShotSeconds(ERole::Impact) + ReleaseFadeSeconds() + .1);
            Check(VfxLoopEnded(Burst), TEXT("world-placed burst (SpawnAt) is bounded"));
        }

        // Persistent overlay (a buff aura / zone): never bounded by time, ends with Release (its owner), hard-stopped after the fade.
        TWeakObjectPtr<UFXSystemComponent> Loop = bLive ? SpawnAttached(PillarSystem, Hero->GetRootComponent(), FeetOffset(Hero), 1.f, false) : StandIn(PillarSystem, false);
        Pump(30.0);
        Check(Loop.IsValid() && !Loop->IsBeingDestroyed(), TEXT("persistent overlay is never cut by time while its owner lives"));
        Release(Loop.Get());
        Pump(ReleaseFadeSeconds() + .1);
        Check(VfxLoopEnded(Loop), TEXT("released overlay is gone after the fade window (even when the asset ignores Deactivate)"));
    }
    else ++Skipped; // clean clone without the Big Pack: the procedural presentation stands alone

    // ---- 3. A Fab buff aura ends with its buff and sits at the feet ---------------------------------------------------
    if (UCireAuraSubsystem* Auras = CireAuraVisuals::Get(World); Auras && PillarSystem)
    {
        UCireAuraComponent* Aura = Hero->FindComponentByClass<UCireAuraComponent>();
        const TWeakObjectPtr<AActor> OldObserver = Auras->ObserverOverride; const bool bOldCamera = Auras->bCameraOverride;
        const FVector OldLoc = Auras->CameraOverrideLocation; const FRotator OldRot = Auras->CameraOverrideRotation;
        ON_SCOPE_EXIT { Auras->ObserverOverride = OldObserver; Auras->bCameraOverride = bOldCamera; Auras->CameraOverrideLocation = OldLoc; Auras->CameraOverrideRotation = OldRot; };
        Auras->ObserverOverride = Hero; Auras->bCameraOverride = true;
        Auras->CameraOverrideLocation = Stage + FVector(-600, 0, 400); Auras->CameraOverrideRotation = (Stage - Auras->CameraOverrideLocation).Rotation();
        const FName Id(TEXT("artillery")); // stance, FabVFX buffs.artillery = NS_Fire_Magic_Buff
        float Clock = 5000.f;
        Check(Aura && CireBuffs::Apply(Hero, Id, 3.f, Hero), TEXT("fire stance recorded on the hero"));
        if (Aura)
        {
            Auras->UpdateNow(Clock);
            // Headless (or a culled live spawn): Niagara skipped the component; a stand-in carries the lifecycle check.
            if (!Aura->FabAuras.Contains(Id)) { UE_LOG(LogCireVFXLoop, Display, TEXT("CIRE_VFX_LOOP_NOTE aura spawn skipped by Niagara (live=%d): stand-in"), bLive); Aura->FabAuras.Add(Id, StandIn(PillarSystem, false)); }
            Clock += .5f; Auras->UpdateNow(Clock);
            const TWeakObjectPtr<UFXSystemComponent>* FX = Aura->FabAuras.Find(Id);
            TWeakObjectPtr<UFXSystemComponent> Live = FX ? *FX : nullptr;
            Check(Live.IsValid() && !Live->IsBeingDestroyed(), TEXT("fire buff keeps its Fab aura while the buff lasts"));
            if (Live.IsValid())
                Check(FMath::Abs(static_cast<float>(Live->GetComponentLocation().Z) - (Feet + 2.f)) < 3.f,
                    FString::Printf(TEXT("fire buff aura sits at the feet (z %.0f, feet %.0f), not the midsection"), Live->GetComponentLocation().Z, Feet));
            CireBuffs::ClearAll(Hero); // the buff ends (expiry / cleanse)
            Clock += 3.f; Auras->UpdateNow(Clock);
            Check(!Aura->FabAuras.Contains(Id), TEXT("buff ended: its Fab aura is released"));
            Pump(ReleaseFadeSeconds() + .1);
            Check(VfxLoopEnded(Live), TEXT("buff ended: its Fab aura stops after the fade (no endless loop)"));
        }
    }
    else ++Skipped;

    // ---- 4. Cast presentation: Ashen Ward / Cinder Cone erupt from the ground under the caster, and stop -------------
    if (FlameSystem && PillarSystem)
    {
        for (const TCHAR* Skill : { TEXT("ashen_square"), TEXT("cinder_cone") })
        {
            ACireSpellVisual* V = CireSpellPresentation::Play(World, Skill, Hero->GetActorLocation(), Hero->GetActorLocation() + FVector(600, 0, 0), ECireSpellCue::Cast, 1, false);
            if (!V) { Check(false, FString(TEXT("cast visual spawns: ")) + Skill); continue; }
            UE_LOG(LogCireVFXLoop, Display, TEXT("CIRE_VFX_LOOP_CAST_START %s mode=%d delay=%.2f hidden=%d"), Skill, static_cast<int32>(V->GetMode()), V->GetStartDelay(), V->IsHidden());
            for (int32 I = 0; I < 60 && V->FabSpawnAt.IsZero() && IsValid(V) && !V->IsActorBeingDestroyed(); ++I)
            {
                const float PreAge = V->GetAgeSeconds(); const bool bPreHidden = V->IsHidden(), bPreTried = V->WasFabTried();
                V->Tick(1.f / 30.f);
                if (IsValid(V)) UE_LOG(LogCireVFXLoop, Verbose, TEXT("CIRE_VFX_LOOP_CAST_TICK %s age=%.2f hidden=%d tried=%d dying=%d"), Skill, V->GetAgeSeconds(), V->IsHidden(), V->WasFabTried(), V->IsActorBeingDestroyed());
                if (IsValid(V) && V->WasFabTried() && V->FabSpawnAt.IsZero()) { UE_LOG(LogCireVFXLoop, Display, TEXT("CIRE_VFX_LOOP_CAST_NO_FAB_ROLE %s age=%.2f"), Skill, V->GetAgeSeconds()); break; }
                if (!IsValid(V) || V->IsActorBeingDestroyed()) UE_LOG(LogCireVFXLoop, Display, TEXT("CIRE_VFX_LOOP_CAST_DIED %s age=%.2f hidden=%d tried=%d"), Skill, PreAge, bPreHidden, bPreTried);
            }
            TWeakObjectPtr<UFXSystemComponent> Fab = IsValid(V) ? V->GetFabFX() : nullptr;
            UE_LOG(LogCireVFXLoop, Display, TEXT("CIRE_VFX_LOOP_CAST %s mode=%d ground=%d at_z=%.0f feet=%.0f centre=%.0f fab=%d"), Skill, IsValid(V) ? static_cast<int32>(V->GetMode()) : -1,
                IsValid(V) && V->bFabAnchoredGround, IsValid(V) ? V->FabSpawnAt.Z : 0.f, Feet, Hero->GetActorLocation().Z, Fab.IsValid());
            // Ashen Ward's cast is a ground-aimed circle drawn by the procedural telegraph (no Fab cast role); Cinder Cone's cone
            // cast is a caster flare that carries its Fab eruption (NS_Fire_Magic_Flame1).
            const bool bNeedsFab = FCString::Strcmp(Skill, TEXT("cinder_cone")) == 0;
            if (!bNeedsFab && IsValid(V) && V->FabSpawnAt.IsZero()) { V->Destroy(); continue; }
            Check(IsValid(V) && !V->FabSpawnAt.IsZero(), FString(TEXT("cast places its Fab signature: ")) + Skill);
            if (!IsValid(V)) continue;
            Check(V->bFabAnchoredGround, FString(TEXT("cast eruption is ground-anchored: ")) + Skill);
            Check(FMath::Abs(static_cast<float>(V->FabSpawnAt.Z) - (Feet + 2.f)) < 3.f,
                FString::Printf(TEXT("%s cast eruption rises from the feet (z %.0f, feet %.0f), not the capsule centre (%.0f)"), Skill, V->FabSpawnAt.Z, Feet, Hero->GetActorLocation().Z));
            if (Fab.IsValid()) Check(FMath::Abs(static_cast<float>(Fab->GetComponentLocation().Z) - (Feet + 2.f)) < 3.f, FString(TEXT("live Fab component sits at the feet: ")) + Skill);
            for (int32 I = 0; I < 120 && IsValid(V) && !V->IsActorBeingDestroyed(); ++I) V->Tick(1.f / 30.f);
            Check(!IsValid(V) || V->IsActorBeingDestroyed(), FString(TEXT("cast visual ends: ")) + Skill);
            Pump(OneShotSeconds(ERole::Cast) + ReleaseFadeSeconds() + .1);
            Check(VfxLoopEnded(Fab), FString(TEXT("cast eruption stops after its cast (no endless loop): ")) + Skill);
        }
    }
    else ++Skipped;

    for (AActor* A : Actors) if (IsValid(A)) A->Destroy();
    Pump(OneShotSeconds(ERole::Impact) + ReleaseFadeSeconds() + .5); // leave nothing tracked from the fixtures
    const bool bPass = Failed == 0;
    UE_LOG(LogCireVFXLoop, Display, TEXT("CIRE_VFX_LOOP_TESTS_%s checks=%d failed=%d skipped_sections=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, Failed, Skipped);
    return bPass;
}
#endif
