#include "CireHUD.h"
#include "CireSkillShop.h" // progression-shop: ready gate caption
#include "GameFramework/GameStateBase.h" // items-v2
#include "CireItems.h" // items-v2
#include "CireUITheme.h" // ui-themes
#include "CireArenas.h" // arenas
#include "CireShopUI.h" // progression-shop
#include "CireLoot.h" // progression-shop: minimap chest markers
#include "CireVendors.h" // vendors: minimap merchant icons
#include "CireKeybindings.h"
#include "CireLanePath.h"
#include "CireGame.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireChampionProfiles.h"
#include "CireTargeting.h"
#include "CireNPCState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "CireVideoSettings.h"
#include "InputCoreTypes.h"

namespace
{
// ui-themes: themed colours are references to CireUIColors so they follow the active UI theme.
const FLinearColor &Ink=CireUIColors::Ink, &Card=CireUIColors::Card, &Hover=CireUIColors::Hover;
// ui-themes: themed colours are references to CireUIColors so they follow the active UI theme.
const FLinearColor &Gold=CireUIColors::Gold, &Parchment=CireUIColors::Parchment, &Muted=CireUIColors::Muted;
const FLinearColor Teal(.20f,.71f,.59f,1.f), Red(.75f,.20f,.23f,1.f), Blue(.23f,.46f,.80f,1.f), Purple(.66f,.46f,.83f,1.f);
const FLinearColor Poison(.61f,.83f,.27f,1.f);
FString ShortName(const FString& Name,int32 Max=22) { return Name.Len()>Max ? Name.Left(Max-2)+TEXT("..") : Name; }
FLinearColor RoleColor(int32 Role) { return Role==0?Gold:Role==1?Teal:Role==3?Purple:Blue; }
float Fraction(float A,float B) { return B>0.f?FMath::Clamp(A/B,0.f,1.f):0.f; }
int32 PoisonCount(const AActor* Actor) {
    if(const auto* Hero=Cast<ACireHero>(Actor))return Hero->bDead?0:Hero->PoisonAreaCount;
    if(const auto* Monster=Cast<ACireMonster>(Actor))return Monster->Health>0?Monster->PoisonAreaCount:0;
    return 0;
}
FString PoisonLabel(int32 Count) {return FString::Printf(TEXT("POISON x%d"),Count);}
const FLinearColor LifeGreen(.10f,.70f,.14f,1.f);
// Number of living enemies currently attacking this hero (WoW raid-frame aggro glow).
int32 AggroCount(const UWorld* World,const ACireHero* Hero)
{
    int32 Count=0;
    if(World&&Hero)for(TActorIterator<ACireMonster> It(const_cast<UWorld*>(World));It;++It)if(It->Health>0&&It->Victim==Hero)++Count;
    return Count;
}
}

void ACireHUD::BeginPlay()
{
    Super::BeginPlay(); UISettings.Load(); UISettings.bLayoutLocked=true; BuildFonts();
    AggroHandle=UCireNPCState::OnAggroChanged().AddUObject(this,&ACireHUD::OnAggroEvent);
}
void ACireHUD::ResetTransform() { Origin=FVector2D::ZeroVector; Stretch=FVector2D(1,1); PanelAlpha=1.f; }
FCireUIRect ACireHUD::PanelRect(FName Id) const
{
    // The saved, anchored rectangle, then a small responsive pass: at large
    // interface scales the side panels give way to the panel they would cover
    // (chat/meter beside the action bar, focus between target and minimap, boss
    // frames above the threat meter) instead of overlapping it.
    const FVector2D View(ViewW,ViewH);
    FCireUIRect R=UISettings.GetRect(Id,View);
    auto Overlap=[](const FCireUIRect& A,const FCireUIRect& B){return A.X<B.X+B.W&&B.X<A.X+A.W&&A.Y<B.Y+B.H&&B.Y<A.Y+A.H;};
    auto GiveWay=[&](FName Other)
    {
        const FCireUIRect O=UISettings.GetRect(Other,View);
        if(!Overlap(R,O))return;
        const float MinW=R.W*.7f;
        if(R.X+R.W*.5f<O.X+O.W*.5f){const float W=O.X-6-R.X;if(W>=MinW)R.W=W;}
        else{const float Right=R.X+R.W,X=O.X+O.W+6;if(Right-X>=MinW){R.X=X;R.W=Right-X;}}
    };
    if(Id==TEXT("Chat")||Id==TEXT("Meter")||Id==TEXT("CombatLog"))
    {
        GiveWay(TEXT("Skills"));
        if(UISettings.bShowActionBar2)GiveWay(TEXT("Bar2"));
        if(UISettings.bShowActionBar3)GiveWay(TEXT("Bar3"));
    }
    if(Id==TEXT("Focus"))
    {
        GiveWay(TEXT("Target"));GiveWay(TEXT("Minimap"));
        // No room beside the target frame: tuck the focus frame under it instead.
        const FCireUIRect T=UISettings.GetRect(TEXT("Target"),View);
        if(Overlap(R,T)){R.X=FMath::Clamp(T.X+T.W-R.W,0.f,ViewW-R.W);R.Y=FMath::Min(T.Y+T.H+6,ViewH-R.H);}
    }
    // Collapsed compact panels only occupy their header (for clicks and layout).
    if(((Id==TEXT("Meter")&&UISettings.bMeterCollapsed)||(Id==TEXT("Threat")&&UISettings.bThreatCollapsed))&&!bEditLayout)
        R.H=FMath::Min(R.H,20.f*R.W/220.f);
    if(Id==TEXT("Boss"))
    {
        const FCireUIRect T=UISettings.GetRect(TEXT("Threat"),View);
        if(Overlap(R,T)&&T.Y>R.Y)R.H=FMath::Max(40.f,T.Y-6-R.Y);
    }
    return R;
}
void ACireHUD::UsePanel(FName Id,float W,float H)
{
    PanelAlpha=1.f;
    FCireUIRect R=PanelRect(Id);
    // New target: the frame fades and slides in (WoW-like), 0.18s.
    if(Id==TEXT("Target")&&!bEditLayout)
    {
        const float T=FMath::Clamp(static_cast<float>(GetWorld()->GetRealTimeSeconds()-TargetChangedAt)/.18f,0.f,1.f);
        const float Ease=1.f-FMath::Square(1.f-T);R.Y-=(1.f-Ease)*12.f;PanelAlpha=.25f+.75f*Ease;
    }
    // Height-trimmed panels (boss frames) keep their scale and simply show fewer rows.
    if(Id==TEXT("Boss")||Id==TEXT("Meter")||Id==TEXT("Threat"))R.H=UISettings.GetRect(Id,FVector2D(ViewW,ViewH)).H;
    Origin=FVector2D(R.X,R.Y); Stretch=FVector2D(R.W/W,R.H/H);
    VisiblePanels.AddUnique(Id);
    static const TMap<FName,FString> Help={
        {TEXT("Player"),TEXT("Your character: green health, blue mana (INT grants 30 each), gold energy for physical abilities. Left click to target yourself. STR grants 10 health and 0.1 armor and spell ward, AGI 1% attack speed per point; your primary attribute adds basic attack damage.")},
        {TEXT("Party"),TEXT("Your four teammates. Left click a frame to target for healing or support. The + button sets a focus target. Hover status icons for duration and removal rules.")},
        {TEXT("Match"),TEXT("Three cleared PvE waves lead to town preparation, arena PvP, then recovery. Each team begins with 100 lives. Normal leaks cost one life; bosses cost ten.")},
        {TEXT("Minimap"),TEXT("Your team's separate PvE lane and town entrance. The enemy realm stays obscured during PvE. Teams can fight only after teleporting to a shared arena.")},
        {TEXT("Skills"),TEXT("Basic attack, six learned active abilities, one passive and one ultimate. Empty slots do nothing. Hover a skill for its effect and cost.")},
        {TEXT("Chat"),TEXT("Player messages only. Party reaches teammates; Everyone reaches both teams. Enter opens chat. Hover this panel and scroll to read earlier messages.")},
        {TEXT("Meter"),TEXT("Effective team damage or healing. Overkill and excess healing do not count. Click the title to collapse or expand; DMG / HEAL switches the ranking.")},
        {TEXT("CombatLog"),TEXT("Chronological confirmed combat events. Damage, healing and avoided attacks are separate from player chat.")},
        {TEXT("Threat"),TEXT("Threat meter: who your target (or the enemy attacking you) wants to hit. The aggro holder is on top at 100%; others show their share of that. Reaching 100% pulls the enemy.")},
        {TEXT("Boss"),TEXT("Boss and pack-leader frames: health, casts and your threat for the biggest enemies in your lane. Click a frame to target it.")},
        {TEXT("CombatText"),TEXT("Your incoming and outgoing combat feedback. F10 moves this panel. Outgoing damage is coloured by school (gold physical, orange fire, blue frost, green poison, purple shadow), incoming damage red, healing green, misses grey. Critical hits pop larger.")}};
    // Panel descriptions only while arranging the interface (F10); never during gameplay.
    if(bEditLayout)if(const FString* Description=Help.Find(Id))if(Hit(0,0,W,H)){EditHelpTitle=Id.ToString();EditHelpBody=*Description;}
}
void ACireHUD::Panel(float X,float Y,float W,float H,FLinearColor Color)
{
    Painter().Rect(X,Y,W,H,Color);
}
void ACireHUD::Wrapped(const FString& Text,float X,float Y,float Width,float Size,FLinearColor Color,int32 MaxLines)
{
    TArray<FString> Words; Text.ParseIntoArrayWS(Words); FString Row; int32 Count=0;
    for(const FString& Word:Words) {
        const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
        if(!Row.IsEmpty()&&TextWidth(Next,Size)>Width) {
            Label(Row,X,Y+Count*(CireUIStyle::ReadableSize(Size)+4),Size,Color); if(++Count>=MaxLines)return; Row=Word;
        } else Row=Next;
    }
    if(!Row.IsEmpty()&&Count<MaxLines)Label(Row,X,Y+Count*(CireUIStyle::ReadableSize(Size)+4),Size,Color);
}
void ACireHUD::Line(float X1,float Y1,float X2,float Y2,FLinearColor Color,float Width)
{
    Painter().Line(X1,Y1,X2,Y2,Color,Width);
}
void ACireHUD::Bar(float X,float Y,float W,float H,float Value,FLinearColor Color)
{
    // Every bar animates (smooth fill + trailing damage chunk). Its identity is its
    // on-screen slot, which is stable while the panel is not being moved.
    const FVector2D At=Painter().ToScreen(X,Y);
    const uint64 Key=(static_cast<uint64>(FMath::RoundToInt(At.X))<<32)^static_cast<uint64>(FMath::RoundToInt(At.Y)*131+FMath::RoundToInt(W));
    CireUIStyle::Bar(Painter(),X,Y,W,H,Value,Color,&BarTrails.FindOrAdd(Key),GetWorld()->GetRealTimeSeconds());
}
void ACireHUD::Frame(float X,float Y,float W,float H,FLinearColor Accent)
{
    CireUIStyle::Frame(Painter(),X,Y,W,H,Accent);
}
bool ACireHUD::Hit(float X,float Y,float W,float H) const
{
    return MX>=Origin.X+X*Stretch.X && MX<=Origin.X+(X+W)*Stretch.X &&
        MY>=Origin.Y+Y*Stretch.Y && MY<=Origin.Y+(Y+H)*Stretch.Y;
}
static FString PaintedRoleIcon(int32 Archetype)
{
    // Painted role emblems (T_role_*): tank, support, everything else reads as damage.
    return Archetype==0?TEXT("role_tank"):Archetype==2?TEXT("role_support"):TEXT("role_damage");
}
void ACireHUD::Icon(const FString& Id,float X,float Y,float S,FLinearColor Color)
{
    // icon-art: role portraits (unit frames) use the painted role emblems when drawn large enough.
    static const TMap<FString,FString> PaintedRoles={{TEXT("role0"),TEXT("role_tank")},{TEXT("role1"),TEXT("role_damage")},
        {TEXT("role2"),TEXT("role_support")},{TEXT("role3"),TEXT("role_damage")},{TEXT("role4"),TEXT("role_damage")}};
    if(const FString* RoleIcon=S>=24?PaintedRoles.Find(Id):nullptr)
        if(UTexture2D* Tex=CireUIStyle::FindAbilityIcon(*RoleIcon)){Painter().Tex(Tex,X,Y,S,S,FLinearColor(1,1,1,Color.A));return;}
    CireUIStyle::Sigil(Painter(),Id,X,Y,S,Color);
}

