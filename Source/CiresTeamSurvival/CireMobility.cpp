#include "CireMobility.h"
#include "CireGame.h"
#include "CireRollSkills.h" // champion-draft: dodge-roll skills
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "CireAbilityDB.h"
#include "GameFramework/RootMotionSource.h"
#include "Net/UnrealNetwork.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/World.h"

namespace
{
FCireMovementTuning MovementValues;bool Loaded=false;
FString Filename(){return FPaths::Combine(FPaths::ProjectContentDir(),TEXT("Data/MovementTuning.json"));}
bool Valid(const FCireMovementTuning& V)
{
    const auto In=[](float X,float A,float B){return FMath::IsFinite(X)&&X>=A&&X<=B;};
    return In(V.RunSpeed,300,800)&&In(V.WalkSpeed,100,V.RunSpeed)&&In(V.JumpVelocity,200,650)&&
        In(V.RollSpeed,300,1400)&&In(V.RollDuration,.25f,.9f)&&In(V.RollCooldown,1,15)&&
        In(V.RollEnergy,5,80)&&In(V.InvulnerableStart,0,V.RollDuration)&&
        In(V.InvulnerableEnd,V.InvulnerableStart,V.RollDuration)&&V.InvulnerableEnd-V.InvulnerableStart<=.4f&&
        In(V.Acceleration,500,10000)&&In(V.BrakingDeceleration,200,10000)&&In(V.GroundFriction,0,30)&&
        In(V.RotationRate,90,2000)&&In(V.AirControl,0,1)&&In(V.KeyboardTurnRate,45,720)&&
        In(V.BackpedalScale,.3f,1)&&In(V.TankBodyScale,1,1.5f);
}
}
const FCireMovementTuning& CireMovement::Tuning(){if(!Loaded){Loaded=true;FString Error;Reload(Error);}return MovementValues;}
bool CireMovement::Apply(const FCireMovementTuning& V,FString& Error)
{
    if(!Valid(V)){Error=TEXT("Invalid movement values. Invulnerability must fit inside the roll and last at most 0.4s.");return false;}
    MovementValues=V;Loaded=true;Error.Reset();return true;
}
bool CireMovement::Reload(FString& Error)
{
    FString Text;TSharedPtr<FJsonObject> O;FCireMovementTuning V;
    if(!FFileHelper::LoadFileToString(Text,*Filename())||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),O)||!O)
    {Error=TEXT("Cannot read MovementTuning.json");return false;}
    double Version=0;if(!O->TryGetNumberField(TEXT("schemaVersion"),Version)||Version!=1){Error=TEXT("Unsupported movement schema");return false;}
#define CIRE_READ_MOVE(Name) {double N=0;if(!O->TryGetNumberField(TEXT(#Name),N)){Error=TEXT("Missing movement field " #Name);return false;}V.Name=N;}
    CIRE_READ_MOVE(RunSpeed);CIRE_READ_MOVE(WalkSpeed);CIRE_READ_MOVE(JumpVelocity);CIRE_READ_MOVE(RollSpeed);
    CIRE_READ_MOVE(RollDuration);CIRE_READ_MOVE(RollCooldown);CIRE_READ_MOVE(RollEnergy);CIRE_READ_MOVE(InvulnerableStart);CIRE_READ_MOVE(InvulnerableEnd);
#undef CIRE_READ_MOVE
    // Responsiveness/turning/body-scale fields are optional so older data files keep loading.
#define CIRE_READ_OPTIONAL(Name) {double N=0;if(O->TryGetNumberField(TEXT(#Name),N))V.Name=N;}
    CIRE_READ_OPTIONAL(Acceleration);CIRE_READ_OPTIONAL(BrakingDeceleration);CIRE_READ_OPTIONAL(GroundFriction);
    CIRE_READ_OPTIONAL(RotationRate);CIRE_READ_OPTIONAL(AirControl);CIRE_READ_OPTIONAL(KeyboardTurnRate);
    CIRE_READ_OPTIONAL(BackpedalScale);CIRE_READ_OPTIONAL(TankBodyScale);
