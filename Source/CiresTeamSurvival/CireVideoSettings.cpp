#include "CireVideoSettings.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LineBatchComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "UObject/UObjectIterator.h"
#include "ComponentRecreateRenderStateContext.h"
#include "Misc/ConfigCacheIni.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireVideo, Log, All);

FString FCireVideoState::ToString() const
{
    return FString::Printf(TEXT("%dx%d mode=%d quality=%d scale=%.0f vsync=%d fps=%.0f"), Resolution.X, Resolution.Y, WindowMode, Quality, RenderScale, bVSync ? 1 : 0, FrameRateLimit);
}

namespace
{
struct FVideoQueue
{
    bool bPreviewQueued = false, bKeepQueued = false, bRevertQueued = false;
    FCireVideoState Wanted, Previous;
    double ConfirmSeconds = 15.0;
    bool bPreviewLive = false;
    double Deadline = 0;
    FTSTicker::FDelegateHandle Ticker;
    int32 AppliesDuringDraw = 0;
    int32 DrawDepth = 0;
};
FVideoQueue& Queue() { static FVideoQueue Q; return Q; }

UGameUserSettings* Settings() { return GEngine ? GEngine->GetGameUserSettings() : nullptr; }

bool Process(float)
{
    FVideoQueue& Q = Queue();
    if (Q.DrawDepth > 0) return true; // never inside a draw; try again next tick
    if (Q.bPreviewQueued)
    {
        Q.bPreviewQueued = false;
        if (!Q.bPreviewLive) Q.Previous = CireVideo::Current(); // a second Apply keeps the original restore point
        CireVideo::ApplyNow(Q.Wanted, false);
        Q.bPreviewLive = true; Q.Deadline = FPlatformTime::Seconds() + Q.ConfirmSeconds;
        UE_LOG(LogCireVideo, Display, TEXT("CIRE_VIDEO_PREVIEW %s (was %s)"), *Q.Wanted.ToString(), *Q.Previous.ToString());
    }
    if (Q.bRevertQueued)
    {
        Q.bRevertQueued = false;
        if (Q.bPreviewLive) { CireVideo::ApplyNow(Q.Previous, false); UE_LOG(LogCireVideo, Display, TEXT("CIRE_VIDEO_REVERT %s"), *Q.Previous.ToString()); }
        Q.bPreviewLive = false;
    }
    if (Q.bKeepQueued)
    {
        Q.bKeepQueued = false;
        if (UGameUserSettings* S = Settings()) { S->ConfirmVideoMode(); S->SaveSettings(); }
        Q.bPreviewLive = false;
        UE_LOG(LogCireVideo, Display, TEXT("CIRE_VIDEO_KEEP %s saved=%s"), *CireVideo::Current().ToString(), *GGameUserSettingsIni);
    }
    if (Q.bPreviewLive && FPlatformTime::Seconds() >= Q.Deadline)
    {
        CireVideo::ApplyNow(Q.Previous, false); Q.bPreviewLive = false;
        UE_LOG(LogCireVideo, Display, TEXT("CIRE_VIDEO_REVERT timeout %s"), *Q.Previous.ToString());
    }
    // Stay registered only while there is something to wait for.
    if (!CireVideo::IsBusy() && !Q.bPreviewLive) { Q.Ticker.Reset(); return false; }
    return true;
}

void Arm()
{
    FVideoQueue& Q = Queue();
    if (!Q.Ticker.IsValid()) Q.Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Process));
}
}

