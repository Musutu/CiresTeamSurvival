#include "CireArtPreview.h"

#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/BlendSpace.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireArtPreview, Log, All);

namespace
{
struct FPreviewLocomotion
{
    TWeakObjectPtr<UBlendSpace> Asset;
    FVector Inputs[3] = {};
};

struct FArtPreviewState
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<ADirectionalLight> Key;
    TWeakObjectPtr<APointLight> Fill;
    TArray<TWeakObjectPtr<ASkeletalMeshActor>> Models;
    TArray<FPreviewLocomotion> Locomotion;
    TArray<TWeakObjectPtr<ATextRenderActor>> Labels;
    TArray<FString> Screenshots;
    TArray<double> FloorHeights;
    FVector Center = FVector::ZeroVector;
    float CameraDistance = 800;
    FString Directory;
    double Started = 0, ReadyAt = -1;
    int32 Stage = 0;
    int32 LocomotionState = INDEX_NONE;
    bool bDone = false;
    bool bPoseChecksPassed = true;
};
FArtPreviewState Preview;

// Keep the same reference-bone calculation as CireChampionArt's runtime transform.
bool GetFacingYaw(const USkeletalMesh& Mesh, float& OutYaw)
{
    const auto& Skeleton = Mesh.GetRefSkeleton();
    const auto Position = [&Skeleton](const TCHAR* Name, FVector& OutPosition)
    {
        int32 Index = Skeleton.FindBoneIndex(FName(Name));
        if (Index == INDEX_NONE) return false;
        FTransform Transform = Skeleton.GetRefBonePose()[Index];
        while ((Index = Skeleton.GetParentIndex(Index)) != INDEX_NONE)
            Transform = Transform * Skeleton.GetRefBonePose()[Index];
        OutPosition = Transform.GetLocation();
        return !OutPosition.ContainsNaN();
    };
    FVector LeftFoot, LeftToe, RightFoot, RightToe;
    if (!Position(TEXT("foot_l"), LeftFoot) || !Position(TEXT("ball_l"), LeftToe) ||
        !Position(TEXT("foot_r"), RightFoot) || !Position(TEXT("ball_r"), RightToe)) return false;
    const FVector Forward = ((LeftToe - LeftFoot) + (RightToe - RightFoot)).GetSafeNormal2D();
    if (Forward.IsNearlyZero()) return false;
    OutYaw = -Forward.Rotation().Yaw;
    return FMath::IsFinite(OutYaw);
}

void Finish(bool bPassed, const TCHAR* Reason)
{
    if (Preview.bDone) return;
    Preview.bDone = true;
    UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_%s models=%d screenshots=%d reason=%s directory=%s"),
        bPassed ? TEXT("PASS") : TEXT("FAIL"), Preview.Models.Num(), Preview.Screenshots.Num(), Reason, *Preview.Directory);
    FPlatformMisc::RequestExitWithStatus(false, bPassed ? 0 : 1);
}

void SetView(bool bBack)
{
    if (!Preview.Camera.IsValid()) return;
    const float Side = bBack ? -1.f : 1.f;
    const FVector Position = Preview.Center + FVector(Side * Preview.CameraDistance, 0, 35);
    Preview.Camera->SetActorLocation(Position);
    Preview.Camera->SetActorRotation((Preview.Center - Position).Rotation());
    if (Preview.Key.IsValid()) Preview.Key->SetActorRotation(FRotator(-35, bBack ? -35.f : 145.f, 0));
    if (Preview.Fill.IsValid()) Preview.Fill->SetActorLocation(Preview.Center + FVector(Side * 350, -250, 240));
    for (auto& Label : Preview.Labels)
        if (Label.IsValid()) Label->SetActorRotation(FRotator(0, bBack ? 180.f : 0.f, 0));
}

