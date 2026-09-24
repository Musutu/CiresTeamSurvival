#pragma once
#include "CoreMinimal.h"

class ACireController;
class ACireHero;
class FCireUISettings;

/**
 * World of Warcraft style third-person camera and steering for the local player.
 *
 *  - Left drag orbits the camera around the character without turning it.
 *  - Right drag steers: the camera turns and the character faces the camera yaw (mouselook).
 *  - Both buttons run forward. W/S move forward/backpedal along the character facing.
 *  - A/D turn the character when no mouse button is held and strafe while the right button is held.
 *  - Wheel zoom is smoothed; camera collision pulls in instantly and eases back out.
 *
 * Presentation is local; the character facing uses the replicated control rotation, so the
 * server/other clients see the same heading. Nothing here changes combat rules.
 */
namespace CireCamera
{
    struct FFrame
    {
        /** Mouse may drive the camera (no modal options/layout editor). */
        bool bMouseAllowed = true;
        /** Keyboard steering/movement allowed (drafted, alive, not chatting, no shop). */
        bool bSteeringAllowed = false;
        /** A new press this frame may become a world click (not consumed by aiming/UI). */
        bool bPressEligible = true;
        /** The cursor is over a HUD panel; drags starting there never orbit. */
        bool bPointerOverInterface = false;
        /** Wheel steps this frame (+ zooms in) that were not consumed by the HUD. */
        float WheelSteps = 0.f;
        FCireUISettings* Options = nullptr; // wheel zoom writes CameraDistance
    };
    struct FResult
    {
        /** Left button released without dragging: select under this screen position. */
        bool bClick = false;
        FVector2D ClickPosition = FVector2D::ZeroVector;
    };

    CIRESTEAMSURVIVAL_API FResult Tick(ACireController* Controller, ACireHero* Hero, float DeltaSeconds, const FFrame& Frame);
    CIRESTEAMSURVIVAL_API void Cleanup(ACireController* Controller);
    /** Current local camera view rotation (falls back to the control rotation). */
    CIRESTEAMSURVIVAL_API FRotator ViewRotation(const ACireController* Controller);
    /** Base degrees per mouse count at sensitivity 1.0 (raw mouse delta, no engine smoothing). */
    constexpr float DegreesPerCount = .07f;
    constexpr float MinPitch = -80.f, MaxPitch = 30.f;

#if !UE_BUILD_SHIPPING
    /** Native checks for the pure steering math (turn rate, backpedal, sensitivity mapping). */
    CIRESTEAMSURVIVAL_API bool RunSmoke();
#endif
}
