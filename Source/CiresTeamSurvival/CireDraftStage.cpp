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
#include "RenderingThread.h"
#include "Scalability.h"
#include "UnrealEngine.h"
#include "Engine/Texture.h"
#if WITH_EDITOR
#include "ShaderCompiler.h"
#endif
#include "RHIGPUReadback.h"
#include "TextureResource.h"
#include <atomic>

DEFINE_LOG_CATEGORY_STATIC(LogCireDraftStage,Log,All);

// champ-select-hq: async readback of the preview's colour + depth for exposure metering.
struct FDraftMeter
{
    TUniquePtr<FRHIGPUTextureReadback> Color,Depth;
    std::atomic<int32> State{0}; // 0 idle, 1 copy in flight, 2 result ready
    int32 Width=0,Height=0;float MaxDepth=0;
    // video-crash: size the staging textures were created for. FRHIGPUTextureReadback keeps its first staging
    // texture ("assume every enqueue happens on a texture of the same size"), so after the window grew and the
    // preview target with it, copying the bigger target into the old staging texture failed the GPU command list
    // (D3D12 CloseCommandList E_INVALIDARG, a fatal RHI error) and the CPU read overran the mapped buffer.
    int32 CopyWidth=0,CopyHeight=0;
    float Median=0,High=0,Clip=0;int32 Count=0;
};

namespace
{
// Far above the battlefield: stage lights (bounded attenuation) never reach the
// map, and the capture renders only its show-only list.
const FVector StageOrigin(0.f,0.f,90000.f);
constexpr int32 PreviewWidth=720,PreviewHeight=960;
constexpr float CaptureFov=30.f;
// champ-select-hq: manual exposure base (stops). Metering adds a per-champion trim on top.
constexpr float BaseExposureBias=-3.9f;
// Metering targets on the tone-mapped (sRGB byte) luma of the champion's own pixels.
// video-crash: a notch darker than the first tuning (122/238/92): in the real game the Iron Warden's polished plate
// metered to median 97 with 2.5% of its pixels clipped and read as glowing on the dark painted backdrop.
constexpr float MeterTargetMedian=110.f,MeterTargetHigh=232.f,MeterFloorMedian=84.f;
// Clipped share of the figure's pixels above which the highlights win over the mid-tone floor.
constexpr float MeterMaxClipShare=.012f;
// Keyed by profile and the scalability levels it was metered under (shadows, AO, reflections and
// post-processing change how bright the same body renders).
TMap<FString,float>& MeteredExposure(){static TMap<FString,float> Cache;return Cache;}
FString MeterKey(const FString& Id)
{
    const Scalability::FQualityLevels Q=Scalability::GetQualityLevels();
    return FString::Printf(TEXT("%s|%d%d%d%d%d%d"),*Id,Q.ShadowQuality,Q.PostProcessQuality,Q.ReflectionQuality,Q.EffectsQuality,Q.GlobalIlluminationQuality,Q.TextureQuality);
}

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
    ApplyLook();
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
    // champ-select-hq: cinematic three-point rig (+ kicker). Key: soft, warm-neutral, 40 deg off the
    // camera and 35 deg up, the only shadow caster. Fill: broad and cool from the other side at ~1:4 so
    // shadows keep colour instead of going black. Rim + kicker: two back lights in the scene's colour
    // that trace the silhouette and separate the champion from the painted background.
    KeyLight=Light<USpotLightComponent>(this,TEXT("StageKey"),FLinearColor(1.f,.95f,.88f),5200.f,2600.f,true);
    RimLight=Light<USpotLightComponent>(this,TEXT("StageRim"),FLinearColor(.52f,.68f,1.f),9000.f,2600.f,false);
    FillLight=Light<USpotLightComponent>(this,TEXT("StageFill"),FLinearColor(.62f,.68f,.82f),1500.f,2400.f,false);
    KickerLight=Light<USpotLightComponent>(this,TEXT("StageKicker"),FLinearColor(1.f,.72f,.45f),6000.f,2400.f,false);
    Cast<USpotLightComponent>(KeyLight)->SetOuterConeAngle(34.f);Cast<USpotLightComponent>(KeyLight)->SetInnerConeAngle(14.f);
    Cast<USpotLightComponent>(RimLight)->SetOuterConeAngle(30.f);Cast<USpotLightComponent>(RimLight)->SetInnerConeAngle(12.f);
    Cast<USpotLightComponent>(FillLight)->SetOuterConeAngle(44.f);Cast<USpotLightComponent>(FillLight)->SetInnerConeAngle(20.f);
    Cast<USpotLightComponent>(KickerLight)->SetOuterConeAngle(30.f);Cast<USpotLightComponent>(KickerLight)->SetInnerConeAngle(12.f);
    // Soft sources: broad highlights on armour instead of pin-point hot spots, soft shadow edges.
    const auto Soft=[](ULocalLightComponent* L,float Radius){if(auto* P=Cast<UPointLightComponent>(L)){P->SetSourceRadius(Radius);P->SetUseInverseSquaredFalloff(true);}};
    Soft(KeyLight,34.f);Soft(FillLight,80.f);Soft(RimLight,12.f);Soft(KickerLight,12.f);
    FitStage(185.f);
}

