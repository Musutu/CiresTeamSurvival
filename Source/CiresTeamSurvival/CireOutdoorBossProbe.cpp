// outdoor-bosses: -CireOutdoorBossProbe (Tools/RunOutdoorBossProbe.py, Docs/OutdoorBosses.md). Eric's own MapLayout.json on
// the town (-CireUseMapLayout: the probe runs the layout a match runs, trimmed to the Play Bounds), in BOTH realms:
//   1. every Boss and Challenge Pack marker projects onto the navmesh exactly like the editor's VALIDATE checks it, and
//      VALIDATE (with its navmesh checks) raises nothing about those markers;
//   2. each boss lair's leash area (MonsterLeash.json boss radius, inside the Play Bounds) is walkable navmesh, and the lair
//      is reachable from the realm's player spawn (not a sealed island);
//   3. one world boss per Boss marker per realm, each a different race boss, the same boss on a marker in both realms,
//      neutral, standing on the navmesh at its marker;
//   4. a kill pays the boss bounty and the boss comes back after its respawn time (-CireOutdoorBossRespawn=5).
// Logs CIRE_OUTDOOR_BOSS_PROBE_PASS / _FAIL.
#include "CireOutdoorBosses.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLayoutEditorState.h"
#include "CireLeash.h"
#include "CireLoot.h"
#include "CireMapLayout.h"
#include "CireNav.h"
#include "CireNPCState.h"
#include "CireRouteEditor.h"
#include "CireTownMap.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireOutdoorBossProbe, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
namespace ML = CireMapLayout;
struct FBossProbe
{
    bool bEnabled = false, bDone = false, bPass = true;
    int32 Stage = 0, Gold = 0, Bounty = 0, PackId = -1;
    FName KilledBoss;
    float Clock = 0, StageAt = 0;
    TArray<FString> Lines;
};
FBossProbe P;
void Note(const FString& Line) { UE_LOG(LogCireOutdoorBossProbe, Display, TEXT("%s"), *Line); P.Lines.Add(Line); }
void Fail(const FString& Why) { P.bPass = false; UE_LOG(LogCireOutdoorBossProbe, Error, TEXT("CIRE_OUTDOOR_BOSS_PROBE_CHECK_FAIL %s"), *Why); P.Lines.Add(TEXT("FAIL ") + Why); }
void Finish()
{
    P.bDone = true;
    Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_%s"), P.bPass ? TEXT("PASS") : TEXT("FAIL")));
    FString Path;
    if (!FParse::Value(FCommandLine::Get(), TEXT("CireOutdoorBossProbeSummary="), Path)) Path = FPaths::ProjectSavedDir() / TEXT("OutdoorBossProbe/probe.txt");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
    FFileHelper::SaveStringToFile(FString::Join(P.Lines, TEXT("\n")) + TEXT("\n"), *Path);
    FPlatformMisc::RequestExitWithStatus(false, P.bPass ? 0 : 1);
}
ACireMonster* BossAt(ACireGameMode* Mode, int32 Realm, int32 Index)
{
    const int32 Id = CireOutdoorBosses::PackIdFor(Realm, Index);
    for (ACireMonster* M : Mode->Monsters) if (IsValid(M) && M->PackId == Id && M->Health > 0) return M;
    return nullptr;
}

