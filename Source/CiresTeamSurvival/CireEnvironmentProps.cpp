#include "CireEnvironmentProps.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireEnvironmentProps,Log,All);
namespace
{
struct FPropModel {FString Id,Path;float Radius=0;bool bCollision=false,bOptional=false;TWeakObjectPtr<UHierarchicalInstancedStaticMeshComponent> Component;};
struct FPropPlacement {FString Model,Anchor;FVector Offset=FVector::ZeroVector;float Yaw=0,Scale=1,Fraction=0;int32 Tier=0,Side=0;};
struct FPlaced {int32 Team=0;FVector Position=FVector::ZeroVector;float Radius=0;};
struct FWorldProps {TArray<FPropModel> Models;TArray<FPropPlacement> Placements;TArray<FPlaced> Checked;int32 Visible=0;};
TMap<TWeakObjectPtr<ACireWorld>,FWorldProps> Worlds;
bool Number(const TSharedPtr<FJsonObject>& O,const TCHAR* Key,float& Out,float Low,float High,float Default)
{
    double N=Default;if(O->HasField(Key)&&!O->TryGetNumberField(Key,N))return false;
    if(!FMath::IsFinite(N)||N<Low||N>High)return false;Out=static_cast<float>(N);return true;
}
bool Safe(const UWorld* World,int32 Team,FVector P,float Radius)
{
    const auto& R=CireLanePath::Get(World);const FVector2D Local(P.X,P.Y-CireLanePath::CenterY(Team));
    // Editable marching roads have a 205 cm half-width, plus capsule/steering clearance.
    for(int32 I=1;I<R.LocalPoints[Team].Num();++I)
    {
        const FVector2D A=R.LocalPoints[Team][I-1],B=R.LocalPoints[Team][I],D=B-A;
        const double T=FMath::Clamp(FVector2D::DotProduct(Local-A,D)/FMath::Max(1.,D.SizeSquared()),0.,1.);
        if(FVector2D::DistSquared(Local,A+D*T)<FMath::Square(Radius+285.f))return false;
    }
    if(FVector::DistSquared2D(P,CireLanePath::SpawnPosition(World,Team))<FMath::Square(Radius+340.f))return false;
    for(int32 Tier=1;Tier<=3;++Tier)if(FVector::DistSquared2D(P,CireLanePath::ChallengePosition(World,Team,Tier))<FMath::Square(Radius+260.f))return false;
    // Keep the defended entrance and its actual leak boundary unobstructed.
    if(FMath::Abs(P.X+1400)<Radius+160 && FMath::Abs(Local.Y)<900+Radius)return false;
    return true;
}
FVector Position(const UWorld* World,const FPropPlacement& P,int32 Team)
{
    const auto& R=CireLanePath::Get(World);const float Center=CireLanePath::CenterY(Team);
    FVector Result(0,Center,0);
    if(P.Anchor==TEXT("town"))Result.X=-2000;
    else if(P.Anchor==TEXT("challenge"))Result=CireLanePath::ChallengePosition(World,Team,P.Tier,0);
    else if(P.Anchor==TEXT("spawn"))Result=CireLanePath::SpawnPosition(World,Team,0);
    else if(P.Anchor==TEXT("perimeter"))Result=FVector(FMath::Lerp(100.f,R.MaxX-450.f,P.Fraction),Center+P.Side*(R.HalfWidth+150),0);
    else if(P.Anchor==TEXT("outer_skyline"))Result=FVector(FMath::Lerp(100.f,R.MaxX-450.f,P.Fraction),Center+(Team==0?-1:1)*(R.HalfWidth+570),0);
    return Result+P.Offset;
}
bool Read(FWorldProps& Out)
{
    FString Json;TSharedPtr<FJsonObject> Root;const TArray<TSharedPtr<FJsonValue>> *Models=nullptr,*Placements=nullptr;double Version=0;
    if(!FFileHelper::LoadFileToString(Json,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/EnvironmentPlacements.json")))||Json.Len()>262144||
       !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root||!Root->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1||
       !Root->TryGetArrayField(TEXT("models"),Models)||!Root->TryGetArrayField(TEXT("placements"),Placements)||Models->Num()>32||Placements->Num()>256)return false;
    TSet<FString> Seen;
    for(const auto& V:*Models)
    {
        const auto O=V->AsObject();FPropModel M;
        if(!O||!O->TryGetStringField(TEXT("id"),M.Id)||M.Id.IsEmpty()||Seen.Contains(M.Id)||!O->TryGetStringField(TEXT("mesh"),M.Path)||
           !M.Path.StartsWith(TEXT("/Game/"))||M.Path.Contains(TEXT(".."))||!Number(O,TEXT("radiusCm"),M.Radius,10,1500,80))return false;
        O->TryGetBoolField(TEXT("collision"),M.bCollision);O->TryGetBoolField(TEXT("optional"),M.bOptional);Seen.Add(M.Id);Out.Models.Add(MoveTemp(M));
    }
    for(const auto& V:*Placements)
    {
        const auto O=V->AsObject();FPropPlacement P;float X=0,Y=0,Z=0,Tier=0,Side=0;
        if(!O||!O->TryGetStringField(TEXT("model"),P.Model)||!Seen.Contains(P.Model)||!O->TryGetStringField(TEXT("anchor"),P.Anchor)||
           !(P.Anchor==TEXT("town")||P.Anchor==TEXT("challenge")||P.Anchor==TEXT("spawn")||P.Anchor==TEXT("perimeter")||P.Anchor==TEXT("outer_skyline")||P.Anchor==TEXT("lane"))||
           !Number(O,TEXT("x"),X,-30000,30000,0)||!Number(O,TEXT("y"),Y,-5000,5000,0)||!Number(O,TEXT("z"),Z,-20,2000,0)||
           !Number(O,TEXT("scale"),P.Scale,.1f,8,1)||!Number(O,TEXT("yaw"),P.Yaw,-360,360,0)||
           !Number(O,TEXT("fraction"),P.Fraction,0,1,0)||!Number(O,TEXT("tier"),Tier,0,3,0)||!Number(O,TEXT("side"),Side,-1,1,0))return false;
        P.Offset=FVector(X,Y,Z);P.Tier=FMath::RoundToInt(Tier);P.Side=FMath::RoundToInt(Side);
        if((P.Anchor==TEXT("challenge")&&(P.Tier<1||P.Tier>3))||(P.Anchor==TEXT("perimeter")&&P.Side==0))return false;
        Out.Placements.Add(MoveTemp(P));
    }
    return !Out.Models.IsEmpty()&&!Out.Placements.IsEmpty();
}
}
void CireEnvironmentProps::Build(ACireWorld* WorldActor)
{
    if(!IsValid(WorldActor))return;
    for(auto I=Worlds.CreateIterator();I;++I)if(!I.Key().IsValid())I.RemoveCurrent();
    FWorldProps New;if(!Read(New)){UE_LOG(LogCireEnvironmentProps,Warning,TEXT("Environment placement data invalid; authored props not spawned."));return;}
    if(auto* Existing=Worlds.Find(WorldActor))for(auto& M:Existing->Models)if(M.Component.IsValid())M.Component->DestroyComponent();
    for(auto& M:New.Models)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*M.Path);
        if(!Mesh){if(!M.bOptional)UE_LOG(LogCireEnvironmentProps,Warning,TEXT("Environment mesh is not built: %s"),*M.Path);continue;}
        auto* Component=NewObject<UHierarchicalInstancedStaticMeshComponent>(WorldActor,*FString::Printf(TEXT("Prop_%s"),*M.Id));
        Component->SetupAttachment(WorldActor->GetRootComponent());Component->SetStaticMesh(Mesh);Component->SetMobility(EComponentMobility::Movable);
        Component->SetCollisionObjectType(ECC_WorldStatic);Component->SetCollisionResponseToAllChannels(ECR_Block);
        Component->SetCollisionEnabled(M.bCollision?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
        Component->SetGenerateOverlapEvents(false);Component->SetCanEverAffectNavigation(M.bCollision);
        Component->ComponentTags.Add(TEXT("CireWorldProp"));Component->ComponentTags.Add(FName(*M.Id));
        WorldActor->AddInstanceComponent(Component);Component->RegisterComponent();M.Component=Component;
    }
    Worlds.Add(WorldActor,MoveTemp(New));Refresh(WorldActor);
}
void CireEnvironmentProps::Refresh(ACireWorld* WorldActor)
{
    auto* Data=Worlds.Find(WorldActor);if(!Data)return;Data->Visible=0;Data->Checked.Reset();
    for(auto& M:Data->Models)if(M.Component.IsValid())M.Component->ClearInstances();
    int32 Skipped=0;
    for(const auto& P:Data->Placements)
    {
        auto* M=Data->Models.FindByPredicate([&](const FPropModel& V){return V.Id==P.Model;});if(!M||!M->Component.IsValid())continue;
        for(int32 Team=0;Team<2;++Team)
        {
            const FVector Location=Position(WorldActor->GetWorld(),P,Team);const float Radius=M->Radius*P.Scale;
            if(!Safe(WorldActor->GetWorld(),Team,Location,Radius)){++Skipped;continue;}
            M->Component->AddInstance(FTransform(FRotator(0,P.Yaw,0),Location,FVector(P.Scale)),true);
            Data->Checked.Add({Team,Location,Radius});++Data->Visible;
        }
    }
    UE_LOG(LogCireEnvironmentProps,Display,TEXT("CIRE_ENVIRONMENT_PROPS_READY instances=%d suppressed_for_route_clearance=%d models=%d"),Data->Visible,Skipped,Data->Models.Num());
}
int32 CireEnvironmentProps::InstanceCount(const ACireWorld* WorldActor)
{const auto* Data=Worlds.Find(TWeakObjectPtr<ACireWorld>(const_cast<ACireWorld*>(WorldActor)));return Data?Data->Visible:0;}
bool CireEnvironmentProps::HasSafeClearance(const ACireWorld* WorldActor)
{
    const auto* Data=Worlds.Find(TWeakObjectPtr<ACireWorld>(const_cast<ACireWorld*>(WorldActor)));if(!Data)return false;
    for(const auto& P:Data->Checked)if(!Safe(WorldActor->GetWorld(),P.Team,P.Position,P.Radius))return false;
    return true;
}
