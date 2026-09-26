// video-crash: -CireVideoCycle regression fixture (Tools/RunVideoCycle.py).
//
// Real windowed game (not offscreen). With the Options > Video page open and the HUD drawing, it
// applies every scalability preset (Low .. Cinematic) at 1600x900 and 1920x1080 through the same
// CireVideo queue the Options buttons use, plus render-scale and window-mode changes, first on the
// champion-select screen (live 3D preview) and then again in the match. After each change it waits
// for the preview to re-meter and saves a screenshot (+ the raw figure render) so the champion can
// be compared across presets. It also scans every primitive for NaN / zero-scale / out-of-range
// transforms (the distance-field ensures of Eric's crash reports) and proves an apply requested
// inside the HUD draw is deferred. The runner fails on any ensure, crash or CIRE_VIDEO_CYCLE_FAIL.
#include "CireVideoSettings.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#if !UE_BUILD_SHIPPING
#include "CireDraftStage.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "TextureResource.h"
#include "UnrealClient.h"
#include "Scalability.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireVideoCycle, Log, All);

namespace
{
struct FStep { FString Name; FCireVideoState State; bool bOptionsOpen = true; };
struct FCycle
{
    bool bInit = false, bDone = false, bPass = true;
    int32 Phase = 0; // 0 boot, 1 draft steps, 2 drafting, 3 match steps, 4 finish
    int32 Step = 0, SubStage = 0, Checks = 0;
    double Started = 0, StageAt = 0;
    uint64 StageFrame = 0;
    TArray<FStep> DraftSteps, MatchSteps;
    FCireVideoState Original;
    FString Directory;
    int32 DeferralBaseline = 0;
    int32 Shots = 0;
};
FCycle G;

void Check(bool bOk, const FString& Name)
{
    ++G.Checks; G.bPass &= bOk;
    if (bOk) { UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_CYCLE_CHECK_PASS %s"), *Name); }
    else { UE_LOG(LogCireVideoCycle, Error, TEXT("CIRE_VIDEO_CYCLE_CHECK_FAIL %s"), *Name); }
}

TArray<FStep> BuildSteps(const TCHAR* Prefix, bool bFull)
{
    static const TCHAR* Names[] = { TEXT("low"), TEXT("medium"), TEXT("high"), TEXT("epic"), TEXT("cine") };
    TArray<FStep> Out;
    const FIntPoint Sizes[] = { FIntPoint(1600, 900), FIntPoint(1920, 1080) };
    for (int32 Q = 0; Q < 5; ++Q)
        for (const FIntPoint& Size : Sizes)
        {
            if (!bFull && Size.X != 1600) continue;
            FStep S; S.State.Resolution = Size; S.State.WindowMode = 2; S.State.Quality = Q; S.State.RenderScale = 100; S.State.bVSync = false; S.State.FrameRateLimit = 60;
            S.Name = FString::Printf(TEXT("%s_q%d_%s_%dx%d"), Prefix, Q, Names[Q], Size.X, Size.Y);
            Out.Add(S);
        }
    // Render scale and window mode through the same path (Eric's crash: Cine + 2560x1440 at once).
    FStep Scale; Scale.State.Resolution = FIntPoint(1600, 900); Scale.State.WindowMode = 2; Scale.State.Quality = 3; Scale.State.RenderScale = 50; Scale.State.bVSync = false; Scale.State.FrameRateLimit = 60;
    Scale.Name = FString::Printf(TEXT("%s_q3_scale50_1600x900"), Prefix); Out.Add(Scale);
    FStep Big = Scale; Big.State.Resolution = FIntPoint(2560, 1440); Big.State.Quality = 4; Big.State.RenderScale = 75; Big.Name = FString::Printf(TEXT("%s_q4_scale75_2560x1440"), Prefix); Out.Add(Big);
    FStep Borderless = Scale; Borderless.State.WindowMode = 1; Borderless.State.RenderScale = 100; Borderless.Name = FString::Printf(TEXT("%s_q3_borderless"), Prefix); Out.Add(Borderless);
    if (FParse::Param(FCommandLine::Get(), TEXT("CireVideoCycleFullscreen")))
    { FStep Full = Borderless; Full.State.WindowMode = 0; Full.Name = FString::Printf(TEXT("%s_q3_fullscreen"), Prefix); Out.Add(Full); }
    FStep Back = Scale; Back.State.RenderScale = 100; Back.bOptionsOpen = false; Back.Name = FString::Printf(TEXT("%s_q3_windowed_again"), Prefix); Out.Add(Back);
    return Out;
}

ACireDraftStage* FindStage(UWorld* W)
{
    for (TCireActorIterator<ACireDraftStage> It(W); It; ++It) if (!It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}

void SaveFigure(ACireDraftStage* Stage, const FString& Name)
{
    UTextureRenderTarget2D* RT = Stage ? Stage->GetRenderTarget() : nullptr;
    FTextureRenderTargetResource* Res = RT ? RT->GameThread_GetRenderTargetResource() : nullptr;
    TArray<FColor> Px;
    if (!Res || !Res->ReadPixels(Px) || Px.Num() != int32(RT->SizeX * RT->SizeY)) return;
    // Luma of the champion's own pixels (post-process alpha is inverse opacity: A < 128 = figure).
    int64 Count = 0, Clipped = 0; double Sum = 0;
    for (const FColor& C : Px) if (C.A < 128) { const float Y = .2126f * C.R + .7152f * C.G + .0722f * C.B; Sum += Y; ++Count; Clipped += (C.R >= 250 || C.G >= 250 || C.B >= 250); }
    for (FColor& C : Px) C.A = 255 - C.A; // store as normal alpha for review
    FImageUtils::SaveImageByExtension(*FPaths::Combine(G.Directory, Name + TEXT(".figure.png")), FImageView(Px.GetData(), RT->SizeX, RT->SizeY));
    UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_CYCLE_FIGURE %s target=%dx%d figure_px=%lld mean_luma=%.1f clipped=%.4f exposure=%.2f median=%.0f p97=%.0f meter_clip=%.4f settled=%d"),
        *Name, RT->SizeX, RT->SizeY, Count, Count ? Sum / Count : 0.0, Count ? double(Clipped) / Count : 0.0, Stage->GetExposureOffset(), Stage->MeteredMedian(), Stage->MeteredHighlight(), Stage->MeteredClipShare(), Stage->IsContentSettled() ? 1 : 0);
}
}

namespace CireVideoCycle
{
bool IsActive()
{
    static const bool bActive = FParse::Param(FCommandLine::Get(), TEXT("CireVideoCycle")) || FCommandLine::Get() && FString(FCommandLine::Get()).Contains(TEXT("-CireVideoPersist="));
    return bActive;
}

// -CireVideoPersist=set|check (Tools/RunVideoCycle.py --persist): the "save and relaunch" flow. "set" keeps
// 1280x720 windowed, High, 80 % render scale through the Options queue and quits; the runner relaunches the
// game exactly like Play.cmd (no -ResX/-windowed) and "check" verifies the new process started with them.
static bool TickPersist(ACireController* C, const FString& Mode)
{
    static double Started = 0; static int32 Stage = 0;
    const double Now = FPlatformTime::Seconds(); if (Started == 0) Started = Now;
    if (Now - Started < 4.0) return true;
    FCireVideoState Want; Want.Resolution = FIntPoint(1280, 720); Want.WindowMode = 2; Want.Quality = 2; Want.RenderScale = 80; Want.bVSync = false; Want.FrameRateLimit = 90;
    if (Mode == TEXT("set"))
    {
        if (Stage == 0) { CireVideo::RequestPreview(Want, 60.0); CireVideo::RequestKeep(); Stage = 1; return true; }
        if (CireVideo::IsBusy() || Now - Started < 6.0) return true;
        UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_PERSIST_SET %s"), *CireVideo::Current().ToString());
        FPlatformMisc::RequestExitWithStatus(false, 0);
        Stage = 2; return true;
    }
    if (Stage != 0) return true;
    Stage = 1;
    const FCireVideoState Got = CireVideo::Current();
    FIntPoint ViewportSize(0, 0);
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
    const bool bOk = Got == Want && ViewportSize == Want.Resolution && Scalability::GetQualityLevels().ShadowQuality == 2;
    UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_PERSIST_%s settings=%s viewport=%dx%d shadows=%d"), bOk ? TEXT("PASS") : TEXT("FAIL"), *Got.ToString(), ViewportSize.X, ViewportSize.Y, Scalability::GetQualityLevels().ShadowQuality);
    // Restore the first-launch defaults for the next person who runs Play.cmd from this worktree.
    FCireVideoState Defaults; Defaults.Resolution = FIntPoint(1600, 900); Defaults.WindowMode = 2; Defaults.Quality = 3; Defaults.RenderScale = 100; Defaults.bVSync = false; Defaults.FrameRateLimit = 0;
    CireVideo::ApplyNow(Defaults, true);
    FPlatformMisc::RequestExitWithStatus(false, bOk ? 0 : 1);
    return true;
}

void Tick(ACireController* C)
{
    if (!IsActive() || G.bDone || !C || !C->IsLocalController()) return;
    if (FString Persist; FParse::Value(FCommandLine::Get(), TEXT("CireVideoPersist="), Persist)) { TickPersist(C, Persist); return; }
    UWorld* W = C->GetWorld(); auto* HUD = Cast<ACireHUD>(C->GetHUD());
    if (!W || !HUD) return;
    const double Now = FPlatformTime::Seconds();
    auto Finish = [&](const TCHAR* Why)
    {
        G.bDone = true;
        CireVideo::ApplyNow(G.Original, true); // leave the profile as it was
        UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_CYCLE_%s checks=%d shots=%d phase=%d step=%d directory=%s %s"), G.bPass ? TEXT("PASS") : TEXT("FAIL"), G.Checks, G.Shots, G.Phase, G.Step, *G.Directory, Why);
        FPlatformMisc::RequestExitWithStatus(false, G.bPass ? 0 : 1);
    };
    if (!G.bInit)
    {
        G.bInit = true; G.Started = Now; G.StageAt = Now; G.StageFrame = GFrameCounter;
        G.Original = CireVideo::Current();
        G.DraftSteps = BuildSteps(TEXT("draft"), true);
        G.MatchSteps = BuildSteps(TEXT("match"), false);
        FString Tag; FParse::Value(FCommandLine::Get(), TEXT("CireVideoCycleTag="), Tag);
        G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("VideoCycle"), FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ")) + (Tag.IsEmpty() ? FString() : TEXT("_") + Tag)));
        IFileManager::Get().MakeDirectory(*G.Directory, true);
        UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_CYCLE_START original=%s directory=%s"), *G.Original.ToString(), *G.Directory);
    }
    if (Now - G.Started > 900) { G.bPass = false; Finish(TEXT("timeout")); return; }
    auto* H = Cast<ACireHero>(C->GetPawn());
    // Keep the real OS cursor (wherever it is over the window) from hovering other champions.
    if (G.Phase <= 1) HUD->DebugSetPointer(HUD->LogicalViewport() - FVector2D(1.f, 1.f));
    auto NextStage = [&]() { G.SubStage = 0; G.StageAt = Now; G.StageFrame = GFrameCounter; };

    // Runs one step: open Options > Video, request preview + keep (as the buttons do, from inside
    // the HUD's frame), wait for the resize and the preview to settle, then capture.
    auto RunStep = [&](const FStep& S, bool bDraft) -> bool
    {
        ACireDraftStage* Stage = bDraft ? FindStage(W) : nullptr;
        switch (G.SubStage)
        {
        case 0:
            HUD->DebugOptionsPage(2, 0, S.bOptionsOpen);
            {
                // Same as the Options buttons: requested while the HUD is drawing (draw scope held).
                const CireVideo::FDrawScope InsideDraw;
                CireVideo::RequestPreview(S.State, 60.0);
                CireVideo::RequestKeep();
            }
            ++G.SubStage; G.StageAt = Now; G.StageFrame = GFrameCounter; return false;
        case 1:
            if (CireVideo::IsBusy() || GFrameCounter < G.StageFrame + 20 || Now - G.StageAt < 1.0) return false;
            {
                const FCireVideoState Got = CireVideo::Current();
                Check(Got.Quality == S.State.Quality && Got.Resolution == S.State.Resolution && Got.WindowMode == S.State.WindowMode && FMath::IsNearlyEqual(Got.RenderScale, S.State.RenderScale, 1.f),
                    FString::Printf(TEXT("%s applied (%s)"), *S.Name, *Got.ToString()));
                Check(!CireVideo::IsPreviewPending(), FString::Printf(TEXT("%s kept"), *S.Name));
                FIntPoint ViewportSize(0, 0);
                if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport) ViewportSize = GEngine->GameViewport->Viewport->GetSizeXY();
                UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_CYCLE_VIEWPORT %s viewport=%dx%d"), *S.Name, ViewportSize.X, ViewportSize.Y);
                if (S.State.WindowMode == 2 && S.State.Resolution.X <= 1920) Check(ViewportSize == S.State.Resolution, FString::Printf(TEXT("%s viewport resized to %dx%d"), *S.Name, ViewportSize.X, ViewportSize.Y));
            }
            HUD->DebugOptionsPage(2, 0, false); // capture the screen itself, not the Options window
            ++G.SubStage; G.StageAt = Now; G.StageFrame = GFrameCounter; return false;
        case 2:
        {
            // The preview re-renders at the new size / preset: wait for it to be metered and settled.
            const bool bStageReady = !bDraft || (Stage && Stage->IsPreviewReady() && Stage->FramesShown() > 30);
            if (!(bStageReady && GFrameCounter > G.StageFrame + 45 && Now - G.StageAt > 2.5) && Now - G.StageAt < 20.0) return false;
            if (bDraft) Check(bStageReady, FString::Printf(TEXT("%s draft preview ready"), *S.Name));
            const FString File = FPaths::Combine(G.Directory, S.Name + TEXT(".png"));
            FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
            ++G.Shots;
            if (Stage) SaveFigure(Stage, S.Name);
            const int32 Bad = CireRenderSanity::Scan(W, true);
            Check(Bad == 0, FString::Printf(TEXT("%s render sanity (%d bad primitives)"), *S.Name, Bad));
            UE_LOG(LogCireVideoCycle, Display, TEXT("CIRE_VIDEO_CYCLE_SHOT %s file=%s"), *S.Name, *File);
            ++G.SubStage; G.StageAt = Now; G.StageFrame = GFrameCounter; return false;
        }
        default:
            return GFrameCounter > G.StageFrame + 10 && Now - G.StageAt > .5; // screenshot written
        }
    };

