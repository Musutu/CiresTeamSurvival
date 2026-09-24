#pragma once
#include "CoreMinimal.h"
class ACireController;
class AActor;
namespace CireSelection
{
    CIRESTEAMSURVIVAL_API void Update(ACireController* Controller);
    CIRESTEAMSURVIVAL_API void Cleanup(ACireController* Controller);
    /**
     * WoW tab targeting. Hostile: nearest observable hostile inside the camera's view cone first,
     * then outward by distance without repeating until every candidate was visited; bReverse walks
     * back through that history. Falls back to all-around candidates when nothing is in front.
     * Friendly: cycles living, observable allies by distance. Returns the actor to select or null.
     */
    CIRESTEAMSURVIVAL_API AActor* NextTarget(ACireController* Controller, bool bFriendly, bool bReverse, const FTransform* ViewOverride = nullptr);
    /** Clears a dead/destroyed hostile target (optionally reacquiring the next Tab target). */
    CIRESTEAMSURVIVAL_API void HandleTargetLoss(ACireController* Controller, bool bAutoReacquire);
    /** Tab-cycle tuning. */
    constexpr float TabRange = 2500.f;
    constexpr float TabHistorySeconds = 3.f;
}
