#include "CireDraftStage.h"
#include "HAL/IConsoleManager.h"
#include "CireGame.h"
#include "CireChampionArt.h"
#include "CireChampionRoster.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Materials/MaterialInterface.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireDraftStage,Log,All);

namespace
{
// Far above the battlefield: stage lights (bounded attenuation) never reach the
// map, and the capture renders only its show-only list.
const FVector StageOrigin(0.f,0.f,90000.f);
constexpr int32 PreviewWidth=720,PreviewHeight=960;
constexpr float CaptureFov=30.f;

UStaticMesh* Mesh(const TCHAR* Path){return LoadObject<UStaticMesh>(nullptr,Path);}
UMaterialInterface* Material(const TCHAR* Path){return LoadObject<UMaterialInterface>(nullptr,Path);}

UStaticMeshComponent* Prop(AActor* Owner,const TCHAR* Name,UStaticMesh* StaticMesh,UMaterialInterface* Override,bool bShadows=true)
{
    auto* C=NewObject<UStaticMeshComponent>(Owner,Name);
    C->SetupAttachment(Owner->GetRootComponent());
    C->SetStaticMesh(StaticMesh);
    if(Override)for(int32 I=0;I<FMath::Max(1,C->GetNumMaterials());++I)C->SetMaterial(I,Override);
    C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    C->SetCastShadow(bShadows);C->SetMobility(EComponentMobility::Movable);
    C->RegisterComponent();Owner->AddInstanceComponent(C);
    return C;
}
template<typename T>
T* Light(AActor* Owner,const TCHAR* Name,FLinearColor Color,float Lumens,float Radius,bool bShadows)
{
    auto* L=NewObject<T>(Owner,Name);
    L->SetupAttachment(Owner->GetRootComponent());L->SetMobility(EComponentMobility::Movable);
    L->SetIntensityUnits(ELightUnits::Lumens);L->SetIntensity(Lumens);L->SetLightColor(Color);
    L->SetAttenuationRadius(Radius);L->SetCastShadows(bShadows);
    L->RegisterComponent();Owner->AddInstanceComponent(L);
    return L;
}
// Uniform scale that makes a mesh StaticMesh roughly TargetHeight tall (Z extent).
float HeightScale(const UStaticMeshComponent* C,float TargetHeight)
{
    const UStaticMesh* M=C?C->GetStaticMesh():nullptr;if(!M)return 1.f;
    const float Height=M->GetBounds().BoxExtent.Z*2.f;
    return Height>1.f?TargetHeight/Height:1.f;
}
double ServerNow(const UWorld* World)
{
    const AGameStateBase* State=World?World->GetGameState():nullptr;
    return State?State->GetServerWorldTimeSeconds():(World?World->GetTimeSeconds():0.0);
}
}

ACireDraftStage::ACireDraftStage()
{
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.bTickEvenWhenPaused=true;
    bReplicates=false;
    SetCanBeDamaged(false);
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("StageRoot"));
    RootComponent->SetMobility(EComponentMobility::Movable);
}

ACireDraftStage* ACireDraftStage::SpawnStage(UWorld* World)
{
    if(!World||World->GetNetMode()==NM_DedicatedServer)return nullptr;
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.ObjectFlags|=RF_Transient;
    auto* Stage=World->SpawnActor<ACireDraftStage>(ACireDraftStage::StaticClass(),FTransform(StageOrigin),P);
    if(Stage)Stage->BuildStage();
    return Stage;
}

