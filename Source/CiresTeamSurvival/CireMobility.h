#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "CireMobility.generated.h"
class ACireGameMode;

struct FCireMovementTuning
{
    float RunSpeed=520,WalkSpeed=240,JumpVelocity=470,RollSpeed=820,RollDuration=.55f;
    float RollCooldown=3.5f,RollEnergy=25,InvulnerableStart=.08f,InvulnerableEnd=.32f;
};
namespace CireMovement
{
    const FCireMovementTuning& Tuning();
    bool Apply(const FCireMovementTuning& Value,FString& Error);
    bool Save(FString& Error);
    bool Reload(FString& Error);
    bool RunSmoke(ACireGameMode* Mode);
}
UCLASS()
class CIRESTEAMSURVIVAL_API UCireMobility : public UActorComponent
{
    GENERATED_BODY()
public:
    UCireMobility();
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
    UPROPERTY(Replicated) bool bWalking=false;
    UPROPERTY(Replicated) bool bStrafing=false;
    UPROPERTY(Replicated) float RollStartedAt=-100;
    UPROPERTY(Replicated) float RollDuration=.55f;
    UPROPERTY(Replicated) float ReadyAt=0;
    UPROPERTY(Replicated) float InvulnerableFrom=-100;
    UPROPERTY(Replicated) float InvulnerableUntil=-100;
    UPROPERTY(Replicated) FVector RollDirection=FVector::ForwardVector;
    double Now() const;
    bool IsRolling() const;
    bool IsInvulnerable() const;
    float RollProgress() const;
    float CooldownRemaining() const;
    float MovementSpeed(bool bSlowed) const;
    bool StartRoll(FVector Direction);
    void CancelRoll();
    UFUNCTION(Server,Reliable) void ServerRoll(FVector_NetQuantizeNormal Direction);
    UFUNCTION(Server,Reliable) void ServerSetWalk(bool Walking);
    UFUNCTION(Server,Reliable) void ServerSetStrafe(bool Strafing);
    UFUNCTION(NetMulticast,Reliable) void MulticastRoll(float Started,float Duration,FVector_NetQuantizeNormal Direction,float Speed);
};
