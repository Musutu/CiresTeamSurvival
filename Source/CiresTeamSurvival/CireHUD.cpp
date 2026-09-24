#include "CireHUD.h"
#include "CireKeybindings.h"
#include "CireLanePath.h"
#include "CireGame.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireChampionProfiles.h"
#include "CireTargeting.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "InputCoreTypes.h"

namespace
{
const FLinearColor Ink(.014f,.020f,.026f,.95f), Card(.034f,.046f,.055f,.98f), Hover(.075f,.106f,.116f,1.f);
const FLinearColor Gold(.77f,.61f,.34f,1.f), Parchment(.91f,.90f,.83f,1.f), Muted(.50f,.57f,.59f,1.f);
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
}

void ACireHUD::BeginPlay() { Super::BeginPlay(); UISettings.Load(); UISettings.bLayoutLocked=true; }
void ACireHUD::ResetTransform() { Origin=FVector2D::ZeroVector; Stretch=FVector2D(1,1); }
void ACireHUD::UsePanel(FName Id,float W,float H)
{
    const FCireUIRect R=UISettings.GetRect(Id,FVector2D(ViewW,ViewH));
    Origin=FVector2D(R.X,R.Y); Stretch=FVector2D(R.W/W,R.H/H);
    VisiblePanels.AddUnique(Id);
    static const TMap<FName,FString> Help={
        {TEXT("Player"),TEXT("Your character resources and attributes. Left click to target yourself. STR grants 25 health, INT grants 30 mana, AGI grants 1% attack speed per point. Your primary attribute also adds basic attack damage.")},
        {TEXT("Party"),TEXT("Your four teammates. Left click a frame to target for healing or support. The + button sets a focus target. Hover status icons for duration and removal rules.")},
        {TEXT("Match"),TEXT("Three cleared PvE waves lead to town preparation, arena PvP, then recovery. Each team begins with 100 lives. Normal leaks cost one life; bosses cost ten.")},
        {TEXT("Minimap"),TEXT("Your team's separate PvE lane and town entrance. The enemy realm stays obscured during PvE. Teams can fight only after teleporting to a shared arena.")},
        {TEXT("Skills"),TEXT("Basic attack, six learned active abilities, one passive and one ultimate. Empty slots do nothing. Hover a skill for its effect and cost.")},
        {TEXT("Chat"),TEXT("Player messages only. Party reaches teammates; Everyone reaches both teams. Enter opens chat. Hover this panel and scroll to read earlier messages.")},
        {TEXT("Meter"),TEXT("Effective team damage or healing. Overkill and excess healing do not increase these totals. Click Damage or Healing to change the ranking.")},
        {TEXT("CombatLog"),TEXT("Chronological confirmed combat events. Damage, healing and avoided attacks are separate from player chat.")},
        {TEXT("CombatText"),TEXT("Your incoming and outgoing combat feedback. F10 moves this panel. Orange critical strikes, gold outgoing damage, red incoming damage, green healing, and muted MISS/DODGE.")}};
    if(const FString* Description=Help.Find(Id))Tip(Id.ToString(),*Description,0,0,W,H);
}
void ACireHUD::Panel(float X,float Y,float W,float H,FLinearColor Color)
{
    DrawRect(Color,(Origin.X+X*Stretch.X)*Scale,(Origin.Y+Y*Stretch.Y)*Scale,W*Stretch.X*Scale,H*Stretch.Y*Scale);
}
void ACireHUD::Label(const FString& Text,float X,float Y,float Size,FLinearColor Color)
{
    DrawText(Text,Color,(Origin.X+X*Stretch.X)*Scale,(Origin.Y+Y*Stretch.Y)*Scale,GEngine?GEngine->GetMediumFont():nullptr,
        Size/16.f*Scale*FMath::Min(Stretch.X,Stretch.Y),false);
}
float ACireHUD::TextWidth(const FString& Text,float Size) const
{
    float W=0,H=0;
    GetTextSize(Text,W,H,GEngine?GEngine->GetMediumFont():nullptr,Size/16.f);
    return W;
}
void ACireHUD::Wrapped(const FString& Text,float X,float Y,float Width,float Size,FLinearColor Color,int32 MaxLines)
{
    TArray<FString> Words; Text.ParseIntoArrayWS(Words); FString Row; int32 Count=0;
    for(const FString& Word:Words) {
        const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
        if(!Row.IsEmpty()&&TextWidth(Next,Size)>Width) {
            Label(Row,X,Y+Count*(Size+4),Size,Color); if(++Count>=MaxLines)return; Row=Word;
        } else Row=Next;
    }
    if(!Row.IsEmpty()&&Count<MaxLines)Label(Row,X,Y+Count*(Size+4),Size,Color);
}
void ACireHUD::Line(float X1,float Y1,float X2,float Y2,FLinearColor Color,float Width)
{
    DrawLine((Origin.X+X1*Stretch.X)*Scale,(Origin.Y+Y1*Stretch.Y)*Scale,
        (Origin.X+X2*Stretch.X)*Scale,(Origin.Y+Y2*Stretch.Y)*Scale,Color,Width*Scale*FMath::Min(Stretch.X,Stretch.Y));
}
void ACireHUD::Bar(float X,float Y,float W,float H,float Value,FLinearColor Color)
{
    Panel(X,Y,W,H,FLinearColor(.004f,.008f,.012f,.96f));
    Value=FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,1.f):0.f;
    const float BW=FMath::Max(0.f,W-2)*Value;
    Panel(X+1,Y+1,BW,FMath::Max(0.f,H-2),Color);
    Panel(X+1,Y+1,BW,FMath::Max(1.f,H*.22f),FLinearColor(1,1,1,.11f));
}
void ACireHUD::Frame(float X,float Y,float W,float H,FLinearColor Accent)
{
    Panel(X+3,Y+4,W,H,FLinearColor(0,0,0,.30f)); Panel(X,Y,W,H,Ink);
    const FLinearColor Edge(.22f,.25f,.25f,.9f);
    Line(X,Y,X+W,Y,Accent,.85f); Line(X,Y+H,X+W,Y+H,Edge);
    Line(X,Y,X,Y+H,Edge); Line(X+W,Y,X+W,Y+H,Edge);
    Line(X+3,Y+3,X+14,Y+3,Accent); Line(X+3,Y+3,X+3,Y+12,Accent);
    Line(X+W-3,Y+3,X+W-14,Y+3,Accent); Line(X+W-3,Y+3,X+W-3,Y+12,Accent);
}
bool ACireHUD::Hit(float X,float Y,float W,float H) const
{
    return MX>=Origin.X+X*Stretch.X && MX<=Origin.X+(X+W)*Stretch.X &&
        MY>=Origin.Y+Y*Stretch.Y && MY<=Origin.Y+(Y+H)*Stretch.Y;
}
void ACireHUD::Icon(const FString& Id,float X,float Y,float S,FLinearColor Color)
{
    // Original geometric sigils: each silhouette remains distinct without licensed artwork.
    auto L=[&](float A,float B,float C,float D,float Weight=1.8f){Line(X+A*S,Y+B*S,X+C*S,Y+D*S,Color,Weight);};
    auto Ring=[&](float Radius){for(int32 I=0;I<24;++I){float A=I*PI/12,B=(I+1)*PI/12; L(.5f+FMath::Cos(A)*Radius,.5f+FMath::Sin(A)*Radius,.5f+FMath::Cos(B)*Radius,.5f+FMath::Sin(B)*Radius,.75f);}};
    if(Id.IsEmpty()) { L(.43f,.5f,.57f,.5f,.6f); L(.5f,.43f,.5f,.57f,.6f); return; }
    if(Id==TEXT("role4")||Id==TEXT("oathbound_guardian")||Id==TEXT("spectral_pack")) {
        Ring(.36f);Ring(.22f);L(.5f,.14f,.5f,.86f);L(.18f,.7f,.82f,.7f);L(.18f,.7f,.5f,.18f);L(.5f,.18f,.82f,.7f);
    } else if(Id==TEXT("venom_ground")||Id==TEXT("blight_sigil")||Id==TEXT("npc_blight_pool")) {
        Ring(.34f);for(int32 I=0;I<3;++I){const float A=I*2*PI/3;L(.5f,.5f,.5f+FMath::Cos(A)*.28f,.5f+FMath::Sin(A)*.28f,3);}Ring(.10f);
    } else if(Id==TEXT("runic_wall")) {
        L(.17f,.78f,.83f,.78f);L(.17f,.78f,.17f,.25f);L(.83f,.78f,.83f,.25f);L(.17f,.25f,.83f,.25f);L(.17f,.51f,.83f,.51f);L(.5f,.25f,.5f,.51f);L(.33f,.51f,.33f,.78f);L(.67f,.51f,.67f,.78f);
    } else if(Id==TEXT("bastion_of_dawn")) {
        Ring(.39f);L(.25f,.75f,.25f,.32f);L(.25f,.32f,.38f,.32f);L(.38f,.32f,.38f,.21f);L(.38f,.21f,.61f,.21f);L(.61f,.21f,.61f,.32f);L(.61f,.32f,.75f,.32f);L(.75f,.32f,.75f,.75f);L(.25f,.75f,.75f,.75f);L(.43f,.75f,.43f,.53f);L(.43f,.53f,.57f,.53f);L(.57f,.53f,.57f,.75f);
    } else if(Id==TEXT("cataclysm")) {
        L(.21f,.79f,.57f,.43f,3);L(.57f,.43f,.8f,.19f,3);L(.38f,.44f,.66f,.13f);L(.63f,.65f,.90f,.35f);L(.19f,.61f,.19f,.8f);L(.19f,.8f,.4f,.8f);L(.13f,.88f,.5f,.88f);L(.45f,.71f,.58f,.77f);L(.45f,.71f,.40f,.61f);
    } else if(Id==TEXT("executioners_verdict")) {
        L(.2f,.82f,.74f,.26f,3);L(.8f,.82f,.26f,.26f,3);L(.64f,.15f,.83f,.14f,3);L(.83f,.14f,.87f,.33f,3);L(.87f,.33f,.64f,.39f,3);L(.36f,.15f,.17f,.14f,3);L(.17f,.14f,.13f,.33f,3);L(.13f,.33f,.36f,.39f,3);
    } else if(Id==TEXT("renewal")) {
        Ring(.16f);for(int32 I=0;I<8;++I){float A=I*PI/4,B=A+PI/8;L(.5f+FMath::Cos(A)*.17f,.5f+FMath::Sin(A)*.17f,.5f+FMath::Cos(B)*.4f,.5f+FMath::Sin(B)*.4f);L(.5f+FMath::Cos(B)*.4f,.5f+FMath::Sin(B)*.4f,.5f+FMath::Cos(A+PI/4)*.17f,.5f+FMath::Sin(A+PI/4)*.17f);}
    } else if(Id==TEXT("iron_guard")||Id==TEXT("shield_slam")||Id==TEXT("stone_skin")||Id==TEXT("role0")) {
        L(.24f,.24f,.5f,.15f);L(.5f,.15f,.76f,.24f);L(.76f,.24f,.71f,.61f);L(.71f,.61f,.5f,.84f);L(.5f,.84f,.29f,.61f);L(.29f,.61f,.24f,.24f);
        L(.5f,.27f,.5f,.66f);L(.36f,.43f,.64f,.43f);
        if(Id==TEXT("shield_slam")){L(.77f,.12f,.91f,.08f);L(.82f,.31f,.96f,.33f);}
    } else if(Id==TEXT("restoring_light")||Id==TEXT("purify")||Id==TEXT("soul_conduit")||Id==TEXT("role2")) {
        Ring(.33f); L(.5f,.22f,.5f,.78f,3);L(.22f,.5f,.78f,.5f,3);L(.32f,.32f,.68f,.68f,.7f);L(.68f,.32f,.32f,.68f,.7f);
    } else if(Id==TEXT("frost_bind")) {
        for(int32 I=0;I<6;++I){float A=I*PI/3;float DX=FMath::Cos(A),DY=FMath::Sin(A);L(.5f,.5f,.5f+DX*.37f,.5f+DY*.37f);L(.5f+DX*.24f,.5f+DY*.24f,.5f+DX*.22f-DY*.12f,.5f+DY*.22f+DX*.12f);}
    } else if(Id==TEXT("chain_spark")||Id==TEXT("deep_reserves")) {
        L(.62f,.12f,.29f,.52f,3);L(.29f,.52f,.61f,.46f,3);L(.61f,.46f,.39f,.87f,3);
        if(Id==TEXT("chain_spark")){L(.77f,.32f,.88f,.41f);L(.2f,.65f,.1f,.77f);}
    } else if(Id==TEXT("ember_lance")) {
        L(.48f,.12f,.28f,.43f,2);L(.28f,.43f,.22f,.67f,2);L(.22f,.67f,.47f,.87f,2);L(.47f,.87f,.77f,.63f,2);L(.77f,.63f,.69f,.30f,2);L(.69f,.30f,.56f,.52f,2);L(.56f,.52f,.48f,.12f,2);L(.47f,.57f,.43f,.78f,2);
    } else if(Id==TEXT("sanctuary")) {
        Ring(.34f);Ring(.23f);L(.19f,.77f,.81f,.77f);L(.5f,.15f,.5f,.64f);L(.28f,.39f,.72f,.39f);
    } else if(Id==TEXT("war_cry")) {
        L(.24f,.34f,.7f,.16f);L(.7f,.16f,.7f,.69f);L(.7f,.69f,.24f,.55f);L(.24f,.55f,.24f,.34f);L(.35f,.59f,.43f,.84f);L(.78f,.2f,.9f,.1f);L(.8f,.44f,.94f,.44f);L(.78f,.67f,.89f,.77f);
    } else if(Id==TEXT("piercing_shot")||Id==TEXT("role1")) {
        L(.23f,.77f,.78f,.22f,2.5f);L(.55f,.2f,.8f,.2f);L(.8f,.2f,.8f,.46f);L(.24f,.60f,.24f,.77f);L(.24f,.77f,.41f,.77f);L(.34f,.69f,.34f,.51f);
    } else if(Id==TEXT("shadow_step")) {
        L(.60f,.13f,.32f,.31f);L(.32f,.31f,.25f,.62f);L(.25f,.62f,.47f,.84f);L(.47f,.84f,.68f,.7f);L(.68f,.7f,.46f,.65f);L(.46f,.65f,.46f,.4f);L(.46f,.4f,.6f,.13f);L(.7f,.4f,.88f,.4f);L(.73f,.52f,.91f,.52f);
    } else if(Id==TEXT("battle_rhythm")) {
        Ring(.33f);L(.24f,.50f,.38f,.50f);L(.38f,.5f,.45f,.3f);L(.45f,.3f,.56f,.72f);L(.56f,.72f,.63f,.5f);L(.63f,.5f,.79f,.5f);
    } else {
        L(.23f,.8f,.7f,.26f,3);L(.7f,.26f,.85f,.15f,3);L(.85f,.15f,.77f,.38f,2);L(.77f,.38f,.3f,.84f,2);L(.22f,.6f,.46f,.84f,3);L(.18f,.85f,.26f,.93f,3);
        if(Id==TEXT("cleaving_strike")){L(.15f,.43f,.28f,.24f);L(.28f,.24f,.53f,.13f);}
    }
}

