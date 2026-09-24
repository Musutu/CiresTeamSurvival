// Native arena checks (run by the combat expansion suite, -CireCombatExpansionProbe).
#include "CireArenas.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireAmbience.h"
#include "CireMusic.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireArenaTests, Log, All);

bool CireArenas::RunSmoke(ACireGameMode* Mode)
{
    bool bPassed = true; int32 Checks = 0;
    auto Check = [&](bool bOk, const FString& Label)
    {
        ++Checks;
        if (!bOk) { bPassed = false; UE_LOG(LogCireArenaTests, Error, TEXT("CIRE_ARENA_CHECK_FAIL %s"), *Label); }
    };
    UWorld* World = Mode ? Mode->GetWorld() : nullptr;
    if (!World) { UE_LOG(LogCireArenaTests, Error, TEXT("CIRE_ARENA_FAIL no world")); return false; }
    const FPool& P = Pool(true);
    Check(P.bValid && P.Errors.IsEmpty(), TEXT("Arenas.json parses without errors"));
    Check(P.Arenas.IsValidIndex(P.FallbackIndex) && P.Arenas[P.FallbackIndex].Id == TEXT("sundered_court") && P.Arenas[P.FallbackIndex].bFallbackOnly,
        TEXT("the legacy court is kept as the fallback arena"));

    // ---- every arena's data validates: fairness, bounds, spawns, slots, lighting, paths ----
    int32 Themed = 0;
    TSet<FName> Ids;
    for (const FArena& A : P.Arenas)
    {
        const TArray<FString> Errors = Validate(A, P);
        for (const FString& E : Errors) UE_LOG(LogCireArenaTests, Error, TEXT("CIRE_ARENA_INVALID %s"), *E);
        Check(Errors.IsEmpty(), FString::Printf(TEXT("%s validates"), *A.Id.ToString()));
        Check(!Ids.Contains(A.Id), FString::Printf(TEXT("%s id is unique"), *A.Id.ToString())); Ids.Add(A.Id);
        if (!A.bFallbackOnly) ++Themed;
        // Spawns: five a side, mirrored across the centre line, inside the bounds, never inside a blocker.
        bool bSymmetric = A.Spawns[0].Num() == 5 && A.Spawns[1].Num() == 5, bInside = true, bClear = true;
        const TArray<FFootprint> Blockers = Footprints(A, P);
        for (int32 I = 0; bSymmetric && I < 5; ++I) bSymmetric = A.Spawns[0][I].Equals(FVector2D(-A.Spawns[1][I].X, A.Spawns[1][I].Y), 1.0);
        for (int32 Team = 0; Team < 2; ++Team) for (const FVector2D& S : A.Spawns[Team])
        {
            bInside &= FMath::Abs(S.X) < A.HalfExtents.X - 100 && FMath::Abs(S.Y) < A.HalfExtents.Y - 100;
            for (const FFootprint& F : Blockers)
            {
                const FVector2D L = (S - F.Center).GetRotated(-F.Yaw);
                if (F.bRound ? L.Size() < F.Extent.X + 100 : FMath::Abs(L.X) < F.Extent.X + 100 && FMath::Abs(L.Y) < F.Extent.Y + 100) bClear = false;
            }
        }
        Check(bSymmetric, FString::Printf(TEXT("%s spawns are mirror-symmetric"), *A.Id.ToString()));
        Check(bInside, FString::Printf(TEXT("%s spawns lie inside the bounds"), *A.Id.ToString()));
        Check(bClear, FString::Printf(TEXT("%s blockers do not crowd or trap any spawn"), *A.Id.ToString()));
        FString PathError; float Reachable = 0;
        Check(SpawnsConnected(A, P, 50.f, &PathError, &Reachable), FString::Printf(TEXT("%s has a walkable path between every pair of spawns %s"), *A.Id.ToString(), *PathError));
        Check(Reachable >= .9f, FString::Printf(TEXT("%s: %.0f%% of the floor reachable (no sealed pockets)"), *A.Id.ToString(), Reachable * 100));
        if (!A.bFallbackOnly)
        {
            int32 Tall = 0; for (const FFootprint& F : Blockers) if (F.Height >= 190) ++Tall;
            Check(Tall >= 6, FString::Printf(TEXT("%s has at least six line-of-sight blockers (has %d)"), *A.Id.ToString(), Tall));
            Check(!A.Lighting.SkyMaterial.IsEmpty(), FString::Printf(TEXT("%s has its own sky"), *A.Id.ToString()));
            Check(!A.Ambience.IsNone() && A.Ambience != TEXT("arena"), FString::Printf(TEXT("%s names its own ambience"), *A.Id.ToString()));
            const CireAmbience::FDistrict* District = CireAmbience::Data().Districts.Find(A.Ambience);
            Check(District && District->Beds.Num() > 0, FString::Printf(TEXT("%s ambience district %s exists with beds"), *A.Id.ToString(), *A.Ambience.ToString()));
            Check(A.Music.IsEmpty() || CireMusic::Data().Titles.Contains(A.Music), FString::Printf(TEXT("%s music override %s is a known track"), *A.Id.ToString(), *A.Music));
        }
        UE_LOG(LogCireArenaTests, Display, TEXT("CIRE_ARENA_DATA id=%s name=\"%s\" blockers=%d pieces=%d scatter=%d reachable=%.3f errors=%d"),
            *A.Id.ToString(), *A.Name, Blockers.Num(), A.Pieces.Num(), A.Scatter.Num(), Reachable, Errors.Num());
    }
    Check(Themed >= 4, FString::Printf(TEXT("at least four themed arenas (%d)"), Themed));

    // ---- selection: random, uniform-ish, never an immediate repeat ----
    const TArray<int32> Rot = Rotation();
    Check(Rot.Num() >= 4, FString::Printf(TEXT("at least four arenas in the rotation (%d)"), Rot.Num()));
    Check(!Rot.Contains(P.FallbackIndex), TEXT("the fallback court is not in the normal rotation"));
    {
        FRandomStream Stream(20260924); TMap<int32, int32> Counts; int32 Previous = INDEX_NONE; bool bNoRepeat = true, bInRotation = true;
        constexpr int32 Draws = 600;
        for (int32 I = 0; I < Draws; ++I)
        {
            const int32 Next = PickNext(Previous, &Stream);
            bNoRepeat &= Rot.Num() < 2 || Next != Previous; bInRotation &= Rot.Contains(Next);
            ++Counts.FindOrAdd(Next); Previous = Next;
        }
        Check(bNoRepeat, TEXT("the same arena is never picked twice in a row"));
        Check(bInRotation, TEXT("picks come only from the rotation"));
        for (int32 I : Rot) Check(Counts.FindRef(I) >= Draws / Rot.Num() / 2, FString::Printf(TEXT("arena %s is picked (%d of %d)"), *P.Arenas[I].Id.ToString(), Counts.FindRef(I), Draws));
        FRandomStream A(7), B(7); bool bDeterministic = true;
        for (int32 I = 0; I < 20; ++I) bDeterministic &= PickNext(I % 2, &A) == PickNext(I % 2, &B);
        Check(bDeterministic, TEXT("seeded picks are reproducible"));
    }
    Check(SpawnLocation(-5, 0, 2).Equals(SpawnLocation(P.FallbackIndex, 0, 2)), TEXT("an unknown arena index falls back to the legacy court"));

    // ---- build, show and clean up every arena in the live world ----
    auto CountActors = [&](bool bStagesOnly)
    {
        int32 N = 0;
        for (TActorIterator<AActor> It(World); It; ++It) if (!bStagesOnly || It->IsA<ACireArenaStage>() || It->ActorHasTag(TEXT("CireArena"))) ++N;
        return N;
    };
    auto TownSunsVisible = [&]()
    {
        int32 Visible = 0; for (TActorIterator<ADirectionalLight> It(World); It; ++It) if (It->GetLightComponent()->IsVisible()) ++Visible; return Visible;
    };
    ReleaseForce(World);
    const int32 Baseline = CountActors(false), TownSuns = TownSunsVisible();
    Check(CountActors(true) == 0, TEXT("no arena exists outside the arena phases"));
    TArray<int32> ToBuild = Rot; ToBuild.Add(P.FallbackIndex);
    FCollisionObjectQueryParams Static; Static.AddObjectTypesToQuery(ECC_WorldStatic);
    for (const int32 Index : ToBuild)
    {
        const FArena& A = P.Arenas[Index];
        Force(World, Index, true);
        ACireArenaStage* S = Stage(World);
        Check(S && S->ArenaIndex == Index && S->bShown, FString::Printf(TEXT("%s builds and shows"), *A.Id.ToString()));
        if (!S) continue;
        Check(CountActors(true) == 1, FString::Printf(TEXT("%s is exactly one stage actor"), *A.Id.ToString()));
        Check(S->BlockerCount == Footprints(A, P).Num(), FString::Printf(TEXT("%s collision proxies match its blockers"), *A.Id.ToString()));
        Check(S->FallbackSlots == 0, FString::Printf(TEXT("%s resolved every art slot (missing: %s)"), *A.Id.ToString(), *FString::JoinBy(S->MissingSlots, TEXT(","), [](FName N) { return N.ToString(); })));
        for (int32 Team = 0; Team < 2; ++Team) for (int32 Slot = 0; Slot < 5; ++Slot)
        {
            const FVector Spawn = SpawnLocation(Index, Team, Slot, 0);
            FHitResult Hit;
            const bool bFloor = World->LineTraceSingleByObjectType(Hit, Spawn + FVector(0, 0, 400), Spawn - FVector(0, 0, 300), Static);
            Check(bFloor && FMath::Abs(Hit.ImpactPoint.Z - Spawn.Z) < 5, FString::Printf(TEXT("%s team %d spawn %d stands on the floor"), *A.Id.ToString(), Team, Slot));
            FCollisionQueryParams Q(SCENE_QUERY_STAT(CireArenaSpawn), false);
            const bool bBlocked = World->OverlapAnyTestByObjectType(Spawn + FVector(0, 0, 100), FQuat::Identity, Static, FCollisionShape::MakeCapsule(44, 88), Q);
            Check(!bBlocked, FString::Printf(TEXT("%s team %d spawn %d has capsule clearance"), *A.Id.ToString(), Team, Slot));
        }
        // Tall blockers really block line of sight at chest height.
        int32 Sight = 0, SightBlocked = 0;
        for (const FFootprint& F : Footprints(A, P))
        {
            if (F.Height < 190) continue;
            const FVector C = Origin() + FVector(F.Center.X, F.Center.Y, 130);
            const float R = F.Extent.GetMax() + 80;
            FHitResult Hit; ++Sight;
            if (World->LineTraceSingleByObjectType(Hit, C - FVector(0, R, 0), C + FVector(0, R, 0), Static) && Hit.GetComponent() &&
                Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) ++SightBlocked;
        }
        Check(Sight == SightBlocked, FString::Printf(TEXT("%s: %d/%d tall blockers stop sight lines"), *A.Id.ToString(), SightBlocked, Sight));
        // Arena bounds keep players in.
        FHitResult Wall;
        Check(World->LineTraceSingleByObjectType(Wall, Origin() + FVector(A.HalfExtents.X - 10, 0, 150), Origin() + FVector(A.HalfExtents.X + 400, 0, 150), Static),
            FString::Printf(TEXT("%s bounds wall stops players leaving"), *A.Id.ToString()));
        if (World->GetNetMode() != NM_DedicatedServer && A.Id != TEXT("sundered_court"))
        {
            int32 Suns = 0; for (UActorComponent* C : S->GetComponents()) if (C && C->IsA<UDirectionalLightComponent>()) ++Suns;
            Check(Suns == 1 && TownSunsVisible() == 0, FString::Printf(TEXT("%s replaces the town lighting while shown"), *A.Id.ToString()));
        }
        UE_LOG(LogCireArenaTests, Display, TEXT("CIRE_ARENA_BUILT id=%s blockers=%d instances=%d components=%d"), *A.Id.ToString(), S->BlockerCount, S->InstanceCount, S->GetComponents().Num());
    }
    ReleaseForce(World);
    Check(Stage(World) == nullptr && CountActors(true) == 0, TEXT("cleanup destroys the arena stage"));
    Check(CountActors(false) == Baseline, FString::Printf(TEXT("cleanup leaves no leftover actors (%d vs %d)"), CountActors(false), Baseline));
    Check(TownSunsVisible() == TownSuns, TEXT("town lighting is restored after the arena"));

    // ---- the real server flow: prep prebuilds hidden, the arena shows, recovery cleans up ----
    if (auto* State = Mode->GetGameState<ACireGameState>())
    {
        const int32 OldPhase = State->Phase, OldIndex = State->ArenaIndex, OldModeIndex = Mode->ArenaIndex;
        State->Phase = 1; ServerPrepare(Mode);
        const int32 First = Mode->ArenaIndex;
        Check(State->ArenaIndex == First && Rot.Contains(First), TEXT("prep picks a rotation arena and replicates its index"));
        Check(Stage(World) && Stage(World)->ArenaIndex == First && !Stage(World)->bShown, TEXT("prep prebuilds the arena hidden"));
        State->Phase = 2; ServerBegin(Mode);
        Check(Mode->ArenaIndex == First && Stage(World) && Stage(World)->bShown, TEXT("the arena phase shows the prepared arena"));
        const FVector Spawn = Mode->ArenaPosition(1, 3);
        Check(InBounds(World, Spawn, 100) && Spawn.Equals(SpawnLocation(First, 1, 3, 110)), TEXT("arena positions come from the picked arena"));
        State->Phase = 4; Sync(World);
        Check(Stage(World) == nullptr && CountActors(false) == Baseline, TEXT("recovery removes the arena without leftovers"));
        State->Phase = 1; ServerPrepare(Mode);
        Check(Mode->ArenaIndex != First || Rot.Num() < 2, TEXT("the next prep never repeats the previous arena"));
        State->Phase = 2; ServerBegin(Mode); State->Phase = 0; Sync(World);
        Check(Stage(World) == nullptr, TEXT("survival phase has no arena"));
        State->Phase = OldPhase; State->ArenaIndex = OldIndex; Mode->ArenaIndex = OldModeIndex; Sync(World);
    }
    UE_LOG(LogCireArenaTests, Display, TEXT("CIRE_ARENA_%s checks=%d arenas=%d rotation=%d"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Checks, P.Arenas.Num(), Rot.Num());
    return bPassed;
}
#endif