bool ACireHUD::IsPointerOverInterface() const
{
    if(bModal||bSettings||bEditLayout)return true;
    float CursorX=0,CursorY=0;
    if(!PlayerOwner||!PlayerOwner->GetMousePosition(CursorX,CursorY))return true;
    CursorX/=Scale;CursorY/=Scale;
    if(IsDeveloperLauncherVisible()){const auto R=DeveloperLauncherRect();if(CursorX>=R.X&&CursorX<=R.X+R.W&&CursorY>=R.Y&&CursorY<=R.Y+R.H)return true;}
    if(bLayoutEditor)for(const FCireUIRect& R:LayoutUIRects)if(CursorX>=R.X&&CursorX<=R.X+R.W&&CursorY>=R.Y&&CursorY<=R.Y+R.H)return true; // dev-route-tools
    for(FName Id:VisiblePanels) {
        if(Id==TEXT("CombatText")||Id==TEXT("Tooltip"))continue;
        const auto R=PanelRect(Id);
        if(CursorX>=R.X&&CursorX<=R.X+R.W&&CursorY>=R.Y&&CursorY<=R.Y+R.H)return true;
    }
    return false;
}
void ACireHUD::ToggleLayoutEditor() { RevertVideoPreview();bEditLayout=!bEditLayout; bSettings=false;UISettings.bLayoutLocked=!bEditLayout; if(!bEditLayout){DragPanel=NAME_None;UISettings.Save();} }
void ACireHUD::ToggleSettings() { if(bSettings)RevertVideoPreview();bSettings=!bSettings;bVideoLoaded=false;if(bEditLayout){bEditLayout=false;UISettings.bLayoutLocked=true;UISettings.Save();} }
void ACireHUD::ToggleDeveloperTools()
{
    if(!CireDeveloperTools::CanEdit(GetWorld()))return;
    bShowDevLauncher=true; // F8 reveals the Developer Tools button for the rest of the session
    if(bSettings&&OptionsTab==5){ToggleSettings();return;}
    if(bEditLayout)ToggleLayoutEditor();
    RevertVideoPreview();bSettings=true;OptionsTab=5;DeveloperPage=5;bVideoLoaded=false;
}
bool ACireHUD::HandleEscape() { if(bQuickKeybind){ToggleQuickKeybind();return true;}if(bLayoutEditor&&!bSettings&&LayoutEditorEscape())return true;/* dev-route-tools */if(bRouteEditor&&!bSettings){OpenRouteEditor(false);return true;}/* nav-paths */if(bSettings){RevertVideoPreview();bSettings=false;UISettings.Save();return true;}if(bEditLayout){ToggleLayoutEditor();return true;}return false; }
void ACireHUD::HandleMouseWheel(float Delta)
{
    if(bSettings&&OptionsTab==0&&ControlsPage==1){KeybindScroll=FMath::Max(0,KeybindScroll+(Delta>0?-2:2));return;}
    const auto R=PanelRect(TEXT("Chat"));
    if(MX>=R.X&&MX<=R.X+R.W&&MY>=R.Y&&MY<=R.Y+R.H)ChatScroll=FMath::Clamp(ChatScroll+(Delta>0?2:-2),0,100);
}
void ACireHUD::LayoutInteraction()
{
    if(!bEditLayout)return;
    const bool Down=PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
    if(Clicked&&DragPanel.IsNone()) {
        for(int32 I=VisiblePanels.Num()-1;I>=0;--I) {
            const FName Id=VisiblePanels[I]; const auto R=PanelRect(Id);
            if(MX<R.X||MX>R.X+R.W||MY<R.Y||MY>R.Y+R.H)continue;
            if(MX>R.X+R.W-24&&MY<R.Y+20) { UISettings.SetPanelLocked(Id,!UISettings.IsPanelLocked(Id));UISettings.Save();Clicked=false;break; }
            if(!UISettings.IsPanelLocked(Id)){DragPanel=Id;DragOrigin=FVector2D(MX,MY);DragRect=R;bResizing=MX>R.X+R.W-20&&MY>R.Y+R.H-20;}
            Clicked=false;break;
        }
    }
    if(!DragPanel.IsNone()&&Down) {
        FCireUIRect R=DragRect; const FVector2D Delta=FVector2D(MX,MY)-DragOrigin;
        if(bResizing){R.W+=Delta.X;R.H+=Delta.Y;}else{R.X+=Delta.X;R.Y+=Delta.Y;}
        UISettings.SetRect(DragPanel,R,FVector2D(ViewW,ViewH));
    }
    if(bWasMouseDown&&!Down&&!DragPanel.IsNone()){UISettings.Save();DragPanel=NAME_None;}
    bWasMouseDown=Down;
}
void ACireHUD::DrawLayoutEditor()
{
    if(!bEditLayout)return;
    ResetTransform();
    for(FName Id:VisiblePanels) {
        const auto R=PanelRect(Id);
        const bool Locked=UISettings.IsPanelLocked(Id);
        Panel(R.X,R.Y,R.W,R.H,FLinearColor(.10f,.39f,.40f,.15f));
        Line(R.X,R.Y,R.X+R.W,R.Y,Locked?Muted:Teal,2);
        Line(R.X,R.Y+R.H,R.X+R.W,R.Y+R.H,Locked?Muted:Teal,2);
        Panel(R.X,R.Y, FMath::Min(R.W-24,150.f),20,Ink);Label(Id.ToString(),R.X+6,R.Y+3,10,Parchment);
        Panel(R.X+R.W-24,R.Y,24,20,Locked?Red:Card);Label(Locked?TEXT("L"):TEXT("U"),R.X+R.W-17,R.Y+3,11,Gold);
        Line(R.X+R.W-13,R.Y+R.H-3,R.X+R.W-3,R.Y+R.H-13,Teal,2);
        Line(R.X+R.W-8,R.Y+R.H-3,R.X+R.W-3,R.Y+R.H-8,Teal,2);
    }
    // Instructions sit just above the action bar, clear of the centre where the
    // tooltip anchor and combat text panels live.
    const auto SkillsRect=PanelRect(TEXT("Skills"));
    const float X=ViewW*.5f-245,Y=FMath::Max(4.f,SkillsRect.Y-66);
    Frame(X,Y,490,58,Teal);Label(TEXT("EDIT LAYOUT  /  DRAG PANELS  /  RESIZE AT LOWER RIGHT"),X+13,Y+10,11,Parchment);
    Label(TEXT("U / L locks a panel.   F10 or Escape saves and locks your layout."),X+13,Y+33,10,Muted);
    Clicked=false;
}

