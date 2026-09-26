// dev-route-tools: -CireLayoutGallery (Tools/RunLayoutGallery.py). A scripted authoring session in the edit mode that
// drives the real map layout editor and captures it: the map view with every setter placed, Replace on a pack, the walk
// view placing a pack at the cursor, the vendor group, validation per team, the preview walking both realms, and the
// packs applied live. It writes no data files (the draft autosave and Apply's file writes are off; the live apply is
// in memory only). Logs CIRE_LAYOUT_GALLERY_DONE captures=N dir=... and exits.
#include "CireRouteEditMode.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireLanePath.h"
#include "CireLayoutEditorState.h"
#include "CireNav.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireLayoutGallery, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
namespace ML = CireMapLayout;
struct FLayoutGallery
{
    bool bChecked = false, bEnabled = false, bDone = false;
    int32 Stage = 0;
    double Started = 0, StageAt = 0;
    FString Directory;
    TArray<FString> Files;
    FString Pack, Vendor, Road;
};
FLayoutGallery Gallery;
void Shot(const FString& Name)
{
    const FString File = FPaths::Combine(Gallery.Directory, Name);
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    Gallery.Files.Add(File);
    UE_LOG(LogCireLayoutGallery, Display, TEXT("CIRE_LAYOUT_GALLERY_CAPTURE file=%s"), *File);
}
}