void ACireDraftStage::ApplyLook()
{
    // champ-select-hq: fixed manual exposure (no eye adaptation pumping between champions) and a
    // vibrant, contrasty grade: deeper toe so shadows read as shadows, saturation lifted mostly in
    // the mid-tones and highlights so skin, cloth and enamel colours pop without neon shadows.
    if(!Capture)return;
    auto& PP=Capture->PostProcessSettings;Capture->PostProcessBlendWeight=1.f;
    PP.bOverride_AutoExposureMethod=true;PP.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    PP.bOverride_AutoExposureApplyPhysicalCameraExposure=true;PP.AutoExposureApplyPhysicalCameraExposure=false;
    PP.bOverride_AutoExposureBias=true;PP.AutoExposureBias=BaseExposureBias+ExposureOffset;
    PP.bOverride_DynamicGlobalIlluminationMethod=true;PP.DynamicGlobalIlluminationMethod=EDynamicGlobalIlluminationMethod::None;
    PP.bOverride_ReflectionMethod=true;PP.ReflectionMethod=EReflectionMethod::ScreenSpace;
    PP.bOverride_BloomIntensity=true;PP.BloomIntensity=.12f;
    PP.bOverride_BloomThreshold=true;PP.BloomThreshold=1.2f;
    PP.bOverride_VignetteIntensity=true;PP.VignetteIntensity=bCutout?0.f:.55f;
    PP.bOverride_MotionBlurAmount=true;PP.MotionBlurAmount=0.f;
    PP.bOverride_SceneFringeIntensity=true;PP.SceneFringeIntensity=0.f;
    PP.bOverride_FilmToe=true;PP.FilmToe=.60f;
    PP.bOverride_FilmShoulder=true;PP.FilmShoulder=.30f;
    PP.bOverride_ColorSaturation=true;PP.ColorSaturation=FVector4(1.10f,1.10f,1.10f,1.10f);
    PP.bOverride_ColorSaturationMidtones=true;PP.ColorSaturationMidtones=FVector4(1.f,1.f,1.f,1.08f);
    PP.bOverride_ColorSaturationHighlights=true;PP.ColorSaturationHighlights=FVector4(1.f,1.f,1.f,1.12f);
    PP.bOverride_ColorContrast=true;PP.ColorContrast=FVector4(1.f,1.f,1.f,1.10f);
    PP.bOverride_ColorGammaShadows=true;PP.ColorGammaShadows=FVector4(1.f,1.f,1.f,.94f);
    PP.bOverride_AmbientOcclusionIntensity=true;PP.AmbientOcclusionIntensity=.6f;
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
    // The camera looks from +X (slightly +Y). Key camera-right and high, fill camera-left at chest
    // height, rim behind-left high, kicker behind-right low (catches the far edge of the silhouette).
    const float D=FMath::Max(BodyHeight*1.9f,380.f);
    Aim(KeyLight,FVector(D*.72f,D*.62f,BodyHeight*.70f+D*.62f));
    Aim(FillLight,FVector(D*.85f,-D*.72f,BodyHeight*.62f+D*.12f));
    Aim(RimLight,FVector(-D*.78f,-D*.55f,BodyHeight*.70f+D*.55f));
    Aim(KickerLight,FVector(-D*.70f,D*.70f,BodyHeight*.55f+D*.18f));
    for(ULocalLightComponent* L:{KeyLight.Get(),RimLight.Get(),FillLight.Get(),KickerLight.Get()})L->SetAttenuationRadius(D*2.6f);
    // Inverse-square: scale lumens with distance squared so the rig reads the same on a gnome or a behemoth.
    const float K=FMath::Square(D/380.f);
    KeyLight->SetIntensity(5200.f*K);FillLight->SetIntensity(2000.f*K);RimLight->SetIntensity(15000.f*K);KickerLight->SetIntensity(9000.f*K);
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
    // Exposure: the live cutout preview meters itself (cached per champion); portraits and the
    // stage-backdrop fallback keep the stored table.
    bMetered=false;bShownMetered=false;MeterPasses=0;MeterRequestFrame=0;MeterSettleFrame=0;MeterMedian=MeterHigh=0.f;SettledAt=0;MeterClip=0.f;
    MeterQualityKey=MeterKey(Id);
    if(const float* Cached=MeteredExposure().Find(MeterQualityKey);Cached&&bCutout&&!PortraitTarget){SetExposureOffset(*Cached);bMetered=bShownMetered=true;}
    else SetExposureOffset(bCutout&&!PortraitTarget?0.f:StoredExposure(Id));
    // Ask the streamer for full-resolution body/weapon textures immediately (the preview is a close-up);
    // Tick re-arms this while the champion is shown so an idle screen never drops to blurry mips.
    Hero->PrestreamTextures(15.f,true);LastPrestream=FPlatformTime::Seconds();
    ForceTopDetail(Hero);
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
        // champ-select-hq: only what the camera shows (body, weapons, gear on the skeleton), never
        // helper geometry such as ground auras, rings or selection meshes the hero carries.
        bool bBody=C->IsA<USkeletalMeshComponent>();
        for(const USceneComponent* Up=C->GetAttachParent();Up&&!bBody;Up=Up->GetAttachParent())bBody=Up->IsA<USkeletalMeshComponent>();
        if(!bBody||!(C->IsA<USkeletalMeshComponent>()||C->IsA<UStaticMeshComponent>()))continue;
        const FBox Part=C->Bounds.GetBox();
        if(Part.IsValid&&Part.GetExtent().GetMax()<1000.f)Box+=Part;
    }
    return Box;
}