void ACireHUD::DrawPlayer(ACireHero* Hero)
{
    UsePanel(TEXT("Player"),260,132); const FLinearColor Accent=RoleColor(Hero->Archetype);
    const int32 Aggro=AggroCount(GetWorld(),Hero);
    if(Aggro>0){const float P=.55f+.3f*FMath::Sin(GetWorld()->GetRealTimeSeconds()*6.f);Panel(-3,-3,266,138,FLinearColor(.9f,.08f,.05f,.35f*P));}
    const bool bThemed=CireUIStyle::HasThemeArt();
    if(bThemed)
    {
        // ui-themes: round portrait in the theme's ring with a level medallion (the concept layout).
        CireUIStyle::Frame(Painter(),0,0,260,132,Aggro>0?FLinearColor(1.f,.25f,.2f,1):Gold,ECireFrame::Unit);
        // Larger ring (concept); the painted role emblem is inscribed in the circle so no square corners show.
        Disc(41,45,34,FLinearColor(0,0,0,.9f));Disc(41,45,32,FLinearColor(.035f,.04f,.06f,1));
        // hud-art: the painted champion portrait fills the ring; the role emblem becomes a small badge.
        const bool bFace=CireUIStyle::PortraitFace(Painter(),Hero->ChampionProfileId,41,45,32,Hero->bDead);
        if(!bFace)Icon(FString::Printf(TEXT("role%d"),Hero->Archetype),41-22.f,45-22.f,44,Accent);
        CireUIStyle::PortraitRing(Painter(),41,45,32);
        if(bFace)CireUIStyle::RoleBadge(Painter(),41+32*.74f,45+32*.7f,9.f,FString::Printf(TEXT("role%d"),Hero->Archetype),Accent,PaintedRoleIcon(Hero->Archetype));
    }
    else{Frame(0,0,260,132,Aggro>0?FLinearColor(1.f,.25f,.2f,1):Accent);Frame(8,9,49,59,Accent);Icon(FString::Printf(TEXT("role%d"),Hero->Archetype),10,13,44,Accent);}
    if(Aggro>0){Panel(186,116,66,14,FLinearColor(.35f,.03f,.02f,.9f));Label(FString::Printf(TEXT("AGGRO x%d"),Aggro),191,116,9,FLinearColor(1.f,.55f,.5f,1));
        Tip(TEXT("Enemies on you"),FString::Printf(TEXT("%d enemies are attacking you. Tanks want this; damage dealers and healers should move to their tank."),Aggro),186,116,66,14);}
    if(bThemed)CireUIStyle::Medallion(Painter(),17,77,12.f,FString::FromInt(Hero->Level),CireUIColors::BrightGold);
    else{Panel(19,63,28,18,Card);Label(FString::FromInt(Hero->Level),26,64,13,Gold);}
    const int32 Poisoned=PoisonCount(Hero);
    // ui-themes: the themed layout gives the larger portrait ring room (bars start further right).
    const float BX=bThemed?88.f:66.f,BW=bThemed?160.f:182.f;
    // readability: bigger outlined name and centred, outlined health / mana values.
    TextFx(Painter().Fit(Hero->HeroName,15,Poisoned>0?BW-86:BW,ECireFont::Bold),BX,6,15,Parchment,ECireFont::Bold,true,true);
    if(Poisoned>0){Panel(165,8,84,17,Card);Label(PoisonLabel(Poisoned),170,10,9,Poison);}
    Bar(BX,31,BW,20,Fraction(Hero->Health,Hero->MaxHealth),LifeGreen);
    {const FString HP=FString::Printf(TEXT("%.0f / %.0f"),Hero->Health,Hero->MaxHealth);TextFx(HP,BX+(BW-TextWidthFont(HP,12.5f,ECireFont::Numbers))*.5f,31+(20-CireUIStyle::ReadableSize(12.5f)*1.28f)*.5f,12.5f,FLinearColor::White,ECireFont::Numbers,true,false);}
    // items-v2: absorb shield (party shields, ultimate upgrades) overlays the health bar.
    const UCireInventory* Inv=Hero->Inventory.Get();
    const float WorldNowS=GetWorld()->GetGameState()?GetWorld()->GetGameState()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    if(Inv&&Inv->BarrierHP>0&&Inv->BarrierEndsAt>WorldNowS){const float SW=BW*Fraction(Inv->BarrierHP,FMath::Max(Hero->MaxHealth,1.f));
        Panel(BX+BW-SW,31,SW,20,FLinearColor(1.f,.93f,.62f,.55f));Label(FString::Printf(TEXT("+%.0f"),Inv->BarrierHP),BX+BW-TextWidth(FString::Printf(TEXT("+%.0f"),Inv->BarrierHP),11)-5,33,11,FLinearColor(1.f,.96f,.8f,1));}
    // items-v2: mana is a real budget now: pulse when low, flash red on "Not enough mana", show regen.
    const float RealNow=GetWorld()->GetRealTimeSeconds();
    static TMap<TWeakObjectPtr<const ACireHero>,TPair<int32,float>> ShortfallSeen;
    TPair<int32,float>& Seen=ShortfallSeen.FindOrAdd(Hero);
    if(Inv&&Inv->ResourceFailSerial!=Seen.Key){if(Seen.Key!=0||Inv->ResourceFailSerial==1)Seen.Value=RealNow;Seen.Key=Inv->ResourceFailSerial;if(Seen.Value==RealNow)PlayWowSound(2,.5f);}
    const float Flash=FMath::Clamp(1.f-(RealNow-Seen.Value)/.9f,0.f,1.f);
    const bool bManaFlash=Flash>0&&Inv&&Inv->ResourceFailKind!=2;
    const float Shake=bManaFlash?FMath::Sin(RealNow*70.f)*3.f*Flash:0.f;
    const float ManaFrac=Fraction(Hero->Mana,Hero->MaxMana);
    if(bManaFlash)Panel(BX-3+Shake,52,BW+6,19,FLinearColor(1.f,.12f,.08f,.75f*Flash));
    else if(ManaFrac<.25f&&Hero->MaxMana>0){const float P=.5f+.5f*FMath::Sin(RealNow*5.f);Panel(BX-2,53,BW+4,17,FLinearColor(.3f,.45f,1.f,.25f+.3f*P));}
    Bar(BX+Shake,54,BW,15,ManaFrac,Blue);
    const float ItemManaRegen=Inv?static_cast<float>(Inv->Totals().Stats.Get(Cires::Items::ItemStat::ManaRegen)):0.f;
    const float ManaRegenNow=CireItems::BaseManaRegen(Hero,Hero->HasSkill(TEXT("deep_reserves"))?1.5f:1.f)+ItemManaRegen;
    const float ManaTY=54+(15-CireUIStyle::ReadableSize(10.5f)*1.28f)*.5f;
    TextFx(FString::Printf(TEXT("%.0f / %.0f"),Hero->Mana,Hero->MaxMana),BX+7+Shake,ManaTY,10.5f,FLinearColor::White,ECireFont::Numbers,true,false);
    const FString RegenText=FString::Printf(TEXT("+%.1f/s"),ManaRegenNow);TextFx(RegenText,BX+BW-TextWidthFont(RegenText,9.5f,ECireFont::Numbers)-6+Shake,ManaTY+.5f,9.5f,FLinearColor(.8f,.9f,1.f,1),ECireFont::Numbers,true,false);
    if(bManaFlash){const FString Warn=FString::Printf(TEXT("NOT ENOUGH MANA  %.0f / %.0f"),Hero->Mana,Inv->ResourceFailNeed);
        Label(Warn,(ViewW-TextWidth(Warn,16))*.5f+Shake,ViewH-236,16,FLinearColor(.55f,.72f,1.f,Flash));}
    else if(Flash>0&&Inv&&Inv->ResourceFailKind==2){const FString Warn=FString::Printf(TEXT("NOT ENOUGH ENERGY  %.0f / %.0f"),Hero->Energy,Inv->ResourceFailNeed);
        Label(Warn,(ViewW-TextWidth(Warn,16))*.5f,ViewH-236,16,FLinearColor(1.f,.85f,.25f,Flash));}
    Bar(BX,72,BW,5,Hero->Energy/100.f,CireUIColors::Energy); // energy stays yellow in every theme
    TextFx(FString::Printf(TEXT("STR %d   AGI %d   INT %d"),Hero->Strength,Hero->Agility,Hero->Intelligence),10,112,10.5f,Parchment*FLinearColor(.9f,.9f,.9f,1),ECireFont::Numbers,true,true);
    if(Aggro==0){const FString EN=FString::Printf(TEXT("EN %.0f"),Hero->Energy);TextFx(EN,252-TextWidthFont(EN,10.5f,ECireFont::Numbers),112,10.5f,CireUIColors::Energy,ECireFont::Numbers,true,true);}
    // (Health/mana/energy help text now lives in the F10 panel description; no gameplay popups.)
    DrawStatuses(Hero,BX,83,24,bThemed?4:5);
    if(Clicked&&Hit(0,0,260,132)&&!bModal&&!bSettings&&!bEditLayout){if(auto* C=Cast<ACireController>(PlayerOwner))C->ServerAction(0,0,Hero);Clicked=false;}
}
void ACireHUD::DrawParty(ACireHero* Hero,ACireController* Controller)
{
    UsePanel(TEXT("Party"),210,248);
    const bool bThemedParty=CireUIStyle::HasThemeArt();
    if(bThemedParty)CireUIStyle::Frame(Painter(),-4,-6,218,256,Gold,ECireFrame::Unit); // ui-themes: the company panel around the rows
    // readability: larger outlined captions, names, HP values and level medallions on every row.
    TextFx(Hero->TeamId==0?TEXT("EMBER COMPANY"):TEXT("DUSK COMPANY"),bThemedParty?8:1,2,10.5f,bThemedParty?Parchment:Gold,ECireFont::Heading,true,true);
    {const FString Count=TEXT("PARTY / 5");TextFx(Count,202-TextWidthFont(Count,9,ECireFont::Heading),3,9,Parchment*.75f,ECireFont::Heading,true,true);}
    TArray<ACireHero*> Allies;
    for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(*It!=Hero&&It->TeamId==Hero->TeamId&&!Cast<ACireSummon>(*It))Allies.Add(*It);
    Allies.Sort([](const ACireHero& A,const ACireHero& B){return A.GetName()<B.GetName();});
    for(int32 I=0;I<FMath::Min(Allies.Num(),4);++I) {
        ACireHero* Ally=Allies[I]; const float Y=20+I*56;
        const bool Selected=Hero->Target==Ally, Focus=Controller&&Controller->FocusTarget==Ally;
        const bool Over=Hit(0,Y,210,50)&&!bModal&&!bEditLayout&&!bSettings;
        const int32 AllyAggro=Ally->bDead?0:AggroCount(GetWorld(),Ally);
        if(AllyAggro>0)Panel(-2,Y-2,214,54,FLinearColor(.9f,.08f,.05f,.4f));
        // ui-themes: party rows are unit frames (no crest); selection / aggro colour the accent line.
        if(bThemedParty)
        {
            CireUIStyle::Frame(Painter(),2,Y,206,50,Selected?CireUIColors::ThemeAccent*1.3f:AllyAggro>0?FLinearColor(1.f,.25f,.2f,1):Gold,ECireFrame::Card);
            Disc(19.f,Y+25.f,16,FLinearColor(0,0,0,.9f));Disc(19.f,Y+22.f,11,RoleColor(Ally->Archetype)*FLinearColor(1,1,1,.14f));
        }
        else Frame(0,Y,210,50,Selected?Parchment:AllyAggro>0?FLinearColor(1.f,.25f,.2f,1):RoleColor(Ally->Archetype)*.65f);
        if(Over)Panel(1,Y+1,208,48,FLinearColor(.3f,.45f,.48f,.12f));
        // ui-themes: inside the round ring the class sigil reads cleanly (the square painted role emblem
        // was cropped by the ring into a red "slash" glyph); the ring sits on top.
        if(bThemedParty)
        {
            // hud-art: painted champion portrait in the ring (role emblem as a badge); sigil fallback.
            const bool bFace=CireUIStyle::PortraitFace(Painter(),Ally->ChampionProfileId,19.f,Y+25.f,15.5f,Ally->bDead);
            if(!bFace)CireUIStyle::Sigil(Painter(),FString::Printf(TEXT("role%d"),Ally->Archetype),7.f,Y+13.f,24.f,Ally->bDead?Muted:RoleColor(Ally->Archetype)*1.1f+FLinearColor(.08f,.08f,.08f,0));
            CireUIStyle::PortraitRing(Painter(),19.f,Y+25.f,15.5f);
            if(bFace)CireUIStyle::RoleBadge(Painter(),19.f+12.f,Y+37.f,6.f,FString::Printf(TEXT("role%d"),Ally->Archetype),Ally->bDead?Muted:RoleColor(Ally->Archetype)*1.1f+FLinearColor(.08f,.08f,.08f,0));
        }
        else Icon(FString::Printf(TEXT("role%d"),Ally->Archetype),4,Y+10,29,Ally->bDead?Muted:RoleColor(Ally->Archetype));
        TextFx(Painter().Fit(Ally->HeroName,12.5f,142,ECireFont::Bold),39,Y+1,12.5f,Ally->bDead?Muted:Parchment,ECireFont::Bold,true,true);
        if(bThemedParty)CireUIStyle::Medallion(Painter(),6.5f,Y+40.f,7.5f,FString::FromInt(Ally->Level),CireUIColors::BrightGold);
        else TextFx(FString::FromInt(Ally->Level),158,Y+3,11,Gold,ECireFont::Numbers,true,true);
        Bar(39,Y+20,130,15,Fraction(Ally->Health,Ally->MaxHealth),Ally->bDead?Muted:LifeGreen);
        {
            const FString HP=Ally->bDead?FString(TEXT("FALLEN")):FString::Printf(TEXT("%.0f / %.0f"),Ally->Health,Ally->MaxHealth);
            const ECireFont HF=Ally->bDead?ECireFont::Heading:ECireFont::Numbers;
            TextFx(HP,39+(130-TextWidthFont(HP,10,HF))*.5f,Y+20+(15-CireUIStyle::ReadableSize(10)*1.28f)*.5f,10,FLinearColor::White,HF,true,false);
        }
        const int32 Poisoned=PoisonCount(Ally);
        Bar(39,Y+38,Poisoned>0?60:130,6,Fraction(Ally->Mana,Ally->MaxMana),Blue);
        if(Poisoned>0)TextFx(PoisonLabel(Poisoned),104,Y+35,8,Poison,ECireFont::Heading,true,false);
        Panel(186,Y+3,20,17,Focus?Gold:Card);TextFx(Focus?TEXT("*"):TEXT("+"),196-TextWidthFont(Focus?TEXT("*"):TEXT("+"),13,ECireFont::Bold)*.5f,Y+1,13,Focus?Ink:Gold,ECireFont::Bold,false,true);
        UnitTip(Ally,0,Y,184,49);
        Tip(TEXT("Focus teammate"),TEXT("Keep a second persistent unit frame for this teammate. Click the focus frame to make them your current target."),186,Y+3,20,17);
        DrawStatuses(Ally,172,Y+24,15,2);
        if(Over&&Clicked&&Controller) {
            if(Hit(182,Y,28,23))Controller->SetFocusTarget(Focus?nullptr:Ally);
            else Controller->ServerAction(0,0,Ally);
            Clicked=false;
        }
    }
}
void ACireHUD::DrawMatch(ACireGameState* State)
{
    // Compact top-centre match plate (250x70 design).
    if(!State)return;UsePanel(TEXT("Match"),250,70);Frame(0,0,250,70,Gold);
    const TCHAR* Phases[]={TEXT("SURVIVAL"),TEXT("TOWN PREPARATION"),TEXT("PORTAL ARENA"),TEXT("MATCH COMPLETE"),TEXT("RECOVERY")};
    const FString Phase=Phases[FMath::Clamp(State->Phase,0,4)];
    Label(Phase,(250-TextWidth(Phase,9))/2,7,9,State->Phase==2?Red:Teal);
    // Sentinel / frozen clocks (huge or non-finite) show as "--:--" instead of leaking.
    const bool bClockValid=FMath::IsFinite(State->SecondsLeft)&&State->SecondsLeft<100*60;
    const int32 Seconds=bClockValid?FMath::Max(0,FMath::CeilToInt(State->SecondsLeft)):0;
    const FString Time=State->Phase==0?FString::Printf(TEXT("%d / %d"),State->CycleWavesDone,State->WavesPerCycle):bClockValid?FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60):FString(TEXT("--:--"));
    Label(Time,(250-TextWidth(Time,22))/2,20,22,Parchment);
    Label(FString::Printf(TEXT("%02d"),State->EmberLives),14,19,21,Gold);Label(TEXT("EMBER"),13,44,8,Muted);
    const FString Dusk=FString::Printf(TEXT("%02d"),State->DuskLives);
    Label(Dusk,236-TextWidth(Dusk,21),19,21,Blue);Label(TEXT("DUSK"),236-TextWidth(TEXT("DUSK"),8),44,8,Muted);
    // wave-director: current and next wave from the authoritative director (replicated labels).
    FString Sub=State->Phase==0?FString::Printf(TEXT("WAVE %d / CYCLE CLEARS"),State->Wave):FString::Printf(TEXT("ROUND %d  /  WAVE %d"),State->Round,State->Wave);
    if(State->Phase==0&&!State->WaveLabel.IsEmpty())Sub=State->WaveLabel.ToUpper();
    Sub=Painter().Fit(Sub,8,226,ECireFont::Heading);
    Label(Sub,(250-TextWidth(Sub,8))/2,54,8,Muted);
    if(State->Phase==0&&!State->NextWaveLabel.IsEmpty()) {
        const FString NextWave=Painter().Fit(FString(TEXT("NEXT: "))+State->NextWaveLabel.ToUpper(),8,240,ECireFont::Heading);
        Label(NextWave,(250-TextWidth(NextWave,8))/2,90,8,Muted);
    }
    if(State->Phase==0&&FMath::IsFinite(State->NextWaveSeconds)&&State->NextWaveSeconds>.05f&&State->NextWaveSeconds<3600.f) {
        // progression-shop: in Skill Shop mode the wave waits for every human's READY TO CONTINUE.
        const FString Next=State->bReadyGateHold?CireSkillShop::WaitingLabel(State->BreatherPlayers,State->BreatherReady)
            :FString::Printf(TEXT("NEXT WAVE IN %ds"),FMath::CeilToInt(State->NextWaveSeconds));
        Label(Next,(250-TextWidth(Next,9))/2,76,9,Gold);
        // wave-director: breather Ready (the Skill Shop window ends early once every player is ready).
        if(BreatherReadyWave!=State->Wave){BreatherReadyWave=State->Wave;bBreatherReadyLocal=false;}
        auto* Controller=Cast<ACireController>(PlayerOwner);
        if(State->BreatherPlayers>0&&State->NextWaveSeconds>1.2f&&Controller) {
            const bool bGate=CireSkillShop::IsSkillShopMode(GetWorld()); // progression-shop: READY TO CONTINUE gate
            const FString Caption=FString::Printf(TEXT("%s  %d/%d"),bBreatherReadyLocal?TEXT("READY"):bGate?TEXT("READY TO CONTINUE"):TEXT("READY UP"),State->BreatherReady,State->BreatherPlayers);
            const bool Over=Hit(bGate?55:75,104,bGate?140:100,22);
            CireUIStyle::Button(Painter(),bGate?55:75,104,bGate?140:100,22,Caption,bBreatherReadyLocal?ECireButtonState::Selected:Over?ECireButtonState::Hover:ECireButtonState::Normal,bBreatherReadyLocal?Teal:Gold,8.5f);
            Tip(TEXT("Ready"),TEXT("Start the next wave early. It begins 1 second after every player is ready; otherwise the full breather runs."),75,104,100,22);
            if(Over&&Clicked){Clicked=false;PlayUIFeedback();bBreatherReadyLocal=!bBreatherReadyLocal;Controller->ServerAction(10,bBreatherReadyLocal?1:0,nullptr);}
        }
    }
}
void ACireHUD::DrawMinimap(ACireHero* Hero,ACireGameState* State)
{
    UsePanel(TEXT("Minimap"),220,178);Frame(0,0,220,178,Gold);
    const bool Arena=State&&State->Phase==2;
    // arenas: the picked arena's name, bounds and blocker layout (Content/Data/Arenas.json)
    const CireArenas::FArena* ArenaDef=Arena?CireArenas::Current(GetWorld()):nullptr;
    const FVector ArenaCentre=CireArenas::Center(GetWorld());
    const FVector2D ArenaHalf=CireArenas::HalfExtents(GetWorld());
    const float ArenaScale=FMath::Min(184.f/(2*ArenaHalf.X),106.f/(2*ArenaHalf.Y));
    Label(Arena?(ArenaDef?ArenaDef->Name.ToUpper():FString(TEXT("PORTAL BATTLEFIELD"))):FString(TEXT("THE TWIN CITADELS")),12,9,10,Parchment);
    Label(TEXT("N"),204,9,9,Gold);Panel(9,28,202,125,FLinearColor(.032f,.044f,.039f,1));
    auto Map=[&](FVector P,int32 Team)->FVector2D {
        if(Arena) {
            // arenas: Ember (west, -X) on the left, Dusk on the right, north up; aspect preserved.
            const float LX=FMath::Clamp(static_cast<float>(P.X-ArenaCentre.X),-ArenaHalf.X,ArenaHalf.X),LY=FMath::Clamp(static_cast<float>(P.Y-ArenaCentre.Y),-ArenaHalf.Y,ArenaHalf.Y);
            return FVector2D(110+LX*ArenaScale,91-LY*ArenaScale);
        }
        const auto& R=CireLanePath::Get(GetWorld());
        const FVector2D L=CireLanePath::ToLocal(Team,P); // medieval-kingdom: realm frame from data
        return FVector2D(Team*101+13+FMath::Clamp((L.Y+R.HalfWidth)/(2*R.HalfWidth),0.f,1.f)*93,
            145-FMath::Clamp((L.X-R.MinX)/(R.MaxX-R.MinX),0.f,1.f)*111);
    };
    if(Arena) {
        // arenas: themed ground, blocker footprints, spawn edges.
        const float W=2*ArenaHalf.X*ArenaScale,H=2*ArenaHalf.Y*ArenaScale,L=110-W*.5f,T=91-H*.5f;
        Panel(L,T,W,H,ArenaDef?ArenaDef->MinimapGround:FLinearColor(.05f,.05f,.05f,1));
        Line(L,T,L+W,T,Muted);Line(L,T+H,L+W,T+H,Muted);Line(L,T,L,T+H,Muted);Line(L+W,T,L+W,T+H,Muted);
        if(ArenaDef) for(const auto& F:CireArenas::Footprints(*ArenaDef,CireArenas::Pool())) {
            const FVector2D C(110+F.Center.X*ArenaScale,91-F.Center.Y*ArenaScale);
            const FLinearColor Fill=ArenaDef->MinimapBlocker;
            if(F.bRound) {
                const float R=FMath::Max(1.5f,static_cast<float>(F.Extent.X)*ArenaScale);
                for(float Y=-R;Y<=R;Y+=1.f){const float Half=FMath::Sqrt(FMath::Max(0.f,R*R-Y*Y));Line(C.X-Half,C.Y+Y,C.X+Half,C.Y+Y,Fill,1.2f);}
                continue;
            }
            const float Yaw=FMath::DegreesToRadians(F.Yaw),Cs=FMath::Cos(Yaw),Sn=FMath::Sin(Yaw);
            const FVector2D AX(Cs*F.Extent.X*ArenaScale,-Sn*F.Extent.X*ArenaScale),AY(-Sn*F.Extent.Y*ArenaScale,-Cs*F.Extent.Y*ArenaScale);
            const float Along=FMath::Max(1.f,static_cast<float>(2*F.Extent.Y*ArenaScale));
            for(float S=-1;S<=1.001f;S+=1.f/Along){const FVector2D A=C+AY*S-AX,B=C+AY*S+AX;Line(A.X,A.Y,B.X,B.Y,Fill,1.4f);}
        }
        const FLinearColor Accent=ArenaDef?ArenaDef->MinimapAccent:Gold;
        Line(L+2,T+2,L+2,T+H-2,Teal*.8f,2);Line(L+W-2,T+2,L+W-2,T+H-2,Red*.8f,2);
        Line(110,T+1,110,T+H-1,Accent*.35f);
    } else {
        for(int32 Team=0;Team<2;++Team) {
            const float X=Team*101+12;const bool Visible=Team==Hero->TeamId;
            Panel(X,31,94,119,FLinearColor(.025f,.037f,.036f,1));
            if(!Visible) {Panel(X+2,33,90,115,FLinearColor(.01f,.017f,.023f,.93f));Label(TEXT("ENEMY"),X+26,73,10,Muted);Label(TEXT("REALM"),X+26,87,10,Muted);Label(TEXT("OBSCURED"),X+16,111,8,Muted);continue;}
            const auto& Points=CireLanePath::Get(GetWorld()).LocalPoints[Team];
            for(int32 I=1;I<Points.Num();++I) {
                const auto A=Map(FVector(Points[I-1]+CireLanePath::RealmOrigin(Team),0),Team);
                const auto B=Map(FVector(Points[I]+CireLanePath::RealmOrigin(Team),0),Team);
                Line(A.X,A.Y,B.X,B.Y,FLinearColor(.24f,.27f,.23f,1),5);
                Line(A.X,A.Y,B.X,B.Y,Gold*.7f,1);
            }
            const auto Spawn=Map(CireLanePath::SpawnPosition(GetWorld(),Team),Team);
            Panel(Spawn.X-9,Spawn.Y-3,18,6,Red);Label(TEXT("BREACH"),X+25,30,8,Muted);
            DrawRouteMinimap(Team,Map); // nav-paths: navmesh coverage and the path editor draft
            const auto Base=Map(CireLanePath::GoalZoneCenter(GetWorld(),Team,0),Team); // nav-paths: editable goal zone
            Panel(Base.X-12,Base.Y-3,24,6,Teal);Label(TEXT("KEEP"),X+32,139,8,Teal);
            for(int32 Bay=1,Bays=CireLanePath::BayCount(GetWorld(),Team);Bay<=Bays;++Bay) { // dev-route-tools: 1..16 packs
                const auto P=Map(CireLanePath::ChallengePosition(GetWorld(),Team,Bay),Team);
                Line(P.X,P.Y-4,P.X+4,P.Y,Gold);Line(P.X+4,P.Y,P.X,P.Y+4,Gold);Line(P.X,P.Y+4,P.X-4,P.Y,Gold);Line(P.X-4,P.Y,P.X,P.Y-4,Gold);
            }
        }
        Line(110,28,110,153,Gold*.45f);
    }
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It) {
        if(Arena||It->Health<=0||It->Lane!=Hero->TeamId)continue;
        const auto P=Map(It->GetActorLocation(),It->Lane);
        if(It->bBoss){Panel(P.X-3,P.Y-3,7,7,Gold);Panel(P.X-2,P.Y-2,5,5,Red);}
        else if(It->bArmoredEscort){Panel(P.X-3,P.Y-3,6,6,Gold);Panel(P.X-1,P.Y-1,2,2,Ink);}
        else Panel(P.X-1,P.Y-1,3,3,It->PackId>=0?Purple:Red);
    }
    // progression-shop: your unopened personal loot chests (only the owner receives them).
    if(!Arena)for(TActorIterator<ACireLootDrop> It(GetWorld());It;++It) {
        if(It->bOpened||It->OwnerHero!=Hero)continue;
        const auto P=Map(It->GetActorLocation(),Hero->TeamId);const FLinearColor C=ACireLootDrop::RarityColor(It->Rarity);
        Panel(P.X-4,P.Y-3,9,7,Ink);Panel(P.X-3,P.Y-2,7,5,C);Line(P.X-3,P.Y,P.X+4,P.Y,Ink,1);
    }
    // vendors: the three merchants' emblems in your realm's town.
    if(!Arena)for(TActorIterator<ACireVendor> It(GetWorld());It;++It) {
        if(It->Team!=Hero->TeamId)continue;
        const FCireVendorDef* Def=CireVendors::Find(It->VendorId);if(!Def)continue;
        const auto P=Map(It->GetActorLocation(),Hero->TeamId);
        FCireUIPainter MP=Painter();
        MP.Disc(P.X,P.Y,5.2f,Ink,16);MP.Disc(P.X,P.Y,4.6f,Def->Accent,16);
        if(UTexture2D* Emblem=CireVendors::Emblem(Def->Id))MP.TexDisc(Emblem,P.X,P.Y,3.8f,FLinearColor::White,0,0,1,1,16);
    }
    for(TActorIterator<ACireHero> It(GetWorld());It;++It) {
        if(It->bDead||(!Arena&&It->TeamId!=Hero->TeamId))continue;
        const auto P=Map(It->GetActorLocation(),It->TeamId);const bool Self=*It==Hero;
        const FLinearColor C=Self?Parchment:It->TeamId==Hero->TeamId?Teal:Red;
        Panel(P.X-2,P.Y-2,Self?5:4,Self?5:4,C);
        if(Self){Line(P.X,P.Y-6,P.X-4,P.Y+2,Gold,1.5f);Line(P.X,P.Y-6,P.X+4,P.Y+2,Gold,1.5f);}
    }
    CireUIStyle::MinimapFrame(Painter(),9,28,202,125); // ui-themes: ornate border over the map
    Label(Arena?TEXT("ALLIES   /   ENEMIES"):TEXT("ALLIES   /   WAVES   /   CHALLENGES"),12,160,8,Muted);
}

