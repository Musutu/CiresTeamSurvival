#include "CireHUD.h"
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

namespace
{
const FLinearColor Ink(.014f,.020f,.026f,.98f),Card(.034f,.046f,.055f,.98f),Hover(.075f,.106f,.116f,1);
const FLinearColor Gold(.77f,.61f,.34f,1),Parchment(.91f,.9f,.83f,1),Muted(.50f,.57f,.59f,1),Teal(.20f,.71f,.59f,1),Red(.75f,.20f,.23f,1);
}
void ACireHUD::PlayUIFeedback()
{
    if(UISettings.bMuteAudio || UISettings.MasterVolume*UISettings.UIVolume<=0) return;
    if(auto* Sound=LoadObject<USoundWave>(nullptr,TEXT("/Game/Audio/CireCombat/S_Critical.S_Critical")))
        UGameplayStatics::PlaySound2D(this,Sound,UISettings.MasterVolume*UISettings.UIVolume*.12f,1.5f);
}
void ACireHUD::Tip(const FString& Title,const FString& Body,float X,float Y,float W,float H)
{
    if(UISettings.bTooltips && Hit(X,Y,W,H)) { TooltipTitle=Title; TooltipBody=Body; }
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
    LastTooltipRect={};LastTooltipBodyLines=0;LastTooltipBodyFontSize=0;
    if(bDebugTooltip){TooltipTitle=DebugTooltipTitle;TooltipBody=DebugTooltipBody;Cursor=DebugTooltipCursor;bDebug=true;}
    if(DebugHoverUnit.IsValid()&&TooltipTitle.IsEmpty()){TooltipUnit=DebugHoverUnit;Cursor=DebugHoverCursor;bDebug=true;}
    if(!DebugAbilityId.IsEmpty()&&TooltipTitle.IsEmpty()){TooltipAbility=DebugAbilityId;Cursor=DebugHoverCursor;bDebug=true;}
#endif
    if(!UISettings.bTooltips || bEditLayout) return;
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
    const float Size=FMath::Clamp(UISettings.TooltipScale,.6f,1.4f);
    const float Padding=12*Size,TitleFont=15*Size,BodyFont=11*Size,Gap=6*Size;
    float W=340*Size;
    if(UISettings.TooltipMode==1||UISettings.TooltipMode==3)
    {
        const auto R=UISettings.GetRect(TEXT("Tooltip"),FVector2D(ViewW,ViewH));W=R.W*Size;
    }
    W=FMath::Clamp(W,100.f,FMath::Max(100.f,ViewW-8));
    const auto WrapLines=[&](const FString& Text,float Font,ECireFont Face)
    {
        TArray<FString> Lines,Paragraphs;Text.ParseIntoArrayLines(Paragraphs,false);
        for(const FString& Paragraph:Paragraphs)
        {
            TArray<FString> Words;Paragraph.ParseIntoArrayWS(Words);FString Row;
            for(const FString& Word:Words)
            {
                if(TextWidthFont(Word,Font,Face)>W-2*Padding)
                {
                    if(!Row.IsEmpty()){Lines.Add(Row);Row.Reset();}
                    for(int32 I=0;I<Word.Len();++I)
                    {
                        const FString Next=Row+Word.Mid(I,1);
                        if(!Row.IsEmpty()&&TextWidthFont(Next,Font,Face)>W-2*Padding){Lines.Add(Row);Row=Word.Mid(I,1);}else Row=Next;
                    }
                    continue;
                }
                const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
                if(!Row.IsEmpty()&&TextWidthFont(Next,Font,Face)>W-2*Padding){Lines.Add(Row);Row=Word;}else Row=Next;
            }
            if(!Row.IsEmpty())Lines.Add(Row);
        }
        return Lines;
    };
    NextFont=ECireFont::Bold;
    const TArray<FString> TitleLines=WrapLines(TooltipTitle,TitleFont,ECireFont::Bold);
    TArray<FString> BodyLines=WrapLines(TooltipBody,BodyFont,ECireFont::Body);
    const float TitleHeight=TitleLines.Num()*(TitleFont+4*Size),BodyStep=BodyFont+4*Size;
    const int32 MaxBodyLines=FMath::Max(1,FMath::FloorToInt((ViewH-8-2*Padding-TitleHeight-Gap)/BodyStep));
    if(BodyLines.Num()>MaxBodyLines){BodyLines.SetNum(MaxBodyLines);BodyLines.Last()=BodyLines.Last().LeftChop(3)+TEXT("...");}
    const float H=2*Padding+TitleHeight+(BodyLines.IsEmpty()?0:Gap+BodyLines.Num()*BodyStep);
    const FCireUIRect Box=PlaceTooltip(W,H,Cursor);
    const float X=Box.X,Y=Box.Y;
    TooltipBox(X,Y,W,H,FLinearColor(.55f,.58f,.64f,1));
    for(int32 I=0;I<TitleLines.Num();++I){NextFont=ECireFont::Bold;Label(TitleLines[I],X+Padding,Y+Padding+I*(TitleFont+4*Size),TitleFont,FLinearColor(1.f,.86f,.3f,1));}
    for(int32 I=0;I<BodyLines.Num();++I){NextFont=ECireFont::Body;Label(BodyLines[I],X+Padding,Y+Padding+TitleHeight+Gap+I*BodyStep,BodyFont,FLinearColor(.86f,.87f,.84f,1));}
    NextFont=ECireFont::Auto;
#if !UE_BUILD_SHIPPING
    LastTooltipRect={X,Y,W,H};LastTooltipBodyLines=BodyLines.Num();LastTooltipBodyFontSize=BodyFont;
#endif
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
    auto Button=[&](const FString& Caption,float BX,float BY,float BW,const FString& Help=FString()) {
        const bool Over=Hit(BX,BY,BW,27);
        CireUIStyle::Button(Painter(),BX,BY,BW,27,Caption,Over?(PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton)?ECireButtonState::Pressed:ECireButtonState::Hover):ECireButtonState::Normal,Gold,10.5f);
        Tip(Caption,Help.IsEmpty()?Caption:Help,BX,BY,BW,27);
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
        Label(FString::Printf(TEXT("%.2f"),Value),BX+235,BY,10,Gold);
        const float Knob=FMath::Clamp((Value-Min)/(Max-Min),0.f,1.f);
        CireUIStyle::Frame(Painter(),BX,BY+22,286,8,Gold,ECireFrame::Inset);
        if(const auto& Kit=CireUIStyle::Assets();Kit.Gloss)Painter().Tex(Kit.Gloss,BX+1,BY+23,284*Knob,6,Enabled?FLinearColor(1.1f,.85f,.4f,1):Muted);
        if(const auto& Kit=CireUIStyle::Assets();Kit.Gem)Painter().Tex(Kit.Gem,BX+284*Knob-8,BY+18,16,16,Enabled?FLinearColor(1.f,.85f,.45f,1):Muted);
        else Panel(BX+282*Knob,BY+18,5,16,Enabled?Parchment:Muted);
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
        if(OptionsTab==I)Panel(X+10,Y+84+I*45,154,34,Hover);
        if(Button(Tabs[I],X+18,Y+88+I*45,138))OptionsTab=I;
    }
    Line(X+168,Y+77,X+168,Y+531,Gold*.35f);
    if(OptionsTab==0)
    {
        const TCHAR* ControlPages[]={TEXT("Camera"),TEXT("Keybindings")};
        for(int32 I=0;I<2;++I){if(ControlsPage==I)Panel(L+I*155,Top-40,146,27,Hover);if(Button(ControlPages[I],L+I*155,Top-40,146))ControlsPage=I;}
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
        }
        else DrawKeybindingsPage(L,Top-40);
    }
    else if(OptionsTab==1)
    {
        const TCHAR* Pages[]={TEXT("Combat text"),TEXT("Tooltips"),TEXT("Status / chat"),TEXT("Scale / threat")};
        for(int32 I=0;I<4;++I){if(InterfacePage==I)Panel(L+I*155,Top,146,27,Hover);if(Button(Pages[I],L+I*155,Top,146))InterfacePage=I;}
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
            Wrapped(TEXT("Buffs use gold/blue edges; debuffs use red/purple edges. Hover an icon for its effect, remaining time and removal rule."),R,B+200,286,12,Muted,5);
        }
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
        Toggle(TEXT("Motion blur"),UISettings.bMotionBlur,R,Top+184,TEXT("Controls local camera motion blur. Off preserves clarity during fast turns."));
        Slider(TEXT("Other units' aura effects"),UISettings.OtherEffectsIntensity,0,1,.05f,R,Top+222,TEXT("Strength of buff auras, rage swirls and empowered-attack trails on units other than you. 0 keeps only overhead marks. Your own effects stay full.")); // aura-vfx
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
        else Wrapped(TEXT("Display changes are saved only after confirmation. Closing Options or waiting 15 seconds restores the prior resolution, window mode, quality, VSync and frame cap."),L,Top+296,593,12,Muted,4);
    }
    else if(OptionsTab==3)
    {
        Label(TEXT("COMBAT / INTERFACE SOUND"),L,Top,12,Gold);
        Toggle(TEXT("Mute all game cues"),UISettings.bMuteAudio,L,Top+39,TEXT("Mutes combat presentation and interface feedback without changing combat text."));
        Slider(TEXT("Master volume"),UISettings.MasterVolume,0,1,.02f,L,Top+93,TEXT("Overall level for all currently implemented sound cues."));
        Slider(TEXT("Combat effects"),UISettings.SFXVolume,0,1,.02f,R,Top+93,TEXT("Weapon swings, shots, spell casts and confirmed-hit audio. Combat sounds are spatially attenuated."));
        Slider(TEXT("Interface feedback"),UISettings.UIVolume,0,1,.02f,L,Top+157,TEXT("Options clicks and interface feedback. This level multiplies the master volume."));
        if(Button(TEXT("TEST INTERFACE CUE"),R,Top+177,286))PlayUIFeedback();
        Wrapped(TEXT("Misses and dodges display text without a successful-impact sound. The launch or swing is still audible. Combat sound uses a shared 16-voice limit so large fights remain controlled."),L,Top+248,595,12,Muted,5);
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