// bShownMetered: once a champion was metered it stays on screen while a preset change re-meters it.
bool ACireDraftStage::IsPreviewReady() const {return IsValid(Preview)&&bFramed&&SecondsShown()>.35f&&FramesShown()>6&&(bMetered||bShownMetered||PortraitTarget||!bCutout);}
bool ACireDraftStage::IsContentSettled() const
{
    // video-crash: meter only the finished look. At startup the preview renders while its meshes build
    // (AssetCompile), shaders compile (default material) and PSOs precache (parts not drawn yet); an exposure
    // metered then was cached for the session and the champion stayed too bright or too dark.
    if(!IsValid(Preview))return false;
#if WITH_EDITOR
    if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())return false;
#endif
    TArray<UPrimitiveComponent*> Parts;Preview->GetComponents(Parts);
    TArray<UTexture*> Textures;
    for(UPrimitiveComponent* Part:Parts)
    {
        if(!Part||!Part->IsRegistered()||!Part->IsVisible())continue;
        if(Part->IsPSOPrecaching())return false;
        if(const auto* Skinned=Cast<USkinnedMeshComponent>(Part);Skinned&&Skinned->GetSkinnedAsset()&&Skinned->GetSkinnedAsset()->IsCompiling())return false;
        if(const auto* Static=Cast<UStaticMeshComponent>(Part);Static&&Static->GetStaticMesh()&&Static->GetStaticMesh()->IsCompiling())return false;
        Textures.Reset();Part->GetUsedTextures(Textures,GetCachedScalabilityCVars().MaterialQualityLevel);
        for(UTexture* T:Textures)if(T&&T->HasPendingInitOrStreaming())return false;
    }
    return true;
}
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
    ExposureOffset=FMath::Clamp(Stops,-4.f,3.f);
    if(Capture){Capture->PostProcessSettings.AutoExposureBias=BaseExposureBias+ExposureOffset;}
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
    // Framing is driven by height; horizontally the camera backs off only as far as the body's real
    // on-screen width needs (box corners projected through the camera, so perspective on a long bear or
    // centaur walking toward the lens is accounted for). The screen shows the middle ~92% x 86%.
    const FRotator ViewRot(-7.f,188.f,0);
    const FVector Fwd=ViewRot.Vector(),Right=FRotationMatrix(ViewRot).GetUnitAxis(EAxis::Y);
    // Skinned bounds are padded (often 2-3x the body): use the bones plus a flesh margin; props
    // (weapons, shields) keep their tight static-mesh bounds.
    FBox Tight(ForceInit);
    {
        TArray<UPrimitiveComponent*> Parts;Preview->GetComponents(Parts);
        for(UPrimitiveComponent* C:Parts)
        {
            if(!C||!C->IsRegistered()||!C->IsVisible()||C->bHiddenInGame)continue;
            bool bBody=C->IsA<USkeletalMeshComponent>();
            for(const USceneComponent* Up=C->GetAttachParent();Up&&!bBody;Up=Up->GetAttachParent())bBody=Up->IsA<USkeletalMeshComponent>();
            if(!bBody)continue;
            if(auto* Sk=Cast<USkeletalMeshComponent>(C))
            {
                if(!Sk->GetSkeletalMeshAsset()||Sk->GetNumBones()<4){if(C->Bounds.BoxExtent.GetMax()<1000.f)Tight+=C->Bounds.GetBox();continue;}
                FBox Bones(ForceInit);for(int32 I=0;I<Sk->GetNumBones();++I)Bones+=Sk->GetBoneTransform(I).GetLocation();
                Tight+=Bones.ExpandBy(FVector(Height*.10f,Height*.10f,0.f));
            }
            else if(C->IsA<UStaticMeshComponent>()&&C->Bounds.BoxExtent.GetMax()<1000.f)Tight+=C->Bounds.GetBox();
        }
    }
    if(!Tight.IsValid)Tight=Box;
    // Nothing above the frame: raised staffs, shields and tall heads (behemoth) are kept in view too.
    const float FitTop=FMath::Max(Top,FMath::Min(Tight.Max.Z+Height*.05f,FloorZ+Height*1.6f));
    const FVector Focus(Center.X,Center.Y,FloorZ+(FitTop-FloorZ)*.47f);
    float Distance=((FitTop-FloorZ)*.5f*1.10f+14.f)/TanV;
    const FVector Up=FRotationMatrix(ViewRot).GetUnitAxis(EAxis::Z);
    for(const float X:{Tight.Min.X,Tight.Max.X})for(const float Y:{Tight.Min.Y,Tight.Max.Y})for(const float Z:{FloorZ,FitTop})
    {
        const FVector P=FVector(X,Y,Z)-Focus;
        Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(P,Right))/(TanH*.86f)-FVector::DotProduct(P,Fwd));
        Distance=FMath::Max(Distance,FMath::Abs(FVector::DotProduct(P,Up))/(TanV*.80f)-FVector::DotProduct(P,Fwd));
    }
    Distance+=10.f;
    const float Alpha=bSnap||!bFramed?1.f:FMath::Clamp(DeltaSeconds*3.f,0.f,1.f);
    CameraFocus=FMath::Lerp(CameraFocus,Focus,Alpha);CameraDistance=FMath::Lerp(CameraDistance,Distance,Alpha);
    // Camera sits in front of the champion (+X), a little to its left and above, looking back at it.
    const FVector View=CameraFocus-FRotator(-7.f,188.f,0).Vector()*CameraDistance;
    Capture->SetWorldLocationAndRotation(View,(CameraFocus-View).Rotation());
    {   // Where the ground under the champion lands in the render (the HUD puts the contact shadow there).
        const FRotator Look=(CameraFocus-View).Rotation();const FMatrix Basis=FRotationMatrix(Look);
        const FVector P=FVector(Center.X,Center.Y,FloorZ)-View;const float Depth=FVector::DotProduct(P,Basis.GetUnitAxis(EAxis::X));
        if(Depth>1.f)FeetV=.5f-FVector::DotProduct(P,Basis.GetUnitAxis(EAxis::Z))/(Depth*TanV)*.5f;
    }
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
    Pixels=FMath::Clamp(Pixels,960,3200)&~7;
    // Grow whenever the screen needs more pixels (never show an upscaled figure); shrink only on big changes.
    const int32 Have=static_cast<int32>(Target?Target->SizeY:0);
    if(!Target||(Pixels<=Have&&Have-Pixels<Pixels/3))return;
    const int32 W=(Pixels*3/4)&~7;
    Target->InitAutoFormat(W,Pixels);Target->UpdateResourceImmediate(true);
    if(DepthTarget){DepthTarget->InitAutoFormat(W,Pixels);DepthTarget->UpdateResourceImmediate(true);}
}
void ACireDraftStage::SetMood(const FLinearColor& Key,const FLinearColor& Rim,const FLinearColor& Fill)
{
    if(KeyLight)KeyLight->SetLightColor(Key);
    if(RimLight)RimLight->SetLightColor(Rim);
    if(FillLight)FillLight->SetLightColor(Fill);
    if(KickerLight)KickerLight->SetLightColor(FMath::Lerp(Rim,Key,.35f));
}
void ACireDraftStage::SetCutout(bool bEnable)
{
    if(bCutout==bEnable)return;
    bCutout=bEnable;RetainPropagateAlpha(bEnable);
    if(Target){Target->ClearColor=bEnable?FLinearColor(0,0,0,0):FLinearColor(.006f,.007f,.009f,1);Target->UpdateResourceImmediate(true);}
    ApplyLook();
    SetPortraitTarget(PortraitTarget);
}

