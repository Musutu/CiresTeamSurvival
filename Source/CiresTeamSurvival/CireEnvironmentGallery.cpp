#include "CireEnvironmentGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireEnvironmentProps.h"
#include "CireNPCCombat.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireEnvironmentGallery,Log,All);
namespace
{
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<ACireMonster> Escort;
    TWeakObjectPtr<UTextRenderComponent> Label;
    FString Directory;
    TArray<FString> Captures;
    double Started=0,Ready=-1;
    int32 CapturedStage=-1,Checks=0;
    bool bPass=true,bDone=false,bEscortMoving=false;
} Gallery;
void Check(bool Value,const TCHAR* Label)
{
    ++Gallery.Checks;
    if(!Value){Gallery.bPass=false;UE_LOG(LogCireEnvironmentGallery,Error,TEXT("CIRE_ENVIRONMENT_GALLERY_CHECK_FAIL %s"),Label);}
}
bool Fail(const TCHAR* Reason)
{
    UE_LOG(LogCireEnvironmentGallery,Error,TEXT("CIRE_ENVIRONMENT_GALLERY_ABORT reason=%s world=%s camera=%s escort=%s ready=%.3f elapsed=%.3f"),
        Reason,*GetNameSafe(Gallery.Mode.IsValid()?Gallery.Mode->GetWorld():nullptr),*GetNameSafe(Gallery.Camera.Get()),
        *GetNameSafe(Gallery.Escort.Get()),Gallery.Ready,FPlatformTime::Seconds()-Gallery.Started);
    Gallery.bPass=false;return false;
}
void Finish(bool bPass)
{
    if(Gallery.bDone)return;Gallery.bDone=true;
    UE_LOG(LogCireEnvironmentGallery,Display,TEXT("CIRE_ENVIRONMENT_GALLERY_%s captures=%d checks=%d directory=%s"),
        bPass?TEXT("PASS"):TEXT("FAIL"),Gallery.Captures.Num(),Gallery.Checks,*Gallery.Directory);
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
bool GroundAt(UWorld* World,FVector Point,FVector& Ground)
{
    FHitResult Hit;FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    const FVector From=Point+FVector(0,0,1000),To=Point-FVector(0,0,500);
    if(!World->LineTraceSingleByObjectType(Hit,From,To,Objects))
    {
        // Keep the required static-floor query strict. A second diagnostic query
        // identifies accidentally dynamic geometry without accepting it as ground.
        FHitResult DynamicHit;FCollisionObjectQueryParams DynamicObjects;DynamicObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
        const bool bDynamic=World->LineTraceSingleByObjectType(DynamicHit,From,To,DynamicObjects);
        const auto* Component=DynamicHit.GetComponent();
        UE_LOG(LogCireEnvironmentGallery,Error,TEXT("CIRE_ENVIRONMENT_GROUND_MISSING from=%s to=%s dynamic_hit=%d actor=%s component=%s object_type=%d collision=%d impact=%s"),
            *From.ToString(),*To.ToString(),bDynamic,*GetNameSafe(DynamicHit.GetActor()),*GetNameSafe(Component),
            Component?static_cast<int32>(Component->GetCollisionObjectType()):-1,Component?static_cast<int32>(Component->GetCollisionEnabled()):-1,*DynamicHit.ImpactPoint.ToString());
        return false;
    }
    Ground=Hit.ImpactPoint;return true;
}
void CheckEnvironment(UWorld* World)
{
    ACireWorld* Battlefield=nullptr;
    for(TActorIterator<ACireWorld> It(World);It;++It){Battlefield=*It;break;}
    Check(Battlefield!=nullptr,TEXT("battlefield actor exists"));
    if(Battlefield)
    {
        auto* Road=Battlefield->RouteRoad.Get();auto* Material=Road?Road->GetMaterial(0):nullptr;
        Check(Road && Road->GetStaticMesh() && Road->GetInstanceCount()>0 && Material && Material->GetPathName()==TEXT("/Game/Art/Environment/Materials/M_BasaltRoad.M_BasaltRoad"),TEXT("route paving has geometry and the authored basalt road material"));
        Check(Battlefield->RenderedRouteRevision==CireLanePath::Revision(World),TEXT("rendered road uses current world route revision"));
        Check(CireEnvironmentProps::InstanceCount(Battlefield)>40,TEXT("original modeled environment props are present"));
        Check(CireEnvironmentProps::HasSafeClearance(Battlefield),TEXT("authored prop footprints preserve route, challenge and town entrance clearance"));
    }
    const auto& Routes=CireLanePath::Get(World);
    FCollisionObjectQueryParams Objects;Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireEnvironmentWalkline),false);
    for(int32 Team=0;Team<2;++Team)
    {
        const auto& Points=Routes.LocalPoints[Team];
        for(int32 I=0;I+1<Points.Num();++I)
        {
            FVector A=FVector::ZeroVector,B=FVector::ZeroVector;
            const bool bSupported=GroundAt(World,FVector(Points[I].X,Points[I].Y+CireLanePath::CenterY(Team),0),A) &&
                GroundAt(World,FVector(Points[I+1].X,Points[I+1].Y+CireLanePath::CenterY(Team),0),B);
            Check(bSupported,TEXT("route segment endpoints have supporting floor"));
            if(!bSupported)continue;
            A.Z+=100;B.Z+=100;FHitResult Hit;
            const bool bBlocked=World->SweepSingleByObjectType(Hit,A,B,FQuat::Identity,Objects,FCollisionShape::MakeCapsule(45,90),Query);
            Check(!bBlocked,TEXT("route segment has capsule clearance against world geometry"));
            if(bBlocked)UE_LOG(LogCireEnvironmentGallery,Error,TEXT("CIRE_ENVIRONMENT_BLOCKER team=%d segment=%d actor=%s component=%s point=%s"),Team,I,*GetNameSafe(Hit.GetActor()),*GetNameSafe(Hit.GetComponent()),*Hit.ImpactPoint.ToString());
        }
    }
}
bool Build(ACireGameMode* Mode,ACireController* Controller)
{
    UWorld* World=Mode->GetWorld();auto* Player=Cast<ACireHero>(Controller->GetPawn());if(!Player)return Fail(TEXT("controller pawn is not a CireHero"));
    Player->TeamId=0;Player->bDrafted=true;Player->SetActorHiddenInGame(true);Player->SetActorEnableCollision(false);Player->SetActorTickEnabled(false);
    Player->GetCharacterMovement()->StopMovementImmediately();Player->GetCharacterMovement()->DisableMovement();
    Controller->SetIgnoreMoveInput(true);Controller->SetIgnoreLookInput(true);Controller->bShowMouseCursor=false;
    if(Controller->GetHUD())Controller->GetHUD()->bShowHUD=false;
    for(auto* Monster:Mode->Monsters)if(IsValid(Monster))Monster->Destroy();Mode->Monsters.Reset();
    Gallery.Camera=World->SpawnActor<ACameraActor>();if(!Gallery.Camera.IsValid())return Fail(TEXT("camera actor failed to spawn"));
    auto* Camera=Gallery.Camera->GetCameraComponent();Camera->SetFieldOfView(65);Camera->SetAspectRatio(16.f/9.f);Camera->bConstrainAspectRatio=true;
    auto& Post=Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;
    Post.bOverride_AutoExposureBias=true;Post.AutoExposureBias=.65f;
    Post.bOverride_MotionBlurAmount=true;Post.MotionBlurAmount=0;
    auto* Label=NewObject<UTextRenderComponent>(Gallery.Camera.Get());Gallery.Camera->AddInstanceComponent(Label);
    Label->SetupAttachment(Camera);Label->SetRelativeLocation(FVector(250,-145,76));Label->SetRelativeRotation(FRotator(0,180,0));
    Label->SetWorldSize(5.f);Label->SetTextRenderColor(FColor(246,219,155));Label->SetHorizontalAlignment(EHTA_Left);
    Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);Label->RegisterComponent();Gallery.Label=Label;
    const auto& Points=CireLanePath::Get(World).LocalPoints[0];
    if(Points.Num()<3)return Fail(TEXT("lane zero has fewer than three waypoints"));
    const FVector2D Center=FMath::Lerp(Points[Points.Num()/2-1],Points[Points.Num()/2],.5);
    FVector Ground;if(!GroundAt(World,FVector(Center.X,Center.Y+CireLanePath::CenterY(0),0),Ground))return Fail(TEXT("escort staging point has no WorldStatic floor"));
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Gallery.Escort=World->SpawnActor<ACireMonster>(Ground+FVector(0,0,140),FRotator(0,180,0),Params);
    if(!Gallery.Escort.IsValid())return Fail(TEXT("escort actor failed to spawn"));
    auto* Escort=Gallery.Escort.Get();Escort->Lane=0;CireNPCCombat::Configure(Escort,1,4);
    const float BaseHealth=Escort->Health;CireLanePath::ConfigureEscort(Escort);Escort->SetActorScale3D(FVector(1.45f));
    Escort->SetActorLocation(Ground+FVector(0,0,Escort->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+3));Escort->SetActorTickEnabled(false);
    CireLanePath::InitializeProgress(Escort);Mode->Monsters.Add(Escort);
    Check(Escort->bArmoredEscort && FMath::IsNearlyEqual(Escort->Health,BaseHealth*CireLanePath::Get(World).EscortHealthMultiplier),TEXT("gallery escort uses actual authored escort health"));
    Gallery.Controller=Controller;Controller->SetViewTarget(Gallery.Camera.Get());Gallery.Ready=FPlatformTime::Seconds();
    CheckEnvironment(World);
    UE_LOG(LogCireEnvironmentGallery,Display,TEXT("CIRE_ENVIRONMENT_GALLERY_READY views=4 route_clearance=45x90 original_environment=1"));return true;
}
}
bool CireEnvironmentGallery::Initialize(ACireGameMode* Mode)
{
    Gallery={};if(!FParse::Param(FCommandLine::Get(),TEXT("CireEnvironmentGallery")))return false;
    Gallery.Mode=Mode;Gallery.Started=FPlatformTime::Seconds();
    Gallery.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("EnvironmentGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode || Mode->GetNetMode()!=NM_Standalone){Fail(TEXT("gallery requires a standalone authoritative match"));Finish(false);return true;}
    if(!IFileManager::Get().MakeDirectory(*Gallery.Directory,true)){Fail(TEXT("capture directory could not be created"));Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;return true;
}
bool CireEnvironmentGallery::Tick(ACireGameMode* Mode)
{
    if(Gallery.Mode.Get()!=Mode)return false;if(Gallery.bDone)return true;
    if(FPlatformTime::Seconds()-Gallery.Started>90){Fail(TEXT("gallery exceeded 90 seconds waiting for setup or captures"));Finish(false);return true;}
    if(Gallery.Ready<0)
    {
        auto* Controller=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(Controller && Controller->GetPawn() && Controller->GetHUD() && !Build(Mode,Controller))Finish(false);
        return true;
    }
    if(!Gallery.Camera.IsValid() || !Gallery.Controller.IsValid() || !Gallery.Escort.IsValid() || !Gallery.Label.IsValid())
    {Fail(TEXT("a required gallery actor/component disappeared"));Finish(false);return true;}
    const double Age=FPlatformTime::Seconds()-Gallery.Ready;const int32 Stage=FMath::Clamp(FMath::FloorToInt((Age-8)/5),0,3);
    const auto& Routes=CireLanePath::Get(Mode->GetWorld());FVector Target,View;
    const TCHAR* Titles[]={TEXT("01  TOWN DEFENSE / ORIGINAL ENVIRONMENT"),TEXT("02  WINDING STREET / EDITABLE ROUTE"),TEXT("03  PRIVATE LANE / FULL OVERVIEW"),TEXT("04  ARMORED ESCORT / PROTOTYPE BODY")};
    // Wider approach view includes the keep name, both entry pillars and the
    // entire defended line with margin instead of cropping the landmark top.
    if(Stage==0){Target=FVector(-1900,-2100,440);View=FVector(650,-3100,1180);}
    else if(Stage==1){Target=FVector(5900,-2100,190);View=FVector(2900,-2800,1000);}
    else if(Stage==2){Target=FVector((Routes.MinX+Routes.MaxX)*.5,-2100,0);View=Target+FVector(700,-9500,12500);}
    else
    {
        if(!Gallery.bEscortMoving){Gallery.Escort->SetActorTickEnabled(true);Gallery.bEscortMoving=true;}
        Target=Gallery.Escort->GetActorLocation()+FVector(0,0,30);View=Target+FVector(-530,-550,240);
    }
    Gallery.Camera->SetActorLocation(View);Gallery.Camera->SetActorRotation((Target-View).Rotation());
    Gallery.Label->SetText(FText::FromString(Titles[Stage]));
    if(Age>=10+Stage*5 && Stage>Gallery.CapturedStage)
    {
        const TCHAR* Names[]={TEXT("01_town.png"),TEXT("02_street.png"),TEXT("03_lane_overview.png"),TEXT("04_armored_escort.png")};
        const FString File=FPaths::Combine(Gallery.Directory,Names[Stage]);FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);
        Gallery.Captures.Add(File);Gallery.CapturedStage=Stage;
        if(Stage==3)
        {
            FVector2D Pixel;int32 Width=0,Height=0;Gallery.Controller->GetViewportSize(Width,Height);
            Check(!Gallery.Escort->IsHidden() && Gallery.Controller->ProjectWorldLocationToScreen(Gallery.Escort->GetActorLocation(),Pixel) && Pixel.X>0 && Pixel.X<Width && Pixel.Y>0 && Pixel.Y<Height,TEXT("escort is visible and framed in the final capture"));
        }
        UE_LOG(LogCireEnvironmentGallery,Display,TEXT("CIRE_ENVIRONMENT_GALLERY_CAPTURE stage=%d file=%s"),Stage,*File);
    }
    if(Age>=29)
    {
        Check(Gallery.Captures.Num()==4,TEXT("all four environment views captured"));
        for(const FString& File:Gallery.Captures)Check(IFileManager::Get().FileSize(*File)>1024,TEXT("environment PNG saved with rendered content"));
        Finish(Gallery.bPass);
    }
    return true;
}
#endif
