#include "CireSelection.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
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
#include "Camera/PlayerCameraManager.h"
#include "EngineUtils.h"

namespace
{
struct FTabState
{
    TArray<TWeakObjectPtr<AActor>> History;
    double LastTab = -100.0;
    TWeakObjectPtr<AActor> LastHostile;
    TWeakObjectPtr<AActor> ClearRequested;
};
TMap<TWeakObjectPtr<ACireController>,FTabState> TabStates;

bool IsLivingUnit(const AActor* Actor)
{
    if(!IsValid(Actor)||Actor->IsActorBeingDestroyed())return false;
    if(const auto* Hero=Cast<ACireHero>(Actor))return !Hero->bDead&&Hero->Health>0;
    if(const auto* Monster=Cast<ACireMonster>(Actor))return Monster->Health>0;
    if(const auto* Construct=Cast<ACireConstruct>(Actor))return Construct->Health>0;
    return false;
}
}


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
    TabStates.Remove(Key);
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


AActor* CireSelection::NextTarget(ACireController* Controller,bool bFriendly,bool bReverse,const FTransform* ViewOverride)
{
    auto* Self=Controller?Cast<ACireHero>(Controller->GetPawn()):nullptr;
    if(!Self||!Controller->GetWorld())return nullptr;
    const FVector Origin=Self->GetActorLocation();
    TArray<AActor*> All;
    const auto Consider=[&](AActor* Actor)
    {
        if(!IsLivingUnit(Actor)||Actor==Self||Actor->IsHidden()||!CireRealm::CanObserve(Self,Actor)||!Self->InRange(Actor,TabRange))return;
        All.Add(Actor);
    };
    if(bFriendly)
    {
        for(TCireActorIterator<ACireHero> It(Controller->GetWorld());It;++It)if(It->TeamId==Self->TeamId)Consider(*It);
    }
    else
    {
        for(TCireActorIterator<ACireMonster> It(Controller->GetWorld());It;++It)if(Self->IsHostile(*It))Consider(*It);
        for(TCireActorIterator<ACireHero> It(Controller->GetWorld());It;++It)if(Self->IsHostile(*It))Consider(*It);
    }
    All.Sort([&Origin](const AActor& A,const AActor& B){return FVector::DistSquared(Origin,A.GetActorLocation())<FVector::DistSquared(Origin,B.GetActorLocation());});
    if(All.IsEmpty())return nullptr;
    AActor* Current=Self->Target;
    if(bFriendly)
    {
        const int32 I=All.IndexOfByKey(Current);
        return All[bReverse?(I<=0?All.Num()-1:I-1):(I+1)%All.Num()];
    }
    // Candidates in front of the camera (horizontal cone slightly wider than the view).
    TArray<AActor*> Front;
    const auto* View=Controller->PlayerCameraManager.Get();
    if(View||ViewOverride)
    {
        const FVector Eye=ViewOverride?ViewOverride->GetLocation():View->GetCameraLocation();
        const FVector Forward=(ViewOverride?ViewOverride->GetRotation().Rotator():View->GetCameraRotation()).Vector().GetSafeNormal2D();
        const float Half=FMath::Min(85.f,(View?View->GetFOVAngle():80.f)*.5f+15.f);const float MinDot=FMath::Cos(FMath::DegreesToRadians(Half));
        for(AActor* Actor:All)
        {
            const FVector To=(Actor->GetActorLocation()-Eye).GetSafeNormal2D();
            if(To.IsNearlyZero()||FVector::DotProduct(To,Forward)>=MinDot)Front.Add(Actor);
        }
    }
    const TArray<AActor*>& Pool=Front.IsEmpty()?All:Front;
    auto& State=TabStates.FindOrAdd(TWeakObjectPtr<ACireController>(Controller));
    const double Now=Controller->GetWorld()->GetRealTimeSeconds();
    State.History.RemoveAll([](const TWeakObjectPtr<AActor>& A){return !IsLivingUnit(A.Get());});
    const bool bFresh=Now-State.LastTab>TabHistorySeconds||!IsValid(Current)||State.History.IsEmpty()||State.History.Last().Get()!=Current;
    if(bFresh){State.History.Reset();if(IsLivingUnit(Current)&&Self->IsHostile(Current))State.History.Add(Current);}
    State.LastTab=Now;
    AActor* Pick=nullptr;
    if(bReverse)
    {
        // Walk back through the tab chain, then continue from the farthest candidate.
        while(State.History.Num()>1&&!Pick)
        {
            State.History.Pop();AActor* Previous=State.History.Last().Get();
            if(Pool.Contains(Previous)||All.Contains(Previous))Pick=Previous;
        }
        if(!Pick)for(int32 I=Pool.Num()-1;I>=0&&!Pick;--I)if(Pool[I]!=Current)Pick=Pool[I];
        if(Pick&&(State.History.IsEmpty()||State.History.Last().Get()!=Pick))State.History.Add(Pick);
        return Pick?Pick:Current;
    }
    for(AActor* Actor:Pool)if(Actor!=Current&&!State.History.Contains(Actor)){Pick=Actor;break;}
    if(!Pick)
    {
        // Everything in front was visited: restart the cycle from the nearest.
        State.History.Reset();if(IsValid(Current))State.History.Add(Current);
        for(AActor* Actor:Pool)if(Actor!=Current){Pick=Actor;break;}
    }
    if(Pick)State.History.Add(Pick);
    return Pick?Pick:(IsValid(Current)?Current:Pool[0]);
}