void ACireDraftStage::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    // Frame-based so a long asset-load hitch never tears down a live draft screen.
    if(LastTouchedFrame>0&&GFrameCounter>LastTouchedFrame+4){Destroy();return;}
    if(!IsValid(Preview)){if(Capture)Capture->bCaptureEveryFrame=false;if(DepthCapture)DepthCapture->bCaptureEveryFrame=false;return;}
    const double Now=FPlatformTime::Seconds();
    Capture->bCaptureEveryFrame=true;
    if(bSpin)Yaw=FMath::Fmod(Yaw+DeltaSeconds*11.f+360.f,360.f);
    Preview->SetActorRotation(FRotator(0,Yaw,0));
    // Occasional weapon flourish so the preview reads as the unit's real attack style.
    if(bSpin&&SecondsShown()>1.4f&&Now-LastAttackAt>7.0)
    {
        LastAttackAt=Now;Preview->AttackSerial=Preview->AttackSerial+1;Preview->AttackDuration=.65f;
        Preview->AttackStartedServerTime=static_cast<float>(ServerNow(GetWorld()));
        Preview->AttackAimLocation=Preview->GetActorLocation()+Preview->GetActorForwardVector()*600.f;
    }
    Preview->ChampionArt->UpdateVisuals(*Preview,DeltaSeconds);
    if(bCutout&&!PortraitTarget)RefreshCutoutParts(); // weapons and gear attach after the body loads
    FrameCamera(DeltaSeconds,false);
    if(Now-LastPrestream>5.0){LastPrestream=Now;Preview->PrestreamTextures(15.f,true);ForceTopDetail(Preview);} // gear attaches after spawn
    UpdateMetering();
}

