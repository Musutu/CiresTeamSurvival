#include "CireMobility.h"
#include "CireGame.h"

#if !UE_BUILD_SHIPPING
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/RootMotionSource.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireMovementTests, Log, All);
#endif

bool CireMovement::RunSmoke(ACireGameMode* Mode)
{
#if UE_BUILD_SHIPPING
    return false;
#else
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool Passed=true; int32 Checks=0;
    auto Check=[&](bool Condition,const TCHAR* Name)
    {
        ++Checks;
        if(!Condition){Passed=false;UE_LOG(LogCireMovementTests,Error,TEXT("CIRE_MOVEMENT_CHECK_FAIL %s"),Name);}
    };
    const auto SavedTuning=Tuning(); const auto SavedClock=Mode->Clock;
    const auto SavedHeroes=Mode->Heroes; const auto SavedMonsters=Mode->Monsters;
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        for(int32 I=Actors.Num()-1;I>=0;--I)if(IsValid(Actors[I]))Actors[I]->Destroy();
        Mode->Clock=SavedClock;Mode->Heroes=SavedHeroes;Mode->Monsters=SavedMonsters;
        FString Ignore;Apply(SavedTuning,Ignore);
    };
    Mode->Clock=Cires::MatchClock(); Mode->Heroes.Reset(); Mode->Monsters.Reset();
    FCireMovementTuning V; FString Error;
    Check(Apply(V,Error),TEXT("known finite movement tuning accepted"));
    auto Bad=V;Bad.InvulnerableEnd=V.RollDuration+.1f;
    Check(!Apply(Bad,Error)&&Tuning().InvulnerableEnd==V.InvulnerableEnd,TEXT("invalid invulnerability rejected transactionally"));
    Bad=V;Bad.RollEnergy=-1;
    Check(!Apply(Bad,Error)&&Tuning().RollEnergy==V.RollEnergy,TEXT("negative resource cost rejected"));

    UWorld* World=Mode->GetWorld();const FVector Ground(2000,-2100,3000);
    auto* Floor=World->SpawnActor<AActor>();
    if(Floor)
    {
        Actors.Add(Floor);auto* Box=NewObject<UBoxComponent>(Floor);
        Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(1200,800,50));
        Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Floor->SetActorLocation(Ground-FVector(0,0,50));
    }
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Hero=World->SpawnActor<ACireHero>(Ground+FVector(0,0,94),FRotator::ZeroRotator,Params);
    auto* Enemy=World->SpawnActor<ACireMonster>(Ground+FVector(-500,0,94),FRotator::ZeroRotator,Params);
    if(Hero)Actors.Add(Hero);if(Enemy)Actors.Add(Enemy);
    Check(Floor&&Hero&&Enemy&&Hero->Mobility,TEXT("isolated legal-lane floor and actors spawned"));
    if(!Floor||!Hero||!Enemy||!Hero->Mobility)
    {UE_LOG(LogCireMovementTests,Error,TEXT("CIRE_MOVEMENT_FAIL checks=%d"),Checks);return false;}
    Hero->SetActorTickEnabled(false);Enemy->SetActorTickEnabled(false);
    Hero->TeamId=0;Hero->bDrafted=true;Hero->Health=Hero->MaxHealth=1000;Hero->Skills.Reset();Hero->ShieldUntil=0;
    Enemy->Lane=0;Enemy->Health=Enemy->MaxHealth=1000;
    Mode->Heroes.Add(Hero);Mode->Monsters.Add(Enemy);
    auto* Mobility=Hero->Mobility.Get();auto* Move=Hero->GetCharacterMovement();
    Move->bRunPhysicsWithNoController=true;Move->SetMovementMode(MOVE_Walking);
    Move->TickComponent(.01f,LEVELTICK_All,nullptr);
    Check(Move->IsMovingOnGround()&&Move->CurrentFloor.IsWalkableFloor(),TEXT("real collision floor supports character movement"));
    Mobility->ServerSetWalk(true);Mobility->ServerSetStrafe(true);
    Check(Mobility->bWalking&&Mobility->bStrafing&&Mobility->MovementSpeed(false)==V.WalkSpeed&&
        FMath::IsNearlyEqual(Mobility->MovementSpeed(true),V.WalkSpeed*.65f),TEXT("walk and strafe RPC state with slowed walk speed"));
    Mobility->ServerSetWalk(false);Mobility->ServerSetStrafe(false);
    Check(Mobility->MovementSpeed(false)==V.RunSpeed,TEXT("run toggle restores run speed"));
    Hero->Energy=V.RollEnergy-1;
    Check(!Mobility->StartRoll(FVector::ForwardVector)&&Hero->Energy==V.RollEnergy-1,TEXT("insufficient energy denies roll without charging"));
    Hero->Energy=100;Hero->PendingAttackTarget=Enemy;
    Check(Mobility->StartRoll(FVector(4,0,3)),TEXT("grounded authoritative dodge starts"));
    Check(Hero->Energy==100-V.RollEnergy&&Mobility->RollDirection.Equals(FVector::ForwardVector)&&
        !Hero->PendingAttackTarget.IsValid()&&Hero->GlobalCooldown>=V.RollDuration,TEXT("roll charges once, normalizes horizontal direction, interrupts attack"));
    Check(Mobility->IsRolling()&&!Mobility->IsInvulnerable()&&!Hero->CanJump(),TEXT("windup is vulnerable and roll blocks jumping"));
    const float EnergyAfter=Hero->Energy;
    Check(!Mobility->StartRoll(FVector::RightVector)&&Hero->Energy==EnergyAfter,TEXT("repeat roll cannot bypass active roll or double-charge"));
    Check(CireCombat::ApplyDamage(Enemy,Hero,50,TEXT("Windup hit"))==50,TEXT("real combat damage applies during vulnerable windup"));
    auto SetAge=[&](float Age)
    {
        Mobility->RollStartedAt=Mobility->Now()-Age;
        Mobility->InvulnerableFrom=Mobility->RollStartedAt+V.InvulnerableStart;
        Mobility->InvulnerableUntil=Mobility->RollStartedAt+V.InvulnerableEnd;
    };
    SetAge((V.InvulnerableStart+V.InvulnerableEnd)*.5f);const float Before=Hero->Health;
    Check(Mobility->IsInvulnerable()&&CireCombat::ApplyDamage(Enemy,Hero,50,TEXT("Dodge window"))==0&&Hero->Health==Before,
        TEXT("actual damage pipeline rejects hit only inside dodge window"));
    SetAge(V.InvulnerableEnd+.03f);
    Check(!Mobility->IsInvulnerable()&&CireCombat::ApplyDamage(Enemy,Hero,50,TEXT("Recovery hit"))==50,
        TEXT("recovery remains vulnerable while roll continues"));
    Mobility->CancelRoll();
    Check(!Mobility->IsRolling()&&!Mobility->IsInvulnerable()&&Mobility->CooldownRemaining()>0&&
        !Mobility->StartRoll(FVector::ForwardVector)&&Hero->Energy==EnergyAfter,TEXT("cancel preserves cooldown and cannot refund/restart roll"));
    Mobility->ReadyAt=0;Move->SetMovementMode(MOVE_Falling);
    Check(!Mobility->StartRoll(FVector::ForwardVector),TEXT("airborne roll rejected"));
    Move->SetMovementMode(MOVE_Walking);Hero->bDead=true;
    Check(!Mobility->StartRoll(FVector::ForwardVector),TEXT("dead hero cannot roll"));Hero->bDead=false;

    // Let CharacterMovement discard the cancelled source before looking up its replacement.
    Move->TickComponent(.001f,LEVELTICK_All,nullptr);Move->StopMovementImmediately();
    // Exercise the real delayed multicast implementation and the engine's RMS identity check.
    const float DelayedStart=Mobility->Now()-.1f;
    Mobility->MulticastRoll_Implementation(DelayedStart,V.RollDuration,FVector::ForwardVector,V.RollSpeed);
    auto Delayed=Move->GetRootMotionSource(TEXT("CireDodgeRoll"));
    FRootMotionSource_ConstantForce ServerIdentity;ServerIdentity.InstanceName=TEXT("CireDodgeRoll");
    ServerIdentity.Priority=600;ServerIdentity.AccumulateMode=ERootMotionAccumulateMode::Override;
    ServerIdentity.Duration=V.RollDuration;ServerIdentity.Force=FVector::ForwardVector*V.RollSpeed;
    Check(Delayed&&Delayed->Matches(&ServerIdentity)&&FMath::IsNearlyEqual(Delayed->GetTime(),.1f,.005f),
        TEXT("delayed multicast retains server RMS identity and starts at elapsed time"));
    Mobility->CancelRoll();Move->TickComponent(.001f,LEVELTICK_All,nullptr);Move->StopMovementImmediately();

    FCireConstructSpec WallSpec;WallSpec.CastRange=750;
    auto* Wall=ACireConstruct::Spawn(Hero,WallSpec,Ground+FVector(180,0,0),FRotator::ZeroRotator,TEXT("Movement fixture wall"));
    if(Wall){Actors.Add(Wall);Wall->SetActorTickEnabled(false);}
    Check(Wall!=nullptr,TEXT("destructible gameplay wall validates on test floor"));
    Hero->SetActorLocation(Ground+FVector(0,0,94),false,nullptr,ETeleportType::TeleportPhysics);
    Move->SetMovementMode(MOVE_Walking);Move->bForceNextFloorCheck=true;Move->TickComponent(.01f,LEVELTICK_All,nullptr);
    Mobility->ReadyAt=0;Hero->Energy=100;
    const float StartX=Hero->GetActorLocation().X;
    Check(Mobility->StartRoll(FVector::ForwardVector),TEXT("wall collision test starts real root motion"));
    for(int32 I=0;I<8;++I)Move->TickComponent(.05f,LEVELTICK_All,nullptr);
    const float Moved=Hero->GetActorLocation().X-StartX;
    Check(Wall&&Moved>25&&Moved<145,TEXT("root-motion roll advances but capsule cannot tunnel through constructed wall"));
    const float ReadyBeforeRevive=Mobility->ReadyAt;
    Hero->ReviveAt(Ground+FVector(-300,0,94));
    auto Remaining=Move->GetRootMotionSource(TEXT("CireDodgeRoll"));
    Check(!Mobility->IsRolling()&&!Mobility->IsInvulnerable()&&
        (!Remaining||Remaining->Status.HasFlag(ERootMotionSourceStatusFlags::MarkedForRemoval))&&Mobility->ReadyAt==ReadyBeforeRevive,
        TEXT("revive cancels root motion and invulnerability without resetting dodge cooldown"));
    UE_LOG(LogCireMovementTests,Display,TEXT("CIRE_MOVEMENT_%s checks=%d wall_travel=%.1f controls=E_jump_Ctrl_roll_CapsLock_walk_RMB_strafe"),
        Passed?TEXT("PASS"):TEXT("FAIL"),Checks,Moved);
    return Passed;
#endif
}
