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
    // True once the body is bound, bounded, framed and metered (exposure converged or timed out).
    bool IsPreviewReady() const;
    // champ-select-hq: live exposure metering of the full-body preview. The stage reads its own
    // render (async GPU readback: colour + depth), measures the champion's pixels only and steps the
    // manual exposure until the brightest 2% sit just under white and the mid-tones are lit, so no
    // body is blown out (pale stone, skin, polished steel) or muddy (black plate). Result per profile
    // is cached for the session.
    bool IsMetered() const { return bMetered; }
    float MeteredMedian() const { return MeterMedian; }
    float MeteredHighlight() const { return MeterHigh; }
    float MeteredClipShare() const { return MeterClip; }
    // Meshes built, shaders compiled, PSOs ready and textures streamed: what the player will actually see.
    bool IsContentSettled() const;
    // Normalised render V of the floor under the champion (contact shadow placement).
    float GetFeetV() const { return FeetV; }
    float SecondsShown() const;
    void SetTurntable(bool bSpin, float FixedYaw = -28.f);
    // Per-champion exposure trim (stops). Bright albedo bodies (granite, felfire) are
    // measured by the portrait tool and stored in Content/UI/Draft/Portraits/Exposure.json;
    // ShowProfile applies the stored value automatically.
    void SetExposureOffset(float Stops);
    float GetExposureOffset() const { return ExposureOffset; }
    static float StoredExposure(const FString& ProfileId);
    // Non-null: frame a head-and-shoulders bust into this square target every frame
    // (portrait generation). Null returns to the full-body draft preview.
    void SetPortraitTarget(UTextureRenderTarget2D* Into);
    // Cutout: hide the stage architecture and render the champion alone with alpha, so the
    // draft screen can stand it on a painted background (enables post-process alpha while any
    // cutout stage exists).
    void SetCutout(bool bEnable);
    bool IsCutout() const { return bCutout; }
    // Resize the preview (3:4) so it is never upscaled on screen: Pixels = target height.
    void SetPreviewHeight(int32 Pixels);
    // Light the champion to sit in a painted scene: key (front), rim (back edge), fill (ambient).
    void SetMood(const FLinearColor& Key,const FLinearColor& Rim,const FLinearColor& Fill);
    // Scene depth of the cutout view (R, world units): masks out anything far behind the champion.
    UTextureRenderTarget2D* GetDepthTarget() const { return DepthTarget; }
    float GetCutoutMaxDepth() const { return CameraDistance+650.f*StageScale; }
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
    void RefreshCutoutParts();
    void UpdateMetering();
    static void ForceTopDetail(AActor* Actor);
    void ApplyLook();
    UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> Capture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Target;
    UPROPERTY(Transient) TObjectPtr<ACireHero> Preview;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> PortraitTarget;
    bool bCutout = false;
    UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> DepthCapture;
    UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> DepthTarget;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Floor;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Dais;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Wall;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Arch;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Braziers;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Embers;
    UPROPERTY(Transient) TObjectPtr<ULocalLightComponent> KeyLight;
    UPROPERTY(Transient) TObjectPtr<ULocalLightComponent> RimLight;
    UPROPERTY(Transient) TObjectPtr<ULocalLightComponent> FillLight;
    UPROPERTY(Transient) TObjectPtr<ULocalLightComponent> KickerLight;
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
    float ExposureOffset = 0.f;
    // Metering state (see IsMetered).
    TSharedPtr<struct FDraftMeter, ESPMode::ThreadSafe> Meter;
    bool bMetered = false;
    int32 MeterPasses = 0;
    uint64 MeterRequestFrame = 0;
    uint64 MeterSettleFrame = 0;
    float MeterMedian = 0.f, MeterHigh = 0.f, MeterClip = 0.f;
    // video-crash: metering waits for the finished look and re-runs when the scalability preset changes.
    bool bShownMetered = false;
    double SettledAt = 0;
    FString MeterQualityKey;
    double LastPrestream = 0;
    float FeetV = .92f;
};
