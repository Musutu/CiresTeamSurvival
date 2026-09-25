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
    /** Smart cast: best hostile for a spell with Range. The observable living hostile under the
     *  cursor (if any and in range) wins, else the nearest in the camera cone, else the nearest around. */
    CIRESTEAMSURVIVAL_API AActor* BestHostile(ACireController* Controller, float Range, AActor* UnderCursor = nullptr, const FTransform* ViewOverride = nullptr);
    /** Observable unit under the cursor (hero, monster or construct), or null. */
    CIRESTEAMSURVIVAL_API AActor* UnitUnderCursor(ACireController* Controller);
    /** Clears a dead/destroyed hostile target (optionally reacquiring the next Tab target). */
    CIRESTEAMSURVIVAL_API void HandleTargetLoss(ACireController* Controller, bool bAutoReacquire);
    /** Tab-cycle tuning. */
    constexpr float TabRange = 2500.f;
    constexpr float TabHistorySeconds = 3.f;
}
