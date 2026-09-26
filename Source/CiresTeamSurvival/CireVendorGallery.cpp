// vendors: review gallery for the merchants (see CireVendorGallery.h).
#include "CireVendorGallery.h"

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireHUD.h"
#include "CireLanePath.h"
#include "CireShopUI.h"
#include "CireVendors.h"
#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireVendorGallery, Log, All);

namespace
{
enum class EKind : uint8 { Mesh, Stall, Wide, Plate, Shop };
enum class EView : uint8 { Front, Right, Back, Left, HandsFront, HandsRight, HandsLeft, Face, BackHead };
struct FStage
{
    EKind Kind = EKind::Mesh;
    FString Mesh;        // asset path (Mesh stages)
    FString Tag;         // file tag
    FName Clip;          // "", idle, greet, agree
    float ClipFraction = .5f;
    EView View = EView::Front;
    int32 Vendor = -1;   // Stall / Plate / Shop
    float Settle = 1.2f;
};
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<AActor>> Scene;
    TArray<FStage> Stages;
    TArray<FString> Meshes, Captures;
    FString Directory;
    FVector Hold = FVector::ZeroVector;
    float MeshYaw = -90.f;
    double Started = 0, StageStarted = 0;
    int32 Stage = -1;
    bool bCaptured = false, bDone = false, bPass = true, bBuilt = false, bMeshesOnly = false;
};
FGallery G;

UWorld* World() { return G.Mode.IsValid() ? G.Mode->GetWorld() : nullptr; }
void Fail(const FString& Why) { G.bPass = false; UE_LOG(LogCireVendorGallery, Error, TEXT("CIRE_VENDOR_GALLERY_CHECK_FAIL %s"), *Why); }
void Finish()
{
    if (G.bDone) return;
    G.bDone = true;
    for (const FString& File : G.Captures) if (IFileManager::Get().FileSize(*File) < 1024) Fail(TEXT("capture not written: ") + File);
    UE_LOG(LogCireVendorGallery, Display, TEXT("CIRE_VENDOR_GALLERY_%s captures=%d directory=%s"), G.bPass ? TEXT("PASS") : TEXT("FAIL"), G.Captures.Num(), *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, G.bPass ? 0 : 1);
}
float FloorZ(const FVector& P)
{
    FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(CireVendorGalleryFloor), false);
    for (const auto& Actor : G.Scene) if (Actor.IsValid()) Query.AddIgnoredActor(Actor.Get());
    return World()->LineTraceSingleByObjectType(Hit, P + FVector(0, 0, 2000), P - FVector(0, 0, 4000), FCollisionObjectQueryParams(ECC_WorldStatic), Query) ? Hit.ImpactPoint.Z : P.Z;
}
void Look(const FVector& Eye, const FVector& Target, float Fov)
{
    if (!G.Camera.IsValid()) return;
    G.Camera->SetActorLocation(Eye); G.Camera->SetActorRotation((Target - Eye).Rotation());
    G.Camera->GetCameraComponent()->SetFieldOfView(Fov);
}
void ClearScene()
{
    for (auto& Actor : G.Scene) if (Actor.IsValid()) Actor->Destroy();
    G.Scene.Reset();
}
ACireController* Controller() { return World() ? Cast<ACireController>(World()->GetFirstPlayerController()) : nullptr; }
ACireHero* Player() { ACireController* C = Controller(); return C ? Cast<ACireHero>(C->GetPawn()) : nullptr; }
void ShowHUD(bool bShow) { if (ACireController* C = Controller()) if (C->GetHUD()) C->GetHUD()->bShowHUD = bShow; }

