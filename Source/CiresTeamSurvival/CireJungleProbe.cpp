// jungle-packs: -CireJungleProbe, the town jungle-pack probe (Tools/RunJunglePackProbe.py, Docs/JunglePacks.md).
// pack-formations: it first loads Eric's committed layout (Content/Data/MapLayout.json), applies it live and restarts the
// match on it the way Alt+F5 does (cycle 1), spawns the packs through the REAL schedule (ACireGameMode::SpawnPacks) and
// checks that every authored pack of every tier (T1..T4) spawns in both realms, in its preset formation, facing the path.
// Then, on the running map it places 40 packs of mixed tiers (1..4) and types (every race and Mixed, some with composition
// overrides) beside the march route in BOTH realms, applies them through the real live pipeline, spawns every pack and
// checks each one: the composition rules (3-6: 1-2 tanks, 1-2 healers, 1-3 DPS), exactly one leader, the tier's ability
// count on every member, members on the navmesh inside the pack radius, and the pack ids unique. It then lets the world
// run a few seconds (no errors) and logs CIRE_JUNGLE_PROBE_PASS / _FAIL.
#include "CireJunglePacks.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLoot.h"
#include "CireMapLayout.h"
#include "CireOutdoorBosses.h"
#include "Rules/CireItemRules.h"
#include "CireNav.h"
#include "CireNPCArchetypes.h"
#include "CireNPCState.h"
#include "CireTownMap.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireJungleProbe, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
struct FJungleProbe
{
    bool bEnabled = false, bDone = false, bPass = true;
    int32 Stage = 0, Packs = 0, LayoutPacks[2] = {0, 0};
    float Clock = 0, StageAt = 0;
    TArray<FString> Lines;
};
FJungleProbe J;
void Note(const FString& Line) { UE_LOG(LogCireJungleProbe, Display, TEXT("%s"), *Line); J.Lines.Add(Line); }
void Fail(const FString& Why) { J.bPass = false; UE_LOG(LogCireJungleProbe, Error, TEXT("CIRE_JUNGLE_PROBE_CHECK_FAIL %s"), *Why); J.Lines.Add(TEXT("FAIL ") + Why); }
void Finish()
{
    J.bDone = true;
    Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_%s packs=%d per realm"), J.bPass ? TEXT("PASS") : TEXT("FAIL"), J.Packs));
    FString Path;
    if (!FParse::Value(FCommandLine::Get(), TEXT("CireJungleProbeSummary="), Path)) Path = FPaths::ProjectSavedDir() / TEXT("JungleProbe/probe.txt");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(FString::Join(J.Lines, TEXT("\n")) + TEXT("\n"), *Path);
    FPlatformMisc::RequestExitWithStatus(false, J.bPass ? 0 : 1);
}
FVector2D Along(const TArray<FVector2D>& Points, double Fraction, FVector2D& OutDir)
{
    double Remaining = CireLanePath::PathLength(Points) * FMath::Clamp(Fraction, 0., 1.);
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
    {
        const double S = FVector2D::Distance(Points[I], Points[I + 1]);
        if (Remaining <= S) { OutDir = (Points[I + 1] - Points[I]).GetSafeNormal(); return FMath::Lerp(Points[I], Points[I + 1], S > 0 ? Remaining / S : 0.); }
        Remaining -= S;
    }
    OutDir = FVector2D(1, 0);
    return Points.Last();
}
/** 40 pack spots beside the primary route (on the navmesh in both realms, 3 m apart, clear of the breach and the goal). */
TArray<FVector2D> PackSpots(UWorld* World, const FCireBattlefieldRoutes& R, int32 Want)
{
    TArray<FVector2D> Out;
    const TArray<FVector2D>& Route = R.LocalPoints[0];
    const FVector2D GoalHalf = R.GoalSize * .5;
    for (int32 Step = 0; Step < 400 && Out.Num() < Want; ++Step)
    {
        FVector2D Dir;
        const FVector2D At = Along(Route, .08 + .84 * (Step % 100) / 100., Dir);
        const double Side = (Step % 2 ? 1. : -1.) * (500. + 250. * (Step / 100));
        FVector2D Candidate = At + FVector2D(-Dir.Y, Dir.X) * Side;
        bool bOk = true;
        for (int32 Realm = 0; Realm < 2 && bOk; ++Realm)
        {
            FVector Projected;
            bOk = CireNav::Project(World, CireLanePath::ToWorld(Realm, Candidate, 60.f), Projected, FVector(250, 250, 600), 45.f);
            if (bOk && Realm == 0) Candidate = CireLanePath::ToLocal(0, Projected);
        }
        if (!bOk || FVector2D::Distance(Candidate, Route[0]) < 900. || !CireLanePath::InsidePlayBounds(R, Candidate)) continue;
        if (FMath::Abs(Candidate.X - R.GoalCenter.X) <= GoalHalf.X + 600 && FMath::Abs(Candidate.Y - R.GoalCenter.Y) <= GoalHalf.Y + 600) continue;
        if (Out.ContainsByPredicate([&](const FVector2D& O) { return FVector2D::Distance(O, Candidate) < 300.; })) continue;
        Out.Add(Candidate);
    }
    return Out;
}
}

