#include "CireGripGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireChampionActions.h"
#include "CireChampionArt.h"
#include "CireChampionRoster.h"
#include "CireFabAnimation.h"
#include "CireMobility.h"
#include "CireWeaponPresentation.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUD.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ShaderCompiler.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireGripGallery, Log, All);

namespace
{
const TCHAR* States[] = {TEXT("idle"), TEXT("run"), TEXT("windup"), TEXT("contact"), TEXT("cast"), TEXT("roll")};
constexpr int32 NumStates = UE_ARRAY_COUNT(States);
const TCHAR* Views[] = {TEXT("wide"), TEXT("hand")};
constexpr int32 NumViews = UE_ARRAY_COUNT(Views);
const FVector Stage(3000, 6000, 9000); // clear of the map and of other galleries' stages

struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<APointLight> Fill;
    TWeakObjectPtr<ACireHero> Hero;
    TArray<FString> Profiles, Captures;
    FString Directory;
    int32 Champion = -1, Step = 0, Checks = 0;
    double Started = 0, StepStarted = 0, Requested = -1;
    float BodyHeight = 180.f;
    bool bBuilt = false, bDone = false, bPass = true;
} G;

void Fail(const FString& Why) { G.bPass = false; UE_LOG(LogCireGripGallery, Error, TEXT("CIRE_GRIP_GALLERY_CHECK_FAIL %s"), *Why); }
void Finish()
{
    if (G.bDone) return;
    G.bDone = true;
    if (G.Hero.IsValid()) G.Hero->Destroy();
    for (const FString& File : G.Captures) if (IFileManager::Get().FileSize(*File) < 1024) Fail(TEXT("capture not written: ") + File);
    UE_LOG(LogCireGripGallery, Display, TEXT("CIRE_GRIP_GALLERY_%s champions=%d captures=%d legacy=%d directory=%s"), G.bPass ? TEXT("PASS") : TEXT("FAIL"),
        G.Profiles.Num(), G.Captures.Num(), CireWeapons::LegacyGrips() ? 1 : 0, *G.Directory);
    FPlatformMisc::RequestExitWithStatus(false, G.bPass ? 0 : 1);
}

bool BuildStage(ACireGameMode* Mode, ACireController* Controller)
{
    auto* Player = Cast<ACireHero>(Controller->GetPawn()); if (!Player) return false;
    G.Controller = Controller;
    Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false); Player->SetActorTickEnabled(false);
    Player->GetCharacterMovement()->DisableMovement(); Controller->SetIgnoreMoveInput(true); Controller->SetIgnoreLookInput(true);
    if (Controller->GetHUD()) Controller->GetHUD()->bShowHUD = false;
    UWorld* World = Mode->GetWorld();
    for (auto* M : Mode->Monsters) if (IsValid(M)) M->Destroy();
    Mode->Monsters.Reset();
    for (TActorIterator<APointLight> It(World); It; ++It) It->PointLightComponent->SetIntensity(0);
    for (TActorIterator<ADirectionalLight> It(World); It; ++It) It->GetLightComponent()->SetIntensity(0);
    for (TActorIterator<ASkyLight> It(World); It; ++It) { It->GetLightComponent()->SetLightColor(FLinearColor::White); It->GetLightComponent()->SetIntensity(.9f); }
    auto* Key = World->SpawnActor<ADirectionalLight>(Stage + FVector(0, 0, 2000), FRotator(-38, 145, 0)); if (!Key) return false;
    Key->GetLightComponent()->SetMobility(EComponentMobility::Movable); Key->GetLightComponent()->SetIntensity(5.5f);
    G.Fill = World->SpawnActor<APointLight>(); if (!G.Fill.IsValid()) return false;
    auto* Fill = G.Fill->PointLightComponent.Get(); Fill->SetMobility(EComponentMobility::Movable); Fill->SetIntensityUnits(ELightUnits::Lumens);
    Fill->SetIntensity(9000); Fill->SetAttenuationRadius(2000); Fill->SetCastShadows(false);
    auto* Floor = World->SpawnActor<AStaticMeshActor>(Stage - FVector(0, 0, 50), FRotator::ZeroRotator); if (!Floor) return false;
    Floor->SetMobility(EComponentMobility::Movable); Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->GetStaticMeshComponent()->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_Slate.M_Slate")));
    Floor->SetActorScale3D(FVector(30, 30, 1)); Floor->SetActorEnableCollision(false);
    G.Camera = World->SpawnActor<ACameraActor>(); if (!G.Camera.IsValid()) return false;
    auto* Camera = G.Camera->GetCameraComponent(); Camera->SetFieldOfView(40); Camera->SetAspectRatio(16.f / 9.f); Camera->bConstrainAspectRatio = true;
    auto& Post = Camera->PostProcessSettings; Post.bOverride_AutoExposureMethod = true; Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true; Post.AutoExposureApplyPhysicalCameraExposure = false; Post.bOverride_AutoExposureBias = true; Post.AutoExposureBias = .5f;
    Post.bOverride_MotionBlurAmount = true; Post.MotionBlurAmount = 0; Post.bOverride_BloomIntensity = true; Post.BloomIntensity = 0;
    Controller->SetViewTarget(G.Camera.Get());
    G.bBuilt = true;
    return true;
}

