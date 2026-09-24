#pragma once

#include "CoreMinimal.h"
#include "CireKeybindings.h" // feat/camera-movement: action -> key map stored in this profile

/** A rectangle in the HUD's logical coordinate system, before its DPI scale. */
struct FCireUIRect
{
    float X = 0.f;
    float Y = 0.f;
    float W = 120.f;
    float H = 64.f;
};

namespace CireOptions
{
#if !UE_BUILD_SHIPPING
    CIRESTEAMSURVIVAL_API bool RunSettingsSmoke();
#endif
}

/** Local presentation preferences. These never change server combat rules. */
class CIRESTEAMSURVIVAL_API FCireUISettings
{
public:
    FCireUISettings();

    /** An optional filename supports isolated profiles and deterministic tests. */
    void Load(const FString& Filename = FString());
    bool Save();
    /** Restore the reference layout/preferences in memory. Save to persist it. */
    void Reset();

    FCireUIRect GetRect(FName PanelId, const FVector2D& LogicalViewport) const;
    /** Changes only an unlocked panel while layout edit mode is enabled. */
    bool SetRect(FName PanelId, const FCireUIRect& Rect, const FVector2D& LogicalViewport);
    bool IsPanelLocked(FName PanelId) const;
    void SetPanelLocked(FName PanelId, bool bLocked);
    const TArray<FName>& GetPanelIds() const { return PanelIds; }
    const FString& GetFilename() const { return ConfigFilename; }

    bool bLayoutLocked = true;
    bool bShowChat = true;
    bool bShowCombatLog = false;
    bool bShowMeter = true;
    float ChatFontSize = 11.f;
    FLinearColor ChatColor = FLinearColor(.83f, .87f, .88f, 1.f);
    bool bCombatTextEnabled = true;
    bool bShowFloatingNumbers = true;
    bool bShowScrollingText = true;
    float WorldNumberFontSize = 26.f;
    float CombatTextFontSize = 24.f;
    // Legacy source compatibility only; neither persisted nor used as a mode switch.
    bool bCombatTextWorldSpace = true;
    bool bShowDamage = true;
    bool bShowHealing = true;
    bool bShowIncoming = true;
    bool bShowOutgoing = true;
    /** 0 = damage dealt, 1 = effective healing. */
    int32 MeterMode = 0;
    float CameraYawSensitivity = 1.f;
    float CameraPitchSensitivity = 1.f;
    bool bInvertMouseY = false;
    float CameraDistance = 650.f;
    float CameraFOV = 80.f;
    float MasterVolume = .85f;
    float SFXVolume = .85f;
    float UIVolume = .7f;
    bool bMuteAudio = false;
    bool bShowFPS = false;
    bool bShowNetwork = true;
    bool bTooltips = true;
    bool bQuickGroundCast = false;
    float TooltipScale = .8f;
    int32 TooltipMode = 0; // cursor, fixed panel, radial cursor offset
    float TooltipAngleDegrees = 45.f;
    float TooltipDistance = 40.f;
    bool bTooltipOffsetLocked = true;
    int32 StatusFilter = 0; // all, buffs, debuffs
    bool bDispellableOnly = false;
    bool bShowStatusDurations = true;
    bool bShowCriticalSymbol = true;
    bool bBloom = true;
    bool bMotionBlur = false;
    // --- WoW camera / targeting preferences (feat/camera-movement) ---
    /** Swing the camera back behind the character while it moves and no mouse button is held. */
    bool bCameraAutoFollow = true;
    /** After the hostile target dies, Tab-select the nearest hostile in front of the camera. */
    bool bAutoReacquireTarget = false;
    // --- end WoW camera / targeting preferences ---
    /** feat/camera-movement: keybindings + action-bar placements, section [CireUI.Keybindings]. */
    FCireKeybindings Keybindings;

private:
    struct FPanelLayout
    {
        FCireUIRect Normalized;
        FVector2D MinimumSize = FVector2D(60.f, 36.f);
        bool bLocked = false;
    };
    FString ConfigFilename;
    TMap<FName, FPanelLayout> Panels;
    TArray<FName> PanelIds;

    void SanitizePreferences();
    static FCireUIRect ClampRect(const FCireUIRect& Rect, const FVector2D& Viewport,
        const FVector2D& MinimumSize);
};