void Capture(const TCHAR* Name)
{
    int32 Width = 0, Height = 0;
    if (Preview.Controller.IsValid()) Preview.Controller->GetViewportSize(Width, Height);
    int32 RequestedWidth = Width, RequestedHeight = Height;
    FParse::Value(FCommandLine::Get(), TEXT("ResX="), RequestedWidth);
    FParse::Value(FCommandLine::Get(), TEXT("ResY="), RequestedHeight);
    const bool bResolutionMatches = Width > 0 && Height > 0 && Width == RequestedWidth && Height == RequestedHeight;
    Preview.bPoseChecksPassed &= bResolutionMatches;
    UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_VIEWPORT capture=%s actual=%dx%d requested=%dx%d match=%d"),
        Name, Width, Height, RequestedWidth, RequestedHeight, bResolutionMatches);
    for (int32 Index = 0; Index < Preview.Models.Num(); ++Index)
    {
        if (!Preview.Models[Index].IsValid()) { Preview.bPoseChecksPassed = false; continue; }
        auto* Component = Preview.Models[Index]->GetSkeletalMeshComponent();
        const FBoxSphereBounds Bounds = Component->Bounds;
        bool bValid = !Component->GetComponentTransform().ContainsNaN() && !Bounds.Origin.ContainsNaN() && !Bounds.BoxExtent.ContainsNaN()
            && Component->IsVisible() && !Preview.Models[Index]->IsHidden() && Bounds.BoxExtent.Z >= 20 && Bounds.BoxExtent.Z <= 400;
        UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_POSE capture=%s model=%d component=%s bounds_origin=%s bounds_extent=%s visible=%d hidden=%d"),
            Name, Index, *Component->GetComponentTransform().ToString(), *Bounds.Origin.ToString(), *Bounds.BoxExtent.ToString(),
            Component->IsVisible(), Preview.Models[Index]->IsHidden());
        const TCHAR* BoneNames[] = {TEXT("root"), TEXT("pelvis"), TEXT("head"), TEXT("foot_l"), TEXT("foot_r")};
        FVector Positions[5] = {};
        for (int32 Bone = 0; Bone < UE_ARRAY_COUNT(BoneNames); ++Bone)
        {
            const int32 BoneIndex = Component->GetBoneIndex(FName(BoneNames[Bone]));
            if (BoneIndex == INDEX_NONE) { bValid = false; continue; }
            const FTransform Evaluated = Component->GetBoneTransform(BoneIndex);
            Positions[Bone] = Evaluated.GetLocation();
            bValid &= !Evaluated.ContainsNaN();
            UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_BONE capture=%s model=%d bone=%s world=%s scale=%s"),
                Name, Index, BoneNames[Bone], *Positions[Bone].ToString(), *Evaluated.GetScale3D().ToString());
            if (Bone == 0 && Component->GetSkeletalMeshAsset())
            {
                const auto& Reference = Component->GetSkeletalMeshAsset()->GetRefSkeleton();
                const FVector ReferenceScale = Reference.GetRefBonePose()[BoneIndex].GetScale3D();
                UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_ROOT_SCALE capture=%s model=%d reference_local=%s evaluated_world=%s component_world=%s"),
                    Name, Index, *ReferenceScale.ToString(), *Evaluated.GetScale3D().ToString(), *Component->GetComponentScale().ToString());
            }
        }
        const double HeadToFeet = Positions[2].Z - FMath::Min(Positions[3].Z, Positions[4].Z);
        bValid &= HeadToFeet >= 50 && HeadToFeet <= 350
            && Positions[2].Z >= Preview.FloorHeights[Index] + 60 && Positions[2].Z <= Preview.FloorHeights[Index] + 400
            && FVector::Dist2D(Positions[2], Component->GetComponentLocation()) <= 350;
        FVector2D HeadPixel;
        bValid &= Preview.Controller.IsValid() && Preview.Controller->ProjectWorldLocationToScreen(Positions[2], HeadPixel)
            && HeadPixel.X >= 0 && HeadPixel.X < Width && HeadPixel.Y >= 0 && HeadPixel.Y < Height;
        Preview.bPoseChecksPassed &= bValid;
        UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_POSE_CHECK capture=%s model=%d valid=%d head_to_feet=%.3f"),
            Name, Index, bValid, HeadToFeet);
    }
    const FString Filename = FPaths::Combine(Preview.Directory, FString(Name) + TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(Filename, false, false, false, FIntRect(), true);
    Preview.Screenshots.Add(Filename);
    UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_CAPTURE name=%s file=%s"), Name, *Filename);
}