// Clip for a body: the vendor's Vendors.json anims, else <dir>/Animations/<name>_<clip>.
UAnimSequence* FindClip(const FString& MeshPath, FName Clip)
{
    for (const FCireVendorDef& V : CireVendors::Get().Vendors)
        if (V.Mesh == MeshPath) { const FString* P = V.Anims.Find(Clip == TEXT("greet") ? FName(TEXT("greet")) : Clip); if (P) return LoadObject<UAnimSequence>(nullptr, **P, nullptr, LOAD_Quiet | LOAD_NoWarn); }
    const FString Name = FPaths::GetBaseFilename(MeshPath.Left(MeshPath.Find(TEXT("."))));
    const FString Suffix = Clip == TEXT("greet") ? TEXT("greet_01") : Clip.ToString();
    const FString Path = FPaths::GetPath(MeshPath) + TEXT("/Animations/") + Name + TEXT("_") + Suffix + TEXT(".") + Name + TEXT("_") + Suffix;
    return LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_Quiet | LOAD_NoWarn);
}

const TCHAR* ViewName(EView V)
{
    switch (V)
    {
    case EView::Front: return TEXT("front");
    case EView::Right: return TEXT("right");
    case EView::Back: return TEXT("back");
    case EView::Left: return TEXT("left");
    case EView::HandsFront: return TEXT("hands_front");
    case EView::HandsRight: return TEXT("hand_right_side");
    case EView::HandsLeft: return TEXT("hand_left_side");
    case EView::Face: return TEXT("face");
    default: return TEXT("back_of_head");
    }
}

void EnterMesh(const FStage& S)
{
    ShowHUD(false);
    // Turnarounds float high above the town: a clean sky backdrop, lit by the same sun and sky light.
    const FVector At(G.Hold.X, G.Hold.Y, 4000.f);
    const float Height = 185.f;
    USkeletalMeshComponent* Skel = nullptr;
    FBox Bounds(ForceInit);
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *S.Mesh, nullptr, LOAD_Quiet | LOAD_NoWarn))
    {
        auto* A = World()->SpawnActor<ASkeletalMeshActor>(At, FRotator::ZeroRotator, Params);
        G.Scene.Add(A);
        Skel = A->GetSkeletalMeshComponent();
        Skel->SetSkeletalMesh(Mesh);
        Skel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        const FBoxSphereBounds B = Mesh->GetImportedBounds();
        const float Scale = Height / FMath::Max(1.f, static_cast<float>(B.BoxExtent.Z * 2));
        Skel->SetWorldScale3D(FVector(Scale));
        Skel->SetWorldRotation(FRotator(0, G.MeshYaw, 0));
        Skel->SetWorldLocation(At + FVector(0, 0, -(B.Origin.Z - B.BoxExtent.Z) * Scale));
        if (!S.Clip.IsNone())
        {
            if (UAnimSequence* Clip = FindClip(S.Mesh, S.Clip))
            {
                Skel->SetAnimationMode(EAnimationMode::AnimationSingleNode);
                Skel->PlayAnimation(Clip, false);
                Skel->SetPosition(Clip->GetPlayLength() * S.ClipFraction, false);
                Skel->Stop();
            }
            else UE_LOG(LogCireVendorGallery, Warning, TEXT("CIRE_VENDOR_GALLERY_NOCLIP %s %s"), *S.Mesh, *S.Clip.ToString());
        }
        Skel->TickAnimation(0.f, false); Skel->RefreshBoneTransforms();
    }
    else if (UStaticMesh* Static = LoadObject<UStaticMesh>(nullptr, *S.Mesh, nullptr, LOAD_Quiet | LOAD_NoWarn))
    {
        auto* A = World()->SpawnActor<AStaticMeshActor>(At, FRotator::ZeroRotator, Params);
        G.Scene.Add(A);
        A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
        A->GetStaticMeshComponent()->SetStaticMesh(Static);
        const FBox B = Static->GetBoundingBox();
        const float Scale = Height / FMath::Max(1.f, static_cast<float>(B.GetSize().Z));
        A->SetActorScale3D(FVector(Scale));
        A->SetActorRotation(FRotator(0, G.MeshYaw, 0));
        A->SetActorLocation(At + FVector(0, 0, -B.Min.Z * Scale));
    }
    else { Fail(TEXT("load ") + S.Mesh); return; }
    // The body faces +X after MeshYaw: front camera on +X, the character's right on +Y.
    const FVector Mid = At + FVector(0, 0, Height * .52f);
    const float Dist = Height * 2.35f + 60.f;
    auto Bone = [&](const TCHAR* Name)
    {
        if (Skel && Skel->GetBoneIndex(Name) != INDEX_NONE) return Skel->GetBoneLocation(Name);
        return FCString::Strcmp(Name, TEXT("head")) == 0 ? At + FVector(0, 0, Height * .88f) : Mid; // static check meshes: no bones
    };
    switch (S.View)
    {
    case EView::Front: Look(Mid + FVector(Dist, 0, 20), Mid, 40.f); break;
    case EView::Right: Look(Mid + FVector(0, Dist, 20), Mid, 40.f); break;
    case EView::Back: Look(Mid + FVector(-Dist, 0, 20), Mid, 40.f); break;
    case EView::Left: Look(Mid + FVector(0, -Dist, 20), Mid, 40.f); break;
    case EView::Face: { const FVector H = Bone(TEXT("head")) + FVector(0, 0, 6); Look(H + FVector(95, 0, 0), H, 35.f); break; }
    case EView::BackHead: { const FVector H = Bone(TEXT("head")) + FVector(0, 0, 6); Look(H + FVector(-95, 0, 0), H, 35.f); break; }
    case EView::HandsFront:
    {
        const FVector L = Bone(TEXT("hand_l")), R = Bone(TEXT("hand_r")), C = (L + R) * .5f;
        const float Sep = FVector::Dist(L, R);
        Look(C + FVector(Sep * 1.1f + 95.f, 0, 12), C, 42.f);
        break;
    }
    case EView::HandsRight: { const FVector R = Bone(TEXT("hand_r")); Look(R + FVector(45, 125, 10), R, 40.f); break; }
    case EView::HandsLeft: { const FVector L = Bone(TEXT("hand_l")); Look(L + FVector(45, -125, 10), L, 40.f); break; }
    }
    UE_LOG(LogCireVendorGallery, Display, TEXT("CIRE_VENDOR_GALLERY_MESH %s clip=%s view=%s skeletal=%d hands=%d"), *S.Mesh, *S.Clip.ToString(), ViewName(S.View),
        Skel ? 1 : 0, Skel && Skel->GetBoneIndex(TEXT("hand_l")) != INDEX_NONE ? 1 : 0);
}

