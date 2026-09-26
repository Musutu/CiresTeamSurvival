// town-trim: CireTownShots.h
#include "CireTownShots.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireNav.h"
#include "CireTownMap.h"
#include "CireTownTrim.h"
#include "CireHUD.h"
#include "CireLayoutEditorState.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Level.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "WaterBodyComponent.h"
#include "WaterZoneActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTownShots, Log, All);

namespace
{
struct FShot
{
    FString Name;
    FVector From, At;
    int32 Toggle = 0;      // 0 plain, 1 castle fills, 2 water
    bool bOn = true;       // the toggled set shown (after) or hidden (before)
};
struct FShots
{
    bool bEnabled = false, bBuilt = false, bDone = false, bScanned = false;
    TWeakObjectPtr<UWorld> World;
    FTSTicker::FDelegateHandle Ticker;
    TArray<FString> Kinds;
    TArray<FShot> List;
    int32 Index = 0, Phase = 0;
    double ReadyAt = 0, PhaseAt = 0;
    uint64 PhaseFrame = 0;
    FString Directory;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> Water;
};
FShots G;

bool Playable(UWorld* World)
{
    if (!CireTownMap::IsActive() || CireTownMap::LoadedLevels(World) == 0 || !CireNav::IsReady(World)) return false;
    for (ULevelStreaming* L : World->GetStreamingLevels()) if (L && L->ShouldBeVisible() && !L->IsLevelVisible()) return false;
    APlayerController* PC = World->GetFirstPlayerController();
    return PC && PC->GetPawn();
}
float GroundZ(UWorld* World, int32 Realm, const FVector2D& Local) { return CireTownMap::Ground(World, FVector2D(CireLanePath::ToWorld(Realm, Local))); }
FVector At(UWorld* World, int32 Realm, const FVector2D& Local, float AboveGround) { FVector W = CireLanePath::ToWorld(Realm, Local); W.Z = GroundZ(World, Realm, Local) + AboveGround; return W; }

void AddEdgeShots(UWorld* World)
{
    const TArray<FVector2D>& Poly = CireTownTrim::Polygon();
    if (Poly.Num() < 3) { UE_LOG(LogCireTownShots, Warning, TEXT("CIRE_TOWN_SHOTS no Play Bounds: no edge shots")); return; }
    FBox2D Box(ForceInit); FVector2D Centroid = FVector2D::ZeroVector;
    for (const FVector2D& P : Poly) { Box += P; Centroid += P; }
    Centroid /= Poly.Num();
    // The longest edges, looking out across the border from 35 m inside.
    TArray<int32> Edges; for (int32 I = 0; I < Poly.Num(); ++I) Edges.Add(I);
    Edges.Sort([&](int32 A, int32 B) { return FVector2D::DistSquared(Poly[A], Poly[(A + 1) % Poly.Num()]) > FVector2D::DistSquared(Poly[B], Poly[(B + 1) % Poly.Num()]); });
    for (int32 Realm = 0; Realm < 2; ++Realm)
    {
        const double Size = FMath::Max(Box.GetSize().X, Box.GetSize().Y);
        G.List.Add({FString::Printf(TEXT("edge_realm%d_overview"), Realm), At(World, Realm, Centroid - FVector2D(Size * .95, Size * .35), Size * .55f), At(World, Realm, Centroid, 0.f)});
        G.List.Add({FString::Printf(TEXT("edge_realm%d_topdown"), Realm), At(World, Realm, Centroid + FVector2D(1, 0), Size * 1.05f), At(World, Realm, Centroid, 0.f)});
        const int32 Count = Realm == 0 ? 4 : 2;
        for (int32 K = 0; K < Count && K < Edges.Num(); ++K)
        {
            const FVector2D A = Poly[Edges[K]], B = Poly[(Edges[K] + 1) % Poly.Num()];
            const FVector2D Mid = (A + B) * .5, Dir = (B - A).GetSafeNormal();
            FVector2D In(-Dir.Y, Dir.X);
            if (!CireTownTrim::InsidePolygon(Poly, Mid + In * 200.0)) In = -In;
            G.List.Add({FString::Printf(TEXT("edge_realm%d_%d"), Realm, K), At(World, Realm, Mid + In * 3500.0, 1400.f), At(World, Realm, Mid - In * 3000.0, 200.f)});
        }
    }
}
void AddCastleShots(UWorld* World)
{
    const auto& D = CireTownMap::Def();
    if (D.Castle.Views.IsEmpty()) { UE_LOG(LogCireTownShots, Warning, TEXT("CIRE_TOWN_SHOTS CastleTown.json castleInterior has no views")); return; }
    for (int32 Realm = 0; Realm < 2; ++Realm)
        for (const FCireTownView& V : D.Castle.Views)
            for (int32 On = 0; On < 2; ++On)
            {
                const FVector O(CireLanePath::RealmOrigin(Realm), D.Offsets[Realm].Z);
                G.List.Add({FString::Printf(TEXT("castle_realm%d_%s_%s"), Realm, *V.Name, On ? TEXT("after") : TEXT("before")), O + V.From, O + V.At, 1, On == 1});
            }
}
void AddWaterShots(UWorld* World)
{
    // Every pack water body (both realms), outside the ocean's own vast bounds: a view from its side, 25 m up.
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level || Level == World->PersistentLevel) continue;
        for (AActor* A : Level->Actors)
        {
            if (!A || !A->GetClass()->GetName().Contains(TEXT("Water"))) continue;
            if (A->GetClass()->GetName().Contains(TEXT("WaterBody"))) G.Water.Add(A);
            FString Override;
            if (FParse::Value(FCommandLine::Get(), TEXT("CireWaterMaterial="), Override))
                if (UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Override))
                {
                    TInlineComponentArray<UActorComponent*> All(A);
                    for (UActorComponent* C : All)
                        if (UFunction* F = C->FindFunction(TEXT("SetWaterMaterial")))
                        {
                            struct { UMaterialInterface* M; } Args{M};
                            C->ProcessEvent(F, &Args);
                            UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_WATER_MATERIAL %s -> %s"), *C->GetName(), *M->GetPathName());
                        }
                }
            const FBox B = A->GetComponentsBoundingBox(true);
            if (FParse::Param(FCommandLine::Get(), TEXT("CireTownWaterDump")) && CireTownMap::RealmAt(A->GetActorLocation()) == 0)
            {
                // Diagnostics: the water classes' own properties (not the engine base classes').
                auto Dump = [](UObject* O)
                {
                    FString Line;
                    for (UClass* C = O->GetClass(); C && !C->GetPathName().StartsWith(TEXT("/Script/Engine")); C = C->GetSuperClass())
                        for (TFieldIterator<FProperty> It(C, EFieldIteratorFlags::ExcludeSuper); It; ++It)
                        {
                            FString V; It->ExportTextItem_Direct(V, It->ContainerPtrToValuePtr<void>(O), nullptr, O, PPF_None);
                            Line += FString::Printf(TEXT(" %s=%s"), *It->GetName(), *V.Left(120));
                        }
                    UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_WATER_DUMP %s:%s"), *O->GetName(), *Line.Left(6000));
                };
                Dump(A);
                TInlineComponentArray<UActorComponent*> All(A);
                for (UActorComponent* C : All) if (C->GetClass()->GetPathName().Contains(TEXT("/Script/Water"))) Dump(C);
            }
            if (UWaterBodyComponent* WB = A->FindComponentByClass<UWaterBodyComponent>())
            {
                UMaterialInstanceDynamic* MID = WB->GetWaterMaterialInstance();
                UStaticMeshComponent* Info = nullptr;
                { TInlineComponentArray<UStaticMeshComponent*> Meshes(A); for (UStaticMeshComponent* M : Meshes) if (M->GetName() == TEXT("WaterInfoMeshComponent")) Info = M; }
                UStaticMesh* SM = Info ? Info->GetStaticMesh().Get() : nullptr;
                UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_WATER_STATE %s realm=%d should_render=%d gen_tile=%d material=%s mid=%s mid_water_usage=%d parent_water_usage=%d info_mesh=%s compiling=%d render_data=%d zone=%s"),
                    *A->GetName(), CireTownMap::RealmAt(A->GetActorLocation()), WB->ShouldRender() ? 1 : 0, WB->ShouldGenerateWaterMeshTile() ? 1 : 0, *GetNameSafe(WB->GetWaterMaterial()), *GetNameSafe(MID),
                    MID && MID->CheckMaterialUsage_Concurrent(MATUSAGE_Water) ? 1 : 0, WB->GetWaterMaterial() && WB->GetWaterMaterial()->CheckMaterialUsage_Concurrent(MATUSAGE_Water) ? 1 : 0,
                    *GetNameSafe(SM), SM && SM->IsCompiling() ? 1 : 0, SM && SM->GetRenderData() ? 1 : 0, *GetNameSafe(WB->GetWaterZone()));
            }
            TInlineComponentArray<UPrimitiveComponent*> Prims(A);
            for (UPrimitiveComponent* P : Prims)
                UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_WATER_PART actor=%s comp=%s class=%s visible=%d hidden=%d registered=%d collision=%d bounds=%s z=%.0f"), *A->GetName(), *P->GetName(), *P->GetClass()->GetName(),
                    P->IsVisible() ? 1 : 0, P->bHiddenInGame ? 1 : 0, P->IsRegistered() ? 1 : 0, int32(P->GetCollisionEnabled()), *P->Bounds.BoxExtent.ToCompactString(), P->Bounds.Origin.Z);
            UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_WATER_BODY class=%s realm=%d local=%s size=%s level=%s"), *A->GetClass()->GetName(), CireTownMap::RealmAt(A->GetActorLocation()),
                *CireLanePath::ToLocal(CireTownMap::RealmAt(A->GetActorLocation()), A->GetActorLocation()).ToString(), *B.GetSize().ToCompactString(), *Level->GetOuter()->GetName());
        }
    }
    // Where the water should show: the lowest ground inside the Play Bounds (riverbeds, the harbour), 10 m grid in realm 0.
    {
        const TArray<FVector2D>& Poly = CireTownTrim::Polygon();
        FBox2D Area(ForceInit);
        if (Poly.Num() >= 3) for (const FVector2D& P : Poly) Area += P; else Area = FBox2D(FVector2D(-25000), FVector2D(25000));
        TArray<FVector> Low;
        TArray<int32> Histogram; Histogram.Init(0, 12);
        for (double X = Area.Min.X; X <= Area.Max.X; X += 1000.0)
            for (double Y = Area.Min.Y; Y <= Area.Max.Y; Y += 1000.0)
            {
                if (Poly.Num() >= 3 && !CireTownTrim::InsidePolygon(Poly, FVector2D(X, Y))) continue;
                const float Z = GroundZ(World, 0, FVector2D(X, Y));
                Low.Add(FVector(X, Y, Z));
                ++Histogram[FMath::Clamp(int32((Z + 1500.f) / 250.f), 0, 11)];
            }
        Low.Sort([](const FVector& A, const FVector& B) { return A.Z < B.Z; });
        FString H; for (int32 I = 0; I < Histogram.Num(); ++I) H += FString::Printf(TEXT(" <%d:%d"), -1500 + (I + 1) * 250, Histogram[I]);
        UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_WATER_GROUND samples=%d min=%.0f histogram%s"), Low.Num(), Low.Num() ? Low[0].Z : 0.f, *H);
        TArray<FVector> Picks;
        for (const FVector& P : Low)
        {
            bool bNear = false; for (const FVector& Q : Picks) bNear |= FVector2D::Distance(FVector2D(P), FVector2D(Q)) < 9000.0;
            if (!bNear) Picks.Add(P);
            if (Picks.Num() >= 3) break;
        }
        for (int32 I = 0; I < Picks.Num(); ++I)
        {
            UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_WATER_LOW local=%.0f,%.0f ground=%.0f"), Picks[I].X, Picks[I].Y, Picks[I].Z);
            for (int32 Realm = 0; Realm < 2; ++Realm)
                for (int32 On = 0; On < 2; ++On)
                {
                    const FVector2D L(Picks[I]);
                    G.List.Add({FString::Printf(TEXT("water_realm%d_low%d_%s"), Realm, I, On ? TEXT("after") : TEXT("before")), At(World, Realm, L + FVector2D(-3000, -1800), 1800.f), At(World, Realm, L, 0.f), 2, On == 1});
                }
        }
    }
    const auto& D = CireTownMap::Def();
    for (int32 Realm = 0; Realm < 2; ++Realm)
        for (const FCireTownView& V : D.WaterViews)
            for (int32 On = 0; On < 2; ++On)
            {
                const FVector O(CireLanePath::RealmOrigin(Realm), D.Offsets[Realm].Z);
                G.List.Add({FString::Printf(TEXT("water_realm%d_%s_%s"), Realm, *V.Name, On ? TEXT("after") : TEXT("before")), O + V.From, O + V.At, 2, On == 1});
            }
}
void SetToggle(UWorld* World, int32 Toggle, bool bOn)
{
    if (Toggle == 1)
    {
        for (ULevel* Level : World->GetLevels())
            if (Level) for (AActor* A : Level->Actors) if (A && A->Tags.Contains(TEXT("CireCastleFill"))) A->SetActorHiddenInGame(!bOn);
    }
    else if (Toggle == 2)
        for (const auto& A : G.Water) if (A.IsValid()) A->SetActorHiddenInGame(!bOn);
}

