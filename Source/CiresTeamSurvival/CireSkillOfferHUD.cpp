// Level-up skill offer: four large ability cards in the style-kit visual language
// (shared with the champion draft screen). Cards show the painted icon, role tags,
// type frame (ultimate gold, passive octagon/violet), cost / cooldown / range /
// target, a numeric description, synergy with the learned kit and the slot the pick
// fills. The panel animates in, can be deferred (N) to a pulsing reminder so it never
// blocks combat, auto-waits while you are fighting, and plays a pick animation + chime.
#include "CireHUD.h"
#include "CireAbilityIcons.h"
#include "CireChampionProfiles.h"
#include "CireClassTraits.h"
#include "CireCrowdControl.h"
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "EngineUtils.h"
#include "CireGame.h"
#include "CireKeybindings.h"
#include "CireRoleSkills.h"
#include "CireTargeting.h"
#include "CireUIStyle.h"
#include "CireBanners.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "Internationalization/Regex.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Sound/SoundBase.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSkillOffer,Log,All);

namespace
{
using CireUIColors::Gold;using CireUIColors::BrightGold;using CireUIColors::Parchment;using CireUIColors::Muted;using CireUIColors::Teal;using CireUIColors::Orange;
const FLinearColor TankColor=FLinearColor::FromSRGBColor(FColor(92,148,228)),DpsColor=FLinearColor::FromSRGBColor(FColor(216,80,64)),
    SupportColor=FLinearColor::FromSRGBColor(FColor(88,198,126)),Violet=FLinearColor::FromSRGBColor(FColor(170,130,222));
constexpr float CardW=236,CardH=310,CardGap=14,PickSeconds=.95f;

// ---------------------------------------------------------------- skill facts
struct FSkillFacts
{
    float Mana=0,Energy=0,Cooldown=0,Range=0;
    bool bPassive=false,bUltimate=false;
    ECireTargetKind Target=ECireTargetKind::None;
    FString TargetLabel,Body,Full;
};
float FirstNumber(const FString& Text,const TCHAR* Pattern)
{
    FRegexMatcher M(FRegexPattern(Pattern),Text);
    return M.FindNext()?FCString::Atof(*M.GetCaptureGroup(1)):0.f;
}
void Strip(FString& Text,const TCHAR* Pattern)
{
    for(int32 Guard=0;Guard<8;++Guard)
    {
        FRegexMatcher M(FRegexPattern(Pattern),Text);
        if(!M.FindNext())return;
        Text.RemoveAt(M.GetMatchBeginning(),M.GetMatchEnding()-M.GetMatchBeginning());
    }
}
FSkillFacts Facts(const FString& Id)
{
    FSkillFacts F;
    F.bPassive=ACireHero::IsPassive(Id);F.bUltimate=ACireHero::IsUltimate(Id);
    F.Full=ACireHero::SkillDescription(Id);
    F.Mana=FirstNumber(F.Full,TEXT("(\\d+(?:\\.\\d+)?) mana"));
    F.Energy=FirstNumber(F.Full,TEXT("(\\d+(?:\\.\\d+)?) energy"));
    F.Cooldown=FirstNumber(F.Full,TEXT("(\\d+(?:\\.\\d+)?)s (?:base cooldown|CD)"));
    const FCireTargetDescriptor D=CireTargeting::Describe(Id);
    F.Target=D.Kind;F.TargetLabel=D.Label;F.Range=D.Range;
    if(F.Range<=0)F.Range=FirstNumber(F.Full,TEXT("(\\d+) cm (?:cast )?range"));
    FString B=F.Full;
    Strip(B,TEXT("^(?:ULTIMATE|PASSIVE)\\s*[:|]\\s*"));
    Strip(B,TEXT("\\d+(?:\\.\\d+)? (?:mana|energy) \\| \\d+(?:\\.\\d+)?s CD\\.\\s*"));
    Strip(B,TEXT("\\s*\\d+(?:\\.\\d+)? mana / \\d+(?:\\.\\d+)? energy\\s*[;|]\\s*\\d+(?:\\.\\d+)?s (?:base cooldown|CD)\\s*[;|]\\s*\\d+(?:\\.\\d+)? cm (?:cast )?range\\.?"));
    Strip(B,TEXT("\\s*Uses your only passive slot\\."));
    if(CireRoleSkills::Handles(Id)){const FString T=CireRoleSkills::Targeting(Id)+TEXT(". ");if(B.StartsWith(T))B.RightChopInline(T.Len());}
    B.TrimStartAndEndInline();
    if(!B.IsEmpty())B[0]=FChar::ToUpper(B[0]);
    F.Body=B;
    return F;
}
FString TargetWord(const FSkillFacts& F)
{
    if(F.bPassive)return TEXT("PASSIVE");
    switch(F.Target)
    {
    case ECireTargetKind::Self:return TEXT("SELF");
    case ECireTargetKind::Friendly:return TEXT("ALLY");
    case ECireTargetKind::Hostile:return TEXT("ENEMY");
    case ECireTargetKind::Ground:return TEXT("AIM");
    default:return TEXT("-");
    }
}
FLinearColor TargetColor(const FSkillFacts& F)
{
    if(F.bPassive)return Violet;
    return F.Target==ECireTargetKind::Friendly?SupportColor:F.Target==ECireTargetKind::Hostile?DpsColor:F.Target==ECireTargetKind::Ground?Orange:Parchment;
}

// ---------------------------------------------------------------- synergy hints (cheap, rule based)
enum ETag : uint32 { Heal=1,Slow=2,Area=4,Taunt=8,Guard=16,Summon=32,Mobility=64,Finisher=128,Construct=256,Cleanse=512 };
uint32 Tags(const FString& Id)
{
    static const TMap<FString,uint32> T={
        {TEXT("restoring_light"),Heal},{TEXT("sanctuary"),Heal|Guard},{TEXT("purify"),Heal|Cleanse},{TEXT("renewal"),Heal|Cleanse},
        {TEXT("wellspring"),Heal|Guard},{TEXT("second_wind"),Heal},{TEXT("last_stand"),Heal|Cleanse},{TEXT("bastion_of_dawn"),Heal|Guard},
        {TEXT("frost_bind"),Slow},{TEXT("shield_slam"),Slow|Taunt},{TEXT("war_cry"),Taunt|Guard},{TEXT("challenge_of_iron"),Taunt|Guard},
        {TEXT("iron_guard"),Guard},{TEXT("mass_aegis"),Guard|Cleanse},{TEXT("protection_dome"),Guard|Construct},{TEXT("summoned_wall"),Construct},
        {TEXT("venom_ground"),Area},{TEXT("cinder_cone"),Area},{TEXT("grave_line"),Area},{TEXT("ashen_square"),Area},{TEXT("blight_sigil"),Area},
        {TEXT("starfall"),Area},{TEXT("cataclysm"),Area},{TEXT("seismic_reprisal"),Area},{TEXT("cleaving_strike"),Area},{TEXT("chain_spark"),Area},
        {TEXT("oathbound_guardian"),Summon},{TEXT("spectral_pack"),Summon},{TEXT("spectral_hunt"),Summon},
        {TEXT("shadow_step"),Mobility},{TEXT("executioners_verdict"),Finisher},{TEXT("piercing_shot"),Finisher},{TEXT("ember_lance"),Finisher}};
    const uint32* Found=T.Find(Id);return Found?*Found:0;
}
FString FirstWith(const TArray<FString>& Learned,uint32 Tag)
{
    for(const FString& L:Learned)if(Tags(L)&Tag)return ACireHero::SkillName(L);
    return {};
}
int32 CountWith(const TArray<FString>& Learned,uint32 Tag){int32 N=0;for(const FString& L:Learned)N+=(Tags(L)&Tag)!=0;return N;}
TArray<FString> Synergies(const FString& Id,const TArray<FString>& Learned)
{
    TArray<FString> Out;
    const uint32 Mine=Tags(Id);
    const auto Add=[&](const FString& Line){if(!Line.IsEmpty()&&Out.Num()<2)Out.Add(Line);};
    if(Id==TEXT("soul_conduit")&&CountWith(Learned,Heal)>0)
        Add(FString::Printf(TEXT("+25%% healing on your %d heal%s (%s)."),CountWith(Learned,Heal),CountWith(Learned,Heal)>1?TEXT("s"):TEXT(""),*FirstWith(Learned,Heal)));
    if((Mine&Heal)&&Learned.Contains(TEXT("soul_conduit")))Add(TEXT("Soul Conduit makes this heal 25% stronger."));
    if(Id==TEXT("deep_reserves"))
    {
        float SumMana=0,SumEnergy=0;for(const FString& L:Learned){const FSkillFacts F=Facts(L);SumMana+=F.Mana;SumEnergy+=F.Energy;}
        if(SumMana+SumEnergy>0)Add(FString::Printf(TEXT("Refills the %.0f mana / %.0f energy your learned skills spend 50%% faster."),SumMana,SumEnergy));
    }
    if(Id==TEXT("stone_skin")&&CountWith(Learned,Taunt)>0)Add(FString::Printf(TEXT("Take 10%% less damage while %s holds their attention."),*FirstWith(Learned,Taunt)));
    if(Id==TEXT("battle_rhythm")&&CountWith(Learned,Mobility|Finisher)>0)Add(TEXT("Faster swings between your burst cooldowns."));
    if((Mine&Taunt)&&CountWith(Learned,Guard)>0)Add(FString::Printf(TEXT("Pull enemies, then weather them with %s."),*FirstWith(Learned,Guard)));
    if((Mine&Taunt)&&Learned.Contains(TEXT("stone_skin")))Add(TEXT("Stone Skin softens the hits you draw."));
    if((Mine&Guard)&&!(Mine&Taunt)&&CountWith(Learned,Taunt)>0)Add(FString::Printf(TEXT("Covers you after %s pulls the pack."),*FirstWith(Learned,Taunt)));
    if((Mine&Slow)&&CountWith(Learned,Area)>0)Add(FString::Printf(TEXT("Slowed enemies stay inside your %s."),*FirstWith(Learned,Area)));
    if((Mine&Area)&&CountWith(Learned,Slow)>0)Add(FString::Printf(TEXT("%s slows keep enemies in this area."),*FirstWith(Learned,Slow)));
    if((Mine&Construct)&&CountWith(Learned,Area)>0)Add(FString::Printf(TEXT("Wall enemies into your %s."),*FirstWith(Learned,Area)));
    if((Mine&Mobility)&&CountWith(Learned,Finisher)>0)Add(FString::Printf(TEXT("Close the gap, then finish with %s."),*FirstWith(Learned,Finisher)));
    if((Mine&Finisher)&&CountWith(Learned,Mobility)>0)Add(FString::Printf(TEXT("%s gets you in range to finish."),*FirstWith(Learned,Mobility)));
    if((Mine&Summon)&&CountWith(Learned,Summon)>0)Add(FString::Printf(TEXT("More bodies on the field alongside %s."),*FirstWith(Learned,Summon)));
    if((Mine&Cleanse)&&CountWith(Learned,Heal)>0&&!(Mine&Heal))Add(TEXT("Rounds out your healing with a cleanse."));
    if(Out.IsEmpty())
    {
        const FString School=CireAbilityIcons::School(Id);
        for(const FString& L:Learned)if(!School.IsEmpty()&&CireAbilityIcons::School(L)==School)
        {Add(FString::Printf(TEXT("Same %s school as %s."),*School,*ACireHero::SkillName(L)));break;}
    }
    return Out;
}

// ---------------------------------------------------------------- per-HUD state
struct FOfferUI
{
    FString Key;
    TArray<FString> Ids;
    int32 SkillsAtOpen=0;
    double OpenedAt=0,CalmSince=0;
    bool bOpen=false,bUserDeferred=false,bCombatHeld=false,bPlayOpenSound=false;
    int32 Hovered=-1,ForcedHover=-1;
    TArray<FVector2D> CardPos;
    TArray<FVector2D> SlotPos;
    // Pick animation.
    FString PickedId;int32 PickedIndex=-1,PickedSlot=-1;double PickAt=-100;FVector2D PickFrom,PickTo;
    TArray<FString> PickedFrom;
    bool bPickSound=false;
#if !UE_BUILD_SHIPPING
    int32 GalleryStage=-1;double GalleryAt=0,GalleryStarted=0,ShotAt=0;FString GalleryDir;TArray<FString> GalleryFiles;bool bGalleryDone=false,bGalleryPass=true;
#endif
};
TMap<TWeakObjectPtr<const ACireHUD>,FOfferUI> OfferStates;

bool InCombat(const APlayerController* PC)
{
    const auto* C=Cast<ACireController>(PC);if(!C||!C->GetWorld())return false;
    const float Now=C->GetWorld()->GetTimeSeconds();
    for(int32 I=C->CombatEvents.Num()-1;I>=0;--I)
    {
        const auto& E=C->CombatEvents[I];
        if((E.bLocalSource||E.bLocalTarget)&&Now-E.TimeSeconds<4.f&&Now>=E.TimeSeconds)return true;
    }
    return false;
}

// Brings the state in line with the replicated offer. Safe to call from const queries.
FOfferUI& Sync(const ACireHUD* HUD)
{
    for(auto It=OfferStates.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    FOfferUI& S=OfferStates.FindOrAdd(HUD);
    const auto* Hero=HUD->PlayerOwner?Cast<ACireHero>(HUD->PlayerOwner->GetPawn()):nullptr;
    if(!Hero||!HUD->GetWorld())return S;
    const double Now=HUD->GetWorld()->GetRealTimeSeconds();
    if(Hero->Offers.Num()>0)
    {
        const FString Key=FString::Join(Hero->Offers,TEXT(","))+FString::Printf(TEXT("|%d"),Hero->Skills.Num());
        if(Key!=S.Key)
        {
            const bool bFighting=InCombat(HUD->PlayerOwner);
            S.Key=Key;S.Ids=Hero->Offers;S.SkillsAtOpen=Hero->Skills.Num();S.OpenedAt=Now;S.bUserDeferred=false;
            S.bCombatHeld=bFighting;S.bOpen=!bFighting;S.bPlayOpenSound=S.bOpen;S.Hovered=-1;S.CalmSince=Now;
        }
        if(!S.bOpen&&S.bCombatHeld&&!S.bUserDeferred)
        {
            if(InCombat(HUD->PlayerOwner))S.CalmSince=Now;
            else if(Now-S.CalmSince>1.5){S.bOpen=true;S.bCombatHeld=false;S.OpenedAt=Now;S.bPlayOpenSound=true;}
        }
    }
    else if(!S.Ids.IsEmpty())
    {
        // Offer resolved: animate the learned card if a skill was added.
        if(Hero->Skills.Num()>S.SkillsAtOpen)
        {
            S.PickedId=Hero->Skills.Last();S.PickedIndex=S.Ids.IndexOfByKey(S.PickedId);S.PickAt=Now;S.PickedFrom=S.Ids;S.bPickSound=true;
            int32 Actives=0;for(const FString& Id:Hero->Skills)Actives+=!ACireHero::IsPassive(Id)&&!ACireHero::IsUltimate(Id);
            S.PickedSlot=ACireHero::IsPassive(S.PickedId)?6:ACireHero::IsUltimate(S.PickedId)?7:FMath::Clamp(Actives-1,0,5);
            S.PickFrom=S.CardPos.IsValidIndex(S.PickedIndex)?S.CardPos[S.PickedIndex]:FVector2D(640,300);
            S.PickTo=S.SlotPos.IsValidIndex(S.PickedSlot)?S.SlotPos[S.PickedSlot]:S.PickFrom+FVector2D(0,260);
        }
        S.Ids.Reset();S.Key.Reset();S.bOpen=false;
    }
    return S;
}

// Which kit slot a skill fills: 0..5 actives, 6 passive, 7 ultimate.
int32 KitSlotFor(const ACireHero* Hero,const FString& Id)
{
    if(ACireHero::IsPassive(Id))return 6;
    if(ACireHero::IsUltimate(Id))return 7;
    int32 Actives=0;for(const FString& S:Hero->Skills)Actives+=!ACireHero::IsPassive(S)&&!ACireHero::IsUltimate(S);
    return FMath::Min(Actives,5);
}
FString KitIdAt(const ACireHero* Hero,int32 Slot)
{
    int32 Actives=0;
    for(const FString& S:Hero->Skills)
    {
        if(ACireHero::IsPassive(S)){if(Slot==6)return S;continue;}
        if(ACireHero::IsUltimate(S)){if(Slot==7)return S;continue;}
        if(Actives++==Slot)return S;
    }
    return {};
}
void RoleChips(const FCireUIPainter& P,const FString& Id,float CX,float Y)
{
    const Cires::RoleMask Tags=Cires::SkillRoleTags(TCHAR_TO_UTF8(*Id));
    TArray<TPair<FString,FLinearColor>> Chips;
    if(Tags==Cires::RoleAll)Chips.Add({TEXT("ANY ROLE"),Gold});
    else
    {
        if(Tags&Cires::RoleTank)Chips.Add({TEXT("TANK"),TankColor});
        if(Tags&Cires::RoleDamage)Chips.Add({TEXT("DPS"),DpsColor});
        if(Tags&Cires::RoleSupport)Chips.Add({TEXT("SUPPORT"),SupportColor});
        if(Chips.Num()>1)Chips.Add({TEXT("HYBRID"),Parchment});
    }
    float Total=0;for(const auto& C:Chips)Total+=P.TextWidth(C.Key,7.5f,ECireFont::Heading)+14+5;
    float X=CX-(Total-5)*.5f;
    for(const auto& C:Chips)
    {
        const float W=P.TextWidth(C.Key,7.5f,ECireFont::Heading)+14;
        P.Rect(X,Y,W,14,FLinearColor(C.Value.R*.22f,C.Value.G*.22f,C.Value.B*.22f,.95f));
        P.Rect(X,Y,2,14,C.Value);
        P.Text(C.Key,X+8,Y+2,7.5f,C.Value*1.1f,ECireFont::Heading,false,false);
        X+=W+5;
    }
}
}

bool ACireHUD::IsSkillOfferOpen() const
{
    const FOfferUI& S=Sync(this);
    return S.bOpen;
}
void ACireHUD::SetSkillOfferOpen(bool bOpen)
{
    FOfferUI& S=Sync(this);
    const auto* Hero=PlayerOwner?Cast<ACireHero>(PlayerOwner->GetPawn()):nullptr;
    if(!Hero||Hero->Offers.IsEmpty())return;
    if(bOpen&&!S.bOpen){S.OpenedAt=GetWorld()->GetRealTimeSeconds();S.bPlayOpenSound=true;}
    S.bOpen=bOpen;S.bUserDeferred=!bOpen;S.bCombatHeld=false;
}

void ACireHUD::DrawSkillOffer(ACireHero* Hero,ACireController* Controller)
{
    FOfferUI& S=Sync(this);
    if(!Hero||Hero->Offers.IsEmpty())return;
    ResetTransform();
    const double Now=GetWorld()->GetRealTimeSeconds();
    const float Age=static_cast<float>(Now-S.OpenedAt);
    const bool bInteractive=Controller&&!bSettings&&!bEditLayout;
    FCireUIPainter P=Painter();
    const auto& Keys=UISettings.Keybindings;
#if !UE_BUILD_SHIPPING
    // Fixture hover (offscreen captures have no cursor): aim the logical mouse at the card.
    if(S.ForcedHover>=0&&S.CardPos.IsValidIndex(S.ForcedHover)){MX=S.CardPos[S.ForcedHover].X;MY=S.CardPos[S.ForcedHover].Y+60;}
#endif

    // Backdrop: the world stays visible but recedes.
    const float Fade=FMath::Clamp(Age/.25f,0.f,1.f);
    for(int32 I=0;I<10;++I)P.Rect(0,ViewH*I/10.f,ViewW,ViewH/10.f+1,FLinearColor(0,0,0,(.42f+.035f*FMath::Abs(I-4.5f))*Fade));

    const float TotalW=4*CardW+3*CardGap,X0=(ViewW-TotalW)*.5f;
    const float Y0=FMath::Clamp(ViewH*.15f+86.f,150.f,ViewH-CardH-130.f);

    // Header: kicker, title with shimmer, rule context.
    TArray<FString> Learned=Hero->Skills;
    const int32 SlotNumber=Learned.Num()+1;
    bool bFinalPassive=true;for(const FString& Id:Hero->Offers)bFinalPassive&=ACireHero::IsPassive(Id);
    bool bHasPassive=false;for(const FString& Id:Learned)bHasPassive|=ACireHero::IsPassive(Id);
    const Cires::RoleMask Roles=static_cast<Cires::RoleMask>(Cires::RoleBit(CireChampionProfiles::DraftRole(Hero))|CireChampionProfiles::SecondaryRoles(Hero));
    FString Pool;
    {
        const Cires::RoleMask Bits[]={Cires::RoleTank,Cires::RoleDamage,Cires::RoleSupport};const TCHAR* Names[]={TEXT("TANK"),TEXT("DPS"),TEXT("SUPPORT")};
        for(int32 R=0;R<3;++R)if(Roles&Bits[R])Pool+=(Pool.IsEmpty()?TEXT(""):TEXT(" + "))+FString(Names[R]);
    }
    {
        // The shared Level Up banner owns this space while it plays; the header fades in after it.
        static double BannerClearAt=0;if(CireBanners::IsShowing())BannerClearAt=Now;
        FCireUIPainter H=P;H.Alpha=Fade*FMath::Clamp(static_cast<float>(Now-BannerClearAt)/.3f,0.f,1.f);
        const FString Kicker=FString::Printf(TEXT("NEW ABILITY  |  SKILL %d OF 8  |  LEVEL %d"),SlotNumber,Hero->Level);
        H.Text(Kicker,(ViewW-H.TextWidth(Kicker,10,ECireFont::Heading))*.5f,Y0-66,10,FLinearColor(1.f,.9f,.7f,1),ECireFont::Heading,true,true);
        const bool bOpening=Learned.IsEmpty();
        const auto Primary=CireChampionProfiles::DraftRole(Hero);
        const TCHAR* PrimaryWord=Primary==Cires::SkillDraftRole::Tank?TEXT("Tank"):Primary==Cires::SkillDraftRole::Support?TEXT("Support"):TEXT("DPS");
        const FString Title=bOpening?FString::Printf(TEXT("Choose Your Opening %s Ability"),PrimaryWord):bFinalPassive?FString(TEXT("Choose Your Passive")):FString(TEXT("Choose Your Power"));
        const float TW=H.TextWidth(Title,24,ECireFont::Heading);
        H.Line((ViewW-TW)*.5f-150,Y0-38,(ViewW-TW)*.5f-16,Y0-38,Gold*FLinearColor(1,1,1,.8f),1.2f);
        H.Line((ViewW+TW)*.5f+16,Y0-38,(ViewW+TW)*.5f+150,Y0-38,Gold*FLinearColor(1,1,1,.8f),1.2f);
        H.Text(Title,(ViewW-TW)*.5f,Y0-52,24,BrightGold,ECireFont::Heading,true,true);
        const float Sweep=FMath::Clamp((Age-.2f)/.8f,0.f,1.f);
        if(Sweep>0&&Sweep<1)CireUIStyle::Glow(H,(ViewW-TW)*.5f-30+Sweep*(TW+60)-25,Y0-54,50,30,FLinearColor(1.f,.95f,.8f,.45f*(1.f-FMath::Abs(Sweep*2-1))));
        const FString Rule=bOpening?FString::Printf(TEXT("Your starting skill point: four %s actives that define your role. Passives and ultimates are offered from level 3."),*FString(PrimaryWord).ToUpper()):
            bFinalPassive?FString(TEXT("Final slot: all four choices are passives. Your passive shapes the whole build.")):
            !bHasPassive?FString::Printf(TEXT("Drawn from your %s pool. One or two passives appear until you take one."),*Pool):
            FString::Printf(TEXT("Drawn from your %s pool. Six actives, one passive and one ultimate."),*Pool);
        H.Text(Rule,(ViewW-H.TextWidth(Rule,10,ECireFont::Body))*.5f,Y0-22,10,Parchment*FLinearColor(1,1,1,.85f),ECireFont::Body,true,true);
    }

    // Cards.
    S.CardPos.SetNum(Hero->Offers.Num());
    int32 NewHover=-1;
    for(int32 I=0;I<FMath::Min(4,Hero->Offers.Num());++I)
    {
        const FString& Id=Hero->Offers[I];
        const FSkillFacts F=Facts(Id);
        const float T=FMath::Clamp((Age-.08f*I)/.38f,0.f,1.f),Ease=1.f-FMath::Pow(1.f-T,3.f);
        const float X=X0+I*(CardW+CardGap);
        const bool bOver=bInteractive&&T>=1.f&&Hit(X,Y0,CardW,CardH);
        if(bOver)NewHover=I;
        const float Lift=bOver?-6.f:0.f;
        const float Y=Y0+(1.f-Ease)*40.f+Lift;
        S.CardPos[I]=FVector2D(X+CardW*.5f,Y+69);
        FCireUIPainter C=P;C.Alpha=Ease;
        const FLinearColor Accent=F.bUltimate?BrightGold:F.bPassive?Violet:CireAbilityIcons::Accent(Id);
        // Frame by type.
        if(F.bUltimate){CireUIStyle::Glow(C,X-10,Y-10,CardW+20,CardH+20,FLinearColor(1.f,.75f,.2f,.28f+.12f*FMath::Sin(static_cast<float>(Now)*3.f)));}
        if(bOver)CireUIStyle::Glow(C,X-8,Y-8,CardW+16,CardH+16,Accent*FLinearColor(1,1,1,.45f));
        CireUIStyle::Frame(C,X,Y,CardW,CardH,Accent,ECireFrame::Card);
        // School wash at the top of the card.
        for(int32 B=0;B<8;++B)C.Rect(X+3,Y+3+B*14,CardW-6,14,FLinearColor(Accent.R,Accent.G,Accent.B,.10f*(8-B)/8.f));
        if(F.bUltimate||bOver)
        {
            const FLinearColor Edge=bOver?FLinearColor(1.f,.86f,.55f,1):FLinearColor(.95f,.72f,.30f,1);
            C.Line(X-3,Y-3,X+CardW+3,Y-3,Edge,1.5f);C.Line(X-3,Y+CardH+3,X+CardW+3,Y+CardH+3,Edge,1.5f);
            C.Line(X-3,Y-3,X-3,Y+CardH+3,Edge,1.5f);C.Line(X+CardW+3,Y-3,X+CardW+3,Y+CardH+3,Edge,1.5f);
        }
        if(F.bPassive)
        {
            // Octagonal clipped corners with a violet inner line: the passive frame.
            const float K=16;const FLinearColor Cut(.01f,.01f,.015f,1);
            C.Tri({X-1,Y-1},{X+K,Y-1},{X-1,Y+K},Cut);C.Tri({X+CardW+1,Y-1},{X+CardW-K,Y-1},{X+CardW+1,Y+K},Cut);
            C.Tri({X-1,Y+CardH+1},{X+K,Y+CardH+1},{X-1,Y+CardH-K},Cut);C.Tri({X+CardW+1,Y+CardH+1},{X+CardW-K,Y+CardH+1},{X+CardW+1,Y+CardH-K},Cut);
            C.Line(X+K,Y+1,X+1,Y+K,Violet,1.5f);C.Line(X+CardW-K,Y+1,X+CardW-1,Y+K,Violet,1.5f);
            C.Line(X+K,Y+CardH-1,X+1,Y+CardH-K,Violet,1.5f);C.Line(X+CardW-K,Y+CardH-1,X+CardW-1,Y+CardH-K,Violet,1.5f);
        }
        // Type ribbon + key badge.
        const FString Type=F.bUltimate?TEXT("ULTIMATE"):F.bPassive?TEXT("PASSIVE"):TEXT("ACTIVE");
        const float RW=C.TextWidth(Type,9,ECireFont::Heading)+26;
        C.Rect(X+(CardW-RW)*.5f,Y+10,RW,17,FLinearColor(Accent.R*.28f,Accent.G*.28f,Accent.B*.28f,.95f));
        C.Line(X+(CardW-RW)*.5f,Y+27,X+(CardW+RW)*.5f,Y+27,Accent,1.2f);
        C.Text(Type,X+(CardW-C.TextWidth(Type,9,ECireFont::Heading))*.5f,Y+12,9,Accent*1.15f,ECireFont::Heading,false,true);
        const FString Key=Keys.Label(CireKeybindings::SlotAction(1,I+1));
        if(!Key.IsEmpty())
        {
            C.Rect(X+10,Y+9,22,20,FLinearColor(0,0,0,.6f));C.Line(X+10,Y+29,X+32,Y+29,Gold,1);
            C.Text(Key,X+21-C.TextWidth(Key,11,ECireFont::Numbers)*.5f,Y+11,11,Parchment,ECireFont::Numbers,false,true);
        }
        // Big icon.
        const float IS=70,IX=X+(CardW-IS)*.5f,IY=Y+34;
        CireUIStyle::Glow(C,IX-12,IY-12,IS+24,IS+24,Accent*FLinearColor(1,1,1,.35f));
        FCireIconSlot Slot;Slot.IconId=Id;Slot.IconTexture=CireAbilityIcons::Texture(Id);Slot.Tint=Accent;
        Slot.Kind=F.bUltimate?ECireSlotKind::Ultimate:F.bPassive?ECireSlotKind::Passive:ECireSlotKind::Normal;Slot.bHover=bOver;Slot.bGlow=F.bUltimate;
        CireUIStyle::IconSlot(C,IX,IY,IS,Slot,Now);
        // Name + roles.
        const FString Name=ACireHero::SkillName(Id);
        float NameSize=15;while(NameSize>11&&C.TextWidth(Name,NameSize,ECireFont::Bold)>CardW-20)NameSize-=.5f;
        C.Text(Name,X+(CardW-C.TextWidth(Name,NameSize,ECireFont::Bold))*.5f,Y+110,NameSize,Parchment,ECireFont::Bold,false,true);
        RoleChips(C,Id,X+CardW*.5f,Y+131);
        // Stats grid.
        const float GY=Y+151,GW=(CardW-20)/4.f;
        const FString Cost=F.bPassive?TEXT("-"):F.Mana>0&&F.Energy>0?FString::Printf(TEXT("%.0f/%.0f"),F.Mana,F.Energy):F.Mana>0?FString::Printf(TEXT("%.0f MP"),F.Mana):F.Energy>0?FString::Printf(TEXT("%.0f EN"),F.Energy):TEXT("FREE");
        const FString CD=F.bPassive?TEXT("-"):F.Cooldown>0?FString::Printf(TEXT("%.0fs"),F.Cooldown):TEXT("-");
        const FString Range=F.bPassive||F.Target==ECireTargetKind::Self?TEXT("SELF"):F.Range>0?(F.Range<=320?FString::Printf(TEXT("MELEE")):FString::Printf(TEXT("%.0f m"),F.Range/100.f)):TEXT("-");
        const struct {const TCHAR* Cap;FString Val;FLinearColor Col;} Cells[]={
            {TEXT("COST"),Cost,F.Mana>0?FLinearColor(.45f,.65f,1.f,1):F.Energy>0?CireUIColors::Energy:Muted},{TEXT("COOLDOWN"),CD,Parchment},
            {TEXT("RANGE"),Range,Parchment},{TEXT("TARGET"),TargetWord(F),TargetColor(F)}};
        for(int32 K=0;K<4;++K)
        {
            const float CX=X+10+K*GW;
            C.Rect(CX+1,GY,GW-2,34,FLinearColor(0,0,0,.42f));
            C.Text(Cells[K].Cap,CX+(GW-C.TextWidth(Cells[K].Cap,6.5f,ECireFont::Heading))*.5f,GY+4,6.5f,Muted,ECireFont::Heading,false,false);
            C.Text(Cells[K].Val,CX+(GW-C.TextWidth(Cells[K].Val,10,ECireFont::Numbers))*.5f,GY+16,10,Cells[K].Col,ECireFont::Numbers,false,true);
        }
        // Description.
        const TArray<FString> Syn=Synergies(Id,Learned);
        C.Wrapped(F.Body,X+12,Y+193,CardW-24,9.5f,Parchment*FLinearColor(1,1,1,.92f),Syn.Num()>0?3:5,ECireFont::Body,3.f);
        // Synergy.
        if(Syn.Num()>0)
        {
            const float SY=Y+CardH-70;
            C.Rect(X+10,SY,CardW-20,1,Teal*FLinearColor(1,1,1,.5f));
            C.Text(TEXT("SYNERGY"),X+12,SY+5,7,Teal,ECireFont::Heading,false,false);
            C.Wrapped(Syn[0],X+12,SY+17,CardW-24,8.5f,FLinearColor(.62f,.95f,.85f,1),2,ECireFont::Body,2.f);
        }
        // Footer: slot indicator + learn prompt.
        const int32 KitSlot=KitSlotFor(Hero,Id);
        const FString SlotText=F.bUltimate?TEXT("ULTIMATE SLOT"):F.bPassive?TEXT("PASSIVE SLOT"):FString::Printf(TEXT("ACTIVE SLOT %d / 6"),KitSlot+1);
        C.Rect(X+8,Y+CardH-30,CardW-16,22,FLinearColor(0,0,0,.35f));
        C.Text(SlotText,X+16,Y+CardH-25,8,Accent*1.1f,ECireFont::Heading,false,false);
        const FString Learn=bOver?TEXT("CLICK TO LEARN"):TEXT("LEARN");
        C.Text(Learn,X+CardW-16-C.TextWidth(Learn,8.5f,ECireFont::Heading),Y+CardH-25,8.5f,bOver?BrightGold:Gold,ECireFont::Heading,false,false);
        // Tooltip with everything.
        FString TipText=F.Full;
        TipText+=FString::Printf(TEXT(" Targeting: %s."),*F.TargetLabel);
        if(Syn.Num()>0)TipText+=TEXT(" Synergy: ")+FString::Join(Syn,TEXT(" "));
        TipText+=TEXT(" ")+SlotText.Left(1)+SlotText.RightChop(1).ToLower()+TEXT(". Learned skills cannot be swapped out later.");
        Tip(Name,TipText,X,Y,CardW,CardH);
        if(bOver&&Clicked&&Controller){Controller->ServerAction(3,I,nullptr);Clicked=false;}
    }
    if(NewHover!=S.Hovered){if(NewHover>=0)if(auto* Tick=LoadObject<USoundBase>(nullptr,TEXT("/Game/UI/Draft/Sounds/S_SkillHover.S_SkillHover")))
        if(!UISettings.bMuteAudio)UGameplayStatics::PlaySound2D(this,Tick,UISettings.MasterVolume*UISettings.UIVolume*.35f);S.Hovered=NewHover;}

    // Learned-kit strip: six actives, passive, ultimate; the hovered card's slot pulses.
    {
        const float SY=Y0+CardH+20,SS=36,Gap=6,StripW=8*SS+7*Gap+10+150+120;
        const float SX=(ViewW-StripW)*.5f;
        FCireUIPainter K=P;K.Alpha=Fade;
        CireUIStyle::Frame(K,SX,SY-8,StripW,SS+16,Gold,ECireFrame::Inset);
        K.Text(TEXT("YOUR KIT"),SX+12,SY+1,10,Gold,ECireFont::Heading,false,true);
        K.Text(FString::Printf(TEXT("%d / 8 learned"),Learned.Num()),SX+12,SY+17,9,Muted,ECireFont::Body,false,false);
        S.SlotPos.SetNum(8);
        const int32 Target=S.Hovered>=0&&Hero->Offers.IsValidIndex(S.Hovered)?KitSlotFor(Hero,Hero->Offers[S.Hovered]):-1;
        for(int32 I=0;I<8;++I)
        {
            const float X=SX+110+I*(SS+Gap)+(I>=6?10.f*(I-5):0.f);
            S.SlotPos[I]=FVector2D(X+SS*.5f,SY+SS*.5f);
            const FString Id=KitIdAt(Hero,I);
            FCireIconSlot Slot;Slot.IconId=Id;Slot.IconTexture=Id.IsEmpty()?nullptr:CireAbilityIcons::Texture(Id);Slot.Tint=CireAbilityIcons::Accent(Id);
            Slot.Kind=I==7?ECireSlotKind::Ultimate:I==6?ECireSlotKind::Passive:ECireSlotKind::Normal;Slot.bEmpty=Id.IsEmpty();
            Slot.bGlow=I==Target;
            CireUIStyle::IconSlot(K,X,SY,SS,Slot,Now);
            if(Id.IsEmpty())K.Text(I==7?TEXT("ULT"):I==6?TEXT("PAS"):FString::FromInt(I+1),X+(SS-K.TextWidth(I==7?TEXT("ULT"):I==6?TEXT("PAS"):FString::FromInt(I+1),8,ECireFont::Heading))*.5f,SY+13,8,Muted*FLinearColor(1,1,1,.7f),ECireFont::Heading,false,false);
            else Tip(ACireHero::SkillName(Id),ACireHero::SkillDescription(Id),X,SY,SS,SS);
        }
        // Decide later.
        const FString ToggleKey=Keys.Label(TEXT("ToggleSkillOffer"));
        const FString Later=ToggleKey.IsEmpty()?FString(TEXT("DECIDE LATER")):FString::Printf(TEXT("DECIDE LATER [%s]"),*ToggleKey.ToUpper());
        const float BW=140,BX=SX+StripW-BW-10,BY=SY+2;
        const bool bOver=bInteractive&&Hit(BX,BY,BW,SS-4);
        CireUIStyle::Button(K,BX,BY,BW,SS-4,Later,bOver?ECireButtonState::Hover:ECireButtonState::Normal,Gold,9.5f);
        Tip(TEXT("Decide later"),TEXT("Hide the choice and keep fighting. A pulsing reminder stays above your action bar; press the key or click it to choose. While the choice is hidden your number keys cast skills."),BX,BY,BW,SS-4);
        if(bOver&&Clicked){Clicked=false;SetSkillOfferOpen(false);}
    }
    ResetTransform();
}

void ACireHUD::DrawSkillOfferExtras(ACireHero* Hero,ACireController* Controller)
{
    FOfferUI& S=Sync(this);
    if(!Hero||!GetWorld())return;
    const double Now=GetWorld()->GetRealTimeSeconds();
    auto Play=[&](const TCHAR* Path,float Volume)
    {
        if(UISettings.bMuteAudio)return;
        if(auto* Sound=LoadObject<USoundBase>(nullptr,Path))UGameplayStatics::PlaySound2D(this,Sound,UISettings.MasterVolume*UISettings.UIVolume*Volume);
    };
    if(S.bPlayOpenSound){S.bPlayOpenSound=false;Play(TEXT("/Game/UI/Draft/Sounds/S_SkillOffer.S_SkillOffer"),.6f);}
    if(S.bPickSound){S.bPickSound=false;Play(TEXT("/Game/UI/Draft/Sounds/S_SkillLearned.S_SkillLearned"),.9f);}
    const bool bTyping=Controller&&Controller->bChatInput;
    if(!bTyping&&!bSettings&&!bEditLayout&&Hero->Offers.Num()>0&&UISettings.Keybindings.WasPressed(PlayerOwner,TEXT("ToggleSkillOffer")))
        SetSkillOfferOpen(!S.bOpen);
    ResetTransform();
    FCireUIPainter P=Painter();

    // Pick animation: the chosen card blooms, then its icon flies into the kit slot.
    const float PT=static_cast<float>(Now-S.PickAt);
    if(PT>=0&&PT<PickSeconds&&!S.PickedId.IsEmpty())
    {
        const float Fade=1.f-FMath::Clamp((PT-.55f)/.4f,0.f,1.f);
        P.Rect(0,0,ViewW,ViewH,FLinearColor(0,0,0,.45f*Fade));
        const FVector2D From=S.PickFrom;
        const float Fly=FMath::Clamp((PT-.25f)/.55f,0.f,1.f),E=FMath::SmoothStep(0.f,1.f,Fly);
        const FVector2D At=FMath::Lerp(From,S.PickTo,E)+FVector2D(0,-80.f*FMath::Sin(E*PI));
        const float Size=FMath::Lerp(96.f,40.f,E)*(1.f+.25f*FMath::Sin(FMath::Clamp(PT/.25f,0.f,1.f)*PI));
        const FLinearColor Accent=ACireHero::IsUltimate(S.PickedId)?BrightGold:ACireHero::IsPassive(S.PickedId)?Violet:CireAbilityIcons::Accent(S.PickedId);
        // The kit strip stays while the icon flies home, then fades with the backdrop.
        if(S.SlotPos.Num()==8)
        {
            FCireUIPainter K=P;K.Alpha=Fade;const float SS=36;
            for(int32 I=0;I<8;++I)
            {
                const FString Id=KitIdAt(Hero,I);const bool bLanding=I==S.PickedSlot;
                FCireIconSlot Slot;Slot.IconId=Id;Slot.IconTexture=Id.IsEmpty()?nullptr:CireAbilityIcons::Texture(Id);Slot.Tint=CireAbilityIcons::Accent(Id);
                Slot.Kind=I==7?ECireSlotKind::Ultimate:I==6?ECireSlotKind::Passive:ECireSlotKind::Normal;
                Slot.bEmpty=Id.IsEmpty()||(bLanding&&Fly<1.f);Slot.Flash=bLanding?FMath::Clamp(1.f-(PT-.8f)/.15f,0.f,1.f):0.f;
                CireUIStyle::IconSlot(K,S.SlotPos[I].X-SS*.5f,S.SlotPos[I].Y-SS*.5f,SS,Slot,Now);
            }
        }
        // Burst rays at the card.
        const float Burst=FMath::Clamp(PT/.35f,0.f,1.f);
        if(Burst<1)for(int32 R=0;R<12;++R)
        {
            const float A=R*PI/6+PT*2.f,L=40+140*Burst;
            P.Line(From.X+FMath::Cos(A)*30,From.Y+FMath::Sin(A)*30,From.X+FMath::Cos(A)*L,From.Y+FMath::Sin(A)*L,Accent*FLinearColor(1,1,1,1.f-Burst),2.f);
        }
        CireUIStyle::Glow(P,At.X-Size,At.Y-Size,Size*2,Size*2,Accent*FLinearColor(1,1,1,.8f));
        // Trail.
        for(int32 K=1;K<=5;++K)
        {
            const float TE=FMath::SmoothStep(0.f,1.f,FMath::Max(0.f,Fly-K*.05f));
            const FVector2D TA=FMath::Lerp(From,S.PickTo,TE)+FVector2D(0,-80.f*FMath::Sin(TE*PI));
            P.Disc(TA.X,TA.Y,Size*.18f*(1.f-K*.15f),Accent*FLinearColor(1,1,1,.5f*(1.f-K*.18f)),12);
        }
        FCireIconSlot Slot;Slot.IconId=S.PickedId;Slot.IconTexture=CireAbilityIcons::Texture(S.PickedId);Slot.Tint=Accent;
        Slot.Kind=ACireHero::IsUltimate(S.PickedId)?ECireSlotKind::Ultimate:ACireHero::IsPassive(S.PickedId)?ECireSlotKind::Passive:ECireSlotKind::Normal;
        Slot.Flash=1.f-Burst;
        CireUIStyle::IconSlot(P,At.X-Size*.5f,At.Y-Size*.5f,Size,Slot,Now);
        const FString Name=FString(TEXT("LEARNED  "))+ACireHero::SkillName(S.PickedId).ToUpper();
        FCireUIPainter T=P;T.Alpha=Fade;
        T.Text(Name,(ViewW-T.TextWidth(Name,20,ECireFont::Heading))*.5f,From.Y-150,20,Accent*1.2f,ECireFont::Heading,true,true);
    }

    CireCrowdControl::RegisterCastProvider(); // champion-draft: hero cast bars are drawn by the shared CireCasts bars
    // Deferred reminder: pulsing, above the action bar; click or key to open.
    if(Hero->Offers.Num()>0&&!S.bOpen&&!bSettings)
    {
        const auto Bar=PanelRect(TEXT("Skills"));
        const float W=330,H=52,X=(ViewW-W)*.5f,Y=FMath::Clamp(FMath::Min(Bar.Y,ActionBarsTop())-H-48,60.f,ViewH-H-10); // wow-ui: above the extra bars and movement hint
        const float Pulse=.5f+.5f*FMath::Sin(static_cast<float>(Now)*4.f);
        CireUIStyle::Glow(P,X-10,Y-10,W+20,H+20,FLinearColor(1.f,.78f,.25f,.25f+.35f*Pulse));
        const bool bOver=!bEditLayout&&Hit(X,Y,W,H);
        CireUIStyle::Frame(P,X,Y,W,H,BrightGold,ECireFrame::Card);
        const FLinearColor Edge(1.f,.8f,.35f,.35f+.6f*Pulse);
        P.Line(X-2,Y-2,X+W+2,Y-2,Edge,1.5f);P.Line(X-2,Y+H+2,X+W+2,Y+H+2,Edge,1.5f);P.Line(X-2,Y-2,X-2,Y+H+2,Edge,1.5f);P.Line(X+W+2,Y-2,X+W+2,Y+H+2,Edge,1.5f);
        for(int32 I=0;I<FMath::Min(4,Hero->Offers.Num());++I)
        {
            FCireIconSlot Slot;Slot.IconId=Hero->Offers[I];Slot.IconTexture=CireAbilityIcons::Texture(Hero->Offers[I]);Slot.Tint=CireAbilityIcons::Accent(Hero->Offers[I]);
            Slot.Kind=ACireHero::IsUltimate(Hero->Offers[I])?ECireSlotKind::Ultimate:ACireHero::IsPassive(Hero->Offers[I])?ECireSlotKind::Passive:ECireSlotKind::Normal;
            CireUIStyle::IconSlot(P,X+8+I*30,Y+12,28,Slot,Now);
        }
        const FString Key=UISettings.Keybindings.Label(TEXT("ToggleSkillOffer"));
        P.Text(TEXT("NEW ABILITY READY"),X+134,Y+9,11,BrightGold*(0.85f+.15f*Pulse),ECireFont::Heading,false,true);
        const FString Sub=S.bCombatHeld?FString::Printf(TEXT("Opens after combat  |  [%s] now"),*Key.ToUpper()):FString::Printf(TEXT("[%s] or click to choose"),*Key.ToUpper());
        P.Text(Sub,X+134,Y+28,9,Parchment,ECireFont::Body,false,true);
        Tip(TEXT("New ability ready"),TEXT("You have a skill choice waiting. It waits while you fight; open it when you have a moment. Your number keys keep casting until you open it."),X,Y,W,H);
        if(bOver&&Clicked){Clicked=false;SetSkillOfferOpen(true);}
    }

#if !UE_BUILD_SHIPPING
    // ---------- Fixture: -CireSkillOfferGallery (Tools/RunSkillOfferGallery.py) ----------
    static const bool bGallery=FParse::Param(FCommandLine::Get(),TEXT("CireSkillOfferGallery"));
    if(bGallery&&!S.bGalleryDone&&GetNetMode()==NM_Standalone)
    {
        auto* Mode=GetWorld()->GetAuthGameMode<ACireGameMode>();
        if(Mode){Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;}
        const auto Setup=[&](const TCHAR* Profile,std::initializer_list<const TCHAR*> LearnedIds,std::initializer_list<const TCHAR*> OfferIds)
        {
            if(!Hero->bDrafted||Hero->ChampionProfileId!=Profile){Hero->bDrafted=false;Hero->DraftProfile(Profile);}
            Hero->Skills.Reset();Hero->Cooldowns.Reset();Hero->Progression.LearnedSkills.clear();
            const auto Pool=Cires::StarterSkillPool();
            const auto Def=[&](const TCHAR* Id){for(const auto& D:Pool)if(FString(UTF8_TO_TCHAR(D.Id.c_str()))==Id)return D;return Cires::SkillDefinition{};};
            for(const TCHAR* Id:LearnedIds){Hero->Skills.Add(Id);Hero->Cooldowns.Add(0);Hero->Progression.LearnedSkills.push_back(Def(Id));}
            Hero->Progression.Level=Hero->Skills.Num()==0?1:FMath::Max(Hero->Progression.Level,Cires::BreakpointForSkill(Hero->Skills.Num()));
            Hero->Progression.NextAugmentLevel=Cires::BreakpointForSkill(Hero->Skills.Num());Hero->Recalculate(true);
            Hero->Offers.Reset();Hero->CurrentOffer={};Hero->CurrentOffer.BreakpointLevel=Hero->Progression.NextAugmentLevel;
            for(const TCHAR* Id:OfferIds){Hero->Offers.Add(Id);Hero->CurrentOffer.Choices.push_back(Def(Id));}
            Hero->bBot=false;Hero->bAutoAttack=false;
        };
        struct FShot{const TCHAR* Name;int32 Hover;bool bCollapse;bool bPick;};
        static const FShot Shots[]={{TEXT("01_normal_offer_hover"),1,false,false},{TEXT("02_ultimate_offer"),0,false,false},
            {TEXT("03_passive_only_offer"),2,false,false},{TEXT("04_deferred_reminder"),-1,true,false},{TEXT("05_pick_animation"),-1,false,true},
            {TEXT("06_opening_offer_tank"),0,false,false},{TEXT("07_opening_offer_support"),-1,false,false}};
        if(S.GalleryStage<0)
        {
            S.GalleryStage=0;S.GalleryAt=S.GalleryStarted=Now;
            S.GalleryDir=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("SkillOfferGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%SZ"))));
            IFileManager::Get().MakeDirectory(*S.GalleryDir,true);
        }
        if(S.GalleryStage<static_cast<int32>(UE_ARRAY_COUNT(Shots))&&Now-S.GalleryAt<.05)
        {
            switch(S.GalleryStage)
            {
            case 0: Setup(TEXT("wizard"),{TEXT("ember_lance"),TEXT("cinder_cone")},{TEXT("chain_spark"),TEXT("frost_bind"),TEXT("deep_reserves"),TEXT("restoring_light")});break;
            case 1: Setup(TEXT("knight"),{TEXT("iron_guard"),TEXT("war_cry"),TEXT("shield_slam"),TEXT("cleaving_strike"),TEXT("stone_skin"),TEXT("second_wind")},
                        {TEXT("challenge_of_iron"),TEXT("summoned_wall"),TEXT("last_stand"),TEXT("bastion_of_dawn")});break;
            case 2: Setup(TEXT("keeper_of_light"),{TEXT("restoring_light"),TEXT("sanctuary"),TEXT("purify"),TEXT("chain_spark"),TEXT("iron_guard"),TEXT("frost_bind"),TEXT("renewal")},
                        {TEXT("soul_conduit"),TEXT("stone_skin"),TEXT("battle_rhythm"),TEXT("deep_reserves")});break;
            case 3: Setup(TEXT("ranger"),{TEXT("piercing_shot")},{TEXT("shadow_step"),TEXT("venom_ground"),TEXT("battle_rhythm"),TEXT("grave_line")});break;
            case 5: Setup(TEXT("knight"),{},{TEXT("shield_slam"),TEXT("war_cry"),TEXT("iron_guard"),TEXT("summoned_wall")});break;
            case 6: Setup(TEXT("scholar"),{},{TEXT("restoring_light"),TEXT("sanctuary"),TEXT("purify"),TEXT("protection_dome")});break;
            default: Setup(TEXT("ranger"),{TEXT("piercing_shot")},{TEXT("shadow_step"),TEXT("venom_ground"),TEXT("battle_rhythm"),TEXT("grave_line")});break;
            }
            S.Key.Reset();
        }
        if(S.GalleryStage<static_cast<int32>(UE_ARRAY_COUNT(Shots)))
        {
            const FShot& Shot=Shots[S.GalleryStage];
            const float StageAge=static_cast<float>(Now-S.GalleryAt);
            static int32 CollapsedStage=-1;
            if(StageAge>.3f&&Shot.bCollapse&&CollapsedStage!=S.GalleryStage){CollapsedStage=S.GalleryStage;SetSkillOfferOpen(false);}
            // Fake hover: move the logical mouse over the requested card.
            S.ForcedHover=Shot.Hover;
            if(Shot.bPick&&StageAge>1.6f&&!Hero->Offers.IsEmpty()){S.CardPos.SetNum(4);Hero->Learn(0);}
            const float ShotTime=Shot.bPick?2.05f:2.4f;
            if(S.ShotAt==0&&StageAge>ShotTime&&Now-S.GalleryStarted>4.0)
            {
                const FString File=FPaths::Combine(S.GalleryDir,FString(Shot.Name)+TEXT(".png"));
                FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);S.GalleryFiles.Add(File);S.ShotAt=Now;
                UE_LOG(LogCireSkillOffer,Display,TEXT("CIRE_SKILL_OFFER_SHOT %s offers=%d open=%d"),*File,Hero->Offers.Num(),S.bOpen?1:0);
            }
            if(S.ShotAt>0&&Now-S.ShotAt>1.0){++S.GalleryStage;S.ShotAt=0;S.GalleryAt=Now;}
        }
        else
        {
            S.bGalleryDone=true;S.ForcedHover=-1;
            for(const FString& File:S.GalleryFiles)S.bGalleryPass&=IFileManager::Get().FileSize(*File)>20000;
            S.bGalleryPass&=S.GalleryFiles.Num()==static_cast<int32>(UE_ARRAY_COUNT(Shots));
            UE_LOG(LogCireSkillOffer,Display,TEXT("CIRE_SKILL_OFFER_GALLERY_%s captures=%d directory=%s"),S.bGalleryPass?TEXT("PASS"):TEXT("FAIL"),S.GalleryFiles.Num(),*S.GalleryDir);
            FPlatformMisc::RequestExitWithStatus(false,S.bGalleryPass?0:1);
        }
        if(!S.bGalleryDone&&Now-S.GalleryStarted>120){S.bGalleryDone=true;UE_LOG(LogCireSkillOffer,Error,TEXT("CIRE_SKILL_OFFER_GALLERY_FAIL timeout"));FPlatformMisc::RequestExitWithStatus(false,1);}
    }
#endif
    ResetTransform();
}