void ACireHUD::DrawSkills(ACireHero* Hero,ACireController* Controller)
{
    UsePanel(TEXT("Skills"),584,155);Frame(0,0,584,155,Gold);
    Label(TEXT("ARSENAL"),14,9,10,Muted);
    Label(FString::Printf(TEXT("%d GOLD"),Hero->Gold),469,9,12,Gold);
    const FString Stats=FString::Printf(TEXT("ATK %.0f   /   CDR %.0f%%   /   XP %d"),Hero->AttackDamage(),Hero->CDR*100,Hero->Experience);
    Label(Stats,106,11,9,Muted);
    const bool Interactive=!bModal&&!bEditLayout&&!bSettings;
    Frame(12,33,66,78,Hero->bAutoAttack?Teal:Gold);
    Icon(TEXT("basic"),19,40,50,Gold);Panel(14,92,62,17,Card);Label(UISettings.Keybindings.Label(TEXT("ToggleAutoAttack")).ToUpper(),26,94,9,Parchment); // key labels: feat/camera-movement keybindings
    Label(TEXT("ATTACK"),23,118,9,Hero->bAutoAttack?Teal:Muted);
    if(Clicked&&Interactive&&Hit(12,33,66,78)&&Controller){Controller->ServerAction(1,0,nullptr);Clicked=false;}
    Tip(TEXT("Basic attack / ")+UISettings.Keybindings.FullLabel(TEXT("ToggleAutoAttack")),FString::Printf(TEXT("ENEMY target / %.1fm range / %s. "),Hero->BasicAttackRange()/100,*Hero->BasicAttackStyle())+(Hero->IsRangedBasicAttack()?TEXT("Targeted shots follow the selected enemy. Shooting uphill increases miss chance."):TEXT("Toggle close-range auto attacks against the selected enemy."))+TEXT(" Hits show damage; misses and dodges show combat text. The selected unit's outer ring marks its attack range."),12,33,66,78);
    int32 Hovered=INDEX_NONE,PassiveSlot=INDEX_NONE,UltimateSlot=INDEX_NONE;bool HoverEmpty=false;
    TArray<int32> ActiveSlots;
    for(int32 I=0;I<Hero->Skills.Num();++I) {
        if(ACireHero::IsPassive(Hero->Skills[I]))PassiveSlot=I;
        else if(ACireHero::IsUltimate(Hero->Skills[I]))UltimateSlot=I;
        else ActiveSlots.Add(I);
    }
    for(int32 Position=0;Position<6;++Position) {
        const int32 Slot=ActiveSlots.IsValidIndex(Position)?ActiveSlots[Position]:INDEX_NONE;
        const float X=96+(Position%3)*67,Y=31+(Position/3)*57;
        const bool Learned=Hero->Skills.IsValidIndex(Slot);
        const bool Over=Hit(X,Y,53,53)&&Interactive;
        const FString Id=Learned?Hero->Skills[Slot]:FString();
        const float Cooldown=Hero->Cooldowns.IsValidIndex(Slot)?Hero->Cooldowns[Slot]:0.f;
        Frame(X,Y,53,53,Over?Parchment:Learned?Gold:Muted*.4f);
        Panel(X+3,Y+3,47,47,Learned?FLinearColor(.06f,.095f,.11f,1):FLinearColor(.015f,.024f,.031f,1));
        Icon(Id,X+5,Y+3,43,Learned?Teal:Muted*.36f);
        if(Cooldown>.05f) {
            Panel(X+3,Y+3,47,47,FLinearColor(0,0,0,.58f));
            const FString CD=FString::Printf(TEXT("%.1f"),Cooldown);Label(CD,X+(53-TextWidth(CD,18))/2,Y+17,18,Parchment);
        }
        if(Learned){const FString KeyText=UISettings.Keybindings.Label(CireKeybindings::SlotAction(1,Position+1));const float KW=FMath::Max(14.f,TextWidth(KeyText,10)+6);Panel(X+52-KW,Y+1,KW,15,Ink);Label(KeyText,X+55-KW,Y+1,10,Gold);}
        if(Learned){const auto D=CireTargeting::Describe(Id);const TCHAR* Tag=D.Kind==ECireTargetKind::Self?TEXT("SELF"):D.Kind==ECireTargetKind::Friendly?TEXT("ALLY"):D.Kind==ECireTargetKind::Ground?TEXT("AIM"):TEXT("ENEMY");Panel(X+3,Y+41,47,10,Ink);Label(Tag,X+6,Y+41,7,D.Kind==ECireTargetKind::Friendly?Teal:Gold);}
        if(Over){Hovered=Slot;HoverEmpty=!Learned;if(Clicked&&Learned&&Controller){Controller->RequestCast(Slot);Clicked=false;}}
    }
    Line(303,32,303,138,Gold*.35f);
    Frame(316,34,59,59,PassiveSlot>=0?Purple:Muted*.4f);
    Icon(PassiveSlot>=0?Hero->Skills[PassiveSlot]:FString(),321,39,49,PassiveSlot>=0?Purple:Muted*.35f);
    Label(TEXT("PASSIVE"),324,103,9,Purple);Label(PassiveSlot>=0?TEXT("BOUND"):TEXT("UNBOUND"),325,120,8,Muted);
    if(Hit(316,34,59,59)&&Interactive&&PassiveSlot>=0)Hovered=PassiveSlot;
    const bool HasUltimate=UltimateSlot>=0;
    const float UltimateCD=Hero->Cooldowns.IsValidIndex(UltimateSlot)?Hero->Cooldowns[UltimateSlot]:0.f;
    Frame(390,34,59,59,HasUltimate?Gold:Muted*.4f);
    Icon(HasUltimate?Hero->Skills[UltimateSlot]:FString(),395,39,49,HasUltimate?Gold:Muted*.35f);
    if(HasUltimate){const FString KeyText=UISettings.Keybindings.Label(CireKeybindings::SlotAction(1,8));const float KW=FMath::Max(14.f,TextWidth(KeyText,10)+6);Panel(448-KW,35,KW,15,Ink);Label(KeyText,451-KW,35,10,Gold);}
    if(HasUltimate){const auto D=CireTargeting::Describe(Hero->Skills[UltimateSlot]);const TCHAR* Tag=D.Kind==ECireTargetKind::Self?TEXT("SELF"):D.Kind==ECireTargetKind::Friendly?TEXT("ALLY"):D.Kind==ECireTargetKind::Ground?TEXT("AIM"):TEXT("ENEMY");Panel(394,78,49,11,Ink);Label(Tag,398,79,7,D.Kind==ECireTargetKind::Friendly?Teal:Gold);}
    if(UltimateCD>.05f){Panel(393,37,53,53,FLinearColor(0,0,0,.58f));const FString CD=FString::Printf(TEXT("%.0f"),UltimateCD);Label(CD,419-TextWidth(CD,18)/2,54,18,Parchment);}
    Label(TEXT("ULTIMATE"),391,103,9,HasUltimate?Gold:Muted);Label(HasUltimate?(UltimateCD>.05f?FString(TEXT("COOLDOWN")):TEXT("READY / ")+UISettings.Keybindings.Label(CireKeybindings::SlotAction(1,8))):FString(TEXT("UNBOUND")),394,120,8,HasUltimate?Gold:Muted);
    if(Hit(390,34,59,59)&&Interactive&&HasUltimate){Hovered=UltimateSlot;if(Clicked&&Controller){Controller->RequestCast(UltimateSlot);Clicked=false;}}
    Frame(466,34,103,59,Gold*.65f);Label(TEXT("TOWN SHOP"),477,46,11,Gold);Label(TEXT("[ ")+UISettings.Keybindings.Label(TEXT("ToggleShop"))+TEXT(" ]"),498,69,11,Muted);
    if(Hit(466,34,103,59)&&Clicked&&Interactive&&Controller){Controller->bShop=!Controller->bShop;Clicked=false;}
    Label(UISettings.Keybindings.Label(TEXT("ToggleOptions"))+TEXT("  UI OPTIONS"),468,105,9,Muted);Label(UISettings.Keybindings.Label(TEXT("ToggleLayoutEditor"))+TEXT("  EDIT LAYOUT"),468,123,9,Muted);
    if(Hit(465,101,108,20)&&Clicked&&Interactive){ToggleSettings();Clicked=false;}
    if(Hit(465,122,108,20)&&Clicked&&Interactive){ToggleLayoutEditor();Clicked=false;}
    Tip(TEXT("Town shop / B"),TEXT("Buy experience, primary-stat tomes, gear and cooldown reduction. Purchases require preparation or recovery at your own town."),466,34,103,59);
    Tip(TEXT("Options / F9"),TEXT("Camera controls, combat text, tooltips, status filters, display settings, audio and diagnostics."),465,101,108,20);
    Tip(TEXT("Edit layout / F10"),TEXT("Move and resize each interface panel. U/L locks individual panels. F10 or Escape saves the layout."),465,122,108,20);
    if(Hovered!=INDEX_NONE||HoverEmpty) {
        const bool Learned=Hero->Skills.IsValidIndex(Hovered);
        TooltipTitle=Learned?ACireHero::SkillName(Hero->Skills[Hovered]):TEXT("Unbound ability slot");
        TooltipBody=Learned?ACireHero::SkillDescription(Hero->Skills[Hovered]):Hero->Skills.Num()>=8?TEXT("Your build is complete: six active skills, one passive, and one ultimate."):TEXT("Learn an ability through your choices. Six regular active skills appear here; empty slots do nothing.");
        if(Learned){const auto D=CireTargeting::Describe(Hero->Skills[Hovered]);TooltipBody=TEXT("TARGET: ")+D.Label+(D.Range>0?FString::Printf(TEXT(" / %.1fm range. "),D.Range/100):TEXT(". "))+TooltipBody;}
    }
    if(Hit(390,34,59,59)&&Interactive&&!HasUltimate) {
        TooltipTitle=TEXT("Ultimate slot");TooltipBody=TEXT("Your chosen ultimate appears here. Press ")+UISettings.Keybindings.FullLabel(CireKeybindings::SlotAction(1,8))+TEXT(" to use it. You can learn only one ultimate in a completed build.");
    }
}
void ACireHUD::DrawChat(ACireController* Controller)
{
    if(!UISettings.bShowChat&&!bEditLayout&&!(Controller&&Controller->bChatInput))return;
    UsePanel(TEXT("Chat"),306,172);Frame(0,0,306,172,Gold*.6f);
    Label(TEXT("CHAT"),12,8,10,Parchment);
    const bool Interactive=!bModal&&!bEditLayout&&!bSettings;
    const bool Team=Controller&&Controller->bChatTeamOnly;
    CireUIStyle::Button(Painter(),179,5,54,19,TEXT("PARTY"),Team?ECireButtonState::Selected:Hit(179,5,54,19)?ECireButtonState::Hover:ECireButtonState::Normal,Teal,8.f);
    CireUIStyle::Button(Painter(),236,5,60,19,TEXT("EVERYONE"),!Team?ECireButtonState::Selected:Hit(236,5,60,19)?ECireButtonState::Hover:ECireButtonState::Normal,Gold,8.f);
    if(Controller&&Clicked&&Interactive&&Hit(174,4,121,22)){Controller->bChatTeamOnly=Hit(174,4,60,22);Clicked=false;}
    if(Controller) {
        const float Font=UISettings.ChatFontSize,LineH=Font+4;
        const int32 Lines=FMath::Max(2,FMath::FloorToInt(106/LineH));
        TArray<TPair<FString,FLinearColor>> Display;
        for(const auto& Message:Controller->ChatMessages) {
            const FString Prefix=FString::Printf(TEXT("[%s] %s: "),Message.bTeamOnly?TEXT("P"):TEXT("ALL"),*ShortName(Message.Sender,16));
            TArray<FString> Words;(Prefix+Message.Text).ParseIntoArrayWS(Words);FString Row;
            const FLinearColor C=Message.bTeamOnly?UISettings.ChatColor:FLinearColor(.88f,.76f,.49f,1);
            for(const auto& Word:Words) {const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
                if(!Row.IsEmpty()&&TextWidth(Next,Font)>280){Display.Emplace(Row,C);Row=Word;}else Row=Next;}
            if(!Row.IsEmpty())Display.Emplace(Row,C);
        }
        ChatScroll=FMath::Clamp(ChatScroll,0,FMath::Max(0,Display.Num()-Lines));
        const int32 End=Display.Num()-ChatScroll,Start=FMath::Max(0,End-Lines);
        for(int32 I=Start;I<End;++I)Label(Display[I].Key,12,30+(I-Start)*LineH,Font,Display[I].Value);
        if(Display.Num()==0){Label(TEXT("Your party's conversation appears here."),12,36,10,Muted);Label(TEXT("Enter to chat. Tab changes the channel."),12,54,10,Muted);}
        if(ChatScroll>0)Label(TEXT("SCROLLED / WHEEL TO LATEST"),80,124,8,Gold);
        if(CireUIStyle::HasThemeArt())CireUIStyle::Frame(Painter(),8,141,290,24,Controller->bChatInput?CireUIColors::ThemeAccent:Gold,ECireFrame::Inset); // ui-themes
        else Panel(8,141,290,24,Controller->bChatInput?Hover:Card);
        const FString Draft=Controller->bChatInput?Controller->ChatDraft.Right(38)+TEXT("|"):TEXT("Press Enter to chat...");
        Label(Controller->bChatTeamOnly?TEXT("P"):TEXT("ALL"),13,147,9,Team?Teal:Gold);
        Label(Draft,40,146,10,Controller->bChatInput?Parchment:Muted);
        if(Clicked&&Interactive&&Hit(8,141,290,24)){Controller->BeginChat();Clicked=false;}
    }
}
void ACireHUD::DrawMeters(ACireHero* Hero,ACireController* Controller)
{
    if(UISettings.bShowMeter||bEditLayout) {
        // Compact, collapsible meter (220x134 design). Click the header to fold it.
        const bool Collapsed=UISettings.bMeterCollapsed&&!bEditLayout;
        UsePanel(TEXT("Meter"),220,134);
        const bool Heal=UISettings.MeterMode==1,Interactive=!bModal&&!bSettings&&!bEditLayout;
        FCireUIPainter P=Painter();
        if(!Collapsed)CireUIStyle::Frame(P,0,0,220,134,Gold*.65f,ECireFrame::Card);
        P.Rect(0,0,220,20,FLinearColor(.12f,.08f,.03f,.9f));P.Line(0,20,220,20,Gold*.6f,1.f);
        CireUIStyle::Chevron(P,5,5,10,Collapsed);
        P.Text(Heal?TEXT("HEALING"):TEXT("DAMAGE"),19,4,9.5f,Parchment,ECireFont::Heading);
        CireUIStyle::Button(P,118,2,48,16,TEXT("DMG"),!Heal?ECireButtonState::Selected:Hit(118,2,48,16)?ECireButtonState::Hover:ECireButtonState::Normal,Gold,7.5f);
        CireUIStyle::Button(P,168,2,48,16,TEXT("HEAL"),Heal?ECireButtonState::Selected:Hit(168,2,48,16)?ECireButtonState::Hover:ECireButtonState::Normal,Teal,7.5f);
        if(Clicked&&Interactive&&Hit(118,2,98,16)){UISettings.MeterMode=Hit(118,2,48,16)?0:1;UISettings.Save();Clicked=false;}
        else if(Clicked&&Interactive&&Hit(0,0,116,20)){UISettings.bMeterCollapsed=!UISettings.bMeterCollapsed;UISettings.Save();Clicked=false;}
        if(!Collapsed)
        {
            TArray<ACireHero*> Party;for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(It->TeamId==Hero->TeamId&&!Cast<ACireSummon>(*It))Party.Add(*It);
            Party.Sort([Heal](const ACireHero& A,const ACireHero& B){return Heal?A.HealingDone>B.HealingDone:A.DamageDone>B.DamageDone;});
            float Total=0,Max=1;for(auto* H:Party){float V=Heal?H->HealingDone:H->DamageDone;Total+=V;Max=FMath::Max(Max,V);}
            for(int32 I=0;I<FMath::Min(Party.Num(),5);++I) {
                auto* H=Party[I];const float Value=Heal?H->HealingDone:H->DamageDone,Y=25+I*21.5f;
                Bar(5,Y,210,18,Value/Max,(Heal?Teal:RoleColor(H->Archetype))*.45f);
                const FString Amount=FString::Printf(TEXT("%.0f (%.0f%%)"),Value,Fraction(Value,Total)*100);
                const float AW=P.TextWidth(Amount,8.5f,ECireFont::Numbers);
                P.Text(P.Fit(FString::Printf(TEXT("%d. %s"),I+1,*(H==Hero?FString(TEXT("You")):H->HeroName)),9.5f,200-AW-12,ECireFont::Bold),10,Y+2,9.5f,H==Hero?FLinearColor::White:Parchment,ECireFont::Bold,true,false);
                P.Text(Amount,210-AW,Y+3,8.5f,Parchment,ECireFont::Numbers,true,false);
            }
        }
    }
    if(UISettings.bShowCombatLog||bEditLayout) {
        UsePanel(TEXT("CombatLog"),304,150);Frame(0,0,304,150,Gold*.5f);Label(TEXT("COMBAT EVENTS"),12,9,10,Parchment);
        Label(TEXT("DAMAGE / HEALING / AVOIDANCE"),135,11,7,Muted);
        if(Controller) {
            const int32 Start=FMath::Max(0,Controller->CombatEvents.Num()-4);
            for(int32 I=Start;I<Controller->CombatEvents.Num();++I) {
                const auto& E=Controller->CombatEvents[I];const float Y=30+(I-Start)*28;
                const FString Result=E.Outcome==ECireHitOutcome::Hit?FString::Printf(TEXT("%s%.0f%s"),E.bHealing?TEXT("+"):TEXT("-"),E.Amount,E.bCritical?TEXT(" CRIT"):TEXT("")):
                    (E.Amount>0?FString::Printf(TEXT("%s %.0f"),*CireCombat::OutcomeText(E.Outcome).ToUpper(),E.Amount):CireCombat::OutcomeText(E.Outcome).ToUpper()); // scaling-kits: BLOCK / RESIST
                const FString Desc=FString::Printf(TEXT("%s > %s  %s"),*ShortName(E.SourceName,12),*ShortName(E.TargetName,12),*Result);
                Label(ShortName(Desc,48),12,Y,9,E.bHealing?Teal:Muted);
                Label(ShortName(E.AbilityName,48),12,Y+12,8,Muted*.8f);
            }
            if(Controller->CombatEvents.Num()==0)Label(TEXT("Combat events will appear as they happen."),12,38,10,Muted);
        }
    }
}
void ACireHUD::DrawHUD()
{
    const CireVideo::FDrawScope VideoDrawScope; // video-crash: no viewport resize while the HUD draws
    Super::DrawHUD();if(!Canvas||!PlayerOwner||Canvas->ClipX<=0||Canvas->ClipY<=0)return;
    // Interface scale (WoW-style): the resolution fit times the player's UI scale. A
    // slider drag is applied on release so the Options window does not move under it.
    if(PendingUIScale>0&&!PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton)){UISettings.UIScale=PendingUIScale;UISettings.bAutoUIScale=false;PendingUIScale=-1;UISettings.Save();}
    Scale=FMath::Max(.25f,FMath::Min(Canvas->ClipX/1280.f,Canvas->ClipY/720.f)*UISettings.ResolveUIScale(Canvas->ClipY));ViewW=Canvas->ClipX/Scale;ViewH=Canvas->ClipY/Scale;
    CireUIStyle::Assets();
    // ui-themes: the profile's theme (a -CireUITheme=<Id> command line overrides it for galleries,
    // without saving). SetActive only reapplies the palette when the theme actually changes.
    {
        static const FString Override=[]{FString Id;FParse::Value(FCommandLine::Get(),TEXT("CireUITheme="),Id);return Id;}();
        CireUITheme::SetActive(FName(Override.IsEmpty()?*UISettings.UITheme:*Override));
    }
    MX=MY=-100;float MouseX=0,MouseY=0;if(PlayerOwner->GetMousePosition(MouseX,MouseY)){MX=MouseX/Scale;MY=MouseY/Scale;}
