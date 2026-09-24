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
#include "ShaderCompiler.h"
#include "ContentStreaming.h"
#include "CireTownGoal.h"

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
        const auto* Cobble=CireEnvironmentProps::SurfaceMaterial(TEXT("cobblestone_material"),TEXT("/Game/Environment/Town/Materials/MI_TownW_Cobble.MI_TownW_Cobble"));
        Check(Road && Road->GetStaticMesh() && Road->GetInstanceCount()>0 && Material && Material==Cobble,TEXT("route paving has geometry and the town cobblestone material slot"));
        Check(Battlefield->RenderedRouteRevision==CireLanePath::Revision(World),TEXT("rendered road uses current world route revision"));
        Check(CireEnvironmentProps::InstanceCount(Battlefield)>200,TEXT("medieval town layout placed in both realms"));
        Check(CireEnvironmentProps::LightCount(Battlefield)>=20,TEXT("street lamps and braziers light the town"));
        Check(CireEnvironmentProps::HasSafeClearance(Battlefield),TEXT("town footprints preserve route, challenge bay, breach spawn and realm divider clearance"));
        int32 Landmarks=0;
        for(const auto* C:Battlefield->GetComponents())if(const auto* H=Cast<UInstancedStaticMeshComponent>(C))
            for(const TCHAR* Slot:{TEXT("castle_gate"),TEXT("castle_keep"),TEXT("gatehouse"),TEXT("shrine"),TEXT("market_hall")})
                if(H->ComponentHasTag(FName(Slot))&&H->GetInstanceCount()==2)++Landmarks;
        Check(Landmarks>=5,TEXT("castle gate, keep, town gatehouse, square shrine and market hall exist once per realm"));
        for(const TCHAR* Slot:{TEXT("castle_gate"),TEXT("gatehouse"),TEXT("castle_keep"),TEXT("shrine"),TEXT("house_a"),TEXT("market_stall_a"),TEXT("cobblestone_material"),TEXT("plaster_material")})
            UE_LOG(LogCireEnvironmentGallery,Display,TEXT("CIRE_TOWN_SLOT slot=%s source=%s"),Slot,*CireEnvironmentProps::SlotSource(FName(Slot)));
        for(int32 Team=0;Team<2;++Team)
        {
            Check(CireEnvironmentProps::DistrictAt(World,Team,CireLanePath::GoalPosition(World,Team))==FName(TEXT("castle")),TEXT("defended leak zone lies in the castle district"));
            Check(CireEnvironmentProps::DistrictAt(World,Team,CireLanePath::SpawnPosition(World,Team))==FName(TEXT("breach")),TEXT("monsters spawn in the breach fields outside the town gate"));
            Check(FMath::IsNearlyEqual(CireLanePath::RouteProgress(World,Team,CireLanePath::PointAlongRoute(World,Team,.5f)),.5f,.01f),TEXT("route progress API round-trips"));
        }
        int32 Goals=0;
        for(TActorIterator<ACireTownGoal> It(World);It;++It)
            if(It->ContainsLocation(CireLanePath::GoalPosition(World,It->TeamId,150))&&!It->ContainsLocation(CireLanePath::PointAlongRoute(World,It->TeamId,.97f,150)))++Goals;
        Check(Goals==2,TEXT("each castle-gate leak zone contains its route end but not the approach"));
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
    UE_LOG(LogCireEnvironmentGallery,Display,TEXT("CIRE_ENVIRONMENT_GALLERY_READY views=%d route_clearance=45x90 medieval_town=1"),7);return true;
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
    if(FPlatformTime::Seconds()-Gallery.Started>330){Fail(TEXT("gallery exceeded 330 seconds waiting for setup, shaders or captures"));Finish(false);return true;}
    if(Gallery.Ready<0)
    {
        auto* Controller=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(Controller && Controller->GetPawn() && Controller->GetHUD() && !Build(Mode,Controller))Finish(false);
        return true;
    }
    if(!Gallery.Camera.IsValid() || !Gallery.Controller.IsValid() || !Gallery.Escort.IsValid() || !Gallery.Label.IsValid())
    {Fail(TEXT("a required gallery actor/component disappeared"));Finish(false);return true;}
    // Hold the clock while shaders compile so captures never show placeholder materials.
    if(GShaderCompilingManager&&GShaderCompilingManager->GetNumRemainingJobs()>0&&FPlatformTime::Seconds()-Gallery.Started<240)
    {Gallery.Ready=FPlatformTime::Seconds();return true;}
    constexpr int32 Views=7;
    const double Age=FPlatformTime::Seconds()-Gallery.Ready;const int32 Stage=FMath::Clamp(FMath::FloorToInt((Age-8)/5),0,Views-1);
    const auto& Routes=CireLanePath::Get(Mode->GetWorld());FVector Target,View;const float Y=CireLanePath::CenterY(0);
    const TCHAR* Titles[]={TEXT("01  TOWN GATE / BREACH FIELDS"),TEXT("02  MARKET DISTRICT"),TEXT("03  COOPER'S LANES / RESIDENTIAL"),
        TEXT("04  TOWN SQUARE"),TEXT("05  CASTLE GATE / DEFENDED LEAK ZONE"),TEXT("06  PRIVATE REALM / FULL TOWN OVERVIEW"),TEXT("07  ARMORED ESCORT ON THE MARCH ROAD")};
    switch(Stage)
    {
    case 0: Target=FVector(11200,Y,560);View=FVector(14700,Y+1350,980);break;
    case 1: Target=FVector(6900,Y+150,120);View=FVector(8900,Y-950,820);break;
    case 2: Target=FVector(3900,Y-350,260);View=FVector(5750,Y+500,720);break;
    case 3: Target=FVector(1650,Y-150,230);View=FVector(3500,Y+1000,900);break;
    case 4: Target=FVector(-1150,Y,820);View=FVector(1650,Y+750,760);break;
    case 5: Target=FVector(6200,Y,0);View=FVector(-5200,Y-3400,6200);break;
    default:
        if(!Gallery.bEscortMoving){Gallery.Escort->SetActorTickEnabled(true);Gallery.bEscortMoving=true;}
        Target=Gallery.Escort->GetActorLocation()+FVector(0,0,30);View=Target+FVector(-530,-550,240);
    }
    Gallery.Camera->SetActorLocation(View);Gallery.Camera->SetActorRotation((Target-View).Rotation());
    Gallery.Label->SetText(FText::FromString(Titles[Stage]));
    if(Age>=7+Stage*5&&Age<7.3+Stage*5)IStreamingManager::Get().StreamAllResources(1.f);
    if(Age>=10+Stage*5 && Stage>Gallery.CapturedStage)
    {
        const TCHAR* Names[]={TEXT("01_gate.png"),TEXT("02_market.png"),TEXT("03_residential.png"),TEXT("04_square.png"),TEXT("05_castle.png"),TEXT("06_overview.png"),TEXT("07_armored_escort.png")};
        const FString File=FPaths::Combine(Gallery.Directory,Names[Stage]);FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);
        Gallery.Captures.Add(File);Gallery.CapturedStage=Stage;
        if(Stage==Views-1)
        {
            FVector2D Pixel;int32 Width=0,Height=0;Gallery.Controller->GetViewportSize(Width,Height);
            Check(!Gallery.Escort->IsHidden() && Gallery.Controller->ProjectWorldLocationToScreen(Gallery.Escort->GetActorLocation(),Pixel) && Pixel.X>0 && Pixel.X<Width && Pixel.Y>0 && Pixel.Y<Height,TEXT("escort is visible and framed in the final capture"));
        }
        UE_LOG(LogCireEnvironmentGallery,Display,TEXT("CIRE_ENVIRONMENT_GALLERY_CAPTURE stage=%d file=%s"),Stage,*File);
    }
    if(Age>=14+Views*5)
    {
        Check(Gallery.Captures.Num()==Views,TEXT("all environment views captured"));
        for(const FString& File:Gallery.Captures)Check(IFileManager::Get().FileSize(*File)>1024,TEXT("environment PNG saved with rendered content"));
        Finish(Gallery.bPass);
    }
    return true;
}
#endif