bool SetLocomotion(int32 State)
{
    const TCHAR* Names[] = {TEXT("idle"), TEXT("walk"), TEXT("jog")};
    if (State < 0 || State > 2 || Preview.Models.Num() != 3 || Preview.Locomotion.Num() != 3) return false;
    for (int32 Index = 0; Index < Preview.Models.Num(); ++Index)
    {
        if (!Preview.Models[Index].IsValid() || !Preview.Locomotion[Index].Asset.IsValid()) return false;
        auto* Component = Preview.Models[Index]->GetSkeletalMeshComponent();
        Component->SetForceRefPose(false);
        auto* Animation = Component->GetSingleNodeInstance();
        if (!Animation) return false;
        Animation->SetAnimationAsset(Preview.Locomotion[Index].Asset.Get(), true, 1.f);
        Animation->SetRootMotionMode(ERootMotionMode::IgnoreRootMotion);
        Animation->SetBlendSpacePosition(Preview.Locomotion[Index].Inputs[State]);
        Animation->SetPosition(0, false);
        Animation->SetPlaying(true);
        UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_ANIMATION model=%d state=%s asset=%s input=%s matching_skeleton=1"),
            Index, Names[State], *Preview.Locomotion[Index].Asset->GetPathName(), *Preview.Locomotion[Index].Inputs[State].ToString());
    }
    Preview.LocomotionState = State;
    SetView(false);
    return true;
}

bool FreezeLocomotion()
{
    if (Preview.LocomotionState == INDEX_NONE) return false;
    for (int32 Index = 0; Index < Preview.Models.Num(); ++Index)
    {
        if (!Preview.Models[Index].IsValid()) return false;
        auto* Component = Preview.Models[Index]->GetSkeletalMeshComponent();
        auto* Animation = Component->GetSingleNodeInstance();
        if (!Animation || Animation->GetAnimationAsset() != Preview.Locomotion[Index].Asset.Get()) return false;
        // Capture the same gait phase on all three rigs after their blend filters have settled.
        // UAnimSingleNodeInstance uses normalized [0,1] position for BlendSpaces.
        Animation->SetPosition(.25f, false);
        Animation->SetPlaying(false);
    }
    return true;
}

