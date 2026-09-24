#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "CireUISettings.h"
#include "Scalability.h"
#include "CireDeveloperTools.h"
#include "CireMobility.h"
#include "CireHUD.generated.h"

class ACireHero;
class ACireController;
class ACireGameState;
class ACireMonster;
class UFont;
class UFontFace;
class USoundBase;

/** Font hierarchy of the WoW-style interface. Auto picks by text content and size. */
enum class ECireFont : uint8 { Auto, Body, Heading, Bold, Numbers };

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
    virtual void DrawHUD() override;
    bool IsEditingLayout() const { return bEditLayout; }
    bool IsBlockingGameplayInput() const { return bEditLayout || bSettings; }
    bool IsPointerOverInterface() const;
    bool HandleEscape();
    void ToggleLayoutEditor();
    void ToggleSettings();
    void ToggleDeveloperTools();
    void HandleMouseWheel(float Delta);
    FCireUISettings UISettings;
    void RevertVideoPreview();
    bool DraftRosterSlot(int32 Slot);
    void ChangeDraftRosterPage(int32 Delta);
    FString DraftRosterIdForSlot(int32 Slot) const;
    int32 DraftRosterPageCount() const;
#if !UE_BUILD_SHIPPING
    void DebugOptionsPage(int32 Tab,int32 Page,bool bOpen=true) { OptionsTab=Tab;InterfacePage=Page;if(Tab==5)DeveloperPage=Page;bSettings=bOpen;bVideoLoaded=false; }
    void DebugDraftRosterPage(int32 Page) { RosterPage=FMath::Clamp(Page,0,DraftRosterPageCount()-1); }
    void DebugTooltip(const FString& Title,const FString& Body,FVector2D Cursor);
    void DebugTooltipClear() { bDebugTooltip=false; }
    FCireUIRect DebugTooltipRect() const { return LastTooltipRect; }
    int32 DebugTooltipBodyLines() const { return LastTooltipBodyLines; }
    float DebugTooltipBodyFontSize() const { return LastTooltipBodyFontSize; }
    FVector2D DebugTooltipViewport() const { return FVector2D(ViewW,ViewH); }
    /** WoW UI gallery hooks: force a unit tooltip, a level-up burst or an alert. */
    void DebugUnitTooltip(AActor* Unit,FVector2D Cursor) { DebugHoverUnit=Unit;DebugHoverCursor=Cursor; }
    void DebugLevelUp(ACireHero* Hero,bool bLocal);
    void DebugAlert(const FString& Title,const FString& Subtitle,FLinearColor Color) { ShowAlert(Title,Subtitle,Color,false); }
    float DebugScale() const { return Scale; }
    bool DebugFontsReady() const { return WowFonts.Num()==4; }
#endif
private:
    void Panel(float X, float Y, float W, float H, FLinearColor Color);
    void Label(const FString& Text, float X, float Y, float Size, FLinearColor Color=FLinearColor::White);
    /** Crisp TTF text at its rendered pixel size with an optional 1px outline and drop shadow. */
    void TextFx(const FString& Text, float X, float Y, float Size, FLinearColor Color, ECireFont Font, bool bOutline, bool bShadow=true);
    float TextWidthFont(const FString& Text, float Size, ECireFont Font) const;
    UFont* ResolveFont(ECireFont Font, const FString& Text, float Size) const;
    float FontPoints(ECireFont Font, float Size) const;
    void BuildFonts();
    /** Filled circle / ring / triangle in panel space. */
    void Disc(float X, float Y, float R, FLinearColor Color, int32 Sides=28);
    void Circle(float X, float Y, float R, FLinearColor Color, float Width=1.f, int32 Sides=36);
    void Tri(FVector2D A, FVector2D B, FVector2D C, FLinearColor Color);
    void PlayWowSound(int32 Index, float Volume=1.f);
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
    void DrawPlayer(ACireHero* Hero);
    void DrawParty(ACireHero* Hero, ACireController* Controller);
    void DrawUnit(AActor* Actor, const FString& Caption, bool bFocus);
    // ---- WoW-style frames and feedback (CireHUDWow.cpp) ----
    void DrawPortrait(AActor* Actor, float CX, float CY, float R, bool bSmall);
    void DrawBossFrames(ACireHero* Hero, ACireController* Controller);
    void DrawThreatMeter(ACireHero* Hero, ACireController* Controller);
    void UpdateThreatAlerts(ACireHero* Hero);
    void DrawAlert();
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
    void DrawChat(ACireController* Controller);
    void DrawMeters(ACireHero* Hero, ACireController* Controller);
    void DrawCombatText(ACireHero* Hero, ACireController* Controller);
    void DrawNameplates(ACireHero* Hero);
    void DrawStatuses(AActor* Actor,float X,float Y,float Size,int32 MaxIcons=4);
    void DrawPet(ACireHero* Hero,ACireController* Controller);
    void Tip(const FString& Title,const FString& Body,float X,float Y,float W,float H);
    void DrawTooltip();
    void PlayUIFeedback();
    void DrawDiagnostics();
    void DrawDeveloperPanel(float X,float Y);
    void DrawDeveloperLauncher();
    FCireUIRect DeveloperLauncherRect() const;
    FCireUIRect PanelRect(FName Id) const;
    FCireMovementTuning MovementDraft;
    bool bMovementLoaded=false;
    bool DrawReplayScreen();
    FCireDeveloperSettings DeveloperDraft;
    bool bDeveloperLoaded=false,bLabPlayerMode=false,bLabArena=false,bReplayListLoaded=false;
    int32 DeveloperPage=0,LabWave=1,LabEnemyKind=-1,LabTeamSize=5,LabEnemyCount=5,ReplayOffset=0;
    float LabSeconds=60;
    FString DeveloperMessage;
    FString TooltipTitle,TooltipBody;
    // WoW interface state.
    UPROPERTY(Transient) TArray<TObjectPtr<UFont>> WowFonts;
    UPROPERTY() TArray<TObjectPtr<UFontFace>> WowFontFaces;
    UPROPERTY() TArray<TObjectPtr<USoundBase>> WowSounds;
    ECireFont NextFont = ECireFont::Auto;
    TArray<float> FontCalibration;
    TWeakObjectPtr<AActor> HoverUnit, TooltipUnit, LastTargetSeen;
    TMap<TWeakObjectPtr<ACireMonster>, TWeakObjectPtr<ACireHero>> AggroMemory;
    TMap<TWeakObjectPtr<ACireHero>, int32> SeenLevels;
    TArray<FCireLevelBurst> LevelBursts;
    FCireHUDAlert Alert;
    double LastThreatWarning = -100.0;
    FString TooltipHoverKey;
    double TooltipHoverStart = 0.0;
    float PendingUIScale = -1.f;
    TWeakObjectPtr<AActor> DebugHoverUnit;
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
