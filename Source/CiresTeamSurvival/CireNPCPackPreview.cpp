#include "CireNPCPackPreview.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireNPCArchetypes.h"
#include "CireAreaEffects.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNPCPackPreview,Log,All);
namespace
{
struct FPackPreview
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TArray<TWeakObjectPtr<ACireMonster>> Pack;
    TWeakObjectPtr<ACireMonster> Leader;
    TArray<FString> Captures;
    FString Directory;
    FVector Center=FVector::ZeroVector;
    double Started=0,Ready=-1;
    int32 Shots=0;
    bool bDone=false,bChecks=true;
};
FPackPreview Preview;
void Finish(bool bPass)
{
    if(Preview.bDone)return;
    Preview.bDone=true;
    UE_LOG(LogCireNPCPackPreview,Display,TEXT("CIRE_NPC_PACK_PREVIEW_%s captures=%d directory=%s"),bPass?TEXT("PASS"):TEXT("FAIL"),Preview.Captures.Num(),*Preview.Directory);
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
float FloorZ(UWorld* World,FVector P)
{
    FHitResult Hit;
    return World->LineTraceSingleByChannel(Hit,P+FVector(0,0,1500),P-FVector(0,0,1500),ECC_Visibility)?Hit.ImpactPoint.Z:110.f;
}
void Label(UWorld* World,FVector At,const FString& Text,FColor Color,float Size)
{
    if(auto* L=World->SpawnActor<ATextRenderActor>(At,FRotator(0,0,0)))
    {
        L->GetTextRender()->SetText(FText::FromString(Text));L->GetTextRender()->SetWorldSize(Size);
        L->GetTextRender()->SetTextRenderColor(Color);L->GetTextRender()->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    }
}
bool Build(ACireGameMode& Mode,ACireController& Controller)
{
    UWorld* World=Mode.GetWorld();
    if(auto* Player=Cast<ACireHero>(Controller.GetPawn()))
    {Player->SetActorHiddenInGame(true);Player->SetActorEnableCollision(false);Player->SetActorTickEnabled(false);}
    Controller.SetIgnoreMoveInput(true);Controller.SetIgnoreLookInput(true);Controller.bShowMouseCursor=false;
    if(Controller.GetHUD())Controller.GetHUD()->bShowHUD=false;
    for(TActorIterator<ASkyLight> It(World);It;++It){It->GetLightComponent()->SetIntensity(1.f);}
    auto* Sun=World->SpawnActor<ADirectionalLight>(FVector(0,-2100,1600),FRotator(-45,150,0));
    if(Sun){Sun->GetLightComponent()->SetMobility(EComponentMobility::Movable);Sun->GetLightComponent()->SetIntensity(5);}
    const auto& D=CireNPCArchetypes::Get();
    Preview.Center=FVector(-300,-2100,0);Preview.Center.Z=FloorZ(World,Preview.Center);
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const int32 Members=D.PackMembers.Num();
    for(int32 I=0;I<=Members;++I)
    {
        const bool bLeader=I==Members;
        const FVector Offset=bLeader?FVector(-60,0,0):FVector(160,(I-(Members-1)*.5f)*320.f,0);
        FVector P=Preview.Center+Offset;P.Z=FloorZ(World,P)+100;
        auto* M=World->SpawnActor<ACireMonster>(P,FRotator(0,0,0),Params);
        if(!M)return false;
        M->Lane=0;M->PackId=9901;
        if(!CireNPCCombat::ConfigureArchetype(M,bLeader?D.PackLeader:D.PackMembers[I],1,2,1))return false;
        M->SetActorTickEnabled(false);M->GetCharacterMovement()->DisableMovement();
        const float Half=M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        M->SetActorLocation(FVector(P.X,P.Y,FloorZ(World,P)+Half+2));M->SetActorRotation(FRotator(0,0,0));
        M->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        Mode.Monsters.Add(M);Preview.Pack.Add(M);if(bLeader)Preview.Leader=M;
        const auto* A=M->NPCState->Archetype();
        const FString Text=bLeader?FString::Printf(TEXT("BOSS  %s"),*A->DisplayName):FString::Printf(TEXT("%s  (%s)"),*CireNPCArchetypes::RoleLabel(A->Role).ToUpper(),*A->DisplayName);
        Label(World,M->GetActorLocation()+FVector(0,0,Half+40),Text,bLeader?FColor(255,200,60):FColor::White,bLeader?22.f:15.f);
        UE_LOG(LogCireNPCPackPreview,Display,TEXT("CIRE_NPC_PACK_UNIT id=%s role=%s class=%s scale=%.2f health=%.0f props=%d abilities=%d"),*A->Id.ToString(),
            *CireNPCArchetypes::RoleLabel(A->Role),*CireNPCArchetypes::ClassLabel(M->GetNPCClassification()),M->GetActorScale3D().X,M->MaxHealth,
            M->NPCState->VisualParts.Num(),A->Abilities.Num());
        Preview.bChecks&=M->NPCState->VisualParts.Num()>0;
    }
    // Show the leader's Sundering Cleave telegraph (long warning, no damage in the fixture).
    if(auto* Leader=Preview.Leader.Get())
        if(const auto* Cleave=Leader->NPCState->Archetype()->FindAbility(TEXT("boss_leader_cleave")))
        {
            FCireAreaSpec S;S.Shape=ECireAreaShape::Cone;S.Radius=Cleave->Radius;S.ConeAngleDegrees=Cleave->Angle;
            S.WarningSeconds=9;S.DurationSeconds=.3f;S.bPersistent=false;S.bPoison=false;S.DamagePerSecond=0;S.BurstDamage=0;
            S.Color=Cleave->Color;S.AbilityName=Cleave->Name;
            FVector Feet=Leader->GetActorLocation();Feet.Z=FloorZ(World,Feet);
            Preview.bChecks&=ACireAreaEffect::Spawn(Leader,S,Feet,FRotator(0,0,0))!=nullptr;
        }
    Preview.Camera=World->SpawnActor<ACameraActor>();
    if(!Preview.Camera.IsValid())return false;
    auto* Camera=Preview.Camera->GetCameraComponent();Camera->SetFieldOfView(55);Camera->SetAspectRatio(16.f/9.f);Camera->bConstrainAspectRatio=true;
    auto& Post=Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;
    Post.bOverride_AutoExposureBias=true;Post.AutoExposureBias=.5;
    Post.bOverride_MotionBlurAmount=true;Post.MotionBlurAmount=0;
    Controller.SetViewTarget(Preview.Camera.Get());Preview.Controller=&Controller;
    Preview.Ready=FPlatformTime::Seconds();
    UE_LOG(LogCireNPCPackPreview,Display,TEXT("CIRE_NPC_PACK_PREVIEW_READY units=%d"),Preview.Pack.Num());
    return true;
}
}

