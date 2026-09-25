#include "CireBatchArtGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireChampionActions.h" // creature-anim
#include "CireChampionArt.h"
#include "CireCreatureArt.h"
#include "CireMonsterAnim.h" // fab-integration: native Fab creature bodies
#include "CireMobility.h"
#include "CireChampionRoster.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireBatchArtGallery,Log,All);
namespace
{
constexpr int32 PageSize=2;
int32 StateCount=5;
const TCHAR* StateNames[]={TEXT("idle_front"),TEXT("walk_angled"),TEXT("attack_windup"),TEXT("attack_release"),TEXT("recovered_idle"),TEXT("walking_slow"),TEXT("airborne"),TEXT("roll_mid")};
// creature-anim: Tripo action clips settle by 1.5x the attack duration (0.975 s at base speed).
const float AttackPhases[]={-1,-1,.15f,.25f,1.05f,-1,-1,-1};
const FVector StageCenter(0,-2100,5000);
struct FBinding {FString Id,Mesh,Locomotion,Attack,Motion;float Height=0;bool bCustom=false,bFab=false;}; // fab-integration: bFab = Fab creature body
struct FModel
{
    int32 BindingIndex=0;
    TWeakObjectPtr<ACireHero> Hero;
    TWeakObjectPtr<ATextRenderActor> Label;
    TWeakObjectPtr<USkeletalMesh> ExpectedMesh;
    TWeakObjectPtr<UBlendSpace> ExpectedBlend;
    TWeakObjectPtr<UAnimSequence> ExpectedAttack;
    TWeakObjectPtr<UObject> ExpectedCustomSource;
    FVector IdleFoot=FVector::ZeroVector;
    FVector StagePosition=FVector::ZeroVector;
    FVector IdleHand=FVector::ZeroVector,WindupHand=FVector::ZeroVector;
    FName FootBone,HandBone; // fab-integration: sampled bones of a native (Fab) creature body, chosen on the idle pose
};
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> Controller;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<APointLight> Fill;
    TWeakObjectPtr<UTextRenderComponent> Title;
    TArray<FBinding> Bindings;
    TArray<FModel> Models;
    TArray<FString> Files;
    TArray<TSharedPtr<FJsonValue>> Captures;
    FString Directory;
    double Started=0,PageStarted=0;
    float MaxHeight=0,CameraDistance=0;
    int32 Page=0,Pages=0,LastCapture=INDEX_NONE,Checks=0;
    bool bBuilt=false,bPass=true,bDone=false;
} Gallery;