TArray<ACireVendor*> TeamVendors(int32 Team)
{
    TArray<ACireVendor*> Out;
    for (TActorIterator<ACireVendor> It(World()); It; ++It) if (It->Team == Team) Out.Add(*It);
    Out.Sort([](const ACireVendor& A, const ACireVendor& B) { return CireVendors::IndexOf(A.VendorId) < CireVendors::IndexOf(B.VendorId); });
    return Out;
}

void EnterStage(const FStage& S)
{
    ClearScene();
    if (ACireController* C = Controller()) C->bShop = false;
    if (S.Kind == EKind::Mesh) { EnterMesh(S); return; }
    const TArray<ACireVendor*> Vendors = TeamVendors(0);
    ACireVendor* V = Vendors.IsValidIndex(S.Vendor) ? Vendors[S.Vendor] : nullptr;
    ACireHero* Hero = Player();
    if (Hero) { Hero->SetActorHiddenInGame(true); Hero->SetActorLocation(FVector(0, 0, -50000)); }
    switch (S.Kind)
    {
    case EKind::Stall:
    {
        ShowHUD(false);
        if (!V) { Fail(TEXT("stall vendor missing")); return; }
        const FVector F = V->GetActorForwardVector(), R = V->GetActorRightVector(), At = V->GetActorLocation();
        Look(At + F * 620.f + R * 260.f + FVector(0, 0, 230), At + F * 60.f + FVector(0, 0, 150), 50.f);
        break;
    }
    case EKind::Wide:
    {
        ShowHUD(false);
        FVector Sum = FVector::ZeroVector; for (ACireVendor* X : Vendors) Sum += X->GetActorLocation();
        const FVector Mid = Vendors.Num() ? Sum / Vendors.Num() : G.Hold;
        const FVector Base = G.Mode->BasePosition(0);
        const FVector Dir = (Base - Mid).GetSafeNormal2D().IsNearlyZero() ? FVector(1, 0, 0) : (Base - Mid).GetSafeNormal2D();
        Look(Mid + Dir * 1500.f + FVector(0, 0, 620), Mid + FVector(0, 0, 120), 60.f);
        break;
    }
    case EKind::Plate:
    case EKind::Shop:
    {
        ShowHUD(true);
        if (!V || !Hero) { Fail(TEXT("plate vendor or player missing")); return; }
        Hero->SetActorHiddenInGame(false);
        const FVector Stand = V->InteractPoint();
        Hero->SetActorLocation(FVector(Stand.X, Stand.Y, FloorZ(Stand) + 100.f), false, nullptr, ETeleportType::TeleportPhysics);
        Hero->SetActorRotation(FRotator(0, (V->GetActorLocation() - Stand).Rotation().Yaw, 0));
        const FVector F = V->GetActorForwardVector();
        Look(Stand + F * 380.f + FVector(0, 0, 260), V->GetActorLocation() + FVector(0, 0, 160), 55.f);
        if (S.Kind == EKind::Shop)
        {
            Hero->Gold = 3200;
            CireShopUI::DebugItemTab();
            CireShopUI::DebugVendor(S.Tag == TEXT("shop_all") ? NAME_None : V->VendorId);
            if (ACireController* C = Controller()) C->bShop = true;
        }
        break;
    }
    default: break;
    }
}

