#include "CireCombatArtPreview.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireChampionArt.h"
#include "CireAreaEffects.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireCombatArtPreview, Log, All);
namespace
{
struct FPreview
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<APointLight> FrontFill;
    TArray<TWeakObjectPtr<ACireHero>> Heroes;
    TArray<TWeakObjectPtr<ACireMonster>> Targets;
    TArray<TWeakObjectPtr<ACireAreaEffect>> GroundAreas;
    TArray<TWeakObjectPtr<ATextRenderActor>> GroundLabels;
    TArray<FString> Captures;
    FString Directory;
    double Started = 0, Ready = -1;
    int32 Stage = -1;
    bool bDone = false;
    bool bGroundCaptured = false;
    bool bGroundChecksPassed = false;
};
FPreview Preview;
void Finish(bool bPass)
{
    if (Preview.bDone) return;
    Preview.bDone = true;
    UE_LOG(LogCireCombatArtPreview, Display, TEXT("CIRE_COMBAT_ART_PREVIEW_%s heroes=%d captures=%d directory=%s"),
        bPass ? TEXT("PASS") : TEXT("FAIL"), Preview.Heroes.Num(), Preview.Captures.Num(), *Preview.Directory);
    FPlatformMisc::RequestExitWithStatus(false, bPass ? 0 : 1);
}
bool Build(ACireGameMode& Mode, ACireController& Controller)
{
    UWorld* World = Mode.GetWorld();
    if (auto* Player = Cast<ACireHero>(Controller.GetPawn()))
    {
        Player->TeamId = 0; Player->bDrafted = true;
        Player->SetActorHiddenInGame(true); Player->SetActorEnableCollision(false); Player->SetActorTickEnabled(false);
    }
    Controller.SetIgnoreMoveInput(true); Controller.SetIgnoreLookInput(true); Controller.bShowMouseCursor = false;
    if (Controller.GetHUD()) Controller.GetHUD()->bShowHUD = false;
    for (TActorIterator<APointLight> It(World); It; ++It) It->PointLightComponent->SetIntensity(0);
    for (TActorIterator<ADirectionalLight> It(World); It; ++It) It->GetLightComponent()->SetIntensity(0);
    for (TActorIterator<ASkyLight> It(World); It; ++It)
    { It->GetLightComponent()->SetLightColor(FLinearColor::White); It->GetLightComponent()->SetIntensity(.85f); }
    auto* Light = World->SpawnActor<ADirectionalLight>(FVector(0,-2100,1600), FRotator(-40,145,0));
    Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Light->GetLightComponent()->SetIntensity(5); Light->GetLightComponent()->SetLightColor(FLinearColor::White);
    Preview.FrontFill = World->SpawnActor<APointLight>();
    if (!Preview.FrontFill.IsValid()) return false;
    UPointLightComponent* Fill = Preview.FrontFill->PointLightComponent;
    Fill->SetMobility(EComponentMobility::Movable);
    Fill->SetLightColor(FLinearColor::White);
    Fill->SetIntensityUnits(ELightUnits::Lumens);
    Fill->SetIntensity(12000);
    Fill->SetAttenuationRadius(2000);
    Fill->SetCastShadows(false);
    const TCHAR* Names[] = {TEXT("WARDEN: SWORD + SHIELD"), TEXT("RANGER: BOW"), TEXT("SCHOLAR: CAST"), TEXT("LANCER: THROW / TEMP BODY")};
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FVector Position(-900, -2520 + Index * 280, 120);
        auto* Hero = World->SpawnActor<ACireHero>(Position, FRotator::ZeroRotator, Params);
        if (!Hero) return false;
        FHitResult Floor;
        FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(CireCombatArtFloor),false,Hero);
        if (World->LineTraceSingleByChannel(Floor, Position+FVector(0,0,900), Position-FVector(0,0,900), ECC_Visibility, FloorQuery))
            Hero->SetActorLocation(FVector(Position.X,Position.Y,Floor.ImpactPoint.Z+Hero->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()));
        Hero->Archetype = Index; Hero->TeamId = 0; Hero->bDrafted = true; Hero->bBot = false; Hero->bAutoAttack = false;
        Hero->GetCharacterMovement()->StopMovementImmediately(); Hero->GetCharacterMovement()->DisableMovement();
        Hero->SetActorTickEnabled(false); Hero->SetActorEnableCollision(false);
        Hero->ChampionArt->UpdateVisuals(*Hero, 0);
        if (!Hero->ChampionArt->IsApplied()) return false;
        Hero->GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Hero->GetMesh()->SetVisibility(true, true);
        Hero->AttackSerial = 1; Hero->AttackDuration = .65f;
        auto* Target = World->SpawnActor<ACireMonster>(Position + FVector(Index == 0 ? 160 : 420,0,0),FRotator(0,180,0),Params);
        if (!Target) return false;
        Target->Lane = 0; Target->Health = Target->MaxHealth = 10000; Target->SetActorTickEnabled(false);
        Target->GetCharacterMovement()->DisableMovement(); Target->SetActorEnableCollision(false);
        Target->GetMesh()->SetVisibility(false,true); // Keep silhouettes unobstructed; target positions still drive aim.
        Hero->Target = Target; Hero->AttackAimLocation = Target->GetActorLocation();
        Preview.Heroes.Add(Hero); Preview.Targets.Add(Target);
        auto* Label = World->SpawnActor<ATextRenderActor>(FVector(-860,Position.Y,15),FRotator::ZeroRotator);
        if (Label)
        {
            Label->GetTextRender()->SetText(FText::FromString(Names[Index]));
            Label->GetTextRender()->SetWorldSize(13);
            Label->GetTextRender()->SetTextRenderColor(FColor::White);
            Label->GetTextRender()->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
        }
    }
    Preview.Camera = World->SpawnActor<ACameraActor>();
    if (!Preview.Camera.IsValid()) return false;
    auto* Camera = Preview.Camera->GetCameraComponent(); Camera->SetFieldOfView(48); Camera->SetAspectRatio(16.f/9.f); Camera->bConstrainAspectRatio = true;
    auto& Post = Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod = true; Post.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure = true; Post.AutoExposureApplyPhysicalCameraExposure = false;
    Post.bOverride_AutoExposureBias = true; Post.AutoExposureBias = .5;
    Post.bOverride_MotionBlurAmount = true; Post.MotionBlurAmount = 0;
    Post.bOverride_BloomIntensity = true; Post.BloomIntensity = 0;
    Controller.SetViewTarget(Preview.Camera.Get()); Preview.Controller = &Controller;
    Preview.Ready = FPlatformTime::Seconds();
    UE_LOG(LogCireCombatArtPreview, Display, TEXT("CIRE_COMBAT_ART_PREVIEW_READY original_prototype_art=1 lancer_shared_body=1 release=.25 duration=.65"));
    return true;
}