bool BuildFixture(ACireGameMode* Mode, ACireController* Controller, ACireHero* Hero)
{
    UWorld* World = Mode->GetWorld();
    Preview.Controller = Controller;
    Hero->bBot = false;
    Hero->bAutoAttack = false;
    Hero->Target = nullptr;
    Hero->SetActorHiddenInGame(true);
    Hero->SetActorEnableCollision(false);
    Hero->GetCharacterMovement()->StopMovementImmediately();
    Hero->GetCharacterMovement()->DisableMovement();
    Hero->SetActorTickEnabled(false);
    Controller->SetIgnoreMoveInput(true);
    Controller->SetIgnoreLookInput(true);
    Controller->bShowMouseCursor = false;
    if (AHUD* HUD = Controller->GetHUD()) HUD->bShowHUD = false;

    // Neutral light colors only in this opt-in process; normal world lighting is untouched.
    for (TActorIterator<APointLight> It(World); It; ++It) It->PointLightComponent->SetIntensity(0);
    for (TActorIterator<ADirectionalLight> It(World); It; ++It) It->GetLightComponent()->SetIntensity(0);
    for (TActorIterator<ASkyLight> It(World); It; ++It)
    {
        It->GetLightComponent()->SetLightColor(FLinearColor::White);
        It->GetLightComponent()->SetIntensity(0.9f);
    }
    Preview.Key = World->SpawnActor<ADirectionalLight>(FVector(0, -2100, 2000), FRotator(-35, 145, 0));
    if (!Preview.Key.IsValid()) return false;
    Preview.Key->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Preview.Key->GetLightComponent()->SetLightColor(FLinearColor::White);
    Preview.Key->GetLightComponent()->SetIntensity(6.f);
    Preview.Fill = World->SpawnActor<APointLight>();
    if (!Preview.Fill.IsValid()) return false;
    Preview.Fill->PointLightComponent->SetMobility(EComponentMobility::Movable);
    Preview.Fill->PointLightComponent->SetLightColor(FLinearColor::White);
    Preview.Fill->PointLightComponent->SetIntensityUnits(ELightUnits::Lumens);
    Preview.Fill->PointLightComponent->SetIntensity(1800);
    Preview.Fill->PointLightComponent->SetAttenuationRadius(1600);
    Preview.Fill->PointLightComponent->SetCastShadows(false);

    const TCHAR* Assets[] = {
        TEXT("/Game/TripoModels/medieval_knight_armor_3d_model/medieval_knight_armor_3d_model.medieval_knight_armor_3d_model"),
        TEXT("/Game/TripoModels/armored_archer_3d_model/armored_archer_3d_model.armored_archer_3d_model"),
        TEXT("/Game/TripoModels/battlefield_healer_3d_model/battlefield_healer_3d_model.battlefield_healer_3d_model")
    };
    const TCHAR* Names[] = {TEXT("IRON WARDEN | 184 cm"), TEXT("ASH RANGER | 178 cm"), TEXT("VEIL SCHOLAR | 176 cm")};
    const TCHAR* RigNames[] = {TEXT("Warden"), TEXT("Ranger"), TEXT("Scholar")};
    const float Heights[] = {184.f, 178.f, 176.f};
    FBox GroupBounds(ForceInit);
    FActorSpawnParameters Params;
    Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for (int32 Index = 0; Index < 3; ++Index)
    {
        auto* Asset = LoadObject<USkeletalMesh>(nullptr, Assets[Index]);
        if (!Asset)
        {
            UE_LOG(LogCireArtPreview, Error, TEXT("CIRE_ART_PREVIEW_ASSET_MISSING path=%s"), Assets[Index]);
            return false;
        }
        const FString BlendName = FString::Printf(TEXT("BS_Idle_Walk_Run_%s"), RigNames[Index]);
        const FString BlendPath = FString::Printf(TEXT("/Game/Art/Characters/TripoRetarget/Preview02/%s/Animations/%s.%s"),
            RigNames[Index], *BlendName, *BlendName);
        auto* Blend = LoadObject<UBlendSpace>(nullptr, *BlendPath);
        if (!Blend || !Asset->GetSkeleton() || Blend->GetSkeleton() != Asset->GetSkeleton())
        {
            UE_LOG(LogCireArtPreview, Error, TEXT("CIRE_ART_PREVIEW_ANIMATION_INVALID path=%s mesh_skeleton=%s animation_skeleton=%s"),
                *BlendPath, *GetNameSafe(Asset->GetSkeleton()), *GetNameSafe(Blend ? Blend->GetSkeleton() : nullptr));
            return false;
        }
        FPreviewLocomotion Locomotion;
        Locomotion.Asset = Blend;
        const FString SampleNames[] = {
            FString::Printf(TEXT("MM_Idle_%s"), RigNames[Index]),
            FString::Printf(TEXT("MF_Unarmed_Walk_Fwd_%s"), RigNames[Index]),
            FString::Printf(TEXT("MF_Unarmed_Jog_Fwd_%s"), RigNames[Index])
        };
        bool bFoundSamples[3] = {};
        for (const FBlendSample& Sample : Blend->GetBlendSamples())
        {
            if (!Sample.Animation || Sample.Animation->GetSkeleton() != Asset->GetSkeleton() || Sample.Animation->GetPlayLength() <= 0.f)
            {
                UE_LOG(LogCireArtPreview, Error, TEXT("CIRE_ART_PREVIEW_SAMPLE_INVALID blend=%s sample=%s"),
                    *BlendPath, *GetNameSafe(Sample.Animation.Get()));
                return false;
            }
            for (int32 State = 0; State < 3; ++State)
            {
                if (Sample.Animation->GetName() != SampleNames[State]) continue;
                // Idle can occur at several direction coordinates; prefer the forward sample.
                if (!bFoundSamples[State] || Sample.SampleValue.SizeSquared() < Locomotion.Inputs[State].SizeSquared())
                {
                    Locomotion.Inputs[State] = Sample.SampleValue;
                    bFoundSamples[State] = true;
                }
            }
        }
        if (!bFoundSamples[0] || !bFoundSamples[1] || !bFoundSamples[2])
        {
            UE_LOG(LogCireArtPreview, Error, TEXT("CIRE_ART_PREVIEW_SAMPLE_MISSING blend=%s idle=%d walk=%d jog=%d"),
                *BlendPath, bFoundSamples[0], bFoundSamples[1], bFoundSamples[2]);
            return false;
        }
        float FacingYaw = 0.f;
        if (!GetFacingYaw(*Asset, FacingYaw)) return false;
        const FBoxSphereBounds Imported = Asset->GetImportedBounds();
        const double ImportedHeight = Imported.BoxExtent.Z * 2;
        if (!FMath::IsFinite(ImportedHeight) || ImportedHeight < 0.001) return false;
        const float Factor = static_cast<float>(Heights[Index] / ImportedHeight);
        FVector Position(450, -2100 + (Index - 1) * 250.f, 0);
        FHitResult Floor;
        if (!World->LineTraceSingleByChannel(Floor, Position + FVector(0, 0, 900), Position - FVector(0, 0, 600), ECC_Visibility))
            return false;
        Position.Z = Floor.ImpactPoint.Z + 1 - (Imported.Origin.Z - Imported.BoxExtent.Z) * Factor;
        auto* Model = World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), Position, FRotator(0, FacingYaw, 0), Params);
        if (!Model) return false;
        Model->SetReplicates(false);
        auto* Component = Model->GetSkeletalMeshComponent();
        Component->SetSkeletalMesh(Asset);
        Component->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        Component->SetAnimation(Blend);
        Component->Stop();
        Component->SetForceRefPose(true);
        Component->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Component->bEnableUpdateRateOptimizations = false;
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);
        Component->SetCanEverAffectNavigation(false);
        Component->SetTextureForceResidentFlag(true);
        Component->SetWorldScale3D(FVector(Factor));
        Component->UpdateBounds();
        GroupBounds += Component->Bounds.GetBox();
        Preview.Models.Add(Model);
        Preview.Locomotion.Add(Locomotion);
        Preview.FloorHeights.Add(Floor.ImpactPoint.Z);

        auto* Label = World->SpawnActor<ATextRenderActor>(Position + FVector(0, 0, 0), FRotator::ZeroRotator);
        if (!Label) return false;
        Label->SetActorLocation(FVector(Position.X, Position.Y, Floor.ImpactPoint.Z + 214));
        auto* Text = Label->GetTextRender();
        Text->SetText(FText::FromString(Names[Index]));
        Text->SetHorizontalAlignment(EHTA_Center);
        Text->SetWorldSize(9.f);
        Text->SetTextRenderColor(FColor(220, 225, 225));
        Preview.Labels.Add(Label);
        UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_MODEL index=%d asset=%s skeleton=%s imported_height=%.3f target_height=%.0f scale=%.5f floor=%.3f facing_yaw=%.3f pose=reference material_slots=%d"),
            Index, Assets[Index], *GetNameSafe(Asset->GetSkeleton()), ImportedHeight, Heights[Index], Factor, Floor.ImpactPoint.Z, FacingYaw, Component->GetNumMaterials());
    }
    Preview.Center = GroupBounds.GetCenter();
    Preview.Center.Z = FMath::Max(Preview.Center.Z, 104.0);
    Preview.CameraDistance = FMath::Max(700.f, static_cast<float>(GroupBounds.GetSize().Y * 0.59 / FMath::Tan(FMath::DegreesToRadians(25.f))));
    Preview.Camera = World->SpawnActor<ACameraActor>();
    if (!Preview.Camera.IsValid()) return false;
    auto* Camera = Preview.Camera->GetCameraComponent();
    Camera->SetFieldOfView(50);
    Camera->SetAspectRatio(16.f / 9.f);
    Camera->bConstrainAspectRatio = true;
    Camera->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Camera->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Camera->PostProcessSettings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
    Camera->PostProcessSettings.AutoExposureApplyPhysicalCameraExposure = false;
    Camera->PostProcessSettings.bOverride_AutoExposureBias = true;
    Camera->PostProcessSettings.AutoExposureBias = .5f;
    Camera->PostProcessSettings.bOverride_AutoExposureBiasCurve = true;
    Camera->PostProcessSettings.AutoExposureBiasCurve = nullptr;
    Camera->PostProcessSettings.bOverride_BloomIntensity = true;
    Camera->PostProcessSettings.BloomIntensity = 0;
    Camera->PostProcessSettings.bOverride_MotionBlurAmount = true;
    Camera->PostProcessSettings.MotionBlurAmount = 0;
    Camera->PostProcessBlendWeight = 1;
    SetView(false);
    Controller->SetViewTarget(Preview.Camera.Get());
    Preview.ReadyAt = FPlatformTime::Seconds();
    UE_LOG(LogCireArtPreview, Display, TEXT("CIRE_ART_PREVIEW_READY models=3 reference_pose=1 animations=idle,walk,jog duration=44 yaw=reference_bone_derived camera_distance=%.1f directory=%s"),
        Preview.CameraDistance, *Preview.Directory);
    return true;
}
}