bool ACireHUD::IsPointerOverInterface() const
{
    if(bModal||bSettings||bEditLayout)return true;
    float CursorX=0,CursorY=0;
    if(!PlayerOwner||!PlayerOwner->GetMousePosition(CursorX,CursorY))return true;
    CursorX/=Scale;CursorY/=Scale;
    if(CireDeveloperTools::CanEdit(GetWorld())){const auto R=DeveloperLauncherRect();if(CursorX>=R.X&&CursorX<=R.X+R.W&&CursorY>=R.Y&&CursorY<=R.Y+R.H)return true;}
    for(FName Id:VisiblePanels) {
        if(Id==TEXT("CombatText")||Id==TEXT("Tooltip"))continue;
        const auto R=UISettings.GetRect(Id,FVector2D(ViewW,ViewH));
        if(CursorX>=R.X&&CursorX<=R.X+R.W&&CursorY>=R.Y&&CursorY<=R.Y+R.H)return true;
    }
    return false;
}
void ACireHUD::ToggleLayoutEditor() { RevertVideoPreview();bEditLayout=!bEditLayout; bSettings=false;UISettings.bLayoutLocked=!bEditLayout; if(!bEditLayout){DragPanel=NAME_None;UISettings.Save();} }
void ACireHUD::ToggleSettings() { if(bSettings)RevertVideoPreview();bSettings=!bSettings;bVideoLoaded=false;if(bEditLayout){bEditLayout=false;UISettings.bLayoutLocked=true;UISettings.Save();} }
void ACireHUD::ToggleDeveloperTools()
{
    if(!CireDeveloperTools::CanEdit(GetWorld()))return;
    if(bSettings&&OptionsTab==5){ToggleSettings();return;}
    if(bEditLayout)ToggleLayoutEditor();
    RevertVideoPreview();bSettings=true;OptionsTab=5;DeveloperPage=5;bVideoLoaded=false;
}
bool ACireHUD::HandleEscape() { if(bSettings){RevertVideoPreview();bSettings=false;UISettings.Save();return true;}if(bEditLayout){ToggleLayoutEditor();return true;}return false; }
void ACireHUD::HandleMouseWheel(float Delta)
{
    const auto R=UISettings.GetRect(TEXT("Chat"),FVector2D(ViewW,ViewH));
    if(MX>=R.X&&MX<=R.X+R.W&&MY>=R.Y&&MY<=R.Y+R.H)ChatScroll=FMath::Clamp(ChatScroll+(Delta>0?2:-2),0,100);
}
void ACireHUD::LayoutInteraction()
{
    if(!bEditLayout)return;
    const bool Down=PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
    if(Clicked&&DragPanel.IsNone()) {
        for(int32 I=VisiblePanels.Num()-1;I>=0;--I) {
            const FName Id=VisiblePanels[I]; const auto R=UISettings.GetRect(Id,FVector2D(ViewW,ViewH));
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
        const auto R=UISettings.GetRect(Id,FVector2D(ViewW,ViewH));
        const bool Locked=UISettings.IsPanelLocked(Id);
        Panel(R.X,R.Y,R.W,R.H,FLinearColor(.10f,.39f,.40f,.15f));
        Line(R.X,R.Y,R.X+R.W,R.Y,Locked?Muted:Teal,2);
        Line(R.X,R.Y+R.H,R.X+R.W,R.Y+R.H,Locked?Muted:Teal,2);
        Panel(R.X,R.Y, FMath::Min(R.W-24,150.f),20,Ink);Label(Id.ToString(),R.X+6,R.Y+3,10,Parchment);
        Panel(R.X+R.W-24,R.Y,24,20,Locked?Red:Card);Label(Locked?TEXT("L"):TEXT("U"),R.X+R.W-17,R.Y+3,11,Gold);
        Line(R.X+R.W-13,R.Y+R.H-3,R.X+R.W-3,R.Y+R.H-13,Teal,2);
        Line(R.X+R.W-8,R.Y+R.H-3,R.X+R.W-3,R.Y+R.H-8,Teal,2);
    }
    const float X=ViewW*.5f-245,Y=ViewH*.5f-29;
    Frame(X,Y,490,58,Teal);Label(TEXT("EDIT LAYOUT  /  DRAG PANELS  /  RESIZE AT LOWER RIGHT"),X+13,Y+10,11,Parchment);
    Label(TEXT("U / L locks a panel.   F10 or Escape saves and locks your layout."),X+13,Y+33,10,Muted);
    Clicked=false;
}

void ACireHUD::DrawPlayer(ACireHero* Hero)
{
    UsePanel(TEXT("Player"),260,132); const FLinearColor Accent=RoleColor(Hero->Archetype);
    Frame(0,0,260,132,Accent);Frame(8,9,49,59,Accent);Icon(FString::Printf(TEXT("role%d"),Hero->Archetype),10,13,44,Accent);
    Panel(19,63,28,18,Card);Label(FString::FromInt(Hero->Level),26,64,13,Gold);
    const int32 Poisoned=PoisonCount(Hero);
    Label(ShortName(Hero->HeroName,Poisoned>0?12:23),66,8,14,Parchment);
    if(Poisoned>0){Panel(165,8,84,17,Card);Label(PoisonLabel(Poisoned),170,10,9,Poison);}
    Bar(66,31,182,20,Fraction(Hero->Health,Hero->MaxHealth),Red);
    Label(FString::Printf(TEXT("%.0f / %.0f"),Hero->Health,Hero->MaxHealth),73,33,12,Parchment);
    Bar(66,55,182,13,Fraction(Hero->Mana,Hero->MaxMana),Blue);
    Label(FString::Printf(TEXT("%.0f / %.0f"),Hero->Mana,Hero->MaxMana),73,55,10,Parchment);
    Bar(66,72,182,5,Hero->Energy/100.f,Gold);
    Label(FString::Printf(TEXT("STR %d  AGI %d  INT %d"),Hero->Strength,Hero->Agility,Hero->Intelligence),10,116,10,Muted);
    Label(FString::Printf(TEXT("EN %.0f"),Hero->Energy),210,116,10,Gold);
    Tip(TEXT("Health"),TEXT("Your remaining life. At zero you fall. Guard effects and tank threat management reduce pressure on the team."),66,31,182,20);
    Tip(TEXT("Mana"),TEXT("Resource for spells. Each INT grants 30 maximum mana."),66,55,182,13);
    Tip(TEXT("Energy"),TEXT("Regenerating resource used by physical abilities. It is separate from mana."),66,70,182,9);
    DrawStatuses(Hero,66,83,24,5);
    if(Clicked&&Hit(0,0,260,132)&&!bModal&&!bSettings&&!bEditLayout){if(auto* C=Cast<ACireController>(PlayerOwner))C->ServerAction(0,0,Hero);Clicked=false;}
}
void ACireHUD::DrawParty(ACireHero* Hero,ACireController* Controller)
{
    UsePanel(TEXT("Party"),250,248);
    Label(Hero->TeamId==0?TEXT("EMBER COMPANY"):TEXT("DUSK COMPANY"),1,0,10,Gold);
    Label(TEXT("PARTY / 5"),183,0,9,Muted);
    TArray<ACireHero*> Allies;
    for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(*It!=Hero&&It->TeamId==Hero->TeamId&&!Cast<ACireSummon>(*It))Allies.Add(*It);
    Allies.Sort([](const ACireHero& A,const ACireHero& B){return A.GetName()<B.GetName();});
    for(int32 I=0;I<FMath::Min(Allies.Num(),4);++I) {
        ACireHero* Ally=Allies[I]; const float Y=20+I*56;
        const bool Selected=Hero->Target==Ally, Focus=Controller&&Controller->FocusTarget==Ally;
        const bool Over=Hit(0,Y,250,50)&&!bModal&&!bEditLayout&&!bSettings;
        Frame(0,Y,250,50,Selected?Parchment:RoleColor(Ally->Archetype)*.65f);
        if(Over)Panel(1,Y+1,248,48,FLinearColor(.3f,.45f,.48f,.12f));
        Icon(FString::Printf(TEXT("role%d"),Ally->Archetype),4,Y+10,29,Ally->bDead?Muted:RoleColor(Ally->Archetype));
        Label(ShortName(Ally->HeroName,22),39,Y+4,11,Ally->bDead?Muted:Parchment);
        Label(FString::FromInt(Ally->Level),183,Y+4,10,Gold);
        Bar(39,Y+21,161,13,Fraction(Ally->Health,Ally->MaxHealth),Ally->bDead?Muted:Red);
        Label(Ally->bDead?TEXT("FALLEN"):FString::Printf(TEXT("%.0f%%"),Fraction(Ally->Health,Ally->MaxHealth)*100),42,Y+20,9,Parchment);
        const int32 Poisoned=PoisonCount(Ally);
        Bar(39,Y+37,Poisoned>0?66:161,5,Fraction(Ally->Mana,Ally->MaxMana),Blue);
        if(Poisoned>0)Label(PoisonLabel(Poisoned),112,Y+35,8,Poison);
        Panel(226,Y+3,20,17,Focus?Gold:Card);Label(Focus?TEXT("*"):TEXT("+"),231,Y+2,13,Focus?Ink:Gold);
        Tip(Ally->HeroName,FString::Printf(TEXT("Level %d. Health %.0f / %.0f. Mana %.0f / %.0f. Left click to target this teammate."),Ally->Level,Ally->Health,Ally->MaxHealth,Ally->Mana,Ally->MaxMana),0,Y,222,49);
        Tip(TEXT("Focus teammate"),TEXT("Keep a second persistent unit frame for this teammate. Click the focus frame to make them your current target."),226,Y+3,20,17);
        DrawStatuses(Ally,205,Y+24,17,2);
        if(Over&&Clicked&&Controller) {
            if(Hit(222,Y,28,23))Controller->SetFocusTarget(Focus?nullptr:Ally);
            else Controller->ServerAction(0,0,Ally);
            Clicked=false;
        }
    }
}
void ACireHUD::DrawUnit(AActor* Actor,const FString& Caption,bool bFocus)
{
    if(!IsValid(Actor)&&!bEditLayout)return;
    FString Name=TEXT("No focus selected");float HP=0,MaxHP=1,MP=0,MaxMP=0;int32 Level=0;FLinearColor Color=Red;
    const auto* Self=Cast<ACireHero>(PlayerOwner->GetPawn());
    if(const auto* Hero=Cast<ACireHero>(Actor)) {
        Name=Hero->HeroName;HP=Hero->Health;MaxHP=Hero->MaxHealth;MP=Hero->Mana;MaxMP=Hero->MaxMana;Level=Hero->Level;
        Color=Self&&Hero->TeamId==Self->TeamId?Teal:Red;
    } else if(const auto* Mob=Cast<ACireMonster>(Actor)) {Name=Mob->MonsterName;HP=Mob->Health;MaxHP=Mob->MaxHealth;Color=Mob->PackId>=0?Purple:Red;}
    else if(const auto* Object=Cast<ACireConstruct>(Actor)){Name=Object->GetDisplayName();HP=Object->Health;MaxHP=Object->MaxHealth;Color=Self&&Object->OriginTeam==Self->TeamId?Teal:Red;}
    const float W=bFocus?218:300,H=bFocus?123:140;
    UsePanel(bFocus?TEXT("Focus"):TEXT("Target"),W,H);Frame(0,0,W,H,bFocus?Gold:Color);
    const auto* Boss=Cast<ACireMonster>(Actor);
    const int32 Poisoned=PoisonCount(Actor);
    const auto* SelectedHero=Cast<ACireHero>(Actor);const auto* SelectedObject=Cast<ACireConstruct>(Actor);
    const bool Friendly=Self&&((SelectedHero&&SelectedHero->TeamId==Self->TeamId)||(SelectedObject&&SelectedObject->OriginTeam==Self->TeamId));
    FString Relationship=Actor==Self?TEXT("SELF"):Friendly?TEXT("ALLY"):TEXT("ENEMY");
    float AttackRange=Cast<ACireHero>(Actor)?Cast<ACireHero>(Actor)->BasicAttackRange():Boss?(Boss->CombatArchetype>=2?650.f:170.f):0;
    const FString Heading=Boss&&Boss->bBoss?TEXT("BOSS / 10 LIVES AT RISK"):bFocus?Caption:Relationship+(SelectedObject?TEXT(" / CONSTRUCT"):FString::Printf(TEXT("  /  ATTACK RANGE %.1fm"),AttackRange/100));
    Label(Poisoned>0?ShortName(Heading,bFocus?14:26):Heading,10,5,9,Boss&&Boss->bBoss?Gold:Muted);
    if(Poisoned>0)Label(PoisonLabel(Poisoned),W-91,5,9,Poison);
    Label(ShortName(Name,bFocus?23:29),10,21,bFocus?12:15,Color);
    if(Level>0)Label(FString::Printf(TEXT("%d"),Level),W-27,20,11,Gold);
    Bar(10,bFocus?42:45,W-20,bFocus?11:16,Fraction(HP,MaxHP),Red);
    Label(FString::Printf(TEXT("%.0f / %.0f"),HP,MaxHP),15,bFocus?41:46,bFocus?9:10,Parchment);
    if(!bFocus&&MaxMP>0)Bar(10,64,W-20,4,Fraction(MP,MaxMP),Blue);
    Tip(Name,FString::Printf(TEXT("Health %.0f / %.0f. "),HP,MaxHP)+(Cast<ACireConstruct>(Actor)?TEXT("A destructible summoned object. Its health and collision are authoritative; damaging it can reopen a blocked route."):TEXT("Your selected unit. Targeting allies enables support abilities; hostile targeting enables attacks. Hover status icons for details.")),0,0,W,H);
    DrawStatuses(Actor,10,bFocus?60:73,20,bFocus?5:7);
    if(Boss)
    {
        const float Now=GetWorld()->GetGameState<ACireGameState>()?GetWorld()->GetGameState<ACireGameState>()->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
        const FString Focus=IsValid(Boss->Victim)?Boss->Victim==Self?TEXT("YOU HAVE AGGRO"):TEXT("Attacking ")+ShortName(Boss->Victim->HeroName,18):TEXT("Advancing toward town");
        Label(Focus,10,H-16,9,Boss->Victim==Self?Red:Muted);
        if(Boss->CastEndsAt>Now&&!Boss->CastingAbility.IsEmpty())
        {
            Bar(10,H-36,W-20,14,1-Fraction(Boss->CastEndsAt-Now,Boss->CastEndsAt-Boss->CastStartedAt),Purple);
            Label(ShortName(ACireHero::SkillName(Boss->CastingAbility),27),14,H-35,9,Parchment);
            Tip(TEXT("Enemy cast"),Boss->CastingAbility+TEXT(" is being cast. Reposition away from its ground warning or projectile path before release."),10,H-36,W-20,14);
        }
    }
    if(bFocus&&Clicked&&Hit(0,0,W,H)&&!bEditLayout&&!bModal&&!bSettings) {
        if(auto* C=Cast<ACireController>(PlayerOwner))C->ServerAction(0,0,Actor);Clicked=false;
    }
}
void ACireHUD::DrawMatch(ACireGameState* State)
{
    if(!State)return;UsePanel(TEXT("Match"),320,74);Frame(0,0,320,74,Gold);
    const TCHAR* Phases[]={TEXT("SURVIVAL"),TEXT("TOWN PREPARATION"),TEXT("PORTAL ARENA"),TEXT("MATCH COMPLETE"),TEXT("RECOVERY")};
    const FString Phase=Phases[FMath::Clamp(State->Phase,0,4)];
    Label(Phase,(320-TextWidth(Phase,10))/2,8,10,State->Phase==2?Red:Teal);
    const int32 Seconds=FMath::Max(0,FMath::CeilToInt(State->SecondsLeft));
    const FString Time=State->Phase==0?FString::Printf(TEXT("%d / %d"),State->CycleWavesDone,State->WavesPerCycle):FString::Printf(TEXT("%02d:%02d"),Seconds/60,Seconds%60);
    Label(Time,(320-TextWidth(Time,25))/2,26,25,Parchment);
    Label(FString::Printf(TEXT("%02d"),State->EmberLives),18,23,24,Gold);
    Label(TEXT("EMBER"),17,52,9,Muted);Label(FString::Printf(TEXT("%02d"),State->DuskLives),269,23,24,Blue);Label(TEXT("DUSK"),269,52,9,Muted);
    Label(State->Phase==0?FString::Printf(TEXT("WAVE %d / CYCLE CLEARS"),State->Wave):FString::Printf(TEXT("ROUND %d   /   WAVE %d"),State->Round,State->Wave),88,59,9,Muted);
    if(State->Phase==0&&State->NextWaveSeconds>.05f) {
        const FString Next=FString::Printf(TEXT("NEXT WAVE IN %ds"),FMath::CeilToInt(State->NextWaveSeconds));
        Label(Next,(320-TextWidth(Next,9))/2,80,9,Gold);
    }
}
void ACireHUD::DrawMinimap(ACireHero* Hero,ACireGameState* State)
{
    UsePanel(TEXT("Minimap"),220,178);Frame(0,0,220,178,Gold);
    const bool Arena=State&&State->Phase==2;
    Label(Arena?TEXT("PORTAL BATTLEFIELD"):TEXT("THE TWIN CITADELS"),12,9,10,Parchment);
    Label(TEXT("N"),204,9,9,Gold);Panel(9,28,202,125,FLinearColor(.032f,.044f,.039f,1));
    auto Map=[&](FVector P,int32 Team)->FVector2D {
        if(Arena) {
            const float ArenaY=10000+(State?State->ArenaIndex:0)*6000;
            return FVector2D(16+FMath::Clamp((P.Y-ArenaY+1600)/3200.f,0.f,1.f)*188,145-FMath::Clamp((P.X+1700)/3400.f,0.f,1.f)*109);
        }
        const auto& R=CireLanePath::Get(GetWorld());
        const float CenterY=CireLanePath::CenterY(Team);
        return FVector2D(Team*101+13+FMath::Clamp((P.Y-CenterY+R.HalfWidth)/(2*R.HalfWidth),0.f,1.f)*93,
            145-FMath::Clamp((P.X-R.MinX)/(R.MaxX-R.MinX),0.f,1.f)*111);
    };
    if(Arena) {
        Line(18,37,202,37,Muted);Line(18,145,202,145,Muted);Line(18,37,18,145,Muted);Line(202,37,202,145,Muted);
        for(int32 I=0;I<24;++I) {float A=I*PI/12,B=(I+1)*PI/12;Line(110+FMath::Cos(A)*29,91+FMath::Sin(A)*19,110+FMath::Cos(B)*29,91+FMath::Sin(B)*19,Gold*.6f);}
        Line(20,120,200,120,Teal*.5f);Line(20,62,200,62,Red*.5f);
    } else {
        for(int32 Team=0;Team<2;++Team) {
            const float X=Team*101+12;const bool Visible=Team==Hero->TeamId;
            Panel(X,31,94,119,FLinearColor(.025f,.037f,.036f,1));
            if(!Visible) {Panel(X+2,33,90,115,FLinearColor(.01f,.017f,.023f,.93f));Label(TEXT("ENEMY"),X+26,73,10,Muted);Label(TEXT("REALM"),X+26,87,10,Muted);Label(TEXT("OBSCURED"),X+16,111,8,Muted);continue;}
            const auto& Points=CireLanePath::Get(GetWorld()).LocalPoints[Team];
            for(int32 I=1;I<Points.Num();++I) {
                const auto A=Map(FVector(Points[I-1].X,Points[I-1].Y+CireLanePath::CenterY(Team),0),Team);
                const auto B=Map(FVector(Points[I].X,Points[I].Y+CireLanePath::CenterY(Team),0),Team);
                Line(A.X,A.Y,B.X,B.Y,FLinearColor(.24f,.27f,.23f,1),5);
                Line(A.X,A.Y,B.X,B.Y,Gold*.7f,1);
            }
            const auto Spawn=Map(CireLanePath::SpawnPosition(GetWorld(),Team),Team);
            Panel(Spawn.X-9,Spawn.Y-3,18,6,Red);Label(TEXT("BREACH"),X+25,30,8,Muted);
            const auto Base=Map(FVector(-1850,CireLanePath::CenterY(Team),0),Team);
            Panel(Base.X-12,Base.Y-3,24,6,Teal);Label(TEXT("KEEP"),X+32,139,8,Teal);
            for(int32 Tier=1;Tier<=3;++Tier) {
                const auto P=Map(CireLanePath::ChallengePosition(GetWorld(),Team,Tier),Team);
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
    for(TActorIterator<ACireHero> It(GetWorld());It;++It) {
        if(It->bDead||(!Arena&&It->TeamId!=Hero->TeamId))continue;
        const auto P=Map(It->GetActorLocation(),It->TeamId);const bool Self=*It==Hero;
        const FLinearColor C=Self?Parchment:It->TeamId==Hero->TeamId?Teal:Red;
        Panel(P.X-2,P.Y-2,Self?5:4,Self?5:4,C);
        if(Self){Line(P.X,P.Y-6,P.X-4,P.Y+2,Gold,1.5f);Line(P.X,P.Y-6,P.X+4,P.Y+2,Gold,1.5f);}
    }
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
    Panel(179,5,52,19,Team?Hover:Card);Label(TEXT("PARTY"),187,8,9,Team?Teal:Muted);
    Panel(236,5,58,19,!Team?Hover:Card);Label(TEXT("EVERYONE"),240,8,8,!Team?Gold:Muted);
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
        Panel(8,141,290,24,Controller->bChatInput?Hover:Card);
        const FString Draft=Controller->bChatInput?Controller->ChatDraft.Right(38)+TEXT("|"):TEXT("Press Enter to chat...");
        Label(Controller->bChatTeamOnly?TEXT("P"):TEXT("ALL"),13,147,9,Team?Teal:Gold);
        Label(Draft,40,146,10,Controller->bChatInput?Parchment:Muted);
        if(Clicked&&Interactive&&Hit(8,141,290,24)){Controller->BeginChat();Clicked=false;}
    }
}
void ACireHUD::DrawMeters(ACireHero* Hero,ACireController* Controller)
{
    if(UISettings.bShowMeter||bEditLayout) {
        UsePanel(TEXT("Meter"),304,174);Frame(0,0,304,174,Gold*.65f);
        const bool Heal=UISettings.MeterMode==1,Interactive=!bModal&&!bSettings&&!bEditLayout;
        Label(Heal?TEXT("HEALING DONE"):TEXT("DAMAGE DONE"),12,10,11,Parchment);
        Label(TEXT("TOTAL / MATCH"),198,12,8,Muted);
        Panel(12,30,75,18,Heal?Card:Hover);Label(TEXT("DAMAGE"),23,32,9,Heal?Muted:Gold);
        Panel(93,30,77,18,Heal?Hover:Card);Label(TEXT("HEALING"),104,32,9,Heal?Teal:Muted);
        if(Clicked&&Interactive&&Hit(12,30,159,19)){UISettings.MeterMode=Hit(12,30,75,19)?0:1;UISettings.Save();Clicked=false;}
        TArray<ACireHero*> Party;for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(It->TeamId==Hero->TeamId)Party.Add(*It);
        Party.Sort([Heal](const ACireHero& A,const ACireHero& B){return Heal?A.HealingDone>B.HealingDone:A.DamageDone>B.DamageDone;});
        float Total=0,Max=1;for(auto* H:Party){float V=Heal?H->HealingDone:H->DamageDone;Total+=V;Max=FMath::Max(Max,V);}
        for(int32 I=0;I<FMath::Min(Party.Num(),5);++I) {
            auto* H=Party[I];const float Value=Heal?H->HealingDone:H->DamageDone,Y=54+I*22;
            Bar(12,Y,280,18,Value/Max,(Heal?Teal:RoleColor(H->Archetype))*.4f);
            Label(FString::Printf(TEXT("%d  %s"),I+1,*ShortName(H==Hero?TEXT("You"):H->HeroName,20)),18,Y+2,10,H==Hero?Parchment:Muted);
            const FString Amount=FString::Printf(TEXT("%.0f  %.0f%%"),Value,Fraction(Value,Total)*100);
            Label(Amount,287-TextWidth(Amount,9),Y+3,9,Parchment);
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
                    E.Outcome==ECireHitOutcome::Miss?TEXT("MISS"):TEXT("DODGE");
                const FString Desc=FString::Printf(TEXT("%s > %s  %s"),*ShortName(E.SourceName,12),*ShortName(E.TargetName,12),*Result);
                Label(ShortName(Desc,48),12,Y,9,E.bHealing?Teal:Muted);
                Label(ShortName(E.AbilityName,48),12,Y+12,8,Muted*.8f);
            }
            if(Controller->CombatEvents.Num()==0)Label(TEXT("Combat events will appear as they happen."),12,38,10,Muted);
        }
    }
}
void ACireHUD::DrawCombatText(ACireHero* Hero,ACireController* Controller)
{
    if(!UISettings.bCombatTextEnabled||!Controller||!Hero)return;
    UsePanel(TEXT("CombatText"),430,220);
    const FVector2D AnchoredOrigin=Origin,AnchoredStretch=Stretch;
    const float Now=GetWorld()->GetTimeSeconds();
    auto Eligible=[&](const FCireCombatEvent& E) {
        if(!E.bLocalTarget&&!E.bLocalSource)return false;
        if(!(E.bLocalTarget&&UISettings.bShowIncoming)&&!(E.bLocalSource&&UISettings.bShowOutgoing))return false;
        return E.bHealing?UISettings.bShowHealing:UISettings.bShowDamage;
    };
    auto ColorFor=[](const FCireCombatEvent& E) {
        return E.Outcome!=ECireHitOutcome::Hit?FLinearColor(.76f,.82f,.9f,1):E.bCritical?FLinearColor(1.f,.53f,.08f,1):E.bHealing?FLinearColor(.35f,1.f,.62f,1):
            E.bLocalTarget?FLinearColor(1.f,.30f,.28f,1):FLinearColor(1.f,.84f,.42f,1);
    };
    auto NumberFor=[](const FCireCombatEvent& E) {
        if(E.Outcome!=ECireHitOutcome::Hit)return FString(E.Outcome==ECireHitOutcome::Miss?TEXT("MISS"):TEXT("DODGE"));
        return FString::Printf(TEXT("%s%.0f"),E.bHealing?TEXT("+"):E.bLocalTarget?TEXT("-"):TEXT(""),E.Amount);
    };
    auto Outlined=[&](const FString& Text,float X,float Y,float Size,FLinearColor Color) {
        const FLinearColor Shadow(0,0,0,Color.A*.95f);
        Label(Text,X-1,Y,Size,Shadow);Label(Text,X+1,Y,Size,Shadow);
        Label(Text,X,Y-1,Size,Shadow);Label(Text,X,Y+2,Size,Shadow);
        Label(Text,X,Y,Size,Color);
    };

    // The impact position and personal attribution are snapshots. A lethal hit
    // remains visible even after the affected actor has left the client world.
    TArray<FBox2D> Occupied;
    TArray<FBox2D> ProtectedPanels;
    for(FName Id:VisiblePanels)if(Id!=TEXT("CombatText")&&Id!=TEXT("Tooltip"))
    {
        const auto R=UISettings.GetRect(Id,FVector2D(ViewW,ViewH));
        ProtectedPanels.Emplace(FVector2D(R.X-5,R.Y-5),FVector2D(R.X+R.W+5,R.Y+R.H+5));
    }
    ResetTransform();
    for(int32 I=Controller->CombatEvents.Num()-1;UISettings.bShowFloatingNumbers&&I>=0&&Occupied.Num()<32;--I) {
        const auto& E=Controller->CombatEvents[I];const float Age=Now-E.TimeSeconds;
        if(Age<0||Age>2.1f||!Eligible(E))continue;
        FVector2D Screen;
        if(!PlayerOwner->ProjectWorldLocationToScreen(E.Location,Screen,false))continue;
        const float Pop=1.f+.22f*FMath::Clamp(1.f-Age/.16f,0.f,1.f);
        const float Font=UISettings.WorldNumberFontSize*Pop*(E.bCritical?1.3f:1.f);
        const FString Amount=NumberFor(E);const float W=TextWidth(Amount,Font),H=Font+3;
        const float Drift=static_cast<int32>(E.Sequence%3)-1;
        float X=Screen.X/Scale-W*.5f+Drift*(8.f+Age*9.f);
        float Y=Screen.Y/Scale-H-10.f-Age*38.f;
        if(X+W<0||X>ViewW||Y+H<0||Y>ViewH)continue;
        X=FMath::Clamp(X,3.f,FMath::Max(3.f,ViewW-W-3.f));
        // Keep floating numbers near their target while avoiding both previous
        // numbers and user-positioned unit/match frames. Include the crit symbol
        // in the box so a large critical never covers frame text or resources.
        const bool Marker=E.bCritical&&UISettings.bShowCriticalSymbol;
        const float BaseX=X,BaseY=Y,TopPad=Marker?15.f:2.f,LeftPad=Marker?26.f:3.f;
        bool Placed=false;FBox2D Placement;
        for(int32 Side=0;Side<3&&!Placed;++Side)for(int32 Attempt=0;Attempt<16&&!Placed;++Attempt)
        {
            X=BaseX+(Side==0?0.f:Side==1?-(W+36.f):W+36.f);
            const int32 Row=(Attempt+1)/2;
            Y=BaseY+(Attempt==0?0.f:(Attempt%2?1.f:-1.f)*Row*(H+TopPad+6.f));
            Placement=FBox2D(FVector2D(X-LeftPad,Y-TopPad),FVector2D(X+W+3,Y+H));
            if(Placement.Min.X<3||Placement.Min.Y<3||Placement.Max.X>ViewW-3||Placement.Max.Y>ViewH-3)continue;
            bool Overlap=false;
            for(const auto& Other:ProtectedPanels)if(Placement.Intersect(Other)){Overlap=true;break;}
            if(!Overlap)for(const auto& Other:Occupied)if(Placement.Intersect(Other)){Overlap=true;break;}
            Placed=!Overlap;
        }
        if(!Placed)continue;
        Occupied.Add(Placement);
        FLinearColor C=ColorFor(E);C.A=FMath::Clamp((2.1f-Age)/.65f,0.f,1.f);
        Outlined(Amount,X,Y,Font,C);
        if(E.bCritical&&UISettings.bShowCriticalSymbol)
        {
            Outlined(TEXT("CRIT"),X+W*.5f-12,Y-13,9,C);
            const float StarX=X-14,StarY=Y+Font*.45f;
            for(int32 Ray=0;Ray<8;++Ray){const float A=Ray*PI/4;const float R=Ray%2?5.f:9.f;
                Line(StarX+FMath::Cos(A)*2,StarY+FMath::Sin(A)*2,StarX+FMath::Cos(A)*R,StarY+FMath::Sin(A)*R,C,1.5f);}
        }
    }

    // Combine each ability's simultaneous area/burst hits into one SCT entry.
    // Every affected target still gets its own world number; the personal lane
    // shows their combined effective amount and target count without losing AoE.
    struct FScrollingEntry {
        const FCireCombatEvent* Event=nullptr;
        float Amount=0;
        int32 Hits=0,Lane=0;
        TSet<FName> Targets;
    };
    TArray<FScrollingEntry> Entries;
    for(int32 I=Controller->CombatEvents.Num()-1;UISettings.bShowScrollingText&&I>=0;--I) {
        const auto& E=Controller->CombatEvents[I];const float Age=Now-E.TimeSeconds;
        if(Age<0||Age>3.2f||!Eligible(E))continue;
        const int32 Lane=E.bLocalTarget&&UISettings.bShowIncoming?0:1;
        auto* Group=Entries.FindByPredicate([&](const FScrollingEntry& Entry) {
            return E.Outcome==ECireHitOutcome::Hit&&Entry.Event->Outcome==E.Outcome&&Entry.Lane==Lane&&Entry.Event->AbilityName==E.AbilityName&&
                Entry.Event->SourceId==E.SourceId&&Entry.Event->bHealing==E.bHealing&&Entry.Event->bCritical==E.bCritical&&
                FMath::Abs(Entry.Event->ServerTime-E.ServerTime)<=.12f;
        });
        if(!Group){FScrollingEntry Entry;Entry.Event=&E;Entry.Lane=Lane;Entries.Add(MoveTemp(Entry));Group=&Entries.Last();}
        Group->Amount+=E.Amount;++Group->Hits;Group->Targets.Add(E.TargetId);
    }

    // Independent personal lanes stay readable for off-screen targets as well.
    // New hits enter at the bottom and push previous hits up; the area is movable.
    Origin=AnchoredOrigin;Stretch=AnchoredStretch;
    int32 Rows[2]={0,0};
    const float RowHeight=FMath::Max(39.f,UISettings.CombatTextFontSize+17.f);
    const int32 MaxRows=FMath::Max(1,FMath::FloorToInt(194.f/RowHeight));
    for(const auto& Entry:Entries) {
        const auto& E=*Entry.Event;const float Age=Now-E.TimeSeconds;
        const int32 Lane=Entry.Lane;if(Rows[Lane]>=MaxRows)continue;
        const float X=Lane==0?0.f:246.f,Y=191.f-(++Rows[Lane])*RowHeight-FMath::Min(Age*5.f,12.f);
        FLinearColor C=ColorFor(E);C.A=FMath::Clamp((3.2f-Age)/.8f,0.f,1.f);
        const FString Amount=E.Outcome!=ECireHitOutcome::Hit?NumberFor(E):FString::Printf(TEXT("%s%.0f"),E.bHealing?TEXT("+"):Lane==0?TEXT("-"):TEXT(""),Entry.Amount);
        const float SCTFont=UISettings.CombatTextFontSize*(E.bCritical?1.13f:1.f);
        const float NumberX=Lane==0?182.f-TextWidth(Amount,SCTFont):X;
        Outlined(Amount,NumberX,Y,SCTFont,C);
        const FString Recipient=Entry.Targets.Num()>1?FString::Printf(TEXT("%d targets"),Entry.Targets.Num()):
            Entry.Hits>1?FString::Printf(TEXT("%d hits"),Entry.Hits):ShortName(Lane==0?E.SourceName:E.TargetName,14);
        const FString Detail=(E.bCritical?TEXT("CRIT / "):TEXT(""))+ShortName(E.AbilityName,17)+TEXT(" / ")+Recipient;
        const float DetailX=Lane==0?182.f-TextWidth(Detail,10):X;
        Outlined(Detail,DetailX,Y+SCTFont,10,FLinearColor(.88f,.91f,.91f,C.A*.95f));
    }
    if(Rows[0]>0)Outlined(TEXT("INCOMING"),125,201,9,FLinearColor(1.f,.52f,.48f,.8f));
    if(Rows[1]>0)Outlined(TEXT("OUTGOING"),246,201,9,FLinearColor(1.f,.84f,.42f,.8f));
    ResetTransform();
}

void ACireHUD::DrawNameplates(ACireHero* Hero)
{
    ResetTransform();const auto* State=GetWorld()->GetGameState<ACireGameState>();const bool Arena=State&&State->Phase==2;int32 Count=0;
    auto Plate=[&](AActor* Actor,const FString& Name,float HP,float MaxHP,FLinearColor Color,float Lift) {
        if(!IsValid(Actor)||Actor==Hero||HP<=0||MaxHP<=0)return;
        const bool Selected=Hero->Target==Actor;
        if(!Selected&&(Count>=32||FVector::DistSquared(Hero->GetActorLocation(),Actor->GetActorLocation())>FMath::Square(2400.f)))return;
        FVector2D Screen;if(!PlayerOwner->ProjectWorldLocationToScreen(Actor->GetActorLocation()+FVector(0,0,Lift),Screen,false))return;
        const float X=Screen.X/Scale,Y=Screen.Y/Scale;if(X<35||X>ViewW-35||Y<105||Y>ViewH-185)return;
        if(Selected){Frame(X-74,Y-4,148,28,Gold);Label(ShortName(Name,22),X-65,Y,11,Color);Bar(X-66,Y+17,132,4,HP/MaxHP,Color);}
        else Bar(X-25,Y,50,4,HP/MaxHP,Color);++Count;
        const int32 Poisoned=PoisonCount(Actor);
        if(Poisoned>0&&Selected){Panel(X-48,Y+25,96,15,Ink);Label(PoisonLabel(Poisoned),X-42,Y+27,9,Poison);}
    };
    for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(Arena||It->TeamId==Hero->TeamId)Plate(*It,It->HeroName,It->Health,It->MaxHealth,It->TeamId==Hero->TeamId?Teal:Red,120);
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It)if(!Arena&&It->Lane==Hero->TeamId)Plate(*It,It->MonsterName,It->Health,It->MaxHealth,It->PackId>=0?Purple:Red,100);
    for(TActorIterator<ACireConstruct> It(GetWorld());It;++It)if(It->CanObserve(PlayerOwner))Plate(*It,It->GetDisplayName(),It->Health,It->MaxHealth,It->OriginTeam==Hero->TeamId?Teal:Red,It->ConstructSpec.Height*.5f+25);
}
void ACireHUD::DrawHUD()
{
    Super::DrawHUD();if(!Canvas||!PlayerOwner||Canvas->ClipX<=0||Canvas->ClipY<=0)return;
    Scale=FMath::Max(.25f,FMath::Min(Canvas->ClipX/1280.f,Canvas->ClipY/720.f));ViewW=Canvas->ClipX/Scale;ViewH=Canvas->ClipY/Scale;
    MX=MY=-100;float MouseX=0,MouseY=0;if(PlayerOwner->GetMousePosition(MouseX,MouseY)){MX=MouseX/Scale;MY=MouseY/Scale;}
    Clicked=PlayerOwner->WasInputKeyJustPressed(EKeys::LeftMouseButton)&&!PlayerOwner->IsInputKeyDown(EKeys::RightMouseButton);
    auto* Controller=Cast<ACireController>(PlayerOwner);auto* Hero=Cast<ACireHero>(PlayerOwner->GetPawn());auto* State=GetWorld()->GetGameState<ACireGameState>();
    bModal=Hero&&((!Hero->bDrafted||Hero->Offers.Num()>0||(Controller&&Controller->bShop))||(State&&State->Phase==3));
    TooltipTitle.Reset();TooltipBody.Reset();
    LayoutInteraction();VisiblePanels.Reset();ResetTransform();
    if(DrawReplayScreen()){DrawSettings();DrawDiagnostics();DrawTooltip();ResetTransform();return;}
    if(!Hero){Label(TEXT("Joining the battlefield..."),ViewW*.5f-130,ViewH*.5f,20,Parchment);return;}
    if(!bModal)DrawNameplates(Hero);
    DrawPlayer(Hero);DrawParty(Hero,Controller);DrawMatch(State);DrawMinimap(Hero,State);
    if(IsValid(Hero->Target)||bEditLayout)DrawUnit(Hero->Target,TEXT("TARGET"),false);
    if(Controller&&(IsValid(Controller->FocusTarget)||bEditLayout))DrawUnit(Controller->FocusTarget,TEXT("FOCUS / CLICK TO TARGET"),true);
    DrawSkills(Hero,Controller);DrawChat(Controller);DrawMeters(Hero,Controller);DrawPet(Hero,Controller);
    if(!bModal&&!bSettings)DrawCombatText(Hero,Controller);
    ResetTransform();
    if(!bModal&&!bSettings&&Controller)
    {
        const auto Aim=CireTargeting::Snapshot(Controller);
        if(Aim.bActive){const FString Text=ACireHero::SkillName(Aim.SkillId)+TEXT(" | ")+Aim.Message;const float W=FMath::Min(750.f,TextWidth(Text,12)+28);Frame((ViewW-W)/2,ViewH-248,W,31,Aim.bValid?Teal:Red);Label(Text,(ViewW-W)/2+14,ViewH-240,12,Aim.bValid?Parchment:Red);}
        if(Hero->Mobility){const float CD=Hero->Mobility->CooldownRemaining();const FString Move=FString::Printf(TEXT("[%s] JUMP   [%s] DODGE %s   [%s] %s"),*UISettings.Keybindings.Label(TEXT("Jump")).ToUpper(),*UISettings.Keybindings.Label(TEXT("DodgeRoll")).ToUpper(),CD>0?*FString::Printf(TEXT("%.1fs"),CD):TEXT("READY"),*UISettings.Keybindings.Label(TEXT("ToggleWalk")).ToUpper(),Hero->Mobility->bWalking?TEXT("WALK"):TEXT("RUN"));Label(Move,(ViewW-TextWidth(Move,9))/2,ViewH-185,9,Hero->Mobility->IsInvulnerable()?Teal:Muted);}
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
    if(bEditLayout)VisiblePanels.AddUnique(TEXT("Tooltip"));
    DrawLayoutEditor();DrawDeveloperLauncher();DrawSettings();DrawDiagnostics();DrawTooltip();ResetTransform();
}

void ACireHUD::DrawModal(ACireHero* Hero,ACireController* Controller,ACireGameState* State)
{
    TooltipTitle.Reset();TooltipBody.Reset();
    ResetTransform(); const float W=ViewW,H=ViewH;
    const bool MatchFinished=State&&State->Phase==3;
    const bool DraftOpen=Hero&&!Hero->bDrafted&&!MatchFinished;
    const bool OfferOpen=Hero&&Hero->bDrafted&&Hero->Offers.Num()>0&&!MatchFinished;
    const bool ShopOpen=Controller&&Controller->bShop&&!DraftOpen&&!OfferOpen&&!MatchFinished;
    const bool ModalOpen=DraftOpen||OfferOpen||ShopOpen;
    auto Center=[&](const FString& Text,float Y,float Size,FLinearColor Color){Label(Text,(W-TextWidth(Text,Size))*.5f,Y,Size,Color);};
    auto Action=[&](int32 Type,int32 Value){if(Clicked&&Controller&&!bSettings&&!bEditLayout){Controller->ServerAction(Type,Value,nullptr);Clicked=false;}};
    if (ModalOpen)
    {
        Panel(0, 135, W, H - 330, FLinearColor(0.004f, 0.008f, 0.011f, .78f));
        if (DraftOpen)
        {
            DrawDraftRoster(Hero,Controller);
        }
        else if (OfferOpen)
        {
            const float X = W / 2 - 512;
            Panel(X, 168, 1024, 338, Ink);
            Panel(X, 168, 1024, 2, Gold);
            Label(FString(TEXT("POWER TAKES SHAPE  /  "))+UTF8_TO_TCHAR(Cires::DraftRoleName(CireChampionProfiles::DraftRole(Hero))), X + 22, 187, 24, Parchment);
            Label(TEXT("Choose one skill. Your final build has six active skills, one passive and one ultimate."), X + 23, 226, 12, Muted);
            for (int32 I = 0; I < FMath::Min(4, Hero->Offers.Num()); ++I)
            {
                const float CX = X + 22 + I * 247.f;
                const bool Over = Hit(CX, 258, 237, 222);
                const FString& Id = Hero->Offers[I];
                const bool Passive = ACireHero::IsPassive(Id);
                const bool Ultimate = ACireHero::IsUltimate(Id);
                Panel(CX, 258, 237, 222, Over ? Hover : Card);
                Panel(CX, 258, 237, 3, Ultimate ? Gold : Passive ? Purple : Teal);
                Label(Ultimate ? TEXT("ULTIMATE / ONE ONLY") : Passive ? TEXT("PASSIVE / ONE ONLY") : TEXT("ACTIVE ABILITY"), CX + 15, 274, 10, Ultimate ? Gold : Passive ? Purple : Teal);
                Wrapped(ACireHero::SkillName(Id), CX + 15, 303, 208, 18, Parchment, 2);
                Wrapped(ACireHero::SkillDescription(Id), CX + 15, 355, 205, 12, Muted, 4);
                Label(FString::Printf(TEXT("[%d] CLAIM ABILITY >"), I + 1), CX + 15, 451, 12, Over ? Parchment : Gold);
                Tip(ACireHero::SkillName(Id),ACireHero::SkillDescription(Id),CX,258,237,222);
                if (Over) Action(3, I);
            }
        }
        else if (ShopOpen)
        {
            const float X = W / 2 - 395;
            Panel(X, 173, 790, 333, Ink);
            Panel(X, 173, 790, 2, Gold);
            Label(TEXT("THE QUARTERMASTER"), X + 23, 191, 23, Parchment);
            Label(State && State->Phase == 1 ? TEXT("Town preparation / spend wisely before the portal opens.") : State && State->Phase == 4 ? TEXT("Town recovery / resupply before the next wave cycle.") : TEXT("Purchases require town preparation or recovery at your base."), X + 24, 229, 12, Muted);
            Label(TEXT("B  CLOSE"), X + 687, 197, 11, Gold);
            // Indices and visible prices are mirrored from ACireHero::Purchase.
            const TCHAR* Names[] = { TEXT("TOME OF EXPERIENCE"), TEXT("TOME OF PRIMARY STAT"), TEXT("FIELD EQUIPMENT"), TEXT("FOCUS RELIC") };
            const TCHAR* Details[] = { TEXT("Gain 300 experience toward your next level."), TEXT("Gain +3 to your primary attribute this match."), TEXT("Gain +4 to your primary attribute and a gear rank."), TEXT("Gain 5% pure cooldown reduction. Maximum 60%.") };
            const int32 Prices[] = { 100, 120, 180, 160 };
            for (int32 I = 0; I < 4; ++I)
            {
                const float CX = X + 24 + (I % 2) * 374;
                const float CY = 265 + (I / 2) * 109;
                const bool Over = Hit(CX, CY, 365, 96);
                Panel(CX, CY, 365, 96, Over ? Hover : Card);
                Label(Names[I], CX + 14, CY + 13, 15, Parchment);
                Wrapped(Details[I], CX + 14, CY + 40, 335, 11, Muted, 2);
                Label(FString::Printf(TEXT("%d GOLD  /  PURCHASE >"), Prices[I]), CX + 14, CY + 76, 10, Hero->Gold >= Prices[I] ? Gold : Red);
                Tip(Names[I],FString(Details[I])+FString::Printf(TEXT(" Costs %d gold. Purchases are validated at your own town during preparation or recovery."),Prices[I]),CX,CY,365,96);
                if (Over) Action(4, I);
            }
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

