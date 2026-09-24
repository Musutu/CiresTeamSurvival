#include "CireHUD.h"
#include "CireGame.h"
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
    if(!FParse::Param(FCommandLine::Get(),TEXT("CireTooltipGallery")))return;
    bDebugTooltip=true;DebugTooltipTitle=Title;DebugTooltipBody=Body;DebugTooltipCursor=Cursor;
}
#endif
void ACireHUD::DrawTooltip()
{
    FVector2D Cursor(MX,MY);
#if !UE_BUILD_SHIPPING
    LastTooltipRect={};LastTooltipBodyLines=0;LastTooltipBodyFontSize=0;
    if(bDebugTooltip){TooltipTitle=DebugTooltipTitle;TooltipBody=DebugTooltipBody;Cursor=DebugTooltipCursor;}
#endif
    if(!UISettings.bTooltips || TooltipTitle.IsEmpty() || bEditLayout) return;
    ResetTransform();
    const float Size=FMath::Clamp(UISettings.TooltipScale,.6f,1.4f);
    const float Padding=12*Size,TitleFont=15*Size,BodyFont=11*Size,Gap=6*Size;
    float X=Cursor.X+20,Y=Cursor.Y+24,W=340*Size;
    if(UISettings.TooltipMode==1)
    {
        const auto R=UISettings.GetRect(TEXT("Tooltip"),FVector2D(ViewW,ViewH)); X=R.X;Y=R.Y;W=R.W*Size;
    }
    W=FMath::Clamp(W,100.f,FMath::Max(100.f,ViewW-8));
    const auto WrapLines=[&](const FString& Text,float Font)
    {
        TArray<FString> Lines,Paragraphs;Text.ParseIntoArrayLines(Paragraphs,false);
        for(const FString& Paragraph:Paragraphs)
        {
            TArray<FString> Words;Paragraph.ParseIntoArrayWS(Words);FString Row;
            for(const FString& Word:Words)
            {
                if(TextWidth(Word,Font)>W-2*Padding)
                {
                    if(!Row.IsEmpty()){Lines.Add(Row);Row.Reset();}
                    for(int32 I=0;I<Word.Len();++I)
                    {
                        const FString Next=Row+Word.Mid(I,1);
                        if(!Row.IsEmpty()&&TextWidth(Next,Font)>W-2*Padding){Lines.Add(Row);Row=Word.Mid(I,1);}else Row=Next;
                    }
                    continue;
                }
                const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
                if(!Row.IsEmpty()&&TextWidth(Next,Font)>W-2*Padding){Lines.Add(Row);Row=Word;}else Row=Next;
            }
            if(!Row.IsEmpty())Lines.Add(Row);
        }
        return Lines;
    };
    const TArray<FString> TitleLines=WrapLines(TooltipTitle,TitleFont);
    TArray<FString> BodyLines=WrapLines(TooltipBody,BodyFont);
    const float TitleHeight=TitleLines.Num()*(TitleFont+4*Size),BodyStep=BodyFont+4*Size;
    const int32 MaxBodyLines=FMath::Max(1,FMath::FloorToInt((ViewH-8-2*Padding-TitleHeight-Gap)/BodyStep));
    if(BodyLines.Num()>MaxBodyLines){BodyLines.SetNum(MaxBodyLines);BodyLines.Last()=BodyLines.Last().LeftChop(3)+TEXT("...");}
    const float H=2*Padding+TitleHeight+(BodyLines.IsEmpty()?0:Gap+BodyLines.Num()*BodyStep);
    if(UISettings.TooltipMode==2)
    {
        const float Angle=FMath::DegreesToRadians(UISettings.TooltipAngleDegrees);
        X=Cursor.X+FMath::Cos(Angle)*UISettings.TooltipDistance;
        Y=Cursor.Y+FMath::Sin(Angle)*UISettings.TooltipDistance;
        if(FMath::Cos(Angle)<0) X-=W;
        if(FMath::Sin(Angle)<0) Y-=H;
    }
    X=FMath::Clamp(X,4.f,FMath::Max(4.f,ViewW-W-4));Y=FMath::Clamp(Y,4.f,FMath::Max(4.f,ViewH-H-4));
    Frame(X,Y,W,H,Gold);
    for(int32 I=0;I<TitleLines.Num();++I)Label(TitleLines[I],X+Padding,Y+Padding+I*(TitleFont+4*Size),TitleFont,Parchment);
    for(int32 I=0;I<BodyLines.Num();++I)Label(BodyLines[I],X+Padding,Y+Padding+TitleHeight+Gap+I*BodyStep,BodyFont,Muted);
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
    Label(TEXT("Local preferences save automatically. Video changes need confirmation."),L,Y+53,11,Muted);
    auto Button=[&](const FString& Caption,float BX,float BY,float BW,const FString& Help=FString()) {
        const bool Over=Hit(BX,BY,BW,27);Panel(BX,BY,BW,27,Over?Hover:Card);Label(Caption,BX+9,BY+6,11,Over?Parchment:Gold);
        Tip(Caption,Help.IsEmpty()?Caption:Help,BX,BY,BW,27);
        if(Over&&Clicked){Clicked=false;PlayUIFeedback();return true;}return false;
    };
    auto Toggle=[&](const FString& Caption,bool& Value,float BX,float BY,const FString& Help) {
        Panel(BX,BY,17,17,Value?Teal:Card);if(Value)Label(TEXT("+"),BX+4,BY-2,15,Ink);
        Label(Caption,BX+26,BY,11,Parchment);Tip(Caption,Help,BX,BY,290,22);
        if(Clicked&&Hit(BX,BY,290,22)){Value=!Value;UISettings.Save();Clicked=false;PlayUIFeedback();}
    };
    auto Slider=[&](const FString& Caption,float& Value,float Min,float Max,float Step,float BX,float BY,const FString& Help,bool Enabled=true,bool SaveChange=true) {
        Label(Caption,BX,BY,11,Enabled?Parchment:Muted);
        Label(FString::Printf(TEXT("%.2f"),Value),BX+235,BY,10,Gold);
        Panel(BX,BY+24,286,4,Card);Panel(BX,BY+24,286*FMath::Clamp((Value-Min)/(Max-Min),0.f,1.f),4,Enabled?Gold:Muted);
        Panel(BX+282*FMath::Clamp((Value-Min)/(Max-Min),0.f,1.f),BY+18,5,16,Enabled?Parchment:Muted);
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
        Label(TEXT("CAMERA / HOLD RIGHT MOUSE TO LOOK"),L,Top,12,Gold);
        Slider(TEXT("Horizontal sensitivity"),UISettings.CameraYawSensitivity,.1f,3,.05f,L,Top+34,TEXT("Multiplies horizontal mouse camera rotation while holding the right mouse button. Movement input is unaffected."));
        Slider(TEXT("Vertical sensitivity"),UISettings.CameraPitchSensitivity,.1f,3,.05f,R,Top+34,TEXT("Multiplies vertical mouse camera rotation independently from horizontal sensitivity."));
        Toggle(TEXT("Invert vertical camera"),UISettings.bInvertMouseY,L,Top+88,TEXT("Reverse pitch direction while holding the right mouse button."));
        Slider(TEXT("Camera distance (cm)"),UISettings.CameraDistance,300,1200,25,L,Top+134,TEXT("Preferred third-person camera boom length. World collision can pull the camera closer near walls."));
        Slider(TEXT("Field of view"),UISettings.CameraFOV,55,105,1,R,Top+134,TEXT("Horizontal camera field of view in degrees. A wider view shows more surroundings."));
        Label(TEXT("BATTLEFIELD KEYS"),L,Top+203,12,Gold);
        const TCHAR* Keys[]={TEXT("W A S D   Move / strafe       RMB   Camera / face direction"),TEXT("E   Jump       Ctrl   Dodge roll       Caps Lock   Walk / run"),TEXT("Left click   Select       F1   Self       F   Ally       Tab   Enemy"),TEXT("Space   Auto attack       1-6   Skills       Q   Ultimate"),TEXT("B   Shop       R   Recall       Enter   Chat       H   Help"),TEXT("F8   Developer tools       F9   Options       F10   Edit layout"),TEXT("Ground skills: press key, aim, click to cast; RMB / Esc cancels")};
        for(int32 I=0;I<7;++I)Label(Keys[I],L,Top+232+I*23,11,I%2?Muted:Parchment);
        Toggle(TEXT("Quick cast ground skills at cursor"),UISettings.bQuickGroundCast,L,Top+408,TEXT("Off: preview the real footprint, then left click to confirm. On: cast immediately at the cursor. Server range, line-of-sight and resource checks always apply."));
    }
    else if(OptionsTab==1)
    {
        const TCHAR* Pages[]={TEXT("Combat text"),TEXT("Tooltips / status"),TEXT("Chat / layout")};
        for(int32 I=0;I<3;++I)if(Button(Pages[I],L+I*207,Top,196))InterfacePage=I;
        const float B=Top+48;
        if(InterfacePage==0)
        {
            Toggle(TEXT("Enable combat text"),UISettings.bCombatTextEnabled,L,B,TEXT("Master switch for both world damage numbers and personal scrolling combat text."));
            Toggle(TEXT("Floating target numbers"),UISettings.bShowFloatingNumbers,L,B+35,TEXT("Displays confirmed damage/healing above each affected target, including killing blows."));
            Toggle(TEXT("Personal scrolling text"),UISettings.bShowScrollingText,L,B+70,TEXT("Separate incoming and outgoing lanes; simultaneous area hits combine by skill and critical status."));
            Toggle(TEXT("Show critical strike symbol"),UISettings.bShowCriticalSymbol,L,B+105,TEXT("Shows a gold starburst and CRIT marker over critically hit targets. Critical amounts also use a larger orange font."));
            Toggle(TEXT("Show damage"),UISettings.bShowDamage,R,B,TEXT("Display damage and avoided attacks. MISS and DODGE produce no hit-impact sound."));
            Toggle(TEXT("Show healing"),UISettings.bShowHealing,R,B+35,TEXT("Shows effective healing in green. Excess healing does not inflate the amount."));
            Toggle(TEXT("Show incoming"),UISettings.bShowIncoming,R,B+70,TEXT("Include events affecting your character in personal combat feedback."));
            Toggle(TEXT("Show outgoing"),UISettings.bShowOutgoing,R,B+105,TEXT("Include events caused by your character in personal combat feedback."));
            Slider(TEXT("Floating number font"),UISettings.WorldNumberFontSize,12,48,1,L,B+168,TEXT("Base size for damage over targets. Critical damage is enlarged on top of this value."));
            Slider(TEXT("Scrolling number font"),UISettings.CombatTextFontSize,12,42,1,R,B+168,TEXT("Base size for the two personal scrolling combat lanes."));
            Toggle(TEXT("Show combat event log"),UISettings.bShowCombatLog,L,B+242,TEXT("Optional readable chronological combat events. Player chat stays in its own panel."));
            Toggle(TEXT("Show damage / healing meter"),UISettings.bShowMeter,R,B+242,TEXT("Displays effective team damage or healing. Click its tab to change which total is ranked."));
        }
        else if(InterfacePage==1)
        {
            Toggle(TEXT("Detailed tooltips"),UISettings.bTooltips,L,B,TEXT("Shows explanations when hovering unit frames, status icons, skills, controls, shops and information panels."));
            const TCHAR* Modes[]={TEXT("At cursor"),TEXT("Fixed saved position"),TEXT("Radial cursor offset")};
            if(Button(FString(TEXT("Tooltip: "))+Modes[UISettings.TooltipMode],L,B+37,286,TEXT("Cycle tooltip position. Fixed position uses the Tooltip panel in F10 layout editing. Radial uses a locked angle and distance around the cursor.")))
            { UISettings.TooltipMode=(UISettings.TooltipMode+1)%3;UISettings.Save(); }
            Toggle(TEXT("Lock radial offset"),UISettings.bTooltipOffsetLocked,L,B+80,TEXT("Lock the angle and distance. Unlock to change the sliders below; the tooltip still follows the mouse at that fixed offset."));
            Slider(TEXT("Offset angle (degrees)"),UISettings.TooltipAngleDegrees,0,360,5,L,B+120,TEXT("0 points right, 90 down, 180 left and 270 up from the cursor. Viewport edges always clamp the tooltip."),!UISettings.bTooltipOffsetLocked);
            Slider(TEXT("Offset distance"),UISettings.TooltipDistance,16,240,4,L,B+175,TEXT("Logical pixels between cursor and tooltip anchor."),!UISettings.bTooltipOffsetLocked);
            const TCHAR* Filters[]={TEXT("All statuses"),TEXT("Buffs only"),TEXT("Debuffs only")};
            if(Button(FString(TEXT("Status: "))+Filters[UISettings.StatusFilter],R,B+37,286,TEXT("Filter status icons on your player, party, target and focus frames.")))
            {UISettings.StatusFilter=(UISettings.StatusFilter+1)%3;UISettings.Save();}
            Toggle(TEXT("Dispellable only"),UISettings.bDispellableOnly,R,B+80,TEXT("Shows only statuses marked as dispellable. Ground poison is not dispellable: leave its area to remove it."));
            Toggle(TEXT("Status remaining time"),UISettings.bShowStatusDurations,R,B+120,TEXT("Durations use seconds below one minute and minutes above it. Ground effects show their remaining area lifetime."));
            Wrapped(TEXT("Buffs use gold/blue edges; debuffs use red/purple edges. Hover an icon for its effect, remaining time and removal rule."),R,B+179,280,12,Muted,5);
            float TooltipPercent=UISettings.TooltipScale*100;
            Slider(TEXT("Tooltip size (%)"),TooltipPercent,60,140,5,L,B+224,TEXT("Scales tooltip text and frame from 60% to 140%. Applies to cursor, fixed and radial positions. Height fits the description; your size preference is saved."),true,false);
            if(!FMath::IsNearlyEqual(UISettings.TooltipScale,TooltipPercent/100)){UISettings.TooltipScale=TooltipPercent/100;UISettings.Save();}
            if(Button(TEXT("MOVE FIXED TOOLTIP / F10"),L,B+282,286)){UISettings.TooltipMode=1;UISettings.Save();ToggleLayoutEditor();}
        }
        else
        {
            Toggle(TEXT("Show player chat"),UISettings.bShowChat,L,B,TEXT("Party chat goes to your teammates. Everyone chat reaches both teams. Combat events never enter player chat."));
            Slider(TEXT("Chat font size"),UISettings.ChatFontSize,9,24,1,L,B+55,TEXT("Adjust the chat font. Resize and move the chat frame in F10 edit mode."));
            if(Button(TEXT("CYCLE CHAT COLOR"),L,B+117,286))
            {UISettings.ChatColor=UISettings.ChatColor.R>.75f&&UISettings.ChatColor.B>.7f?FLinearColor(.45f,.88f,.74f,1):UISettings.ChatColor.G>.8f?FLinearColor(.91f,.76f,.43f,1):FLinearColor(.83f,.87f,.88f,1);UISettings.Save();}
            if(Button(TEXT("EDIT / LOCK INTERFACE  [F10]"),R,B,286,TEXT("Drag a panel, resize its lower-right corner, or use U/L to lock just that panel. F10/Escape saves and locks the interface.")))ToggleLayoutEditor();
            Wrapped(TEXT("Player, party, selected target, focus, minimap, skills, chat, meters, combat text, tooltip and pet commands are individually movable and lockable. Saved positions scale with your viewport."),R,B+53,286,12,Muted,7);
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