bool CireArtPreview::Initialize(ACireGameMode* Mode)
{
    Preview = {};
    if (!FParse::Param(FCommandLine::Get(), TEXT("CireArtPreview"))) return false;
    Preview.Mode = Mode;
    Preview.Started = FPlatformTime::Seconds();
    Preview.Directory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ArtPreview"),
        FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8)));
    if (!IsValid(Mode) || Mode->GetNetMode() != NM_Standalone || !IFileManager::Get().MakeDirectory(*Preview.Directory, true))
    {
        Finish(false, TEXT("requires standalone rendering and writable screenshot directory"));
        return true;
    }
    Mode->bBotsFilled = true;
    Mode->BotFillTimer = MAX_flt;
    Mode->WaveTimer = MAX_flt;
    return true;
}

bool CireArtPreview::Tick(ACireGameMode* Mode)
{
    if (Preview.Mode.Get() != Mode) return false;
    if (Preview.bDone) return true;
    const double Now = FPlatformTime::Seconds();
    if (Now - Preview.Started > 55)
    {
        Finish(false, TEXT("preview exceeded 55-second safety limit"));
        return true;
    }
    if (Preview.ReadyAt < 0)
    {
        auto* Controller = Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        auto* Hero = Controller ? Cast<ACireHero>(Controller->GetPawn()) : nullptr;
        if (Controller && Hero && Controller->GetHUD() && !BuildFixture(Mode, Controller, Hero))
            Finish(false, TEXT("asset/skeleton/sample validation, floor trace, or fixture spawn failed"));
        return true;
    }
    const double Elapsed = Now - Preview.ReadyAt;
    if (Preview.Stage == 0 && Elapsed >= 8) { Capture(TEXT("01_front_reference")); Preview.Stage = 1; }
    else if (Preview.Stage == 1 && Elapsed >= 11) { SetView(true); Preview.Stage = 2; }
    else if (Preview.Stage == 2 && Elapsed >= 16) { Capture(TEXT("02_back_reference")); Preview.Stage = 3; }
    else if (Preview.Stage == 3 && Elapsed >= 18)
    {
        if (!SetLocomotion(0)) Finish(false, TEXT("idle playback setup failed"));
        Preview.Stage = 4;
    }
    else if (Preview.Stage == 4 && Elapsed >= 23)
    {
        if (!FreezeLocomotion()) Finish(false, TEXT("idle instance validation failed"));
        Preview.Stage = 5;
    }
    else if (Preview.Stage == 5 && Elapsed >= 24) { Capture(TEXT("03_front_idle")); Preview.Stage = 6; }
    else if (Preview.Stage == 6 && Elapsed >= 26)
    {
        if (!SetLocomotion(1)) Finish(false, TEXT("walk playback setup failed"));
        Preview.Stage = 7;
    }
    else if (Preview.Stage == 7 && Elapsed >= 31)
    {
        if (!FreezeLocomotion()) Finish(false, TEXT("walk instance validation failed"));
        Preview.Stage = 8;
    }
    else if (Preview.Stage == 8 && Elapsed >= 32) { Capture(TEXT("04_front_walk")); Preview.Stage = 9; }
    else if (Preview.Stage == 9 && Elapsed >= 34)
    {
        if (!SetLocomotion(2)) Finish(false, TEXT("jog playback setup failed"));
        Preview.Stage = 10;
    }
    else if (Preview.Stage == 10 && Elapsed >= 39)
    {
        if (!FreezeLocomotion()) Finish(false, TEXT("jog instance validation failed"));
        Preview.Stage = 11;
    }
    else if (Preview.Stage == 11 && Elapsed >= 40) { Capture(TEXT("05_front_jog")); Preview.Stage = 12; }
    if (Elapsed >= 44)
    {
        bool bSaved = Preview.Models.Num() == 3 && Preview.Locomotion.Num() == 3 && Preview.Stage == 12 && Preview.Screenshots.Num() == 5
            && Preview.bPoseChecksPassed;
        for (const FString& Filename : Preview.Screenshots) bSaved &= IFileManager::Get().FileSize(*Filename) > 1024;
        Finish(bSaved, TEXT("skeletons, samples, evaluated body bounds/bones, requested resolution and five captures checked; visual quality requires review"));
    }
    return true;
}
#endif