void CireSelection::HandleTargetLoss(ACireController* Controller,bool bAutoReacquire)
{
    auto* Self=Controller?Cast<ACireHero>(Controller->GetPawn()):nullptr;
    if(!Self)return;
    auto& State=TabStates.FindOrAdd(TWeakObjectPtr<ACireController>(Controller));
    AActor* Target=Self->Target;
    const bool bHostileAlive=IsLivingUnit(Target)&&Self->IsHostile(Target);
    if(bHostileAlive){State.LastHostile=Target;State.ClearRequested.Reset();return;}
    const bool bDeadTarget=IsValid(Target)&&!IsLivingUnit(Target);
    // A hostile target died (still replicated as dead) or was destroyed since last frame.
    const bool bLost=bDeadTarget||(!IsValid(Target)&&State.LastHostile.IsStale(true));
    // Living non-hostile selections (allies, self) and manual clears end the hostile watch.
    if(!bLost){State.LastHostile.Reset();return;}
    // Dead allies (and yourself) stay selected; only hostile losses clear or reacquire.
    const bool bWasHostile=bDeadTarget?(Cast<ACireMonster>(Target)||Cast<ACireConstruct>(Target)||(Cast<ACireHero>(Target)&&Cast<ACireHero>(Target)->TeamId!=Self->TeamId)):true;
    State.LastHostile.Reset();
    if(!bWasHostile)return;
    if(bAutoReacquire&&bWasHostile&&Self->bDrafted&&!Self->bDead)
    {
        State.History.Reset();State.LastTab=-100;
        if(AActor* Next=NextTarget(Controller,false,false);Next&&Next!=Target){Controller->ServerAction(0,0,Next);State.ClearRequested.Reset();return;}
    }
    if(bDeadTarget&&State.ClearRequested.Get()!=Target){State.ClearRequested=Target;Controller->ServerAction(6,0,nullptr);}
}

AActor* CireSelection::UnitUnderCursor(ACireController* Controller)
{
    auto* Self=Controller?Cast<ACireHero>(Controller->GetPawn()):nullptr;if(!Self)return nullptr;
    FHitResult Hit;
    if(!Controller->GetHitResultUnderCursorByChannel(UEngineTypes::ConvertToTraceType(ECC_GameTraceChannel1),false,Hit))return nullptr;
    AActor* A=Hit.GetActor();
    return (Cast<ACireHero>(A)||Cast<ACireMonster>(A)||Cast<ACireConstruct>(A))&&IsLivingUnit(A)&&CireRealm::CanObserve(Self,A)?A:nullptr;
}
AActor* CireSelection::BestHostile(ACireController* Controller,float Range,AActor* UnderCursor,const FTransform* ViewOverride)
{
    auto* Self=Controller?Cast<ACireHero>(Controller->GetPawn()):nullptr;if(!Self||!Controller->GetWorld())return nullptr;
    Range=FMath::Max(Range,150.f);
    if(IsLivingUnit(UnderCursor)&&Self->IsHostile(UnderCursor)&&Self->InRange(UnderCursor,Range))return UnderCursor;
    TArray<AActor*> All;
    const auto Consider=[&](AActor* A){if(IsLivingUnit(A)&&!A->IsHidden()&&Self->IsHostile(A)&&CireRealm::CanObserve(Self,A)&&Self->InRange(A,Range))All.Add(A);};
    for(TCireActorIterator<ACireMonster> It(Controller->GetWorld());It;++It)Consider(*It);
    for(TCireActorIterator<ACireHero> It(Controller->GetWorld());It;++It)Consider(*It);
    if(All.IsEmpty())return nullptr;
    const FVector Origin=Self->GetActorLocation();
    All.Sort([&Origin](const AActor& A,const AActor& B){return FVector::DistSquared(Origin,A.GetActorLocation())<FVector::DistSquared(Origin,B.GetActorLocation());});
    const auto* View=Controller->PlayerCameraManager.Get();
    if(View||ViewOverride)
    {
        const FVector Eye=ViewOverride?ViewOverride->GetLocation():View->GetCameraLocation();
        const FVector Forward=(ViewOverride?ViewOverride->GetRotation().Rotator():View->GetCameraRotation()).Vector().GetSafeNormal2D();
        const float MinDot=FMath::Cos(FMath::DegreesToRadians(FMath::Min(85.f,(View?View->GetFOVAngle():80.f)*.5f+15.f)));
        for(AActor* A:All){const FVector To=(A->GetActorLocation()-Eye).GetSafeNormal2D();if(FVector::DotProduct(To,Forward)>=MinDot)return A;}
    }
    return All[0];
}