void Scan(UWorld* World)
{
    // Castle interior: walkable spots (on the navmesh) under a roof or walled in, within the castle levels' footprint.
    G.bScanned = true;
    FBox2D Area(ForceInit);
    for (ULevel* Level : World->GetLevels())
    {
        if (!Level || Level == World->PersistentLevel) continue;
        const FString Name = Level->GetOutermost()->GetName();
        if (!(Name.Contains(TEXT("SL_Castle_CireRealm")) || Name.Contains(TEXT("SL_Courtyard")))) continue; // the keep and its yards, not the town walls
        for (AActor* A : Level->Actors)
        {
            if (!A || A->GetClass()->GetName().Contains(TEXT("Landscape")) || CireTownMap::RealmAt(A->GetActorLocation()) != 0) continue;
            const FBox B = A->GetComponentsBoundingBox(true);
            if (!B.IsValid || B.GetSize().GetMax() > 30000) continue;
            Area += FVector2D(CireLanePath::ToLocal(0, B.Min)); Area += FVector2D(CireLanePath::ToLocal(0, B.Max));
        }
    }
    if (!Area.bIsValid) { UE_LOG(LogCireTownShots, Warning, TEXT("CIRE_CASTLE_SCAN no castle levels")); return; }
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireCastleScan), true);
    // Only where people go: reachable on foot from the castle goal (no tower tops, no wall walks without stairs).
    FVector Goal;
    const bool bGoal = CireNav::Project(World, CireLanePath::GoalZoneCenter(World, 0, 100.f), Goal, FVector(600, 600, 1500), 40.f);
    struct FSpot { FVector P; float Roof; int32 Walls; bool bCovered; };
    TArray<FSpot> Spots;
    const double Step = 300.0;
    for (double X = Area.Min.X; X <= Area.Max.X; X += Step)
        for (double Y = Area.Min.Y; Y <= Area.Max.Y; Y += Step)
        {
            const FVector Top = CireLanePath::ToWorld(0, FVector2D(X, Y), 9000.f), Bottom = CireLanePath::ToWorld(0, FVector2D(X, Y), -1500.f);
            // Floor by floor, top down (a gate passage has a walkway above it).
            FVector From = Top;
            for (int32 Floor = 0; Floor < 6; ++Floor)
            {
                FHitResult Hit;
                if (!World->LineTraceSingleByChannel(Hit, From, Bottom, ECC_Visibility, Params)) break;
                From = Hit.ImpactPoint - FVector(0, 0, 60);
                if (Hit.ImpactNormal.Z < .7f) continue;
                FVector Nav;
                if (!CireNav::Project(World, Hit.ImpactPoint + FVector(0, 0, 40), Nav, FVector(40, 40, 120), 40.f)) continue;
                if (bGoal && FMath::Abs(Nav.Z - Goal.Z) > 900.f) continue; // wall walks and tower tops
                FHitResult Up;
                const bool bRoof = World->LineTraceSingleByChannel(Up, Nav + FVector(0, 0, 150), Nav + FVector(0, 0, 2500), ECC_Visibility, Params);
                int32 Walls = 0;
                for (int32 K = 0; K < 8; ++K)
                {
                    const FVector Dir = FRotator(0, K * 45.f, 0).Vector();
                    FHitResult Side; Walls += World->LineTraceSingleByChannel(Side, Nav + FVector(0, 0, 250), Nav + FVector(0, 0, 250) + Dir * 1400.f, ECC_Visibility, Params) ? 1 : 0;
                }
                if (bRoof || Walls >= 6) Spots.Add({Nav, bRoof ? float(Up.ImpactPoint.Z - Nav.Z) : 0.f, Walls, bRoof});
            }
        }
    // Spaced picks: covered passages every 9 m, walled-in yards every 16 m.
    TArray<FSpot> Picks;
    Spots.Sort([](const FSpot& A, const FSpot& B) { return A.bCovered != B.bCovered ? A.bCovered : A.Walls > B.Walls; });
    for (const FSpot& S : Spots)
    {
        bool bNear = false;
        for (const FSpot& P : Picks) if (FVector::Dist(P.P, S.P) < ((S.bCovered && P.bCovered) ? 900.f : 1600.f)) { bNear = true; break; }
        int32 Covered = 0; for (const FSpot& P : Picks) Covered += P.bCovered ? 1 : 0;
        if (!bNear && (S.bCovered ? Covered < 28 : Picks.Num() - Covered < 14)) Picks.Add(S);
    }
    FString Json = TEXT("[\n");
    for (int32 I = 0; I < Picks.Num(); ++I)
    {
        const FSpot& S = Picks[I];
        const FVector2D L = CireLanePath::ToLocal(0, S.P);
        const float H = S.bCovered ? FMath::Clamp(S.Roof * .7f, 180.f, 420.f) : 500.f;
        Json += FString::Printf(TEXT("  [%.0f, %.0f, %.0f, %d]%s  // %s\n"), L.X, L.Y, S.P.Z - CireTownMap::Def().Offsets[0].Z + H, S.bCovered ? 1300 : 2200, I + 1 < Picks.Num() ? TEXT(",") : TEXT(""),
            S.bCovered ? TEXT("covered") : TEXT("walled"));
    }
    Json += TEXT("]\n");
    const FString File = FPaths::ProjectSavedDir() / TEXT("TownShots/castle_anchors.json");
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
    FFileHelper::SaveStringToFile(Json, *File);
    UE_LOG(LogCireTownShots, Display, TEXT("CIRE_CASTLE_SCAN area=%s spots=%d picks=%d file=%s"), *Area.ToString(), Spots.Num(), Picks.Num(), *File);
}

