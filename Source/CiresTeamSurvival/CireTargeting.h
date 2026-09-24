#pragma once
#include "CoreMinimal.h"
#include "CireAreaEffects.h"
class ACireController;
class ACireHero;
class ACireGameMode;

enum class ECireTargetKind : uint8 { None,Self,Friendly,Hostile,Ground };
struct FCireTargetDescriptor
{
    ECireTargetKind Kind=ECireTargetKind::None;
    FString Label=TEXT("Unavailable");
    float Range=0;
    bool bSelfFallback=false,bDirectional=false,bProjectile=false,bNeedsHostile=false,bHasFootprint=false;
    FCireAreaSpec Footprint;
};
struct FCireTargetingSnapshot
{
    bool bActive=false,bValid=false;
    int32 Slot=INDEX_NONE;
    FString SkillId,Message;
    FVector Point=FVector::ZeroVector;
    float Range=0;
};
namespace CireTargeting
{
    FCireTargetDescriptor Describe(const FString& Id);
    FCireTargetingSnapshot Snapshot(const ACireController* Controller);
    void Request(ACireController* Controller,int32 Slot);
    // Returns true only when it consumed a confirmation/cancellation click.
    bool Tick(ACireController* Controller);
    void Cancel(ACireController* Controller);
    void Cleanup(ACireController* Controller);
    // Shared local/server placement preflight. Server handlers still validate
    // gameplay, slot ownership, costs, cooldown, collision and realm independently.
    bool ValidateGround(ACireHero* Hero,const FString& Id,FVector Point,FVector& Center,FRotator& Heading,FString& Reason);
#if !UE_BUILD_SHIPPING
    bool RunDescriptorSmoke();
    bool RunRuntimeSmoke(ACireGameMode* Mode);
#endif
}
