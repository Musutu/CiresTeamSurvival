#include "CireHUD.h"
#include "CireUITheme.h" // ui-themes
#include "CireGame.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundWave.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "CireAudio.h" // audio: UI cues, Options > Audio buses and music credits

namespace
{
// ui-themes: themed colours are references to CireUIColors so they follow the active UI theme.
const FLinearColor &Ink=CireUIColors::Ink, &Card=CireUIColors::Card, &Hover=CireUIColors::Hover;
// ui-themes: themed colours are references to CireUIColors so they follow the active UI theme.
const FLinearColor &Gold=CireUIColors::Gold, &Parchment=CireUIColors::Parchment, &Muted=CireUIColors::Muted;
const FLinearColor Teal(.20f,.71f,.59f,1), Red(.75f,.20f,.23f,1);
}
void ACireHUD::PlayUIFeedback()
{
    if(UISettings.bMuteAudio || UISettings.MasterVolume*UISettings.UIVolume<=0) return;
    if(CireAudio::PlayCue2D(this,TEXT("ui_click"))) return; // audio: recorded CC0 click; legacy tone below is the fallback
    if(auto* Sound=LoadObject<USoundWave>(nullptr,TEXT("/Game/Audio/CireCombat/S_Critical.S_Critical")))
        UGameplayStatics::PlaySound2D(this,Sound,UISettings.MasterVolume*UISettings.UIVolume*.12f,1.5f);
}
void ACireHUD::Tip(const FString& Title,const FString& Body,float X,float Y,float W,float H)
{
    if(bSettings && Hit(X,Y,W,H)) CireAudio::NoteHover(this,X,Y); // audio: hover tick on Options controls
    if(UISettings.bTooltips && Hit(X,Y,W,H)) { TooltipTitle=Title; TooltipBody=Body; bRichTip=false; TooltipRegion={float(Origin.X+X*Stretch.X),float(Origin.Y+Y*Stretch.Y),float(W*Stretch.X),float(H*Stretch.Y)}; }
}
void ACireHUD::RichTip(const FCireTooltipSpec& Spec,float X,float Y,float W,float H)
{
    if(bSettings && Hit(X,Y,W,H)) CireAudio::NoteHover(this,X,Y);
    if(UISettings.bTooltips && Hit(X,Y,W,H)) { SetRichTooltip(Spec); TooltipRegion={float(Origin.X+X*Stretch.X),float(Origin.Y+Y*Stretch.Y),float(W*Stretch.X),float(H*Stretch.Y)}; }
}
// readability: every tooltip (controls, items, buffs, abilities, units) is a rich WoW-style card, sized by
// the player's tooltip scale on a larger base (the default 80% now reads like the old 125%).
void ACireHUD::DrawRichTooltip(const FCireTooltipSpec& Spec,FVector2D Cursor,float Width,bool bUseRegion)
{
    const float S=FMath::Clamp(UISettings.TooltipScale,.6f,1.4f)*1.25f;
    const float W=FMath::Clamp(Width,160.f,FMath::Max(160.f,ViewW-8));
    int32 Lines=0;
    // WoW anchor: the card grows up from the anchor corner, so it may use the height above that corner.
    const FCireUIRect Anchor=UISettings.GetRect(TEXT("Tooltip"),FVector2D(ViewW,ViewH));
    const float MaxH=UISettings.TooltipMode==3?FMath::Max(200.f,Anchor.Y+Anchor.H-4):ViewH-8;
    const float H=CireUIStyle::RichTooltip(Painter(),0,0,W,Spec,S,UISettings.TooltipOpacity,false,MaxH,&Lines);
    FCireUIRect Box=PlaceTooltip(W,H,Cursor);
    // Large hover targets (cards, big buttons): below (else above) the hovered region so the tooltip never
    // covers the card itself or its neighbours.
    if(bUseRegion&&TooltipRegion.W*TooltipRegion.H>=150.f*150.f)
    {
        const float TX=FMath::Clamp(TooltipRegion.X,4.f,FMath::Max(4.f,ViewW-W-4));
        if(TooltipRegion.Y+TooltipRegion.H+6+H<=ViewH-4)Box={TX,TooltipRegion.Y+TooltipRegion.H+6,W,H};
        else if(TooltipRegion.Y-6-H>=4)Box={TX,TooltipRegion.Y-6-H,W,H};
        else {TooltipHoverKey.Reset();return;} // the card already shows its full text
    }
    ResetTransform();
    CireUIStyle::RichTooltip(Painter(),Box.X,Box.Y,W,Spec,S,UISettings.TooltipOpacity,true,MaxH);
#if !UE_BUILD_SHIPPING
    LastTooltipRect=Box;LastTooltipBodyLines=Lines;LastTooltipBodyFontSize=11.f*S;LastTooltipTitle=Spec.Title;
#endif
}
#if !UE_BUILD_SHIPPING
void ACireHUD::DebugTooltip(const FString& Title,const FString& Body,FVector2D Cursor)
{
    if(!FParse::Param(FCommandLine::Get(),TEXT("CireTooltipGallery"))&&!FParse::Param(FCommandLine::Get(),TEXT("CireWowUIGallery")))return;
    bDebugTooltip=true;DebugTooltipTitle=Title;DebugTooltipBody=Body;DebugTooltipCursor=Cursor;
}
#endif
void ACireHUD::DrawTooltip()
{
    FVector2D Cursor(MX,MY);
    bool bDebug=false;
#if !UE_BUILD_SHIPPING
    LastTooltipRect={};LastTooltipBodyLines=0;LastTooltipBodyFontSize=0;LastTooltipTitle.Reset();
    if(bDebugTooltip){TooltipTitle=DebugTooltipTitle;TooltipBody=DebugTooltipBody;Cursor=DebugTooltipCursor;bDebug=true;}
    if(DebugHoverUnit.IsValid()&&TooltipTitle.IsEmpty()){TooltipUnit=DebugHoverUnit;Cursor=DebugHoverCursor;bDebug=true;}
    if(!DebugAbilityId.IsEmpty()&&TooltipTitle.IsEmpty()){TooltipAbility=DebugAbilityId;Cursor=DebugHoverCursor;bDebug=true;}
#endif
    if(bEditLayout)
    {
        // F10: the only tooltip is the description of the panel under the pointer.
        if(EditHelpTitle.IsEmpty()||!UISettings.bTooltips)return;
        TooltipTitle=EditHelpTitle;TooltipBody=EditHelpBody;TooltipUnit.Reset();TooltipAbility.Reset();TooltipRegion={0,0,0,0};
    }
    else if(!UISettings.bTooltips) return;
    // Specific hovers (skills, statuses, controls) win over a unit; then world hover.
    AActor* Unit=nullptr;
    const bool bAbility=TooltipTitle.IsEmpty()&&!TooltipAbility.IsEmpty()&&!bBarDragging;
    if(TooltipTitle.IsEmpty()&&!bAbility&&UISettings.bUnitTooltips)Unit=TooltipUnit.IsValid()?TooltipUnit.Get():HoverUnit.Get();
    if(TooltipTitle.IsEmpty()&&!Unit&&!bAbility){TooltipHoverKey.Reset();return;}
    // Hover delay: the tooltip appears once the pointer rests on the same element.
    const FString Key=bAbility?TooltipAbility:Unit?Unit->GetName():TooltipTitle;
    const double Now=GetWorld()->GetRealTimeSeconds();
    if(Key!=TooltipHoverKey){TooltipHoverKey=Key;TooltipHoverStart=Now;}
    if(!bDebug&&Now-TooltipHoverStart<UISettings.TooltipDelay)return;
    ResetTransform();
    if(bAbility){DrawAbilityTooltip(TooltipAbility,Cursor);return;}
    if(Unit){DrawUnitTooltip(Unit,Cursor);return;}
    // readability: plain title/body tooltips become rich cards too (stat lines, section headers, dividers).
    const float Size=FMath::Clamp(UISettings.TooltipScale,.6f,1.4f)*1.25f;
    float W=300*Size;
    if(UISettings.TooltipMode==1||UISettings.TooltipMode==3)
    {
        const auto R=UISettings.GetRect(TEXT("Tooltip"),FVector2D(ViewW,ViewH));W=FMath::Max(R.W,280.f)*Size;
    }
    const bool bRich=bRichTip&&RichTipSpec.Title==TooltipTitle;
    DrawRichTooltip(bRich?RichTipSpec:CireUIStyle::TooltipFromText(TooltipTitle,TooltipBody),Cursor,W,!bDebug);
}
void ACireHUD::RevertVideoPreview()
{
    if(!bVideoPending) return;
    if(auto* Settings=GEngine?GEngine->GetGameUserSettings():nullptr)
    {
        Settings->SetScreenResolution(PreviousResolution);Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(PreviousMode));
        Settings->ScalabilityQuality=PreviousQuality;Settings->SetVSyncEnabled(bPreviousVSync);Settings->SetFrameRateLimit(PreviousFPS);
        Settings->ApplyResolutionSettings(false);Settings->ApplyNonResolutionSettings();
    }
    bVideoPending=false;bVideoLoaded=false;
}
void ACireHUD::DrawDiagnostics()
{
    if(bVideoPending && FPlatformTime::Seconds()>=VideoDeadline) RevertVideoPreview();
    ResetTransform();float Y=ViewH-17;
    if(UISettings.bShowFPS)
    {
        const float DT=GetWorld()->GetDeltaSeconds();
        Label(FString::Printf(TEXT("%.0f FPS / %.1f ms"),DT>0?1/DT:0,DT*1000),ViewW-160,Y,10,Gold);Y-=14;
    }
    if(UISettings.bShowNetwork && PlayerOwner && PlayerOwner->PlayerState)
        Label(FString::Printf(TEXT("Latency %.0f ms"),PlayerOwner->PlayerState->GetPingInMilliseconds()),ViewW-160,Y,10,Muted);
}
void ACireHUD::DrawSettings()
{
    if(!bSettings)return;
    ResetTransform();TooltipTitle.Reset();TooltipBody.Reset();
    Panel(0,0,ViewW,ViewH,FLinearColor(0,0,0,.62f));
    const float X=(ViewW-840)/2,Y=(ViewH-590)/2,L=X+188,R=X+506,Top=Y+90;
    Frame(X,Y,840,590,Gold);Label(TEXT("OPTIONS"),X+23,Y+17,24,Parchment);
    Label(TEXT("CIRE'S TEAM SURVIVAL"),X+188,Y+23,12,Gold);
    if(OptionsTab!=0)Label(TEXT("Local preferences save automatically. Video changes need confirmation."),L,Y+53,11,Muted);
    // readability: 30-unit buttons with a readable face; the current tab / page uses the Selected state.
    auto Button=[&](const FString& Caption,float BX,float BY,float BW,const FString& Help=FString(),bool bSelected=false,float BH=30.f) {
        const bool Over=Hit(BX,BY,BW,BH);
        CireUIStyle::Button(Painter(),BX,BY,BW,BH,Caption,Over?(PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton)?ECireButtonState::Pressed:ECireButtonState::Hover):bSelected?ECireButtonState::Selected:ECireButtonState::Normal,Gold,11.f);
        Tip(Caption,Help.IsEmpty()?Caption:Help,BX,BY,BW,BH);
        if(Over&&Clicked){Clicked=false;PlayUIFeedback();return true;}return false;
    };
    auto Toggle=[&](const FString& Caption,bool& Value,float BX,float BY,const FString& Help) {
        // Style-kit checkbox: recessed well, gold check with a soft glow when on.
        const bool OverToggle=Hit(BX,BY,290,22);
        CireUIStyle::Frame(Painter(),BX,BY,17,17,Gold,ECireFrame::Inset);
        if(OverToggle)CireUIStyle::Glow(Painter(),BX,BY,17,17,FLinearColor(1.f,.85f,.5f,.25f));
        if(Value){CireUIStyle::Glow(Painter(),BX,BY,17,17,FLinearColor(1.f,.8f,.3f,.35f));Line(BX+3.5f,BY+9,BX+7,BY+13,FLinearColor(1.f,.84f,.35f,1),2.4f);Line(BX+7,BY+13,BX+14,BY+3.5f,FLinearColor(1.f,.84f,.35f,1),2.4f);}
        Label(Caption,BX+26,BY,11,OverToggle?FLinearColor(1.f,.95f,.82f,1):Parchment);Tip(Caption,Help,BX,BY,290,22);
        if(Clicked&&Hit(BX,BY,290,22)){Value=!Value;UISettings.Save();Clicked=false;PlayUIFeedback();}
    };
    auto Slider=[&](const FString& Caption,float& Value,float Min,float Max,float Step,float BX,float BY,const FString& Help,bool Enabled=true,bool SaveChange=true) {
        Label(Caption,BX,BY,11,Enabled?Parchment:Muted);
        {const FString V=FString::Printf(TEXT("%.2f"),Value);TextFx(V,BX+286-TextWidthFont(V,11,ECireFont::Numbers),BY,11,Gold,ECireFont::Numbers,true,true);}
        const float Knob=FMath::Clamp((Value-Min)/(Max-Min),0.f,1.f);
        CireUIStyle::Slider(Painter(),BX,BY+22,286,Knob,Enabled,Hit(BX,BY+12,290,27));
        Tip(Caption,Help,BX,BY,290,37);
        if(Enabled&&Hit(BX,BY+12,290,27)&&PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton))
        {
            const float New=FMath::Clamp(FMath::RoundToFloat((Min+(Max-Min)*FMath::Clamp((MX-BX)/286,0.f,1.f))/Step)*Step,Min,Max);
            if(New!=Value){Value=New;if(SaveChange)UISettings.Save();}Clicked=false;
        }
    };
    const TCHAR* Tabs[]={TEXT("Controls"),TEXT("Interface"),TEXT("Video"),TEXT("Audio"),TEXT("System"),TEXT("Developer")};
    const bool DeveloperAvailable=CireDeveloperTools::CanEdit(GetWorld());
    if(!DeveloperAvailable&&OptionsTab==5)OptionsTab=4;
    for(int32 I=0;I<(DeveloperAvailable?6:5);++I)
    {
        if(Button(Tabs[I],X+14,Y+84+I*46,146,FString(),OptionsTab==I,36.f))OptionsTab=I;
    }
    Line(X+168,Y+77,X+168,Y+531,Gold*.35f);
    if(OptionsTab==0)
    {
        const TCHAR* ControlPages[]={TEXT("Camera"),TEXT("Keybindings")};
        for(int32 I=0;I<2;++I){if(Button(ControlPages[I],L+I*155,Top-42,146,FString(),ControlsPage==I))ControlsPage=I;}
        if(ControlsPage==0)
        {
            Label(TEXT("CAMERA / LEFT DRAG ORBIT, RIGHT DRAG STEER"),L,Top+4,12,Gold);
            Slider(TEXT("Horizontal sensitivity"),UISettings.CameraYawSensitivity,.05f,5,.05f,L,Top+34,TEXT("Multiplies horizontal mouse camera rotation while dragging with either mouse button. Movement input is unaffected."));
            Slider(TEXT("Vertical sensitivity"),UISettings.CameraPitchSensitivity,.05f,5,.05f,R,Top+34,TEXT("Multiplies vertical mouse camera rotation independently from horizontal sensitivity."));
            Toggle(TEXT("Invert vertical camera"),UISettings.bInvertMouseY,L,Top+88,TEXT("Reverse pitch direction while dragging the camera. Off: moving the mouse up looks up."));
            // feat/camera-movement: WoW camera follow and target reacquire preferences.
            Toggle(TEXT("Camera follows movement"),UISettings.bCameraAutoFollow,R,Top+88,TEXT("While moving with no mouse button held, the camera swings back behind your character."));
            Toggle(TEXT("Auto-target next enemy"),UISettings.bAutoReacquireTarget,R,Top+111,TEXT("When your hostile target dies, select the nearest hostile in front of the camera."));
            Slider(TEXT("Camera distance (cm)"),UISettings.CameraDistance,300,1200,25,L,Top+134,TEXT("Preferred third-person camera boom length. World collision can pull the camera closer near walls. The mouse wheel zooms within this range."));
            Slider(TEXT("Field of view"),UISettings.CameraFOV,55,105,1,R,Top+134,TEXT("Horizontal camera field of view in degrees. A wider view shows more surroundings."));
            Toggle(TEXT("Quick cast ground skills at cursor"),UISettings.bQuickGroundCast,L,Top+210,TEXT("Off: preview the real footprint, then left click to confirm. On: cast immediately at the cursor. Server range, line-of-sight and resource checks always apply."));
            // feat/camera-movement: casting while moving / smart targeting preferences.
            Label(TEXT("CASTING"),L,Top+248,12,Gold);
            Toggle(TEXT("Smart cast (auto-target enemy)"),UISettings.bSmartCast,L,Top+278,TEXT("An enemy spell with no valid target selects the enemy under the cursor, else the nearest enemy in front of the camera. Summons that need an enemy do the same."));
            Toggle(TEXT("Mouseover casting"),UISettings.bMouseoverCast,R,Top+278,TEXT("Spells go to the valid unit under the cursor without changing your target (enemy spells on enemies, ally spells on allies)."));
            Toggle(TEXT("Right click cancels ground aim"),UISettings.bRightClickCancelsAim,L,Top+310,TEXT("Only a clean right click (no camera drag) cancels an armed reticle. Right-drag steering and movement never cancel. Escape always cancels."));
            Toggle(TEXT("Press ability again to cast at reticle"),UISettings.bPressAgainToCast,R,Top+310,TEXT("While a ground ability is armed, pressing its key again casts it at the reticle."));
            Toggle(TEXT("Stop moving to cast"),UISettings.bAutoStopToCast,L,Top+342,TEXT("Cast-time spells need you to stand still (WoW). On: pressing one while moving stops your movement keys and casts; press a movement key again to move (cancels the cast). Off: shows Can't cast while moving."));
        }
        else DrawKeybindingsPage(L,Top-40);
    }
    else if(OptionsTab==1)
    {
        // ui-themes: a fifth page for the UI theme (5 page buttons now share the row).
        const TCHAR* Pages[]={TEXT("Combat text"),TEXT("Tooltips"),TEXT("Status / chat"),TEXT("Scale / threat"),TEXT("UI theme")};
        for(int32 I=0;I<5;++I){if(Button(Pages[I],L+I*126,Top-2,122,FString(),InterfacePage==I))InterfacePage=I;}
        const float B=Top+48;
        if(InterfacePage==0)
        {
            Toggle(TEXT("Enable combat text"),UISettings.bCombatTextEnabled,L,B,TEXT("Master switch for both world damage numbers and personal scrolling combat text."));
            Toggle(TEXT("Floating target numbers"),UISettings.bShowFloatingNumbers,L,B+32,TEXT("Displays confirmed damage/healing above each affected target, including killing blows."));
            Toggle(TEXT("Personal scrolling text"),UISettings.bShowScrollingText,L,B+64,TEXT("Separate incoming and outgoing lanes in the movable CombatText panel."));
            Toggle(TEXT("Critical strike starburst"),UISettings.bShowCriticalSymbol,L,B+96,TEXT("Shows a starburst and CRIT marker behind critical hits."));
            Toggle(TEXT("Critical pop (enlarge)"),UISettings.bCritPop,L,B+128,TEXT("Critical numbers appear large and settle to a bigger-than-normal size, like WoW's crit pop."));
            Toggle(TEXT("Merge area hits"),UISettings.bMergeAoE,L,B+160,TEXT("Simultaneous hits of one ability combine into one scrolling entry (for example 4 targets). Each target still shows its own floating number."));
            Toggle(TEXT("Damage"),UISettings.bShowDamage,R,B,TEXT("Display damage numbers."));
            Toggle(TEXT("Healing"),UISettings.bShowHealing,R,B+32,TEXT("Shows effective healing in green. Excess healing does not inflate the amount."));
            Toggle(TEXT("Incoming (damage/healing taken)"),UISettings.bShowIncoming,R,B+64,TEXT("Include events affecting your character."));
            Toggle(TEXT("Outgoing (damage/healing done)"),UISettings.bShowOutgoing,R,B+96,TEXT("Include events caused by your character."));
            Toggle(TEXT("Misses and dodges"),UISettings.bShowMisses,R,B+128,TEXT("Show Miss / Dodge text for avoided attacks."));
            Toggle(TEXT("Colour by spell school"),UISettings.bSchoolColors,R,B+160,TEXT("Outgoing damage uses its school colour: gold physical, orange fire, pale blue frost, green poison/nature, purple shadow, yellow holy, blue storm."));
            Slider(TEXT("Floating number font"),UISettings.WorldNumberFontSize,12,48,1,L,B+200,TEXT("Base size for damage over targets. Critical damage is enlarged on top of this value."));
            Slider(TEXT("Scrolling number font"),UISettings.CombatTextFontSize,12,42,1,R,B+200,TEXT("Base size for the two personal scrolling combat lanes."));
            Slider(TEXT("Scroll speed"),UISettings.SCTSpeed,.5f,2.f,.05f,L,B+255,TEXT("How fast numbers drift in their scroll direction."));
            Slider(TEXT("Display time (seconds)"),UISettings.SCTFadeSeconds,1.5f,5.f,.1f,R,B+255,TEXT("How long scrolling entries stay before fading. Floating numbers use two thirds of this."));
            const TCHAR* Directions[]={TEXT("Up"),TEXT("Down"),TEXT("Fountain (arc outward)")};
            if(Button(FString(TEXT("Scroll direction: "))+Directions[UISettings.SCTDirection],L,B+310,286,TEXT("Up: new entries push older ones up. Down: new entries enter at the top. Fountain: entries arc out to the sides.")))
            {UISettings.SCTDirection=(UISettings.SCTDirection+1)%3;UISettings.Save();}
            Toggle(TEXT("Show combat event log"),UISettings.bShowCombatLog,R,B+310,TEXT("Optional readable chronological combat events. Player chat stays in its own panel."));
            Toggle(TEXT("Show damage / healing meter"),UISettings.bShowMeter,R,B+342,TEXT("Displays effective team damage or healing. Click its tab to change which total is ranked."));
        }
        else if(InterfacePage==1)
        {
            Toggle(TEXT("Detailed tooltips"),UISettings.bTooltips,L,B,TEXT("Shows explanations when hovering unit frames, status icons, skills, controls, shops and information panels."));
            const TCHAR* Modes[]={TEXT("At cursor"),TEXT("Fixed (top-left of anchor)"),TEXT("Radial cursor offset"),TEXT("WoW corner anchor")};
            if(Button(FString(TEXT("Position: "))+Modes[UISettings.TooltipMode],L,B+37,286,TEXT("Cycle tooltip position. WoW corner anchor grows up and left from the Tooltip panel's lower-right corner (default, bottom right like WoW). Fixed uses the panel's top-left. Move the Tooltip panel with F10. Radial follows the cursor at a locked angle and distance.")))
            { UISettings.TooltipMode=(UISettings.TooltipMode+1)%4;UISettings.Save(); }
            Toggle(TEXT("Lock radial offset"),UISettings.bTooltipOffsetLocked,L,B+80,TEXT("Lock the angle and distance. Unlock to change the sliders below; the tooltip still follows the mouse at that fixed offset."));
            Slider(TEXT("Offset angle (degrees)"),UISettings.TooltipAngleDegrees,0,360,5,L,B+120,TEXT("0 points right, 90 down, 180 left and 270 up from the cursor. Viewport edges always clamp the tooltip."),!UISettings.bTooltipOffsetLocked);
            Slider(TEXT("Offset distance"),UISettings.TooltipDistance,16,240,4,L,B+175,TEXT("Logical pixels between cursor and tooltip anchor."),!UISettings.bTooltipOffsetLocked);
            float TooltipPercent=UISettings.TooltipScale*100;
            Slider(TEXT("Tooltip size (%)"),TooltipPercent,60,140,5,L,B+230,TEXT("Scales tooltip text and frame from 60% to 140%. Applies to every position mode. Height fits the description; your size preference is saved."),true,false);
            if(!FMath::IsNearlyEqual(UISettings.TooltipScale,TooltipPercent/100)){UISettings.TooltipScale=TooltipPercent/100;UISettings.Save();}
            if(Button(TEXT("MOVE TOOLTIP ANCHOR / F10"),L,B+290,286,TEXT("Opens layout editing. Drag the Tooltip panel; WoW anchor mode uses its lower-right corner, fixed mode its top-left.")))
            {if(UISettings.TooltipMode==0||UISettings.TooltipMode==2)UISettings.TooltipMode=3;UISettings.Save();ToggleLayoutEditor();}
            Toggle(TEXT("Unit tooltips (hover characters)"),UISettings.bUnitTooltips,R,B,TEXT("WoW-style unit tooltips when hovering a character, frame or nameplate: reaction-coloured name, classification, role, health, target, status and abilities."));
            Toggle(TEXT("Keep clear of reticle / aim"),UISettings.bTooltipAvoidCenter,R,B+37,TEXT("Tooltips never cover the centre of the screen, nor the ground area you are aiming a spell at; they slide aside instead."));
            float Opacity=UISettings.TooltipOpacity*100;
            Slider(TEXT("Background opacity (%)"),Opacity,30,100,5,R,B+80,TEXT("Tooltip background opacity. Text stays fully readable."),true,false);
            if(!FMath::IsNearlyEqual(UISettings.TooltipOpacity,Opacity/100)){UISettings.TooltipOpacity=Opacity/100;UISettings.Save();}
            Slider(TEXT("Show delay (seconds)"),UISettings.TooltipDelay,0,1.5f,.05f,R,B+140,TEXT("How long the pointer must rest on something before its tooltip appears. 0 shows immediately."));
            Wrapped(TEXT("Default: WoW-style corner anchor at the lower right, above your action bar. Tooltips never overlap the reticle or an armed ground-aim area."),R,B+200,286,12,Muted,5);
        }
        else if(InterfacePage==2)
        {
            Toggle(TEXT("Show player chat"),UISettings.bShowChat,L,B,TEXT("Party chat goes to your teammates. Everyone chat reaches both teams. Combat events never enter player chat."));
            Slider(TEXT("Chat font size"),UISettings.ChatFontSize,9,24,1,L,B+45,TEXT("Adjust the chat font. Resize and move the chat frame in F10 edit mode."));
            if(Button(TEXT("CYCLE CHAT COLOR"),L,B+100,286))
            {UISettings.ChatColor=UISettings.ChatColor.R>.75f&&UISettings.ChatColor.B>.7f?FLinearColor(.45f,.88f,.74f,1):UISettings.ChatColor.G>.8f?FLinearColor(.91f,.76f,.43f,1):FLinearColor(.83f,.87f,.88f,1);UISettings.Save();}
            const TCHAR* Filters[]={TEXT("All statuses"),TEXT("Buffs only"),TEXT("Debuffs only")};
            if(Button(FString(TEXT("Status: "))+Filters[UISettings.StatusFilter],L,B+160,286,TEXT("Filter status icons on your player, party, target and focus frames.")))
            {UISettings.StatusFilter=(UISettings.StatusFilter+1)%3;UISettings.Save();}
            Toggle(TEXT("Dispellable only"),UISettings.bDispellableOnly,L,B+203,TEXT("Shows only statuses marked as dispellable. Ground poison is not dispellable: leave its area to remove it."));
            Toggle(TEXT("Status remaining time"),UISettings.bShowStatusDurations,L,B+240,TEXT("Durations use seconds below one minute and minutes above it. Ground effects show their remaining area lifetime."));
            if(Button(TEXT("EDIT / LOCK INTERFACE  [F10]"),R,B,286,TEXT("Drag a panel, resize its lower-right corner, or use U/L to lock just that panel. F10/Escape saves and locks the interface.")))ToggleLayoutEditor();
            Wrapped(TEXT("Player, party, target, focus, boss frames, threat meter, minimap, skills, chat, meters, combat text, tooltip anchor and pet commands are individually movable and lockable. Panels keep their size and stay anchored to the nearest screen edge."),R,B+45,286,12,Muted,7);
            Wrapped(TEXT("Buffs have gold edges; debuffs are coloured by type: blue magic, green poison, purple curse, red physical. Hover for DEF +40% style summaries."),R,B+180,286,11,Muted,4);
            Toggle(TEXT("Buff / debuff callouts"),UISettings.bEffectCallouts,R,B+238,TEXT("A short banner with the icon and effect when you gain a notable buff or debuff (throttled so combat is not spammy)."));
            Toggle(TEXT("Crowd-control alerts"),UISettings.bControlAlerts,R,B+268,TEXT("STUNNED / SILENCED / HEALING CUT in the centre and a coloured screen edge while you are controlled."));
            Toggle(TEXT("Player cast bar"),UISettings.bPlayerCastBar,R,B+298,TEXT("Your cast bar above the action bars (gold interruptible, grey uninterruptible, green heals)."));
            const TCHAR* Overhead[]={TEXT("All units"),TEXT("Enemies only"),TEXT("Off")};
            if(Button(FString(TEXT("Overhead status: "))+Overhead[FMath::Clamp(UISettings.OverheadStatusMode,0,2)],L,B+285,286,TEXT("Status chips above heads: STUN / SILENCE / ROOT with a duration ring, ATK / DEF / SPD arrows for stat changes. Far units show crowd control only.")))
            {UISettings.OverheadStatusMode=(UISettings.OverheadStatusMode+1)%3;UISettings.Save();}
        }
        else if(InterfacePage==4)DrawThemePicker(L,B);
        else
        {
            Toggle(TEXT("Automatic interface scale"),UISettings.bAutoUIScale,L,B,TEXT("Chooses the scale from your resolution: 100% up to 1080p, slightly smaller on 1440p/4K displays."));
            float ScaleDraft=PendingUIScale>0?PendingUIScale:UISettings.ResolveUIScale(Canvas?Canvas->ClipY:1080.f);
            const float Before=ScaleDraft;
            Slider(TEXT("Interface scale"),ScaleDraft,.64f,1.15f,.01f,L,B+40,TEXT("Scales every HUD element together (0.64 - 1.15, like WoW). Applied when you release the slider; dragging it switches off automatic scale."),true,false);
            if(!FMath::IsNearlyEqual(Before,ScaleDraft))PendingUIScale=ScaleDraft;
            Label(FString::Printf(TEXT("Current: %.0f%%%s"),UISettings.ResolveUIScale(Canvas?Canvas->ClipY:1080.f)*100,UISettings.bAutoUIScale?TEXT(" (automatic)"):TEXT("")),L,B+84,10,Muted);
            Toggle(TEXT("Level-up burst and banner"),UISettings.bLevelUpEffect,L,B+120,TEXT("Golden pillar of light, rising motes, a LEVEL banner and chime when a hero levels up."));
            Toggle(TEXT("Boss / pack leader frames"),UISettings.bShowBossFrames,L,B+155,TEXT("Large health frames for bosses and elite pack leaders in your lane, with cast bars and your threat."));
            if(Button(TEXT("MOVE BARS / THREAT / BOSS [F10]"),L,B+200,286))ToggleLayoutEditor();
            Toggle(TEXT("Action bar 2 (Shift+1-6)"),UISettings.bShowActionBar2,L,B+245,TEXT("A second 12-slot bar above the main bar. Drag abilities onto it; keys are set in Controls > Keybindings."));
            Toggle(TEXT("Action bar 3 (Alt+1-6)"),UISettings.bShowActionBar3,L,B+280,TEXT("A third 12-slot bar above bar 2."));
            Toggle(TEXT("Lock action bars"),UISettings.bLockActionBars,L,B+315,TEXT("Prevents accidental drags. Hold Shift to move an ability while locked."));
            Toggle(TEXT("Threat meter"),UISettings.bShowThreatMeter,R,B,TEXT("Lists who is on your target's threat table with % bars. The aggro holder is 100%."));
            Toggle(TEXT("Aggro warnings"),UISettings.bThreatWarnings,R,B+35,TEXT("Damage dealers and healers: warns when you pull an enemy or approach its tank's threat. Tanks: warns when an enemy leaves you for an ally."));
            Toggle(TEXT("Warning sounds"),UISettings.bThreatSound,R,B+70,TEXT("Plays an alarm when you gain (or, as a tank, lose) aggro and a soft ping on the threat warning."));
            Slider(TEXT("Warn at threat (%)"),UISettings.ThreatWarningPercent,60,100,1,R,B+110,TEXT("Non-tanks get a warning when their threat on an enemy reaches this share of the current aggro holder's."));
            Wrapped(TEXT("Nameplates glow red when an enemy is on you (damage/healer) and orange when you are close to pulling it. As a tank, orange means an engaged enemy is attacking someone else."),R,B+170,286,12,Muted,6);
        }
    }
    else if(OptionsTab==2)
    {
        auto* Settings=GEngine?GEngine->GetGameUserSettings():nullptr;
        if(Settings&&!bVideoLoaded)
        {VideoResolution=Settings->GetScreenResolution();VideoMode=Settings->GetFullscreenMode();VideoQuality=FMath::Max(0,Settings->GetOverallScalabilityLevel());bVideoVSync=Settings->IsVSyncEnabled();VideoFPS=Settings->GetFrameRateLimit();bVideoLoaded=true;}
        Label(TEXT("DISPLAY / GRAPHICS"),L,Top,12,Gold);
        if(Button(FString::Printf(TEXT("Resolution: %d x %d"),VideoResolution.X,VideoResolution.Y),L,Top+36,286,TEXT("Cycles standard display resolutions. Apply starts a 15-second confirmation window; unconfirmed changes are restored.")))
        {const FIntPoint Sizes[]={FIntPoint(1280,720),FIntPoint(1600,900),FIntPoint(1920,1080),FIntPoint(2560,1440)};int32 I=0;for(int32 J=0;J<4;++J)if(Sizes[J]==VideoResolution)I=(J+1)%4;VideoResolution=Sizes[I];}
        const TCHAR* Modes[]={TEXT("Fullscreen"),TEXT("Borderless"),TEXT("Windowed")};
        if(Button(FString(TEXT("Window mode: "))+Modes[FMath::Clamp(VideoMode,0,2)],R,Top+36,286))VideoMode=(VideoMode+1)%3;
        const TCHAR* Qualities[]={TEXT("Low"),TEXT("Medium"),TEXT("High"),TEXT("Epic"),TEXT("Cinematic")};
        if(Button(FString(TEXT("Quality: "))+Qualities[FMath::Clamp(VideoQuality,0,4)],L,Top+83,286,TEXT("Applies Unreal's full scalability preset, including shadows, effects, textures and post-processing.")))VideoQuality=(VideoQuality+1)%5;
        if(Button(FString(TEXT("VSync: "))+(bVideoVSync?TEXT("On"):TEXT("Off")),R,Top+83,286))bVideoVSync=!bVideoVSync;
        Slider(TEXT("Frame rate cap (0 = uncapped)"),VideoFPS,0,240,15,L,Top+136,TEXT("Limits rendered frames per second. VSync may impose a lower display refresh limit."));
        Toggle(TEXT("Spell / scene bloom"),UISettings.bBloom,R,Top+143,TEXT("Controls the local camera bloom intensity. It does not remove enemy telegraphs."));
        Toggle(TEXT("Impact camera shake"),UISettings.bImpactCameraShake,L,Top+190,TEXT("A small camera kick when a heavy spell or critical hit lands on or next to your champion. Never moves the camera for distant fights.")); // ability-vfx
        Toggle(TEXT("Motion blur"),UISettings.bMotionBlur,R,Top+184,TEXT("Controls local camera motion blur. Off preserves clarity during fast turns."));
        Slider(TEXT("Ground telegraph intensity"),UISettings.GroundTelegraphIntensity,.1f,1,.05f,R,Top+268,TEXT("Brightness of ground telegraphs, aim previews, lingering zones and Fab ground effects (fill, rim and runes). Default 0.3 keeps the ground visible through them; enemy warnings keep a readable rim at the lowest setting.")); // ability-vfx; telegraphs: 0.1..1, default 0.3
        Slider(TEXT("Ally / other units' effects"),UISettings.OtherEffectsIntensity,0,1,.05f,R,Top+222,TEXT("Strength of buff auras, rage swirls and empowered-attack trails on units other than you. 0 keeps only overhead marks. Your own effects stay full.")); // aura-vfx
        if(Settings && !bVideoPending && Button(TEXT("APPLY VIDEO PREVIEW"),L,Top+239,286))
        {
            PreviousResolution=Settings->GetScreenResolution();PreviousMode=Settings->GetFullscreenMode();PreviousQuality=Settings->ScalabilityQuality;
            bPreviousVSync=Settings->IsVSyncEnabled();PreviousFPS=Settings->GetFrameRateLimit();
            Settings->SetScreenResolution(VideoResolution);Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(VideoMode));
            Settings->SetOverallScalabilityLevel(VideoQuality);Settings->SetVSyncEnabled(bVideoVSync);Settings->SetFrameRateLimit(VideoFPS);
            Settings->ApplyResolutionSettings(false);Settings->ApplyNonResolutionSettings();bVideoPending=true;VideoDeadline=FPlatformTime::Seconds()+15;
        }
        if(bVideoPending)
        {
            Label(FString::Printf(TEXT("Keep these settings? Reverting in %ds"),FMath::Max(0,FMath::CeilToInt(VideoDeadline-FPlatformTime::Seconds()))),L,Top+299,14,Gold);
            if(Button(TEXT("KEEP CHANGES"),L,Top+337,286)){Settings->ConfirmVideoMode();Settings->SaveSettings();bVideoPending=false;}
            if(Button(TEXT("REVERT NOW"),R,Top+337,286))RevertVideoPreview();
        }
        else Wrapped(TEXT("Display changes are saved only after confirmation. Closing Options or waiting 15 seconds restores the prior resolution, window mode, quality, VSync and frame cap."),L,Top+318,593,12,Muted,4);
    }
    else if(OptionsTab==3)
    {
        // audio: five buses, music switch, heavy-step shake and the CC-BY music credits (Docs/Audio.md).
        Label(TEXT("SOUND"),L,Top,12,Gold);
        Toggle(TEXT("Mute all game sound"),UISettings.bMuteAudio,L,Top+30,TEXT("Silences music, ambience, combat, footsteps and interface sound. Combat text is unaffected."));
        Toggle(TEXT("Play music"),UISettings.bMusicEnabled,R,Top+30,TEXT("Turns the score on or off. It fades out immediately and resumes with the current state (town, combat, Pack Leader, arena)."));
        Toggle(TEXT("Heavy footstep camera shake"),UISettings.bFootstepCameraShake,L,Top+56,TEXT("A slight camera bump on each footfall when you play a heavy body (Bear, Behemoth, Ether Golem). Off by default."));
        if(const UCireAudioSubsystem* Audio=UCireAudioSubsystem::Get(this))
            Label(FString::Printf(TEXT("Score: %s   %s"),*CireAudio::MusicStateName(this),*Audio->Music.CurrentTitle()),R,Top+58,10,Muted);
        const bool bSound=!UISettings.bMuteAudio;
        Slider(TEXT("Master volume"),UISettings.MasterVolume,0,1,.02f,L,Top+92,TEXT("Overall level; every bus below multiplies it."),bSound);
        Slider(TEXT("Music"),UISettings.MusicVolume,0,1,.02f,R,Top+92,TEXT("Orchestral score. Cross-fades between town, combat, Pack Leader and arena themes."),bSound&&UISettings.bMusicEnabled);
        Slider(TEXT("Combat effects"),UISettings.SFXVolume,0,1,.02f,L,Top+146,TEXT("Weapon swings, spells, hits, footsteps, horns and roars. Spatially attenuated."),bSound);
        Slider(TEXT("Ambience"),UISettings.AmbienceVolume,0,1,.02f,R,Top+146,TEXT("District soundscapes (wind, market crowd, dogs, fountain, church bell, forge) and nearby fires or wells. Ducks under combat."),bSound);
        Slider(TEXT("Interface feedback"),UISettings.UIVolume,0,1,.02f,L,Top+200,TEXT("Clicks, hovers, level-up, aggro and phase banners."),bSound);
        if(Button(TEXT("TEST INTERFACE CUE"),R,Top+208,286))PlayUIFeedback();
        Wrapped(TEXT("Misses and dodges show text without an impact sound. Footsteps follow each body's armour (plate, leather, cloth, hooves, heavy beasts) and the ground under it."),L,Top+252,595,11,Muted,3);
        Label(TEXT("MUSIC CREDITS (CC BY 4.0)"),L,Top+310,11,Gold);
        Wrapped(FString::Join(CireAudio::Credits(),TEXT("  ")),L,Top+330,595,10,Muted,7);
        // audio: end
    }
    else if(OptionsTab==4)
    {
        Label(TEXT("PERFORMANCE / LOCAL PROFILE"),L,Top,12,Gold);
        Toggle(TEXT("Show FPS / frame time"),UISettings.bShowFPS,L,Top+42,TEXT("Shows frames per second and the current frame duration in milliseconds at the bottom right."));
        Toggle(TEXT("Show network latency"),UISettings.bShowNetwork,R,Top+42,TEXT("Shows the local PlayerState ping in milliseconds. A local standalone game normally reports zero."));
        Wrapped(TEXT("Interface and control preferences are local to this PC. Match rules, hit chance, damage and projectile collision are authoritative game data and cannot be changed by these Options."),L,Top+116,594,12,Muted,5);
        Wrapped(TEXT("Profile: ")+UISettings.GetFilename(),L,Top+225,594,10,Muted,5);
        if(Button(TEXT("RESTORE LOCAL DEFAULTS"),L,Top+330,286,TEXT("Restores UI layout, camera, audio and interface defaults. It does not reset a match, remove assets or change confirmed video settings.")))
        {RevertVideoPreview();UISettings.Reset();UISettings.Save();}
    }
    else if(OptionsTab==5&&DeveloperAvailable)DrawDeveloperPanel(L,Top);
    Line(X+20,Y+535,X+820,Y+535,Gold*.3f);
    Label(TEXT("F9 / Escape closes Options"),X+23,Y+553,11,Muted);
    if(Button(TEXT("SAVE & CLOSE"),X+628,Y+547,189)){RevertVideoPreview();UISettings.Save();bSettings=false;}
}

