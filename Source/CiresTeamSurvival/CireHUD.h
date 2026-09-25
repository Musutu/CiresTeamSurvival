#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CireUISettings.h"
#include "Scalability.h"
#include "CireDeveloperTools.h"
#include "CireMobility.h"
#include "CireUIStyle.h"
#include "CireEffects.h"
#include "CireWaves.h" // wave-director
#include "CireHUD.generated.h"

class ACireHero;
class ACireController;
class ACireGameState;
class ACireMonster;
class UFont;
class UFontFace;
class USoundBase;


/** A transient WoW-style centre-screen alert (aggro, threat, level). */
struct FCireHUDAlert
{
    FString Title, Subtitle;
    FLinearColor Color = FLinearColor::White;
    double Start = -100.0;
    float Duration = 2.6f;
};

/** A running level-up burst on one hero. */
struct FCireLevelBurst
{
    TWeakObjectPtr<ACireHero> Hero;
    int32 Level = 0;
    double Start = 0.0;
    bool bLocal = false;
};

UCLASS()
class CIRESTEAMSURVIVAL_API ACireHUD : public AHUD
{
    GENERATED_BODY()
public:
    ACireHUD();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void DrawHUD() override;
    bool IsEditingLayout() const { return bEditLayout; }
    bool IsBlockingGameplayInput() const { return bEditLayout || bSettings || bQuickKeybind || bRouteEditor; } // nav-paths: + route editor
    // nav-paths: in-world route editor (F8 > Developer > Paths; CireRouteEditorHUD.cpp, Docs/Navigation.md).
    bool IsRouteEditorOpen() const { return bRouteEditor; }
    void OpenRouteEditor(bool bOpen);
#if !UE_BUILD_SHIPPING
    /** Gallery/tests: frame the editor camera and hold a waypoint drag at a world point (bRelease ends it). */
    void DebugRouteView(const FVector& Focus, float Distance, float Pitch, float Yaw);
    void DebugRouteDrag(int32 Team, int32 Index, const FVector& World, bool bRelease);
#endif
    /** WoW Quick Keybind mode: hover an action button and press a key to bind it. */
    void ToggleQuickKeybind();
    bool IsQuickKeybind() const { return bQuickKeybind; }
    bool IsPointerOverInterface() const;
    bool HandleEscape();
    void ToggleLayoutEditor();
    void ToggleSettings();
    void ToggleDeveloperTools();
    void HandleMouseWheel(float Delta);
    FCireUISettings UISettings;
    void RevertVideoPreview();
    bool DraftRosterSlot(int32 Slot);
    // champion-draft: level-up skill offer (CireSkillOfferHUD.cpp). Open = cards shown and
    // action-bar keys 1-4 pick; collapsed/deferred = a pulsing reminder, combat keys cast.
    bool IsSkillOfferOpen() const;
    void SetSkillOfferOpen(bool bOpen);
    void ChangeDraftRosterPage(int32 Delta);
    FString DraftRosterIdForSlot(int32 Slot) const;
    int32 DraftRosterPageCount() const;
#if !UE_BUILD_SHIPPING
    void DebugOptionsPage(int32 Tab,int32 Page,bool bOpen=true) { OptionsTab=Tab;InterfacePage=Page;if(Tab==0)ControlsPage=Page;if(Tab==5)DeveloperPage=Page;bSettings=bOpen;bVideoLoaded=false; }
    void DebugKeybindCategory(int32 Category) { KeybindCategory=Category; }
    void DebugDraftRosterPage(int32 Page) { RosterPage=FMath::Clamp(Page,0,DraftRosterPageCount()-1); }
    void DebugTooltip(const FString& Title,const FString& Body,FVector2D Cursor);
    void DebugTooltipClear() { bDebugTooltip=false; }
    FCireUIRect DebugTooltipRect() const { return LastTooltipRect; }
    int32 DebugTooltipBodyLines() const { return LastTooltipBodyLines; }
    float DebugTooltipBodyFontSize() const { return LastTooltipBodyFontSize; }
    FVector2D DebugTooltipViewport() const { return FVector2D(ViewW,ViewH); }
    /** WoW UI gallery hooks: force a unit tooltip, a level-up burst or an alert. */
    void DebugUnitTooltip(AActor* Unit,FVector2D Cursor) { DebugHoverUnit=Unit;DebugHoverCursor=Cursor; }
    void DebugAbilityTooltip(const FString& Id,FVector2D Cursor) { DebugAbilityId=Id;DebugHoverCursor=Cursor; }
    void DebugLevelUp(ACireHero* Hero,bool bLocal);
    void DebugAlert(const FString& Title,const FString& Subtitle,FLinearColor Color) { ShowAlert(Title,Subtitle,Color,false); }
    float DebugScale() const { return Scale; }
    void DebugSetPointer(FVector2D Logical) { DebugPointer=Logical; }
    bool DebugCalloutActive(FName& OutId) const { if(!EffectCallouts.Active.IsSet())return false; OutId=EffectCallouts.Active->Id; return true; }
    const FString& DebugLastTooltipTitle() const { return LastTooltipTitle; }
    FVector2D DebugPointer = FVector2D(-1,-1);
    FString LastTooltipTitle;
    FCireUIRect PanelRectForTest(FName Id) const { return PanelRect(Id); }
    bool DebugFontsReady() const { return CireUIStyle::Assets().bFonts; }
#endif
    // progression-shop: hooks for CireShopUI (shop, bag/belt/teleport bar, stats window, loot toasts).
    FCireUIPainter ScreenPainter() const { FCireUIPainter P; P.Canvas=Canvas; P.Scale=Scale; return P; }
    FVector2D LogicalViewport() const { return FVector2D(ViewW,ViewH); }
    FVector2D LogicalMouse() const { return FVector2D(MX,MY); }
    bool HasClick() const { return Clicked; }
    bool TakeClick() { const bool bWasClicked=Clicked; Clicked=false; return bWasClicked; }
    bool IsInteractive() const { return !bEditLayout&&!bSettings; }
    bool IsModalOpen() const { return bModal; }
    void RegisterPanel(FName Id) { VisiblePanels.AddUnique(Id); }
    FCireUIRect LayoutRect(FName Id) const { return PanelRect(Id); }
    void SetTooltip(const FString& Title,const FString& Body) { TooltipTitle=Title; TooltipBody=Body; }
    void PlayInterfaceSound(int32 Index,float Volume=1.f) { PlayWowSound(Index,Volume); }
    // F8 > Economy page (CireEconomyPage.cpp).
    void DrawEconomyPage(float X,float Y);
    // Skill Shop READY button shares the match plate's breather Ready state (wave-director).
    bool IsBreatherReadyLocal(int32 Wave) const { return BreatherReadyWave==Wave&&bBreatherReadyLocal; }
    void SetBreatherReadyLocal(int32 Wave,bool bReady) { BreatherReadyWave=Wave; bBreatherReadyLocal=bReady; }
    // progression-shop: end
private:
    void Panel(float X, float Y, float W, float H, FLinearColor Color);
    void Label(const FString& Text, float X, float Y, float Size, FLinearColor Color=FLinearColor::White);
    /** Crisp TTF text at its rendered pixel size with an optional 1px outline and drop shadow. */
    void TextFx(const FString& Text, float X, float Y, float Size, FLinearColor Color, ECireFont Font, bool bOutline, bool bShadow=true);
    float TextWidthFont(const FString& Text, float Size, ECireFont Font) const;
    UFont* ResolveFont(ECireFont Font, const FString& Text, float Size) const;
    void BuildFonts();
    /** Filled circle / ring / triangle in panel space. */
    void Disc(float X, float Y, float R, FLinearColor Color, int32 Sides=28);
    void Circle(float X, float Y, float R, FLinearColor Color, float Width=1.f, int32 Sides=36);
    void Tri(FVector2D A, FVector2D B, FVector2D C, FLinearColor Color);
    void PlayWowSound(int32 Index, float Volume=1.f);
    FCireUIPainter Painter() const;
    void Bar(float X, float Y, float W, float H, float Fraction, FLinearColor Color);
    void Line(float X1, float Y1, float X2, float Y2, FLinearColor Color, float Width=1.f);
    void Frame(float X, float Y, float W, float H, FLinearColor Accent);
    void Icon(const FString& Id, float X, float Y, float Size, FLinearColor Color);
    void Wrapped(const FString& Text, float X, float Y, float Width, float Size, FLinearColor Color, int32 MaxLines);
    float TextWidth(const FString& Text, float Size) const;
    bool Hit(float X, float Y, float W, float H) const;
    void UsePanel(FName Id, float DesignW, float DesignH);
    void ResetTransform();
    void LayoutInteraction();
    void DrawLayoutEditor();
    void DrawSettings();
    void DrawModal(ACireHero* Hero, ACireController* Controller, ACireGameState* State);
    void DrawDraftRoster(ACireHero* Hero, ACireController* Controller);
    void DrawSkillOffer(ACireHero* Hero, ACireController* Controller);        // champion-draft: modal cards
    void DrawSkillOfferExtras(ACireHero* Hero, ACireController* Controller);  // champion-draft: reminder, pick animation, toggle key
    void DrawPlayer(ACireHero* Hero);
    void DrawParty(ACireHero* Hero, ACireController* Controller);
    void DrawUnit(AActor* Actor, const FString& Caption, bool bFocus);
    // ---- WoW-style frames and feedback (CireHUDWow.cpp) ----
    void DrawPortrait(AActor* Actor, float CX, float CY, float R, bool bSmall);
    void DrawBossFrames(ACireHero* Hero, ACireController* Controller);
    void DrawThreatMeter(ACireHero* Hero, ACireController* Controller);
    void UpdateThreatAlerts(ACireHero* Hero);
    void OnAggroEvent(const struct FCireAggroEvent& Event);
    void DrawAlert();
    void UpdateBanners(ACireHero* Hero, ACireGameState* State);
    void DrawBanners();
    void ShowAlert(const FString& Title, const FString& Subtitle, FLinearColor Color, bool bSound, int32 SoundIndex=1);
    void UpdateLevelUps(ACireHero* Hero);
    void DrawLevelUps(ACireHero* Hero);
    void UpdateHoverUnit(ACireHero* Hero);
    bool DrawUnitTooltip(AActor* Unit, FVector2D Cursor);
    void UnitTip(AActor* Unit, float X, float Y, float W, float H);
    FCireUIRect PlaceTooltip(float W, float H, FVector2D Cursor) const;
    void TooltipBox(float X, float Y, float W, float H, FLinearColor Border);
    void DrawMatch(ACireGameState* State);
    void DrawMinimap(ACireHero* Hero, ACireGameState* State);
    void DrawSkills(ACireHero* Hero, ACireController* Controller);
    // ---- action bars / keybinding (CireHUDActionBars.cpp) ----
    void DrawActionBars(ACireHero* Hero, ACireController* Controller);
    bool DrawActionButton(ACireHero* Hero, ACireController* Controller, int32 Bar, int32 Index, float X, float Y, float Size);
    void DrawAbilityTooltip(const FString& Id, FVector2D Cursor);
    void UpdateQuickKeybind();
    void DrawQuickKeybind();
    void DrawKeybindingsPage(float L, float Top);
    /** ui-themes: Options > Interface > UI theme (three live preview cards). */
    void DrawThemePicker(float L, float B);
    FString QuickActionName(FName Action) const;
    void DrawChat(ACireController* Controller);
    void DrawMeters(ACireHero* Hero, ACireController* Controller);
    void DrawCombatText(ACireHero* Hero, ACireController* Controller);
    void DrawNameplates(ACireHero* Hero);
    void DrawStatuses(AActor* Actor,float X,float Y,float Size,int32 MaxIcons=4);
    FString EffectSigil(FName Id,const struct FCireEffectInfo& I);
    FString StatusIconId(const struct FCireEffectInfo& I);
    void DrawEffectIcon(const struct FCireActiveEffect& E,const struct FCireEffectInfo& I,float X,float Y,float Size,float Remaining,float Total);
    // ---- buff/debuff callouts, CC and cast bars (CireHUDEffects.cpp) ----
    void UpdateEffectCallouts(ACireHero* Hero);
    void DrawEffectCallouts(ACireHero* Hero);
    void DrawControlEdge(ACireHero* Hero);
    void DrawPlayerCastBar(ACireHero* Hero);
    /** Cast bar with interrupt/silence flash; returns true when something was drawn. */
    bool DrawCastBar(const AActor* Unit,float X,float Y,float W,float H,float TextSize,bool bShowTime=true);
    void DrawControlBadge(const AActor* Unit,float X,float Y,float Size);
    void DrawOverheadStatus(const AActor* Unit,float CX,float BottomY,float Fade,bool bNear);
    FCireCalloutQueue EffectCallouts;
    TSet<FName> SeenEffectIds;
    bool bEffectsSeeded = false;
    TMap<FString, FVector2D> OverheadSeen;
    void DrawPet(ACireHero* Hero,ACireController* Controller);
    void Tip(const FString& Title,const FString& Body,float X,float Y,float W,float H);
    void DrawTooltip();
    void PlayUIFeedback();
    void DrawDiagnostics();
    void DrawDeveloperPanel(float X,float Y);
    // nav-paths: F8 > Paths page, the in-world editor overlay/toolbar and the minimap navmesh overlay.
    void DrawRoutePage(float X,float Y);
    void TickRouteEditor();
    void DrawRouteMinimap(int32 Team,TFunctionRef<FVector2D(FVector,int32)> Map);
    bool bRouteEditor=false,bMinimapNav=false;
    TSharedPtr<struct FCireRouteEditorState> RouteEditor;
    // wave-director: F8 > Waves live wave composer (CireWaveEditor.cpp).
    void DrawWaveEditor(float X,float Y);
    FCireWaveConfig WaveDraft;
    bool bWaveDraftLoaded=false;
    int32 WaveSelected=0,WaveListScroll=0;
    bool bBreatherReadyLocal=false; int32 BreatherReadyWave=-1; // wave-director: breather Ready button
    void DrawDeveloperLauncher();
    FCireUIRect DeveloperLauncherRect() const;
    FCireUIRect PanelRect(FName Id) const;
    /** Top edge of the highest visible action bar (for reminders placed above the bars). */
    float ActionBarsTop() const;
    /** Centre X of the free band between side frames (banners, alerts). */
    float CentreGapX(float Top, float Bottom) const;
    bool IsDeveloperLauncherVisible() const;
    bool IsInBossFrames(const AActor* Actor) const;
    bool bShowDevLauncher = false;
    TArray<FBox2D> LastPanelBoxes;
    TArray<TWeakObjectPtr<AActor>> BossFrameUnits;
    FCireMovementTuning MovementDraft;
    bool bMovementLoaded=false;
    bool DrawReplayScreen();
    FCireDeveloperSettings DeveloperDraft;
    bool bDeveloperLoaded=false,bLabPlayerMode=false,bLabArena=false,bReplayListLoaded=false;
    int32 DeveloperPage=0,LabWave=1,LabEnemyKind=-1,LabTeamSize=5,LabEnemyCount=5,ReplayOffset=0;
    float LabSeconds=60;
    FString DeveloperMessage;
    FString TooltipTitle,TooltipBody;
    FString TooltipAbility;
    FString EditHelpTitle, EditHelpBody; // panel descriptions, shown only in F10 layout editing
    FCireUIRect TooltipRegion; // logical rect of the element whose Tip() won this frame
    FName TooltipAbilitySlot, HoverSlot, DragSlot, QuickHold;
    FString DragAbility, QuickMessage;
    FVector2D DragStart = FVector2D::ZeroVector;
    bool bBarDragging = false, bBarPressCandidate = false, bQuickKeybind = false, bQuickCapturing = false, KeybindWheelArmed = false;
    double QuickMessageAt = -100.0;
    int32 KeybindCategory = 4, KeybindScroll = 0, ControlsPage = 1;
    TMap<FString, float> LastCooldown, CooldownMax;
    TMap<FString, double> PressFlashAt, ReadyFlashAt;
    // WoW interface state.
    UPROPERTY() TArray<TObjectPtr<UObject>> WowAssetRefs;
    TMap<uint64, FCireBarTrail> BarTrails;
    float PanelAlpha = 1.f;
    int32 BannerSeenPhase = -1, BannerSeenWave = 0, BannerSeenCleared = 0, BannerCountdownWave = 0, BannerSeenChallengeTier = 0;
    TSet<TWeakObjectPtr<ACireMonster>> BannerSeenBosses;
    FName BannerPendingDistrict, BannerShownDistrict;
    double BannerDistrictSince = 0.0, TargetChangedAt = -100.0;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> WowSounds;
    ECireFont NextFont = ECireFont::Auto;
    TWeakObjectPtr<AActor> HoverUnit, TooltipUnit, LastTargetSeen;
    FDelegateHandle AggroHandle;
    TMap<TWeakObjectPtr<ACireHero>, int32> SeenLevels;
    TArray<FCireLevelBurst> LevelBursts;
    FCireHUDAlert Alert;
    double LastThreatWarning = -100.0;
    FString TooltipHoverKey;
    double TooltipHoverStart = 0.0;
    float PendingUIScale = -1.f;
    TWeakObjectPtr<AActor> DebugHoverUnit;
    FString DebugAbilityId;
    FVector2D DebugHoverCursor = FVector2D::ZeroVector;
#if !UE_BUILD_SHIPPING
    bool bDebugTooltip=false;
    FString DebugTooltipTitle,DebugTooltipBody;
    FVector2D DebugTooltipCursor;
    FCireUIRect LastTooltipRect;
    int32 LastTooltipBodyLines=0;
    float LastTooltipBodyFontSize=0;
#endif
    int32 OptionsTab=0,InterfacePage=0;
    bool bVideoLoaded=false,bVideoPending=false;
    FIntPoint VideoResolution=FIntPoint(1920,1080),PreviousResolution;
    int32 VideoMode=1,VideoQuality=2,PreviousMode=1;
    float VideoFPS=120,PreviousFPS=120;
    bool bVideoVSync=true,bPreviousVSync=true;
    Scalability::FQualityLevels PreviousQuality;
    double VideoDeadline=0;
    float MX=0, MY=0, Scale=1, ViewW=1280, ViewH=720;
    FVector2D Origin=FVector2D::ZeroVector;
    FVector2D Stretch=FVector2D(1,1);
    bool Clicked=false, bModal=false, bEditLayout=false, bSettings=false;
    bool bResizing=false, bWasMouseDown=false;
    FName DragPanel=NAME_None;
    FVector2D DragOrigin=FVector2D::ZeroVector;
    FCireUIRect DragRect;
    TArray<FName> VisiblePanels;
    int32 ChatScroll=0;
    int32 RosterPage=0;
};