#if !UE_BUILD_SHIPPING
    if(DebugPointer.X>=0){MX=DebugPointer.X;MY=DebugPointer.Y;} // gallery: virtual mouse (logical units)
#endif
    Clicked=PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton)&&!PlayerOwner->IsInputKeyDown(EKeys::RightMouseButton);
    auto* Controller=Cast<ACireController>(PlayerOwner);auto* Hero=Cast<ACireHero>(PlayerOwner->GetPawn());auto* State=GetWorld()->GetGameState<ACireGameState>();
    bModal=Hero&&((!Hero->bDrafted||(Hero->Offers.Num()>0&&IsSkillOfferOpen())||(Controller&&Controller->bShop))||(State&&State->Phase==3));
    TooltipTitle.Reset();TooltipBody.Reset();bRichTip=false;TooltipUnit.Reset();TooltipAbility.Reset();TooltipRegion={0,0,0,0};EditHelpTitle.Reset();EditHelpBody.Reset();
    LastPanelBoxes.Reset();
    for(FName Id:VisiblePanels)if(Id!=TEXT("CombatText")&&Id!=TEXT("Tooltip")){const FCireUIRect R=PanelRect(Id);LastPanelBoxes.Emplace(FVector2D(R.X,R.Y),FVector2D(R.X+R.W,R.Y+R.H));}
    LayoutInteraction();VisiblePanels.Reset();ResetTransform();
    if(DrawReplayScreen()){DrawSettings();DrawDiagnostics();DrawTooltip();ResetTransform();return;}
    if(!Hero){Label(TEXT("Joining the battlefield..."),ViewW*.5f-130,ViewH*.5f,20,Parchment);return;}
    // nav-paths: the in-world path editor replaces the gameplay HUD (minimap, editor overlay, toolbar, F8).
    DrawExploreOverlay(); // medieval-kingdom: -CireExplore coordinates, keys and the provisional route
    if(bRouteEditor){DrawMinimap(Hero,State);ResetTransform();TickRouteEditor();ResetTransform();DrawSettings();DrawTooltip();ResetTransform();return;}
    // dev-route-tools: the map layout editor replaces the whole gameplay HUD (edit mode: no combat UI at all).
    if(bLayoutEditor){ResetTransform();TickLayoutEditor();ResetTransform();DrawSettings();DrawTooltip();ResetTransform();return;}
    UpdateLevelUps(Hero);UpdateThreatAlerts(Hero);UpdateBanners(Hero,State);UpdateEffectCallouts(Hero);
    if(LastTargetSeen.Get()!=Hero->Target){if(IsValid(Hero->Target)&&!bModal)PlayWowSound(4,.55f);LastTargetSeen=Hero->Target;TargetChangedAt=GetWorld()->GetRealTimeSeconds();}
    if(!bModal)DrawNameplates(Hero);
    if(!bModal)DrawLevelUps(Hero);
    DrawPlayer(Hero);DrawParty(Hero,Controller);DrawMatch(State);DrawMinimap(Hero,State);
    if(IsValid(Hero->Target)||bEditLayout)DrawUnit(Hero->Target,TEXT("TARGET"),false);
    if(Controller&&((IsValid(Controller->FocusTarget)&&!IsInBossFrames(Controller->FocusTarget))||bEditLayout))DrawUnit(Controller->FocusTarget,TEXT("FOCUS / CLICK TO TARGET"),true);
    DrawBossFrames(Hero,Controller);DrawThreatMeter(Hero,Controller);
    DrawActionBars(Hero,Controller);DrawChat(Controller);DrawMeters(Hero,Controller);DrawPet(Hero,Controller);
    ResetTransform();CireShopUI::DrawHUDElements(*this,Hero,Controller,State); // progression-shop: bag bar, teleport, stats window
    if(!bModal&&!bSettings)DrawCombatText(Hero,Controller);
    ResetTransform();
    if(!bModal&&!bSettings){DrawControlEdge(Hero);DrawPlayerCastBar(Hero);DrawEffectCallouts(Hero);}
    ResetTransform();
    if(!bModal&&!bSettings)DrawAlert();
    if(!bSettings)DrawBanners();
    ResetTransform();
    if(!bModal&&!bSettings&&Controller)
    {
        const auto Aim=CireTargeting::Snapshot(Controller);
        if(Aim.bActive){const FString Text=ACireHero::SkillName(Aim.SkillId)+TEXT(" | ")+Aim.Message;const float W=FMath::Min(750.f,TextWidth(Text,12)+28);Frame((ViewW-W)/2,ViewH-248,W,31,Aim.bValid?Teal:Red);Label(Text,(ViewW-W)/2+14,ViewH-240,12,Aim.bValid?Parchment:Red);}
        if(Hero->Mobility){const float CD=Hero->Mobility->CooldownRemaining();const FString Move=FString::Printf(TEXT("[%s] JUMP   [%s] DODGE %s   [%s] %s"),*UISettings.Keybindings.Label(TEXT("Jump")).ToUpper(),*UISettings.Keybindings.Label(TEXT("DodgeRoll")).ToUpper(),CD>0?*FString::Printf(TEXT("%.1fs"),CD):Hero->Mobility->MaxRollCharges>1?*FString::Printf(TEXT("%d/%d"),Hero->Mobility->AvailableCharges(),Hero->Mobility->MaxRollCharges):TEXT("READY"),*UISettings.Keybindings.Label(TEXT("ToggleWalk")).ToUpper(),Hero->Mobility->bWalking?TEXT("WALK"):TEXT("RUN"));float HintY=ViewH-185;for(const TCHAR* BarId:{TEXT("Bar2"),TEXT("Bar3")})if(VisiblePanels.Contains(FName(BarId)))HintY=FMath::Min(HintY,PanelRect(FName(BarId)).Y-14);
            Label(Move,(ViewW-TextWidth(Move,9))/2,HintY,9,Hero->Mobility->IsInvulnerable()?Teal:Muted);}
    }
    if(!Hero->Notice.IsEmpty()&&!bModal) {
        const FString Notice=ShortName(Hero->Notice,88);
        Label(Notice,(ViewW-TextWidth(Notice,12))*.5f,ViewH-207,12,Gold);
    }
    if(Hero->bDead&&!bModal)Label(TEXT("FALLEN / Await your return"),ViewW*.5f-126,ViewH*.5f,18,Red);
    if(Controller&&Controller->bHelp&&!bModal&&!bEditLayout&&!bSettings) {
        Frame(ViewW*.5f-216,ViewH*.5f-108,432,186,Gold);Label(TEXT("BATTLEFIELD CONTROLS"),ViewW*.5f-198,ViewH*.5f-93,16,Parchment);
        const auto& B=UISettings.Keybindings;const auto L=[&B](const TCHAR* A){return B.Label(A);};
        const FString Rows[]={
            FString::Printf(TEXT("%s%s Move / %s%s Turn / %s%s Strafe / %s Jump / %s Dodge"),*L(TEXT("MoveForward")),*L(TEXT("MoveBackward")),*L(TEXT("TurnLeft")),*L(TEXT("TurnRight")),*L(TEXT("StrafeLeft")),*L(TEXT("StrafeRight")),*L(TEXT("Jump")),*L(TEXT("DodgeRoll"))),
            FString::Printf(TEXT("%s Self / %s Ally / %s Enemy / %s Attack"),*L(TEXT("TargetSelf")),*L(TEXT("TargetNextAlly")),*L(TEXT("TargetNextEnemy")),*L(TEXT("ToggleAutoAttack"))),
            FString::Printf(TEXT("%s-%s Skills / %s Ultimate / Click ground to place"),*L(*CireKeybindings::SlotAction(1,1).ToString()),*L(*CireKeybindings::SlotAction(1,6).ToString()),*L(*CireKeybindings::SlotAction(1,8).ToString())),
            FString::Printf(TEXT("%s Shop / %s Recall / %s Chat / %s Walk"),*L(TEXT("ToggleShop")),*L(TEXT("RecallToTown")),*L(TEXT("OpenChat")),*L(TEXT("ToggleWalk"))),
            FString::Printf(TEXT("%s Dev tools / %s Options / %s Layout / %s Help"),*L(TEXT("ToggleDeveloperTools")),*L(TEXT("ToggleOptions")),*L(TEXT("ToggleLayoutEditor")),*L(TEXT("ToggleHelp")))};
        for(int32 I=0;I<5;++I)Label(Rows[I],ViewW*.5f-198,ViewH*.5f-58+I*25,12,I==4?Gold:Muted);
    }
    if(bModal)DrawModal(Hero,Controller,State);
    ResetTransform();CireShopUI::DrawOverlay(*this,Hero,Controller); // progression-shop: purchase/loot toasts, teleport channel
    DrawSkillOfferExtras(Hero,Controller); // champion-draft: deferred-offer reminder + pick animation
    if(bEditLayout){VisiblePanels.AddUnique(TEXT("Tooltip"));VisiblePanels.AddUnique(TEXT("Threat"));VisiblePanels.AddUnique(TEXT("Boss"));}
    if(!bModal&&!bSettings&&!bEditLayout)UpdateHoverUnit(Hero);else HoverUnit.Reset();
    UpdateQuickKeybind();DrawQuickKeybind();
    DrawLayoutEditor();DrawDeveloperLauncher();DrawSettings();DrawDiagnostics();DrawTooltip();ResetTransform();
}

