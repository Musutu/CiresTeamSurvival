#include "CireUISettings.h"
#include "CireEffects.h"
#if !UE_BUILD_SHIPPING
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include <limits>

bool CireOptions::RunSettingsSmoke()
{
    bool Pass=true;int32 Count=0;
    auto Check=[&](bool Value,const TCHAR* Name){++Count;Pass &= Value;if(!Value)UE_LOG(LogTemp,Error,TEXT("CIRE_OPTIONS_ASSERT %s"),Name);};
    const FString Directory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("OptionsTests"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString File=FPaths::Combine(Directory,TEXT("profile-")+FGuid::NewGuid().ToString()+TEXT(".ini"));
    FCireUISettings A;A.Load(File);
    Check(A.CameraYawSensitivity==1&&A.CameraPitchSensitivity==1&&A.CameraDistance==650,TEXT("camera defaults"));
    // Schema 4: the WoW corner anchor (mode 3) is the default tooltip position.
    Check(A.TooltipMode==3&&A.StatusFilter==0&&A.bShowCriticalSymbol,TEXT("interface defaults"));
    Check(A.bAutoUIScale&&FMath::IsNearlyEqual(A.UIScale,1.f)&&A.bUnitTooltips&&A.bShowThreatMeter&&A.bThreatWarnings&&A.bLevelUpEffect,TEXT("wow ui defaults"));
    Check(FMath::IsNearlyEqual(A.TooltipScale,.8f),TEXT("compact tooltip default"));
    Check(!A.bQuickGroundCast,TEXT("ground aiming requires confirmation by default"));A.bQuickGroundCast=true;
    A.CameraYawSensitivity=2.3f;A.CameraPitchSensitivity=.45f;A.bInvertMouseY=true;A.CameraDistance=950;A.CameraFOV=92;
    A.MasterVolume=.3f;A.SFXVolume=.7f;A.UIVolume=.2f;A.bMuteAudio=true;
    A.TooltipScale=1.15f;A.TooltipMode=2;A.TooltipAngleDegrees=225;A.TooltipDistance=160;A.bTooltipOffsetLocked=false;
    A.StatusFilter=2;A.bDispellableOnly=true;A.bShowStatusDurations=false;A.bShowCriticalSymbol=false;A.bShowFPS=true;A.bShowNetwork=false;
    A.bBloom=false;A.bMotionBlur=true;A.bLayoutLocked=false;
    Check(A.SetRect(TEXT("Tooltip"),{330,210,410,175},FVector2D(1280,720)),TEXT("tooltip movable"));
    A.SetPanelLocked(TEXT("Tooltip"),true);
    Check(!A.SetRect(TEXT("Tooltip"),{0,0,100,100},FVector2D(1280,720)),TEXT("tooltip lock enforced"));
    Check(A.Save(),TEXT("save v3"));
    FCireUISettings B;B.Load(File);
    Check(FMath::IsNearlyEqual(B.CameraYawSensitivity,2.3f)&&FMath::IsNearlyEqual(B.CameraPitchSensitivity,.45f)&&B.bInvertMouseY,TEXT("camera roundtrip"));
    Check(B.CameraDistance==950&&B.CameraFOV==92,TEXT("view roundtrip"));
    Check(FMath::IsNearlyEqual(B.MasterVolume,.3f)&&FMath::IsNearlyEqual(B.SFXVolume,.7f)&&FMath::IsNearlyEqual(B.UIVolume,.2f)&&B.bMuteAudio,TEXT("audio roundtrip"));
    Check(B.TooltipMode==2&&B.TooltipAngleDegrees==225&&B.TooltipDistance==160&&!B.bTooltipOffsetLocked,TEXT("tooltip radial roundtrip"));
    Check(FMath::IsNearlyEqual(B.TooltipScale,1.15f),TEXT("tooltip scale roundtrip"));
    Check(B.bQuickGroundCast,TEXT("quick ground cast roundtrip"));
    Check(B.StatusFilter==2&&B.bDispellableOnly&&!B.bShowStatusDurations&&!B.bShowCriticalSymbol,TEXT("status crit roundtrip"));
    Check(B.bShowFPS&&!B.bShowNetwork&&!B.bBloom&&B.bMotionBlur,TEXT("diagnostics graphics roundtrip"));
    const auto R=B.GetRect(TEXT("Tooltip"),FVector2D(1280,720));Check(FMath::IsNearlyEqual(R.X,330.f)&&B.IsPanelLocked(TEXT("Tooltip")),TEXT("tooltip layout persisted"));
    B.CameraYawSensitivity=std::numeric_limits<float>::quiet_NaN();B.CameraPitchSensitivity=99;B.CameraDistance=-5;B.CameraFOV=150;
    B.MasterVolume=-4;B.SFXVolume=9;B.UIVolume=std::numeric_limits<float>::quiet_NaN();B.TooltipMode=99;B.StatusFilter=-1;
    B.TooltipScale=99;B.TooltipAngleDegrees=999;B.TooltipDistance=-1;Check(B.Save(),TEXT("sanitize save"));
    FCireUISettings C;C.Load(File);
    Check(C.CameraYawSensitivity==1&&C.CameraPitchSensitivity==5&&C.CameraDistance==300&&C.CameraFOV==105,TEXT("camera finite clamp"));
    Check(C.MasterVolume==0&&C.SFXVolume==1&&FMath::IsNearlyEqual(C.UIVolume,.7f),TEXT("audio finite clamp"));
    Check(C.TooltipMode==3&&C.StatusFilter==0&&C.TooltipAngleDegrees==360&&C.TooltipDistance==16,TEXT("interface bounded"));
    Check(FMath::IsNearlyEqual(C.TooltipScale,1.4f),TEXT("oversized tooltip scale clamped on save"));
    const FString SmallTooltip=TEXT("[CireUI.Preferences]\nVersion=3\nTooltipScale=-1\nbShowChat=False\nCameraDistance=950\n");
    Check(FFileHelper::SaveStringToFile(SmallTooltip,*File),TEXT("small tooltip fixture write"));
    C.Load(File);
    Check(FMath::IsNearlyEqual(C.TooltipScale,.6f)&&!C.bShowChat&&C.CameraDistance==950,TEXT("undersized tooltip clamped on load without changing other preferences"));
    C.TooltipScale=std::numeric_limits<float>::quiet_NaN();
    Check(C.Save(),TEXT("nonfinite tooltip fixture save"));
    C.Load(File);
    Check(FMath::IsNearlyEqual(C.TooltipScale,.8f),TEXT("nonfinite tooltip scale restores default"));
    const FString ExistingV3=TEXT("[CireUI.Preferences]\nVersion=3\nTooltipMode=2\nTooltipAngleDegrees=225\nTooltipDistance=160\nbTooltips=False\nbShowChat=False\nMasterVolume=0.3\n");
    Check(FFileHelper::SaveStringToFile(ExistingV3,*File),TEXT("existing v3 fixture without tooltip scale write"));
    C.TooltipScale=1.3f;C.Load(File);
    Check(FMath::IsNearlyEqual(C.TooltipScale,.8f),TEXT("existing v3 missing tooltip scale uses default on reload"));
    Check(!C.bQuickGroundCast,TEXT("existing profiles retain confirmation casting default"));
    Check(C.TooltipMode==2&&C.TooltipAngleDegrees==225&&C.TooltipDistance==160&&!C.bTooltips&&!C.bShowChat&&FMath::IsNearlyEqual(C.MasterVolume,.3f),TEXT("existing v3 other preferences preserved"));
    C.TooltipScale=1.2f;C.Reset();
    Check(FMath::IsNearlyEqual(C.TooltipScale,.8f),TEXT("reset restores compact tooltip default"));
    const FString V2=TEXT("[CireUI.Preferences]\nVersion=2\nbShowFloatingNumbers=False\nbShowScrollingText=True\nbShowChat=False\nWorldNumberFontSize=34\n[CireUI.Panel.Player]\nX=0.1\nY=0.2\nWidth=0.3\nHeight=0.2\nLocked=True\n");
    Check(FFileHelper::SaveStringToFile(V2,*File),TEXT("legacy fixture write"));
    FCireUISettings D;D.Load(File);
    Check(!D.bShowFloatingNumbers&&D.bShowScrollingText&&!D.bShowChat&&D.WorldNumberFontSize==34,TEXT("v2 preferences preserved"));
    // A pre-schema-4 profile never chose a tooltip mode, so it gets the new corner-anchor default.
    Check(D.CameraYawSensitivity==1&&D.MasterVolume==.85f&&D.TooltipMode==3&&FMath::IsNearlyEqual(D.TooltipScale,.8f),TEXT("v2 new defaults migrated"));
    const auto Legacy=D.GetRect(TEXT("Player"),FVector2D(1280,720));Check(FMath::IsNearlyEqual(Legacy.X,128.f)&&FMath::IsNearlyEqual(Legacy.Y,144.f)&&D.IsPanelLocked(TEXT("Player")),TEXT("v2 custom panel preserved"));
    Check(D.GetPanelIds().Contains(TEXT("Tooltip"))&&D.GetPanelIds().Contains(TEXT("Pet")),TEXT("new panels available after migration"));
    // Schema 3 -> 4: cursor mode (the old default) moves to the corner anchor; radial choice is kept.
    const FString OldCursor=TEXT("[CireUI.Preferences]\nVersion=3\nTooltipMode=0\nbShowChat=False\n");
    Check(FFileHelper::SaveStringToFile(OldCursor,*File),TEXT("v3 cursor fixture write"));
    FCireUISettings E;E.Load(File);
    Check(E.TooltipMode==3&&!E.bShowChat&&E.bAutoUIScale,TEXT("v3 cursor tooltip migrated to WoW anchor"));
    // Schema 4 roundtrip: interface scale, SCT, threat, tooltip extras and panel anchors.
    E.bAutoUIScale=false;E.UIScale=.72f;E.TooltipOpacity=.5f;E.TooltipDelay=.4f;E.SCTDirection=2;E.SCTSpeed=1.6f;E.SCTFadeSeconds=4.2f;
    E.bSchoolColors=false;E.bThreatSound=false;E.ThreatWarningPercent=75;E.bShowBossFrames=false;E.bLayoutLocked=false;
    Check(E.SetRect(TEXT("Minimap"),{1000,30,220,178},FVector2D(1280,720)),TEXT("anchored panel movable"));
    Check(E.Save(),TEXT("save v4"));
    FCireUISettings F;F.Load(File);
    Check(!F.bAutoUIScale&&FMath::IsNearlyEqual(F.UIScale,.72f)&&FMath::IsNearlyEqual(F.TooltipOpacity,.5f)&&FMath::IsNearlyEqual(F.TooltipDelay,.4f),TEXT("v4 scale/tooltip roundtrip"));
    Check(F.SCTDirection==2&&FMath::IsNearlyEqual(F.SCTSpeed,1.6f)&&FMath::IsNearlyEqual(F.SCTFadeSeconds,4.2f)&&!F.bSchoolColors,TEXT("v4 sct roundtrip"));
    Check(!F.bThreatSound&&F.ThreatWarningPercent==75&&!F.bShowBossFrames,TEXT("v4 threat roundtrip"));
    // A right-anchored panel keeps its distance from the right edge when the logical viewport grows (UI scale < 1).
    const auto Wide=F.GetRect(TEXT("Minimap"),FVector2D(1828,1028));
    Check(FMath::IsNearlyEqual(Wide.X+Wide.W,1828.f-60.f,.6f)&&FMath::IsNearlyEqual(Wide.W,220.f,.6f),TEXT("v4 right anchor preserved"));
    F.UIScale=9;F.TooltipOpacity=-2;F.SCTDirection=7;F.ThreatWarningPercent=5;Check(F.Save(),TEXT("v4 sanitize save"));
    FCireUISettings G;G.Load(File);
    Check(FMath::IsNearlyEqual(G.UIScale,1.15f)&&FMath::IsNearlyEqual(G.TooltipOpacity,.3f)&&G.SCTDirection==2&&G.ThreatWarningPercent==60,TEXT("v4 bounds"));
    Check(IFileManager::Get().Delete(*File),TEXT("isolated fixture cleanup"));
    UE_LOG(LogTemp,Display,TEXT("CIRE_OPTIONS_SETTINGS_%s checks=%d schema=5"),Pass?TEXT("PASS"):TEXT("FAIL"),Count);
    // wow-ui: buff/debuff registry coverage, symbol formatting, callout throttling, cast states.
    Pass &= CireEffects::RunSmoke();
    return Pass;
}
#endif