void ACireDraftStage::ForceTopDetail(AActor* Actor)
{
    // Highest-detail geometry regardless of what the (far away) player camera would pick.
    if(!Actor)return;
    TArray<UPrimitiveComponent*> Parts;Actor->GetComponents(Parts);
    for(UPrimitiveComponent* Part:Parts)
    {
        if(auto* Skinned=Cast<USkinnedMeshComponent>(Part)){if(Skinned->GetForcedLOD()!=1)Skinned->SetForcedLOD(1);}
        else if(auto* Static=Cast<UStaticMeshComponent>(Part)){if(Static->ForcedLodModel!=1)Static->SetForcedLodModel(1);}
    }
}

void ACireDraftStage::UpdateMetering()
{
    // A preset change (Options > Video) changes how bright the body renders: meter it again, keep showing it.
    if(bMetered&&bCutout&&!PortraitTarget&&MeterKey(ProfileId)!=MeterQualityKey)
    {
        MeterQualityKey=MeterKey(ProfileId);
        if(const float* Cached=MeteredExposure().Find(MeterQualityKey)){SetExposureOffset(*Cached);return;}
        bMetered=false;MeterPasses=0;MeterSettleFrame=GFrameCounter+4;SettledAt=0;
        UE_LOG(LogCireDraftStage,Log,TEXT("CIRE_DRAFT_METER remeter id=%s key=%s"),*ProfileId,*MeterQualityKey);
    }
    if(bMetered)return;
    if(!bCutout||PortraitTarget||!Target||!DepthTarget||!DepthCapture||!DepthCapture->bCaptureEveryFrame){bMetered=true;return;}
    // Wait (up to 15 s) for the finished look before measuring anything.
    if(!IsContentSettled()&&SecondsShown()<15.f){SettledAt=0;return;}
    const double NowSeconds=FPlatformTime::Seconds();
    if(SettledAt==0){SettledAt=NowSeconds;MeterSettleFrame=FMath::Max(MeterSettleFrame,GFrameCounter+3);}
    // Never hold the preview back for long: a body that cannot be metered shows at its last exposure.
    if(NowSeconds-SettledAt>3.0&&FramesShown()>150)
    {
        bMetered=bShownMetered=true;MeteredExposure().Add(MeterQualityKey,ExposureOffset);
        UE_LOG(LogCireDraftStage,Warning,TEXT("CIRE_DRAFT_METER timeout id=%s offset=%.2f"),*ProfileId,ExposureOffset);return;
    }
    if(!Meter)Meter=MakeShared<FDraftMeter,ESPMode::ThreadSafe>();
    FDraftMeter& M=*Meter;
    const int32 State=M.State.load();
    if(State==0)
    {
        // Measure once the body is framed, animated and has rendered at the current exposure.
        if(!bFramed||FramesShown()<8||SecondsShown()<.25f||GFrameCounter<MeterSettleFrame)return;
        if(Target->SizeX!=DepthTarget->SizeX||Target->SizeY!=DepthTarget->SizeY)return;
        M.Width=Target->SizeX;M.Height=Target->SizeY;M.MaxDepth=GetCutoutMaxDepth();M.State=1;MeterRequestFrame=GFrameCounter;
        ENQUEUE_RENDER_COMMAND(CireDraftMeterCopy)([Shared=Meter,ColorRT=Target.Get(),DepthRT=DepthTarget.Get()](FRHICommandListImmediate& RHICmdList)
        {
            FTextureRenderTargetResource* C=ColorRT->GetRenderTargetResource();FTextureRenderTargetResource* D=DepthRT->GetRenderTargetResource();
            if(!C||!D||!C->GetRenderTargetTexture()||!D->GetRenderTargetTexture()){Shared->Count=0;Shared->State=2;return;}
            const FIntVector CSize=C->GetRenderTargetTexture()->GetSizeXYZ(),DSize=D->GetRenderTargetTexture()->GetSizeXYZ();
            if(CSize.X!=DSize.X||CSize.Y!=DSize.Y){Shared->Count=-1;Shared->State=2;return;} // mid-resize: measure next time
            if(CSize.X!=Shared->CopyWidth||CSize.Y!=Shared->CopyHeight){Shared->Color.Reset();Shared->Depth.Reset();} // new size: new staging
            Shared->CopyWidth=CSize.X;Shared->CopyHeight=CSize.Y;
            if(!Shared->Color)Shared->Color=MakeUnique<FRHIGPUTextureReadback>(TEXT("CireDraftMeterColor"));
            if(!Shared->Depth)Shared->Depth=MakeUnique<FRHIGPUTextureReadback>(TEXT("CireDraftMeterDepth"));
            Shared->Color->EnqueueCopy(RHICmdList,C->GetRenderTargetTexture());
            Shared->Depth->EnqueueCopy(RHICmdList,D->GetRenderTargetTexture());
        });
        return;
    }
    if(State==1)
    {
        if(GFrameCounter>MeterRequestFrame+90){M.State=0;MeterSettleFrame=GFrameCounter+2;UE_LOG(LogCireDraftStage,Log,TEXT("CIRE_DRAFT_METER retry id=%s"),*ProfileId);return;} // lost readback: retry
        ENQUEUE_RENDER_COMMAND(CireDraftMeterRead)([Shared=Meter](FRHICommandListImmediate&)
        {
            FDraftMeter& R=*Shared;
            if(R.State.load()!=1||!R.Color||!R.Depth||!R.Color->IsReady()||!R.Depth->IsReady())return;
            int32 CPitch=0,DPitch=0;
            const FColor* C=static_cast<const FColor*>(R.Color->Lock(CPitch));
            const float* D=static_cast<const float*>(R.Depth->Lock(DPitch));
            uint32 Histogram[256]={};int64 Count=0;
            const int32 W=R.CopyWidth,H=R.CopyHeight; // what was copied, not what the game thread asked for
            if(C&&D&&W>0&&H>0&&CPitch>=W&&DPitch>=W)
                for(int32 Y=0;Y<H;Y+=2)for(int32 X=0;X<W;X+=2)
                {
                    const FColor& P=C[Y*CPitch+X];
                    // Champion pixels only: opaque (post-process alpha is inverse opacity) and near.
                    if(P.A>=128||D[Y*DPitch+X]>=R.MaxDepth-120.f)continue;
                    ++Histogram[FMath::Clamp(FMath::RoundToInt(.2126f*P.R+.7152f*P.G+.0722f*P.B),0,255)];++Count;
                }
            if(C)R.Color->Unlock();
            if(D)R.Depth->Unlock();
            const auto Percentile=[&](double Q){const int64 Want=int64(Count*Q);int64 Run=0;for(int32 I=0;I<256;++I){Run+=Histogram[I];if(Run>Want)return float(I);}return 255.f;};
            R.Count=int32(Count);R.Median=Count?Percentile(.5):0.f;R.High=Count?Percentile(.97):0.f;
            R.Clip=Count?float(Histogram[253]+Histogram[254]+Histogram[255])/float(Count):0.f;
            R.State=2;
        });
        return;
    }
    // State 2: step the exposure toward the targets (the tone curve compresses highlights, hence the gain).
    M.State=0;
    if(M.Count<0){MeterSettleFrame=GFrameCounter+2;return;} // skipped (targets mid-resize): retry
    ++MeterPasses;MeterMedian=M.Median;MeterHigh=M.High;
    MeterClip=M.Clip;
    if(M.Count<400){bMetered=bShownMetered=true;MeteredExposure().Add(MeterQualityKey,ExposureOffset);return;}
    const float Up=FMath::Log2(MeterTargetMedian/FMath::Max(M.Median,2.f));
    // Highlights pull the exposure down, but never below a lit mid-tone: a few specular glints or
    // white hair may clip; a dark, muddy champion is worse.
    const float Down=M.High>=252.f?-.8f:FMath::Log2(MeterTargetHigh/FMath::Max(M.High,2.f))*1.8f;
    // More than ~1% of the body clipped (polished plate, a pale shield) reads as a glowing cut-out: then the
    // highlights may pull the mid-tones a little under the floor (never more than half a stop).
    const float FloorStep=FMath::Log2(MeterFloorMedian/FMath::Max(M.Median,2.f))-(M.Clip>MeterMaxClipShare?.5f:0.f);
    const float Delta=FMath::Clamp(FMath::Min(Up,FMath::Max(Down,FloorStep)),-2.f,1.5f);
    UE_LOG(LogCireDraftStage,Log,TEXT("CIRE_DRAFT_METER id=%s pass=%d pixels=%d median=%.0f p97=%.0f clip=%.4f offset=%.2f step=%.2f"),*ProfileId,MeterPasses,M.Count,M.Median,M.High,M.Clip,ExposureOffset,Delta);
    if(FMath::Abs(Delta)<.12f||MeterPasses>=6||(ExposureOffset<=-4.f&&Delta<0)||(ExposureOffset>=3.f&&Delta>0))
    {bMetered=bShownMetered=true;MeteredExposure().Add(MeterQualityKey,ExposureOffset);return;}
    SetExposureOffset(ExposureOffset+Delta*.85f);MeterSettleFrame=GFrameCounter+3;
}

void ACireDraftStage::EndPlay(const EEndPlayReason::Type Reason)
{
    if(bCutout){bCutout=false;RetainPropagateAlpha(false);}
    DestroyPreview();
    Super::EndPlay(Reason);
}