UStaticMeshComponent* PrimaryProp(ACireHero& H)
{
    auto* Weapons = H.FindComponentByClass<UCireWeaponPresentation>();
    if (!Weapons) return nullptr;
    for (const auto& Part : Weapons->GetParts())
        if (Part && Part->GetStaticMesh() && Part->GetAttachSocketName().ToString().StartsWith(TEXT("hand_"))) return Part.Get();
    for (const auto& Part : Weapons->GetParts()) if (Part && Part->GetStaticMesh()) return Part.Get();
    return nullptr;
}

bool SpawnChampion(ACireGameMode* Mode)
{
    if (G.Hero.IsValid()) G.Hero->Destroy();
    const FString& Id = G.Profiles[G.Champion];
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* H = Mode->GetWorld()->SpawnActor<ACireHero>(Stage + FVector(0, 0, 100), FRotator::ZeroRotator, Params);
    if (!H) { Fail(TEXT("spawn ") + Id); return false; }
    G.Hero = H; H->TeamId = 0;
    if (!H->DraftProfile(Id)) { Fail(TEXT("draft ") + Id); return false; }
    CireMovement::ApplyToHero(*H); // tanks are 15% larger, as in play
    H->bBot = false; H->bAutoAttack = false; H->Target = nullptr; H->AttackSerial = 0; H->AttackDuration = .65f;
    H->SetActorEnableCollision(false); H->SetActorTickEnabled(false);
    H->GetCharacterMovement()->StopMovementImmediately(); H->GetCharacterMovement()->SetComponentTickEnabled(false);
    H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    H->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    H->ChampionArt->UpdateVisuals(*H, .1f);
    H->SetActorLocation(Stage + FVector(0, 0, H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()));
    H->PrestreamTextures(30.f, true);
    // Standing height from the skeleton: skeletal bounds of the Tripo bodies are loose (about twice the body).
    USkeletalMeshComponent* Mesh = H->GetMesh();
    Mesh->TickAnimation(0.f, false); Mesh->RefreshBoneTransforms();
    float Height = static_cast<float>(Mesh->CalcBounds(Mesh->GetComponentTransform()).BoxExtent.Z * 2);
    if (Mesh->GetBoneIndex(TEXT("head")) != INDEX_NONE && Mesh->GetBoneIndex(TEXT("foot_l")) != INDEX_NONE)
        Height = static_cast<float>((Mesh->GetSocketLocation(TEXT("head")).Z - Mesh->GetSocketLocation(TEXT("foot_l")).Z) * 1.1);
    G.BodyHeight = FMath::Clamp(Height, 60.f, 600.f);
    {   // every visible primitive of the hero (review aid: what else is drawn with the body)
        TArray<UPrimitiveComponent*> Prims; H->GetComponents(Prims);
        for (const UPrimitiveComponent* P : Prims)
            if (P && P->IsVisible() && !P->bHiddenInGame)
            {
                const auto* SM = Cast<UStaticMeshComponent>(P);
                UE_LOG(LogCireGripGallery, Display, TEXT("CIRE_GRIP_GALLERY_PRIM %s %s %s mesh=%s extent=%s attach=%s"), *Id, *P->GetClass()->GetName(), *P->GetName(),
                    SM && SM->GetStaticMesh() ? *SM->GetStaticMesh()->GetName() : TEXT("-"), *P->Bounds.BoxExtent.ToString(), *P->GetAttachSocketName().ToString());
            }
    }
    UE_LOG(LogCireGripGallery, Display, TEXT("CIRE_GRIP_GALLERY_CHAMPION %s body=%s height=%.1f applied=%d"), *Id,
        H->GetMesh()->GetSkeletalMeshAsset() ? *H->GetMesh()->GetSkeletalMeshAsset()->GetPathName() : TEXT("none"), G.BodyHeight, H->ChampionArt->IsApplied() ? 1 : 0);
    return true;
}