#undef CIRE_READ_OPTIONAL
    return Apply(V,Error);
}
bool CireMovement::Save(FString& Error)
{
    auto O=MakeShared<FJsonObject>();O->SetNumberField(TEXT("schemaVersion"),1);
#define CIRE_WRITE_MOVE(Name) O->SetNumberField(TEXT(#Name),Tuning().Name)
    CIRE_WRITE_MOVE(RunSpeed);CIRE_WRITE_MOVE(WalkSpeed);CIRE_WRITE_MOVE(JumpVelocity);CIRE_WRITE_MOVE(RollSpeed);
    CIRE_WRITE_MOVE(RollDuration);CIRE_WRITE_MOVE(RollCooldown);CIRE_WRITE_MOVE(RollEnergy);CIRE_WRITE_MOVE(InvulnerableStart);CIRE_WRITE_MOVE(InvulnerableEnd);
    CIRE_WRITE_MOVE(Acceleration);CIRE_WRITE_MOVE(BrakingDeceleration);CIRE_WRITE_MOVE(GroundFriction);CIRE_WRITE_MOVE(RotationRate);
    CIRE_WRITE_MOVE(AirControl);CIRE_WRITE_MOVE(KeyboardTurnRate);CIRE_WRITE_MOVE(BackpedalScale);CIRE_WRITE_MOVE(TankBodyScale);
#undef CIRE_WRITE_MOVE
    FString Text;if(!FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&Text))||!FFileHelper::SaveStringToFile(Text,*Filename())){Error=TEXT("Cannot save movement tuning");return false;}return true;
}
UCireMobility::UCireMobility(){SetIsReplicatedByDefault(true);PrimaryComponentTick.bCanEverTick=false;}
void UCireMobility::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps)const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(UCireMobility,bWalking);DOREPLIFETIME(UCireMobility,bStrafing);DOREPLIFETIME(UCireMobility,bFaceControl);DOREPLIFETIME(UCireMobility,RollStartedAt);
    DOREPLIFETIME(UCireMobility,RollDuration);DOREPLIFETIME(UCireMobility,ReadyAt);DOREPLIFETIME(UCireMobility,RollDirection);
    DOREPLIFETIME(UCireMobility,InvulnerableFrom);DOREPLIFETIME(UCireMobility,InvulnerableUntil);
}
double UCireMobility::Now()const{const auto* S=GetWorld()?GetWorld()->GetGameState():nullptr;return S?S->GetServerWorldTimeSeconds():GetWorld()?GetWorld()->GetTimeSeconds():0;}
bool UCireMobility::IsRolling()const{return Now()>=RollStartedAt&&Now()<RollStartedAt+RollDuration;}
bool UCireMobility::IsInvulnerable()const{return IsRolling()&&Now()>=InvulnerableFrom&&Now()<InvulnerableUntil;}
float UCireMobility::RollProgress()const{return IsRolling()?FMath::Clamp(static_cast<float>((Now()-RollStartedAt)/FMath::Max(.01f,RollDuration)),0.f,1.f):-1.f;}
float UCireMobility::CooldownRemaining()const{return FMath::Max(0.f,static_cast<float>(ReadyAt-Now()));}
float UCireMobility::MovementSpeed(bool bSlowed)const{return (bWalking?CireMovement::Tuning().WalkSpeed:CireMovement::Tuning().RunSpeed)*(bSlowed?.65f:1.f);}
void UCireMobility::ServerSetWalk_Implementation(bool Walking){bWalking=Walking;}
void UCireMobility::ServerSetStrafe_Implementation(bool Strafing){bStrafing=Strafing;}
void UCireMobility::ServerSetFaceControl_Implementation(bool Face){bFaceControl=Face;}
bool CireMovement::IsMovingForCast(const ACireHero& Hero)
{
    const auto* Move=Hero.GetCharacterMovement();if(!Move)return false;
    // Jumping/falling counts (WoW), but a parked actor in the falling state with no velocity does not.
    if(Move->IsFalling())return Hero.GetVelocity().SizeSquared()>100.f;
    return Move->GetCurrentAcceleration().SizeSquared2D()>1.f&&Hero.GetVelocity().Size2D()>10.f;
}
bool CireMovement::BlocksCast(const ACireHero& Hero,const FString& AbilityId)
{
    if(Hero.bBot)return false;
    const auto* D=CireAbilityDB::Find(AbilityId);
    return D&&D->CastTime>0&&!D->bCastWhileMoving&&IsMovingForCast(Hero);
}
float CireMovement::BodyScaleFor(const ACireHero& Hero)
{
    return Hero.bDrafted&&Hero.HasChampionRole(TEXT("tank"))?Tuning().TankBodyScale:1.f;
}
void CireMovement::ApplyToHero(ACireHero& Hero)
{
    const auto& V=Tuning();auto* Move=Hero.GetCharacterMovement();
    Move->JumpZVelocity=V.JumpVelocity;Move->MaxAcceleration=V.Acceleration;
    Move->BrakingDecelerationWalking=V.BrakingDeceleration;Move->GroundFriction=V.GroundFriction;
    Move->BrakingFrictionFactor=1.f;Move->RotationRate=FRotator(0,V.RotationRate,0);Move->AirControl=V.AirControl;
    if(const auto* Mobility=Hero.Mobility.Get())
    {
        // RMB mouselook (bStrafing) and WoW keyboard steering (bFaceControl) face the controller yaw;
        // otherwise (bots, idle players, rolls) the body turns toward its velocity.
        const bool bFaceController=(Mobility->bStrafing||Mobility->bFaceControl)&&!Mobility->IsRolling();
        // Player-controlled heroes never auto-turn toward their velocity: braking after a strafe or a
        // knockback must not swing the body (and the next W direction) sideways. Bots/AI still do.
        Move->bOrientRotationToMovement=!bFaceController&&!Mobility->IsRolling()&&!Hero.IsPlayerControlled();
        Hero.bUseControllerRotationYaw=bFaceController;
    }
    // Tanks are physically larger: actor scale keeps mesh, capsule, selection ring and camera pivot consistent.
    const float Scale=BodyScaleFor(Hero);
    if(!FMath::IsNearlyEqual(static_cast<float>(Hero.GetActorScale3D().Z),Scale,.001f))
    {
        const double OldHalfHeight=Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        Hero.SetActorScale3D(FVector(Scale));
        // Keep the feet planted: grow/shrink around the floor instead of the capsule centre.
        if(Hero.HasAuthority()||Hero.IsLocallyControlled())
            Hero.AddActorWorldOffset(FVector(0,0,Hero.GetCapsuleComponent()->GetScaledCapsuleHalfHeight()-OldHalfHeight));
    }
}
void UCireMobility::ServerRoll_Implementation(FVector_NetQuantizeNormal Direction){StartRoll(Direction);}
bool UCireMobility::StartRoll(FVector Direction)
{
    auto* H=Cast<ACireHero>(GetOwner());auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<ACireGameMode>():nullptr;
    if(!H||!H->HasAuthority()||!Mode||H->bDead||!H->bDrafted||Mode->Clock.Phase()==Cires::MatchPhase::Finished||
        Direction.ContainsNaN()||!H->GetCharacterMovement()->IsMovingOnGround())return false;
    const auto V=CireMovement::Tuning();
    if(IsRolling()||CooldownRemaining()>0){H->Notice=TEXT("Dodge is recovering.");return false;}
    if(H->Energy<V.RollEnergy){H->Notice=TEXT("Not enough energy to dodge.");return false;}
    Direction=Direction.GetSafeNormal2D();if(Direction.IsNearlyZero())Direction=H->GetActorForwardVector();
    H->Energy-=V.RollEnergy;RollStartedAt=Now();RollDuration=V.RollDuration;RollDirection=Direction;
    ReadyAt=RollStartedAt+V.RollCooldown;InvulnerableFrom=RollStartedAt+V.InvulnerableStart;InvulnerableUntil=RollStartedAt+V.InvulnerableEnd;
    H->PendingAttackTarget.Reset();H->GlobalCooldown=FMath::Max(H->GlobalCooldown,V.RollDuration);H->Notice=TEXT("Dodge roll");
    CireRollSkills::OnRoll(H,Direction); // champion-draft: roll skills fire per roll (and per roll charge)
    MulticastRoll(RollStartedAt,RollDuration,RollDirection,V.RollSpeed);H->ForceNetUpdate();return true;
}
void UCireMobility::MulticastRoll_Implementation(float Started,float Duration,FVector_NetQuantizeNormal Direction,float Speed)
{
    auto* H=Cast<ACireHero>(GetOwner());if(!H)return;
    RollStartedAt=Started;RollDuration=Duration;RollDirection=Direction;
    // Simulated proxies consume the CharacterMovement root-motion replication.
    if(!H->HasAuthority()&&!H->IsLocallyControlled())return;
    H->GetCharacterMovement()->RemoveRootMotionSource(TEXT("CireDodgeRoll"));
    const float Elapsed=FMath::Max(0.f,static_cast<float>(Now()-Started));if(Duration-Elapsed<=0)return;
    auto Force=MakeShared<FRootMotionSource_ConstantForce>();Force->InstanceName=TEXT("CireDodgeRoll");
    Force->Priority=600;Force->AccumulateMode=ERootMotionAccumulateMode::Override;Force->Duration=Duration;
    Force->SetTime(Elapsed); // Keep server/client source identity while catching up a delayed multicast.
    Force->Force=FVector(Direction)*Speed;Force->FinishVelocityParams.Mode=ERootMotionFinishVelocityMode::ClampVelocity;
    Force->FinishVelocityParams.ClampVelocity=MovementSpeed(false);
    Force->Settings.SetFlag(ERootMotionSourceSettingsFlags::UseSensitiveLiftoffCheck);
    H->GetCharacterMovement()->ApplyRootMotionSource(Force);
}
void UCireMobility::CancelRoll()
{
    const bool WasRolling=IsRolling();
    RollStartedAt=-100;InvulnerableFrom=InvulnerableUntil=-100;
    if(auto* H=Cast<ACireHero>(GetOwner())){H->GetCharacterMovement()->RemoveRootMotionSource(TEXT("CireDodgeRoll"));if(H->HasAuthority()&&WasRolling)MulticastRoll(-100,RollDuration,RollDirection,0);}
}
