#include "CireHUD.h"
#include "CireChampionRoster.h"
#include "CireChampionProfiles.h"
#include "CireGame.h"
#include "Engine/World.h"

namespace
{
constexpr int32 PageSize=6;
const FLinearColor Ink(.015f,.019f,.023f,.99f),Card(.039f,.046f,.052f,.99f),Hover(.078f,.091f,.093f,1);
const FLinearColor Gold(.80f,.64f,.36f,1),Text(.95f,.92f,.83f,1),Muted(.64f,.67f,.65f,1);
const FLinearColor Tank(.82f,.65f,.35f,1),Damage(.51f,.76f,.69f,1),Healer(.51f,.68f,.89f,1);
FString PrimaryLabel(const FString& Primary)
{return Primary==TEXT("strength")?TEXT("STR"):Primary==TEXT("agility")?TEXT("AGI"):TEXT("INT");}
FString StyleLabel(const FString& Style)
{
    if(Style==TEXT("axes"))return TEXT("Axes");
    if(Style==TEXT("arcane"))return TEXT("Arcane");
    FString Result=Style;if(!Result.IsEmpty())Result[0]=FChar::ToUpper(Result[0]);return Result;
}
int32 RosterSize(){return CireChampionRoster::Count()>0?CireChampionRoster::Count():5;}
}

int32 ACireHUD::DraftRosterPageCount() const {return FMath::Max(1,FMath::DivideAndRoundUp(RosterSize(),PageSize));}
FString ACireHUD::DraftRosterIdForSlot(int32 Slot) const
{
    if(Slot<0||Slot>=PageSize)return {};
    const int32 Page=FMath::Clamp(RosterPage,0,DraftRosterPageCount()-1),Index=Page*PageSize+Slot;
    if(const auto* Profile=CireChampionRoster::FindByIndex(Index))return Profile->Id;
    if(CireChampionRoster::Count()==0)if(const TCHAR* Id=CireChampionProfiles::LegacyProfileId(Index))return Id;
    return {};
}
void ACireHUD::ChangeDraftRosterPage(int32 Delta)
{
    auto* H=PlayerOwner?Cast<ACireHero>(PlayerOwner->GetPawn()):nullptr;
    if(!H||H->bDrafted||bSettings||bEditLayout)return;
    RosterPage=FMath::Clamp(RosterPage+FMath::Clamp(Delta,-1,1),0,DraftRosterPageCount()-1);
}
bool ACireHUD::DraftRosterSlot(int32 Slot)
{
    auto* PC=Cast<ACireController>(PlayerOwner);auto* H=PC?Cast<ACireHero>(PC->GetPawn()):nullptr;
    const auto* State=GetWorld()?GetWorld()->GetGameState<ACireGameState>():nullptr;
    if(!PC||!H||H->bDrafted||H->bDead||bSettings||bEditLayout||(State&&State->Phase==3))return false;
    const FString Id=DraftRosterIdForSlot(Slot);if(Id.IsEmpty())return false;
    if(CireChampionRoster::Count()>0)PC->ServerDraftProfile(Id);
    else PC->ServerAction(5,Slot,nullptr);
    return true;
}

