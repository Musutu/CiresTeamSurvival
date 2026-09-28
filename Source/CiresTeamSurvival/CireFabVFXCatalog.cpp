#include "CireFabVFXCatalog.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireFabVFX.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextRenderActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystemComponent.h"
#include "Serialization/JsonWriter.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Particles/ParticleSystem.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace
{
struct FItem { FString Path, Label; };
struct FCatalog
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireController> PC;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<ATextRenderActor> Caption;
    TWeakObjectPtr<UFXSystemComponent> Live;
    TArray<FItem> Items;
    FString Directory;
    double Start=0,Ready=-1,ItemAt=-1,CapturedAt=0;
    int32 Index=-1,Frames=0,Captures=0,Missing=0,Stage=0; // Stage: 0 spawned, 1 early captured, 2 late captured
    float MaxReach=0;
    bool bDone=false,bChecks=true;
};
FCatalog G;
const FVector Stage(0,0,9000);
constexpr double EarlyAt=.32,LateAt=1.15,Rest=.25;
void Finish(bool Pass)
{
    if(G.bDone)return;G.bDone=true;
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_CATALOG_%s items=%d captures=%d missing=%d directory=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),G.Items.Num(),G.Captures,G.Missing,*G.Directory);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
bool LoadList(const FString& File)
{
    FString Text;if(!FFileHelper::LoadFileToString(Text,*File))return false;
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root)||!Root.IsValid())return false;
    const TArray<TSharedPtr<FJsonValue>>* Systems=nullptr;
    if(!Root->TryGetArrayField(TEXT("systems"),Systems))return false;
    for(const auto& V:*Systems)
    {
        const TSharedPtr<FJsonObject> O=V->AsObject();if(!O.IsValid())continue;
        FItem I;I.Path=O->GetStringField(TEXT("path"));O->TryGetStringField(TEXT("label"),I.Label);
        if(!I.Path.IsEmpty())G.Items.Add(I);
    }
    return G.Items.Num()>0;
}
bool Setup(ACireGameMode* Mode,ACireController* PC)
{
    UWorld* World=Mode->GetWorld();G.PC=PC;
    if(auto* H=Cast<ACireHero>(PC->GetPawn())){H->bDrafted=true;H->TeamId=0;H->SetActorHiddenInGame(true);H->SetActorEnableCollision(false);H->SetActorTickEnabled(false);}
    PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);PC->bShowMouseCursor=false;
    if(PC->GetHUD())PC->GetHUD()->bShowHUD=false;
    auto* Floor=World->SpawnActor<AStaticMeshActor>(Stage-FVector(0,0,15),FRotator::ZeroRotator);
    if(!Floor)return false;
    Floor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
    Floor->GetStaticMeshComponent()->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    if(!Floor->GetStaticMeshComponent()->GetStaticMesh())return false;
    Floor->SetActorScale3D(FVector(40,40,.25));
    Floor->GetStaticMeshComponent()->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Effects/CireSpell/M_Runestone.M_Runestone")));
    auto* Light=World->SpawnActor<ADirectionalLight>(Stage+FVector(400,0,2000),FRotator(-48,-120,0));
    if(!Light)return false;
    Light->GetLightComponent()->SetIntensity(3.f);Light->GetLightComponent()->SetLightColor(FLinearColor(.8f,.86f,1.f));
    // Gameplay-like read: about 8 m out and 4 m up, looking at the unit's chest height.
    G.Camera=World->SpawnActor<ACameraActor>(Stage+FVector(820,0,430),FRotator::ZeroRotator);
    if(!G.Camera.IsValid())return false;
    G.Camera->SetActorRotation((Stage+FVector(0,0,90)-G.Camera->GetActorLocation()).Rotation());
    auto* Camera=G.Camera->GetCameraComponent();Camera->SetFieldOfView(58);Camera->SetAspectRatio(16.f/9.f);Camera->bConstrainAspectRatio=true;
    auto& Post=Camera->PostProcessSettings;
    Post.bOverride_AutoExposureMethod=true;Post.AutoExposureMethod=EAutoExposureMethod::AEM_Manual;
    Post.bOverride_AutoExposureApplyPhysicalCameraExposure=true;Post.AutoExposureApplyPhysicalCameraExposure=false;
    Post.bOverride_AutoExposureBias=true;Post.AutoExposureBias=.5f;
    Post.bOverride_BloomIntensity=true;Post.BloomIntensity=.45f;
    Post.bOverride_MotionBlurAmount=true;Post.MotionBlurAmount=0;
    PC->SetViewTarget(G.Camera.Get());
    auto* Text=World->SpawnActor<ATextRenderActor>(Stage+FVector(-300,0,330),FRotator::ZeroRotator);
    if(Text)
    {
        Text->SetActorRotation((G.Camera->GetActorLocation()-Text->GetActorLocation()).Rotation());
        Text->GetTextRender()->SetWorldSize(22);Text->GetTextRender()->SetTextRenderColor(FColor(219,203,174));
        Text->GetTextRender()->SetHorizontalAlignment(EHTA_Center);G.Caption=Text;
    }
    G.Ready=FPlatformTime::Seconds();return true;
}
FString ColorParams(UFXSystemAsset* System)
{
    UNiagaraSystem* Niagara=Cast<UNiagaraSystem>(System);if(!Niagara)return TEXT("cascade");
    TArray<FNiagaraVariable> Params;Niagara->GetExposedParameters().GetUserParameters(Params);
    TArray<FString> Names;
    for(const FNiagaraVariable& P:Params)if(P.GetType()==FNiagaraTypeDefinition::GetColorDef())Names.Add(P.GetName().ToString().Replace(TEXT("User."),TEXT("")));
    return Names.Num()?FString::Join(Names,TEXT("|")):TEXT("-");
}
void SpawnItem(int32 Index)
{
    if(G.Live.IsValid()){G.Live->DestroyComponent();G.Live=nullptr;}
    G.Index=Index;G.ItemAt=FPlatformTime::Seconds();G.Frames=0;G.Stage=0;G.MaxReach=0;
    const FItem& I=G.Items[Index];
    if(G.Caption.IsValid())G.Caption->GetTextRender()->SetText(FText::FromString(FString::Printf(TEXT("%03d %s"),Index,*I.Label)));
    CireFabVFX::FEntry E;E.Candidates.Add(I.Path);
    UFXSystemAsset* System=CireFabVFX::Resolve(&E);
    if(!System){++G.Missing;UE_LOG(LogTemp,Warning,TEXT("CIRE_FAB_CATALOG_MISSING %s"),*I.Path);G.Stage=2;G.CapturedAt=FPlatformTime::Seconds();return;}
    const float Scale=System->IsA<UParticleSystem>()?.6f:1.f; // Kakky's Cascade systems are authored at UE4 demo scale
    G.Live=CireFabVFX::SpawnAt(G.Mode->GetWorld(),System,Stage+FVector(0,0,2),FRotator::ZeroRotator,Scale);
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_CATALOG_ITEM index=%d path=%s colors=%s"),Index,*I.Path,*ColorParams(System));
}
void Capture(const TCHAR* Frame)
{
    const FString File=FPaths::Combine(G.Directory,FString::Printf(TEXT("%03d_%s.png"),G.Index,Frame));
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);++G.Captures;G.CapturedAt=FPlatformTime::Seconds();
}
}