bool CireNPCPackPreview::Initialize(ACireGameMode* Mode)
{
    Preview={};
    if(!FParse::Param(FCommandLine::Get(),TEXT("CireNPCPackPreview")))return false;
    Preview.Mode=Mode;Preview.Started=FPlatformTime::Seconds();
    Preview.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("NPCPackPreview"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!IFileManager::Get().MakeDirectory(*Preview.Directory,true)){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;
    return true;
}

bool CireNPCPackPreview::Tick(ACireGameMode* Mode)
{
    if(Preview.Mode.Get()!=Mode)return false;
    if(Preview.bDone)return true;
    if(FPlatformTime::Seconds()-Preview.Started>90){Finish(false);return true;}
    if(Preview.Ready<0)
    {
        auto* Controller=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(Controller&&Controller->GetPawn()&&Controller->GetHUD()&&!Build(*Mode,*Controller))Finish(false);
        return true;
    }
    const double Age=FPlatformTime::Seconds()-Preview.Ready;
    const FVector Views[]={FVector(1050,0,330),FVector(760,-700,520)};
    const int32 View=FMath::Clamp(Preview.Shots,0,1);
    const FVector Eye=Preview.Center+Views[View],Look=Preview.Center+FVector(0,0,110);
    Preview.Camera->SetActorLocation(Eye);Preview.Camera->SetActorRotation((Look-Eye).Rotation());
    if(Age>=4+Preview.Shots*3&&Preview.Shots<2)
    {
        int32 Width=0,Height=0;Preview.Controller->GetViewportSize(Width,Height);
        for(const auto& Unit:Preview.Pack)
        {
            FVector2D Pixel;
            const bool bIn=Unit.IsValid()&&Preview.Controller->ProjectWorldLocationToScreen(Unit->GetActorLocation(),Pixel)&&Pixel.X>0&&Pixel.X<Width&&Pixel.Y>0&&Pixel.Y<Height;
            Preview.bChecks&=bIn;
        }
        const FString File=FPaths::Combine(Preview.Directory,FString::Printf(TEXT("%02d_pack_leader_%s.png"),Preview.Shots+1,View==0?TEXT("front"):TEXT("angled")));
        FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);Preview.Captures.Add(File);
        UE_LOG(LogCireNPCPackPreview,Display,TEXT("CIRE_NPC_PACK_PREVIEW_CAPTURE name=%d file=%s"),Preview.Shots+1,*File);
        ++Preview.Shots;
    }
    if(Age>=12)
    {
        bool bPass=Preview.bChecks&&Preview.Captures.Num()==2&&Preview.Leader.IsValid()&&
            Preview.Leader->GetNPCClassification()==ECireNPCClass::Boss;
        for(const auto& Unit:Preview.Pack)bPass&=Unit.IsValid()&&Unit->GetActorScale3D().X<=Preview.Leader->GetActorScale3D().X;
        for(const FString& File:Preview.Captures)bPass&=IFileManager::Get().FileSize(*File)>1024;
        Finish(bPass);
    }
    return true;
}
#endif