/** Stage 0: the layout on the navmesh, the lairs, and the spawned bosses. */
void CheckWorld(ACireGameMode* Mode)
{
    UWorld* World = Mode->GetWorld();
    const FCireBattlefieldRoutes& Routes = CireLanePath::Get(World);
    FCireMapLayout Layout; FString Error;
    if (!ML::Load(Layout, ML::ActivePath(), &Error)) { Fail(TEXT("MapLayout.json does not load: ") + Error); return; }
    if (!CireLanePath::ActiveSource().Contains(TEXT("MapLayout"))) Fail(TEXT("the match does not run MapLayout.json: ") + CireLanePath::ActiveSource());
    Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_REALM bounds x %.0f..%.0f y +-%.0f playBounds=%d trimmed layout=\"%s\""), Routes.MinX, Routes.MaxX, Routes.HalfWidth, Routes.PlayBounds.Num(), *Layout.Name));
    // 1. Every Boss and Challenge Pack marker on the navmesh of each realm it shows in (the editor's exact test).
    int32 Markers[2] = {0, 0}, OffNav = 0, Bosses[2] = {0, 0};
    for (const FCireMapMarker& M : Layout.Markers)
    {
        if (M.Type != ML::BossSpawn && M.Type != ML::ChallengePack) continue;
        for (int32 Realm = 0; Realm < 2; ++Realm)
        {
            if (!ML::ShownInRealm(M, Realm)) continue;
            ++Markers[Realm]; Bosses[Realm] += M.Type == ML::BossSpawn ? 1 : 0;
            if (!CireLayoutEditor::MarkerOnNavmesh(World, Realm, M.Position))
            {
                ++OffNav;
                Fail(FString::Printf(TEXT("%s (%s) at (%.0f,%.0f) is off the navmesh"), *ML::DisplayLabel(Layout, M), *ML::RealmName(Realm), M.Position.X, M.Position.Y));
            }
        }
    }
    Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_MARKERS realm0=%d realm1=%d (boss %d/%d) offnav=%d"), Markers[0], Markers[1], Bosses[0], Bosses[1], OffNav));
    if (Bosses[0] == 0 || Bosses[1] == 0) Fail(TEXT("the layout has no Boss markers in a realm"));
    // VALIDATE with its navmesh checks (what Eric presses in the editor): nothing about a boss or a pack marker.
    const FCireLayoutChecks Checks = CireLayoutEditor::WorldChecks(World, Layout, true);
    int32 Errors = 0, Flagged = 0;
    for (const FCireLayoutIssue& I : ML::Validate(Layout, &Checks))
    {
        const FCireMapMarker* M = I.MarkerId.IsEmpty() ? nullptr : ML::Find(Layout, I.MarkerId);
        Errors += I.bError ? 1 : 0;
        const bool bOurs = M && (M->Type == ML::BossSpawn || M->Type == ML::ChallengePack) && I.bError;
        Flagged += bOurs ? 1 : 0;
        if (bOurs) Fail(TEXT("VALIDATE: ") + I.Message);
        else if (I.bError) Note(TEXT("CIRE_OUTDOOR_BOSS_PROBE_VALIDATE_OTHER ") + I.Message);
    }
    Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_VALIDATE errors=%d boss_or_pack=%d"), Errors, Flagged));
    // 2. Each lair's leash area: navmesh wherever the Play Bounds allow, and a real path from the realm's player spawn.
    const float Radius = CireLeash::Rules().RadiusBoss - CireLeash::Rules().PursuitMargin;
    for (int32 Realm = 0; Realm < 2; ++Realm)
    {
        const FVector From = CireLanePath::PlayerSpawnTransform(World, Realm, 0).GetLocation();
        for (int32 Index = 0; Index < Routes.Bosses[Realm].Num(); ++Index)
        {
            const FCireRouteSpot& Spot = Routes.Bosses[Realm][Index];
            int32 Samples = 0, Walkable = 0;
            for (const float Ring : {Radius * .3f, Radius * .65f, Radius})
                for (int32 A = 0; A < 16; ++A)
                {
                    const FVector2D At = Spot.Position + FVector2D(FMath::Cos(A * PI / 8), FMath::Sin(A * PI / 8)) * Ring;
                    if (!CireLanePath::InsidePlayBounds(Routes, At)) continue; // the trim cuts the world there on purpose
                    ++Samples;
                    FVector Out; Walkable += CireNav::Project(World, CireLanePath::ToWorld(Realm, At, 60.f), Out, FVector(200, 200, 500), 40.f) ? 1 : 0;
                }
            float Length = 0;
            FVector Lair; const bool bLair = CireNav::Project(World, CireLanePath::ToWorld(Realm, Spot.Position, 60.f), Lair, FVector(250, 250, 800), 40.f);
            const ECireRouteReach Reach = bLair ? CireRouteEditor::Reach(World, From, Lair, Length) : ECireRouteReach::None;
            const float Coverage = Samples ? 100.f * Walkable / Samples : 0.f;
            Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_LAIR realm=%d marker=\"%s\" boss=%s at=(%.0f,%.0f) lair_on_nav=%d leash_coverage=%.0f%% (%d/%d) reach=%s path=%.0fm"), Realm, *Spot.Name,
                *CireOutdoorBosses::ResolveSpot(Routes, Realm, Index).ToString(), Spot.Position.X, Spot.Position.Y, bLair ? 1 : 0, Coverage, Walkable, Samples,
                Reach == ECireRouteReach::Direct ? TEXT("direct") : Reach == ECireRouteReach::Detour ? TEXT("detour") : Reach == ECireRouteReach::Partial ? TEXT("PARTIAL") : TEXT("NONE"), Length / 100.f));
            if (!bLair) Fail(FString::Printf(TEXT("realm %d %s: the lair is off the navmesh"), Realm, *Spot.Name));
            if (Coverage < 60.f) Fail(FString::Printf(TEXT("realm %d %s: only %.0f%% of its leash area is walkable"), Realm, *Spot.Name, Coverage));
            if (Reach != ECireRouteReach::Direct && Reach != ECireRouteReach::Detour) Fail(FString::Printf(TEXT("realm %d %s: no path from the player spawn to the lair"), Realm, *Spot.Name));
        }
    }
    // 3. The world bosses themselves.
    if (Routes.Bosses[0].Num() != Bosses[0] || Routes.Bosses[1].Num() != Bosses[1]) Fail(TEXT("the match did not compile every Boss marker"));
    for (int32 Realm = 0; Realm < 2; ++Realm)
    {
        TSet<FName> Seen;
        for (int32 Index = 0; Index < Routes.Bosses[Realm].Num(); ++Index)
        {
            ACireMonster* M = BossAt(Mode, Realm, Index);
            const FString Tag = FString::Printf(TEXT("realm %d %s"), Realm, *Routes.Bosses[Realm][Index].Name);
            if (!M) { Fail(Tag + TEXT(": no world boss")); continue; }
            const FName Id = M->NPCState ? M->NPCState->ArchetypeId : NAME_None;
            if (Seen.Contains(Id)) Fail(Tag + TEXT(": the same boss as another marker"));
            Seen.Add(Id);
            ACireMonster* Twin = BossAt(Mode, 1 - Realm, Index);
            if (!Twin || !Twin->NPCState || Twin->NPCState->ArchetypeId != Id) Fail(Tag + TEXT(": the other realm holds a different boss on this marker"));
            FVector Out;
            const bool bNav = CireNav::Project(World, M->GetActorLocation(), Out, FVector(150, 150, 600), 40.f);
            const double Off = FVector2D::Distance(CireLanePath::ToLocal(Realm, M->GetActorLocation()), Routes.Bosses[Realm][Index].Position);
            if (!bNav) Fail(Tag + TEXT(": the boss stands off the navmesh"));
            if (Off > 600.) Fail(FString::Printf(TEXT("%s: the boss stands %.0f cm from its marker"), *Tag, Off));
            if (!M->bNeutral || M->IsLaneBoss() || !M->bHomeLeash) Fail(Tag + TEXT(": not a neutral, lair-leashed world boss"));
            Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_BOSS realm=%d marker=\"%s\" boss=%s name=\"%s\" health=%.0f damage=%.0f onnav=%d off_marker=%.0f"), Realm,
                *Routes.Bosses[Realm][Index].Name, *Id.ToString(), *M->GetNPCDisplayName(), M->MaxHealth, M->Damage, bNav ? 1 : 0, Off));
        }
    }
}
}

