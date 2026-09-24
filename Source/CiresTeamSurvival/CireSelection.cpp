#include "CireSelection.h"
#include "CireGame.h"
#include "CireConstruct.h"
#include "CireRealm.h"
#include "CireChampionArt.h"
#include "CireNPCCombat.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
struct FSelectionState
{
    TWeakObjectPtr<AActor> Ring;
    TWeakObjectPtr<UInstancedStaticMeshComponent> Segments;
    TWeakObjectPtr<UInstancedStaticMeshComponent> RangeSegments;
    float LastRange=-1;
    TWeakObjectPtr<AActor> Selected;
    TWeakObjectPtr<UMeshComponent> SelectedMesh;
    TWeakObjectPtr<UMaterialInterface> PreviousOverlay;
    TWeakObjectPtr<UMaterialInstanceDynamic> Highlight;
};
TMap<TWeakObjectPtr<ACireController>,FSelectionState> Selections;

void Restore(FSelectionState& State)
{
    if(State.SelectedMesh.IsValid()&&State.SelectedMesh->GetOverlayMaterial()==State.Highlight.Get())
        State.SelectedMesh->SetOverlayMaterial(State.PreviousOverlay.Get());
    State.Selected.Reset();State.SelectedMesh.Reset();State.PreviousOverlay.Reset();State.Highlight.Reset();
    if(State.Ring.IsValid())State.Ring->SetActorHiddenInGame(true);
}

void CreateRing(ACireController* Controller,FSelectionState& State)
{
    FActorSpawnParameters Params;Params.Owner=Controller;Params.ObjectFlags|=RF_Transient;
    AActor* Ring=Controller->GetWorld()->SpawnActor<AActor>(AActor::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
    if(!Ring)return;
    Ring->SetReplicates(false);
    auto* Segments=NewObject<UInstancedStaticMeshComponent>(Ring,TEXT("SelectionRingSegments"));
    Ring->SetRootComponent(Segments);Ring->AddInstanceComponent(Segments);
    Segments->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Segments->SetCollisionEnabled(ECollisionEnabled::NoCollision);Segments->SetGenerateOverlapEvents(false);
    Segments->SetCastShadow(false);Segments->SetCanEverAffectNavigation(false);Segments->RegisterComponent();
    constexpr int32 Count=64;constexpr float Radius=54.f;
    for(int32 I=0;I<Count;++I)
    {
        const float A=2*PI*I/Count;
        Segments->AddInstance(FTransform(FRotator(0,FMath::RadiansToDegrees(A)+90,0),
            FVector(FMath::Cos(A)*Radius,FMath::Sin(A)*Radius,0),FVector(.057f,.035f,.018f)));
    }
    State.Ring=Ring;State.Segments=Segments;
    auto* Range=NewObject<UInstancedStaticMeshComponent>(Ring,TEXT("SelectedAttackRange"));
    Ring->AddInstanceComponent(Range);Range->SetupAttachment(Segments);
    Range->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
    Range->SetCollisionEnabled(ECollisionEnabled::NoCollision);Range->SetGenerateOverlapEvents(false);
    Range->SetCastShadow(false);Range->SetCanEverAffectNavigation(false);Range->SetAbsolute(false,false,true);Range->RegisterComponent();
    State.RangeSegments=Range;
}
}

void CireSelection::Cleanup(ACireController* Controller)
{
    const TWeakObjectPtr<ACireController> Key(Controller);
    if(auto* State=Selections.Find(Key)){Restore(*State);if(State->Ring.IsValid())State->Ring->Destroy();Selections.Remove(Key);}
}