// vfx-loop-fix: loop / placement audit of every system FabVFX.json references (-CireFabLoopAudit[=<out.json>]).
// Each system is spawned (scalability off, never pooled) on a far stage in batches; the audit records when it completes on
// its own, whether it still runs after LoopProbeAt seconds (a looping system), whether it still runs DeactivateGrace seconds
// after Deactivate (ignores deactivation), and its vertical extent relative to the spawn point (an effect whose lowest
// particle sits at the origin and rises tall is authored to be spawned on the ground). Ends with CIRE_FAB_LOOP_AUDIT_DONE.
namespace CireFabLoopAudit
{
struct FRow { FString Path; TWeakObjectPtr<UFXSystemComponent> C; FVector At=FVector::ZeroVector; float DoneAt=-1,MinZ=1e9f,MaxZ=-1e9f,Reach=0; bool bLoop=false,bIgnores=false,bMissing=false,bSeen=false; };
struct FState { TWeakObjectPtr<ACireGameMode> Mode; FString Out; TArray<FRow> Rows; int32 Next=0,BatchStart=0,BatchEnd=0; double BatchAt=-1,Start=0; int32 Phase=0; bool bDone=false; };
FState A;
constexpr float LoopProbeAt=6.f,DeactivateGrace=6.f;constexpr int32 Batch=48;
void CollectPaths(const TSharedPtr<FJsonValue>& V,TSet<FString>& Out)
{
    if(!V.IsValid())return;
    if(V->Type==EJson::String){const FString S=V->AsString();if(S.StartsWith(TEXT("/Game/"))&&S.Contains(TEXT(".")))Out.Add(S);return;}
    if(V->Type==EJson::Array){for(const auto& E:V->AsArray())CollectPaths(E,Out);return;}
    if(V->Type==EJson::Object)for(const auto& KV:V->AsObject()->Values)if(KV.Key!=TEXT("groundRadius")&&KV.Key!=TEXT("groundExcluded"))CollectPaths(KV.Value,Out);
}
bool IsRunning(const UFXSystemComponent* C){return C&&C->IsActive();}
void SpawnBatch(UWorld* World)
{
    A.BatchStart=A.Next;A.BatchEnd=FMath::Min(A.Rows.Num(),A.Next+Batch);A.Next=A.BatchEnd;A.BatchAt=World->GetTimeSeconds();A.Phase=0;
    for(int32 I=A.BatchStart;I<A.BatchEnd;++I)
    {
        FRow& R=A.Rows[I];const int32 K=I-A.BatchStart;R.At=FVector(40000+(K%8)*2500.f,40000+(K/8)*2500.f,30000);
        CireFabVFX::FEntry E;E.Candidates.Add(R.Path);UFXSystemAsset* System=CireFabVFX::Resolve(&E);
        UFXSystemComponent* C=nullptr;
        if(UNiagaraSystem* N=Cast<UNiagaraSystem>(System))
        {
            UNiagaraComponent* NC=UNiagaraFunctionLibrary::SpawnSystemAtLocation(World,N,R.At,FRotator::ZeroRotator,FVector(1.f),false,false,ENCPoolMethod::None,false);
            if(NC){NC->SetAllowScalability(false);NC->Activate(true);}C=NC;
        }
        else if(UParticleSystem* P=Cast<UParticleSystem>(System))C=UGameplayStatics::SpawnEmitterAtLocation(World,P,FTransform(R.At),false,EPSCPoolMethod::None,true);
        R.C=C;R.bMissing=C==nullptr;
    }
}
void Measure(float Age)
{
    for(int32 I=A.BatchStart;I<A.BatchEnd;++I)
    {
        FRow& R=A.Rows[I];UFXSystemComponent* C=R.C.Get();if(!C)continue;
        if(IsRunning(C))R.bSeen=true;
        else if(A.Phase==0&&R.DoneAt<0&&R.bSeen)R.DoneAt=Age;
        if(!IsRunning(C))continue;
        const FBoxSphereBounds B=C->CalcBounds(C->GetComponentTransform());const FBox Box=B.GetBox();
        if(Box.IsValid&&B.SphereRadius>1.f&&B.SphereRadius<1e5f)
        {R.MinZ=FMath::Min(R.MinZ,float(Box.Min.Z-R.At.Z));R.MaxZ=FMath::Max(R.MaxZ,float(Box.Max.Z-R.At.Z));R.Reach=FMath::Max(R.Reach,CireFabVFX::MeasureReach(C));}
    }
}
void Finish()
{
    A.bDone=true;TArray<TSharedPtr<FJsonValue>> Items;int32 Loops=0,Ignores=0;
    for(const FRow& R:A.Rows)
    {
        auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("path"),R.Path);O->SetBoolField(TEXT("missing"),R.bMissing);
        O->SetNumberField(TEXT("doneAt"),R.DoneAt);O->SetBoolField(TEXT("looping"),R.bLoop);O->SetBoolField(TEXT("ignoresDeactivate"),R.bIgnores);
        O->SetNumberField(TEXT("minZ"),R.MinZ>1e8f?0:FMath::RoundToInt(R.MinZ));O->SetNumberField(TEXT("maxZ"),R.MaxZ<-1e8f?0:FMath::RoundToInt(R.MaxZ));O->SetNumberField(TEXT("reach"),FMath::RoundToInt(R.Reach));
        Items.Add(MakeShared<FJsonValueObject>(O));Loops+=R.bLoop;Ignores+=R.bIgnores;
        UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_LOOP path=%s missing=%d done=%.2f loop=%d ignores=%d minz=%.0f maxz=%.0f reach=%.0f"),*R.Path,R.bMissing,R.DoneAt,R.bLoop,R.bIgnores,
            R.MinZ>1e8f?0.f:R.MinZ,R.MaxZ<-1e8f?0.f:R.MaxZ,R.Reach);
    }
    auto Root=MakeShared<FJsonObject>();Root->SetArrayField(TEXT("systems"),Items);FString Text;
    FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Text));FFileHelper::SaveStringToFile(Text,*A.Out);
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_LOOP_AUDIT_DONE systems=%d looping=%d ignoresDeactivate=%d out=%s"),A.Rows.Num(),Loops,Ignores,*A.Out);
    FPlatformMisc::RequestExitWithStatus(false,0);
}
bool Initialize(ACireGameMode* Mode)
{
    A={};if(!FString(FCommandLine::Get()).Contains(TEXT("-CireFabLoopAudit"))||!Mode||Mode->GetNetMode()!=NM_Standalone)return false;
    FParse::Value(FCommandLine::Get(),TEXT("CireFabLoopAudit="),A.Out);
    if(A.Out.IsEmpty())A.Out=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("FabLoopAudit.json")));
    FString Text;TSharedPtr<FJsonValue> Root;TSet<FString> Paths;
    if(FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/FabVFX.json")))&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root))CollectPaths(Root,Paths);
    TArray<FString> Sorted=Paths.Array();Sorted.Sort();for(const FString& P:Sorted){FRow R;R.Path=P;A.Rows.Add(R);}
    A.Mode=Mode;A.Start=FPlatformTime::Seconds();Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;
    UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_LOOP_AUDIT_START systems=%d"),A.Rows.Num());return true;
}
bool Tick(ACireGameMode* Mode)
{
    if(A.Mode.Get()!=Mode||!Mode)return false;
    if(A.bDone)return true;
    UWorld* World=Mode->GetWorld();
    if(FPlatformTime::Seconds()-A.Start>1800){Finish();return true;}
    if(A.BatchAt<0){if(FPlatformTime::Seconds()-A.Start>4)SpawnBatch(World);return true;}
    const float Age=World->GetTimeSeconds()-A.BatchAt;Measure(Age);
    if(A.Phase==0&&Age>=LoopProbeAt)
    {
        for(int32 I=A.BatchStart;I<A.BatchEnd;++I)if(UFXSystemComponent* C=A.Rows[I].C.Get()){A.Rows[I].bLoop=IsRunning(C);C->Deactivate();}
        A.Phase=1;
    }
    else if(A.Phase==1&&Age>=LoopProbeAt+DeactivateGrace)
    {
        for(int32 I=A.BatchStart;I<A.BatchEnd;++I)if(UFXSystemComponent* C=A.Rows[I].C.Get()){A.Rows[I].bIgnores=IsRunning(C);C->DestroyComponent();}
        if(A.Next<A.Rows.Num())SpawnBatch(World);else Finish();
    }
    return true;
}
}

