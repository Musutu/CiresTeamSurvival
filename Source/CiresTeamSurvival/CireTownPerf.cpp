// town-perf: -CireTownPerfProbe (CireTownPerf.h).
#include "CireTownPerf.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireNav.h"
#include "CireNPCCombat.h"
#include "CireTownMap.h"
#include "CireActorIterator.h"
#include "CireDraftStage.h"
#include "Containers/Ticker.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/HUD.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "DynamicRHI.h"
#include "Engine/LevelStreaming.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTownPerf, Log, All);

namespace
{
constexpr int32 MonstersPerRealm = 24;
constexpr double WarmSeconds = 5.0, MeasureSeconds = 30.0;
constexpr double TargetAvgFps = 60.0, TargetP95Ms = 1000.0 / 30.0, TargetPlayableSeconds = 60.0;

struct FSample { double Frame = 0, Game = 0, Render = 0, RHI = 0, GPU = 0; };
struct FRealmStats { int32 Team = 0; TArray<FSample> Samples; int32 Monsters = 0, Heroes = 0; FString Shot; FString Nav; };
struct FPerf
{
    bool bEnabled = false, bDone = false;
    TWeakObjectPtr<UWorld> World;
    FTSTicker::FDelegateHandle Ticker;
    int32 Stage = 0;              // 0 wait playable, 1 wait bots, 2 realm 0, 3 realm 1, 4 done
    double StageAt = 0, LastReal = 0, PlayableSeconds = -1, NextTopUp = 0, WaitStarted = 0;
    bool bShot = false, bProfiled = false;
    int32 DraftStagesLeft = 0; // the champion-select preview must be gone once everyone is drafted
    FRealmStats Realms[2];
    FString Directory;
    TArray<FString> Lines;
};
FPerf GPerf;

void Note(const FString& Line) { UE_LOG(LogCireTownPerf, Display, TEXT("%s"), *Line); GPerf.Lines.Add(Line); }
double Ms(uint32 Cycles) { return FPlatformTime::ToMilliseconds(Cycles); }
double Pct(TArray<double> V, double P)
{
    if (V.IsEmpty()) return 0;
    V.Sort();
    return V[FMath::Clamp(FMath::FloorToInt(P * (V.Num() - 1)), 0, V.Num() - 1)];
}
ACireHero* LocalHero(UWorld* World)
{
    APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
    return PC ? Cast<ACireHero>(PC->GetPawn()) : nullptr;
}
void TopUp(ACireGameMode* Mode)
{
    // A real fight around each realm's hero base: monsters on that realm's lane, spawned on the last stretch of the
    // march route so the bots defend and the column keeps marching past the champions.
    UWorld* World = Mode->GetWorld();
    static const FName Mix[] = {TEXT("hollow_infantry"), TEXT("ironbound_bruiser"), TEXT("hollow_shieldbearer"), TEXT("blight_caster"), TEXT("barbed_hunter"), TEXT("hollow_infantry")};
    for (int32 Team = 0; Team < 2; ++Team)
    {
        int32 Alive = 0;
        for (ACireMonster* M : Mode->Monsters) if (IsValid(M) && M->Lane == Team && M->Health > 0 && M->PackId < 0) ++Alive;
        const TArray<FVector> Route = CireLanePath::RoutePoints(World, Team, 110.f);
        if (Route.Num() < 2) continue;
        const FVector From = Route[FMath::Max(0, Route.Num() - 3)];
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        for (int32 I = Alive; I < MonstersPerRealm; ++I)
        {
            const FVector At = CireLanePath::ClampToLane(World, Team, From + FVector((I / 6) * 170.f, (I % 6 - 2.5f) * 150.f, 0), 80);
            if (auto* M = World->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), At, FRotator::ZeroRotator, Params))
            {
                M->Lane = Team;
                CireNPCCombat::ConfigureArchetype(M, Mix[I % UE_ARRAY_COUNT(Mix)], 3, 0, 1, false);
                Mode->Monsters.Add(M);
            }
        }
    }
}
void MoveLocalHero(ACireGameMode* Mode, int32 Team)
{
    ACireHero* H = LocalHero(Mode->GetWorld());
    if (!H) return;
    H->TeamId = Team; H->HomePosition = Mode->BasePosition(Team);
    H->GetCharacterMovement()->StopMovementImmediately();
    H->SetActorLocation(H->HomePosition, false, nullptr, ETeleportType::TeleportPhysics);
}
void Report()
{
    bool bPass = GPerf.PlayableSeconds >= 0 && GPerf.PlayableSeconds <= TargetPlayableSeconds;
    FString Why;
    if (!bPass) Why += FString::Printf(TEXT(" playable=%.1fs>%.0fs"), GPerf.PlayableSeconds, TargetPlayableSeconds);
    FString Summary = FString::Printf(TEXT("playable_s=%.1f realms_ms=%.0f nav_build_ms=%.0f"), GPerf.PlayableSeconds, CireTownMap::LastLoadMs(), CireNav::Stats(GPerf.World.Get()).InitialBuildMs);
    for (const FRealmStats& R : GPerf.Realms)
    {
        TArray<double> Frames; double Sum = 0, Game = 0, Render = 0, RHI = 0, GPU = 0, Max = 0;
        for (const FSample& S : R.Samples) { Frames.Add(S.Frame); Sum += S.Frame; Game += S.Game; Render += S.Render; RHI += S.RHI; GPU += S.GPU; Max = FMath::Max(Max, S.Frame); }
        const double N = FMath::Max(1, R.Samples.Num()), Avg = Sum / N, P95 = Pct(Frames, .95), Fps = Avg > 0 ? 1000.0 / Avg : 0;
        Note(FString::Printf(TEXT("CIRE_TOWN_PERF_REALM team=%d realm=%s frames=%d fps_avg=%.1f frame_avg_ms=%.2f frame_p95_ms=%.2f fps_p95=%.1f frame_max_ms=%.1f game_ms=%.2f render_ms=%.2f rhi_ms=%.2f gpu_ms=%.2f heroes=%d monsters=%d %s shot=%s"),
            R.Team, *CireTownMap::Def().Lighting[R.Team].Name, R.Samples.Num(), Fps, Avg, P95, P95 > 0 ? 1000.0 / P95 : 0, Max, Game / N, Render / N, RHI / N, GPU / N, R.Heroes, R.Monsters, *R.Nav, *R.Shot));
        Summary += FString::Printf(TEXT(" r%d_fps=%.1f r%d_p95_ms=%.1f"), R.Team, Fps, R.Team, P95);
        if (R.Samples.Num() < 30 || Fps < TargetAvgFps) { bPass = false; Why += FString::Printf(TEXT(" realm%d_fps=%.1f<%.0f"), R.Team, Fps, TargetAvgFps); }
        if (P95 > TargetP95Ms) { bPass = false; Why += FString::Printf(TEXT(" realm%d_p95=%.1fms>%.1fms"), R.Team, P95, TargetP95Ms); }
    }
    if (GPerf.DraftStagesLeft > 0) { bPass = false; Why += FString::Printf(TEXT(" draft_preview_still_capturing=%d"), GPerf.DraftStagesLeft); }
    Note(FString::Printf(TEXT("CIRE_TOWN_PERF_%s %s%s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Summary, *Why));
    IFileManager::Get().MakeDirectory(*GPerf.Directory, true);
    FFileHelper::SaveStringToFile(FString::Join(GPerf.Lines, TEXT("\n")) + TEXT("\n"), *(GPerf.Directory / TEXT("perf.txt")));
    GPerf.bDone = true;
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireTownPerfStay"))) FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
}
// ---------------------------------------------------------------- -CireTownDoorProbe: door walkability + interior shots
struct FDoor
{
    FString Mesh, Level;
    int32 Realm = 0;
    FVector Center = FVector::ZeroVector, Through = FVector::ForwardVector; // Through: horizontal, across the wall
    FVector Outside = FVector::ZeroVector, Inside = FVector::ZeroVector;     // navmesh points (valid when bNav)
    bool bNav = false, bWalkable = false, bRoofed = false;
    float PathLength = 0.f;
    FString Why;
};
struct FShot { FString Name; int32 Realm = 0; FVector From = FVector::ZeroVector, At = FVector::ZeroVector; float Fov = 88.f; };
struct FDoorProbe
{
    TArray<FDoor> Doors;
    TArray<FShot> Shots;   // the same views in both realms: interiors (from a doorway) and street landmarks
    int32 Shot = -1;
    double ShotAt = 0;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<FString> Files;
};
FDoorProbe GDoors;

void GatherDoors(UWorld* World)
{
    TArray<FCireDoorway> Found; CireTownMap::FindDoorways(World, Found);
    for (const FCireDoorway& W : Found)
    {
        FDoor D; D.Mesh = W.Mesh + (W.bLeaf ? TEXT(" (leaf)") : TEXT("")); D.Level = W.Level; D.Realm = W.Realm; D.Center = W.Center; D.Through = W.Through; D.bRoofed = W.bRoofed;
        GDoors.Doors.Add(D);
    }
}
void TestDoors(UWorld* World)
{
    const float Radius = 48.f; // the Hero agent
    for (FDoor& D : GDoors.Doors)
    {
        FVector P[2]; bool bOk[2];
        for (int32 S = 0; S < 2; ++S)
        {
            const FVector Probe = D.Center + D.Through * (S == 0 ? -170.f : 170.f) + FVector(0, 0, 60);
            bOk[S] = CireNav::Project(World, Probe, P[S], FVector(70, 70, 140), Radius);
        }
        const int32 In = 1; // FindDoorways: Through points inside
        D.Inside = P[In]; D.Outside = P[1 - In];
        D.bNav = bOk[0] && bOk[1];
        if (!bOk[0] && !bOk[1]) { D.Why = TEXT("no navmesh on either side"); continue; }
        if (!D.bNav) { D.Why = bOk[In] ? TEXT("no navmesh outside") : TEXT("no navmesh inside"); continue; }
        const FCireNavPath Path = CireNav::FindPath(World, D.Outside, D.Inside, Radius, true);
        D.PathLength = Path.Length;
        D.bWalkable = Path.bValid && !Path.bPartial && Path.Length < 700.f;
        D.Why = D.bWalkable ? TEXT("walkable") : !Path.bValid ? TEXT("no path") : Path.bPartial ? TEXT("partial path") : TEXT("detour (the doorway itself is closed)");
    }
}
void WriteDoors()
{
    TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Counts[2][3] = {{0, 0, 0}, {0, 0, 0}}; // realm x (walkable, blocked, no nav)
    for (const FDoor& D : GDoors.Doors)
    {
        const FVector2D L = CireLanePath::ToLocal(D.Realm, D.Center);
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("mesh"), D.Mesh); O->SetStringField(TEXT("level"), D.Level); O->SetNumberField(TEXT("realm"), D.Realm);
        TArray<TSharedPtr<FJsonValue>> XY; XY.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(L.X))); XY.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(L.Y)));
        O->SetArrayField(TEXT("local"), XY);
        O->SetBoolField(TEXT("walkable"), D.bWalkable); O->SetBoolField(TEXT("roofed"), D.bRoofed);
        O->SetNumberField(TEXT("pathCm"), FMath::RoundToDouble(D.PathLength)); O->SetStringField(TEXT("result"), D.Why);
        Rows.Add(MakeShared<FJsonValueObject>(O));
        ++Counts[FMath::Clamp(D.Realm, 0, 1)][D.bWalkable ? 0 : D.bNav ? 1 : 2];
        if (!D.bWalkable && D.Realm == 0 && D.bRoofed)
            Note(FString::Printf(TEXT("CIRE_TOWN_DOOR_BLOCKED realm=0 local=%.0f,%.0f mesh=%s level=%s why=%s path=%.0f"), L.X, L.Y, *D.Mesh, *D.Level, *D.Why, D.PathLength));
    }
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("notes"), TEXT("town-perf door probe: every pack mesh named *Door* (frames, doorways, castle wall doors), tested with a Hero-agent (48 cm) navmesh path from 1.7 m outside to 1.7 m inside. roofed = one side has a roof overhead (a building interior)."));
    Root->SetArrayField(TEXT("doors"), Rows);
    FString Out; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Out));
    IFileManager::Get().MakeDirectory(*GPerf.Directory, true);
    FFileHelper::SaveStringToFile(Out, *(GPerf.Directory / TEXT("doors.json")));
    for (int32 R = 0; R < 2; ++R)
        Note(FString::Printf(TEXT("CIRE_TOWN_DOORS realm=%d walkable=%d blocked=%d no_nav=%d file=%s"), R, Counts[R][0], Counts[R][1], Counts[R][2], *(GPerf.Directory / TEXT("doors.json"))));
}
bool TickDoors(ACireGameMode* Mode, double Now)
{
    UWorld* World = Mode->GetWorld();
    APlayerController* PC = World->GetFirstPlayerController();
    if (GDoors.Shot < 0)
    {
        const double Started = FPlatformTime::Seconds();
        { FString Info; for (TCireActorIterator<ACireDraftStage> It(World); It; ++It) Info += FString::Printf(TEXT(" begun=%d tick=%d registered=%d"), It->HasActorBegunPlay() ? 1 : 0, It->IsActorTickEnabled() ? 1 : 0, It->PrimaryActorTick.IsTickFunctionRegistered() ? 1 : 0); Note(TEXT("CIRE_TOWN_PERF_DRAFT_STAGES") + Info); }
        GatherDoors(World); TestDoors(World); WriteDoors();
        Note(FString::Printf(TEXT("CIRE_TOWN_DOORS_TESTED doors=%d ms=%.0f"), GDoors.Doors.Num(), (FPlatformTime::Seconds() - Started) * 1000.0));
        // Interior shots: walkable roofed doors in realm 0 spread over the town, the same view in realm 1.
        TArray<int32> Pick;
        for (int32 I = 0; I < GDoors.Doors.Num(); ++I)
        {
            const FDoor& D = GDoors.Doors[I];
            if (D.Realm != 0 || !D.bWalkable || !D.bRoofed) continue;
            bool bNear = false; for (int32 J : Pick) if (FVector::Dist2D(GDoors.Doors[J].Center, D.Center) < 2500.f) { bNear = true; break; }
            if (!bNear) Pick.Add(I);
            if (Pick.Num() >= 6) break;
        }
        const FVector Gap = CireTownMap::RealmOrigin(1) - CireTownMap::RealmOrigin(0);
        const FVector Shift(Gap.X, Gap.Y, 0);
        for (int32 K = 0; K < Pick.Num(); ++K)
        {
            const FDoor& D = GDoors.Doors[Pick[K]];
            const FVector In = (D.Inside - D.Outside).GetSafeNormal2D();
            const FVector From = D.Center - In * 60.f + FVector(0, 0, 175.f), To = D.Inside + In * 300.f + FVector(0, 0, 90.f);
            const FVector2D L = CireLanePath::ToLocal(0, D.Center);
            for (int32 R = 0; R < 2; ++R)
                GDoors.Shots.Add({FString::Printf(TEXT("interior_%02d_%.0f_%.0f_realm%d"), K, L.X, L.Y, R), R, From + (R ? Shift : FVector::ZeroVector), To + (R ? Shift : FVector::ZeroVector), 88.f});
        }
        // Street views (night readability): along the march route, looking down the street toward the next waypoint.
        for (int32 R = 0; R < 2; ++R)
        {
            const TArray<FVector> Route = CireLanePath::RoutePoints(World, R, 0.f);
            for (int32 K = 1; K + 1 < Route.Num(); K += 2)
            {
                const FVector Dir = (Route[K + 1] - Route[K]).GetSafeNormal2D();
                GDoors.Shots.Add({FString::Printf(TEXT("street_route%02d_realm%d"), K, R), R, Route[K] - Dir * 500.f + FVector(0, 0, 420.f), Route[K] + Dir * 900.f + FVector(0, 0, 80.f), 80.f});
            }
        }
        GDoors.Shots.Sort([](const FShot& A, const FShot& B) { return A.Name < B.Name; });
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        GDoors.Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
        if (GDoors.Camera.IsValid())
        {
            auto* Cam = GDoors.Camera->GetCameraComponent(); Cam->SetFieldOfView(88.f); Cam->SetAspectRatio(16.f / 9.f); Cam->bConstrainAspectRatio = true;
            if (PC) { PC->SetViewTarget(GDoors.Camera.Get()); if (PC->MyHUD) PC->MyHUD->bShowHUD = false; }
        }
        GDoors.Shot = 0; GDoors.ShotAt = 0;
    }
    if (!GDoors.Camera.IsValid() || GDoors.Shot >= GDoors.Shots.Num())
    {
        if (PC && PC->GetPawn()) { PC->SetViewTarget(PC->GetPawn()); if (PC->MyHUD) PC->MyHUD->bShowHUD = true; }
        return true; // done
    }
    const FShot& Shot = GDoors.Shots[GDoors.Shot];
    if (GDoors.ShotAt <= 0)
    {
        GDoors.Camera->SetActorLocationAndRotation(Shot.From, (Shot.At - Shot.From).Rotation());
        GDoors.Camera->GetCameraComponent()->SetFieldOfView(Shot.Fov);
        if (ACireHero* H = LocalHero(World)) H->TeamId = Shot.Realm; // the realm's monsters and heroes stay observable
        GDoors.ShotAt = Now;
        return false;
    }
    if (Now - GDoors.ShotAt < 6.0) return false; // the realm view switch, streaming, Lumen and exposure settle
    const FString File = GPerf.Directory / (Shot.Name + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(File, false, false);
    GDoors.Files.Add(File);
    Note(FString::Printf(TEXT("CIRE_TOWN_SHOT %s realm=%d file=%s"), *Shot.Name, Shot.Realm, *File));
    ++GDoors.Shot; GDoors.ShotAt = 0;
    return false;
}

bool Tick(float)
{
    UWorld* World = GPerf.World.Get();
    if (!World || GPerf.bDone) return false;
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    if (!Mode) return true;
    const double Now = FPlatformTime::Seconds();
    const double FrameMs = GPerf.LastReal > 0 ? (Now - GPerf.LastReal) * 1000.0 : 0.0;
    GPerf.LastReal = Now;
    Mode->WaveTimer = 1.e6f; // the probe's own fight replaces the wave schedule
    ACireHero* Hero = LocalHero(World);
    if (Hero && !Hero->bBot) { Hero->Draft(2); Hero->bBot = true; Hero->bAutoAttack = true; Hero->HeroName = TEXT("Perf probe"); }
    switch (GPerf.Stage)
    {
    case 0: // load-to-playable: realms visible, the navmesh ready, the local champion in its realm
    {
        // Settled: nothing left streaming (the landscape's World Partition cells arrive late) and the navmesh idle for 2 s.
        int32 Pending = 0;
        for (ULevelStreaming* L : World->GetStreamingLevels()) if (L && L->ShouldBeVisible() && !L->IsLevelVisible()) ++Pending;
        // -CireProcedural gives the old procedural town as a reference for the same fight.
        const bool bTown = CireTownMap::IsActive();
        const bool bReady = Hero && (!bTown || CireTownMap::LoadedLevels(World) > 0) && CireNav::IsReady(World) && Pending == 0;
        if (!bReady) GPerf.StageAt = 0;
        else if (GPerf.StageAt <= 0) GPerf.StageAt = Now;
        if (bReady && Now - GPerf.StageAt >= 2.0)
        {
            GPerf.PlayableSeconds = GPerf.StageAt - GStartTime;
            Note(FString::Printf(TEXT("CIRE_TOWN_PERF_LOAD playable_s=%.1f realms_ms=%.0f nav_build_ms=%.0f tiles=%d/%d nav_rebuilds=%d last_rebuild_ms=%.0f levels=%d streaming=%d"), GPerf.PlayableSeconds,
                CireTownMap::LastLoadMs(), CireNav::Stats(World).InitialBuildMs, CireNav::Stats(World).Tiles[0], CireNav::Stats(World).Tiles[1],
                CireNav::Stats(World).Rebuilds, CireNav::Stats(World).LastRebuildMs,
                CireTownMap::LoadedLevels(World), World->GetStreamingLevels().Num()));
            for (int32 T = 0; T < 2; ++T)
            {
                // Outdoor walkability: navmesh coverage of the realm's play bounds (400 cm cells; buildings and cliffs are
                // legitimately uncovered, so this is a regression figure, compared run to run and realm to realm).
                const CireNav::FCoverage& C = CireNav::RealmCoverage(World, T);
                int32 On = 0; for (uint8 B : C.Cells) On += B ? 1 : 0;
                Note(FString::Printf(TEXT("CIRE_TOWN_PERF_NAV_COVERAGE realm=%d cells=%d on_nav=%d pct=%.1f"), T, C.Cells.Num(), On, C.Cells.Num() ? 100.0 * On / C.Cells.Num() : 0.0));
            }
            GPerf.Stage = FParse::Param(FCommandLine::Get(), TEXT("CireTownDoorProbe")) ? 20 : 1; GPerf.WaitStarted = Now;
            if (GPerf.Stage == 1 && FParse::Param(FCommandLine::Get(), TEXT("CireTownPerfLoadOnly")))
            {
                // Load-time iterations: no fight, exit once playable.
                CireTownMap::LogSceneStats(World, TEXT("load"));
                IFileManager::Get().MakeDirectory(*GPerf.Directory, true);
                FFileHelper::SaveStringToFile(FString::Join(GPerf.Lines, TEXT("\n")) + TEXT("\n"), *(GPerf.Directory / TEXT("perf.txt")));
                GPerf.bDone = true;
                FPlatformMisc::RequestExitWithStatus(false, GPerf.PlayableSeconds <= TargetPlayableSeconds ? 0 : 1);
                return false;
            }
        }
        else if (Now - GStartTime > 900) { Note(TEXT("CIRE_TOWN_PERF_FAIL the town never became playable within 900 s")); Report(); }
        return true;
    }
    case 1: // 10 champions (bots fill in)
        if ((Mode->bBotsFilled && Mode->Heroes.Num() >= 10) || Now - GPerf.WaitStarted > 60)
        {
            for (ACireHero* H : Mode->Heroes) if (IsValid(H)) { H->bDrafted = true; H->bAutoAttack = true; }
            CireTownMap::LogSceneStats(World, TEXT("fight"));
            {   // Diagnostics: the champion-select preview stage should be gone once everyone is drafted.
                int32 Stages = 0; FString Info;
                for (TCireActorIterator<ACireDraftStage> It(World); It; ++It) { ++Stages; Info += FString::Printf(TEXT(" frames_shown=%llu begun=%d tick=%d registered=%d"), It->FramesShown(), It->HasActorBegunPlay() ? 1 : 0, It->IsActorTickEnabled() ? 1 : 0, It->PrimaryActorTick.IsTickFunctionRegistered() ? 1 : 0); }
                Note(FString::Printf(TEXT("CIRE_TOWN_PERF_DRAFT_STAGES count=%d%s"), Stages, *Info));
            }
            MoveLocalHero(Mode, 0);
            TopUp(Mode); GPerf.NextTopUp = Now + 1.0;
            GPerf.Stage = 2; GPerf.StageAt = Now; GPerf.bShot = GPerf.bProfiled = false;
            Note(FString::Printf(TEXT("CIRE_TOWN_PERF_FIGHT_START realm=0 heroes=%d"), Mode->Heroes.Num()));
        }
        return true;
    case 2: case 3:
    {
        const int32 Team = GPerf.Stage - 2;
        FRealmStats& R = GPerf.Realms[Team]; R.Team = Team;
        if (Now >= GPerf.NextTopUp) { TopUp(Mode); GPerf.NextTopUp = Now + 1.0; }
        const double Age = Now - GPerf.StageAt;
        if (Age >= WarmSeconds && FrameMs > 0)
        {
            if (R.Samples.IsEmpty()) { TRACE_BEGIN_REGION(Team == 0 ? TEXT("CireTownFight0") : TEXT("CireTownFight1")); CireNav::ResetQueryStats(World); } // Insights: -region=
            FSample S; S.Frame = FrameMs; S.Game = Ms(GGameThreadTime); S.Render = Ms(GRenderThreadTime); S.RHI = Ms(GRHIThreadTime); S.GPU = Ms(RHIGetGPUFrameCycles(0));
            R.Samples.Add(S);
        }
        if (!GPerf.bShot && Age >= WarmSeconds + MeasureSeconds * .5)
        {
            GPerf.bShot = true;
            R.Shot = GPerf.Directory / FString::Printf(TEXT("fight_realm%d.png"), Team);
            FScreenshotRequest::RequestScreenshot(R.Shot, true, false);
            int32 Monsters = 0; for (ACireMonster* M : Mode->Monsters) Monsters += IsValid(M) && M->Health > 0 ? 1 : 0;
            R.Monsters = Monsters; R.Heroes = Mode->Heroes.Num();
        }
        if (!GPerf.bProfiled && Age >= WarmSeconds + MeasureSeconds * .7 && FParse::Param(FCommandLine::Get(), TEXT("CireTownPerfProfileGPU")))
        {
            GPerf.bProfiled = true;
            Note(FString::Printf(TEXT("CIRE_TOWN_PERF_PROFILEGPU realm=%d"), Team));
            if (GEngine) GEngine->Exec(World, TEXT("ProfileGPU"));
        }
        if (Age < WarmSeconds + MeasureSeconds) return true;
        TRACE_END_REGION(Team == 0 ? TEXT("CireTownFight0") : TEXT("CireTownFight1"));
        { int32 Stages = 0; for (TCireActorIterator<ACireDraftStage> It(World); It; ++It) ++Stages; Note(FString::Printf(TEXT("CIRE_TOWN_PERF_DRAFT_STAGES realm=%d count=%d"), Team, Stages)); GPerf.DraftStagesLeft += Stages; }
        {
            const FCireNavStats& N = CireNav::Stats(World); const double F = FMath::Max(1, R.Samples.Num());
            R.Nav = FString::Printf(TEXT("nav_queries_per_frame=%.2f nav_query_avg_ms=%.3f nav_query_peak_ms=%.1f steer_ms_per_frame=%.2f deferred=%d"),
                N.Queries / F, N.Queries ? N.QueryMs / N.Queries : 0.0, N.PeakQueryMs, N.SteerMs / F, N.BudgetDeferred);
        }
        if (Team == 0)
        {
            MoveLocalHero(Mode, 1);
            GPerf.Stage = 3; GPerf.StageAt = Now; GPerf.bShot = GPerf.bProfiled = false;
            Note(TEXT("CIRE_TOWN_PERF_FIGHT_START realm=1"));
            return true;
        }
        Report();
        return false;
    }
    case 20: // -CireTownDoorProbe
        if (TickDoors(Mode, Now))
        {
            if (FParse::Param(FCommandLine::Get(), TEXT("CireTownPerfProbe"))) { GPerf.Stage = 1; GPerf.WaitStarted = Now; return true; }
            IFileManager::Get().MakeDirectory(*GPerf.Directory, true);
            FFileHelper::SaveStringToFile(FString::Join(GPerf.Lines, TEXT("\n")) + TEXT("\n"), *(GPerf.Directory / TEXT("perf.txt")));
            Note(TEXT("CIRE_TOWN_DOOR_PROBE_DONE"));
            GPerf.bDone = true; FPlatformMisc::RequestExitWithStatus(false, 0);
            return false;
        }
        return true;
    default: return false;
    }
}
}

bool CireTownPerf::IsEnabled() { return GPerf.bEnabled; }
void CireTownPerf::Initialize(UWorld* World)
{
    if (!World || !World->IsGameWorld() || GPerf.bEnabled) return;
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireTownPerfProbe")) && !FParse::Param(FCommandLine::Get(), TEXT("CireTownDoorProbe"))) return;
    GPerf = FPerf();
    GPerf.bEnabled = true; GPerf.World = World;
    GPerf.Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TownPerf") / FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")));
    GPerf.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
    Note(FString::Printf(TEXT("CIRE_TOWN_PERF_READY town=%d since_start_s=%.1f dir=%s"), CireTownMap::IsActive() ? 1 : 0, FPlatformTime::Seconds() - GStartTime, *GPerf.Directory));
}
