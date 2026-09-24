#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CireDraftStage.generated.h"

class ACireHero;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UStaticMeshComponent;
class ULocalLightComponent;

/**
 * Local-only champion preview for the draft screen. Spawns a non-replicated,
 * tick-disabled ACireHero bound to the selected profile so ChampionArt applies
 * the exact in-game body, weapons and idle animation, then renders it on a lit
 * dark-fantasy stage into a render target with a SceneCapture2D. The stage sits
 * far above the battlefield and the capture uses a show-only list, so neither
 * the map nor gameplay ever see it. Also produces bust portraits on demand.
 */
UCLASS(NotPlaceable, Transient)
class CIRESTEAMSURVIVAL_API ACireDraftStage : public AActor
{
    GENERATED_BODY()
public:
    ACireDraftStage();
    static ACireDraftStage* SpawnStage(UWorld* World);
    // Empty id clears the preview. Re-showing the same id keeps the pose/turntable.
    void ShowProfile(const FString& ProfileId);
    const FString& GetProfileId() const { return ProfileId; }
    UTextureRenderTarget2D* GetRenderTarget() const { return Target; }
    // True once the body is bound, bounded and framed (animation has had time to settle).
    bool IsPreviewReady() const;
    float SecondsShown() const;
    void SetTurntable(bool bSpin, float FixedYaw = -28.f);
    // Non-null: frame a head-and-shoulders bust into this square target every frame
    // (portrait generation). Null returns to the full-body draft preview.
    void SetPortraitTarget(UTextureRenderTarget2D* Into);
    // The owner calls this every frame it shows the stage; an untouched stage
    // (draft screen closed, HUD gone) destroys itself and its preview hero.
    void Touch() { LastTouchedFrame = GFrameCounter; }
    // Rendered frames since the current profile was shown (TAA/animation settling).
    uint64 FramesShown() const { return ProfileId.IsEmpty() ? 0 : GFrameCounter - ShownFrame; }
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    FBox BodyBounds() const;
private:
    void BuildStage();
    void FitStage(float BodyHeight);
    void FrameCamera(float DeltaSeconds, bool bSnap);
    void DestroyPreview();
    UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> Capture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
    UPROPERTY(Transient) TObjectPtr<ACireHero> Preview;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> PortraitTarget;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Dais;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Wall;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Arch;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Braziers;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Embers;
    UPROPERTY(Transient) TObjectPtr<ULocalLightComponent> KeyLight;
    UPROPERTY(Transient) TObjectPtr<ULocalLightComponent> RimLight;
    UPROPERTY(Transient) TObjectPtr<ULocalLightComponent> FillLight;
    UPROPERTY(Transient) TArray<TObjectPtr<ULocalLightComponent>> FireLights;
    FString ProfileId;
    double ShownAt = 0;
    double LastAttackAt = 0;
    uint64 LastTouchedFrame = 0;
    uint64 ShownFrame = 0;
    float Yaw = -28.f;
    float StageScale = 1.f;
    bool bSpin = true;
    bool bFramed = false;
    FVector CameraFocus = FVector::ZeroVector;
    float CameraDistance = 600.f;
    float BodyHeight = 180.f;
};