void CireSelection::Update(ACireController* Controller)
{
    if(!IsValid(Controller)||!Controller->IsLocalController()||Controller->GetNetMode()==NM_DedicatedServer)return;
    for(auto It=Selections.CreateIterator();It;++It)if(!It.Key().IsValid()){Restore(It.Value());if(It.Value().Ring.IsValid())It.Value().Ring->Destroy();It.RemoveCurrent();}
    auto& Visual=Selections.FindOrAdd(TWeakObjectPtr<ACireController>(Controller));
    auto* Self=Cast<ACireHero>(Controller->GetPawn());auto* GS=Controller->GetWorld()->GetGameState<ACireGameState>();
    AActor* Target=Self?Self->Target:nullptr;bool Friendly=false;
    if(auto* Hero=Cast<ACireHero>(Target))
    {
        Friendly=Self&&Hero->TeamId==Self->TeamId;
        if(Hero->bDead||Hero->Health<=0||(!Friendly&&(!GS||GS->Phase!=2)))Target=nullptr;
    }
    else if(auto* Monster=Cast<ACireMonster>(Target))
    {
        if(Monster->Health<=0||!Self||Monster->Lane!=Self->TeamId||(GS&&GS->Phase==2))Target=nullptr;
    }
    else if(auto* Construct=Cast<ACireConstruct>(Target)){Friendly=Self&&Construct->OriginTeam==Self->TeamId;if(Construct->Health<=0||!CireRealm::CanObserve(Self,Construct))Target=nullptr;}
    else Target=nullptr;
    if(!IsValid(Target)||Target->IsActorBeingDestroyed()||Target->IsHidden()){Restore(Visual);return;}
    UMeshComponent* TargetMesh=nullptr;
    if(auto* Character=Cast<ACharacter>(Target))TargetMesh=Character->GetMesh();
    if(auto* Hero=Cast<ACireHero>(Target);Hero&&Hero->ChampionArt&&Hero->ChampionArt->GetVisualMesh())TargetMesh=Hero->ChampionArt->GetVisualMesh();
    else if(auto* Construct=Cast<ACireConstruct>(Target))TargetMesh=Construct->BodyMesh;
    if(!TargetMesh){Restore(Visual);return;}
    const FLinearColor Tint=Friendly?FLinearColor(.05f,1.8f,1.35f,1):FLinearColor(2.1f,.12f,.035f,1);
    if(Visual.Selected.Get()!=Target||Visual.SelectedMesh.Get()!=TargetMesh)
    {
        Restore(Visual);Visual.Selected=Target;Visual.SelectedMesh=TargetMesh;Visual.PreviousOverlay=TargetMesh->GetOverlayMaterial();
        if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Art/Materials/M_SelectionEdge.M_SelectionEdge")))
        {
            auto* MID=UMaterialInstanceDynamic::Create(Material,Controller);
            MID->SetVectorParameterValue(TEXT("SelectionTint"),Tint);TargetMesh->SetOverlayMaterial(MID);Visual.Highlight=MID;
        }
    }
    if(!Visual.Ring.IsValid())CreateRing(Controller,Visual);
    if(!Visual.Ring.IsValid()||!Visual.Segments.IsValid())return;
    Visual.Ring->SetActorHiddenInGame(false);
    float Radius=60,HalfHeight=0;
    if(auto* Character=Cast<ACharacter>(Target)){Radius=Character->GetCapsuleComponent()->GetScaledCapsuleRadius()+14.f;HalfHeight=Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();}
    else if(auto* Construct=Cast<ACireConstruct>(Target)){Radius=FMath::Max(Construct->ConstructSpec.Width,Construct->ConstructSpec.Depth)*.55f;HalfHeight=Construct->ConstructSpec.Height*.5f;}
    Visual.Ring->SetActorScale3D(FVector(Radius/54.f,Radius/54.f,1));
    Visual.Ring->SetActorLocation(Target->GetActorLocation()-FVector(0,0,HalfHeight-4.f));
    Visual.Segments->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Friendly?TEXT("/Game/Art/Materials/M_Ember.M_Ember"):TEXT("/Game/Art/Materials/M_Dusk.M_Dusk")));
    if(Visual.RangeSegments.IsValid())
    {
        float Range=0;
        if(auto* Hero=Cast<ACireHero>(Target))Range=Hero->BasicAttackRange();
        else if(auto* Monster=Cast<ACireMonster>(Target))Range=Monster->CombatArchetype>=2?650.f:170.f;
        Visual.RangeSegments->SetWorldScale3D(FVector(1));
        if(!FMath::IsNearlyEqual(Range,Visual.LastRange))
        {
            Visual.LastRange=Range;Visual.RangeSegments->ClearInstances();
            for(int32 I=0;Range>0&&I<96;++I)
            {
                const float Angle=2*PI*I/96;
                Visual.RangeSegments->AddInstance(FTransform(FRotator(0,FMath::RadiansToDegrees(Angle)+90,0),
                    FVector(FMath::Cos(Angle)*Range,FMath::Sin(Angle)*Range,1),FVector(FMath::Max(.035f,Range*2*PI/96/100*.65f),.019f,.012f)));
            }
        }
        Visual.RangeSegments->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,Friendly?TEXT("/Game/Art/Materials/M_Ember.M_Ember"):TEXT("/Game/Art/Materials/M_Dusk.M_Dusk")));
    }
    if(Visual.Highlight.IsValid())Visual.Highlight->SetVectorParameterValue(TEXT("SelectionTint"),Tint);
}