void Pose(ACireHero& H, int32 State)
{
    const FVector Forward = H.GetActorForwardVector();
    H.GetCharacterMovement()->Velocity = State == 1 ? Forward * 480.f : FVector::ZeroVector;
    H.AttackSerial = 0;
    if (H.Mobility) { H.Mobility->bWalking = false; H.Mobility->RollStartedAt = -100; }
    if (auto* Action = H.FindComponentByClass<UCireChampionAction>()) { Action->bHold = false; Action->Sequence = nullptr; }
    H.ChampionArt->UpdateVisuals(H, FApp::GetDeltaTime());
    auto* Combat = Cast<UCireCombatAnimInstance>(H.GetMesh()->GetAnimInstance());
    if (State <= 1)
    {
        if (auto* Single = H.GetMesh()->GetSingleNodeInstance()) { Single->SetPosition(State == 1 ? .3f : .5f, false); Single->SetPlaying(false); }
        return;
    }
    if (State >= 2 && State <= 4)
    {
        const FString Clip = State == 4 ? FString(TEXT("cast_a_spell")) : CireChampionActions::ClipName(H, TEXT("attack"));
        if (!CireChampionActions::Hold(H, Clip, State == 2 ? .55f : State == 3 ? 1.f : .95f)) return;
        H.ChampionArt->UpdateVisuals(H, 0.f);
        return;
    }
    // Dodge roll: the Fab roll clip held mid-tumble, else the procedural roll at half progress.
    USkeletalMesh* Body = H.GetMesh()->GetSkeletalMeshAsset();
    UAnimSequence* Clip = nullptr; FString Name; CireChampionActions::FWindow W;
    if (H.Mobility)
    {   // the hero is mid-roll either way (carried staffs let go during rolls)
        const AGameStateBase* S = H.GetWorld()->GetGameState();
        const float Now = S ? S->GetServerWorldTimeSeconds() : H.GetWorld()->GetTimeSeconds();
        H.Mobility->RollDuration = CireMovement::Tuning().RollDuration; H.Mobility->RollStartedAt = Now - H.Mobility->RollDuration * .5f; H.Mobility->RollDirection = Forward;
        H.ChampionArt->UpdateVisuals(H, 0.f);
    }
    if (Combat && CireFabAnimation::Pick(Body, CireFabAnimation::FolderFor(Body), CireChampionActions::StyleName(H), CireChampionActions::MotionFor(H), TEXT("roll"), 0, Clip, Name) && CireFabAnimation::Window(Name, W))
    { Combat->AttackSequence = Clip; Combat->AttackTime = FMath::Lerp(W.Start, W.End, .45f); Combat->AttackWeight = 1.f; Combat->AttackLowerBody = 1.f; Combat->RollProgress = -1.f; }
}

void Frame(ACireHero& H, int32 View)
{
    auto* Camera = G.Camera->GetCameraComponent();
    const FBoxSphereBounds B = H.GetMesh()->CalcBounds(H.GetMesh()->GetComponentTransform());
    const FVector Feet(H.GetActorLocation().X, H.GetActorLocation().Y, H.GetActorLocation().Z - H.GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    if (View == 0)
    {
        const FVector Center = Feet + FVector(0, 0, G.BodyHeight * .55f);
        const FVector Direction = FVector(1, .62f, .14f).GetSafeNormal();
        const float Distance = FMath::Max(G.BodyHeight, 190.f) * 1.25f / FMath::Tan(FMath::DegreesToRadians(20.f));
        Camera->SetFieldOfView(40); Camera->SetWorldLocation(Center + Direction * Distance); Camera->SetWorldRotation((-Direction).Rotation());
        G.Fill->SetActorLocation(Center + FVector(420, 260, 220));
        return;
    }
    UStaticMeshComponent* Prop = PrimaryProp(H);
    const FName Bone = Prop ? Prop->GetAttachSocketName() : FName(TEXT("hand_r"));
    FName Hand = Bone.ToString().EndsWith(TEXT("_l")) ? FName(TEXT("hand_l")) : FName(TEXT("hand_r"));
    const FVector At = H.GetMesh()->GetBoneIndex(Hand) != INDEX_NONE ? H.GetMesh()->GetSocketLocation(Hand) : B.Origin;
    const float Side = Hand == TEXT("hand_l") ? -1.f : 1.f;
    const FVector Direction = (H.GetActorForwardVector() * .8f + H.GetActorRightVector() * Side * .75f + FVector(0, 0, .35f)).GetSafeNormal();
    const float Distance = FMath::Clamp(G.BodyHeight / 180.f, .7f, 1.8f) * 150.f;
    Camera->SetFieldOfView(40); Camera->SetWorldLocation(At + Direction * Distance); Camera->SetWorldRotation((-Direction).Rotation());
    G.Fill->SetActorLocation(At + Direction * 160.f + FVector(0, 0, 120));
}

void Capture(ACireHero& H, int32 State, int32 View)
{
    const FString& Id = G.Profiles[G.Champion];
    const FString File = FPaths::Combine(G.Directory, FString::Printf(TEXT("%02d_%s_%d_%s_%s.png"), G.Champion + 1, *Id, State, States[State], Views[View]));
    FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
    G.Captures.Add(File);
    if (View != 0) return;
    auto* Combat = Cast<UCireCombatAnimInstance>(H.GetMesh()->GetAnimInstance());
    const FString Clip = Combat && Combat->AttackSequence && Combat->AttackWeight > .5f ? Combat->AttackSequence->GetName() : FString(TEXT("locomotion"));
    USkeletalMesh* Body = H.GetMesh()->GetSkeletalMeshAsset();
    UE_LOG(LogCireGripGallery, Display, TEXT("CIRE_GRIP_GALLERY_METRIC profile=%s state=%s folder=%s style=%s clip=%s height=%.1f %s"), *Id, States[State],
        *CireFabAnimation::FolderFor(Body), *CireChampionActions::StyleName(H), *Clip, G.BodyHeight, *CireWeapons::DescribeGrips(H));
}
}