namespace CireVideo
{
FDrawScope::FDrawScope() { ++Queue().DrawDepth; }
FDrawScope::~FDrawScope() { --Queue().DrawDepth; }

FCireVideoState Current()
{
    FCireVideoState S;
    if (UGameUserSettings* G = Settings())
    {
        S.Resolution = G->GetScreenResolution();
        S.WindowMode = static_cast<int32>(G->GetFullscreenMode());
        const int32 Overall = G->GetOverallScalabilityLevel();
        S.Quality = Overall >= 0 ? FMath::Clamp(Overall, 0, 4) : FMath::Clamp(G->ScalabilityQuality.ShadowQuality, 0, 4); // custom mix: nearest preset
        float Normalized = 1, Value = 100, Min = 0, Max = 100;
        G->GetResolutionScaleInformationEx(Normalized, Value, Min, Max);
        S.RenderScale = Value > 0 ? Value : 100.f;
        S.bVSync = G->IsVSyncEnabled();
        S.FrameRateLimit = G->GetFrameRateLimit();
    }
    return S;
}

void ApplyNow(const FCireVideoState& In, bool bSave)
{
    UGameUserSettings* G = Settings();
    if (!G) return;
    if (Queue().DrawDepth > 0)
    {
        // Resizing the viewport here would free the canvas the engine is drawing with. Defer.
        ++Queue().AppliesDuringDraw;
        UE_LOG(LogCireVideo, Error, TEXT("CIRE_VIDEO_APPLY_DURING_DRAW deferred %s"), *In.ToString());
        RequestPreview(In); if (bSave) RequestKeep();
        return;
    }
    FCireVideoState S = In;
    S.Quality = FMath::Clamp(S.Quality, 0, 4);
    S.WindowMode = FMath::Clamp(S.WindowMode, 0, 2);
    S.RenderScale = FMath::IsFinite(S.RenderScale) ? FMath::Clamp(S.RenderScale, 50.f, 100.f) : 100.f;
    S.FrameRateLimit = FMath::IsFinite(S.FrameRateLimit) ? FMath::Clamp(S.FrameRateLimit, 0.f, 360.f) : 0.f;
    if (S.Resolution.X < 640 || S.Resolution.Y < 360) S.Resolution = G->GetScreenResolution();
    G->SetScreenResolution(S.Resolution);
    G->SetFullscreenMode(static_cast<EWindowMode::Type>(S.WindowMode));
    G->SetOverallScalabilityLevel(S.Quality);
    G->SetResolutionScaleValueEx(S.RenderScale); // after the preset, which resets the render scale
    G->SetVSyncEnabled(S.bVSync);
    G->SetFrameRateLimit(S.FrameRateLimit);
    {
        // As UGameUserSettings::ApplySettings, minus its save: a preview is saved only once kept.
        FGlobalComponentRecreateRenderStateContext Context;
        G->ApplyResolutionSettings(false);
        G->ApplyNonResolutionSettings();
    }
    if (bSave) { G->ConfirmVideoMode(); G->SaveSettings(); }
    UE_LOG(LogCireVideo, Log, TEXT("CIRE_VIDEO_APPLIED %s save=%d"), *S.ToString(), bSave ? 1 : 0);
}

void RequestPreview(const FCireVideoState& State, double ConfirmSeconds)
{
    FVideoQueue& Q = Queue();
    Q.Wanted = State; Q.ConfirmSeconds = FMath::Clamp(ConfirmSeconds, 1.0, 120.0);
    Q.bPreviewQueued = true; Q.bRevertQueued = false; Q.bKeepQueued = false;
    Arm();
}
void RequestKeep() { FVideoQueue& Q = Queue(); if (!Q.bPreviewLive && !Q.bPreviewQueued) return; Q.bKeepQueued = true; Q.bRevertQueued = false; Arm(); }
void RequestRevert()
{
    FVideoQueue& Q = Queue();
    if (Q.bPreviewQueued && !Q.bPreviewLive) { Q.bPreviewQueued = false; return; } // never applied: just drop it
    if (!Q.bPreviewLive) return;
    Q.bPreviewQueued = false; Q.bKeepQueued = false; Q.bRevertQueued = true; Arm();
}
bool IsPreviewPending() { return Queue().bPreviewLive || Queue().bPreviewQueued; }
bool IsBusy() { const FVideoQueue& Q = Queue(); return Q.bPreviewQueued || Q.bKeepQueued || Q.bRevertQueued; }
double SecondsToRevert() { const FVideoQueue& Q = Queue(); return Q.bPreviewLive ? FMath::Max(0.0, Q.Deadline - FPlatformTime::Seconds()) : Q.ConfirmSeconds; }
int32 AppliesDuringDraw() { return Queue().AppliesDuringDraw; }
}

