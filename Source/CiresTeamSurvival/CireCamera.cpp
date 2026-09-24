#include "CireCamera.h"
#include "CireGame.h"
#include "CireMobility.h"
#include "CireUISettings.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"

namespace
{
struct FCameraState
{
    bool bInitialized = false;
    float Yaw = 0.f, Pitch = -20.f;
    float Zoom = 650.f;          // smoothed preferred boom length
    float Boom = 650.f;          // boom after collision easing
    FRotator LastControl = FRotator::ZeroRotator;
    // Mouse gestures. A press only becomes a world gesture when it began over the world.
    bool bLeft = false, bRight = false;
    bool bLeftClickEligible = false;
    float LeftTravel = 0.f;
    FVector2D LeftPress = FVector2D::ZeroVector;
    double LastManualOrbit = -100.0;
    bool bFaceSent = false;
};
TMap<TWeakObjectPtr<ACireController>, FCameraState> States;

constexpr float ClickTravel = 5.f;        // raw counts before a left press is a drag, not a click
constexpr float ZoomStepFraction = .14f;  // per wheel notch
constexpr float MinZoomStep = 45.f;

struct FSteer
{
    float Forward = 0.f;   // along heading, already scaled for backpedal
    float Right = 0.f;     // strafe, scaled like Forward
    float Turn = 0.f;      // -1 left .. +1 right keyboard turning
};
/** Pure WoW key mapping: W/S drive, A/D turn unless mouselook converts them to strafe. */
FSteer MapSteering(bool W, bool S, bool A, bool D, bool bMouselook, bool bBothButtons, float Backpedal)
{
    FSteer Out;
    float F = (W ? 1.f : 0.f) - (S ? 1.f : 0.f);
    if (bBothButtons && !S) F = 1.f;
    const float Side = (D ? 1.f : 0.f) - (A ? 1.f : 0.f);
    float R = 0.f;
    if (bMouselook) R = Side; else Out.Turn = Side;
    const float Length = FMath::Sqrt(F * F + R * R);
    if (Length > KINDA_SMALL_NUMBER)
    {
        const float Magnitude = F < 0.f ? Backpedal : 1.f;
        Out.Forward = F / Length * Magnitude;
        Out.Right = R / Length * Magnitude;
    }
    return Out;
}
float MouseAxis(const ACireController* C, const FKey& Key)
{
    // Raw counts: the project's axis config applies 0.07 sensitivity plus engine smoothing,
    // which made the old camera feel slow and mushy. Sensitivity is applied here instead.
    return C->PlayerInput ? C->PlayerInput->GetRawKeyValue(Key) : 0.f;
}
}

