// jungle-packs: -CireJungleProbe, the town jungle-pack probe (Tools/RunJunglePackProbe.py, Docs/JunglePacks.md).
// On the running map it places 40 packs of mixed tiers (1..4) and types (every race and Mixed, some with composition
// overrides) beside the march route in BOTH realms, applies them through the real live pipeline, spawns every pack and
// checks each one: the composition rules (3-6: 1-2 tanks, 1-2 healers, 1-3 DPS), exactly one leader, the tier's ability
// count on every member, members on the navmesh inside the pack radius, and the pack ids unique. It then lets the world
// run a few seconds (no errors) and logs CIRE_JUNGLE_PROBE_PASS / _FAIL.
#include "CireJunglePacks.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLoot.h"
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
    int32 Stage = 0, Packs = 0;
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
            if (I % 5 == 0) B.Comp = {2, 1 + I % 2, 3 - I % 2};
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
        J.Stage = 1; J.StageAt = J.Clock;
        return false;
    }
    if (J.Stage == 1 && J.Clock - J.StageAt >= 1.f)
    {
        TMap<int32, TArray<ACireMonster*>> ByPack;
        for (ACireMonster* M : Mode->Monsters) if (::IsValid(M) && M->PackId >= 0) ByPack.FindOrAdd(M->PackId).Add(M);
        if (ByPack.Num() != 2 * J.Packs) Fail(FString::Printf(TEXT("%d packs spawned, expected %d"), ByPack.Num(), 2 * J.Packs));
        int32 Monsters = 0, OffNav = 0, Tiers[5] = {}, Sizes[7] = {};
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
                (Role == ECirePackRole::Tank ? Got.Tanks : Role == ECirePackRole::Healer ? Got.Healers : Got.Dps) += 1;
                Leaders += M->GetNPCClassification() == ECireNPCClass::Boss ? 1 : 0;
                bLoadouts &= M->NPCState->Loadout.Num() == AbilityCount(Pack.Tier, KitSize(*A)) && M->Tier == Pack.Tier;
                bRace &= Pack.PackType == Mixed || A->RaceId == Pack.PackType;
                bInside &= FVector::Dist2D(M->GetActorLocation(), CireLanePath::ChallengePosition(World, Realm, Bay, 0)) <= Pack.Radius + 300.f;
                FVector OnNav; OffNav += CireNav::Project(World, M->GetActorLocation(), OnNav, FVector(120, 120, 300)) ? 0 : 1;
            }
            ++Tiers[FMath::Clamp(Pack.Tier, 0, 4)]; ++Sizes[FMath::Clamp(Got.Total(), 0, 6)]; TypesSeen.Add(Pack.PackType);
            const FString Tag = FString::Printf(TEXT("realm %d pack %d (T%d %s)"), Realm, Bay, Pack.Tier, *TypeLabel(Pack.PackType));
            if (!IsValid(Got) || Got != Want) Fail(FString::Printf(TEXT("%s: composition %d/%d/%d, expected %d/%d/%d"), *Tag, Got.Tanks, Got.Healers, Got.Dps, Want.Tanks, Want.Healers, Want.Dps));
            if (Leaders != 1) Fail(FString::Printf(TEXT("%s has %d leaders"), *Tag, Leaders));
            if (!bLoadouts) Fail(Tag + TEXT(": a member does not use its tier's ability count"));
            if (!bRace) Fail(Tag + TEXT(": a member is outside the pack's race"));
            if (!bInside) Fail(Tag + TEXT(": a member stands outside the pack radius"));
        }
        if (OffNav > 0) Fail(FString::Printf(TEXT("%d pack members are off the navmesh"), OffNav));
        Note(FString::Printf(TEXT("CIRE_JUNGLE_PROBE_SPAWNED packs=%d monsters=%d tiers=%d/%d/%d/%d sizes3-6=%d/%d/%d/%d types=%d offnav=%d"), ByPack.Num(), Monsters,
            Tiers[1], Tiers[2], Tiers[3], Tiers[4], Sizes[3], Sizes[4], Sizes[5], Sizes[6], TypesSeen.Num(), OffNav));
        J.Stage = 2; J.StageAt = J.Clock;
        return false;
    }
    if (J.Stage == 2 && J.Clock - J.StageAt >= 5.f)
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