// ---------------------------------------------------------------------------------------------
// Render sanity: find primitives the distance-field / GPU scene cannot convert (NaN, zero scale,
// out-of-float-range). The engine ensures on these every frame (DistanceFieldObjectManagement.cpp).
// ---------------------------------------------------------------------------------------------
namespace
{
// DoubleFloat.cpp: UE_DF_FLOAT_MAX_VALUE, the largest float with 0.25 cm precision.
constexpr double DFMax = double(1 << 23) * .25 - 1.0;
bool CheckTransform(const FTransform& T, const FBoxSphereBounds* LocalBounds, FString& Why, double* OutDet = nullptr)
{
    const FVector L = T.GetLocation(), S = T.GetScale3D();
    if (L.ContainsNaN() || S.ContainsNaN() || T.GetRotation().ContainsNaN() || !FMath::IsFinite(L.X + L.Y + L.Z + S.X + S.Y + S.Z)) { Why = TEXT("nan"); return false; }
    // What the distance-field scene does with it (DistanceFieldObjectManagement.cpp): a float
    // local-to-world relative to the bounds centre, then InverseFast (fails at |det| ~ 0).
    const FMatrix44f M(T.ToMatrixWithScale());
    const double Det = FMath::Abs(double(M.Determinant()));
    if (OutDet) *OutDet = Det;
    if (S.GetAbsMin() < 1.e-3 || Det < 1.e-6) { Why = FString::Printf(TEXT("degenerate_scale %s det=%g"), *S.ToString(), Det); return false; }
    if (L.GetAbsMax() > DFMax) { Why = FString::Printf(TEXT("far %s"), *L.ToString()); return false; }
    if (LocalBounds)
    {
        if (LocalBounds->ContainsNaN() || LocalBounds->BoxExtent.GetAbsMax() <= 0.0) { Why = FString::Printf(TEXT("mesh_bounds %s"), *LocalBounds->BoxExtent.ToString()); return false; }
        const FVector Offset = T.TransformVector(LocalBounds->Origin);
        if (Offset.GetAbsMax() > DFMax * .5) { Why = FString::Printf(TEXT("pivot_far_from_bounds %s"), *Offset.ToString()); return false; }
    }
    return true;
}
}

namespace CireRenderSanity
{
bool IsRenderSafe(const FTransform& Transform, FString* Why)
{
    FString Reason;
    const bool bOk = CheckTransform(Transform, nullptr, Reason);
    if (Why) *Why = Reason;
    return bOk;
}

int32 Scan(UWorld* World, bool bLogEach)
{
    if (!World) return 0;
    int32 Bad = 0, Checked = 0, Instances = 0;
    for (TObjectIterator<UPrimitiveComponent> It; It; ++It)
    {
        UPrimitiveComponent* C = *It;
        if (!IsValid(C) || C->GetWorld() != World || !C->IsRegistered() || C->IsTemplate() || C->IsA<ULineBatchComponent>()) continue;
        ++Checked;
        const UStaticMeshComponent* SM = Cast<UStaticMeshComponent>(C);
        const UStaticMesh* Mesh = SM ? SM->GetStaticMesh() : nullptr;
        const FBoxSphereBounds MeshBounds = Mesh ? Mesh->GetBounds() : FBoxSphereBounds(ForceInit);
        const auto Report = [&](int32 Instance, const FString& Why)
        {
            ++Bad;
            if (bLogEach && Bad <= 60)
                UE_LOG(LogCireVideo, Error, TEXT("CIRE_RENDER_SANITY_BAD actor=%s component=%s class=%s mesh=%s instance=%d visible=%d df=%d reason=%s"),
                    *GetNameSafe(C->GetOwner()), *C->GetName(), *C->GetClass()->GetName(), *GetNameSafe(Mesh), Instance, C->IsVisible() ? 1 : 0, C->bAffectDistanceFieldLighting ? 1 : 0, *Why);
        };
        FString Why;
        if (Mesh && Mesh->GetRenderData() && (Mesh->GetRenderData()->Bounds.ContainsNaN() || !FMath::IsFinite(Mesh->GetRenderData()->Bounds.SphereRadius))) Report(-1, TEXT("mesh_render_bounds_nan"));
        if (!CheckTransform(C->GetComponentTransform(), Mesh ? &MeshBounds : nullptr, Why)) Report(-1, Why);
        else if (C->Bounds.ContainsNaN() || C->Bounds.BoxExtent.GetAbsMax() > 1.e8) Report(-1, FString::Printf(TEXT("bounds %s"), *C->Bounds.BoxExtent.ToString()));
        if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(C))
        {
            const int32 N = ISM->GetInstanceCount();
            for (int32 I = 0; I < N; ++I)
            {
                FTransform T; if (!ISM->GetInstanceTransform(I, T, true)) continue;
                ++Instances;
                if (!CheckTransform(T, Mesh ? &MeshBounds : nullptr, Why)) Report(I, Why);
            }
        }
    }
    UE_LOG(LogCireVideo, Display, TEXT("CIRE_RENDER_SANITY world=%s components=%d instances=%d bad=%d"), *World->GetName(), Checked, Instances, Bad);
    return Bad;
}
}