void ACireDraftStage::BuildStage()
{
    Target=NewObject<UTextureRenderTarget2D>(this,TEXT("DraftPreviewTarget"));
    Target->RenderTargetFormat=ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
    Target->ClearColor=FLinearColor(.006f,.007f,.009f,1);
    Target->InitAutoFormat(PreviewWidth,PreviewHeight);
    Target->UpdateResourceImmediate(true);

    Capture=NewObject<USceneCaptureComponent2D>(this,TEXT("DraftCapture"));
    Capture->SetupAttachment(RootComponent);
    Capture->TextureTarget=Target;
    Capture->CaptureSource=ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->bCaptureEveryFrame=true;Capture->bCaptureOnMovement=false;Capture->bAlwaysPersistRenderingState=true;
    Capture->FOVAngle=CaptureFov;
    // Only the stage's own lights: the map's sun, sky, fog and atmosphere never leak in.
    Capture->ShowFlags.SetFog(false);Capture->ShowFlags.SetVolumetricFog(false);Capture->ShowFlags.SetAtmosphere(false);
    Capture->ShowFlags.SetCloud(false);Capture->ShowFlags.SetDirectionalLights(false);Capture->ShowFlags.SetSkyLighting(false);
    Capture->ShowFlags.SetMotionBlur(false);Capture->ShowFlags.SetLensFlares(false);
    // Crisp stills: no temporal AA (no history smear or ghosting while the champion idles);
    // the draft screen supersamples the target instead (SetPreviewHeight).
    Capture->ShowFlags.SetTemporalAA(false);
    auto& PP=Capture->PostProcessSettings;Capture->PostProcessBlendWeight=1.f;
    PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=-2.1f;
    PP.bOverride_DynamicGlobalIlluminationMethod=true;PP.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
    PP.bOverride_ReflectionMethod=true;PP.ReflectionMethod=EReflectionMethod::ScreenSpace;
    PP.bOverride_BloomIntensity=true;PP.BloomIntensity=.18f;
    PP.bOverride_VignetteIntensity=true;PP.VignetteIntensity=.55f;
    PP.bOverride_MotionBlurAmount=true;PP.MotionBlurAmount=0.f;
    // Full-strength colour and a touch of contrast so PBR materials read true, not flat.
    PP.bOverride_ColorSaturation=true;PP.ColorSaturation=FVector4(1.06f,1.06f,1.06f,1.f);
    PP.bOverride_ColorContrast=true;PP.ColorContrast=FVector4(1.08f,1.08f,1.08f,1.f);
    PP.bOverride_SceneFringeIntensity=true;PP.SceneFringeIntensity=0.f;
    Capture->RegisterComponent();AddInstanceComponent(Capture);
    Capture->ShowOnlyActors.Add(this);
    // Cutout depth: same camera, scene depth only (see SetCutout / GetDepthTarget).
    DepthTarget=NewObject<UTextureRenderTarget2D>(this,TEXT("DraftDepthTarget"));
    DepthTarget->RenderTargetFormat=ETextureRenderTargetFormat::RTF_R32f;DepthTarget->ClearColor=FLinearColor(1e6f,0,0,0);
    DepthTarget->InitAutoFormat(PreviewWidth,PreviewHeight);DepthTarget->UpdateResourceImmediate(true);
    DepthCapture=NewObject<USceneCaptureComponent2D>(this,TEXT("DraftDepthCapture"));
    DepthCapture->SetupAttachment(Capture);
    DepthCapture->TextureTarget=DepthTarget;DepthCapture->CaptureSource=ESceneCaptureSource::SCS_SceneDepth;
    DepthCapture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    DepthCapture->bCaptureEveryFrame=false;DepthCapture->bCaptureOnMovement=false;DepthCapture->FOVAngle=CaptureFov;
    DepthCapture->ShowFlags=Capture->ShowFlags;
    DepthCapture->RegisterComponent();AddInstanceComponent(DepthCapture);

    UStaticMesh* Cylinder=Mesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Cube=Mesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Sphere=Mesh(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    Floor=Prop(this,TEXT("StageFloor"),Cylinder,Material(TEXT("/Game/Art/Environment/Materials/M_BasaltRoad.M_BasaltRoad")));
    Dais=Prop(this,TEXT("StageDais"),Cylinder,Material(TEXT("/Game/Art/Environment/Materials/M_AshenMasonry.M_AshenMasonry")));
    Wall=Prop(this,TEXT("StageWall"),Cube,Material(TEXT("/Game/Art/Environment/Materials/M_AshenMasonry.M_AshenMasonry")));
    Arch=Prop(this,TEXT("StageArch"),Mesh(TEXT("/Game/Art/Environment/Props01/SM_VoussoirArch.SM_VoussoirArch")),nullptr);
    UStaticMesh* BrazierMesh=Mesh(TEXT("/Game/Art/Environment/Props01/SM_ForgedBrazier.SM_ForgedBrazier"));
    UMaterialInterface* Ember=Material(TEXT("/Game/Art/Materials/M_Ember.M_Ember"));
    for(int32 I=0;I<2;++I)
    {
        Braziers.Add(Prop(this,*FString::Printf(TEXT("StageBrazier%d"),I),BrazierMesh,nullptr));
        Embers.Add(Prop(this,*FString::Printf(TEXT("StageEmber%d"),I),Sphere,Ember,false));
        FireLights.Add(Light<UPointLightComponent>(this,*FString::Printf(TEXT("StageFire%d"),I),FLinearColor(1.f,.42f,.12f),2600.f,900.f,false));
    }
    KeyLight=Light<USpotLightComponent>(this,TEXT("StageKey"),FLinearColor(1.f,.90f,.78f),5200.f,2600.f,true);
    RimLight=Light<USpotLightComponent>(this,TEXT("StageRim"),FLinearColor(.52f,.68f,1.f),11000.f,2600.f,false);
    FillLight=Light<UPointLightComponent>(this,TEXT("StageFill"),FLinearColor(.55f,.60f,.72f),450.f,2400.f,false);
    Cast<USpotLightComponent>(KeyLight)->SetOuterConeAngle(26.f);Cast<USpotLightComponent>(KeyLight)->SetInnerConeAngle(10.f);
    Cast<USpotLightComponent>(RimLight)->SetOuterConeAngle(30.f);Cast<USpotLightComponent>(RimLight)->SetInnerConeAngle(10.f);
    FitStage(185.f);
}

void ACireDraftStage::FitStage(float Height)
{
    BodyHeight=FMath::Clamp(Height,60.f,450.f);
    const float S=StageScale=FMath::Max(1.f,BodyHeight/190.f);
    Floor->SetRelativeLocation(FVector(0,0,-5.f));Floor->SetRelativeScale3D(FVector(11.f*S,11.f*S,.1f));
    Dais->SetRelativeLocation(FVector(0,0,7.f*S));Dais->SetRelativeScale3D(FVector(2.4f*S,2.4f*S,.14f*S));
    Wall->SetRelativeLocation(FVector(-760.f*S,0,480.f*S));Wall->SetRelativeScale3D(FVector(.4f,20.f*S,9.6f*S));
    if(Arch&&Arch->GetStaticMesh())
    {
        const FVector Extent=Arch->GetStaticMesh()->GetBounds().BoxExtent;
        const float Scale=HeightScale(Arch,FMath::Max(380.f,BodyHeight*1.85f));
        Arch->SetRelativeRotation(FRotator(0,Extent.X>Extent.Y?90.f:0.f,0));
        Arch->SetRelativeScale3D(FVector(Scale));
        const FVector Origin=Arch->GetStaticMesh()->GetBounds().Origin;
        Arch->SetRelativeLocation(FVector(-330.f*S,0,-(Origin.Z-Extent.Z)*Scale));
    }
    for(int32 I=0;I<Braziers.Num();++I)
    {
        const float Side=I==0?-1.f:1.f;auto* B=Braziers[I].Get();
        const float Scale=HeightScale(B,105.f*S);B->SetRelativeScale3D(FVector(Scale));
        float Bottom=0,Top=105.f*S;
        if(B->GetStaticMesh()){const auto Bounds=B->GetStaticMesh()->GetBounds();Bottom=(Bounds.Origin.Z-Bounds.BoxExtent.Z)*Scale;Top=(Bounds.Origin.Z+Bounds.BoxExtent.Z)*Scale-Bottom;}
        const FVector Base(-170.f*S,Side*235.f*S,0);
        B->SetRelativeLocation(Base-FVector(0,0,Bottom));
        Embers[I]->SetRelativeLocation(Base+FVector(0,0,Top-6.f*S));Embers[I]->SetRelativeScale3D(FVector(.26f*S,.26f*S,.14f*S));
        FireLights[I]->SetRelativeLocation(Base+FVector(0,0,Top+35.f*S));FireLights[I]->SetAttenuationRadius(900.f*S);
    }
    const FVector Chest(0,0,BodyHeight*.70f+14.f*S);
    const auto Aim=[&](ULocalLightComponent* L,const FVector& At){L->SetRelativeLocation(At);L->SetRelativeRotation((Chest-At).Rotation());};
    Aim(KeyLight,FVector(420.f,300.f,BodyHeight+300.f)*FVector(S,S,1));
    Aim(RimLight,FVector(-340.f*S,-230.f*S,BodyHeight+260.f));
    FillLight->SetRelativeLocation(FVector(460.f*S,-360.f*S,BodyHeight*.45f));
    KeyLight->SetAttenuationRadius(1050.f*S);RimLight->SetAttenuationRadius(1000.f*S);FillLight->SetAttenuationRadius(1100.f*S);
}

void ACireDraftStage::DestroyPreview()
{
    if(IsValid(Preview))Preview->Destroy();
    Preview=nullptr;bFramed=false;
    if(Capture)Capture->ShowOnlyActors.Remove(nullptr);
}

void ACireDraftStage::ShowProfile(const FString& Id)
{
    if(Id==ProfileId&&(Id.IsEmpty()||IsValid(Preview)))return;
    if(Capture)Capture->ShowOnlyActors.Remove(Preview);
    DestroyPreview();ProfileId=Id;ShownAt=FPlatformTime::Seconds();ShownFrame=GFrameCounter;LastAttackAt=ShownAt;Yaw=bSpin?-28.f:Yaw;
    if(Id.IsEmpty()||!CireChampionRoster::Find(Id))return;
    UWorld* World=GetWorld();if(!World)return;
    FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    P.bDeferConstruction=true;P.ObjectFlags|=RF_Transient;
    const FTransform Spawn(FRotator(0,Yaw,0),StageOrigin+FVector(0,0,200.f));
    auto* Hero=World->SpawnActor<ACireHero>(ACireHero::StaticClass(),Spawn,P);
    if(!Hero)return;
    // Local presentation only: never replicated, never on a team, never ticking gameplay.
    Hero->SetReplicates(false);Hero->TeamId=-1;Hero->bBot=false;Hero->bAutoAttack=false;
    Hero->FinishSpawning(Spawn);
    Hero->SetActorTickEnabled(false);Hero->SetActorEnableCollision(false);Hero->SetCanBeDamaged(false);
    Hero->GetCharacterMovement()->StopMovementImmediately();Hero->GetCharacterMovement()->DisableMovement();
    Hero->GetCharacterMovement()->SetComponentTickEnabled(false);
    // A locally spawned actor has local authority, so the real draft path binds
    // exactly the same profile snapshot (body, weapons, scale, stats) as in play.
    if(!Hero->DraftProfile(Id)){Hero->Destroy();UE_LOG(LogCireDraftStage,Warning,TEXT("Draft preview could not bind %s"),*Id);return;}
    Hero->Notice.Reset();
    Hero->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Hero->ChampionArt->UpdateVisuals(*Hero,.016f);
    Preview=Hero;Capture->ShowOnlyActors.AddUnique(Hero);
    RefreshCutoutParts();
    SetExposureOffset(StoredExposure(Id));
    // Ask the streamer for full-resolution body/weapon textures immediately (the preview is a close-up).
    Hero->PrestreamTextures(20.f,true);
    // Stand on the dais: the capsule bottom sits on its top face.
    const float Half=Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    Hero->SetActorLocation(StageOrigin+FVector(0,0,14.f*StageScale+Half));
    bFramed=false;
}

FBox ACireDraftStage::BodyBounds() const
{
    FBox Box(ForceInit);
    if(!IsValid(Preview))return Box;
    TArray<UPrimitiveComponent*> Parts;Preview->GetComponents(Parts);
    for(const UPrimitiveComponent* C:Parts)
    {
        if(!C||!C->IsRegistered()||!C->IsVisible()||C->bHiddenInGame||C->IsA<UCapsuleComponent>())continue;
        if(const auto* Skeletal=Cast<USkeletalMeshComponent>(C);Skeletal&&!Skeletal->GetSkeletalMeshAsset())continue;
        const FBox Part=C->Bounds.GetBox();
        if(Part.IsValid&&Part.GetExtent().GetMax()<1000.f)Box+=Part;
    }
    return Box;
}

bool ACireDraftStage::IsPreviewReady() const {return IsValid(Preview)&&bFramed&&SecondsShown()>.35f&&FramesShown()>6;}
float ACireDraftStage::SecondsShown() const {return ProfileId.IsEmpty()?0.f:static_cast<float>(FPlatformTime::Seconds()-ShownAt);}
float ACireDraftStage::StoredExposure(const FString& Id)
{
    static TMap<FString,float> Table;static bool bLoaded=false;
    if(!bLoaded)
    {
        bLoaded=true;FString Json;TSharedPtr<FJsonObject> Root;
        if(FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("UI/Draft/Portraits/Exposure.json")))&&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)&&Root.IsValid())
            for(const auto& Pair:Root->Values){double V=0;if(Pair.Value->TryGetNumber(V)&&FMath::IsFinite(V))Table.Add(FString(Pair.Key),FMath::Clamp(static_cast<float>(V),-3.f,2.f));}
    }
    const float* Found=Table.Find(Id);return Found?*Found:0.f;
}
void ACireDraftStage::SetExposureOffset(float Stops)
{
    ExposureOffset=FMath::Clamp(Stops,-3.f,2.f);
    if(Capture){Capture->PostProcessSettings.AutoExposureBias=-2.1f+ExposureOffset;}
}
void ACireDraftStage::SetTurntable(bool bInSpin,float FixedYaw){bSpin=bInSpin;if(!bSpin)Yaw=FixedYaw;}

