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
    // feat/camera-movement: casting while moving.
    // Casts the armed ground ability at the reticle (clean left click / key pressed again). False if nothing armed.
    bool Confirm(ACireController* Controller);
    // Sends a cast to the server. Cast-time spells that cannot be cast while moving either wait for the
    // character to stop (Stop moving to cast, movement keys held) or let the server answer "Can't cast while moving".
    void Dispatch(ACireController* Controller,int32 Slot,AActor* ExplicitTarget,bool bAtPoint=false,FVector Point=FVector::ZeroVector);
    // True while a stop-to-cast is holding the movement keys (CireCamera zeroes drive/strafe input).
    bool HoldsMovement(const ACireController* Controller);
    // A movement key was newly pressed: release the hold (the server then cancels a stationary cast).
    void ReleaseMovementHold(ACireController* Controller);
    // Ground point used when the cursor is not over valid ground (summons/quick cast "in front of you").
    FVector FrontPoint(ACireHero* Hero,float Range);
    // Returns true only when it consumed a confirmation/cancellation click.
    bool Tick(ACireController* Controller);
    void Cancel(ACireController* Controller);
    void Cleanup(ACireController* Controller);
    // ability-vfx: hovering a learned skill with a void zone (e.g. Shadow Step on a hostile target) previews both
    // rift zones where it would open. Call every frame while hovered; the preview hides one frame after.
    void HoverPreview(ACireController* Controller, const FString& SkillId);
    // Shared local/server placement preflight. Server handlers still validate
    // gameplay, slot ownership, costs, cooldown, collision and realm independently.
    bool ValidateGround(ACireHero* Hero,const FString& Id,FVector Point,FVector& Center,FRotator& Heading,FString& Reason);
#if !UE_BUILD_SHIPPING
    bool RunDescriptorSmoke();
    bool RunRuntimeSmoke(ACireGameMode* Mode);
    // ability-vfx: galleries/tests aim the armed preview at a world point instead of the cursor (unset clears).
    void DebugSetAimOverride(TOptional<FVector> Point);
    // ability-vfx: void zone radii / icons drawn by the armed preview this frame (x outer, y inner, z icons).
    FVector DebugPreviewVoid(const ACireController* Controller);
#endif
}