void Capture(const FStage& S)
{
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%03d_%s.png"), G.Stage + 1, *S.Tag));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    G.Captures.Add(File);
    UE_LOG(LogCireVendorGallery, Display, TEXT("CIRE_VENDOR_GALLERY_CAPTURE %s"), *File);
}

bool Build(ACireGameMode& Mode, ACireController& Controller)
{
    if (auto* P = Cast<ACireHero>(Controller.GetPawn()))
    { P->TeamId = 0; P->Draft(0); P->Offers.Reset(); P->SetActorHiddenInGame(true); P->SetActorEnableCollision(false); P->SetActorLocation(FVector(0, 0, -50000)); P->Health = P->MaxHealth = 1.e6f; }
    Controller.SetIgnoreMoveInput(true); Controller.SetIgnoreLookInput(true); Controller.bShowMouseCursor = false;
    for (auto* M : Mode.Monsters) if (IsValid(M)) M->Destroy();
    Mode.Monsters.Reset();
    UWorld* W = Mode.GetWorld();
    // Turnarounds stand in the town square, clear of the stalls.
    G.Hold = FVector(1650.f, CireLanePath::CenterY(0), 0.f);
    G.Camera = W->SpawnActor<ACameraActor>();
    if (!G.Camera.IsValid()) return false;
    auto* Camera = G.Camera->GetCameraComponent();
    Camera->SetAspectRatio(16.f / 9.f); Camera->bConstrainAspectRatio = true;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true; Camera->PostProcessSettings.MotionBlurAmount = 0;
    Controller.SetViewTarget(G.Camera.Get());
    if (G.Meshes.IsEmpty()) for (const FCireVendorDef& V : CireVendors::Get().Vendors) G.Meshes.Add(V.Mesh);
    for (const FString& Mesh : G.Meshes)
    {
        const FString Short = FPaths::GetBaseFilename(Mesh.Left(Mesh.Find(TEXT(".")) >= 0 ? Mesh.Find(TEXT(".")) : Mesh.Len()));
        const bool bSkeletal = LoadObject<USkeletalMesh>(nullptr, *Mesh, nullptr, LOAD_Quiet | LOAD_NoWarn) != nullptr;
        if (!bSkeletal && !LoadObject<UStaticMesh>(nullptr, *Mesh, nullptr, LOAD_Quiet | LOAD_NoWarn)) { UE_LOG(LogCireVendorGallery, Warning, TEXT("CIRE_VENDOR_GALLERY_NOMESH %s"), *Mesh); continue; }
        for (EView View : {EView::Front, EView::Right, EView::Back, EView::Left, EView::Face, EView::BackHead})
            G.Stages.Add({EKind::Mesh, Mesh, FString::Printf(TEXT("%s_ref_%s"), *Short, ViewName(View)), NAME_None, 0.f, View, -1, 1.2f});
        if (!bSkeletal) continue;
        for (const TCHAR* Clip : {TEXT("idle"), TEXT("greet")})
            for (EView View : {EView::Front, EView::HandsFront, EView::HandsRight, EView::HandsLeft})
                G.Stages.Add({EKind::Mesh, Mesh, FString::Printf(TEXT("%s_%s_%s"), *Short, Clip, ViewName(View)), FName(Clip), FCString::Strcmp(Clip, TEXT("greet")) == 0 ? .45f : .5f, View, -1, 1.2f});
    }
    if (!G.bMeshesOnly)
    {
        const int32 N = CireVendors::Get().Vendors.Num();
        for (int32 I = 0; I < N; ++I) G.Stages.Add({EKind::Stall, FString(), FString::Printf(TEXT("stall_%s"), *CireVendors::Get().Vendors[I].Id.ToString()), NAME_None, 0, EView::Front, I, 1.5f});
        G.Stages.Add({EKind::Wide, FString(), TEXT("stalls_wide"), NAME_None, 0, EView::Front, 0, 1.5f});
        G.Stages.Add({EKind::Plate, FString(), TEXT("nameplate_prompt"), NAME_None, 0, EView::Front, 0, 1.5f});
        G.Stages.Add({EKind::Shop, FString(), TEXT("shop_all"), NAME_None, 0, EView::Front, 0, 1.6f});
        for (int32 I = 0; I < N; ++I) G.Stages.Add({EKind::Shop, FString(), FString::Printf(TEXT("shop_%s"), *CireVendors::Get().Vendors[I].Id.ToString()), NAME_None, 0, EView::Front, I, 1.6f});
    }
    G.bBuilt = true;
    UE_LOG(LogCireVendorGallery, Display, TEXT("CIRE_VENDOR_GALLERY_READY stages=%d"), G.Stages.Num());
    return true;
}
}