namespace CireLayoutGallery
{
bool Tick(ACireGameMode* Mode)
{
    if (!Gallery.bChecked)
    {
        Gallery.bChecked = true;
        Gallery.bEnabled = Mode && FParse::Param(FCommandLine::Get(), TEXT("CireLayoutGallery"));
        if (!Gallery.bEnabled) return false;
        Gallery.Started = FPlatformTime::Seconds();
        Gallery.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("LayoutGallery"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
        IFileManager::Get().MakeDirectory(*Gallery.Directory, true);
    }
    if (!Gallery.bEnabled || Gallery.bDone) return false;
    UWorld* World = Mode->GetWorld();
    APlayerController* Controller = World->GetFirstPlayerController();
    ACireHero* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
    ACireHUD* HUD = Controller ? Cast<ACireHUD>(Controller->GetHUD()) : nullptr;
    FCireLayoutEditorState* E = HUD ? HUD->LayoutEditorState() : nullptr;
    if (!Hero || !HUD || !E || !HUD->IsLayoutEditorOpen() || !Hero->bDrafted) return true;
    const double Now = FPlatformTime::Seconds(), Game = World->GetRealTimeSeconds();
    auto Next = [&]() { Gallery.StageAt = Now; ++Gallery.Stage; };
    auto Fail = [&](const TCHAR* Why) { UE_LOG(LogCireLayoutGallery, Error, TEXT("CIRE_LAYOUT_GALLERY_FAIL %s"), Why); Gallery.bDone = true; FPlatformMisc::RequestExitWithStatus(false, 1); return true; };
    auto Ground = [&](int32 Realm, const FVector2D& Local) { E->bDebugGround = true; E->DebugGround = CireLanePath::ToWorld(Realm, Local, 0.f); };
    auto MapView = [&](int32 Realm, const FVector2D& Local, float Distance, float Pitch, float Yaw)
    { E->bWalk = false; E->Realm = Realm; E->Focus = CireLanePath::ToWorld(Realm, Local, 0.f); E->Distance = Distance; E->Pitch = Pitch; E->Yaw = Yaw; };
    auto Pointer = [&](int32 Realm, const FVector2D& Local)
    {
        FVector2D Screen;
        if (Controller->ProjectWorldLocationToScreen(CireLanePath::ToWorld(Realm, Local, 0.f), Screen)) HUD->DebugSetPointer(Screen / FMath::Max(.01f, HUD->DebugScale()));
    };
    switch (Gallery.Stage)
    {
    case 0: // shaders and the navmesh first, then a known layout (the current march seeds it)
    {
        if (Now - Gallery.Started < 8 || (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - Gallery.Started < 240) || !CireNav::IsReady(World)) return true;
        E->bNoFiles = true;
        E->Layout = ML::FromRoutes(CireLanePath::Get(World)); E->Layout.Name = TEXT("Gallery");
        E->History.Reset(); E->Sel.Reset(); E->ChainId.Reset();
        FCireMapLayout& L = E->Layout;
        auto Edit = [&](TFunctionRef<bool(FCireMapLayout&)> Change) { CireLayoutEditor::Edit(*E, Change, Game); };
        const ECireMarkerOwner T1 = ECireMarkerOwner::Team1;
        // Every setter once around the market (realm-local, Team 1; mirrored to Team 2).
        Edit([&](FCireMapLayout& X)
        {
            for (int32 I = 0; I < 3; ++I) ML::Place(X, ML::PlayerSpawn, FVector2D(-1500, -400 + I * 400), T1, 0.f);
            Gallery.Pack = ML::Place(X, ML::ChallengePack, FVector2D(6600, -900), T1); ML::SetTier(X, Gallery.Pack, 2); ML::SetRadius(X, Gallery.Pack, 380.f);
            const FString Big = ML::Place(X, ML::ChallengePack, FVector2D(4200, 950), T1); ML::SetTier(X, Big, 4); ML::SetRadius(X, Big, 600.f);
            Gallery.Vendor = ML::Place(X, ML::Vendor, FVector2D(5200, -1000), T1, 90.f);
            const FString Armory = ML::Place(X, ML::Vendor, FVector2D(6000, 1050), T1, -90.f); ML::SetKind(X, Armory, TEXT("armory"));
            const FString Arcane = ML::Place(X, ML::Vendor, FVector2D(7200, 1050), T1, -90.f); ML::SetKind(X, Arcane, TEXT("arcane"));
            ML::Place(X, ML::BossSpawn, FVector2D(8200, 700), T1, 180.f);
            ML::Place(X, ML::Rift, FVector2D(2600, -1000), T1, 0.f);
            ML::Place(X, ML::Respawn, FVector2D(-1200, 700), T1, 0.f);
            ML::Place(X, ML::Blocker, FVector2D(3300, 900), T1);
            // A side gate whose path merges into the main road.
            const FString Side = ML::Place(X, ML::MonsterSpawn, FVector2D(8200, -1000), T1, 200.f);
            ML::SetName(X, Side, TEXT("Market gate"));
            Gallery.Road = ML::ChainPoint(X, ML::MonsterPath, FString(), FVector2D(7600, -600), T1, Side);
            Gallery.Road = ML::ChainPoint(X, ML::MonsterPath, Gallery.Road, FVector2D(7400, 500), T1);
            ML::SetName(X, Gallery.Road, TEXT("Market cut"));
            for (const FVector2D P : {FVector2D(-2250, -1350), FVector2D(43600, -1350), FVector2D(43600, 1350), FVector2D(-2250, 1350)})
                ML::ChainPoint(X, ML::PlayBounds, ML::OfType(X, ML::PlayBounds).Num() ? ML::OfType(X, ML::PlayBounds)[0]->Id : FString(), P, T1);
            return true;
        });
        CireLayoutEditor::RunValidation(World, *E, true); E->bShowIssues = true;
        UE_LOG(LogCireLayoutGallery, Display, TEXT("CIRE_LAYOUT_GALLERY_LAYOUT markers=%d issues=%d nav=%d"), L.Markers.Num(), E->Issues.Num(), E->bNavChecked ? 1 : 0);
        for (const FCireLayoutIssue& I : E->Issues) UE_LOG(LogCireLayoutGallery, Display, TEXT("CIRE_LAYOUT_GALLERY_ISSUE team=%d %s"), I.Team, *I.Message);
        MapView(0, FVector2D(5600, 0), 6200.f, -62.f, 180.f);
        HUD->DebugSetPointer(FVector2D(-1, -1));
        Next(); return true;
    }
    case 1:
        if (Now - Gallery.StageAt < 4) return true;
        Shot(TEXT("01_layout_map_view.png")); Next(); return true;
    case 2: // Replace on a pack: select it, arm Replace, the reticle at the new spot
    {
        E->Sel = {Gallery.Pack, INDEX_NONE, ECireVendorPart::Npc}; E->bReplace = true; E->Armed = NAME_None;
        Ground(0, FVector2D(6100, -700)); Pointer(0, FVector2D(6100, -700));
        if (Now - Gallery.StageAt < 2.5) return true;
        Shot(TEXT("02_replace_pack.png")); Next(); return true;
    }
    case 3: // walk view: the champion in the market, the Challenge Pack setter armed at the cursor
    {
        if (E->bWalk == false)
        {
            E->bReplace = false; E->Sel.Reset(); E->bWalk = true;
            CireRouteEditMode::TeleportTo(Hero, 0, FVector2D(8600, 0), 180.f);
            Controller->SetControlRotation(FRotator(-18.f, 180.f, 0.f));
        }
        E->Armed = ML::ChallengePack; E->NextTier = 3; E->NextRadius.Add(ML::ChallengePack, 520.f);
        Ground(0, FVector2D(7000, 350)); Pointer(0, FVector2D(7000, 350));
        if (Now - Gallery.StageAt < 4) return true;
        Shot(TEXT("03_walk_place_pack.png")); Next(); return true;
    }
    case 4: // the vendor group: NPC, sign and stall, the sign handle selected
    {
        E->Armed = NAME_None; E->bDebugGround = false; HUD->DebugSetPointer(FVector2D(-1, -1));
        E->Sel = {Gallery.Vendor, INDEX_NONE, ECireVendorPart::Sign};
        MapView(0, FVector2D(5250, -900), 2300.f, -48.f, 150.f);
        if (Now - Gallery.StageAt < 3.5) return true;
        Shot(TEXT("04_vendor_group.png")); Next(); return true;
    }
    case 5: // validation per team: break Team 2 on purpose (mirror off, its spawn removed), then show the problems
    {
        if (Now - Gallery.StageAt < .1) return true;
        if (E->bShowIssues && !E->Issues.ContainsByPredicate([](const FCireLayoutIssue& I) { return I.Team == 2; }))
        {
            const TArray<const FCireMapMarker*> Spawns = ML::OfType(E->Layout, ML::MonsterSpawn, ECireMarkerOwner::Team1);
            const FString Side = Spawns.Num() > 1 ? Spawns[1]->Id : FString();
            CireLayoutEditor::Edit(*E, [&](FCireMapLayout& X)
            {
                const FString Twin = ML::Find(X, Side) ? ML::Find(X, Side)->Pair : FString();
                ML::SetMirror(X, Side, false);
                return ML::Remove(X, Twin);
            }, Game);
            CireLayoutEditor::RunValidation(World, *E, true);
            CireLayoutEditor::Say(*E, FString::Printf(TEXT("%d problems found (listed on the right)."), E->Issues.Num()), Game);
            E->Sel.Reset(); E->TeamFilter = 2;
            MapView(1, FVector2D(7000, 0), 5200.f, -62.f, 180.f);
        }
        if (Now - Gallery.StageAt < 3.5) return true;
        UE_LOG(LogCireLayoutGallery, Display, TEXT("CIRE_LAYOUT_GALLERY_VALIDATION issues=%d team2=%d"), E->Issues.Num(),
            E->Issues.FilterByPredicate([](const FCireLayoutIssue& I) { return I.Team == 2; }).Num());
        Shot(TEXT("05_validation_team2.png"));
        CireLayoutEditor::Undo(*E, Game); E->TeamFilter = -1; CireLayoutEditor::RunValidation(World, *E, true);
        Next(); return true;
    }
    case 6: // preview: a monster walks every path in both realms
    {
        if (!E->bPreview) { CireLayoutEditor::StartPreview(World, *E); MapView(0, FVector2D(40500, 0), 4200.f, -55.f, 180.f); }
        if (E->Walkers.Num() == 0) return Fail(TEXT("the preview spawned no walkers"));
        if (Now - Gallery.StageAt < 9) return true;
        int32 Moving = 0;
        for (const FCireLayoutWalker& W : E->Walkers) Moving += W.Walked > 300.f ? 1 : 0;
        UE_LOG(LogCireLayoutGallery, Display, TEXT("CIRE_LAYOUT_GALLERY_PREVIEW walkers=%d moving=%d speed=%.0f"), E->Walkers.Num(), Moving, E->PreviewSpeed);
        if (Moving == 0) return Fail(TEXT("no preview monster walked"));
        Shot(TEXT("06_preview_walk.png")); Next(); return true;
    }
    case 7: // apply live (in memory: the route files are not written): packs rebuild on the ground in both realms
    {
        if (E->bPreview) CireLayoutEditor::StopPreview(*E);
        if (Now - Gallery.StageAt < .1) return true;
        static bool bApplied = false;
        if (!bApplied)
        {
            bApplied = true;
            FCireBattlefieldRoutes Routes; TArray<FString> Notes; FString Error;
            const bool bOk = ML::CompileRoutes(E->Layout, CireLanePath::Get(World), Routes, Notes) && CireLanePath::ApplyLive(World, Routes, &Error);
            UE_LOG(LogCireLayoutGallery, Display, TEXT("CIRE_LAYOUT_GALLERY_APPLY ok=%d packs=%d/%d %s %s"), bOk ? 1 : 0, CireLanePath::BayCount(World, 0), CireLanePath::BayCount(World, 1), *Error, *FString::Join(Notes, TEXT(" | ")));
            if (!bOk) return Fail(TEXT("apply"));
            CireLayoutEditor::Say(*E, TEXT("APPLIED (gallery: live only, no files written)."), Game);
            MapView(0, FVector2D(5400, 0), 5600.f, -70.f, 180.f);
        }
        if (Now - Gallery.StageAt < 4) return true;
        Shot(TEXT("07_applied_packs.png")); Next(); return true;
    }
    default:
    {
        if (Now - Gallery.StageAt < 2) return true;
        bool bOk = Gallery.Files.Num() == 7;
        for (const FString& F : Gallery.Files) bOk &= IFileManager::Get().FileSize(*F) > 10000;
        UE_LOG(LogCireLayoutGallery, Display, TEXT("CIRE_LAYOUT_GALLERY_%s captures=%d dir=%s"), bOk ? TEXT("DONE") : TEXT("INCOMPLETE"), Gallery.Files.Num(), *Gallery.Directory);
        Gallery.bDone = true;
        FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
        return true;
    }
    }
}
}
#endif