bool BuildGroundFigures(ACireGameMode& Mode)
{
    if (Preview.Heroes.IsEmpty() || !Preview.Heroes[0].IsValid()) return false;
    UWorld* World = Mode.GetWorld();
    const TCHAR* Names[] = {TEXT("CIRCLE"), TEXT("CONE"), TEXT("LINE"), TEXT("SQUARE"), TEXT("CUSTOM / CONCAVE")};
    const FLinearColor Colors[] = {
        FLinearColor(.25f,1.f,.18f,.45f), FLinearColor(1.f,.5f,.08f,.45f),
        FLinearColor(.08f,.65f,1.f,.45f), FLinearColor(.75f,.25f,1.f,.45f), FLinearColor(1.f,.2f,.45f,.45f)};
    for (int32 Index = 0; Index < 5; ++Index)
    {
        FCireAreaSpec Spec;
        Spec.Shape = static_cast<ECireAreaShape>(Index);
        Spec.Radius = Index == 1 ? 185.f : 90.f;
        Spec.Length = 190.f; Spec.Width = Index == 3 ? 160.f : 105.f;
        Spec.ConeAngleDegrees = 65.f;
        Spec.WarningSeconds = 0; Spec.DurationSeconds = 8;
        Spec.DamagePerSecond = 0; Spec.BurstDamage = 0;
        Spec.bPoison = false; Spec.bPersistent = true;
        Spec.Color = Colors[Index]; Spec.AbilityName = Names[Index];
        if (Index == 4)
            Spec.CustomPolygon = {FVector2D(-80,-80),FVector2D(80,-80),FVector2D(80,-20),
                FVector2D(-15,-20),FVector2D(-15,80),FVector2D(-80,80)};
        FVector Position(-350, -2540 + Index * 220, 0);
        if (Index == 1 || Index == 2) Position.X -= 95;
        FHitResult Floor;
        if (!World->LineTraceSingleByChannel(Floor,Position+FVector(0,0,800),Position-FVector(0,0,800),ECC_Visibility)) return false;
        Position.Z = Floor.ImpactPoint.Z;
        auto* Area = ACireAreaEffect::Spawn(Preview.Heroes[0].Get(),Spec,Position,FRotator::ZeroRotator);
        if (!Area) return false;
        Preview.GroundAreas.Add(Area);
        auto* Label = World->SpawnActor<ATextRenderActor>(FVector(-190,Position.Y,Position.Z+8),FRotator(90,0,0));
        if (!Label) return false;
        Label->GetTextRender()->SetText(FText::FromString(Names[Index]));
        Label->GetTextRender()->SetWorldSize(Index == 4 ? 14.f : 19.f);
        Label->GetTextRender()->SetTextRenderColor(FColor::White);
        Label->GetTextRender()->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
        Preview.GroundLabels.Add(Label);
    }
    UE_LOG(LogCireCombatArtPreview,Display,TEXT("CIRE_COMBAT_ART_GROUND_READY shapes=5 damage=0 poison=0 labels=5"));
    return true;
}

