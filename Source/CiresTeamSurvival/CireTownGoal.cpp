#include "CireTownGoal.h"
#include "CireGame.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

ACireTownGoal::ACireTownGoal()
{
    bReplicates=true;
    bAlwaysRelevant=true;
    GoalVolume=CreateDefaultSubobject<UBoxComponent>(TEXT("TownDefenseZone"));
    SetRootComponent(GoalVolume);
    GoalVolume->InitBoxExtent(FVector(450,900,250));
    GoalVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    GoalVolume->SetCollisionObjectType(ECC_WorldDynamic);
    GoalVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
    GoalVolume->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
    GoalVolume->SetGenerateOverlapEvents(true);
    GoalVolume->SetHiddenInGame(true);
}

void ACireTownGoal::BeginPlay()
{
    Super::BeginPlay();
    if(HasAuthority())GoalVolume->OnComponentBeginOverlap.AddDynamic(this,&ACireTownGoal::OnGoalOverlap);
}

void ACireTownGoal::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireTownGoal,TeamId);
}

bool ACireTownGoal::ContainsLocation(const FVector& Location) const
{
    if(!GoalVolume)return false;
    const FVector Local=GoalVolume->GetComponentTransform().InverseTransformPosition(Location);
    const FVector Extent=GoalVolume->GetUnscaledBoxExtent();
    return FMath::Abs(Local.X)<=Extent.X&&FMath::Abs(Local.Y)<=Extent.Y&&FMath::Abs(Local.Z)<=Extent.Z;
}

void ACireTownGoal::OnGoalOverlap(UPrimitiveComponent*,AActor* OtherActor,UPrimitiveComponent*,int32,bool,const FHitResult&)
{
    if(!HasAuthority())return;
    auto* Monster=Cast<ACireMonster>(OtherActor);
    auto* Mode=GetWorld()->GetAuthGameMode<ACireGameMode>();
    if(!Monster||!Mode||Monster->Lane!=TeamId||Monster->PackId>=0||Monster->Health<=0||Monster->IsActorBeingDestroyed())return;
    if(Mode->Clock.Phase()!=Cires::MatchPhase::Survival)return;
    Mode->Leak(Monster);
}