bool Check(bool bValue,const FString& Why)
{
    ++Gallery.Checks;
    if(!bValue){Gallery.bPass=false;UE_LOG(LogCireBatchArtGallery,Error,TEXT("CIRE_BATCH_ART_CHECK_FAIL %s"),*Why);}
    return bValue;
}
bool SaveManifest(bool bPass)
{
    auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schemaVersion"),1);
    Root->SetBoolField(TEXT("passed"),bPass);Root->SetBoolField(TEXT("visualReviewAccepted"),false);
    Root->SetBoolField(TEXT("stagedAnimationPresentation"),true);Root->SetNumberField(TEXT("checks"),Gallery.Checks);
    Root->SetNumberField(TEXT("statesPerPage"),StateCount);
    TArray<TSharedPtr<FJsonValue>> Profiles;
    for(const auto& B:Gallery.Bindings)
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("profileId"),B.Id);O->SetStringField(TEXT("mesh"),B.Mesh);
        O->SetStringField(TEXT("locomotion"),B.Locomotion);O->SetStringField(TEXT("attack"),B.Attack);O->SetNumberField(TEXT("heightCm"),B.Height);O->SetStringField(TEXT("motion"),B.Motion);
        Profiles.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("profiles"),Profiles);Root->SetArrayField(TEXT("captures"),Gallery.Captures);
    FString Json;return FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json)) &&
        FFileHelper::SaveStringToFile(Json,*FPaths::Combine(Gallery.Directory,TEXT("manifest.json")));
}
void Finish(bool bPass)
{
    if(Gallery.bDone)return;Gallery.bDone=true;bPass=SaveManifest(bPass)&&bPass;
    UE_LOG(LogCireBatchArtGallery,Display,TEXT("CIRE_BATCH_ART_GALLERY_%s profiles=%d captures=%d checks=%d directory=%s"),
        bPass?TEXT("PASS"):TEXT("FAIL"),Gallery.Bindings.Num(),Gallery.Files.Num(),Gallery.Checks,*Gallery.Directory);
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
bool ReadBindings()
{
    FString Json;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;double Schema=0;
    if(!Check(FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/ChampionArtBindings.json"))) &&
        Json.Len()<=512*1024 && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root) && Root &&
        Root->TryGetNumberField(TEXT("schemaVersion"),Schema) && Schema==1 && Root->TryGetArrayField(TEXT("bindings"),Rows),TEXT("valid runtime binding document required")))return false;
    FString Filter;TArray<FString> Requested;TSet<FString> Seen;
    if(FParse::Value(FCommandLine::Get(),TEXT("CireBatchArtProfiles="),Filter,false))Filter.ParseIntoArray(Requested,TEXT(","),true);
    for(const auto& Value:*Rows)
    {
        const TSharedPtr<FJsonObject>* O=nullptr;FString Status;FBinding B;double Height=0;
        if(!Value || !Value->TryGetObject(O) || !O || !(*O)->TryGetStringField(TEXT("status"),Status) || (Status!=TEXT("ready")&&Status!=TEXT("custom_ready")))continue;
        B.bCustom=Status==TEXT("custom_ready");
        if(!Check((*O)->TryGetStringField(TEXT("profileId"),B.Id) && CireChampionRoster::Find(B.Id) && !Seen.Contains(B.Id),TEXT("ready profile is unique and exists in the roster")))return false;
        Seen.Add(B.Id);
        if(!Requested.IsEmpty() && !Requested.Contains(B.Id))continue;
        if(B.bCustom)
        {
            if(!Check(UCireCreatureArt::Handles(B.Id)&&(*O)->TryGetStringField(TEXT("mesh"),B.Mesh)&&B.Mesh.StartsWith(TEXT("/Game/"))&&
                (*O)->TryGetStringField(TEXT("motion"),B.Motion)&&(*O)->TryGetNumberField(TEXT("heightCm"),Height)&&FMath::IsFinite(Height)&&Height>=50&&Height<=400,TEXT("explicit custom body and motion binding")))return false;
            B.Height=static_cast<float>(Height);
            // fab-integration: the purchased Fab body replaces the committed one when installed (ChampionArtBindings.fab.json).
            FString FabMesh,FabMotion;bool bFab=false;
            if(UCireChampionArt::EffectiveCreatureBinding(B.Id,FabMesh,FabMotion,bFab)&&bFab){B.Mesh=FabMesh;B.Motion=FabMotion;B.bFab=true;}
            Gallery.Bindings.Add(MoveTemp(B));continue;
        }
        if(!Check((*O)->TryGetStringField(TEXT("mesh"),B.Mesh) && (*O)->TryGetStringField(TEXT("locomotion"),B.Locomotion) &&
            (*O)->TryGetStringField(TEXT("attack"),B.Attack) && (*O)->TryGetNumberField(TEXT("heightCm"),Height) && FMath::IsFinite(Height) && Height>=80 && Height<=400 &&
            B.Mesh.StartsWith(TEXT("/Game/")) && B.Locomotion.StartsWith(TEXT("/Game/")) && B.Attack.StartsWith(TEXT("/Game/")),TEXT("ready binding has explicit assets and finite height")))return false;
        B.Height=static_cast<float>(Height);Gallery.Bindings.Add(MoveTemp(B));
    }
    for(const auto& Id:Requested)if(!Check(Seen.Contains(Id),TEXT("requested profile has a ready binding: ")+Id))return false;
    if(!Check(Gallery.Bindings.Num()>0 && Gallery.Bindings.Num()<=32,TEXT("gallery requires 1..32 ready bindings")))return false;
    Gallery.Bindings.Sort([](const FBinding& A,const FBinding& B)
    {
        const int32 AP=A.Id==TEXT("lancer")?0:A.Id==TEXT("summoner")?1:2,BP=B.Id==TEXT("lancer")?0:B.Id==TEXT("summoner")?1:2;
        return AP==BP?A.Id<B.Id:AP<BP;
    });
    Gallery.Pages=FMath::DivideAndRoundUp(Gallery.Bindings.Num(),PageSize);return true;
}
bool MatchingAssets(FModel& M)
{
    const auto& B=Gallery.Bindings[M.BindingIndex];auto* H=M.Hero.Get();auto* Mesh=H?H->GetMesh():nullptr;
    if(B.bCustom)
    {
        auto* Creature=H?H->FindComponentByClass<UCireCreatureArt>():nullptr;auto* Visual=Creature?Creature->VisualMesh():nullptr;
        bool bGood=H&&H->ChampionProfileId==B.Id&&H->ChampionArt&&H->ChampionArt->IsApplied()&&Creature&&Visual&&M.ExpectedCustomSource.IsValid()&&Creature->GetSourceAsset()==M.ExpectedCustomSource.Get();
        if(B.bFab)bGood&=Mesh&&Creature->GetNativeBody()==Mesh&&Mesh->GetSkeletalMeshAsset()==M.ExpectedCustomSource.Get()&&Cast<UCireMonsterAnimInstance>(Mesh->GetAnimInstance())&&Creature->HasReactions();
        else if(B.Id==TEXT("bear"))bGood&=Mesh&&Mesh->GetSkeletalMeshAsset()==M.ExpectedCustomSource.Get()&&Cast<UCireBearAnimInstance>(Mesh->GetAnimInstance());
        else if(B.Id==TEXT("whisp")){const auto* Static=Cast<UStaticMeshComponent>(Visual);bGood&=Static&&Static->GetStaticMesh()==M.ExpectedCustomSource.Get()&&!Mesh->GetSkeletalMeshAsset();}
        else {const auto* Proc=Cast<UProceduralMeshComponent>(Visual);bGood&=Proc&&Proc->GetNumSections()>0&&!Mesh->GetSkeletalMeshAsset();}
        return Check(bGood,TEXT("exact custom body replaces humanoid fallback: ")+B.Id);
    }
    auto* Combat=Mesh?Cast<UCireCombatAnimInstance>(Mesh->GetAnimInstance()):nullptr;
    bool bGood=H && H->ChampionProfileId==B.Id && H->ChampionArt && H->ChampionArt->IsApplied() && Mesh &&
        M.ExpectedMesh.IsValid() && M.ExpectedBlend.IsValid() && M.ExpectedAttack.IsValid() &&
        Mesh->GetSkeletalMeshAsset()==M.ExpectedMesh.Get() && Combat && Combat->GetAnimationAsset()==M.ExpectedBlend.Get() &&
        (Combat->AttackSequence==M.ExpectedAttack.Get() || // creature-anim: or the body's ChampionAttacks02 clip
         (Combat->AttackSequence && Combat->AttackSequence==CireChampionActions::ClipFor(Mesh->GetSkeletalMeshAsset(),CireChampionActions::ClipName(*H,TEXT("attack"))))) &&
        M.ExpectedMesh->GetSkeleton() &&
        M.ExpectedBlend->GetSkeleton()==M.ExpectedMesh->GetSkeleton() && M.ExpectedAttack->GetSkeleton()==M.ExpectedMesh->GetSkeleton();
    if(!bGood)return Check(false,TEXT("exact runtime body/locomotion/attack/skeleton binding: ")+B.Id);
    TSet<UAnimSequence*> Unique;
    for(const auto& Sample:M.ExpectedBlend->GetBlendSamples())
    {
        bGood&=Sample.Animation && Sample.Animation->GetSkeleton()==M.ExpectedMesh->GetSkeleton() && Sample.Animation->GetPlayLength()>0;
        if(Sample.Animation)Unique.Add(Sample.Animation.Get());
    }
    bGood&=Unique.Num()==17;
    const float ActualHeight=static_cast<float>(M.ExpectedMesh->GetImportedBounds().BoxExtent.Z*2*Mesh->GetComponentScale().Z);
    bGood&=FMath::IsNearlyEqual(ActualHeight,B.Height,.1f);
    const auto& Materials=M.ExpectedMesh->GetMaterials();bGood&=!Materials.IsEmpty();
    for(int32 I=0;I<Materials.Num();++I)bGood&=Materials[I].MaterialInterface && Mesh->GetMaterial(I)==Materials[I].MaterialInterface;
    return Check(bGood,TEXT("all 17 locomotion clips, height and materials match bound body: ")+B.Id);
}
bool BuildPage(ACireGameMode* Mode)
{
    for(auto& M:Gallery.Models){if(M.Hero.IsValid())M.Hero->Destroy();if(M.Label.IsValid())M.Label->Destroy();}
    Gallery.Models.Reset();Gallery.MaxHeight=0;Gallery.LastCapture=INDEX_NONE;
    const int32 First=Gallery.Page*PageSize,Count=FMath::Min(PageSize,Gallery.Bindings.Num()-First);
    for(int32 I=0;I<Count;++I)Gallery.MaxHeight=FMath::Max(Gallery.MaxHeight,Gallery.Bindings[First+I].Height);
    const float Spacing=FMath::Max(300.f,Gallery.MaxHeight*1.25f);
    Gallery.CameraDistance=FMath::Max((Count*Spacing+80)/(2*FMath::Tan(FMath::DegreesToRadians(25.f))),
        (Gallery.MaxHeight+(StateCount>5?300:100))/(2*FMath::Tan(FMath::Atan(FMath::Tan(FMath::DegreesToRadians(25.f))/(16.f/9.f)))))*1.06f;
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    for(int32 I=0;I<Count;++I)
    {
        FModel M;M.BindingIndex=First+I;const auto& B=Gallery.Bindings[M.BindingIndex];
        if(B.bCustom)M.ExpectedCustomSource=LoadObject<UObject>(nullptr,*B.Mesh);
        else {M.ExpectedMesh=LoadObject<USkeletalMesh>(nullptr,*B.Mesh);M.ExpectedBlend=LoadObject<UBlendSpace>(nullptr,*B.Locomotion);M.ExpectedAttack=LoadObject<UAnimSequence>(nullptr,*B.Attack);}
        if(!Check(B.bCustom?M.ExpectedCustomSource.IsValid():M.ExpectedMesh.IsValid()&&M.ExpectedBlend.IsValid()&&M.ExpectedAttack.IsValid(),TEXT("saved binding assets load: ")+B.Id))return false;
        const FVector Position=StageCenter+FVector(0,(I-(Count-1)*.5f)*Spacing,88);
        M.StagePosition=Position;
        M.Hero=Mode->GetWorld()->SpawnActor<ACireHero>(Position,FRotator::ZeroRotator,Params);
        if(!M.Hero.IsValid())return false;auto* H=M.Hero.Get();H->TeamId=0;
        if(!Check(H->DraftProfile(B.Id),TEXT("real hero drafts bound profile: ")+B.Id))return false;
        H->bBot=false;H->bAutoAttack=false;H->Target=nullptr;H->AttackSerial=0;H->AttackDuration=.65f;H->AttackAimLocation=Position+FVector(600,0,0);
        H->SetActorEnableCollision(false);H->SetActorTickEnabled(false);H->GetCharacterMovement()->StopMovementImmediately();H->GetCharacterMovement()->SetComponentTickEnabled(false);
        H->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        H->ChampionArt->UpdateVisuals(*H,.1f);H->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
        H->GetMesh()->SetVisibility(true,true);H->SetActorHiddenInGame(false);
        if(!MatchingAssets(M))return false;
        M.Label=Mode->GetWorld()->SpawnActor<ATextRenderActor>(StageCenter+FVector(0,Position.Y-StageCenter.Y,B.Height+28),FRotator::ZeroRotator);
        if(M.Label.IsValid())
        {
            auto* Text=M.Label->GetTextRender();Text->SetText(FText::FromString(B.Id.ToUpper()));Text->SetWorldSize(FMath::Min(13.f,240.f/FMath::Max(1,B.Id.Len())));
            Text->SetTextRenderColor(FColor::White);Text->SetHorizontalAlignment(EHTA_Center);
        }
        UE_LOG(LogCireBatchArtGallery,Display,TEXT("CIRE_BATCH_ART_BINDING_PASS profile=%s mesh=%s motion=%s height=%.1f"),*B.Id,*B.Mesh,B.bCustom?*B.Motion:*B.Locomotion,B.Height);
        Gallery.Models.Add(M);
    }
    Gallery.PageStarted=FPlatformTime::Seconds()+(Gallery.Page==0?5.:2.);return true;
}
bool BuildStage(ACireGameMode* Mode,ACireController* Controller)
{
    auto* Player=Cast<ACireHero>(Controller->GetPawn());if(!Player)return false;Gallery.Controller=Controller;
    Player->TeamId=0;Player->bDrafted=true;Player->SetActorHiddenInGame(true);Player->SetActorEnableCollision(false);Player->SetActorTickEnabled(false);
    Player->GetCharacterMovement()->DisableMovement();Controller->SetIgnoreMoveInput(true);Controller->SetIgnoreLookInput(true);Controller->bShowMouseCursor=false;
    if(Controller->GetHUD())Controller->GetHUD()->bShowHUD=false;
    UWorld* World=Mode->GetWorld();
    for(auto* M:Mode->Monsters)if(IsValid(M))M->Destroy();Mode->Monsters.Reset();
    for(TActorIterator<APointLight> It(World);It;++It)It->PointLightComponent->SetIntensity(0);
    for(TActorIterator<ADirectionalLight> It(World);It;++It)It->GetLightComponent()->SetIntensity(0);
    for(TActorIterator<ASkyLight> It(World);It;++It){It->GetLightComponent()->SetLightColor(FLinearColor::White);It->GetLightComponent()->SetIntensity(.85f);}
    auto* Key=World->SpawnActor<ADirectionalLight>(StageCenter+FVector(0,0,2000),FRotator(-38,145,0));if(!Key)return false;
    Key->GetLightComponent()->SetMobility(EComponentMobility::Movable);Key->GetLightComponent()->SetLightColor(FLinearColor::White);Key->GetLightComponent()->SetIntensity(5.5f);
    Gallery.Fill=World->SpawnActor<APointLight>();if(!Gallery.Fill.IsValid())return false;
    auto* Fill=Gallery.Fill->PointLightComponent.Get();Fill->SetMobility(EComponentMobility::Movable);Fill->SetLightColor(FLinearColor::White);Fill->SetIntensityUnits(ELightUnits::Lumens);
    Fill->SetIntensity(8000);Fill->SetAttenuationRadius(2000);Fill->SetCastShadows(false);
    auto* Floor=World->SpawnActor<AStaticMeshActor>(StageCenter-FVector(0,0,50),FRotator::ZeroRotator);if(!Floor)return false;
    Floor->SetMobility(EComponentMobility::Movable);Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Floor->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_Slate.M_Slate")));
    Floor->SetActorScale3D(FVector(40,25,1));Floor->SetActorEnableCollision(false);
    Gallery.Camera=World->SpawnActor<ACameraActor>();if(!Gallery.Camera.IsValid())return false;
    auto* Camera=Gallery.Camera->GetCameraComponent();Camera->SetFieldOfView(50);Camera->SetAspectRatio(16.f/9.f);Camera->bConstrainAspectRatio=true;
    auto& Post=Camera->PostProcessSettings;Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;Post.bOverride_AutoExposureBias=true;Post.AutoExposureBias=.5f;
    Post.bOverride_MotionBlurAmount=true;Post.MotionBlurAmount=0;Post.bOverride_BloomIntensity=true;Post.BloomIntensity=0;
    auto* Title=NewObject<UTextRenderComponent>(Gallery.Camera.Get());Gallery.Camera->AddInstanceComponent(Title);Title->SetupAttachment(Camera);
    Title->SetRelativeLocation(FVector(250,-103,55));Title->SetRelativeRotation(FRotator(0,180,0));Title->SetWorldSize(4.2f);Title->SetTextRenderColor(FColor(245,220,170));Title->RegisterComponent();Gallery.Title=Title;
    Controller->SetViewTarget(Gallery.Camera.Get());Gallery.bBuilt=true;return BuildPage(Mode);
}
void UpdatePose(ACireGameMode* Mode,int32 State)
{
    const auto* GameState=Mode->GetWorld()->GetGameState();const float Now=GameState?GameState->GetServerWorldTimeSeconds():Mode->GetWorld()->GetTimeSeconds();
    for(auto& M:Gallery.Models)if(M.Hero.IsValid())
    {
        auto& H=*M.Hero.Get();H.GetCharacterMovement()->Velocity=State==1?FVector(300,0,0):State==5?FVector(CireMovement::Tuning().WalkSpeed,0,0):FVector::ZeroVector;
        H.GetCharacterMovement()->SetMovementMode(State==6?MOVE_Falling:MOVE_Walking);
        H.SetActorLocation(M.StagePosition+FVector(0,0,State==6?80:0));
        if(H.Mobility){H.Mobility->bWalking=State==5;H.Mobility->RollDuration=CireMovement::Tuning().RollDuration;H.Mobility->RollStartedAt=State==7?Now-H.Mobility->RollDuration*.5f:-100;H.Mobility->RollDirection=H.GetActorForwardVector();}
        H.AttackSerial=State>=2&&State<=4?Gallery.Page*StateCount+State+1:0;H.AttackStartedServerTime=Now-FMath::Max(0.f,AttackPhases[State]);
        H.ChampionArt->UpdateVisuals(H,Mode->GetWorld()->GetDeltaSeconds());
        if(auto* Animation=H.GetMesh()->GetSingleNodeInstance()){Animation->SetPosition(State==1||State==5?.25f:0.f,false);Animation->SetPlaying(false);}
    }
    const FVector Center=StageCenter+FVector(0,0,Gallery.MaxHeight*.5f+15);
    const FVector Direction=State>=5?FVector(.75,.66f,.08f).GetSafeNormal():State==1||State==3?FVector(1,.32f,.11f).GetSafeNormal():FVector(1,0,.06f).GetSafeNormal();
    const FVector View=Center+Direction*Gallery.CameraDistance;Gallery.Camera->SetActorLocation(View);Gallery.Camera->SetActorRotation((Center-View).Rotation());
    Gallery.Fill->SetActorLocation(Center+FVector(400,-220,230));
    for(auto& M:Gallery.Models)if(M.Label.IsValid())
    {
        M.Label->SetActorRotation(FRotator(0,State>=5?41:State==1||State==3?18:0,0));
        M.Label->SetActorLocation(FVector(M.StagePosition.X,M.StagePosition.Y,StageCenter.Z+Gallery.Bindings[M.BindingIndex].Height+28+(State==6?80:0)));
    }
    Gallery.Title->SetText(FText::FromString(FString::Printf(TEXT("BATCH ART  %d/%d  |  %s  |  PROTOTYPE MOTION"),Gallery.Page+1,Gallery.Pages,StateNames[State])));
}
void Capture(int32 State)
{
    int32 Width=0,Height=0;Gallery.Controller->GetViewportSize(Width,Height);int32 RequestedWidth=1920,RequestedHeight=1080;
    FParse::Value(FCommandLine::Get(),TEXT("ResX="),RequestedWidth);FParse::Value(FCommandLine::Get(),TEXT("ResY="),RequestedHeight);
    Check(Width==RequestedWidth&&Height==RequestedHeight,TEXT("gallery viewport matches requested dimensions"));
    auto Entry=MakeShared<FJsonObject>();Entry->SetNumberField(TEXT("page"),Gallery.Page+1);Entry->SetStringField(TEXT("state"),StateNames[State]);
    TArray<TSharedPtr<FJsonValue>> Profiles,Poses;
    for(auto& M:Gallery.Models)
    {
        const auto& B=Gallery.Bindings[M.BindingIndex];Profiles.Add(MakeShared<FJsonValueString>(B.Id));
        if(!MatchingAssets(M))continue;
        if(B.bCustom)
        {
            auto* H=M.Hero.Get();auto* Creature=H->FindComponentByClass<UCireCreatureArt>();auto* Visual=Creature->VisualMesh();
            FVector Foot=Visual->GetComponentLocation(),Hand=Foot;bool bFinite=true;
            if(B.bFab)
            {
                // Native Fab body: the lowest bone of the idle pose is a foot, the one farthest forward the striking limb.
                USkeletalMeshComponent* Body=H->GetMesh();
                if(State==0||M.FootBone.IsNone())
                {
                    float Low=TNumericLimits<float>::Max(),Far=-TNumericLimits<float>::Max();const FVector Forward=H->GetActorForwardVector();
                    for(int32 I=1;I<Body->GetNumBones();++I) // the root sits on the pivot: skip it
                    {
                        const FVector P=Body->GetBoneLocation(Body->GetBoneName(I));
                        if(FVector::Dist(P,Body->GetComponentLocation())<1.f)continue; // IK / root helpers at the pivot
                        if(P.Z<Low){Low=static_cast<float>(P.Z);M.FootBone=Body->GetBoneName(I);}
                        const float Ahead=static_cast<float>(FVector::DotProduct(P-H->GetActorLocation(),Forward));
                        if(Ahead>Far){Far=Ahead;M.HandBone=Body->GetBoneName(I);}
                    }
                }
                Foot=Body->GetBoneLocation(M.FootBone);Hand=Body->GetBoneLocation(M.HandBone);
                for(int32 I=0;I<Body->GetNumBones();++I)bFinite&=!Body->GetBoneTransform(I).ContainsNaN();
            }
            else if(B.Id==TEXT("bear"))
            {
                Foot=H->GetMesh()->GetSocketLocation(TEXT("0_Right_Limb_5"));Hand=H->GetMesh()->GetSocketLocation(TEXT("0_Left_Limb_5"));
                for(int32 I=0;I<H->GetMesh()->GetNumBones();++I)bFinite&=!H->GetMesh()->GetBoneTransform(I).ContainsNaN();
            }
            else if(auto* Proc=Cast<UProceduralMeshComponent>(Visual))
            {
                bool bFootFound=false,bHandFound=false;
                for(int32 S=0;S<Proc->GetNumSections();++S)if(const auto* Section=Proc->GetProcMeshSection(S))
                    for(const auto& V:Section->ProcVertexBuffer)
                    {
                        bFinite&=!V.Position.ContainsNaN()&&V.Position.Size()<200;
                        if(!bFootFound&&V.Position.X>5&&V.Position.Y>14&&V.Position.Z<9){Foot=Visual->GetComponentTransform().TransformPosition(V.Position);bFootFound=true;}
                        if(!bHandFound&&V.Position.X>14&&V.Position.Z>50){Hand=Visual->GetComponentTransform().TransformPosition(V.Position);bHandFound=true;}
                    }
                bFinite&=bFootFound&&bHandFound;
            }
            if(State==0){M.IdleFoot=Foot;M.IdleHand=Hand;}if(State==2)M.WindupHand=Hand;
            auto Pose=MakeShared<FJsonObject>();Pose->SetStringField(TEXT("profileId"),B.Id);Pose->SetStringField(TEXT("actualMesh"),Creature->GetSourceAsset()->GetPathName());
            Pose->SetStringField(TEXT("motion"),B.Motion);Pose->SetStringField(TEXT("skeleton"),B.bFab?TEXT("fab_native_clips"):B.Id==TEXT("bear")?TEXT("custom_quadruped_45_bones"):TEXT("none_procedural_surface"));
            Pose->SetStringField(TEXT("footSample"),Foot.ToString());Pose->SetStringField(TEXT("attackSample"),Hand.ToString());Pose->SetNumberField(TEXT("motionPhase"),Creature->MotionPhase());
            Pose->SetNumberField(TEXT("walkSampleDistanceCm"),FVector::Dist(Foot,M.IdleFoot));Pose->SetNumberField(TEXT("attackSampleDistanceCm"),FVector::Dist(Hand,M.WindupHand));
            FVector2D CenterPixel;const bool bFramed=Gallery.Controller->ProjectWorldLocationToScreen(Visual->Bounds.Origin,CenterPixel)&&CenterPixel.X>=12&&CenterPixel.X<Width-12&&CenterPixel.Y>=12&&CenterPixel.Y<Height-12;
            bool bValid=bFinite&&bFramed&&!H->IsHidden()&&Visual->IsVisible()&&!Visual->GetComponentTransform().ContainsNaN()&&Visual->GetNumMaterials()>0&&Visual->GetMaterial(0);
            if(State==1)bValid&=H->GetVelocity().Size2D()>290&&FVector::Dist(Foot,M.IdleFoot)>.01;
            if(State==3)bValid&=FVector::Dist(Hand,M.WindupHand)>.01;
            if(State==5)bValid&=H->Mobility&&H->Mobility->bWalking&&H->GetVelocity().Size2D()>CireMovement::Tuning().WalkSpeed*.95;
            if(State==6)bValid&=H->GetCharacterMovement()->IsFalling()&&H->GetActorLocation().Z>M.StagePosition.Z+75;
            if(State==7)bValid&=H->Mobility&&H->Mobility->IsRolling()&&FMath::Abs(H->Mobility->RollProgress()-.5f)<.08f;
            Pose->SetBoolField(TEXT("valid"),bValid);Poses.Add(MakeShared<FJsonValueObject>(Pose));
            Check(bValid,FString::Printf(TEXT("custom finite framed moving body %s / %s"),*B.Id,StateNames[State]));
            UE_LOG(LogCireBatchArtGallery,Display,TEXT("CIRE_CUSTOM_ART_POSE profile=%s state=%s valid=%d walk_delta=%.2f attack_delta=%.2f"),*B.Id,StateNames[State],bValid,FVector::Dist(Foot,M.IdleFoot),FVector::Dist(Hand,M.WindupHand));
            continue;
        }
        auto* H=M.Hero.Get();auto* Mesh=H->GetMesh();auto* Combat=Cast<UCireCombatAnimInstance>(Mesh->GetAnimInstance());
        FVector BlendInput,FilteredInput;Combat->GetBlendSpaceState(BlendInput,FilteredInput);
        auto Pose=MakeShared<FJsonObject>();Pose->SetStringField(TEXT("profileId"),B.Id);Pose->SetStringField(TEXT("actualMesh"),Mesh->GetSkeletalMeshAsset()->GetPathName());
        Pose->SetStringField(TEXT("skeleton"),Mesh->GetSkeletalMeshAsset()->GetSkeleton()->GetPathName());Pose->SetNumberField(TEXT("attackWeight"),Combat->AttackWeight);
        Pose->SetStringField(TEXT("blendInput"),BlendInput.ToString());Pose->SetStringField(TEXT("filteredBlendInput"),FilteredInput.ToString());
        bool bValid=!H->IsHidden()&&Mesh->IsVisible()&&!Mesh->GetComponentTransform().ContainsNaN();
        const auto InFrame=[&](FVector P){FVector2D Pixel;return Gallery.Controller->ProjectWorldLocationToScreen(P,Pixel)&&Pixel.X>=12&&Pixel.X<Width-12&&Pixel.Y>=12&&Pixel.Y<Height-12;};
        for(const TCHAR* Bone:{TEXT("root"),TEXT("pelvis"),TEXT("head"),TEXT("foot_l"),TEXT("foot_r"),TEXT("hand_l"),TEXT("hand_r")})
        {
            const int32 Index=Mesh->GetBoneIndex(Bone);if(Index==INDEX_NONE){bValid=false;continue;}
            const FTransform Transform=Mesh->GetBoneTransform(Index);bValid&=!Transform.ContainsNaN();
            Pose->SetStringField(Bone,Transform.GetLocation().ToString());
            if(FName(Bone)==TEXT("root"))
            {
                const FVector Expected=Mesh->GetComponentScale()*Mesh->GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose()[Index].GetScale3D();
                bValid&=Transform.GetScale3D().Equals(Expected,FMath::Max(.01,Expected.GetAbsMax()*.002));
            }
            else bValid&=InFrame(Transform.GetLocation());
        }
        const FVector Head=Mesh->GetSocketLocation(TEXT("head")),LeftFoot=Mesh->GetSocketLocation(TEXT("foot_l")),RightFoot=Mesh->GetSocketLocation(TEXT("foot_r")),Hand=Mesh->GetSocketLocation(TEXT("hand_r"));
        const double Span=Head.Z-FMath::Min(LeftFoot.Z,RightFoot.Z);
        if(State!=7)bValid&=Span>B.Height*.4f&&Span<B.Height*1.25f&&FMath::Min(LeftFoot.Z,RightFoot.Z)>StageCenter.Z-20&&FMath::Max(LeftFoot.Z,RightFoot.Z)<StageCenter.Z+B.Height*.3+(State==6?110:0);
        else bValid&=FMath::Min(Head.Z,FMath::Min(Hand.Z,FMath::Min(LeftFoot.Z,RightFoot.Z)))>StageCenter.Z-15&&Combat->RollProgress>.42f&&Combat->RollProgress<.58f;
        bValid&=(State==2||State==3)?Combat->AttackWeight>.9f:Combat->AttackWeight<.001f;
        if(State==0)M.IdleHand=Hand;
        if(State==2)M.WindupHand=Hand;
        if(State==3)bValid&=FVector::Dist(Hand,M.WindupHand)>B.Height*.015f;
        if(State==4)bValid&=FVector::Dist(Hand,M.IdleHand)<B.Height*.08f;
        if(State==1)bValid&=H->GetVelocity().Size2D()>290&&BlendInput.Y>290&&FilteredInput.Y>250;
        else if(State==5)bValid&=H->Mobility&&H->Mobility->bWalking&&FMath::Abs(BlendInput.Y-CireMovement::Tuning().WalkSpeed)<3&&FilteredInput.Y>CireMovement::Tuning().WalkSpeed*.8;
        else bValid&=BlendInput.Y<2&&FilteredInput.Y<10;
        if(State==6)bValid&=Combat->AirWeight>.95f&&H->GetCharacterMovement()->IsFalling();
        Pose->SetNumberField(TEXT("airWeight"),Combat->AirWeight);Pose->SetNumberField(TEXT("rollProgress"),Combat->RollProgress);
        Pose->SetBoolField(TEXT("valid"),bValid);Pose->SetNumberField(TEXT("headFeetSpanCm"),Span);Poses.Add(MakeShared<FJsonValueObject>(Pose));
        Check(bValid,FString::Printf(TEXT("visible finite framed pose and attack state %s / %s"),*B.Id,StateNames[State]));
        UE_LOG(LogCireBatchArtGallery,Display,TEXT("CIRE_BATCH_ART_POSE profile=%s state=%s valid=%d attack_weight=%.3f head_feet=%.2f"),*B.Id,StateNames[State],bValid,Combat->AttackWeight,Span);
    }
    FString Group;for(const auto& M:Gallery.Models){if(!Group.IsEmpty())Group+=TEXT("_");Group+=Gallery.Bindings[M.BindingIndex].Id;}
    const FString File=FPaths::Combine(Gallery.Directory,FString::Printf(TEXT("%02d_%s_%s.png"),Gallery.Page+1,*Group,StateNames[State]));
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);Gallery.Files.Add(File);Gallery.LastCapture=State;
    Entry->SetStringField(TEXT("file"),File);Entry->SetArrayField(TEXT("profiles"),Profiles);Entry->SetArrayField(TEXT("poses"),Poses);Gallery.Captures.Add(MakeShared<FJsonValueObject>(Entry));
    UE_LOG(LogCireBatchArtGallery,Display,TEXT("CIRE_BATCH_ART_CAPTURE page=%d state=%s file=%s"),Gallery.Page+1,StateNames[State],*File);
}
}
bool CireBatchArtGallery::Initialize(ACireGameMode* Mode)
{
    Gallery={};if(!FParse::Param(FCommandLine::Get(),TEXT("CireBatchArtGallery")))return false;
    StateCount=FParse::Param(FCommandLine::Get(),TEXT("CireMobilityArtGallery"))?8:5;
    Gallery.Mode=Mode;Gallery.Started=FPlatformTime::Seconds();
    Gallery.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("BatchArtGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))+TEXT("-")+FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(6)));
    if(!IFileManager::Get().MakeDirectory(*Gallery.Directory,true)||!Mode||Mode->GetNetMode()!=NM_Standalone||
        !FParse::Param(FCommandLine::Get(),TEXT("CireTripoChampions"))||!ReadBindings()){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;
    UE_LOG(LogCireBatchArtGallery,Display,TEXT("CIRE_BATCH_ART_GALLERY_READY profiles=%d pages=%d states=%d"),Gallery.Bindings.Num(),Gallery.Pages,StateCount);return true;
}
bool CireBatchArtGallery::Tick(ACireGameMode* Mode)
{
    if(Gallery.Mode.Get()!=Mode)return false;if(Gallery.bDone)return true;
    if(FPlatformTime::Seconds()-Gallery.Started>70+Gallery.Pages*(StateCount*3+3)){Finish(false);return true;}
    if(!Gallery.bBuilt)
    {
        auto* Controller=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(Controller&&Controller->GetPawn()&&Controller->GetHUD()&&!BuildStage(Mode,Controller))Finish(false);
        return true;
    }
    const double Age=FPlatformTime::Seconds()-Gallery.PageStarted;const int32 State=FMath::Clamp(FMath::FloorToInt(Age/3.),0,StateCount-1);
    UpdatePose(Mode,State);
    if(State>Gallery.LastCapture&&Age>=State*3+2)Capture(State);
    if(Age>=StateCount*3)
    {
        if(++Gallery.Page<Gallery.Pages){if(!BuildPage(Mode))Finish(false);}
        else
        {
            Check(Gallery.Files.Num()==Gallery.Pages*StateCount,TEXT("all profile pages have every requested capture state"));
            for(const auto& File:Gallery.Files)Check(IFileManager::Get().FileSize(*File)>1024,TEXT("rendered capture exists: ")+File);
            Finish(Gallery.bPass);
        }
    }
    return true;
}
#endif