void CireOutdoorBosses::InitializeProbe(ACireGameMode* Mode)
{
    P = FBossProbe();
    P.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireOutdoorBossProbe"));
    if (!P.bEnabled) return;
    Mode->BotFillTimer = 0;
    Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_READY town=%d"), CireTownMap::IsActive() ? 1 : 0));
}

void CireOutdoorBosses::TickProbe(ACireGameMode* Mode, float Delta)
{
    if (!P.bEnabled || P.bDone || !Mode) return;
    UWorld* World = Mode->GetWorld();
    auto* State = Mode->GetGameState<ACireGameState>();
    P.Clock += Delta;
    Mode->WaveTimer = 1.e6f; // no waves: only the world bosses and the packs
    // The probe's player drafts as a bot so the match fills; once the checks start every hero stands aside (undrafted).
    for (auto* H : Mode->Heroes) if (::IsValid(H) && !H->bBot) { H->Draft(2); H->bBot = true; H->bAutoAttack = false; H->HeroName = TEXT("Probe player"); }
    if (P.Stage > 0 || (Mode->bBotsFilled && CireNav::IsReady(World)))
        for (auto* H : Mode->Heroes) if (::IsValid(H)) { H->bDrafted = false; H->Target = nullptr; H->bAutoAttack = false; }
    if (P.Stage == 0)
    {
        if (!Mode->bBotsFilled || !CireNav::IsReady(World) || !State || State->Phase != 0)
        {
            if (P.Clock > (CireTownMap::IsActive() ? 900.f : 120.f)) { Fail(TEXT("setup timed out")); Finish(); }
            return;
        }
        CheckWorld(Mode);
        // 4. A kill: the boss bounty to the killer, then the respawn.
        ACireMonster* Boss = BossAt(Mode, 0, 0);
        ACireHero* Killer = nullptr;
        for (ACireHero* H : Mode->Heroes) if (::IsValid(H) && H->TeamId == 0) { Killer = H; break; }
        if (!Boss || !Killer) { Fail(TEXT("no boss or hero for the kill check")); Finish(); return; }
        P.PackId = Boss->PackId; P.KilledBoss = Boss->NPCState ? Boss->NPCState->ArchetypeId : NAME_None;
        Killer->bDrafted = true; Killer->SetActorLocation(Boss->GetActorLocation() + FVector(300, 0, 0));
        P.Bounty = CireLoot::KillBounty(Mode, Boss);
        const int32 Value = Cires::Items::KillGold(CireLoot::Get().Economy, Cires::Items::BountyKind::Boss, CireLoot::BountyWave(Mode, Boss));
        P.Gold = Killer->Gold;
        Boss->Health = 0.f;
        Mode->MonsterKilled(Boss, Killer);
        Boss->Destroy();
        const int32 Paid = Killer->Gold - P.Gold;
        Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_KILL boss=%s bounty=%d paid=%d (bounty + auto-looted boss loot) boss_value=%d respawn_in=%.0fs"), *P.KilledBoss.ToString(), P.Bounty, Paid,
            Value, CireOutdoorBosses::RespawnIn(Mode, P.PackId)));
        // The probe's killer is a bot: bots auto-loot their personal chest, so its gold is the bounty plus the boss loot's gold.
        if (P.Bounty <= 0 || P.Bounty != Value || Paid < P.Bounty) Fail(FString::Printf(TEXT("the killer was paid %d, the boss bounty is %d (boss value %d)"), Paid, P.Bounty, Value));
        if (CireOutdoorBosses::RespawnIn(Mode, P.PackId) <= 0.f) Fail(TEXT("the slain boss has no respawn timer"));
        P.Stage = 1; P.StageAt = P.Clock;
        return;
    }
    if (P.Stage == 1)
    {
        for (ACireMonster* M : Mode->Monsters)
            if (::IsValid(M) && M->PackId == P.PackId && M->Health > 0)
            {
                const bool bSame = M->NPCState && M->NPCState->ArchetypeId == P.KilledBoss;
                Note(FString::Printf(TEXT("CIRE_OUTDOOR_BOSS_PROBE_RESPAWN after=%.1fs same=%d neutral=%d"), P.Clock - P.StageAt, bSame ? 1 : 0, M->bNeutral ? 1 : 0));
                if (!bSame || !M->bNeutral) Fail(TEXT("the boss came back different or hostile"));
                Finish();
                return;
            }
        if (P.Clock - P.StageAt > CireOutdoorBosses::Rules().RespawnSeconds + 10.f) { Fail(TEXT("the slain boss did not come back")); Finish(); }
    }
}
#endif
