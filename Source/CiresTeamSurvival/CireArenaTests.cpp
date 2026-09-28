// Native arena checks (run by the combat expansion suite, -CireCombatExpansionProbe).
#include "CireArenas.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireArenaPortal.h"
#include "CireEffects.h"
#include "CireBuffs.h"
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

// arena-flow: schedule adapter, prep/countdown data, killing-blow gold, the win/loss rewards and the team buff icons.
bool CireArenaFlow::RunTests(ACireGameMode* Mode)
{
    bool bPassed = true; int32 Checks = 0;
    auto Check = [&](bool bOk, const FString& Label)
    {
        ++Checks;
        if (!bOk) { bPassed = false; UE_LOG(LogCireArenaTests, Error, TEXT("CIRE_ARENA_FLOW_CHECK_FAIL %s"), *Label); }
    };
    UWorld* World = Mode ? Mode->GetWorld() : nullptr;
    auto* State = Mode ? Mode->GetGameState<ACireGameState>() : nullptr;
    if (!World || !State) { UE_LOG(LogCireArenaTests, Error, TEXT("CIRE_ARENA_FLOW_TESTS_FAIL no mode")); return false; }
    const CireArenaPortal::FConfig& C = CireArenaPortal::Config();
    Check(FMath::IsNearlyEqual(C.PrepSeconds, 30.f) && FMath::IsNearlyEqual(C.CountdownSeconds, 7.f), TEXT("30 s prep, then a 7 s countdown (Arenas.json flow)"));
    Check(C.KillGold == 50 && C.WinGold == 250 && FMath::IsNearlyEqual(C.PvEBuffPercent, 15.f) && FMath::IsNearlyEqual(C.PvEDebuffPercent, 15.f), TEXT("reward data: 50 g kill, 250 g win, +/-15% PvE"));
    Check(PvPAfterWaves(World) == TArray<int32>({5, 10, 15, 20}), TEXT("fallback PvP schedule is 5/10/15/20"));
    Check(IsPvPAfterWave(World, 5) && IsPvPAfterWave(World, 20) && !IsPvPAfterWave(World, 7) && !IsPvPAfterWave(World, 25), TEXT("schedule adapter answers per wave"));

    // Team buff / debuff: stacks, multiplier, icons.
    const int32 Saved[4] = {State->EmberArenaBuffs, State->EmberArenaDebuffs, State->DuskArenaBuffs, State->DuskArenaDebuffs};
    const Cires::TeamRewards SavedRewards[2] = {Mode->Rewards[0], Mode->Rewards[1]};
    State->EmberArenaBuffs = State->EmberArenaDebuffs = State->DuskArenaBuffs = State->DuskArenaDebuffs = 0;
    Check(FMath::IsNearlyEqual(PvEDamageMultiplier(World, 0), 1.f) && FMath::IsNearlyEqual(PvEDamageMultiplier(World, 1), 1.f), TEXT("no arena yet: PvE damage x1"));
    TArray<ACireHero*> Team[2];
    for (ACireHero* H : Mode->Heroes) if (IsValid(H) && H->bDrafted && H->TeamId >= 0 && H->TeamId < 2) Team[H->TeamId].Add(H);
    TMap<ACireHero*, int32> Gold; for (ACireHero* H : Mode->Heroes) if (IsValid(H)) Gold.Add(H, H->Gold);
    const FString Text = AwardResult(Mode, 0, false);
    Check(State->EmberArenaBuffs == 1 && State->DuskArenaDebuffs == 1 && State->EmberArenaDebuffs == 0 && State->DuskArenaBuffs == 0, TEXT("a win stacks the buff on the winners and the debuff on the losers"));
    Check(FMath::IsNearlyEqual(PvEDamageMultiplier(World, 0), 1.15f) && FMath::IsNearlyEqual(PvEDamageMultiplier(World, 1), .85f), TEXT("+15% / -15% damage to monsters"));
    bool bSplit = true;
    for (ACireHero* H : Team[0]) bSplit &= H->Gold - Gold[H] == 250 / Team[0].Num();
    for (ACireHero* H : Team[1]) bSplit &= H->Gold == Gold[H];
    Check(bSplit, FString::Printf(TEXT("250 g split across the %d winners, nothing for the losers"), Team[0].Num()));
    Check(Text.Contains(TEXT("EMBER won")), TEXT("result announcement names the winner"));
    AwardResult(Mode, 0, false);
    Check(State->EmberArenaBuffs == 2 && FMath::IsNearlyEqual(PvEDamageMultiplier(World, 0), 1.30f) && FMath::IsNearlyEqual(PvEDamageMultiplier(World, 1), .70f), TEXT("the buff and debuff stack"));
    AwardResult(Mode, -1, false);
    Check(State->EmberArenaBuffs == 2 && State->DuskArenaDebuffs == 2, TEXT("a draw changes nothing"));
    const FCireEffectInfo* Victor = CireEffects::Find(VictorId); const FCireEffectInfo* Vanquished = CireEffects::Find(VanquishedId);
    Check(Victor && !Victor->IsHarmful() && Vanquished && Vanquished->IsHarmful(), TEXT("buff rows: Arena Victor (buff), Arena Vanquished (debuff)"));
    // Icons and killing blows do not need a drafted champion: any champion on each side will do.
    TArray<ACireHero*> Side[2];
    for (ACireHero* H : Mode->Heroes) if (IsValid(H) && !H->bDead && H->TeamId >= 0 && H->TeamId < 2) Side[H->TeamId].Add(H);
    if (Side[0].Num() > 0 && Side[1].Num() > 0)
    {
        TArray<FCireActiveEffect> Effects;
        CireEffects::Gather(Side[0][0], CireBuffs::ServerNow(World), Effects);
        const FCireActiveEffect* Mine = Effects.FindByPredicate([](const FCireActiveEffect& E) { return E.Id == VictorId; });
        Check(Mine && Mine->Stacks == 2 && !Effects.ContainsByPredicate([](const FCireActiveEffect& E) { return E.Id == VanquishedId; }), TEXT("winners show the Arena Victor icon with 2 stacks"));
        CireEffects::Gather(Side[1][0], CireBuffs::ServerNow(World), Effects);
        const FCireActiveEffect* Theirs = Effects.FindByPredicate([](const FCireActiveEffect& E) { return E.Id == VanquishedId; });
        Check(Theirs && Theirs->Stacks == 2, TEXT("losers show the Arena Vanquished icon with 2 stacks"));

        // Killing blow: +50 g in the arena only, only for an enemy champion.
        const Cires::MatchClock SavedClock = Mode->Clock;
        ACireHero* A = Side[0][0]; ACireHero* B = Side[1][0];
        const int32 GoldA = A->Gold, GoldB = B->Gold;
        const FString NoticeA = A->Notice;
        OnHeroKilled(Mode, B, A);
        Check(A->Gold == GoldA, TEXT("no killing-blow gold outside the arena"));
        Mode->Clock = Cires::MatchClock({60, 90, 15}); Mode->Clock.BeginIntermission(); Mode->Clock.Advance(60);
        Check(Mode->Clock.Phase() == Cires::MatchPhase::Arena, TEXT("fixture: arena clock"));
        OnHeroKilled(Mode, B, A);
        Check(A->Gold == GoldA + 50, TEXT("+50 g to the champion who lands the killing blow"));
        OnHeroKilled(Mode, A, A); OnHeroKilled(Mode, B, nullptr);
        Check(A->Gold == GoldA + 50 && B->Gold == GoldB, TEXT("no gold for suicides or unknown killers"));
        Check(KillerHero(A) == A && KillerHero(nullptr) == nullptr, TEXT("killer resolution"));
        Mode->Clock = SavedClock; A->Gold = GoldA; A->Notice = NoticeA;
    }
    else UE_LOG(LogCireArenaTests, Display, TEXT("CIRE_ARENA_FLOW_NOTE no champion on both teams: icon and kill checks skipped"));
    for (auto& Pair : Gold) if (IsValid(Pair.Key)) Pair.Key->Gold = Pair.Value;
    State->EmberArenaBuffs = Saved[0]; State->EmberArenaDebuffs = Saved[1]; State->DuskArenaBuffs = Saved[2]; State->DuskArenaDebuffs = Saved[3];
    Mode->Rewards[0] = SavedRewards[0]; Mode->Rewards[1] = SavedRewards[1];
    UE_LOG(LogCireArenaTests, Display, TEXT("CIRE_ARENA_FLOW_TESTS_%s checks=%d"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Checks);
    return bPassed;
}
#endif
