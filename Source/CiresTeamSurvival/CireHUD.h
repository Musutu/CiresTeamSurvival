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

UCLASS()
class CIRESTEAMSURVIVAL_API ACireHUD : public AHUD
{
    GENERATED_BODY()
public:
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
#endif
private:
    void Panel(float X, float Y, float W, float H, FLinearColor Color);
    void Label(const FString& Text, float X, float Y, float Size, FLinearColor Color=FLinearColor::White);
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
    FCireMovementTuning MovementDraft;
    bool bMovementLoaded=false;
    bool DrawReplayScreen();
    FCireDeveloperSettings DeveloperDraft;
    bool bDeveloperLoaded=false,bLabPlayerMode=false,bLabArena=false,bReplayListLoaded=false;
    int32 DeveloperPage=0,LabWave=1,LabEnemyKind=-1,LabTeamSize=5,LabEnemyCount=5,ReplayOffset=0;
    float LabSeconds=60;
    FString DeveloperMessage;
    FString TooltipTitle,TooltipBody;
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