// ---------------------------------------------------------------------------------------------
// video-crash: static meshes whose cooked/DDC render bounds are NaN. The Polyphoria heater shield
// derived for the Iron Warden and both Relic Paladins (/Game/FabDerived/Props/Polyphoria/
// SM_wp_shield_tri_01_a) loads with NaN FStaticMeshRenderData::Bounds while its asset bounds
// (ExtendedBounds, what components use) are sane. The GPU scene takes each instance's local bounds
// from the render data, so the distance-field scene got a NaN world position for the shield: the
// "precision loss converting matrix to GPU format" ensure and the "InverseFast non-invertible
// matrix" error on every frame the shield moved (Eric's crash reports _0000/_0001). The repair
// restores the render bounds from the asset bounds and re-creates the render state of every
// component showing that mesh; a watcher repairs meshes as they load.
// ---------------------------------------------------------------------------------------------
#include "StaticMeshResources.h"
#include "DistanceFieldAtlas.h"
#include "Misc/CoreDelegates.h"
#include "RenderingThread.h"
namespace
{
bool BoundsOk(const FBoxSphereBounds& B) { return !B.ContainsNaN() && FMath::IsFinite(B.SphereRadius) && B.BoxExtent.GetAbsMax() < 1.e7; }
TSet<FObjectKey>& SeenMeshes() { static TSet<FObjectKey> Seen; return Seen; }
int32 GRepairedMeshes = 0;
}
namespace CireRenderSanity
{
bool RepairMesh(UStaticMesh* Mesh)
{
    FStaticMeshRenderData* RD = Mesh ? Mesh->GetRenderData() : nullptr;
    if (!RD || BoundsOk(RD->Bounds)) return false;
    FBoxSphereBounds Good = Mesh->GetBounds();
    if (!BoundsOk(Good)) Good = FBoxSphereBounds(FVector::ZeroVector, FVector(50.f), 87.f);
    bool bDFBad = false;
    for (const FStaticMeshLODResources& LOD : RD->LODResources)
        if (LOD.DistanceFieldData && (LOD.DistanceFieldData->LocalSpaceMeshBounds.ContainsNaN() || !LOD.DistanceFieldData->LocalSpaceMeshBounds.IsValid)) bDFBad = true;
    UE_LOG(LogCireVideo, Warning, TEXT("CIRE_RENDER_SANITY_REPAIRED mesh=%s render_bounds=NaN -> origin=%s extent=%s df_bounds_bad=%d"), *Mesh->GetPathName(), *Good.Origin.ToString(), *Good.BoxExtent.ToString(), bDFBad ? 1 : 0);
    // Proxies being created on the render thread read these: repair between frames.
    FlushRenderingCommands();
    RD->Bounds = Good;
    if (bDFBad)
        for (FStaticMeshLODResources& LOD : RD->LODResources)
            if (LOD.DistanceFieldData) LOD.DistanceFieldData->LocalSpaceMeshBounds = FBox3f(Good.GetBox());
    ++GRepairedMeshes;
    for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
        if (It->GetStaticMesh() == Mesh && It->IsRegistered())
        {
            if (bDFBad) It->bAffectDistanceFieldLighting = false;
            It->UpdateBounds(); It->MarkRenderStateDirty();
        }
    return true;
}
int32 RepairLoadedMeshes()
{
    int32 Repaired = 0;
    TArray<UObject*> Meshes; GetObjectsOfClass(UStaticMesh::StaticClass(), Meshes, true);
    for (UObject* Object : Meshes)
    {
        UStaticMesh* Mesh = static_cast<UStaticMesh*>(Object);
#if WITH_EDITOR
        if (Mesh && Mesh->IsCompiling()) continue; // async build in flight: check next time
#endif
        if (!Mesh || !Mesh->GetRenderData() || !Mesh->GetRenderData()->IsInitialized()) continue; // not built yet: check next time
        bool bAlready = false; SeenMeshes().Add(FObjectKey(Mesh), &bAlready);
        if (!bAlready && RepairMesh(Mesh)) ++Repaired;
    }
    return Repaired;
}
int32 RepairedMeshes() { return GRepairedMeshes; }
}
namespace
{
// Game processes only: repair on the first tick after engine init, then as meshes stream in.
struct FMeshWatch
{
    FMeshWatch()
    {
        FCoreDelegates::GetOnPostEngineInit().AddLambda([]
        {
            if (IsRunningCommandlet() || IsRunningDedicatedServer()) return;
            FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float) { CireRenderSanity::RepairLoadedMeshes(); return true; }), .25f);
            CireRenderSanity::RepairLoadedMeshes();
        });
    }
} GMeshWatch;
}