bool CireGripGallery::Initialize(ACireGameMode* Mode)
{
    G = FGallery();
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireGripGallery"))) return false;
    G.Mode = Mode; G.Started = FPlatformTime::Seconds();
    G.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("GripGallery"),
        FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + (CireWeapons::LegacyGrips() ? TEXT("-before") : TEXT("-after"))));
    IFileManager::Get().MakeDirectory(*G.Directory, true);
    FString Only; TArray<FString> Filter;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireGripGalleryOnly="), Only, false)) Only.ParseIntoArray(Filter, TEXT(","), true);
    for (const auto& Profile : CireChampionRoster::All())
        if (Filter.IsEmpty() || Filter.Contains(Profile.Id)) G.Profiles.Add(Profile.Id);
    if (!Mode || Mode->GetNetMode() != NM_Standalone || G.Profiles.IsEmpty()) { Fail(TEXT("standalone game with profiles required")); Finish(); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    UE_LOG(LogCireGripGallery, Display, TEXT("CIRE_GRIP_GALLERY_READY champions=%d directory=%s"), G.Profiles.Num(), *G.Directory);
    return true;
}

bool CireGripGallery::Tick(ACireGameMode* Mode)
{
    if (G.Mode.Get() != Mode || !Mode) return false;
    if (G.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - G.Started > 240 + G.Profiles.Num() * NumStates * NumViews * 1.5) { Fail(TEXT("gallery timed out")); Finish(); return true; }
    if (!G.bBuilt)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (Controller && Controller->GetPawn() && !BuildStage(Mode, Controller)) { Fail(TEXT("stage")); Finish(); }
        return true;
    }
    if (G.Champion < 0 || G.Step >= NumStates * NumViews)
    {
        // Next armed champion: bodies without held props (natural attacks, creature bodies) are listed and skipped.
        for (;;)
        {
            ++G.Champion; G.Step = 0;
            if (G.Champion >= G.Profiles.Num()) { Finish(); return true; }
            if (!SpawnChampion(Mode)) continue;
            if (PrimaryProp(*G.Hero.Get())) break;
            UE_LOG(LogCireGripGallery, Display, TEXT("CIRE_GRIP_GALLERY_SKIP %s holds no prop"), *G.Profiles[G.Champion]);
        }
        G.StepStarted = Now + 2.0; G.Requested = -1;
    }
    ACireHero* H = G.Hero.Get();
    if (!H) { Fail(TEXT("hero lost")); Finish(); return true; }
    const int32 State = G.Step / NumViews, View = G.Step % NumViews;
    Pose(*H, State);
    Frame(*H, View);
    if (GShaderCompilingManager && GShaderCompilingManager->GetNumRemainingJobs() > 0 && Now - G.Started < 400) { G.StepStarted = Now; return true; }
    if (G.Requested < 0 && Now - G.StepStarted >= .45) { Capture(*H, State, View); G.Requested = Now; }
    else if (G.Requested >= 0 && Now - G.Requested >= .3) { ++G.Step; G.StepStarted = Now; G.Requested = -1; }
    return true;
}
#endif