void CireJunglePacks::InitializeProbe(ACireGameMode* Mode)
{
    J = FJungleProbe();
    J.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireJungleProbe"));
    if (!J.bEnabled) return;
    Mode->BotFillTimer = 0;
    Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_READY town=%d"), CireTownMap::IsActive() ? 1 : 0));
}

bool CireJunglePacks::TickProbe(ACireGameMode* Mode, float Delta)
{
    if (!J.bEnabled || J.bDone || !Mode) return false;
    UWorld* World = Mode->GetWorld();
    auto* State = Mode->GetGameState<ACireGameState>();
    J.Clock += Delta;
    Mode->WaveTimer = 1.e6f; // no waves: the probe spawns the packs itself
    for (auto* H : Mode->Heroes) if (::IsValid(H) && !H->bBot) { H->Draft(2); H->bBot = true; H->bAutoAttack = false; H->HeroName = TEXT("Probe player"); }
    if (J.Stage == 0)
    {
        if (!Mode->bBotsFilled || !CireNav::IsReady(World) || !State || State->Phase != 0)
        {
            if (J.Clock > (CireTownMap::IsActive() ? 480.f : 90.f)) { Fail(TEXT("setup timed out")); Finish(); }
            return false;
        }
        for (auto* H : Mode->Heroes) if (::IsValid(H)) { H->bDrafted = false; H->Target = nullptr; H->bAutoAttack = false; }
        // pack-formations: Eric's committed layout, compiled over the route file exactly as a match (or Alt+F5) runs it.
        FCireMapLayout Layout; FString Error; FCireBattlefieldRoutes Base, Routes; TArray<FString> Notes;
        if (!CireMapLayout::Load(Layout, CireMapLayout::ActivePath(), &Error)) { Fail(TEXT("MapLayout.json does not load: ") + Error); J.Stage = 2; return false; }
        if (!CireMapLayout::MatchesActiveMap(Layout)) { Note(TEXT("CIRE_JUNGLE_PROBE_LAYOUT skipped: MapLayout.json belongs to another map")); J.Stage = 2; return false; }
        if (!CireLanePath::LoadFile(Base)) Base = CireLanePath::Get(World);
        if (!CireMapLayout::CompileRoutes(Layout, Base, Routes, Notes) || !CireLanePath::Validate(Routes, Error) || !CireLanePath::ApplyLive(World, Routes, &Error))
        { Fail(TEXT("Eric's layout does not compile / apply live: ") + Error); J.Stage = 2; return false; }
        int32 Authored[5] = {};
        for (int32 Realm = 0; Realm < 2; ++Realm) { J.LayoutPacks[Realm] = Routes.Bays[Realm].Num(); for (const FCireChallengeBay& B : Routes.Bays[Realm]) ++Authored[FMath::Clamp(B.Tier, 0, 4)]; }
        Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_LAYOUT %s packs=%d/%d authored tiers T1=%d T2=%d T3=%d T4=%d"), *Layout.Name, J.LayoutPacks[0], J.LayoutPacks[1],
            Authored[1], Authored[2], Authored[3], Authored[4]));
        if (Authored[1] == 0 || Authored[2] == 0 || Authored[3] == 0 || Authored[4] == 0) Fail(TEXT("Eric's layout should author packs of every tier (T1..T4)"));
        // Root cause, shown: the previous unlocks (T2 at cycle 1 wave 3, T3 at cycle 2, T4 at cycle 3) lock these packs at a restart.
        {
            Cires::Items::PackSchedule Old = CireLoot::Get().Schedule; Old.Bays.clear();
            Old.Bays.push_back({1, 1, 1, 1}); Old.Bays.push_back({2, 1, 3, 2}); Old.Bays.push_back({3, 2, 1, 3}); Old.Bays.push_back({4, 3, 1, 4});
            std::vector<int> Tiers; for (const FCireChallengeBay& B : Routes.Bays[0]) Tiers.push_back(B.Tier);
            const Cires::Items::PackSchedule Sched = Cires::Items::RouteSchedule(Old, Tiers);
            int32 OldSpawn[5] = {};
            for (const auto& Bay : Sched.Bays) if (Cires::Items::BayTier(Sched, Bay.Bay, 1, 1) > 0) ++OldSpawn[FMath::Clamp(Bay.BaseTier, 0, 4)];
            Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_ROOTCAUSE with the old unlocks cycle 1 wave 1 would spawn T1=%d T2=%d T3=%d T4=%d of realm 0"), OldSpawn[1], OldSpawn[2], OldSpawn[3], OldSpawn[4]));
        }
        // The Alt+F5 restart: clear the field, cycle 1, then the real pack spawner.
        for (int32 I = Mode->Monsters.Num() - 1; I >= 0; --I) if (::IsValid(Mode->Monsters[I])) Mode->Monsters[I]->Destroy();
        Mode->Monsters.Reset(); Mode->RewardedPacks.Reset();
        Mode->Clock = Cires::MatchClock(Mode->Clock.GetDurations());
        State->Round = 1; State->Wave = 0;
        Mode->SpawnPacks();
        J.Stage = 1; J.StageAt = J.Clock;
        return false;
    }
    if (J.Stage == 1 && J.Clock - J.StageAt >= 1.f)
    {
        const FCireBattlefieldRoutes& Routes = CireLanePath::Get(World);
        TMap<int32, TArray<ACireMonster*>> ByPack;
        for (ACireMonster* M : Mode->Monsters) if (::IsValid(M) && M->PackId >= 0 && !CireOutdoorBosses::IsOutdoorBoss(M)) ByPack.FindOrAdd(M->PackId).Add(M);
        int32 Spawned[2][5] = {}, Formed = 0, Faced = 0, Sizes[9] = {};
        TSet<int32> Seen[2];
        for (const auto& Pair : ByPack)
        {
            const int32 Realm = FMath::Clamp(Pair.Value[0]->Lane, 0, 1), Bay = CireProgression::PackBayOf(Pair.Key);
            if (!Routes.Bays[Realm].IsValidIndex(Bay - 1)) continue;
            Seen[Realm].Add(Bay);
            const FCireChallengeBay& Pack = Routes.Bays[Realm][Bay - 1];
            ++Spawned[Realm][FMath::Clamp(Pair.Value[0]->Tier, 0, 4)];
            if (Pair.Value[0]->Tier != Pack.Tier) Fail(FString::Printf(TEXT("realm %d pack %d spawned at T%d, authored T%d"), Realm, Bay, Pair.Value[0]->Tier, Pack.Tier));
            ++Sizes[FMath::Clamp(Pair.Value.Num(), 0, 8)];
            // Formation and facing: every member faces the pack's facing; tanks stand ahead of the healers along it.
            const FVector Center = CireLanePath::ChallengePosition(World, Realm, Bay, 0);
            const float Yaw = CireJunglePacks::FacingYaw(World, Realm, Center);
            const FVector Fwd = FRotator(0.f, Yaw, 0.f).Vector();
            float TankAhead = -1e9f, HealerAhead = -1e9f; bool bFacing = true;
            for (ACireMonster* M : Pair.Value)
            {
                const FCireNPCArchetype* A = M->NPCState ? M->NPCState->Archetype() : nullptr; if (!A) continue;
                const float Along = FVector::DotProduct(M->SpawnPosition - Center, Fwd);
                const ECirePackRole Role = RoleOf(*A);
                if (Role == ECirePackRole::Tank) TankAhead = FMath::Max(TankAhead, Along);
                if (Role == ECirePackRole::Healer) HealerAhead = FMath::Max(HealerAhead, Along);
                bFacing &= FMath::Abs(FMath::FindDeltaAngleDegrees(M->GetActorRotation().Yaw, Yaw)) < 30.f || M->bEngaged;
            }
            Formed += TankAhead > HealerAhead ? 1 : 0; Faced += bFacing ? 1 : 0;
        }
        for (int32 Realm = 0; Realm < 2; ++Realm)
            if (Seen[Realm].Num() != J.LayoutPacks[Realm]) Fail(FString::Printf(TEXT("realm %d: %d of Eric's %d packs spawned after the restart"), Realm, Seen[Realm].Num(), J.LayoutPacks[Realm]));
        for (int32 Tier = 1; Tier <= 4; ++Tier)
            if (Spawned[0][Tier] == 0 || Spawned[1][Tier] == 0) Fail(FString::Printf(TEXT("no T%d pack spawned from Eric's layout (realm 0: %d, realm 1: %d)"), Tier, Spawned[0][Tier], Spawned[1][Tier]));
        // Packs whose members were pushed off the navmesh to the centre can blur the rows; allow a few (logged).
        if (Formed < ByPack.Num() * 9 / 10) Fail(FString::Printf(TEXT("%d of %d packs do not stand tanks-in-front"), ByPack.Num() - Formed, ByPack.Num()));
        if (Faced < ByPack.Num()) Fail(FString::Printf(TEXT("%d of %d packs do not face their facing"), ByPack.Num() - Faced, ByPack.Num()));
        Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_LAYOUT_SPAWNED packs=%d realm0 T1..T4=%d/%d/%d/%d realm1 T1..T4=%d/%d/%d/%d sizes3-8=%d/%d/%d/%d/%d/%d formed=%d faced=%d"), ByPack.Num(),
            Spawned[0][1], Spawned[0][2], Spawned[0][3], Spawned[0][4], Spawned[1][1], Spawned[1][2], Spawned[1][3], Spawned[1][4], Sizes[3], Sizes[4], Sizes[5], Sizes[6], Sizes[7], Sizes[8], Formed, Faced));
        J.Stage = 2;
        return false;
    }
    if (J.Stage == 2)
    {
        if (!Mode->bBotsFilled || !CireNav::IsReady(World) || !State || State->Phase != 0)
        {
            if (J.Clock > (CireTownMap::IsActive() ? 480.f : 90.f)) { Fail(TEXT("setup timed out")); Finish(); }
            return false;
        }
        // Heroes stand aside (undrafted: packs ignore them), so nothing kills the packs while they are checked.
        for (auto* H : Mode->Heroes) if (::IsValid(H)) { H->bDrafted = false; H->Target = nullptr; H->bAutoAttack = false; }
        FCireBattlefieldRoutes Routes = CireLanePath::Get(World);
        const TArray<FVector2D> Spots = PackSpots(World, Routes, 40);
        if (Spots.Num() < 40) Fail(FString::Printf(TEXT("only %d pack spots found beside the route"), Spots.Num()));
        const TArray<FName> Types = PackTypes();
        Routes.Bays[0].Reset();
        for (int32 I = 0; I < Spots.Num(); ++I)
        {
            FCireChallengeBay B; B.Position = Spots[I]; B.Radius = 300.f + (I % 4) * 100.f; B.Tier = 1 + I % 4; B.PackType = Types[I % Types.Num()];
            // pack-formations: overrides of every size 3..8, some with caster / ranged / melee lines.
            if (I % 5 == 0) B.Comp = {1 + I % 3, 1 + (I / 5) % 2, (I / 10) % 3, I % 2, (I / 5) % 2, 1 + (I / 5) % 3};
            Routes.Bays[0].Add(B);
        }
        Routes.Bays[1] = Routes.Bays[0];
        FString Error;
        if (!CireLanePath::ApplyLive(World, Routes, &Error)) { Fail(TEXT("40 packs do not apply live: ") + Error); Finish(); return false; }
        J.Packs = Routes.Bays[0].Num();
        // Clear any scheduled pack spawns, then spawn every pack at its own tier in both realms.
        for (int32 I = Mode->Monsters.Num() - 1; I >= 0; --I) if (::IsValid(Mode->Monsters[I]) && Mode->Monsters[I]->PackId >= 0) { Mode->Monsters[I]->Destroy(); Mode->Monsters.RemoveAt(I); }
        for (int32 Realm = 0; Realm < 2; ++Realm)
            for (int32 Bay = 1; Bay <= J.Packs; ++Bay) CireProgression::SpawnBay(Mode, Realm, Bay, Routes.Bays[Realm][Bay - 1].Tier);
        J.Stage = 3; J.StageAt = J.Clock;
        return false;
    }
    if (J.Stage == 3 && J.Clock - J.StageAt >= 1.f)
    {
        TMap<int32, TArray<ACireMonster*>> ByPack;
        for (ACireMonster* M : Mode->Monsters) if (::IsValid(M) && M->PackId >= 0) ByPack.FindOrAdd(M->PackId).Add(M);
        if (ByPack.Num() != 2 * J.Packs) Fail(FString::Printf(TEXT("%d packs spawned, expected %d"), ByPack.Num(), 2 * J.Packs));
        int32 Monsters = 0, OffNav = 0, Tiers[5] = {}, Sizes[9] = {}, Casters = 0;
        TSet<FName> TypesSeen;
        for (const auto& Pair : ByPack)
        {
            const int32 Realm = Pair.Value.Num() ? Pair.Value[0]->Lane : 0;
            const int32 Bay = CireProgression::PackBayOf(Pair.Key);
            const FCireChallengeBay Pack = CireLanePath::BayAt(CireLanePath::Get(World), Realm, Bay);
            const FCirePackComposition Want = Pack.Composition();
            FCirePackComposition Got{0, 0, 0}; int32 Leaders = 0; bool bLoadouts = true, bInside = true, bRace = true;
            for (ACireMonster* M : Pair.Value)
            {
                ++Monsters;
                const FCireNPCArchetype* A = M->NPCState ? M->NPCState->Archetype() : nullptr;
                if (!A) { Fail(TEXT("a pack member has no archetype")); continue; }
                const ECirePackRole Role = RoleOf(*A);
                Casters += Role == ECirePackRole::Caster ? 1 : 0;
                Got.CountRef(IsDps(Role) ? ECirePackRole::Dps : Role) += 1;
                Leaders += M->GetNPCClassification() == ECireNPCClass::Boss ? 1 : 0;
                bLoadouts &= M->NPCState->Loadout.Num() == AbilityCount(Pack.Tier, KitSize(*A)) && M->Tier == Pack.Tier;
                bRace &= Pack.PackType == Mixed || A->RaceId == Pack.PackType;
                bInside &= FVector::Dist2D(M->GetActorLocation(), CireLanePath::ChallengePosition(World, Realm, Bay, 0)) <= Pack.Radius + 300.f;
                FVector OnNav; OffNav += CireNav::Project(World, M->GetActorLocation(), OnNav, FVector(120, 120, 300)) ? 0 : 1;
            }
            ++Tiers[FMath::Clamp(Pack.Tier, 0, 4)]; ++Sizes[FMath::Clamp(Got.Total(), 0, 8)]; TypesSeen.Add(Pack.PackType);
            const FString Tag = FString::Printf(TEXT("realm %d pack %d (T%d %s)"), Realm, Bay, Pack.Tier, *TypeLabel(Pack.PackType));
            if (!IsValid(Got) || Got.Tanks != Want.Tanks || Got.Healers != Want.Healers || Got.Dps != Want.DpsTotal())
                Fail(FString::Printf(TEXT("%s: composition %d/%d/%d, expected %d/%d/%d"), *Tag, Got.Tanks, Got.Healers, Got.Dps, Want.Tanks, Want.Healers, Want.DpsTotal()));
            if (Leaders != 1) Fail(FString::Printf(TEXT("%s has %d leaders"), *Tag, Leaders));
            if (!bLoadouts) Fail(Tag + TEXT(": a member does not use its tier's ability count"));
            if (!bRace) Fail(Tag + TEXT(": a member is outside the pack's race"));
            if (!bInside) Fail(Tag + TEXT(": a member stands outside the pack radius"));
        }
        if (OffNav > 0) Fail(FString::Printf(TEXT("%d pack members are off the navmesh"), OffNav));
        if (Sizes[7] + Sizes[8] == 0) Fail(TEXT("no pack of 7 or 8 spawned"));
        if (Casters == 0) Fail(TEXT("no ranged caster DPS spawned"));
        Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_SPAWNED packs=%d monsters=%d tiers=%d/%d/%d/%d sizes3-8=%d/%d/%d/%d/%d/%d casters=%d types=%d offnav=%d"), ByPack.Num(), Monsters,
            Tiers[1], Tiers[2], Tiers[3], Tiers[4], Sizes[3], Sizes[4], Sizes[5], Sizes[6], Sizes[7], Sizes[8], Casters, TypesSeen.Num(), OffNav));
        J.Stage = 4; J.StageAt = J.Clock;
        return false;
    }
    if (J.Stage == 4 && J.Clock - J.StageAt >= 5.f)
    {
        // The world ran with every pack alive: the packs are still there (nothing despawned or fell through the town).
        int32 Alive = 0; for (ACireMonster* M : Mode->Monsters) Alive += ::IsValid(M) && M->PackId >= 0 && M->Health > 0 ? 1 : 0;
        Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_SETTLED alive=%d"), Alive));
        if (Alive == 0) Fail(TEXT("no pack member survived the settle"));
        Finish();
    }
    return false;
}
#endif