bool CireVendorGallery::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireVendorGallery"))) return false;
    G.Mode = Mode; G.Started = FPlatformTime::Seconds();
    FString Meshes;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireVendorGalleryMeshes="), Meshes, false)) { Meshes.ParseIntoArray(G.Meshes, TEXT(","), true); G.bMeshesOnly = true; }
    FParse::Value(FCommandLine::Get(), TEXT("CireVendorGalleryYaw="), G.MeshYaw);
    FString Out;
    G.Directory = FParse::Value(FCommandLine::Get(), TEXT("CireVendorGalleryOut="), Out, false) ? Out :
        FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Vendors"), TEXT("Gallery"), FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if (!Mode || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*G.Directory, true)) { Fail(TEXT("standalone match and capture directory")); Finish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    return true;
}

bool CireVendorGallery::Tick(ACireGameMode* Mode)
{
    if (G.Mode.Get() != Mode) return false;
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Started > 1200) { Fail(TEXT("gallery exceeded 1200 seconds")); Finish(); return true; }
    if (!G.bBuilt)
    {
        auto* C = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (C && C->GetPawn() && C->GetHUD() && !Build(*Mode, *C)) { Fail(TEXT("build")); Finish(); }
        return true;
    }
    if (G.Stage < 0 && GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - G.Started < 300) return true;
    if (G.Stage < 0 || (G.bCaptured && Now - G.StageStarted > G.Stages[G.Stage].Settle + .6))
    {
        if (G.Stage + 1 >= G.Stages.Num()) { ClearScene(); Finish(); return true; }
        ++G.Stage; G.bCaptured = false; G.StageStarted = Now;
        EnterStage(G.Stages[G.Stage]);
        return true;
    }
    if (!G.bCaptured && Now - G.StageStarted >= G.Stages[G.Stage].Settle &&
        (!GShaderCompilingManager || GShaderCompilingManager->GetNumRemainingJobs() == 0 || Now - G.StageStarted > 30))
    { Capture(G.Stages[G.Stage]); G.bCaptured = true; }
    return true;
}
#endif
