// world-editor: -CireWorldEditGallery (with -CireRouteEdit -CireTown; Tools/RunWorldEditGallery.py). A scripted session in
// the map layout editor's WORLD tab that captures it: the tab over a house block in map view, a whole building selected,
// the building removed (REMOVED list, savings), ghosts, undo. It writes no data files (bNoFiles). Logs
// CIRE_WORLD_EDIT_GALLERY_DONE captures=N dir=... and exits.
#include "CireWorldEdit.h"
#include "CireWorldEditorState.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireLanePath.h"
#include "CireLayoutEditorState.h"
#include "CireRouteEditMode.h"
#include "CireTownMap.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireWorldEditGallery, Log, All);

#if !UE_BUILD_SHIPPING
namespace
{
struct FWEGallery
{
    bool bChecked = false, bOn = false, bDone = false;
    int32 Stage = 0;
    double StageAt = 0;
    FString Dir;
    int32 Shots = 0;
};
FWEGallery GWEG;
void WEShot(const TCHAR* Name)
{
    const FString File = FPaths::Combine(GWEG.Dir, Name);
    FScreenshotRequest::RequestScreenshot(File, true, false, false, FIntRect(), true);
    ++GWEG.Shots;
    UE_LOG(LogCireWorldEditGallery, Display, TEXT("CIRE_WORLD_EDIT_GALLERY_CAPTURE file=%s"), *File);
}
}
#endif

void CireWorldEdit::TickGallery(UWorld* World)
{
#if !UE_BUILD_SHIPPING
    if (!GWEG.bChecked)
    {
        GWEG.bChecked = true;
        GWEG.bOn = FParse::Param(FCommandLine::Get(), TEXT("CireWorldEditGallery"));
        if (!GWEG.bOn) return;
        GWEG.Dir = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("WorldEditGallery"), FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S"))));
        IFileManager::Get().MakeDirectory(*GWEG.Dir, true);
    }
    if (!GWEG.bOn || GWEG.bDone || !World) return;
    APlayerController* PC = World->GetFirstPlayerController();
    ACireHUD* HUD = PC ? Cast<ACireHUD>(PC->GetHUD()) : nullptr;
    FCireLayoutEditorState* E = HUD ? HUD->LayoutEditorState() : nullptr;
    const double Now = FPlatformTime::Seconds();
    if (!E || !CireTownMap::IsActive() || CireTownMap::LoadedLevels(World) == 0) { GWEG.StageAt = Now; return; }
    if (Now - GWEG.StageAt < (GWEG.Stage == 0 ? 10.0 : 3.0)) return;
    GWEG.StageAt = Now;
    // The house the session works on: SL_Houses/LevelInstance_65 (a large timber house) in DAYLIGHT.
    const FString Key = TEXT("0|SL_Houses/LevelInstance_65");
    FCireWorldEditorState* W = E->WorldEdit.Get();
    switch (GWEG.Stage++)
    {
    case 0:
        E->WorldEdit = MakeShared<FCireWorldEditorState>(); E->WorldEdit->bNoFiles = true;
        E->bWorldTab = true; E->bWalk = false; E->Realm = 0;
        E->Focus = CireLanePath::ToWorld(0, FVector2D(-4370, 2706), 0.f) + FVector(0, 0, CireTownMap::RealmOrigin(0).Z + 800.f);
        E->Yaw = 210.f; E->Pitch = -52.f; E->Distance = 5200.f;
        break;
    case 1: WEShot(TEXT("01_world_tab.png")); break;
    case 2: if (W) { W->Sel = {Key}; } break;
    case 3: WEShot(TEXT("02_building_selected.png")); break;
    case 4: if (W) { CireLayoutEditor::Say(*E, CireWorldEditor::RemoveKeys(*W, E->Layout, W->Sel), World->GetRealTimeSeconds()); W->Sel.Reset(); } break;
    case 5: WEShot(TEXT("03_removed.png")); break;
    case 6: if (W) W->bGhosts = true; break;
    case 7: WEShot(TEXT("04_ghosts.png")); break;
    case 8: if (W) { W->bGhosts = false; FString What; CireWorldEdit::Undo(W->Doc, &What); } break;
    case 9: WEShot(TEXT("05_undone.png")); break;
    default:
        GWEG.bDone = true;
        UE_LOG(LogCireWorldEditGallery, Display, TEXT("CIRE_WORLD_EDIT_GALLERY_DONE captures=%d dir=%s"), GWEG.Shots, *GWEG.Dir);
        FPlatformMisc::RequestExit(false, TEXT("CireWorldEditGallery"));
    }
#endif
}