void CaptureGroundFigures()
{
    int32 Width = 0, Height = 0;
    Preview.Controller->GetViewportSize(Width,Height);
    bool bValid = Width > 0 && Height > 0 && Preview.GroundAreas.Num()==5 && Preview.GroundLabels.Num()==5;
    const auto InFrame = [&](const FVector& Position)
    {
        FVector2D Pixel;
        return Preview.Controller->ProjectWorldLocationToScreen(Position,Pixel) &&
            Pixel.X >= 12 && Pixel.X < Width-12 && Pixel.Y >= 12 && Pixel.Y < Height-12;
    };
    for (const auto& WeakArea : Preview.GroundAreas)
    {
        const auto* Area = WeakArea.Get();
        bool bShapeValid = IsValid(Area) && Area->IsActive() && !Area->IsHidden() &&
            Area->CanObserve(Preview.Controller.Get()) && Area->GroundMesh && Area->GroundMesh->IsVisible() &&
            Area->GroundMesh->GetNumSections() >= 2;
        if (Area)
        {
            for (const FVector2D Point : ACireAreaEffect::BoundaryPoints(Area->AreaSpec))
                bShapeValid &= InFrame(Area->GetActorTransform().TransformPosition(FVector(Point.X,Point.Y,5)));
            UE_LOG(LogCireCombatArtPreview,Display,TEXT("CIRE_COMBAT_ART_GROUND_CHECK shape=%s visible_and_in_frame=%d"),*Area->AreaSpec.AbilityName,bShapeValid);
        }
        bValid &= bShapeValid;
    }
    for (const auto& Label : Preview.GroundLabels) bValid &= Label.IsValid() && InFrame(Label->GetActorLocation());
    const FString File = FPaths::Combine(Preview.Directory,TEXT("09_ground_shapes_labeled.png"));
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);
    Preview.Captures.Add(File); Preview.bGroundCaptured = true; Preview.bGroundChecksPassed = bValid;
}
}