void ACireHUD::DrawModal(ACireHero* Hero,ACireController* Controller,ACireGameState* State)
{
    TooltipTitle.Reset();TooltipBody.Reset();
    ResetTransform(); const float W=ViewW,H=ViewH;
    const bool MatchFinished=State&&State->Phase==3;
    const bool DraftOpen=Hero&&!Hero->bDrafted&&!MatchFinished;
    const bool OfferOpen=Hero&&Hero->bDrafted&&Hero->Offers.Num()>0&&IsSkillOfferOpen()&&!MatchFinished;
    const bool ShopOpen=Controller&&Controller->bShop&&!DraftOpen&&!OfferOpen&&!MatchFinished;
    const bool ModalOpen=DraftOpen||OfferOpen||ShopOpen;
    auto Center=[&](const FString& Text,float Y,float Size,FLinearColor Color){Label(Text,(W-TextWidth(Text,Size))*.5f,Y,Size,Color);};
    auto Action=[&](int32 Type,int32 Value){if(Clicked&&Controller&&!bSettings&&!bEditLayout){Controller->ServerAction(Type,Value,nullptr);Clicked=false;}};
    if (ModalOpen)
    {
        if (!ShopOpen) Panel(0, 135, W, H - 330, FLinearColor(0.004f, 0.008f, 0.011f, .78f)); // progression-shop: the shop draws its own backdrop
        if (DraftOpen)
        {
            DrawDraftRoster(Hero,Controller);
        }
        else if (OfferOpen)
        {
            DrawSkillOffer(Hero,Controller); // champion-draft: CireSkillOfferHUD.cpp
            DrawDraftRoster(Hero,Controller); // champion-select: the LOCKED IN outro fades over the opening offer
        }
        else if (ShopOpen)
        {
            // progression-shop: League-style item shop (CireShopUI.cpp).
            CireShopUI::DrawShop(*this,Hero,Controller,State);
        }
    }

    if (State && State->Phase == 3)
    {
        Panel(W / 2 - 280, 231, 560, 208, Ink);
        Panel(W / 2 - 280, 231, 560, 3, Gold);
        Center(TEXT("THE BATTLE IS DECIDED"), 254, 26, Parchment);
        Center(State->Announcement, 304, 16, Gold);
        const float ButtonX = W / 2 - 110;
        const bool Over = Hit(ButtonX, 351, 220, 44);
        Panel(ButtonX, 351, 220, 44, Over ? Hover : Card);
        Panel(ButtonX, 351, 220, 2, Gold);
        Center(TEXT("PLAY AGAIN  >"), 363, 15, Over ? Parchment : Gold);
        Center(TEXT("Start a fresh match and forge a new build."), 410, 11, Muted);
        if (Over) Action(9, 0);
    }
}