// ---------------------------------------------------------------------------
// ui-themes: Options > Interface > UI theme. Three cards, each a live miniature of the HUD drawn
// with that theme (the theme is activated just for its card, then the profile's theme returns).
// ---------------------------------------------------------------------------
void ACireHUD::DrawThemePicker(float L,float B)
{
    const TArray<FCireUITheme>& Themes=CireUITheme::All();
    const FName Current=CireUITheme::Active()?CireUITheme::Active()->Id:NAME_None;
    const double Now=GetWorld()?GetWorld()->GetRealTimeSeconds():0.0;
    Label(TEXT("UI THEME / THE WHOLE INTERFACE SWITCHES TOGETHER"),L,B-4,11,Gold);
    const int32 N=FMath::Clamp(Themes.Num(),1,4);
    const float Gap=10,CW=(628-Gap*(N-1))/N,CH=336,CY=B+18;
    FName Picked=NAME_None;
    for(int32 I=0;I<N;++I)
    {
        const FCireUITheme& T=Themes[I];
        const float CX=L+I*(CW+Gap);
        const bool bSelected=FName(*UISettings.UITheme)==T.Id,bOver=Hit(CX,CY,CW,CH);
        CireUITheme::SetActive(T.Id); // this card draws in its own theme
        FCireUIPainter P=Painter();
        if(bSelected||bOver)CireUIStyle::Glow(P,CX-2,CY-2,CW+4,CH+4,CireUIColors::ThemeGlow*FLinearColor(1,1,1,bSelected?.45f:.22f));
        CireUIStyle::Frame(P,CX,CY,CW,CH,bSelected?CireUIColors::ThemeAccent*1.3f:CireUIColors::Gold,ECireFrame::Panel);
        P.Text(T.Name.ToUpper(),CX+(CW-P.TextWidth(T.Name.ToUpper(),12,ECireFont::Display))*.5f,CY+12,12,CireUIColors::TitleText,ECireFont::Display,false,true);
        CireUIStyle::Divider(P,CX+14,CY+34,CW-28);
        P.Wrapped(T.Tagline,CX+12,CY+42,CW-24,9,CireUIColors::Muted,4,ECireFont::Body,2.f);
        // Miniature unit frame: portrait ring, name, health and mana bars.
        const float UY=CY+94;
        CireUIStyle::Frame(P,CX+10,UY,CW-20,64,CireUIColors::Gold,ECireFrame::Unit);
        P.Disc(CX+36,UY+32,17,FLinearColor(.035f,.04f,.06f,1));CireUIStyle::Sigil(P,TEXT("role0"),CX+24,UY+20,24,CireUIColors::Gold);
        CireUIStyle::PortraitRing(P,CX+36,UY+32,17);
        P.Text(TEXT("Iron Warden"),CX+62,UY+8,10,CireUIColors::Parchment,ECireFont::Bold,false,true);
        CireUIStyle::Bar(P,CX+62,UY+24,CW-86,12,.78f,CireUIColors::Health,nullptr,Now,TEXT("1320 / 1650"),8);
        CireUIStyle::Bar(P,CX+62,UY+41,CW-86,8,.62f,CireUIColors::Mana,nullptr,Now);
        // Action slots: normal, hover, passive, ultimate (ready glow).
        const float SY=UY+78,S=FMath::Min(34.f,(CW-28-3*8)/4);
        const TCHAR* Ids[]={TEXT("shield_slam"),TEXT("iron_guard"),TEXT("stone_skin"),TEXT("bastion_of_dawn")};
        for(int32 K=0;K<4;++K)
        {
            FCireIconSlot Slot;Slot.IconId=Ids[K];Slot.IconTexture=CireUIStyle::FindAbilityIcon(Slot.IconId);Slot.Tint=CireUIColors::Gold;
            Slot.KeyLabel=K<2?FString::FromInt(K+1):FString();Slot.Kind=K==2?ECireSlotKind::Passive:K==3?ECireSlotKind::Ultimate:ECireSlotKind::Normal;
            Slot.bHover=K==1;Slot.bGlow=K==3;Slot.CooldownFraction=K==0?.4f:0.f;Slot.CooldownRemaining=K==0?3.f:0.f;
            CireUIStyle::IconSlot(P,CX+14+K*(S+8),SY,S,Slot,Now);
        }
        // Cast bar and a tooltip sample.
        CireUIStyle::CastBar(P,CX+20,SY+S+10,CW-40,13,.6f,CireUIColors::Cast,TEXT("Restoring Light"),TEXT("0.7"),8.f);
        CireUIStyle::Tooltip(P,CX+14,SY+S+32,CW-28,TEXT("Shield Slam"),TEXT("Melee strike that slows and draws attention."),.78f,.94f);
        const FString State=bSelected?TEXT("ACTIVE"):TEXT("SELECT");
        CireUIStyle::Button(P,CX+20,CY+CH-32,CW-40,26,State,bSelected?ECireButtonState::Selected:bOver?ECireButtonState::Hover:ECireButtonState::Normal,CireUIColors::ThemeAccent,10.f);
        if(bOver&&Clicked){Clicked=false;Picked=T.Id;}
        Tip(T.Name,T.Tagline+TEXT(" Click to use this theme; it is saved in your profile."),CX,CY,CW,CH);
    }
    CireUITheme::SetActive(Current); // restore the live theme for the rest of the frame
    if(!Picked.IsNone()&&Picked.ToString()!=UISettings.UITheme)
    {
        UISettings.UITheme=Picked.ToString();UISettings.Save();PlayUIFeedback();
        CireUITheme::SetActive(Picked);
    }
}