void ACireHUD::DrawDraftRoster(ACireHero* Hero,ACireController* Controller)
{
    if(!Hero||Hero->bDrafted)return;
    ResetTransform();RosterPage=FMath::Clamp(RosterPage,0,DraftRosterPageCount()-1);
    const float W=FMath::Min(1136.f,ViewW-64),H=608,X=(ViewW-W)*.5f,Y=(ViewH-H)*.5f;
    Panel(0,0,ViewW,ViewH,FLinearColor(.003f,.006f,.009f,.84f));Frame(X,Y,W,H,Gold);
    Panel(X+2,Y+2,W-4,H-4,Ink);Panel(X+2,Y+2,W-4,2,Gold);
    Label(TEXT("CHOOSE YOUR CHAMPION"),X+23,Y+20,26,Text);
    Label(TEXT("Begin with your basic attack. Forge your skill build through choices as you level."),X+24,Y+61,13,Muted);
    const FString Count=FString::Printf(TEXT("%d CHAMPION PROFILES"),RosterSize());
    Label(Count,X+W-24-TextWidth(Count,11),Y+32,11,Gold);
    const float Gap=14,CW=(W-44-2*Gap)/3,CH=210,Top=Y+102;
    const bool Interactive=Controller&&!bSettings&&!bEditLayout;
    for(int32 Slot=0;Slot<PageSize;++Slot)
    {
        const int32 Index=RosterPage*PageSize+Slot;
        const auto* P=CireChampionRoster::FindByIndex(Index);
        FCireChampionProfile Fallback;
        if(!P&&CireChampionRoster::Count()==0&&Index<5)
        {
            static const TCHAR* Names[]={TEXT("Iron Warden"),TEXT("Ash Ranger"),TEXT("Veil Scholar"),TEXT("Lancer"),TEXT("Rift Summoner")};
            Fallback.Id=CireChampionProfiles::LegacyProfileId(Index);Fallback.DisplayName=Names[Index];
            Fallback.RuntimeArchetype=Index;Fallback.PrimaryStat=Index==0?TEXT("strength"):Index==1||Index==3?TEXT("agility"):TEXT("intelligence");
            Fallback.ThreatRole=Index==0?TEXT("tank"):Index==2?TEXT("healer"):TEXT("damage");
            Fallback.Strength=Index==0?20:10;Fallback.Agility=Index==1||Index==3?20:10;Fallback.Intelligence=Index==2||Index==4?20:10;
            Fallback.BasicAttackRange=Index==0?220:750;Fallback.AttackStyle=Index==0?TEXT("sword"):Index==1?TEXT("bow"):Index==3?TEXT("lance"):TEXT("arcane");
            Fallback.Description=TEXT("An original champion foundation. Choose your skills as you level.");P=&Fallback;
        }
        if(!P)continue;
        const float CX=X+22+(Slot%3)*(CW+Gap),CY=Top+(Slot/3)*(CH+Gap);
        const bool Over=Interactive&&Hit(CX,CY,CW,CH);
        const FLinearColor Accent=P->ThreatRole==TEXT("tank")?Tank:P->ThreatRole==TEXT("healer")?Healer:Damage;
        Panel(CX,CY,CW,CH,Over?Hover:Card);Panel(CX,CY,CW,2,Over?Gold:Accent);
        Line(CX,CY+CH,CX+CW,CY+CH,FLinearColor(.18f,.21f,.21f,1));
        Icon(P->Id==TEXT("summoner")?TEXT("role4"):P->ThreatRole==TEXT("tank")?TEXT("role0"):
            P->ThreatRole==TEXT("healer")?TEXT("role2"):P->AttackStyle==TEXT("bow")?TEXT("role1"):TEXT("cleaving_strike"),CX+14,CY+15,35,Accent);
        Wrapped(P->DisplayName,CX+61,CY+13,CW-101,18,Text,2);
        Panel(CX+CW-30,CY+15,19,23,FLinearColor(.015f,.022f,.024f,1));
        Label(FString::FromInt(Slot+1),CX+CW-25,CY+18,13,Gold);
        Label(P->ThreatRole.ToUpper()+TEXT(" / ")+PrimaryLabel(P->PrimaryStat)+TEXT(" PRIMARY"),CX+15,CY+65,12,Accent);
        Label(FString::Printf(TEXT("STR %d     AGI %d     INT %d"),P->Strength,P->Agility,P->Intelligence),CX+15,CY+90,13,Text);
        Label(FString::Printf(TEXT("%s %s  /  %.1f m  /  %.1f s base"),P->BasicAttackRange>300?TEXT("Ranged"):TEXT("Melee"),
            *StyleLabel(P->AttackStyle),P->BasicAttackRange/100.f,P->AttackSeconds),CX+15,CY+115,12,Muted);
        Wrapped(P->Description,CX+15,CY+140,CW-30,11,Muted,2);
        Label(Over?TEXT("SELECT CHAMPION  >"):TEXT("SELECT"),CX+15,CY+185,12,Over?Text:Gold);
        Tip(P->DisplayName,P->Description+TEXT(" Start with zero learned skills. Your primary attribute adds basic attack damage; STR grants health, AGI attack speed and INT mana. Thematic ability examples are earned through choices, and planned abilities are not available."),CX,CY,CW,CH);
        if(Over&&Clicked){DraftRosterSlot(Slot);Clicked=false;}
    }
    const float FooterY=Y+550;
    const auto PageButton=[&](const TCHAR* Title,float BX,bool Enabled,int32 Delta)
    {
        const bool Over=Interactive&&Enabled&&Hit(BX,FooterY,154,34);
        Panel(BX,FooterY,154,34,Over?Hover:Card);
        Label(Title,BX+16,FooterY+9,12,Enabled?Gold:FLinearColor(.33f,.36f,.36f,1));
        if(Over&&Clicked){ChangeDraftRosterPage(Delta);Clicked=false;}
    };
    PageButton(TEXT("<  PREVIOUS"),X+22,RosterPage>0,-1);
    PageButton(TEXT("NEXT  >"),X+W-176,RosterPage+1<DraftRosterPageCount(),1);
    const FString PageText=FString::Printf(TEXT("PAGE %d / %d     |     1-6 SELECT     |     ARROWS TO BROWSE"),RosterPage+1,DraftRosterPageCount());
    Label(PageText,X+(W-TextWidth(PageText,11))*.5f,FooterY+10,11,Text);
    const FString Footnote=TEXT("Preview bodies are temporary. Every champion starts with zero learned skills.");
    Label(Footnote,X+(W-TextWidth(Footnote,10))*.5f,Y+590,10,Muted);
    ResetTransform();
}