void ACireDraftStage::FrameCamera(float DeltaSeconds,bool bSnap)
{
    const FBox Box=BodyBounds();if(!Box.IsValid)return;
    const float FloorZ=StageOrigin.Z+14.f*StageScale;
    // Humanoid rigs: the head bone gives a much tighter top than padded skeletal bounds.
    FVector Head=FVector::ZeroVector;bool bHead=false;
    USkeletalMeshComponent* HeadBody=Preview->GetMesh();
    {   // new-champions: a mounted champion (Huntress) is framed on its rider, not on the mount's head.
        TArray<USkeletalMeshComponent*> Parts;Preview->GetComponents(Parts);
        for(USkeletalMeshComponent* Part:Parts)if(Part&&Part->GetFName()==TEXT("MountedRider")&&Part->GetSkeletalMeshAsset())HeadBody=Part;
    }
    if(USkeletalMeshComponent* Body=HeadBody;Body&&Body->GetSkeletalMeshAsset())
        for(const TCHAR* Bone:{TEXT("head"),TEXT("Head"),TEXT("neck_01")})
            if(Body->GetBoneIndex(Bone)!=INDEX_NONE){Head=Body->GetBoneLocation(Bone);bHead=Head.Z>FloorZ+30.f;break;}
    float Top=FMath::Max(Box.Max.Z,FloorZ+40.f);
    // Creature rigs without a head bone (bear): bone positions are far tighter than padded bounds.
    if(USkeletalMeshComponent* Body=Preview->GetMesh();!bHead&&Body&&Body->GetSkeletalMeshAsset()&&Body->GetNumBones()>4)
    {
        float BoneTop=-MAX_flt;for(int32 I=0;I<Body->GetNumBones();++I)BoneTop=FMath::Max(BoneTop,static_cast<float>(Body->GetBoneTransform(I).GetLocation().Z));
        if(BoneTop>FloorZ+30.f)Top=FMath::Min(Top,FloorZ+(BoneTop-FloorZ)*1.15f);
    }
    if(bHead)
    {
        const float HeadHeight=Head.Z-FloorZ;Top=FloorZ+HeadHeight*1.13f;
        // Weapons are static meshes with tight bounds: keep a raised staff, lance or totem in frame.
        TArray<UStaticMeshComponent*> Parts;Preview->GetComponents(Parts);
        for(const UStaticMeshComponent* Part:Parts)if(Part&&Part->IsVisible()&&Part->GetStaticMesh())
            Top=FMath::Max(Top,FMath::Min(Part->Bounds.GetBox().Max.Z,FloorZ+HeadHeight*1.5f));
    }
    const float Height=Top-FloorZ;
    if(!bFramed||FMath::Abs(Height-BodyHeight)>BodyHeight*.2f)FitStage(Height);
    // Rotation-invariant horizontal radius around the actor, so the turntable never pumps the zoom.
    const FVector2D Center(Preview->GetActorLocation());
    float Radius=0;for(const FVector& Corner:{Box.Min,Box.Max,FVector(Box.Min.X,Box.Max.Y,0),FVector(Box.Max.X,Box.Min.Y,0)})
        Radius=FMath::Max(Radius,FVector2D::Distance(Center,FVector2D(Corner)));
    const float TanH=FMath::Tan(FMath::DegreesToRadians(CaptureFov*.5f)),TanV=TanH*PreviewHeight/PreviewWidth;
    if(PortraitTarget)
    {
        // Head-and-shoulders bust. Without a head bone (spirits, creatures) frame the upper body.
        float HalfBust;
        if(bHead){const float HeadHeight=Head.Z-FloorZ;HalfBust=FMath::Max(HeadHeight*(HeadHeight>200.f?.25f:.2f),27.f);CameraFocus=FVector(Head.X,Head.Y,Head.Z);}
        else if(Height<160.f&&Preview->GetMesh()->GetSkeletalMeshAsset()){HalfBust=Height*.5f;CameraFocus=FVector(Center.X,Center.Y,FloorZ+Height*.52f);}
        else if(Height<160.f){const float BoxHeight=FMath::Min(Box.Max.Z,Top)-Box.Min.Z;HalfBust=FMath::Max(BoxHeight*.62f,30.f);CameraFocus=FVector(Center.X,Center.Y,Box.Min.Z+BoxHeight*.5f);}
        else {HalfBust=Height*.25f;CameraFocus=FVector(Center.X,Center.Y,Top-Height*.25f);}
        CameraDistance=HalfBust/TanH+(bHead?30.f:Radius*.35f);
        const FVector BustView=CameraFocus-FRotator(-3.f,186.f,0).Vector()*CameraDistance;
        Capture->SetWorldLocationAndRotation(BustView,(CameraFocus-BustView).Rotation());
        bFramed=true;return;
    }
    // Framing is driven by height, capped horizontally, with room for long bodies (centaur, behemoth).
    const float HalfV=Height*.5f*1.10f+14.f;
    const float Reach=FMath::Min(Radius,Height*.42f+20.f);
    const float Distance=FMath::Max(HalfV/TanV,Reach/TanH)+Reach*.3f;
    const FVector Focus(Center.X,Center.Y,FloorZ+Height*.47f);
    const float Alpha=bSnap||!bFramed?1.f:FMath::Clamp(DeltaSeconds*3.f,0.f,1.f);
    CameraFocus=FMath::Lerp(CameraFocus,Focus,Alpha);CameraDistance=FMath::Lerp(CameraDistance,Distance,Alpha);
    // Camera sits in front of the champion (+X), a little to its left and above, looking back at it.
    const FVector View=CameraFocus-FRotator(-7.f,188.f,0).Vector()*CameraDistance;
    Capture->SetWorldLocationAndRotation(View,(CameraFocus-View).Rotation());
    bFramed=true;
}