    switch (G.Phase)
    {
    case 0: // boot: the world has rendered, the draft screen is up; nothing may be NaN / zero-scale
    {
        ACireDraftStage* Stage = FindStage(W);
        if (!(Stage && Stage->IsPreviewReady()) && Now - G.StageAt < 60.0) return;
        if (GFrameCounter < G.StageFrame + 60) return;
        Check(Stage != nullptr, TEXT("draft stage present"));
        Check(CireRenderSanity::Scan(W, true) == 0, TEXT("boot render sanity"));
        // An apply requested *inside* the draw must be deferred, never run (the canvas crash).
        const int32 Before = CireVideo::AppliesDuringDraw();
        { const CireVideo::FDrawScope InsideDraw; CireVideo::ApplyNow(CireVideo::Current(), false); }
        Check(CireVideo::AppliesDuringDraw() == Before + 1 && CireVideo::IsBusy(), TEXT("apply inside a draw is deferred to the next tick"));
        G.DeferralBaseline = CireVideo::AppliesDuringDraw();
        G.Phase = 1; G.Step = 0; NextStage();
        return;
    }
    case 1:
        if (CireVideo::IsBusy() && G.SubStage == 0) return; // the deferred probe apply runs first
        if (CireVideo::IsPreviewPending() && G.SubStage == 0) { CireVideo::RequestKeep(); return; }
        if (!G.DraftSteps.IsValidIndex(G.Step)) { G.Phase = 2; NextStage(); return; }
        if (RunStep(G.DraftSteps[G.Step], true)) { ++G.Step; NextStage(); }
        return;
    case 2: // lock a champion and wait for the match to start
        if (!H) return;
        if (!H->bDrafted) { if (GFrameCounter % 30 == 0) C->ServerAction(5, 4, nullptr); if (Now - G.StageAt > 60) { Check(false, TEXT("drafted")); Finish(TEXT("draft")); } return; }
        if (Now - G.StageAt < 12.0) return;
        Check(true, TEXT("match started"));
        HUD->SetSkillOfferOpen(false); // capture the world, not the opening-skill cards
        G.Phase = 3; G.Step = 0; NextStage();
        return;
    case 3:
        if (!G.MatchSteps.IsValidIndex(G.Step)) { G.Phase = 4; NextStage(); return; }
        if (RunStep(G.MatchSteps[G.Step], false)) { ++G.Step; NextStage(); }
        return;
    default:
        Check(CireVideo::AppliesDuringDraw() == G.DeferralBaseline, TEXT("no video apply ran inside a HUD draw"));
        Check(CireRenderSanity::Scan(W, true) == 0, TEXT("final render sanity"));
        Finish(TEXT("done"));
        return;
    }
}
}
#endif
