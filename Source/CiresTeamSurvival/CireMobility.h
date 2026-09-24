#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "CireMobility.generated.h"
class ACireGameMode;
class ACireHero;

struct FCireMovementTuning
{
    float RunSpeed=520,WalkSpeed=240,JumpVelocity=470,RollSpeed=820,RollDuration=.55f;
    float RollCooldown=3.5f,RollEnergy=25,InvulnerableStart=.08f,InvulnerableEnd=.32f;
    // Responsiveness (optional JSON fields; older files keep these defaults).
    float Acceleration=3072,BrakingDeceleration=3584,GroundFriction=10,RotationRate=900,AirControl=.35f;
    // WoW-style keyboard turning (deg/s) and backpedal speed as a fraction of run speed.
    float KeyboardTurnRate=180,BackpedalScale=.65f;
    // Whole-body scale for champions whose role is Tank (mesh, capsule, ring and camera follow).
    float TankBodyScale=1.15f;
};
namespace CireMovement
{
    const FCireMovementTuning& Tuning();
    bool Apply(const FCireMovementTuning& Value,FString& Error);
    bool Save(FString& Error);
    bool Reload(FString& Error);
    bool RunSmoke(ACireGameMode* Mode);
    /** Presentation + collision scale for a hero; Tank-role champions use TankBodyScale. */
    float BodyScaleFor(const ACireHero& Hero);
    /** Applies acceleration/braking/rotation/scale tuning to a hero. Safe on server and clients. */
    void ApplyToHero(ACireHero& Hero);
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
    /** WoW controls: the character faces the controller yaw (keyboard turning / backpedal) instead of its velocity. */
    UPROPERTY(Replicated) bool bFaceControl=false;
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
    UFUNCTION(Server,Reliable) void ServerSetFaceControl(bool Face);
    UFUNCTION(NetMulticast,Reliable) void MulticastRoll(float Started,float Duration,FVector_NetQuantizeNormal Direction,float Speed);
};