void ACireDraftStage::SetPortraitTarget(UTextureRenderTarget2D* Into)
{
    PortraitTarget=Into;
    if(Capture)Capture->TextureTarget=Into?Into:Target.Get();
    // Busts read best as clean silhouettes: hide the architecture, keep the warm fire rim.
    const bool bProps=Into==nullptr&&!bCutout;
    for(UStaticMeshComponent* Part:{Wall.Get(),Arch.Get(),Floor.Get(),Dais.Get()})if(Part){Part->SetVisibility(bProps);Part->SetHiddenInGame(!bProps);}
    for(auto& Part:Braziers)if(Part){Part->SetVisibility(bProps);Part->SetHiddenInGame(!bProps);}
    for(auto& Part:Embers)if(Part){Part->SetVisibility(bProps);Part->SetHiddenInGame(!bProps);}
    RefreshCutoutParts();
    bFramed=false;
}

namespace
{
// Post-process alpha is a renderer-wide switch: on while any cutout stage lives, then restored.
int32 GCutoutStages=0;bool GPropagateAlphaBefore=false;
void RetainPropagateAlpha(bool bRetain)
{
    IConsoleVariable* CVar=IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessing.PropagateAlpha"));
    if(!CVar)return;
    if(bRetain){if(GCutoutStages++==0){GPropagateAlphaBefore=CVar->GetBool();CVar->Set(true,ECVF_SetByCode);}}
    else if(GCutoutStages>0&&--GCutoutStages==0)CVar->Set(GPropagateAlphaBefore,ECVF_SetByCode);
}
}
void ACireDraftStage::RefreshCutoutParts()
{
    // Cutout: only the champion's meshes (body, weapons, gear) reach the capture, never
    // helper geometry such as ground auras or rings the hero may carry.
    if(!Capture)return;
    Capture->ShowOnlyComponents.Reset();
    if(DepthCapture){DepthCapture->ShowOnlyComponents.Reset();DepthCapture->bCaptureEveryFrame=bCutout&&!PortraitTarget&&IsValid(Preview);}
    if(!IsValid(Preview)){Capture->ShowOnlyActors.AddUnique(this);return;}
    if(!bCutout||PortraitTarget){Capture->ShowOnlyActors.AddUnique(this);Capture->ShowOnlyActors.AddUnique(Preview);return;}
    // Lights are not filtered by the show-only list; the stage's own meshes are left out entirely.
    Capture->ShowOnlyActors.Remove(Preview);Capture->ShowOnlyActors.Remove(this);
    TArray<UPrimitiveComponent*> Parts;Preview->GetComponents(Parts);
    for(UPrimitiveComponent* Part:Parts)
    {
        bool bBody=Part->IsA<USkeletalMeshComponent>();
        for(const USceneComponent* Up=Part->GetAttachParent();Up&&!bBody;Up=Up->GetAttachParent())bBody=Up->IsA<USkeletalMeshComponent>();
        if(bBody&&(Part->IsA<USkeletalMeshComponent>()||Part->IsA<UStaticMeshComponent>())){Capture->ShowOnlyComponents.Add(Part);if(DepthCapture)DepthCapture->ShowOnlyComponents.Add(Part);}
    }
}
void ACireDraftStage::SetPreviewHeight(int32 Pixels)
{
    Pixels=FMath::Clamp(Pixels,960,2560)&~7;
    if(!Target||FMath::Abs(static_cast<int32>(Target->SizeY)-Pixels)<Pixels/8)return;
    const int32 W=(Pixels*3/4)&~7;
    Target->InitAutoFormat(W,Pixels);Target->UpdateResourceImmediate(true);
    if(DepthTarget){DepthTarget->InitAutoFormat(W,Pixels);DepthTarget->UpdateResourceImmediate(true);}
}
void ACireDraftStage::SetMood(const FLinearColor& Key,const FLinearColor& Rim,const FLinearColor& Fill)
{
    if(KeyLight)KeyLight->SetLightColor(Key);
    if(RimLight)RimLight->SetLightColor(Rim);
    if(FillLight)FillLight->SetLightColor(Fill);
}
void ACireDraftStage::SetCutout(bool bEnable)
{
    if(bCutout==bEnable)return;
    bCutout=bEnable;RetainPropagateAlpha(bEnable);
    if(Target){Target->ClearColor=bEnable?FLinearColor(0,0,0,0):FLinearColor(.006f,.007f,.009f,1);Target->UpdateResourceImmediate(true);}
    if(Capture){Capture->PostProcessSettings.bOverride_VignetteIntensity=true;Capture->PostProcessSettings.VignetteIntensity=bEnable?0.f:.55f;}
    SetPortraitTarget(PortraitTarget);
}

