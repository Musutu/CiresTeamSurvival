#include "CireUISettings.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"

namespace
{
constexpr float ReferenceWidth = 1280.f;
constexpr float ReferenceHeight = 720.f;
// 4: interface scale, per-panel anchors, WoW tooltip/SCT/threat/level-up preferences.
constexpr int32 LayoutVersion = 4;
const TCHAR* PreferencesSection = TEXT("CireUI.Preferences");

float SafeFloat(float Value, float Default, float Minimum, float Maximum)
{
    return FMath::Clamp(FMath::IsFinite(Value) ? Value : Default, Minimum, Maximum);
}

FVector2D SafeViewport(const FVector2D& Viewport)
{
    return FVector2D(SafeFloat(static_cast<float>(Viewport.X), ReferenceWidth, 1.f, 65536.f),
        SafeFloat(static_cast<float>(Viewport.Y), ReferenceHeight, 1.f, 65536.f));
}

FString PanelSection(FName PanelId)
{
    return TEXT("CireUI.Panel.") + PanelId.ToString();
}
}

FCireUISettings::FCireUISettings()
    : ConfigFilename(FPaths::ConvertRelativePathToFull(
        FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Config"), TEXT("CireUI.ini"))))
{
    Reset();
}

void FCireUISettings::Reset()
{
    Panels.Reset();
    PanelIds.Reset();
    auto Add = [this](const TCHAR* Id, float X, float Y, float W, float H)
    {
        FPanelLayout Layout;
        Layout.Normalized = {X / ReferenceWidth, Y / ReferenceHeight,
            W / ReferenceWidth, H / ReferenceHeight};
        Layout.MinimumSize = FVector2D(FMath::Max(60.f, W * .65f), FMath::Max(36.f, H * .65f));
        Layout.Anchor = AnchorFor(X / ReferenceWidth, Y / ReferenceHeight, (X + W) / ReferenceWidth, (Y + H) / ReferenceHeight);
        Panels.Add(FName(Id), Layout);
        PanelIds.Add(FName(Id));
    };
    Add(TEXT("Player"), 20.f, 20.f, 260.f, 132.f);
    Add(TEXT("Party"), 20.f, 166.f, 250.f, 248.f);
    Add(TEXT("Match"), 480.f, 18.f, 320.f, 74.f);
    Add(TEXT("Target"), 482.f, 111.f, 300.f, 140.f);
    Add(TEXT("Focus"), 796.f, 111.f, 218.f, 123.f);
    Add(TEXT("Minimap"), 1040.f, 20.f, 220.f, 178.f);
    Add(TEXT("Chat"), 20.f, 528.f, 306.f, 172.f);
    Add(TEXT("Skills"), 344.f, 545.f, 584.f, 155.f);
    Add(TEXT("Meter"), 956.f, 526.f, 304.f, 174.f);
    Add(TEXT("CombatLog"), 956.f, 362.f, 304.f, 150.f);
    Add(TEXT("CombatText"), 425.f, 240.f, 430.f, 220.f);
    // The WoW-style default tooltip grows up/left from this panel's lower-right
    // corner: right of the reticle, above the action bar and meter.
    Add(TEXT("Tooltip"), 690.f, 368.f, 340.f, 150.f);
    Add(TEXT("Pet"), 20.f, 426.f, 250.f, 90.f);
    Add(TEXT("Threat"), 1040.f, 398.f, 220.f, 124.f);
    Add(TEXT("Boss"), 1040.f, 242.f, 220.f, 150.f);
    // Extra action bars stack above the main bar (panel "Skills").
    Add(TEXT("Bar2"), 368.f, 492.f, 536.f, 48.f);
    Add(TEXT("Bar3"), 368.f, 440.f, 536.f, 48.f);
    // progression-shop: bag/belt/teleport bar and the compact stats window (CireShopUI).
    Add(TEXT("Inventory"), 20.f, 420.f, 306.f, 102.f);
    Add(TEXT("Stats"), 282.f, 166.f, 176.f, 238.f);

    bLayoutLocked = true;
    bShowChat = true;
    bShowCombatLog = false;
    bShowMeter = true;
    ChatFontSize = 11.f;
    ChatColor = FLinearColor(.83f, .87f, .88f, 1.f);
    bCombatTextEnabled = true;
    bShowFloatingNumbers = true;
    bShowScrollingText = true;
    WorldNumberFontSize = 26.f;
    bCombatTextWorldSpace = true;
    CombatTextFontSize = 24.f;
    bShowDamage = true;
    bShowHealing = true;
    bShowIncoming = true;
    bShowOutgoing = true;
    MeterMode = 0;
    CameraYawSensitivity=CameraPitchSensitivity=1.f; bInvertMouseY=false;
    CameraDistance=650.f; CameraFOV=80.f;
    MasterVolume=.85f; SFXVolume=.85f; UIVolume=.7f; bMuteAudio=false;
    MusicVolume=.6f; AmbienceVolume=.8f; bMusicEnabled=true; bFootstepCameraShake=false; // audio:
    bShowFPS=false; bShowNetwork=true; bTooltips=true; bQuickGroundCast=false;
    TooltipScale=.8f; TooltipMode=3; TooltipAngleDegrees=45.f; TooltipDistance=40.f; bTooltipOffsetLocked=true;
    StatusFilter=0; bDispellableOnly=false; bShowStatusDurations=true; bShowCriticalSymbol=true;
    bBloom=true; bMotionBlur=false;
    UIScale=1.f; bAutoUIScale=true; TooltipOpacity=.94f; TooltipDelay=.12f; bTooltipAvoidCenter=true; bUnitTooltips=true;
    bShowMisses=true; bCritPop=true; bSchoolColors=true; bMergeAoE=true; SCTDirection=0; SCTSpeed=1.f; SCTFadeSeconds=3.2f;
    bShowThreatMeter=true; bThreatWarnings=true; bThreatSound=true; ThreatWarningPercent=90.f; bLevelUpEffect=true; bShowBossFrames=true;
    bShowActionBar2=true; bShowActionBar3=false; bLockActionBars=false;
    bCameraAutoFollow=true; bAutoReacquireTarget=false; // feat/camera-movement
    bShowStats=true; // progression-shop
}

int32 FCireUISettings::AnchorFor(float Left, float Top, float Right, float Bottom)
{
    const auto Safe = [](float V) { return FMath::IsFinite(V) ? V : .5f; };
    // Horizontal: nearest third of the panel centre. Vertical: panels starting in
    // the top third hang from the top, panels ending in the bottom third sit on the
    // bottom, others stay centred; stacked columns then keep their order at any scale.
    const float CenterX = (Safe(Left) + Safe(Right)) * .5f;
    const int32 AX = CenterX < 1.f / 3.f ? 0 : CenterX > 2.f / 3.f ? 2 : 1;
    const int32 AY = Safe(Top) <= .34f ? 0 : Safe(Bottom) >= 2.f / 3.f ? 2 : 1;
    return AX + 3 * AY;
}

float FCireUISettings::ResolveUIScale(float ViewportHeightPixels) const
{
    if (!bAutoUIScale) return SafeFloat(UIScale, 1.f, .64f, 1.15f);
    // Auto: full size at 1080p and below, gently smaller on tall high-DPI displays
    // (1440p ~0.93, 2160p 0.85), like WoW's resolution-derived default.
    const float Height = SafeFloat(ViewportHeightPixels, 1080.f, 1.f, 65536.f);
    return FMath::GetMappedRangeValueClamped(FVector2f(1080.f, 2160.f), FVector2f(1.f, .85f), Height);
}

FCireUIRect FCireUISettings::ClampRect(const FCireUIRect& Rect, const FVector2D& Viewport,
    const FVector2D& MinimumSize)
{
    const FVector2D Safe = SafeViewport(Viewport);
    const float Width = static_cast<float>(Safe.X);
    const float Height = static_cast<float>(Safe.Y);
    FCireUIRect Result;
    Result.W = SafeFloat(Rect.W, 120.f, FMath::Min(static_cast<float>(MinimumSize.X), Width), Width);
    Result.H = SafeFloat(Rect.H, 64.f, FMath::Min(static_cast<float>(MinimumSize.Y), Height), Height);
    Result.X = SafeFloat(Rect.X, 0.f, 0.f, Width - Result.W);
    Result.Y = SafeFloat(Rect.Y, 0.f, 0.f, Height - Result.H);
    return Result;
}

FCireUIRect FCireUISettings::GetRect(FName PanelId, const FVector2D& LogicalViewport) const
{
    const FVector2D Viewport = SafeViewport(LogicalViewport);
    const FPanelLayout* Layout = Panels.Find(PanelId);
    if (!Layout) return ClampRect(FCireUIRect(), Viewport, FVector2D(60.f, 36.f));
    // Sizes are stored in reference units (fractions of 1280x720), so a panel keeps
    // its designed size when the interface scale enlarges the logical viewport. The
    // position stays relative to the panel's anchor edge (WoW-style anchoring):
    // left/top keep their reference offset, right/bottom keep their offset from the
    // far edge and centred panels stay centred. At exactly 1280x720 all anchors agree.
    const FCireUIRect& R = Layout->Normalized;
    const float VW = static_cast<float>(Viewport.X), VH = static_cast<float>(Viewport.Y);
    const float W = R.W * ReferenceWidth, H = R.H * ReferenceHeight;
    const int32 AX = Layout->Anchor % 3, AY = Layout->Anchor / 3;
    const float X = AX == 0 ? R.X * ReferenceWidth : AX == 2 ? VW - (1.f - R.X - R.W) * ReferenceWidth - W
        : (R.X + R.W * .5f) * VW - W * .5f;
    const float Y = AY == 0 ? R.Y * ReferenceHeight : AY == 2 ? VH - (1.f - R.Y - R.H) * ReferenceHeight - H
        : (R.Y + R.H * .5f) * VH - H * .5f;
    return ClampRect({X, Y, W, H}, Viewport, Layout->MinimumSize);
}

bool FCireUISettings::SetRect(FName PanelId, const FCireUIRect& Rect, const FVector2D& LogicalViewport)
{
    FPanelLayout* Layout = Panels.Find(PanelId);
    if (bLayoutLocked || !Layout || Layout->bLocked) return false;
    const FVector2D Viewport = SafeViewport(LogicalViewport);
    const FCireUIRect R = ClampRect(Rect, Viewport, Layout->MinimumSize);
    const float VW = static_cast<float>(Viewport.X), VH = static_cast<float>(Viewport.Y);
    // Re-anchor to the nearest screen third, then invert GetRect for that anchor.
    Layout->Anchor = AnchorFor(R.X / VW, R.Y / VH, (R.X + R.W) / VW, (R.Y + R.H) / VH);
    const int32 AX = Layout->Anchor % 3, AY = Layout->Anchor / 3;
    FCireUIRect N;
    N.W = R.W / ReferenceWidth;
    N.H = R.H / ReferenceHeight;
    N.X = AX == 0 ? R.X / ReferenceWidth : AX == 2 ? 1.f - N.W - (VW - R.X - R.W) / ReferenceWidth
        : (R.X + R.W * .5f) / VW - N.W * .5f;
    N.Y = AY == 0 ? R.Y / ReferenceHeight : AY == 2 ? 1.f - N.H - (VH - R.Y - R.H) / ReferenceHeight
        : (R.Y + R.H * .5f) / VH - N.H * .5f;
    Layout->Normalized = N;
    return true;
}

bool FCireUISettings::IsPanelLocked(FName PanelId) const
{
    const FPanelLayout* Layout = Panels.Find(PanelId);
    return !Layout || Layout->bLocked;
}

void FCireUISettings::SetPanelLocked(FName PanelId, bool bLocked)
{
    if (FPanelLayout* Layout = Panels.Find(PanelId)) Layout->bLocked = bLocked;
}

void FCireUISettings::SanitizePreferences()
{
    ChatFontSize = SafeFloat(ChatFontSize, 11.f, 9.f, 24.f);
    CombatTextFontSize = SafeFloat(CombatTextFontSize, 24.f, 12.f, 42.f);
    WorldNumberFontSize = SafeFloat(WorldNumberFontSize, 26.f, 12.f, 48.f);
    ChatColor.R = SafeFloat(ChatColor.R, .83f, 0.f, 1.f);
    ChatColor.G = SafeFloat(ChatColor.G, .87f, 0.f, 1.f);
    ChatColor.B = SafeFloat(ChatColor.B, .88f, 0.f, 1.f);
    // Chat remains legible even if an externally edited profile contains zero alpha.
    ChatColor.A = SafeFloat(ChatColor.A, 1.f, .35f, 1.f);
    MeterMode = FMath::Clamp(MeterMode, 0, 1);
    CameraYawSensitivity=SafeFloat(CameraYawSensitivity,1,.05f,5); CameraPitchSensitivity=SafeFloat(CameraPitchSensitivity,1,.05f,5); // camera-movement: widened
    CameraDistance=SafeFloat(CameraDistance,650,300,1200); CameraFOV=SafeFloat(CameraFOV,80,55,105);
    MasterVolume=SafeFloat(MasterVolume,.85f,0,1); SFXVolume=SafeFloat(SFXVolume,.85f,0,1); UIVolume=SafeFloat(UIVolume,.7f,0,1);
    MusicVolume=SafeFloat(MusicVolume,.6f,0,1); AmbienceVolume=SafeFloat(AmbienceVolume,.8f,0,1); // audio:
    StatusFilter=FMath::Clamp(StatusFilter,0,2);
    TooltipScale=SafeFloat(TooltipScale,.8f,.6f,1.4f);
    TooltipAngleDegrees=SafeFloat(TooltipAngleDegrees,45,0,360); TooltipDistance=SafeFloat(TooltipDistance,40,16,240);
    TooltipMode=FMath::Clamp(TooltipMode,0,3);
    UIScale=SafeFloat(UIScale,1.f,.64f,1.15f); TooltipOpacity=SafeFloat(TooltipOpacity,.94f,.3f,1.f);
    TooltipDelay=SafeFloat(TooltipDelay,.12f,0.f,1.5f); SCTDirection=FMath::Clamp(SCTDirection,0,2);
    SCTSpeed=SafeFloat(SCTSpeed,1.f,.5f,2.f); SCTFadeSeconds=SafeFloat(SCTFadeSeconds,3.2f,1.5f,5.f);
    ThreatWarningPercent=SafeFloat(ThreatWarningPercent,90.f,60.f,100.f);
}

void FCireUISettings::Load(const FString& Filename)
{
    if (!Filename.IsEmpty()) ConfigFilename = FPaths::ConvertRelativePathToFull(Filename);
    Reset();
    // An independent config file avoids stale global-cache values during profile reloads.
    FConfigFile Config;
    Config.Read(ConfigFilename);
    Keybindings.LoadFrom(Config); // feat/camera-movement: own section + version; old profiles get WoW defaults
    int32 Version = LayoutVersion;
    Config.GetInt(PreferencesSection, TEXT("Version"), Version);
    if (Version < 1 || Version > LayoutVersion) return;
#define CIRE_LOAD_BOOL(Field) Config.GetBool(PreferencesSection, TEXT(#Field), Field)
    CIRE_LOAD_BOOL(bLayoutLocked);
    CIRE_LOAD_BOOL(bShowChat);
    CIRE_LOAD_BOOL(bShowCombatLog);
    CIRE_LOAD_BOOL(bShowMeter);
    CIRE_LOAD_BOOL(bCombatTextEnabled);
    // Version 1 selected one exclusive style. The revised default enables both,
    // while preserving the master switch and filters the player already chose.
    if (Version >= 2)
    {
        CIRE_LOAD_BOOL(bShowFloatingNumbers);
        CIRE_LOAD_BOOL(bShowScrollingText);
    }
    CIRE_LOAD_BOOL(bShowDamage);
    CIRE_LOAD_BOOL(bShowHealing);
    CIRE_LOAD_BOOL(bShowIncoming);
    CIRE_LOAD_BOOL(bShowOutgoing);
    CIRE_LOAD_BOOL(bInvertMouseY); CIRE_LOAD_BOOL(bMuteAudio); CIRE_LOAD_BOOL(bShowFPS); CIRE_LOAD_BOOL(bShowNetwork);
    CIRE_LOAD_BOOL(bTooltips); CIRE_LOAD_BOOL(bQuickGroundCast); CIRE_LOAD_BOOL(bTooltipOffsetLocked); CIRE_LOAD_BOOL(bDispellableOnly);
    CIRE_LOAD_BOOL(bShowStatusDurations); CIRE_LOAD_BOOL(bShowCriticalSymbol); CIRE_LOAD_BOOL(bBloom); CIRE_LOAD_BOOL(bMotionBlur);
    // Schema 4 keys; absent in older profiles, which keep the defaults.
    CIRE_LOAD_BOOL(bAutoUIScale); CIRE_LOAD_BOOL(bTooltipAvoidCenter); CIRE_LOAD_BOOL(bUnitTooltips); CIRE_LOAD_BOOL(bShowMisses);
    CIRE_LOAD_BOOL(bCritPop); CIRE_LOAD_BOOL(bSchoolColors); CIRE_LOAD_BOOL(bMergeAoE); CIRE_LOAD_BOOL(bShowThreatMeter);
    CIRE_LOAD_BOOL(bThreatWarnings); CIRE_LOAD_BOOL(bThreatSound); CIRE_LOAD_BOOL(bLevelUpEffect); CIRE_LOAD_BOOL(bShowBossFrames);
    CIRE_LOAD_BOOL(bShowActionBar2); CIRE_LOAD_BOOL(bShowActionBar3); CIRE_LOAD_BOOL(bLockActionBars);
    CIRE_LOAD_BOOL(bCameraAutoFollow); CIRE_LOAD_BOOL(bAutoReacquireTarget); // feat/camera-movement
    CIRE_LOAD_BOOL(bMusicEnabled); CIRE_LOAD_BOOL(bFootstepCameraShake); // audio: absent keys keep the defaults
    CIRE_LOAD_BOOL(bShowStats); // progression-shop
#undef CIRE_LOAD_BOOL
    Config.GetFloat(PreferencesSection, TEXT("ChatFontSize"), ChatFontSize);
    Config.GetFloat(PreferencesSection, TEXT("ChatColorR"), ChatColor.R);
    Config.GetFloat(PreferencesSection, TEXT("ChatColorG"), ChatColor.G);
    Config.GetFloat(PreferencesSection, TEXT("ChatColorB"), ChatColor.B);
    Config.GetFloat(PreferencesSection, TEXT("ChatColorA"), ChatColor.A);
    Config.GetFloat(PreferencesSection, TEXT("CombatTextFontSize"), CombatTextFontSize);
    if (Version >= 2) Config.GetFloat(PreferencesSection, TEXT("WorldNumberFontSize"), WorldNumberFontSize);
    Config.GetInt(PreferencesSection, TEXT("MeterMode"), MeterMode);
#define CIRE_LOAD_FLOAT(Field) Config.GetFloat(PreferencesSection,TEXT(#Field),Field)
    CIRE_LOAD_FLOAT(CameraYawSensitivity); CIRE_LOAD_FLOAT(CameraPitchSensitivity); CIRE_LOAD_FLOAT(CameraDistance); CIRE_LOAD_FLOAT(CameraFOV);
    CIRE_LOAD_FLOAT(MasterVolume); CIRE_LOAD_FLOAT(SFXVolume); CIRE_LOAD_FLOAT(UIVolume);
    CIRE_LOAD_FLOAT(MusicVolume); CIRE_LOAD_FLOAT(AmbienceVolume); // audio:
    CIRE_LOAD_FLOAT(TooltipScale); CIRE_LOAD_FLOAT(TooltipAngleDegrees); CIRE_LOAD_FLOAT(TooltipDistance);
    CIRE_LOAD_FLOAT(UIScale); CIRE_LOAD_FLOAT(TooltipOpacity); CIRE_LOAD_FLOAT(TooltipDelay); CIRE_LOAD_FLOAT(SCTSpeed);
    CIRE_LOAD_FLOAT(SCTFadeSeconds); CIRE_LOAD_FLOAT(ThreatWarningPercent);
#undef CIRE_LOAD_FLOAT
    Config.GetInt(PreferencesSection,TEXT("TooltipMode"),TooltipMode); Config.GetInt(PreferencesSection,TEXT("StatusFilter"),StatusFilter);
    Config.GetInt(PreferencesSection,TEXT("SCTDirection"),SCTDirection);
    // Before schema 4 the cursor-following tooltip (mode 0) was the default and
    // covered what the player was doing; upgrade it to the WoW corner anchor. Radial
    // and fixed choices were deliberate and are preserved.
    if (Version < 4 && TooltipMode == 0) TooltipMode = 3;
    SanitizePreferences();
    for (TPair<FName, FPanelLayout>& Entry : Panels)
    {
        const FString Section = PanelSection(Entry.Key);
        FCireUIRect& R = Entry.Value.Normalized;
        const FCireUIRect Default = R;
        Config.GetFloat(*Section, TEXT("X"), R.X);
        Config.GetFloat(*Section, TEXT("Y"), R.Y);
        Config.GetFloat(*Section, TEXT("Width"), R.W);
        Config.GetFloat(*Section, TEXT("Height"), R.H);
        R.W = SafeFloat(R.W, Default.W, .001f, 1.f);
        R.H = SafeFloat(R.H, Default.H, .001f, 1.f);
        R.X = SafeFloat(R.X, Default.X, 0.f, 1.f - R.W);
        R.Y = SafeFloat(R.Y, Default.Y, 0.f, 1.f - R.H);
        Config.GetBool(*Section, TEXT("Locked"), Entry.Value.bLocked);
        // Older profiles have no anchor: derive it from the saved centre, which
        // reproduces their 16:9 position exactly.
        int32 Anchor = AnchorFor(R.X, R.Y, R.X + R.W, R.Y + R.H);
        if (Version >= 4) Config.GetInt(*Section, TEXT("Anchor"), Anchor);
        Entry.Value.Anchor = FMath::Clamp(Anchor, 0, 8);
    }
}

bool FCireUISettings::Save()
{
    SanitizePreferences();
    FConfigFile Config;
    Config.SetString(PreferencesSection, TEXT("Version"), *FString::FromInt(LayoutVersion));
    Keybindings.SaveTo(Config); // feat/camera-movement
#define CIRE_SAVE_BOOL(Field) Config.SetBool(PreferencesSection, TEXT(#Field), Field)
    CIRE_SAVE_BOOL(bLayoutLocked);
    CIRE_SAVE_BOOL(bShowChat);
    CIRE_SAVE_BOOL(bShowCombatLog);
    CIRE_SAVE_BOOL(bShowMeter);
    CIRE_SAVE_BOOL(bCombatTextEnabled);
    CIRE_SAVE_BOOL(bShowFloatingNumbers);
    CIRE_SAVE_BOOL(bShowScrollingText);
    CIRE_SAVE_BOOL(bShowDamage);
    CIRE_SAVE_BOOL(bShowHealing);
    CIRE_SAVE_BOOL(bShowIncoming);
    CIRE_SAVE_BOOL(bShowOutgoing);
    CIRE_SAVE_BOOL(bInvertMouseY); CIRE_SAVE_BOOL(bMuteAudio); CIRE_SAVE_BOOL(bShowFPS); CIRE_SAVE_BOOL(bShowNetwork);
    CIRE_SAVE_BOOL(bTooltips); CIRE_SAVE_BOOL(bQuickGroundCast); CIRE_SAVE_BOOL(bTooltipOffsetLocked); CIRE_SAVE_BOOL(bDispellableOnly);
    CIRE_SAVE_BOOL(bShowStatusDurations); CIRE_SAVE_BOOL(bShowCriticalSymbol); CIRE_SAVE_BOOL(bBloom); CIRE_SAVE_BOOL(bMotionBlur);
    CIRE_SAVE_BOOL(bAutoUIScale); CIRE_SAVE_BOOL(bTooltipAvoidCenter); CIRE_SAVE_BOOL(bUnitTooltips); CIRE_SAVE_BOOL(bShowMisses);
    CIRE_SAVE_BOOL(bCritPop); CIRE_SAVE_BOOL(bSchoolColors); CIRE_SAVE_BOOL(bMergeAoE); CIRE_SAVE_BOOL(bShowThreatMeter);
    CIRE_SAVE_BOOL(bThreatWarnings); CIRE_SAVE_BOOL(bThreatSound); CIRE_SAVE_BOOL(bLevelUpEffect); CIRE_SAVE_BOOL(bShowBossFrames);
    CIRE_SAVE_BOOL(bShowActionBar2); CIRE_SAVE_BOOL(bShowActionBar3); CIRE_SAVE_BOOL(bLockActionBars);
    CIRE_SAVE_BOOL(bCameraAutoFollow); CIRE_SAVE_BOOL(bAutoReacquireTarget); // feat/camera-movement
    CIRE_SAVE_BOOL(bMusicEnabled); CIRE_SAVE_BOOL(bFootstepCameraShake); // audio:
    CIRE_SAVE_BOOL(bShowStats); // progression-shop
#undef CIRE_SAVE_BOOL
    Config.SetFloat(PreferencesSection, TEXT("ChatFontSize"), ChatFontSize);
    Config.SetFloat(PreferencesSection, TEXT("ChatColorR"), ChatColor.R);
    Config.SetFloat(PreferencesSection, TEXT("ChatColorG"), ChatColor.G);
    Config.SetFloat(PreferencesSection, TEXT("ChatColorB"), ChatColor.B);
    Config.SetFloat(PreferencesSection, TEXT("ChatColorA"), ChatColor.A);
    Config.SetFloat(PreferencesSection, TEXT("CombatTextFontSize"), CombatTextFontSize);
    Config.SetFloat(PreferencesSection, TEXT("WorldNumberFontSize"), WorldNumberFontSize);
    Config.SetString(PreferencesSection, TEXT("MeterMode"), *FString::FromInt(MeterMode));
#define CIRE_SAVE_FLOAT(Field) Config.SetFloat(PreferencesSection,TEXT(#Field),Field)
    CIRE_SAVE_FLOAT(CameraYawSensitivity); CIRE_SAVE_FLOAT(CameraPitchSensitivity); CIRE_SAVE_FLOAT(CameraDistance); CIRE_SAVE_FLOAT(CameraFOV);
    CIRE_SAVE_FLOAT(MasterVolume); CIRE_SAVE_FLOAT(SFXVolume); CIRE_SAVE_FLOAT(UIVolume);
    CIRE_SAVE_FLOAT(MusicVolume); CIRE_SAVE_FLOAT(AmbienceVolume); // audio:
    CIRE_SAVE_FLOAT(TooltipScale); CIRE_SAVE_FLOAT(TooltipAngleDegrees); CIRE_SAVE_FLOAT(TooltipDistance);
    CIRE_SAVE_FLOAT(UIScale); CIRE_SAVE_FLOAT(TooltipOpacity); CIRE_SAVE_FLOAT(TooltipDelay); CIRE_SAVE_FLOAT(SCTSpeed);
    CIRE_SAVE_FLOAT(SCTFadeSeconds); CIRE_SAVE_FLOAT(ThreatWarningPercent);
#undef CIRE_SAVE_FLOAT
    Config.SetString(PreferencesSection,TEXT("SCTDirection"),*FString::FromInt(SCTDirection));
    Config.SetString(PreferencesSection,TEXT("TooltipMode"),*FString::FromInt(TooltipMode));
    Config.SetString(PreferencesSection,TEXT("StatusFilter"),*FString::FromInt(StatusFilter));
    for (const TPair<FName, FPanelLayout>& Entry : Panels)
    {
        const FString Section = PanelSection(Entry.Key);
        const FCireUIRect& R = Entry.Value.Normalized;
        Config.SetFloat(*Section, TEXT("X"), R.X);
        Config.SetFloat(*Section, TEXT("Y"), R.Y);
        Config.SetFloat(*Section, TEXT("Width"), R.W);
        Config.SetFloat(*Section, TEXT("Height"), R.H);
        Config.SetBool(*Section, TEXT("Locked"), Entry.Value.bLocked);
        Config.SetString(*Section, TEXT("Anchor"), *FString::FromInt(Entry.Value.Anchor));
    }
    if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(ConfigFilename), true)) return false;
    // Replace only after the complete new profile has been successfully written.
    const FString Temporary = ConfigFilename + TEXT(".tmp");
    if (!Config.Write(Temporary, false)) return false;
    return IFileManager::Get().Move(*ConfigFilename, *Temporary, true, true, false, false);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCireUISettingsPersistenceTest, "Cire.UI.Settings.Persistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCireUISettingsPersistenceTest::RunTest(const FString& Parameters)
{
    const FVector2D Viewport(1280.f, 720.f);
    const FString Filename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Automation"),
        TEXT("CireUI-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".ini"));
    FCireUISettings Original;
    Original.Load(Filename);
    TestTrue(TEXT("Both combat displays enabled by default"), Original.bShowFloatingNumbers && Original.bShowScrollingText);
    TestEqual(TEXT("Default floating number size"), Original.WorldNumberFontSize, 26.f);
    TestEqual(TEXT("Default scrolling text size"), Original.CombatTextFontSize, 24.f);
    TestFalse(TEXT("Locked layout rejects changes"), Original.SetRect(TEXT("Chat"), {1, 2, 350, 180}, Viewport));
    Original.bLayoutLocked = false;
    TestTrue(TEXT("Unlocked layout accepts changes"), Original.SetRect(TEXT("Chat"), {42, 360, 350, 180}, Viewport));
    Original.SetPanelLocked(TEXT("Chat"), true);
    TestFalse(TEXT("Per-panel lock rejects changes"), Original.SetRect(TEXT("Chat"), {1, 2, 350, 180}, Viewport));
    Original.ChatFontSize = 17.f;
    Original.ChatColor = FLinearColor(.2f, .3f, .4f, .8f);
    Original.bShowCombatLog = true;
    Original.bShowFloatingNumbers = false;
    Original.bShowScrollingText = true;
    Original.WorldNumberFontSize = 31.f;
    Original.CombatTextFontSize = 19.f;
    Original.bShowHealing = false;
    Original.MeterMode = 1;
    TestTrue(TEXT("Save succeeds"), Original.Save());
    FCireUISettings Loaded;
    Loaded.Load(Filename);
    const FCireUIRect R = Loaded.GetRect(TEXT("Chat"), Viewport);
    TestTrue(TEXT("Position survives save/reload"), FMath::IsNearlyEqual(R.X, 42.f, .02f) && FMath::IsNearlyEqual(R.Y, 360.f, .02f));
    TestTrue(TEXT("Size survives save/reload"), FMath::IsNearlyEqual(R.W, 350.f, .02f) && FMath::IsNearlyEqual(R.H, 180.f, .02f));
    TestFalse(TEXT("Global lock preference survives"), Loaded.bLayoutLocked);
    TestTrue(TEXT("Panel lock survives"), Loaded.IsPanelLocked(TEXT("Chat")));
    TestEqual(TEXT("Chat size survives"), Loaded.ChatFontSize, 17.f);
    TestTrue(TEXT("Chat color survives"), Loaded.ChatColor.Equals(Original.ChatColor, .001f));
    TestTrue(TEXT("Combat log preference survives"), Loaded.bShowCombatLog);
    TestFalse(TEXT("Floating number preference survives"), Loaded.bShowFloatingNumbers);
    TestTrue(TEXT("Scrolling text preference survives independently"), Loaded.bShowScrollingText);
    TestEqual(TEXT("Floating number size survives"), Loaded.WorldNumberFontSize, 31.f);
    TestEqual(TEXT("Scrolling text size survives independently"), Loaded.CombatTextFontSize, 19.f);
    TestFalse(TEXT("Combat text filter survives"), Loaded.bShowHealing);
    TestEqual(TEXT("Meter mode survives"), Loaded.MeterMode, 1);

    for (const bool Floating : {false, true})
    {
        for (const bool Scrolling : {false, true})
        {
            Original.bShowFloatingNumbers = Floating;
            Original.bShowScrollingText = Scrolling;
            Original.bCombatTextEnabled = false;
            TestTrue(TEXT("Independent display settings save"), Original.Save());
            Loaded.Load(Filename);
            TestEqual(TEXT("All floating toggle combinations persist"), Loaded.bShowFloatingNumbers, Floating);
            TestEqual(TEXT("All scrolling toggle combinations persist"), Loaded.bShowScrollingText, Scrolling);
            TestFalse(TEXT("Disabled master switch remains off"), Loaded.bCombatTextEnabled);
        }
    }

    for (const bool LegacyWorldSpace : {false, true})
    {
        for (const bool MasterEnabled : {false, true})
        {
            FConfigFile Legacy;
            Legacy.SetString(PreferencesSection, TEXT("Version"), TEXT("1"));
            Legacy.SetBool(PreferencesSection, TEXT("bCombatTextWorldSpace"), LegacyWorldSpace);
            Legacy.SetBool(PreferencesSection, TEXT("bCombatTextEnabled"), MasterEnabled);
            Legacy.SetBool(PreferencesSection, TEXT("bShowHealing"), false);
            Legacy.SetFloat(PreferencesSection, TEXT("CombatTextFontSize"), 28.f);
            Legacy.SetFloat(TEXT("CireUI.Panel.CombatText"), TEXT("X"), .25f);
            Legacy.SetFloat(TEXT("CireUI.Panel.CombatText"), TEXT("Y"), .3f);
            TestTrue(TEXT("Legacy profile fixture writes"), Legacy.Write(Filename, false));
            Loaded.Load(Filename);
            TestTrue(TEXT("Either legacy style migrates to both displays"), Loaded.bShowFloatingNumbers && Loaded.bShowScrollingText);
            TestEqual(TEXT("Migration preserves explicit master setting"), Loaded.bCombatTextEnabled, MasterEnabled);
            TestFalse(TEXT("Migration preserves combat filters"), Loaded.bShowHealing);
            TestEqual(TEXT("Migration preserves scrolling font preference"), Loaded.CombatTextFontSize, 28.f);
            TestEqual(TEXT("Migration initializes independent floating font"), Loaded.WorldNumberFontSize, 26.f);
            TestTrue(TEXT("Migration preserves anchored text location"), FMath::IsNearlyEqual(Loaded.GetRect(TEXT("CombatText"), Viewport).X, 320.f, .02f));
            TestTrue(TEXT("Migrated profile saves"), Loaded.Save());
            Loaded.Load(Filename);
            TestTrue(TEXT("Migrated display defaults survive new format reload"), Loaded.bShowFloatingNumbers && Loaded.bShowScrollingText);
            TestEqual(TEXT("Migrated master setting survives reload"), Loaded.bCombatTextEnabled, MasterEnabled);
        }
    }

    FConfigFile Damaged;
    Damaged.SetString(PreferencesSection, TEXT("Version"), *FString::FromInt(LayoutVersion));
    Damaged.SetFloat(PreferencesSection, TEXT("ChatFontSize"), 900.f);
    Damaged.SetFloat(PreferencesSection, TEXT("ChatColorA"), -5.f);
    Damaged.SetFloat(PreferencesSection, TEXT("CombatTextFontSize"), -9.f);
    Damaged.SetFloat(PreferencesSection, TEXT("WorldNumberFontSize"), 900.f);
    Damaged.SetString(PreferencesSection, TEXT("MeterMode"), TEXT("99"));
    Damaged.SetFloat(TEXT("CireUI.Panel.Chat"), TEXT("X"), 50.f);
    Damaged.SetFloat(TEXT("CireUI.Panel.Chat"), TEXT("Y"), -9.f);
    Damaged.SetFloat(TEXT("CireUI.Panel.Chat"), TEXT("Width"), 9.f);
    Damaged.SetFloat(TEXT("CireUI.Panel.Chat"), TEXT("Height"), -5.f);
    TestTrue(TEXT("Corrupted profile fixture writes"), Damaged.Write(Filename, false));
    Loaded.Load(Filename);
    TestEqual(TEXT("Oversized font clamped"), Loaded.ChatFontSize, 24.f);
    TestEqual(TEXT("Undersized combat text clamped"), Loaded.CombatTextFontSize, 12.f);
    TestEqual(TEXT("Oversized floating number font clamped"), Loaded.WorldNumberFontSize, 48.f);
    TestEqual(TEXT("Transparent chat corrected"), Loaded.ChatColor.A, .35f);
    TestEqual(TEXT("Invalid meter enum corrected"), Loaded.MeterMode, 1);
    for (const FName Id : Loaded.GetPanelIds())
    {
        for (const FVector2D Size : {FVector2D(320, 180), FVector2D(1280, 720), FVector2D(2560, 1080)})
        {
            const FCireUIRect Bounds = Loaded.GetRect(Id, Size);
            TestTrue(TEXT("Loaded panel remains on screen"), Bounds.X >= 0.f && Bounds.Y >= 0.f
                && Bounds.W > 0.f && Bounds.H > 0.f && Bounds.X + Bounds.W <= Size.X + .01f
                && Bounds.Y + Bounds.H <= Size.Y + .01f);
        }
    }
    Loaded.Reset();
    TestTrue(TEXT("Reset locks layout"), Loaded.bLayoutLocked);
    TestFalse(TEXT("Reset clears panel locks"), Loaded.IsPanelLocked(TEXT("Chat")));
    TestEqual(TEXT("Reset restores font"), Loaded.ChatFontSize, 11.f);
    TestTrue(TEXT("Reset enables both combat displays"), Loaded.bShowFloatingNumbers && Loaded.bShowScrollingText);
    TestTrue(TEXT("Reset enables combat master switch"), Loaded.bCombatTextEnabled);
    TestEqual(TEXT("Reset restores floating font"), Loaded.WorldNumberFontSize, 26.f);
    TestEqual(TEXT("Reset restores scrolling font"), Loaded.CombatTextFontSize, 24.f);
    TestTrue(TEXT("Reset restores reference layout"), FMath::IsNearlyEqual(Loaded.GetRect(TEXT("Chat"), Viewport).Y, 528.f));
    IFileManager::Get().Delete(*Filename, false, true, true);
    IFileManager::Get().Delete(*(Filename + TEXT(".tmp")), false, true, true);
    return true;
}
#endif