bool CireCombatArtPreview::Initialize(ACireGameMode* Mode)
{
    Preview = {};
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireCombatArtPreview"))) return false;
    Preview.Mode = Mode; Preview.Started = FPlatformTime::Seconds();
    Preview.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("CombatArtPreview"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if (!Mode || Mode->GetNetMode()!=NM_Standalone || !IFileManager::Get().MakeDirectory(*Preview.Directory,true)) { Finish(false); return true; }
    Mode->bBotsFilled = true; Mode->BotFillTimer = MAX_flt; Mode->WaveTimer = MAX_flt;
    return true;
}
bool CireCombatArtPreview::Tick(ACireGameMode* Mode)
{
    if (Preview.Mode.Get()!=Mode) return false;
    if (Preview.bDone) return true;
    if (FPlatformTime::Seconds()-Preview.Started>70) { Finish(false); return true; }
    if (Preview.Ready<0)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if (Controller && Controller->GetPawn() && Controller->GetHUD() && !Build(*Mode,*Controller)) Finish(false);
        return true;
    }
    const double Age = FPlatformTime::Seconds()-Preview.Ready;
    const int32 Stage = FMath::Clamp(FMath::FloorToInt((Age-5)/3),0,7);
    const float Phases[] = {.15f,.25f,.45f,.70f};
    const float Phase = Age < 5 ? .7f : Phases[Stage%4];
    for (auto& WeakHero : Preview.Heroes)
    {
        if (!WeakHero.IsValid()) { Finish(false); return true; }
        auto& Hero = *WeakHero.Get();
        Hero.AttackStartedServerTime = Hero.GetWorld()->GetTimeSeconds()-Phase;
        Hero.ChampionArt->UpdateVisuals(Hero,Mode->GetWorld()->GetDeltaSeconds());
    }
    const FVector Center(-900,-2100,130);
    const FVector View = Center + (Stage<4 ? FVector(1380,0,75) : FVector(1200,650,240));
    Preview.Camera->SetActorLocation(View); Preview.Camera->SetActorRotation((Center-View).Rotation());
    if (Preview.FrontFill.IsValid())
        Preview.FrontFill->SetActorLocation(Center + (Stage<4 ? FVector(470,0,280) : FVector(420,230,310)));
    if (Age>=7 && Stage>Preview.Stage && Age>=7+Stage*3)
    {
        const FString Name = FString::Printf(TEXT("%02d_%s_phase_%03d"),Stage+1,Stage<4?TEXT("front"):TEXT("angled"),FMath::RoundToInt(Phase*1000));
        const FString File = FPaths::Combine(Preview.Directory,Name+TEXT(".png"));
        FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true); Preview.Captures.Add(File); Preview.Stage=Stage;
        for (int32 Index=0;Index<Preview.Heroes.Num();++Index)
        {
            const auto* Mesh=Preview.Heroes[Index]->GetMesh();
            const auto* Animation=Cast<UCireCombatAnimInstance>(Mesh->GetAnimInstance());
            UE_LOG(LogCireCombatArtPreview,Display,TEXT("CIRE_COMBAT_ART_POSE stage=%d hero=%d phase=%.3f weight=%.3f left=%s right=%s head=%s"),Stage,Index,Phase,
                Animation?Animation->AttackWeight:-1.f,*Mesh->GetSocketLocation(TEXT("hand_l")).ToString(),*Mesh->GetSocketLocation(TEXT("hand_r")).ToString(),*Mesh->GetSocketLocation(TEXT("head")).ToString());
        }
    }
    if (Age>=32)
    {
        if (Preview.GroundAreas.IsEmpty() && !BuildGroundFigures(*Mode)) { Finish(false); return true; }
        const FVector GroundCenter(-340,-2100,0);
        const FVector GroundView = GroundCenter + FVector(450,0,1450);
        Preview.Camera->SetActorLocation(GroundView);
        Preview.Camera->SetActorRotation((GroundCenter-GroundView).Rotation());
        if (Age>=34 && !Preview.bGroundCaptured) CaptureGroundFigures();
    }
    if (Age>=37)
    {
        bool bPass=Preview.Captures.Num()==9 && Preview.bGroundCaptured && Preview.bGroundChecksPassed;
        for (const FString& File:Preview.Captures)bPass &= IFileManager::Get().FileSize(*File)>1024;
        Finish(bPass);
    }
    return true;
}
#endif