CireCamera::FResult CireCamera::Tick(ACireController* C, ACireHero* H, float Dt, const FFrame& Frame)
{
    FResult Result;
    if (!IsValid(C) || !IsValid(H) || !C->IsLocalController() || !H->Arm || !H->Camera) return Result;
    Dt = FMath::Clamp(Dt, 0.f, .1f);
    auto& S = States.FindOrAdd(TWeakObjectPtr<ACireController>(C));
    const FRotator Control = C->GetControlRotation();
    // Adopt rotations set by possession, previews or fixtures instead of fighting them.
    if (!S.bInitialized || !Control.Equals(S.LastControl, .01f))
    {
        S.Yaw = Control.Yaw;
        S.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Control.Pitch), MinPitch, MaxPitch);
        if (!S.bInitialized) { S.Zoom = S.Boom = Frame.Options ? Frame.Options->CameraDistance : 650.f; }
        S.bInitialized = true;
    }
    auto* Options = Frame.Options;
    const float YawSpeed = (Options ? Options->CameraYawSensitivity : 1.f) * DegreesPerCount;
    const float PitchSpeed = (Options ? Options->CameraPitchSensitivity : 1.f) * DegreesPerCount;
    const float Invert = Options && Options->bInvertMouseY ? -1.f : 1.f;
    const double Now = C->GetWorld() ? C->GetWorld()->GetRealTimeSeconds() : 0.0;

    // Hide and lock the cursor while a world drag is held (engine restores it on release).
    // Drags that start on HUD panels keep the normal cursor so sliders/layout editing work.
    if (auto* LP = C->GetLocalPlayer(); LP && LP->ViewportClient && !C->IsInputKeyDown(EKeys::LeftMouseButton) && !C->IsInputKeyDown(EKeys::RightMouseButton))
        LP->ViewportClient->SetHideCursorDuringCapture(Frame.bMouseAllowed && !Frame.bPointerOverInterface && Frame.bPressEligible);

    // ---- mouse gestures -------------------------------------------------------------
    const bool bWorldPress = Frame.bMouseAllowed && !Frame.bPointerOverInterface;
    if (C->WasInputKeyJustPressed(EKeys::LeftMouseButton))
    {
        S.bLeft = bWorldPress; S.LeftTravel = 0.f; S.bLeftClickEligible = bWorldPress && Frame.bPressEligible;
        float X = 0, Y = 0; C->GetMousePosition(X, Y); S.LeftPress = FVector2D(X, Y);
    }
    if (C->WasInputKeyJustPressed(EKeys::RightMouseButton))
    {
        S.bRight = bWorldPress && Frame.bPressEligible;
        // WoW: pressing the steering button snaps the character to the camera heading.
        if (S.bRight && Frame.bSteeringAllowed) { FRotator R = C->GetControlRotation(); R.Yaw = S.Yaw; C->SetControlRotation(R); }
    }
    if (!C->IsInputKeyDown(EKeys::RightMouseButton)) S.bRight = false;
    const bool bLeftHeld = S.bLeft && C->IsInputKeyDown(EKeys::LeftMouseButton);
    if (bLeftHeld || S.bRight)
    {
        const float X = MouseAxis(C, EKeys::MouseX), Y = MouseAxis(C, EKeys::MouseY);
        if (bLeftHeld) S.LeftTravel += FMath::Abs(X) + FMath::Abs(Y);
        if (Frame.bMouseAllowed)
        {
            S.Yaw = FRotator::NormalizeAxis(S.Yaw + X * YawSpeed);
            // Mouse up (positive Y) looks up unless inverted.
            S.Pitch = FMath::Clamp(S.Pitch + Y * PitchSpeed * Invert, MinPitch, MaxPitch);
            if (!FMath::IsNearlyZero(X) || !FMath::IsNearlyZero(Y)) S.LastManualOrbit = Now;
        }
    }
    if (C->WasInputKeyJustReleased(EKeys::LeftMouseButton))
    {
        if (S.bLeft && S.bLeftClickEligible && S.LeftTravel < ClickTravel) { Result.bClick = true; Result.ClickPosition = S.LeftPress; }
        S.bLeft = false;
    }

    // ---- keyboard steering ---------------------------------------------------------
    const bool bMouselook = S.bRight && Frame.bSteeringAllowed;
    const bool bBoth = bMouselook && bLeftHeld;
    const auto& Tuning = CireMovement::Tuning();
    FSteer Steer;
    if (Frame.bSteeringAllowed)
        Steer = MapSteering(C->IsInputKeyDown(EKeys::W), C->IsInputKeyDown(EKeys::S), C->IsInputKeyDown(EKeys::A),
            C->IsInputKeyDown(EKeys::D), bMouselook, bBoth, Tuning.BackpedalScale);
    FRotator Heading = C->GetControlRotation();
    const bool bFace = Frame.bSteeringAllowed && (bMouselook || Steer.Turn != 0.f || Steer.Forward != 0.f || Steer.Right != 0.f);
    if (bMouselook) Heading.Yaw = S.Yaw;
    else if (bFace)
    {
        const float Turn = Steer.Turn * Tuning.KeyboardTurnRate * Dt;
        Heading.Yaw = FRotator::NormalizeAxis(Heading.Yaw + Turn);
        // Keyboard turning carries the camera along unless the player is holding an orbit.
        if (!bLeftHeld) S.Yaw = FRotator::NormalizeAxis(S.Yaw + Turn);
    }
    else Heading.Yaw = H->GetActorRotation().Yaw; // idle: follow server-authored facing (attacks)
    if (H->Mobility && S.bFaceSent != bFace)
    {
        S.bFaceSent = bFace; H->Mobility->bFaceControl = bFace; H->Mobility->ServerSetFaceControl(bFace);
    }
    if (H->Mobility && H->Mobility->bStrafing != bMouselook)
    {
        H->Mobility->bStrafing = bMouselook; H->Mobility->ServerSetStrafe(bMouselook);
    }
    if (Steer.Forward != 0.f || Steer.Right != 0.f)
    {
        const FRotationMatrix Basis(FRotator(0, Heading.Yaw, 0));
        H->AddMovementInput(Basis.GetUnitAxis(EAxis::X), Steer.Forward);
        H->AddMovementInput(Basis.GetUnitAxis(EAxis::Y), Steer.Right);
    }
    // Optional follow: swing behind a moving character when no mouse button is held.
    if (Options && Options->bCameraAutoFollow && !bLeftHeld && !S.bRight && Now - S.LastManualOrbit > .75 &&
        H->GetVelocity().Size2D() > 60.f && Steer.Forward > 0.f)
    {
        const float Delta = FRotator::NormalizeAxis(H->GetActorRotation().Yaw - S.Yaw);
        S.Yaw = FRotator::NormalizeAxis(S.Yaw + Delta * FMath::Clamp(Dt * 2.2f, 0.f, 1.f));
    }
    Heading.Pitch = S.Pitch; Heading.Roll = 0.f;
    C->SetControlRotation(Heading);
    S.LastControl = C->GetControlRotation();

    // ---- zoom + boom ---------------------------------------------------------------
    const float BodyScale = static_cast<float>(H->GetActorScale3D().Z);
    if (Frame.WheelSteps != 0.f && Frame.bMouseAllowed && Options)
    {
        // Same bounds as the Options slider; the value persists with the next options save.
        const float Step = FMath::Max(MinZoomStep, Options->CameraDistance * ZoomStepFraction);
        Options->CameraDistance = FMath::Clamp(Options->CameraDistance - Frame.WheelSteps * Step, 300.f, 1200.f);
    }
    const float Preferred = (Options ? Options->CameraDistance : 650.f) * BodyScale;
    S.Zoom = FMath::FInterpTo(S.Zoom, Preferred, Dt, 9.f);
    auto* Arm = H->Arm;
    // Measured boom from last frame: collision pulls in instantly, then eases back out.
    const FVector Pivot = Arm->GetComponentLocation() + Arm->TargetOffset;
    const float Actual = static_cast<float>(FVector::Dist(Pivot, H->Camera->GetComponentLocation()));
    if (Arm->IsCollisionFixApplied()) S.Boom = FMath::Min(S.Boom, Actual);
    S.Boom = S.Boom < S.Zoom ? FMath::FInterpTo(S.Boom, S.Zoom, Dt, 3.5f) : S.Zoom;
    Arm->bUsePawnControlRotation = false;
    Arm->SetUsingAbsoluteRotation(true);
    Arm->SetWorldRotation(FRotator(S.Pitch, S.Yaw, 0));
    Arm->bInheritPitch = Arm->bInheritYaw = Arm->bInheritRoll = true;
    Arm->bDoCollisionTest = true; Arm->ProbeSize = 14.f; Arm->ProbeChannel = ECC_Camera;
    Arm->bEnableCameraLag = false; Arm->bEnableCameraRotationLag = false;
    // Pivot just above the shoulders; scaled tanks get a proportionally taller pivot.
    Arm->SocketOffset = FVector::ZeroVector;
    Arm->TargetOffset = FVector(0, 0, H->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * .85f);
    Arm->TargetArmLength = S.Boom;
    return Result;
}