bool CireFabVFXCatalog::Initialize(ACireGameMode* Mode)
{
    if(CireFabLoopAudit::Initialize(Mode)){G={};return true;} // vfx-loop-fix
    G={};FString List;
    if(!FParse::Value(FCommandLine::Get(),TEXT("CireFabVFXCatalog="),List))return false;
    G.Mode=Mode;G.Start=FPlatformTime::Seconds();
    FString Out;FParse::Value(FCommandLine::Get(),TEXT("CireFabVFXCatalogOut="),Out);
    G.Directory=Out.IsEmpty()?FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("FabVFXCatalog"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")))):Out;
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!LoadList(List)||!IFileManager::Get().MakeDirectory(*G.Directory,true)){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;return true;
}

bool CireFabVFXCatalog::Tick(ACireGameMode* Mode)
{
    if(CireFabLoopAudit::Tick(Mode))return true; // vfx-loop-fix
    if(G.Mode.Get()!=Mode)return false;
    if(G.bDone)return true;
    const double Now=FPlatformTime::Seconds();
    if(Now-G.Start>2400){Finish(false);return true;} // 40 min bound for ~550 systems
    if(G.Ready<0)
    {
        auto* PC=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());
        if(PC&&PC->GetPawn()&&PC->GetHUD()&&!Setup(Mode,PC))Finish(false);
        return true;
    }
    if(G.Index<0){if(Now-G.Ready>3)SpawnItem(0);return true;}
    ++G.Frames;
    if(G.Live.IsValid())G.MaxReach=FMath::Max(G.MaxReach,CireFabVFX::MeasureReach(G.Live.Get()));
    const double Age=Now-G.ItemAt;
    if(G.Stage==0&&Age>EarlyAt&&G.Frames>4){Capture(TEXT("early"));G.Stage=1;}
    else if(G.Stage==1&&Age>LateAt&&Now-G.CapturedAt>.3)
    {
        Capture(TEXT("late"));G.Stage=2;
        UE_LOG(LogTemp,Display,TEXT("CIRE_FAB_CATALOG_REACH index=%d reach=%.0f"),G.Index,G.MaxReach);
    }
    else if(G.Stage==2&&Now-G.CapturedAt>Rest)
    {
        if(G.Live.IsValid()){G.Live->DestroyComponent();G.Live=nullptr;}
        if(G.Index+1<G.Items.Num())SpawnItem(G.Index+1);
        else Finish(G.bChecks&&G.Captures>0);
    }
    return true;
}
#endif