void ACireDraftStage::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Frame-based so a long asset-load hitch never tears down a live draft screen.
    if(LastTouchedFrame>0&&GFrameCounter>LastTouchedFrame+4){Destroy();return;}
    if(!IsValid(Preview)){if(Capture)Capture->bCaptureEveryFrame=false;if(DepthCapture)DepthCapture->bCaptureEveryFrame=false;return;}
    Capture->bCaptureEveryFrame=true;
    if(bSpin)Yaw=FMath::Fmod(Yaw+DeltaSeconds*11.f+360.f,360.f);
    Preview->SetActorRotation(FRotator(0,Yaw,0));
    // Occasional weapon flourish so the preview reads as the unit's real attack style.
    const double Now=FPlatformTime::Seconds();
    if(bSpin&&SecondsShown()>1.4f&&Now-LastAttackAt>7.0)
    {
        LastAttackAt=Now;Preview->AttackSerial=Preview->AttackSerial+1;Preview->AttackDuration=.65f;
        Preview->AttackStartedServerTime=static_cast<float>(ServerNow(GetWorld()));
        Preview->AttackAimLocation=Preview->GetActorLocation()+Preview->GetActorForwardVector()*600.f;
    }
    Preview->ChampionArt->UpdateVisuals(*Preview,DeltaSeconds);
    if(bCutout&&!PortraitTarget)RefreshCutoutParts(); // weapons and gear attach after the body loads
    FrameCamera(DeltaSeconds,false);
}

void ACireDraftStage::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bCutout){bCutout=false;RetainPropagateAlpha(false);}
    DestroyPreview();
    Super::EndPlay(Reason);
}