bool Tick(float)
{
    UWorld* World = G.World.Get();
    if (!World || G.bDone) return false;
    if (auto* Mode = World->GetAuthGameMode<ACireGameMode>()) Mode->WaveTimer = 1.e6f; // no waves in the pictures
    const double Now = FPlatformTime::Seconds();
    if (!G.bBuilt)
    {
        if (!Playable(World)) { G.ReadyAt = 0; return true; }
        if (G.ReadyAt <= 0) { G.ReadyAt = Now; return true; }
        if (Now - G.ReadyAt < 4.0) return true;
        G.bBuilt = true;
        FString Exec; // console commands once playable, separated by '+' (no spaces to quote), e.g. cire.TownTrim=preview
        if (FParse::Value(FCommandLine::Get(), TEXT("CireShotsExec="), Exec, false) && GEngine)
        {
            TArray<FString> Cmds; Exec.ParseIntoArray(Cmds, TEXT("+"));
            for (FString C : Cmds) { C.ReplaceInline(TEXT("="), TEXT(" ")); GEngine->Exec(World, *C); UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_SHOTS_EXEC %s"), *C); }
        }
        if (FParse::Param(FCommandLine::Get(), TEXT("CireCastleLightScan"))) Scan(World);
        for (const FString& K : G.Kinds)
        {
            if (K == TEXT("edge")) AddEdgeShots(World);
            else if (K == TEXT("castle")) AddCastleShots(World);
            else if (K == TEXT("water")) AddWaterShots(World);
        }
        const bool bTrim = CireTownTrim::Active();
        G.Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("TownShots") / (FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")) + (bTrim ? TEXT("-trim") : TEXT("-full"))));
        IFileManager::Get().MakeDirectory(*G.Directory, true);
        APlayerController* PC = World->GetFirstPlayerController();
        if (PC && PC->MyHUD && !FParse::Param(FCommandLine::Get(), TEXT("CireTownShotsHUD"))) PC->MyHUD->bShowHUD = false; // the editor overlay is HUD
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        if (!FParse::Param(FCommandLine::Get(), TEXT("CireTownShotsHUD"))) G.Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params);
        if (G.Camera.IsValid() && PC)
        {
            auto* Cam = G.Camera->GetCameraComponent(); Cam->SetFieldOfView(75.f); Cam->SetAspectRatio(16.f / 9.f); Cam->bConstrainAspectRatio = true;
            Cam->PostProcessSettings.bOverride_MotionBlurAmount = true; Cam->PostProcessSettings.MotionBlurAmount = 0;
            PC->SetViewTarget(G.Camera.Get());
        }
        UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_SHOTS_START shots=%d trim=%d dir=%s"), G.List.Num(), bTrim ? 1 : 0, *G.Directory);
        G.PhaseAt = Now; G.PhaseFrame = GFrameCounter;
    }
    if (G.Index >= G.List.Num())
    {
        SetToggle(World, 1, true); SetToggle(World, 2, true);
        UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_SHOTS_DONE shots=%d dir=%s"), G.List.Num(), *G.Directory);
        G.bDone = true;
        if (FParse::Param(FCommandLine::Get(), TEXT("CireTownShotsExit"))) FPlatformMisc::RequestExitWithStatus(false, 0);
        return false;
    }
    FShot& S = G.List[G.Index];
    if (G.Phase == 0)
    {
        // Aim, then let streaming, the sky light, virtual shadow maps and the per-view realm culling settle.
        if (G.Camera.IsValid()) G.Camera->SetActorLocationAndRotation(S.From, (S.At - S.From).Rotation());
        else if (APlayerController* PC = World->GetFirstPlayerController())
            if (auto* HUD = Cast<ACireHUD>(PC->MyHUD); HUD && HUD->LayoutEditorState())
            {
                // -CireTownShotsHUD in the layout editor: its own map view, looking down at the view's target.
                FCireLayoutEditorState& E = *HUD->LayoutEditorState();
                E.bWalk = false; E.Focus = S.At; E.Distance = FMath::Clamp(float(FVector::Dist(S.From, S.At)), 2000.f, 30000.f);
                E.Yaw = (S.At - S.From).Rotation().Yaw; E.Realm = CireTownMap::RealmAt(S.At);
            }
        if (S.Toggle) SetToggle(World, S.Toggle, S.bOn);
        G.Phase = 1; G.PhaseAt = Now; G.PhaseFrame = GFrameCounter;
        return true;
    }
    if (G.Phase == 1 && Now - G.PhaseAt > 3.0 && GFrameCounter - G.PhaseFrame > 45)
    {
        const FString File = G.Directory / FString::Printf(TEXT("%02d_%s.png"), G.Index, *S.Name);
        FScreenshotRequest::RequestScreenshot(File, FParse::Param(FCommandLine::Get(), TEXT("CireTownShotsHUD")), false);
        UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_SHOT %s from=%s at=%s"), *File, *S.From.ToCompactString(), *S.At.ToCompactString());
        G.Phase = 2; G.PhaseFrame = GFrameCounter;
        return true;
    }
    if (G.Phase == 2 && GFrameCounter - G.PhaseFrame > 5) { G.Phase = 0; ++G.Index; }
    return true;
}
}

void CireTownShots::Initialize(UWorld* World)
{
    if (!World || !World->IsGameWorld() || G.bEnabled) return;
    FString Kinds;
    const bool bScan = FParse::Param(FCommandLine::Get(), TEXT("CireCastleLightScan"));
    if (!FParse::Value(FCommandLine::Get(), TEXT("CireTownShots="), Kinds, false) && !bScan) return; // false: keep the commas
    G = FShots(); G.bEnabled = true; G.World = World;
    if (FParse::Param(FCommandLine::Get(), TEXT("CireTownWaterDump")) && GEngine) GEngine->Exec(World, TEXT("log LogWater Verbose"));

    Kinds.ParseIntoArray(G.Kinds, TEXT(","));
    G.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
    UE_LOG(LogCireTownShots, Display, TEXT("CIRE_TOWN_SHOTS_READY kinds=%s scan=%d"), *Kinds, bScan ? 1 : 0);
}