void CireCamera::Cleanup(ACireController* C)
{
    if (auto* LP = C ? C->GetLocalPlayer() : nullptr; LP && LP->ViewportClient) LP->ViewportClient->SetHideCursorDuringCapture(false);
    States.Remove(TWeakObjectPtr<ACireController>(C));
}

FRotator CireCamera::ViewRotation(const ACireController* C)
{
    if (const auto* S = States.Find(TWeakObjectPtr<ACireController>(const_cast<ACireController*>(C))); S && S->bInitialized)
        return FRotator(S->Pitch, S->Yaw, 0);
    return C ? C->GetControlRotation() : FRotator::ZeroRotator;
}

#if !UE_BUILD_SHIPPING
bool CireCamera::RunSmoke()
{
    bool Pass = true; int32 Count = 0;
    auto Check = [&](bool Value, const TCHAR* Name) { ++Count; Pass &= Value; if (!Value) UE_LOG(LogTemp, Error, TEXT("CIRE_CAMERA_ASSERT %s"), Name); };
    auto Near = [](float A, float B) { return FMath::IsNearlyEqual(A, B, .001f); };
    FSteer V = MapSteering(true, false, false, false, false, false, .65f);
    Check(Near(V.Forward, 1) && Near(V.Right, 0) && Near(V.Turn, 0), TEXT("W runs forward"));
    V = MapSteering(false, true, false, false, false, false, .65f);
    Check(Near(V.Forward, -.65f) && Near(V.Turn, 0), TEXT("S backpedals at the tuned fraction"));
    V = MapSteering(false, false, true, false, false, false, .65f);
    Check(Near(V.Forward, 0) && Near(V.Right, 0) && Near(V.Turn, -1), TEXT("A turns left without a mouse button"));
    V = MapSteering(false, false, false, true, true, false, .65f);
    Check(Near(V.Right, 1) && Near(V.Turn, 0), TEXT("D strafes while mouselooking"));
    V = MapSteering(true, false, true, false, true, false, .65f);
    Check(Near(V.Forward, UE_INV_SQRT_2) && Near(V.Right, -UE_INV_SQRT_2), TEXT("diagonal strafe is normalized"));
    V = MapSteering(false, false, false, false, true, true, .65f);
    Check(Near(V.Forward, 1), TEXT("both mouse buttons run forward"));
    V = MapSteering(true, true, false, false, false, false, .65f);
    Check(Near(V.Forward, 0) && Near(V.Right, 0), TEXT("W+S cancel"));
    // 800 counts (about one inch at 800 DPI) turns 56 degrees at default sensitivity.
    Check(Near(800 * DegreesPerCount, 56.f), TEXT("default sensitivity mapping"));
    Check(MinPitch < -60 && MaxPitch > 0, TEXT("pitch range allows looking up from below"));
    UE_LOG(LogTemp, Display, TEXT("CIRE_CAMERA_%s checks=%d"), Pass ? TEXT("PASS") : TEXT("FAIL"), Count);
    return Pass;
}
#endif
