#include "CireReplaySpectator.h"
#include "CireReplay.h"
#include "CireHUD.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ACireReplaySpectator::ACireReplaySpectator()
{
    bAutoManageActiveCameraTarget = false;
    bShouldPerformFullTickWhenPaused = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    bShowMouseCursor = true;
}
void ACireReplaySpectator::PlayerTick(float DeltaSeconds)
{
    // Do not execute gameplay controller input or send gameplay RPCs while viewing.
    APlayerController::PlayerTick(DeltaSeconds);
    if (!IsLocalController() || !CireReplay::IsPlayback(GetWorld())) return;
    auto* Replay = CireReplay::Get(GetWorld());
    if (!Replay) return;
    if (!bReplayViewReady || !IsValid(ReplayCamera))
    {
        ReplayCamera = GetWorld()->SpawnActor<ACameraActor>(FVector(-1400, -3900, 2200), FRotator(-40, 45, 0));
        if (!ReplayCamera) return;
        ReplayCamera->SetReplicates(false);
        SetViewTarget(ReplayCamera);
        ClientSetHUD(ACireHUD::StaticClass());
        FInputModeGameAndUI Mode; Mode.SetHideCursorDuringCapture(false); SetInputMode(Mode);
        Replay->Status = TEXT("Replay: WASD/QE fly, RMB look, 1/2 lanes, Space pause, arrows seek, +/- speed, Esc exit.");
        bReplayViewReady = true;
    }
    if (GetViewTarget() != ReplayCamera) SetViewTarget(ReplayCamera);
    auto* HUD = Cast<ACireHUD>(GetHUD());
    if (WasInputKeyJustPressed(EKeys::F9) && HUD) HUD->ToggleSettings();
    if (WasInputKeyJustPressed(EKeys::Escape))
    {
        if (HUD && HUD->HandleEscape()) return;
        Replay->ExitPlayback(); return;
    }
    if (HUD && HUD->IsBlockingGameplayInput()) return;
    if (WasInputKeyJustPressed(EKeys::SpaceBar)) Replay->SetPaused(!Replay->IsPaused());
    if (WasInputKeyJustPressed(EKeys::Left)) Replay->Seek(Replay->CurrentSeconds() - 10.f);
    if (WasInputKeyJustPressed(EKeys::Right)) Replay->Seek(Replay->CurrentSeconds() + 10.f);
    if (WasInputKeyJustPressed(EKeys::Subtract) || WasInputKeyJustPressed(EKeys::Hyphen)) Replay->SetSpeed(FMath::Max(.25f, Replay->PlaybackSpeed() * .5f));
    if (WasInputKeyJustPressed(EKeys::Add) || WasInputKeyJustPressed(EKeys::Equals)) Replay->SetSpeed(FMath::Min(4.f, Replay->PlaybackSpeed() * 2.f));
    if (WasInputKeyJustPressed(EKeys::One)) { ReplayCamera->SetActorLocation(FVector(-1400, -3900, 2200)); ReplayCamera->SetActorRotation(FRotator(-40, 45, 0)); }
    if (WasInputKeyJustPressed(EKeys::Two)) { ReplayCamera->SetActorLocation(FVector(-1400, 300, 2200)); ReplayCamera->SetActorRotation(FRotator(-40, 45, 0)); }
    FRotator Rotation = ReplayCamera->GetActorRotation();
    if (IsInputKeyDown(EKeys::RightMouseButton))
    {
        float X = 0, Y = 0; GetInputMouseDelta(X, Y);
        Rotation.Yaw += X * .2f; Rotation.Pitch = FMath::Clamp(Rotation.Pitch + Y * .2f, -85.f, 85.f);
        ReplayCamera->SetActorRotation(Rotation);
    }
    const float Forward = (IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
    const float Right = (IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
    const float Up = (IsInputKeyDown(EKeys::E) ? 1.f : 0.f) - (IsInputKeyDown(EKeys::Q) ? 1.f : 0.f);
    FVector Direction = Rotation.Vector() * Forward + FRotationMatrix(Rotation).GetUnitAxis(EAxis::Y) * Right + FVector::UpVector * Up;
    const float Speed = IsInputKeyDown(EKeys::LeftShift) ? 3000.f : 1200.f;
    ReplayCamera->AddActorWorldOffset(Direction.GetClampedToMaxSize(1.f) * Speed * FMath::Min(DeltaSeconds, .1f));
}
void ACireReplaySpectator::EndPlay(const EEndPlayReason::Type Reason)
{
    if (IsValid(ReplayCamera)) ReplayCamera->Destroy();
    Super::EndPlay(Reason);
}
