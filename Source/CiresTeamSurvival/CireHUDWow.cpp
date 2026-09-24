// WoW-style interface layer for ACireHUD: crisp TTF text, unit frames with portraits,
// boss frames, threat meter and aggro alerts, unit tooltips and the level-up burst.
// Everything here is local presentation; it reads replicated/authoritative state and
// never changes combat rules.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireTargeting.h"
#include "CireNPCState.h"
#include "CireUIStyle.h"
#include "CireBanners.h"
#include "CireEnvironmentProps.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Engine/World.h"
#include "EngineFontServices.h"
#include "EngineUtils.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/SlateFontInfo.h"
#include "GlobalRenderResources.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Sound/SoundBase.h"
#include "CireAudio.h" // audio: recorded cues for level-up, aggro and phase banners
#include "UObject/ConstructorHelpers.h"

namespace
{
const FLinearColor Gold(.77f,.61f,.34f,1.f), Parchment(.91f,.90f,.83f,1.f), Muted(.50f,.57f,.59f,1.f);
const FLinearColor Teal(.20f,.71f,.59f,1.f), Red(.75f,.20f,.23f,1.f), Purple(.66f,.46f,.83f,1.f);
// WoW reaction colours: hostile red, neutral yellow, friendly green; unit health is green.
const FLinearColor Hostile(.95f,.20f,.16f,1.f), Neutral(1.f,.86f,.18f,1.f), Friendly(.16f,.92f,.24f,1.f);
const FLinearColor HealthGreen(.10f,.74f,.12f,1.f), ManaBlue(.12f,.36f,.95f,1.f), CastGold(1.f,.70f,.05f,1.f);
const FLinearColor Silver(.76f,.80f,.86f,1.f), WowGold(1.f,.82f,.0f,1.f), Orange(1.f,.55f,.10f,1.f);

enum class ERole : uint8 { None, Tank, Bruiser, Caster, Ranged, Healer };
struct FAbility { FString Id, Name, Text; };
struct FInsight
{
    FString Name, Subtitle, RoleName, ClassName;
    ERole Role = ERole::None;
    int32 Level = 0, Tier = 0;
    int32 Class = 0;    // 0 normal, 1 rare (armored), 2 elite, 3 boss
    int32 Reaction = 0; // 0 hostile, 1 neutral, 2 friendly
    bool bSelf = false, bDead = false, bHero = false, bMonster = false, bConstruct = false;
    float HP = 0, MaxHP = 1, MP = 0, MaxMP = 0;
    AActor* Victim = nullptr;
    FString VictimLine, Casting, Status;
    float CastProgress = 0, CastRemaining = 0;
    bool bInterruptible = false;
    TArray<FAbility> Abilities;
};
FString Short(const FString& Name,int32 Max=22) { return Name.Len()>Max ? Name.Left(Max-2)+TEXT("..") : Name; }
float Frac(float A,float B) { return B>0.f?FMath::Clamp(A/B,0.f,1.f):0.f; }
float ServerNow(const UWorld* World)
{
    const auto* State=World?World->GetGameState<ACireGameState>():nullptr;
    return State?State->GetServerWorldTimeSeconds():World?World->GetTimeSeconds():0.f;
}
bool IsTank(const ACireHero* H)
{
    return H&&(H->ProfileThreatRole.IsEmpty()?H->Archetype==0:H->ProfileThreatRole==TEXT("tank"));
}
ERole HeroRole(const ACireHero* H)
{
    if(!H)return ERole::None;
    if(IsTank(H))return ERole::Tank;
    if(H->ProfileThreatRole==TEXT("healer")||(H->ProfileThreatRole.IsEmpty()&&H->Archetype==2))return ERole::Healer;
    if(H->Archetype==3&&H->ProfileThreatRole.IsEmpty())return ERole::Caster;
    return H->IsRangedBasicAttack()?ERole::Ranged:ERole::Bruiser;
}
/// NPC role, classification, abilities, casts and threat come from the replicated
// UCireNPCState read API (Docs/NPCs.md); valid on the server and on LAN clients.
ERole NpcRole(const ACireMonster* M)
{
    if(!M)return ERole::None;
    switch(M->GetNPCRole())
    {
    case ECireNPCRole::Tank:return ERole::Tank;
    case ECireNPCRole::Caster:return ERole::Caster;
    case ECireNPCRole::Ranged:return ERole::Ranged;
    default:return ERole::Bruiser;
    }
}
const TCHAR* RoleLabel(ERole R)
{
    switch(R){case ERole::Tank:return TEXT("Tank");case ERole::Bruiser:return TEXT("Bruiser");case ERole::Caster:return TEXT("Caster");
        case ERole::Ranged:return TEXT("Ranged");case ERole::Healer:return TEXT("Healer");default:return TEXT("");}
}
FLinearColor RoleTint(ERole R)
{
    switch(R){case ERole::Tank:return FLinearColor(.35f,.62f,1.f,1);case ERole::Bruiser:return FLinearColor(1.f,.36f,.30f,1);
        case ERole::Caster:return FLinearColor(.78f,.52f,1.f,1);case ERole::Ranged:return FLinearColor(.55f,.9f,.35f,1);
        case ERole::Healer:return FLinearColor(.4f,1.f,.6f,1);default:return Muted;}
}
const TCHAR* RoleIcon(ERole R)
{
    switch(R){case ERole::Tank:return TEXT("role0");case ERole::Ranged:return TEXT("role1");case ERole::Healer:return TEXT("role2");
        case ERole::Caster:return TEXT("role_caster");case ERole::Bruiser:return TEXT("executioners_verdict");default:return TEXT("");}
}
FString RoleExplain(ERole R,bool bNpc)
{
    switch(R)
    {
    case ERole::Tank:return bNpc?TEXT("Tank: armored and slow. Provokes champions and guards injured allies; kill its pack or ignore it."):TEXT("Tank: holds enemy attention. Losing aggro is the tank's warning.");
    case ERole::Bruiser:return bNpc?TEXT("Bruiser: melee damage dealer. Charges distant targets and slams in telegraphed cones."):TEXT("Melee damage dealer.");
    case ERole::Caster:return bNpc?TEXT("Caster: keeps its distance, casts with a visible cast bar. Interrupt its heals and bolts."):TEXT("Spellcaster.");
    case ERole::Ranged:return bNpc?TEXT("Ranged: shoots from range and leaps away from melee. Aimed shots fly where you stood."):TEXT("Ranged damage dealer.");
    case ERole::Healer:return TEXT("Healer: restores allies. Healing generates threat on engaged enemies.");
    default:return FString();
    }
}
const TCHAR* AbilityIcon(ECireNPCAbilityKind Kind,ERole Role)
{
    switch(Kind)
    {
    case ECireNPCAbilityKind::Melee:return TEXT("basic");
    case ECireNPCAbilityKind::Projectile:return Role==ERole::Ranged?TEXT("piercing_shot"):TEXT("role_caster");
    case ECireNPCAbilityKind::Cone:return TEXT("cleaving_strike");
    case ECireNPCAbilityKind::TargetCircle:return TEXT("venom_ground");
    case ECireNPCAbilityKind::SelfCircle:return TEXT("cataclysm");
    case ECireNPCAbilityKind::Charge:return TEXT("shadow_step");
    case ECireNPCAbilityKind::Guard:return TEXT("iron_guard");
    case ECireNPCAbilityKind::Provoke:return TEXT("war_cry");
    case ECireNPCAbilityKind::Rally:return TEXT("battle_rhythm");
    case ECireNPCAbilityKind::Enrage:return TEXT("executioners_verdict");
    case ECireNPCAbilityKind::HealAlly:return TEXT("restoring_light");
    case ECireNPCAbilityKind::ShieldWall:return TEXT("stone_skin");
    case ECireNPCAbilityKind::Disengage:return TEXT("chain_spark");
    default:return TEXT("basic");
    }
}
TArray<FAbility> NpcAbilities(const ACireMonster* M)
{
    TArray<FAbility> Out;
    if(!M)return Out;
    if(M->bArmoredEscort)
    {
        Out.Add({TEXT("runic_wall"),TEXT("Armored March"),TEXT("Ignores threat and taunts. Marches to your keep, breaching summoned walls. Kill it before it leaks.")});
        return Out;
    }
    const ERole Role=NpcRole(M);
    if(M->NPCState)for(const FCireNPCAbilityInfo& A:M->NPCState->Abilities())
    {
        FString Facts=A.TypeLabel;
        if(A.CastTime>0)Facts+=FString::Printf(TEXT(", %.1fs cast"),A.CastTime);
        if(A.Cooldown>0)Facts+=FString::Printf(TEXT(", %.0fs cooldown"),A.Cooldown);
        Out.Add({AbilityIcon(A.Kind,Role),A.Name,Facts+TEXT(". ")+A.Description});
    }
    if(M->IsLaneBoss())Out.Add({TEXT("war_cry"),TEXT("Siege Boss"),TEXT("If it reaches your keep it costs 10 lives instead of 1.")});
    if(M->PackId>=0)Out.Add({TEXT("shadow_step"),TEXT("Pack Leash"),TEXT("Pulled more than 17m from its camp, the pack resets to full health.")});
    return Out;
}
FString NpcStatus(const ACireMonster* M,float Now)
{
    TArray<FString> Parts;
    if(const UCireNPCState* S=M?M->NPCState.Get():nullptr)
    {
        if(S->HasStatus(CireNPCStatus::Enraged))Parts.Add(TEXT("ENRAGED"));
        if(S->HasStatus(CireNPCStatus::Rallied))Parts.Add(TEXT("Rallied (+25% damage)"));
        if(S->HasStatus(CireNPCStatus::ShieldWall))Parts.Add(TEXT("Shield Wall"));
        if(S->HasStatus(CireNPCStatus::Guarded))Parts.Add(TEXT("Guarded by an ally"));
        if(S->HasStatus(CireNPCStatus::Provoking))Parts.Add(TEXT("Provoking"));
        if(S->HasStatus(CireNPCStatus::Charging))Parts.Add(TEXT("Charging"));
    }
    if(M&&M->SlowUntil>Now)Parts.Add(TEXT("Slowed"));
    if(M&&M->PoisonAreaCount>0)Parts.Add(FString::Printf(TEXT("Poisoned x%d"),M->PoisonAreaCount));
    return FString::Join(Parts,TEXT(", "));
}
FInsight Describe(UWorld* World,AActor* Actor,const ACireHero* Self)
{
    FInsight U;
    if(!IsValid(Actor))return U;
    U.bSelf=Actor==Self;
    const float Now=ServerNow(World);
    if(const auto* H=Cast<ACireHero>(Actor))
    {
        U.bHero=true;U.Name=H->HeroName;U.Level=H->Level;U.HP=H->Health;U.MaxHP=H->MaxHealth;U.MP=H->Mana;U.MaxMP=H->MaxMana;U.bDead=H->bDead;
        U.Reaction=Self&&H->TeamId==Self->TeamId?2:0;U.Role=HeroRole(H);U.RoleName=RoleLabel(U.Role);
        U.Subtitle=Cast<ACireSummon>(H)?TEXT("<Summoned ally>"):H->TeamId==0?TEXT("<Ember Company>"):TEXT("<Dusk Company>");
        U.ClassName=FString::Printf(TEXT("Level %d %s"),H->Level,*U.RoleName);
        U.Victim=H->Target;
        if(const auto* T=Cast<ACireHero>(H->Target))U.VictimLine=T==Self?TEXT("You"):T->HeroName;
        else if(const auto* M=Cast<ACireMonster>(H->Target))U.VictimLine=M->GetNPCDisplayName();
        if(U.bDead)U.Status=TEXT("Fallen");
        else if(H->TauntUntil>Now)U.Status=TEXT("Commanding presence (taunting)");
        else if(H->ShieldUntil>Now)U.Status=TEXT("Guarded: 40% damage reduction");
        for(const FString& Id:H->Skills)U.Abilities.Add({Id,ACireHero::SkillName(Id),ACireHero::SkillDescription(Id)});
    }
    else if(const auto* M=Cast<ACireMonster>(Actor))
    {
        U.bMonster=true;U.Name=M->GetNPCDisplayName();U.HP=M->Health;U.MaxHP=M->MaxHealth;U.bDead=M->Health<=0;U.Tier=M->Tier;
        U.Reaction=0;U.Role=NpcRole(M);U.RoleName=RoleLabel(U.Role);
        const ECireNPCClass Class=M->GetNPCClassification();
        U.Class=Class==ECireNPCClass::Boss?3:Class==ECireNPCClass::Elite?2:M->bArmoredEscort?1:0;
        const TCHAR* ClassWords[]={TEXT(""),TEXT("Armored "),TEXT("Elite "),TEXT("Boss ")};
        U.ClassName=FString(ClassWords[U.Class])+U.RoleName+(M->Tier>0?FString::Printf(TEXT(" (Tier %d)"),M->Tier):FString());
        U.Subtitle=M->IsLaneBoss()?TEXT("<Siege Host>"):U.Class==3?TEXT("<Pack Leader>"):M->bArmoredEscort?TEXT("<Armored Escort>"):M->PackId>=0?TEXT("<Roaming Pack>"):TEXT("<Breach Horde>");
        U.Victim=M->Victim;
        if(IsValid(M->Victim))U.VictimLine=M->Victim==Self?TEXT("You"):M->Victim->HeroName;
        else U.VictimLine=M->bArmoredEscort?TEXT("Marching on your keep"):M->LeashTimer>0?TEXT("Returning to camp"):TEXT("Advancing toward town");
        if(M->NPCState)
        {
            const FCireNPCCastInfo Cast=M->NPCState->CastInfo();
            if(Cast.bCasting&&Cast.Remaining>0){U.Casting=Cast.Name;U.CastProgress=Cast.Progress;U.CastRemaining=Cast.Remaining;U.bInterruptible=Cast.bInterruptible;}
        }
        else if(M->CastEndsAt>Now&&!M->CastingAbility.IsEmpty())
        {
            U.Casting=ACireHero::SkillName(M->CastingAbility);U.CastRemaining=M->CastEndsAt-Now;
            U.CastProgress=1.f-Frac(M->CastEndsAt-Now,M->CastEndsAt-M->CastStartedAt);U.bInterruptible=true;
        }
        U.Status=NpcStatus(M,Now);
        U.Abilities=NpcAbilities(M);
    }
    else if(const auto* C=Cast<ACireConstruct>(Actor))
    {
        U.bConstruct=true;U.Name=C->GetDisplayName();U.HP=C->Health;U.MaxHP=C->MaxHealth;
        U.Reaction=Self&&C->OriginTeam==Self->TeamId?2:0;U.Subtitle=TEXT("<Summoned construct>");U.ClassName=TEXT("Destructible object");
        U.Status=TEXT("Blocks movement; destroying it reopens the route.");
    }
    return U;
}
FLinearColor ReactionColor(const FInsight& U) { return U.Reaction==2?Friendly:U.Reaction==1?Neutral:Hostile; }
/** Sorted threat rows from the replicated table (server and clients). */
bool ThreatRows(const ACireMonster* M,TArray<TPair<ACireHero*,float>>& Rows)
{
    Rows.Reset();
    if(!IsValid(M)||!M->NPCState)return false;
    for(const FCireThreatEntry& Row:M->NPCState->ThreatTable)if(IsValid(Row.Hero)&&Row.Threat>0.f)Rows.Emplace(Row.Hero.Get(),Row.Threat);
    Rows.Sort([](const TPair<ACireHero*,float>& A,const TPair<ACireHero*,float>& B){return A.Value>B.Value;});
    return true;
}
/** WoW threat %: 100 means you hold aggro; otherwise your threat relative to the current target's. */
float ThreatPercent(const ACireMonster* M,const ACireHero* Hero,bool& bKnown)
{
    bKnown=false;
    if(!IsValid(M)||!Hero)return 0.f;
    if(M->Victim==Hero){bKnown=true;return 100.f;}
    if(!M->NPCState||M->NPCState->ThreatTable.IsEmpty())return 0.f;
    bKnown=true;
    return FMath::Clamp(M->NPCState->ThreatPercent(Hero),0.f,999.f);
}
/** Progress toward pulling aggro, 100 = pulls (WoW 110% melee / 130% ranged rule). */
float PullPercent(const ACireMonster* M,const ACireHero* Hero)
{
    return IsValid(M)&&Hero&&M->NPCState?M->NPCState->PullPercent(Hero):0.f;
}
bool IsBossClass(const ACireMonster* M) { return M&&M->GetNPCClassification()==ECireNPCClass::Boss; }
bool IsEliteOrBoss(const ACireMonster* M) { return M&&M->GetNPCClassification()!=ECireNPCClass::Normal; }
FLinearColor ThreatColor(float Percent)
{
    return Percent>=100.f?Hostile:Percent>=80.f?Orange:Percent>=50.f?Neutral:FLinearColor(.75f,.78f,.8f,1);
}
}

// ---------------------------------------------------------------------------
// Construction, fonts, primitives
// ---------------------------------------------------------------------------
ACireHUD::ACireHUD()
{
    // Hard references cook the style kit's OFL font faces / textures and the UI sounds.
    for(const FString& Path:CireUIStyle::AssetPaths()){ConstructorHelpers::FObjectFinderOptional<UObject> Finder(*Path);if(Finder.Get())WowAssetRefs.Add(Finder.Get());}
    const TCHAR* Sounds[]={TEXT("/Game/UI/WowUI/Sounds/S_LevelUp.S_LevelUp"),TEXT("/Game/UI/WowUI/Sounds/S_AggroGained.S_AggroGained"),
        TEXT("/Game/UI/WowUI/Sounds/S_ThreatWarning.S_ThreatWarning"),TEXT("/Game/UI/WowUI/Sounds/S_AggroLost.S_AggroLost"),
        TEXT("/Game/UI/WowUI/Sounds/S_TargetSelect.S_TargetSelect"),TEXT("/Game/UI/WowUI/Sounds/S_BannerHorn.S_BannerHorn"),
        TEXT("/Game/UI/WowUI/Sounds/S_BannerChime.S_BannerChime")};
    for(const TCHAR* Path:Sounds){ConstructorHelpers::FObjectFinderOptional<USoundBase> Finder(Path);WowSounds.Add(Finder.Get());}
}
FCireUIPainter ACireHUD::Painter() const
{
    FCireUIPainter P;P.Canvas=Canvas;P.Scale=Scale;P.Origin=Origin;P.Stretch=Stretch;P.Alpha=PanelAlpha;return P;
}
void ACireHUD::BuildFonts() { CireUIStyle::Assets(); }
UFont* ACireHUD::ResolveFont(ECireFont Font,const FString& Text,float Size) const { return CireUIStyle::ResolveFont(Font,Text,Size); }
void ACireHUD::TextFx(const FString& Text,float X,float Y,float Size,FLinearColor Color,ECireFont Font,bool bOutline,bool bShadow)
{
    Painter().Text(Text,X,Y,Size,Color,Font,bOutline,bShadow);
}
float ACireHUD::TextWidthFont(const FString& Text,float Size,ECireFont Font) const { return Painter().TextWidth(Text,Size,Font); }
void ACireHUD::Disc(float X,float Y,float R,FLinearColor Color,int32 Sides) { Painter().Disc(X,Y,R,Color,Sides); }
void ACireHUD::Circle(float X,float Y,float R,FLinearColor Color,float Width,int32 Sides) { Painter().Circle(X,Y,R,Color,Width,Sides); }
void ACireHUD::Tri(FVector2D A,FVector2D B,FVector2D C,FLinearColor Color) { Painter().Tri(A,B,C,Color); }
void ACireHUD::PlayWowSound(int32 Index,float Volume)
{
    if(UISettings.bMuteAudio)return;
    if(CireAudio::PlayHudSound(this,Index,Volume))return; // audio: data-driven cue (AudioCues.json hudLegacy); else the synthesized tone
    if(!WowSounds.IsValidIndex(Index)||!WowSounds[Index])return;
    const float Level=UISettings.MasterVolume*UISettings.UIVolume*Volume;
    if(Level>0.f)UGameplayStatics::PlaySound2D(this,WowSounds[Index],Level);
}

// ---------------------------------------------------------------------------
// Portrait, target / focus frame
// ---------------------------------------------------------------------------
void ACireHUD::DrawPortrait(AActor* Actor,float CX,float CY,float R,bool bSmall)
{
    const auto* Self=Cast<ACireHero>(PlayerOwner?PlayerOwner->GetPawn():nullptr);
    const FInsight U=Describe(GetWorld(),Actor,Self);
    const FLinearColor Trim=U.Class==1?Silver:U.Class>=2?WowGold:Gold*.9f;
    // Elite/rare "dragon": a swept wing of feathered blades on the portrait's right
    // side, longest at the top (WoW's elite dragon silhouette), plus a dark outline.
    if(U.Class>=1)
    {
        const FLinearColor Wing=U.Class==1?Silver:WowGold;
        for(int32 Pass=0;Pass<2;++Pass)for(int32 I=0;I<6;++I)
        {
            const float A=FMath::DegreesToRadians(-78.f+I*23.f),Spread=FMath::DegreesToRadians(9.f+I*1.2f);
            const float Tip=R*(1.78f-I*.1f)+(Pass==0?2.f:0.f),Base=R*.82f;
            const float Sweep=FMath::DegreesToRadians(-14.f); // blades lean back like feathers
            Tri(FVector2D(CX+FMath::Cos(A-Spread)*Base,CY+FMath::Sin(A-Spread)*Base),FVector2D(CX+FMath::Cos(A+Sweep)*Tip,CY+FMath::Sin(A+Sweep)*Tip),
                FVector2D(CX+FMath::Cos(A+Spread)*Base,CY+FMath::Sin(A+Spread)*Base),Pass==0?FLinearColor(0,0,0,.8f):Wing*(1.f-I*.06f));
        }
        if(U.Class==3)for(int32 I=0;I<3;++I)
        {
            const float A=FMath::DegreesToRadians(200.f+I*22.f);
            Tri(FVector2D(CX+FMath::Cos(A-.12f)*R*.9f,CY+FMath::Sin(A-.12f)*R*.9f),FVector2D(CX+FMath::Cos(A)*R*1.45f,CY+FMath::Sin(A)*R*1.45f),
                FVector2D(CX+FMath::Cos(A+.12f)*R*.9f,CY+FMath::Sin(A+.12f)*R*.9f),Hostile*.9f);
        }
    }
    Disc(CX,CY,R+2.5f,FLinearColor(0,0,0,.85f));
    Disc(CX,CY,R,FLinearColor(.035f,.04f,.06f,1));
    Disc(CX,CY-R*.25f,R*.72f,ReactionColor(U)*FLinearColor(1,1,1,.12f));
    const FString IconId=U.bConstruct?TEXT("runic_wall"):RoleIcon(U.Role);
    Icon(IconId,CX-R*.62f,CY-R*.62f,R*1.24f,U.bDead?Muted:U.bMonster?RoleTint(U.Role):ReactionColor(U)*.9f+FLinearColor(.1f,.1f,.1f,0));
    if(U.bDead){Disc(CX,CY,R,FLinearColor(0,0,0,.55f));}
    Circle(CX,CY,R,Trim,bSmall?1.6f:2.2f);Circle(CX,CY,R+2.5f,FLinearColor(0,0,0,.9f),1.f);
    // Level badge (bottom-left): hero level, elite tier, or a skull for bosses.
    const float BR=bSmall?8.f:10.5f,BX=CX-R*.78f,BY=CY+R*.74f;
    Disc(BX,BY,BR+1.5f,FLinearColor(0,0,0,.9f));Disc(BX,BY,BR,FLinearColor(.07f,.06f,.04f,1));Circle(BX,BY,BR,Trim,1.2f,20);
    if(U.Class==3)
    {
        Disc(BX,BY-BR*.12f,BR*.55f,Parchment);Panel(BX-BR*.3f,BY+BR*.25f,BR*.6f,BR*.32f,Parchment);
        Disc(BX-BR*.22f,BY-BR*.15f,BR*.15f,FLinearColor(0,0,0,1),8);Disc(BX+BR*.22f,BY-BR*.15f,BR*.15f,FLinearColor(0,0,0,1),8);
    }
    else
    {
        const FString Level=U.bHero?FString::FromInt(U.Level):U.Tier>0?FString::Printf(TEXT("T%d"),U.Tier):FString();
        const float LS=bSmall?8.5f:10.f;
        TextFx(Level,BX-TextWidthFont(Level,LS,ECireFont::Numbers)*.5f,BY-LS*.62f,LS,U.Tier>0?WowGold:Neutral,ECireFont::Numbers,true,false);
    }
    // Role badge (bottom-right): Caster / Ranged / Tank / Bruiser / Healer.
    if(U.Role!=ERole::None)
    {
        const float RX=CX+R*.78f,RY=CY+R*.74f;
        Disc(RX,RY,BR+1.5f,FLinearColor(0,0,0,.9f));Disc(RX,RY,BR,FLinearColor(.05f,.05f,.07f,1));Circle(RX,RY,BR,RoleTint(U.Role),1.2f,20);
        Icon(RoleIcon(U.Role),RX-BR*.7f,RY-BR*.7f,BR*1.4f,RoleTint(U.Role));
        Tip(FString(RoleLabel(U.Role))+TEXT(" role"),RoleExplain(U.Role,U.bMonster),RX-BR,RY-BR,BR*2,BR*2);
    }
}
void ACireHUD::DrawUnit(AActor* Actor,const FString& Caption,bool bFocus)
{
    if(!IsValid(Actor)&&!bEditLayout)return;
    auto* Self=Cast<ACireHero>(PlayerOwner->GetPawn());
    const float W=bFocus?218:300,H=bFocus?123:140;
    UsePanel(bFocus?TEXT("Focus"):TEXT("Target"),W,H);
    auto Backdrop=[&](FLinearColor Border)
    {
        Panel(2,3,W,H,FLinearColor(0,0,0,.35f));Panel(0,0,W,H,FLinearColor(.012f,.014f,.02f,.84f));
        Panel(0,0,W,H*.35f,FLinearColor(1,1,1,.025f));
        Line(0,0,W,0,Border,1.2f);Line(0,H,W,H,Border*.6f);Line(0,0,0,H,Border*.6f);Line(W,0,W,H,Border*.6f);
    };
    if(!IsValid(Actor))
    {
        Backdrop(Muted);Label(bFocus?TEXT("FOCUS"):TEXT("TARGET"),10,8,10,Muted);Label(TEXT("No unit selected"),10,28,12,Muted);return;
    }
    const FInsight U=Describe(GetWorld(),Actor,Self);
    const auto* Mob=Cast<ACireMonster>(Actor);
    const FLinearColor React=ReactionColor(U);
    Backdrop(U.Class==3?Hostile:U.Class==2?WowGold:U.Class==1?Silver:bFocus?Gold:React*.8f);
    UnitTip(Actor,0,0,W,H);
    const float PR=bFocus?26.f:33.f,PCX=W-10-PR,PCY=bFocus?44.f:50.f,BW=PCX-PR-18;
    // Header: classification and role; threat % badge on hostile NPCs.
    FString Header=bFocus?TEXT("FOCUS  "):FString();
    if(U.bMonster)Header+=(U.Class==3?(Mob&&Mob->IsLaneBoss()?FString(TEXT("BOSS  /  10 LIVES AT RISK")):TEXT("BOSS  /  ")+U.RoleName.ToUpper()):U.Class==2?TEXT("ELITE  /  ")+U.RoleName.ToUpper():U.Class==1?TEXT("ARMORED  /  ")+U.RoleName.ToUpper():U.RoleName.ToUpper());
    else if(U.bHero)Header+=(U.bSelf?TEXT("YOU"):U.Reaction==2?TEXT("ALLY"):TEXT("ENEMY"))+FString(TEXT("  /  "))+U.RoleName.ToUpper();
    else Header+=TEXT("CONSTRUCT");
    Label(Short(Header,bFocus?26:34),10,4,bFocus?8.f:9.f,U.Class==3?Hostile:U.Class>=1?WowGold:Muted);
    bool bKnown=false;const float Threat=Mob&&Self?ThreatPercent(Mob,Self,bKnown):0.f;
    if(Mob&&bKnown&&!bFocus)
    {
        const FString T=FString::Printf(TEXT("%.0f%%"),FMath::Min(Threat,999.f));
        const float TW=FMath::Max(34.f,TextWidthFont(T,10,ECireFont::Numbers)+10);
        Panel(10+BW-TW,2,TW,15,FLinearColor(0,0,0,.8f));Panel(10+BW-TW,2,TW,2,ThreatColor(Threat));
        TextFx(T,10+BW-TW+(TW-TextWidthFont(T,10,ECireFont::Numbers))*.5f,3,10,ThreatColor(Threat),ECireFont::Numbers,true,false);
        Tip(TEXT("Your threat"),Mob->Victim==Self?TEXT("You have this enemy's attention (100%). Tanks want this; damage dealers and healers should ease off."):
            FString::Printf(TEXT("Your threat is %.0f%% of its current target's. At 100%% or more it turns on you."),Threat),10+BW-TW,2,TW,15);
    }
    // Name band in reaction colour, then the green health bar with value and percent.
    const float NY=bFocus?17.f:19.f,NH=bFocus?15.f:18.f;
    Panel(10,NY,BW,NH,React*FLinearColor(.42f,.42f,.42f,.92f));Panel(10,NY,BW,NH*.45f,FLinearColor(1,1,1,.07f));
    TextFx(Short(U.Name,bFocus?20:26),14,NY+(bFocus?1.f:1.5f),bFocus?11.f:13.f,FLinearColor::White,ECireFont::Bold,true,false);
    const float HY=NY+NH+2,HH=bFocus?13.f:18.f;
    Panel(10,HY,BW,HH,FLinearColor(0,0,0,.85f));
    const float HF=Frac(U.HP,U.MaxHP);
    Panel(11,HY+1,(BW-2)*HF,HH-2,U.bDead?Muted*.5f:HealthGreen);Panel(11,HY+1,(BW-2)*HF,(HH-2)*.4f,FLinearColor(1,1,1,.16f));
    const float HS=bFocus?9.f:10.5f;
    const FString HPText=U.bDead?TEXT("DEAD"):FString::Printf(TEXT("%.0f / %.0f"),U.HP,U.MaxHP),Pct=FString::Printf(TEXT("%.0f%%"),HF*100);
    TextFx(HPText,10+(BW-TextWidthFont(HPText,HS,ECireFont::Numbers))*.5f,HY+(HH-HS)*.5f-1.5f,HS,FLinearColor::White,ECireFont::Numbers,true,false);
    if(!bFocus&&!U.bDead)TextFx(Pct,10+BW-4-TextWidthFont(Pct,9,ECireFont::Numbers),HY+(HH-9)*.5f-1.5f,9,Parchment,ECireFont::Numbers,true,false);
    float Y=HY+HH+1;
    if(U.MaxMP>0){Panel(10,Y,BW,bFocus?5.f:7.f,FLinearColor(0,0,0,.85f));Panel(11,Y+1,(BW-2)*Frac(U.MP,U.MaxMP),bFocus?3.f:5.f,ManaBlue);Y+=bFocus?6.f:8.f;}
    // Target of target.
    Y+=3;
    const bool bOnMe=U.Victim&&U.Victim==Self;
    TextFx(TEXT(">"),10,Y,9,bOnMe?Hostile:Muted,ECireFont::Bold,false);
    const FString Tot=(U.bMonster&&!IsValid(U.Victim)?FString():FString(TEXT("Target: ")))+(U.VictimLine.IsEmpty()?TEXT("none"):U.VictimLine);
    TextFx(Short(Tot,bFocus?24:30),19,Y,9,bOnMe?Hostile:Parchment,ECireFont::Body,false);
    if(const auto* Victim=Cast<ACireHero>(U.Victim);Victim&&!bFocus)
    {
        Panel(10+BW-52,Y+3,52,6,FLinearColor(0,0,0,.85f));Panel(11+BW-52,Y+4,50*Frac(Victim->Health,Victim->MaxHealth),4,HealthGreen);
    }
    if(IsValid(U.Victim))Tip(TEXT("Target of target"),bOnMe?TEXT("This unit is attacking YOU."):TEXT("Who this unit is currently targeting or attacking."),10,Y,BW,12);
    // Statuses (left) and the unit's abilities (right), each with hover explanations.
    const float RowY=bFocus?H-38:H-50;
    DrawStatuses(Actor,10,RowY,bFocus?16.f:18.f,bFocus?5:6);
    if(!bFocus&&U.Abilities.Num()>0&&(U.bMonster||U.Reaction==0))
    {
        const float S=19;const int32 Count=FMath::Min(U.Abilities.Num(),5);
        for(int32 I=0;I<Count;++I)
        {
            const float AX=W-10-(Count-I)*(S+3);
            Panel(AX,RowY,S,S,FLinearColor(0,0,0,.8f));Icon(U.Abilities[I].Id,AX+1.5f,RowY+1.5f,S-3,U.bMonster?RoleTint(U.Role):Gold);
            Line(AX,RowY,AX+S,RowY,Gold*.6f);
            Tip(U.Abilities[I].Name,U.Abilities[I].Text,AX,RowY,S,S);
        }
    }
    // Cast bar, WoW gold, with remaining time.
    const float CY=H-(bFocus?17.f:21.f),CH=bFocus?13.f:15.f;
    if(!U.Casting.IsEmpty())
    {
        // WoW convention: gold bar = interruptible, grey bar with a shield = cannot be interrupted.
        const FLinearColor Bar=U.bInterruptible?CastGold:FLinearColor(.58f,.6f,.66f,1);
        Panel(10,CY,W-20,CH,FLinearColor(0,0,0,.85f));Panel(11,CY+1,(W-22)*U.CastProgress,CH-2,Bar);Panel(11,CY+1,(W-22)*U.CastProgress,(CH-2)*.4f,FLinearColor(1,1,1,.25f));
        if(!U.bInterruptible){Panel(4,CY+1,5,CH-2,FLinearColor(.75f,.77f,.82f,1));}
        TextFx(Short(U.Casting,24),15,CY+.5f,bFocus?8.5f:9.5f,FLinearColor::White,ECireFont::Bold,true,false);
        const FString Rem=FString::Printf(TEXT("%.1f"),U.CastRemaining);
        TextFx(Rem,W-14-TextWidthFont(Rem,9,ECireFont::Numbers),CY+.5f,9,FLinearColor::White,ECireFont::Numbers,true,false);
        Tip(TEXT("Enemy cast: ")+U.Casting,U.bInterruptible?TEXT("Gold bar: this cast can be interrupted (Shield Slam). Otherwise leave its ground warning or projectile path."):
            TEXT("Grey bar: this cast cannot be interrupted. Leave its ground warning or projectile path before the bar completes."),10,CY,W-20,CH);
    }
    else if(!U.Status.IsEmpty())TextFx(Short(U.Status,bFocus?30:44),10,CY+1,8.5f,U.bDead?Hostile:Neutral*.9f,ECireFont::Body,false);
    else if(Mob&&Mob->IsLaneBoss())TextFx(TEXT("A leak costs 10 lives"),10,CY+1,8.5f,Hostile*.9f,ECireFont::Body,false);
    DrawPortrait(Actor,PCX,PCY,PR,bFocus);
    if(bFocus&&Clicked&&Hit(0,0,W,H)&&!bEditLayout&&!bModal&&!bSettings)
    {
        if(auto* C=Cast<ACireController>(PlayerOwner))C->ServerAction(0,0,Actor);Clicked=false;
    }
}

// ---------------------------------------------------------------------------
// Boss frames and threat meter
// ---------------------------------------------------------------------------
void ACireHUD::DrawBossFrames(ACireHero* Hero,ACireController* Controller)
{
    if(!Hero||(!UISettings.bShowBossFrames&&!bEditLayout))return;
    const auto* State=GetWorld()->GetGameState<ACireGameState>();const bool bArena=State&&State->Phase==2;
    TArray<ACireMonster*> Units;TMap<int32,ACireMonster*> PackLeaders;
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It)
    {
        ACireMonster* M=*It;if(bArena||M->Health<=0||M->Lane!=Hero->TeamId)continue;
        const float Dist=FVector::Dist2D(M->GetActorLocation(),Hero->GetActorLocation());
        // Lane bosses always; boss-classified pack leaders once near or engaged.
        if(M->IsLaneBoss()){Units.Add(M);continue;}
        if(IsBossClass(M)&&(Dist<3600.f||IsValid(M->Victim)))Units.Add(M);
    }
    Units.Sort([&](const ACireMonster& A,const ACireMonster& B){
        if(A.IsLaneBoss()!=B.IsLaneBoss())return A.IsLaneBoss();
        return FVector::DistSquared(A.GetActorLocation(),Hero->GetActorLocation())<FVector::DistSquared(B.GetActorLocation(),Hero->GetActorLocation());});
    if(Units.IsEmpty()&&!bEditLayout)return;
    UsePanel(TEXT("Boss"),220,150);
    if(Units.IsEmpty()){Panel(0,0,220,150,FLinearColor(0,0,0,.4f));Label(TEXT("BOSS FRAMES"),8,6,9,Muted);Label(TEXT("Bosses and pack leaders appear here"),8,22,9,Muted);return;}
    const float Now=ServerNow(GetWorld());
    // Show fewer rows when the panel had to give way to the threat meter.
    const float FullH=UISettings.GetRect(TEXT("Boss"),FVector2D(ViewW,ViewH)).H;
    const int32 MaxRows=FMath::Clamp(FMath::FloorToInt(PanelRect(TEXT("Boss")).H/(FullH/3.f)+.05f),1,3);
    for(int32 I=0;I<FMath::Min(Units.Num(),MaxRows);++I)
    {
        ACireMonster* M=Units[I];const float Y=I*50.f;
        const bool bSelected=Hero->Target==M;
        Panel(2,Y+3,220,46,FLinearColor(0,0,0,.3f));Panel(0,Y,220,46,FLinearColor(.012f,.014f,.02f,.86f));
        const bool bSkull=IsBossClass(M);
        Line(0,Y,220,Y,bSkull?Hostile:WowGold,bSelected?2.f:1.f);if(bSelected){Line(0,Y+46,220,Y+46,Parchment,1.5f);}
        // Mini skull / dragon badge.
        Disc(15,Y+15,9.5f,FLinearColor(.07f,.06f,.04f,1));Circle(15,Y+15,9.5f,bSkull?Hostile:WowGold,1.2f,20);
        if(bSkull){Disc(15,Y+13.5f,5.2f,Parchment);Panel(12,Y+16.5f,6,3,Parchment);Disc(13,Y+13.5f,1.4f,FLinearColor(0,0,0,1),8);Disc(17,Y+13.5f,1.4f,FLinearColor(0,0,0,1),8);}
        else TextFx(FString::Printf(TEXT("T%d"),M->Tier),9.5f,Y+9.5f,8.5f,WowGold,ECireFont::Numbers,true,false);
        TextFx(Short(M->GetNPCDisplayName(),24),30,Y+3,10.5f,bSkull?FLinearColor(1.f,.45f,.35f,1):WowGold,ECireFont::Bold,true,false);
        if(M->NPCState&&M->NPCState->HasStatus(CireNPCStatus::Enraged))TextFx(TEXT("ENRAGED"),160,Y+4,8,Hostile,ECireFont::Heading,true,false);
        bool bKnown=false;const float Threat=ThreatPercent(M,Hero,bKnown);
        if(bKnown&&(IsValid(M->Victim)||Threat>0))
        {
            const FString T=FString::Printf(TEXT("%.0f%%"),FMath::Min(Threat,999.f));
            TextFx(T,214-TextWidthFont(T,9,ECireFont::Numbers),Y+4,9,ThreatColor(Threat),ECireFont::Numbers,true,false);
        }
        const float HF=Frac(M->Health,M->MaxHealth);
        Panel(30,Y+18,184,15,FLinearColor(0,0,0,.85f));Panel(31,Y+19,182*HF,13,HealthGreen);Panel(31,Y+19,182*HF,5,FLinearColor(1,1,1,.16f));
        const FString HP=FString::Printf(TEXT("%.0f / %.0f"),M->Health,M->MaxHealth),Pct=FString::Printf(TEXT("%.0f%%"),HF*100);
        TextFx(HP,35,Y+19,9.5f,FLinearColor::White,ECireFont::Numbers,true,false);
        TextFx(Pct,210-TextWidthFont(Pct,9.5f,ECireFont::Numbers),Y+19,9.5f,FLinearColor::White,ECireFont::Numbers,true,false);
        const FCireNPCCastInfo Cast=M->NPCState?M->NPCState->CastInfo():FCireNPCCastInfo();
        if(Cast.bCasting&&Cast.Remaining>0)
        {
            Panel(30,Y+35,184,9,FLinearColor(0,0,0,.85f));Panel(31,Y+36,182*Cast.Progress,7,Cast.bInterruptible?CastGold:FLinearColor(.58f,.6f,.66f,1));
            TextFx(Short(Cast.Name,26),34,Y+33.5f,7.5f,FLinearColor::White,ECireFont::Bold,true,false);
        }
        else
        {
            const FString V=IsValid(M->Victim)?(M->Victim==Hero?TEXT("Attacking YOU"):TEXT("Attacking ")+Short(M->Victim->HeroName,18)):TEXT("Advancing");
            TextFx(V,31,Y+34,8,M->Victim==Hero?Hostile:Muted,ECireFont::Body,false);
        }
        UnitTip(M,0,Y,220,46);
        if(Clicked&&Hit(0,Y,220,46)&&!bModal&&!bSettings&&!bEditLayout&&Controller){Controller->ServerAction(0,0,M);Clicked=false;}
    }
}
void ACireHUD::DrawThreatMeter(ACireHero* Hero,ACireController* Controller)
{
    if(!Hero||(!UISettings.bShowThreatMeter&&!bEditLayout))return;
    // Source: hostile target; else the nearest enemy attacking you; else the nearest engaged enemy.
    ACireMonster* Source=Cast<ACireMonster>(Hero->Target);
    if(Source&&(Source->Health<=0||!IsValid(Source->Victim)))Source=nullptr;
    if(!Source)
    {
        double Best=MAX_dbl;
        for(TActorIterator<ACireMonster> It(GetWorld());It;++It)
        {
            if(It->Health<=0||It->Lane!=Hero->TeamId||!IsValid(It->Victim))continue;
            const double D=FVector::DistSquared(It->GetActorLocation(),Hero->GetActorLocation())*(It->Victim==Hero?.25:1.);
            if(D<Best&&D<FMath::Square(3000.)){Best=D;Source=*It;}
        }
    }
    if(!Source&&!bEditLayout)return;
    UsePanel(TEXT("Threat"),220,124);
    Panel(2,3,220,124,FLinearColor(0,0,0,.3f));Panel(0,0,220,124,FLinearColor(.012f,.014f,.02f,.8f));
    Panel(0,0,220,18,FLinearColor(.3f,.05f,.04f,.75f));Line(0,0,220,0,Hostile*.8f,1.2f);
    TextFx(TEXT("THREAT"),7,2,9.5f,Parchment,ECireFont::Heading,true,false);
    if(!Source){TextFx(TEXT("No enemy engaged"),8,26,10,Muted,ECireFont::Body,false);return;}
    TextFx(Short(Source->GetNPCDisplayName(),22),62,2.5f,9.5f,WowGold,ECireFont::Bold,true,false);
    Tip(TEXT("Threat meter"),TEXT("Who this enemy wants to attack. The top row holds aggro (100%). Others show their threat relative to it; reaching 100% or more pulls the enemy. Damage and healing both add threat; tanks generate extra."),0,0,220,18);
    TArray<TPair<ACireHero*,float>> Rows;
    ThreatRows(Source,Rows);
    float Top=0;for(const auto& Row:Rows)if(Row.Key==Source->Victim)Top=Row.Value;
    if(Top<=0&&Rows.Num())Top=Rows[0].Value;
    if(Rows.IsEmpty())TextFx(IsValid(Source->Victim)?TEXT("Aggro: ")+Source->Victim->HeroName:FString(TEXT("No threat yet")),8,26,10,Muted,ECireFont::Body,false);
    // Aggro holder first, then by threat.
    Rows.StableSort([&](const TPair<ACireHero*,float>& A,const TPair<ACireHero*,float>& B){return (A.Key==Source->Victim)>(B.Key==Source->Victim);});
    for(int32 I=0;I<FMath::Min(Rows.Num(),5);++I)
    {
        ACireHero* H=Rows[I].Key;const float Pct=Top>0?Rows[I].Value/Top*100.f:0.f;const float Y=21+I*20.f;
        const float Pull=PullPercent(Source,H);
        const bool bMe=H==Hero,bAggro=H==Source->Victim;
        const ERole R=HeroRole(H);
        Panel(4,Y,212,18,FLinearColor(0,0,0,.6f));
        Panel(5,Y+1,210*FMath::Clamp(Pct/100.f,0.f,1.f),16,RoleTint(R)*FLinearColor(.55f,.55f,.55f,bMe?.95f:.75f));
        if(bMe)Line(4,Y,216,Y,Parchment,1.2f);
        Icon(RoleIcon(R),7,Y+2,14,RoleTint(R)*1.2f);
        TextFx(FString::Printf(TEXT("%d. %s"),I+1,*Short(bMe?TEXT("You"):H->HeroName,15)),25,Y+1.5f,9.5f,bMe?FLinearColor::White:Parchment,ECireFont::Bold,true,false);
        const FString Value=bAggro?TEXT("AGGRO"):FString::Printf(TEXT("%.0f%%"),Pct);
        TextFx(Value,212-TextWidthFont(Value,9.5f,bAggro?ECireFont::Heading:ECireFont::Numbers),Y+1.5f,9.5f,bAggro?Hostile:ThreatColor(Pull),bAggro?ECireFont::Heading:ECireFont::Numbers,true,false);
    }
}

// ---------------------------------------------------------------------------
// Aggro / threat alerts
// ---------------------------------------------------------------------------
void ACireHUD::ShowAlert(const FString& Title,const FString& Subtitle,FLinearColor Color,bool bSound,int32 SoundIndex)
{
    Alert.Title=Title;Alert.Subtitle=Subtitle;Alert.Color=Color;Alert.Start=GetWorld()->GetRealTimeSeconds();Alert.Duration=2.6f;
    if(bSound&&UISettings.bThreatSound)PlayWowSound(SoundIndex,.9f);
}
void ACireHUD::UpdateThreatAlerts(ACireHero* Hero)
{
    // Aggro changes arrive through UCireNPCState::OnAggroChanged (OnAggroEvent).
    // This poll only raises the "about to pull" warning for damage dealers/healers,
    // using the WoW pull rule (110% of the target's threat in melee, 130% at range).
    if(!Hero||!UISettings.bThreatWarnings||IsTank(Hero))return;
    const double Now=GetWorld()->GetRealTimeSeconds();
    if(Now-LastThreatWarning<5.0)return;
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It)
    {
        ACireMonster* M=*It;
        if(M->Health<=0||M->Lane!=Hero->TeamId||M->bArmoredEscort||!IsValid(M->Victim)||M->Victim==Hero)continue;
        const float Pull=PullPercent(M,Hero);
        if(Pull>=UISettings.ThreatWarningPercent&&Pull<100.f)
        {
            LastThreatWarning=Now;
            ShowAlert(FString::Printf(TEXT("THREAT %.0f%%"),Pull),TEXT("Ease off ")+M->GetNPCDisplayName()+TEXT(" or you will pull it from ")+M->Victim->HeroName+TEXT("."),Orange,true,2);
            return;
        }
    }
}
void ACireHUD::OnAggroEvent(const FCireAggroEvent& Event)
{
    auto* Hero=Cast<ACireHero>(PlayerOwner?PlayerOwner->GetPawn():nullptr);
    ACireMonster* M=Event.Monster.Get();
    if(!Hero||!M||!UISettings.bThreatWarnings||M->Lane!=Hero->TeamId||M->bArmoredEscort)return;
    ACireHero* NewTarget=Event.NewTarget.Get();ACireHero* OldTarget=Event.OldTarget.Get();
    const bool bTank=IsTank(Hero),bBig=IsEliteOrBoss(M);const FString Name=M->GetNPCDisplayName();
    if(NewTarget==Hero&&OldTarget!=Hero)
    {
        if(bTank)
        {
            if(Event.Reason==ECireAggroReason::Taunted)ShowAlert(TEXT("TAUNTED"),Name+TEXT(" is locked on you."),Friendly,false);
            else if(bBig)ShowAlert(TEXT("AGGRO GAINED"),Name+TEXT(" is on you."),Friendly,false);
        }
        else if(Event.Reason==ECireAggroReason::Pulled||Event.Reason==ECireAggroReason::Acquired||Event.Reason==ECireAggroReason::TargetLost)
            ShowAlert(TEXT("AGGRO!"),Name+(Event.Reason==ECireAggroReason::Pulled?TEXT(" turned on you. Stop and let your tank take it back."):TEXT(" is attacking you. Run to your tank.")),Hostile,true,1);
    }
    else if(OldTarget==Hero&&NewTarget&&NewTarget!=Hero&&bTank&&Event.Reason!=ECireAggroReason::Reset)
    {
        ShowAlert(TEXT("LOST AGGRO"),Name+TEXT(" is attacking ")+NewTarget->HeroName+TEXT(". Taunt it back!"),Orange,true,3);
    }
}
void ACireHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    UCireNPCState::OnAggroChanged().Remove(AggroHandle);AggroHandle.Reset();
    Super::EndPlay(Reason);
}
void ACireHUD::DrawAlert()
{
    const double Age=GetWorld()->GetRealTimeSeconds()-Alert.Start;
    if(Alert.Title.IsEmpty()||Age<0||Age>Alert.Duration)return;
    ResetTransform();
    const float In=FMath::Clamp(static_cast<float>(Age)/.12f,0.f,1.f),Out=FMath::Clamp((Alert.Duration-static_cast<float>(Age))/.6f,0.f,1.f);
    const float A=FMath::Min(In,Out);
    // A red screen-edge pulse for the "you pulled it" warning.
    if(Alert.Color.Equals(Hostile,.01f))
    {
        const float P=A*(.5f+.5f*FMath::Cos(static_cast<float>(Age)*9.f))*.22f;
        for(int32 I=0;I<6;++I){const float T=6.f+I*7.f,C=P*(1.f-I/6.f);
            Panel(0,0,ViewW,T,FLinearColor(1,0,0,C*.5f));Panel(0,ViewH-T,ViewW,T,FLinearColor(1,0,0,C*.5f));
            Panel(0,0,T,ViewH,FLinearColor(1,0,0,C*.5f));Panel(ViewW-T,0,T,ViewH,FLinearColor(1,0,0,C*.5f));}
    }
    const float Pop=1.f+.3f*FMath::Clamp(1.f-static_cast<float>(Age)/.18f,0.f,1.f);
    // Raid-warning position: just under the target frame (or at 24% height), never
    // covering the frame the player is reading.
    const FCireUIRect Target=PanelRect(TEXT("Target"));
    float Y=FMath::Clamp(FMath::Max(ViewH*.24f,Target.Y+Target.H+30.f),40.f,ViewH*.42f);
    if(CireBanners::IsShowing())Y+=96.f; // stack under an active banner
    const float TS=28.f*Pop;
    const float BandW=FMath::Max(TextWidthFont(Alert.Title,28.f,ECireFont::Heading),TextWidthFont(Alert.Subtitle,12,ECireFont::Body))+80.f;
    for(int32 I=0;I<6;++I){const float Inset=I*BandW*.07f;Panel((ViewW-BandW)*.5f+Inset,Y-24,BandW-2*Inset,62,FLinearColor(0,0,0,.09f*A));}
    FLinearColor C=Alert.Color;C.A=A;
    TextFx(Alert.Title,(ViewW-TextWidthFont(Alert.Title,TS,ECireFont::Heading))*.5f,Y-TS*.5f,TS,C,ECireFont::Heading,true,true);
    FLinearColor S=Parchment;S.A=A;
    TextFx(Alert.Subtitle,(ViewW-TextWidthFont(Alert.Subtitle,12,ECireFont::Body))*.5f,Y+TS*.55f+4,12,S,ECireFont::Body,true,true);
}

// ---------------------------------------------------------------------------
// Level-up burst
// ---------------------------------------------------------------------------
void ACireHUD::UpdateLevelUps(ACireHero* Hero)
{
    const double Now=GetWorld()->GetRealTimeSeconds();
    for(auto It=SeenLevels.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    for(TActorIterator<ACireHero> It(GetWorld());It;++It)
    {
        ACireHero* H=*It;if(Cast<ACireSummon>(H))continue;
        int32* Seen=SeenLevels.Find(H);
        if(!Seen){SeenLevels.Add(H,H->Level);continue;}
        if(H->Level>*Seen&&H->bDrafted&&UISettings.bLevelUpEffect)
        {
            LevelBursts.Add({H,H->Level,Now,H==Hero});
            if(H==Hero){PlayWowSound(0,1.f);CireBanners::Show(ECireBanner::LevelUp,FString::Printf(TEXT("Level %d"),H->Level),TEXT("+2 primary attribute  /  +1 to the others"));}
        }
        *Seen=H->Level;
    }
    LevelBursts.RemoveAll([&](const FCireLevelBurst& B){return !B.Hero.IsValid()||Now-B.Start>4.2;});
}
#if !UE_BUILD_SHIPPING
void ACireHUD::DebugLevelUp(ACireHero* Hero,bool bLocal)
{
    if(!Hero)return;
    LevelBursts.Add({Hero,Hero->Level,GetWorld()->GetRealTimeSeconds(),bLocal});SeenLevels.Add(Hero,Hero->Level);
    if(bLocal)CireBanners::Show(ECireBanner::LevelUp,FString::Printf(TEXT("Level %d"),Hero->Level),TEXT("+2 primary attribute  /  +1 to the others"));
}
#endif
void ACireHUD::DrawLevelUps(ACireHero* Hero)
{
    if(LevelBursts.IsEmpty()||!PlayerOwner)return;
    ResetTransform();
    const double Now=GetWorld()->GetRealTimeSeconds();
    const FRotator View=PlayerOwner->PlayerCameraManager?PlayerOwner->PlayerCameraManager->GetCameraRotation():FRotator::ZeroRotator;
    const FVector Right=FRotationMatrix(View).GetUnitAxis(EAxis::Y);
    auto Project=[&](const FVector& World,FVector2D& Out){FVector2D S;if(!PlayerOwner->ProjectWorldLocationToScreen(World,S,false))return false;Out=S/Scale;return true;};
    for(const FCireLevelBurst& B:LevelBursts)
    {
        ACireHero* H=B.Hero.Get();if(!H)continue;
        const float T=static_cast<float>(Now-B.Start);
        float Radius=40,Half=90;H->GetSimpleCollisionCylinder(Radius,Half);
        const FVector Feet=H->GetActorLocation()-FVector(0,0,Half);
        const float Env=FMath::Clamp(T/.18f,0.f,1.f)*FMath::Clamp((2.9f-T)/1.2f,0.f,1.f);
        FVector2D F,Top,Side,Chest;
        // Ground flash: a filled golden disc under the hero that blooms and fades.
        {
            const float GT=FMath::Clamp(T/1.6f,0.f,1.f);const float GR=Radius*1.4f+60.f*GT;FVector2D C0;
            if(Env>0&&Project(Feet+FVector(0,0,3),C0))
            {
                FVector2D Prev;bool bPrev=false;
                for(int32 I=0;I<=28;++I)
                {
                    const float A=I*2*PI/28;FVector2D P;const bool bOk=Project(Feet+FVector(FMath::Cos(A)*GR,FMath::Sin(A)*GR,3),P);
                    if(bOk&&bPrev){Tri(C0,Prev,P,FLinearColor(1.f,.8f,.3f,.28f*Env*(1.f-GT*.6f)));}
                    Prev=P;bPrev=bOk;
                }
            }
        }
        if(Env>0&&Project(Feet,F)&&Project(Feet+FVector(0,0,620),Top)&&Project(Feet+Right*(Radius+30.f),Side))
        {
            // Pillar of light: layered columns, brightest at the white-gold core, fading upward.
            const float Wd=FMath::Clamp(FMath::Abs(Side.X-F.X)*2.f,10.f,220.f);
            const float Widths[]={2.2f,1.5f,1.0f,.6f,.3f,.12f},Alphas[]={.07f,.1f,.14f,.2f,.34f,.8f};
            for(int32 L=0;L<6;++L)for(int32 S=0;S<14;++S)
            {
                const float Y0=FMath::Lerp(F.Y,Top.Y,S/14.f),Y1=FMath::Lerp(F.Y,Top.Y,(S+1)/14.f);
                const float Fade=FMath::Square(1.f-S/14.f);const float X=FMath::Lerp(F.X,Top.X,(S+.5f)/14.f);
                const float Shimmer=1.f+.15f*FMath::Sin(T*12.f+S*.9f);
                const FLinearColor C=L>=4?FLinearColor(1.f,.96f,.78f,Alphas[L]*Env*Fade):FLinearColor(1.f,.74f,.2f,Alphas[L]*Env*Fade*Shimmer);
                Panel(X-Wd*Widths[L]*.5f,FMath::Min(Y0,Y1),Wd*Widths[L],FMath::Abs(Y1-Y0)+.5f,C);
            }
        }
        // Expanding golden rings on the ground.
        for(int32 R=0;R<3;++R)
        {
            const float RT=FMath::Clamp((T-R*.22f)/1.0f,0.f,1.f);if(RT<=0||RT>=1)continue;
            const float Rad=Radius+20.f+150.f*(1.f-FMath::Square(1.f-RT));const FLinearColor C(1.f,.84f,.35f,(1.f-RT)*.95f);
            FVector2D Prev;bool bPrev=false;
            for(int32 I=0;I<=40;++I)
            {
                const float A=I*2*PI/40;FVector2D P;
                const bool bOk=Project(Feet+FVector(FMath::Cos(A)*Rad,FMath::Sin(A)*Rad,4),P);
                if(bOk&&bPrev)Line(Prev.X,Prev.Y,P.X,P.Y,C,3.f*(1.f-RT)+1.f);
                Prev=P;bPrev=bOk;
            }
        }
        // Rising motes spiralling up the column.
        for(int32 I=0;I<44;++I)
        {
            const float Rise=FMath::Fmod(I*53.f,160.f)+T*(200.f+(I%4)*70.f);if(Rise>650.f)continue;
            const float A=I*2.39996f+T*(1.6f+(I%3)*.5f),Rad=Radius*.5f+18.f+(I%5)*12.f;
            FVector2D P;if(!Project(Feet+FVector(FMath::Cos(A)*Rad,FMath::Sin(A)*Rad,Rise),P))continue;
            const float Size=2.6f+(I%3)*1.2f;const float Al=Env*(1.f-Rise/650.f);
            const FLinearColor C=I%4==0?FLinearColor(1,1,.9f,Al):FLinearColor(1.f,.8f,.28f,Al);
            Tri(P+FVector2D(0,-Size*1.6f),P+FVector2D(Size,0),P+FVector2D(-Size,0),C);Tri(P+FVector2D(0,Size*1.6f),P+FVector2D(Size,0),P+FVector2D(-Size,0),C);
        }
        // Initial radial burst from the chest.
        if(T<.6f&&Project(Feet+FVector(0,0,Half*1.2f),Chest))
        {
            const float K=T/.6f;
            for(int32 I=0;I<16;++I)
            {
                const float A=I*PI/8+.2f;const float R0=10.f+K*50.f,R1=R0+20.f+(I%2)*26.f*(1.f-K);
                Line(Chest.X+FMath::Cos(A)*R0,Chest.Y+FMath::Sin(A)*R0,Chest.X+FMath::Cos(A)*R1,Chest.Y+FMath::Sin(A)*R1,FLinearColor(1.f,.9f,.5f,1.f-K),2.2f);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Unit hover and unit tooltips
// ---------------------------------------------------------------------------
void ACireHUD::UnitTip(AActor* Unit,float X,float Y,float W,float H)
{
    if(UISettings.bTooltips&&UISettings.bUnitTooltips&&IsValid(Unit)&&Hit(X,Y,W,H)){TooltipUnit=Unit;}
}
void ACireHUD::UpdateHoverUnit(ACireHero* Hero)
{
    HoverUnit.Reset();
    if(!Hero||!PlayerOwner||!UISettings.bUnitTooltips||MX<0||IsPointerOverInterface())return;
    const auto* State=GetWorld()->GetGameState<ACireGameState>();const bool bArena=State&&State->Phase==2;
    float BestDepth=MAX_flt;
    const FVector Camera=PlayerOwner->PlayerCameraManager?PlayerOwner->PlayerCameraManager->GetCameraLocation():Hero->GetActorLocation();
    auto Consider=[&](AActor* A)
    {
        if(!IsValid(A)||A==Hero)return;
        float Radius=40,Half=90;A->GetSimpleCollisionCylinder(Radius,Half);
        const FVector Loc=A->GetActorLocation();
        if(FVector::DistSquared(Loc,Hero->GetActorLocation())>FMath::Square(4500.f))return;
        FVector2D Top,Bottom;
        if(!PlayerOwner->ProjectWorldLocationToScreen(Loc+FVector(0,0,Half),Top,false)||!PlayerOwner->ProjectWorldLocationToScreen(Loc-FVector(0,0,Half),Bottom,false))return;
        Top/=Scale;Bottom/=Scale;
        const float Height=FMath::Max(12.f,Bottom.Y-Top.Y),Width=FMath::Max(14.f,Height*FMath::Clamp(Radius/Half,.3f,1.f)*1.2f);
        const float CX=(Top.X+Bottom.X)*.5f;
        if(MX<CX-Width*.5f||MX>CX+Width*.5f||MY<Top.Y-6||MY>Bottom.Y+4)return;
        const float Depth=FVector::DistSquared(Camera,Loc);
        if(Depth<BestDepth){BestDepth=Depth;HoverUnit=A;}
    };
    for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(!It->bDead&&(bArena||It->TeamId==Hero->TeamId))Consider(*It);
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It)if(It->Health>0&&!bArena&&It->Lane==Hero->TeamId)Consider(*It);
}
void ACireHUD::TooltipBox(float X,float Y,float W,float H,FLinearColor Border)
{
    const float O=UISettings.TooltipOpacity;
    Panel(X+2,Y+3,W,H,FLinearColor(0,0,0,.35f*O));
    Panel(X,Y,W,H,FLinearColor(.02f,.025f,.06f,O));
    Panel(X+1,Y+1,W-2,FMath::Min(22.f,H*.3f),FLinearColor(1,1,1,.035f*O));
    const FLinearColor B=Border*FLinearColor(1,1,1,FMath::Max(.5f,O));
    Line(X,Y,X+W,Y,B,1.2f);Line(X,Y+H,X+W,Y+H,B,1.2f);Line(X,Y,X,Y+H,B,1.2f);Line(X+W,Y,X+W,Y+H,B,1.2f);
    const FLinearColor Inner(0,0,0,.6f*O);Line(X+2,Y+2,X+W-2,Y+2,Inner);Line(X+2,Y+H-2,X+W-2,Y+H-2,Inner);
}
FCireUIRect ACireHUD::PlaceTooltip(float W,float H,FVector2D Cursor) const
{
    const FCireUIRect Anchor=UISettings.GetRect(TEXT("Tooltip"),FVector2D(ViewW,ViewH));
    TArray<FVector2D> Candidates;
    switch(UISettings.TooltipMode)
    {
    case 0:
        Candidates={Cursor+FVector2D(20,24),Cursor+FVector2D(-20-W,24),Cursor+FVector2D(20,-12-H),Cursor+FVector2D(-20-W,-12-H)};break;
    case 1:
        Candidates={FVector2D(Anchor.X,Anchor.Y)};break;
    case 2:
    {
        const float Angle=FMath::DegreesToRadians(UISettings.TooltipAngleDegrees);
        FVector2D P(Cursor.X+FMath::Cos(Angle)*UISettings.TooltipDistance,Cursor.Y+FMath::Sin(Angle)*UISettings.TooltipDistance);
        if(FMath::Cos(Angle)<0)P.X-=W;
        if(FMath::Sin(Angle)<0)P.Y-=H;
        Candidates={P};break;
    }
    default:
        Candidates={FVector2D(Anchor.X+Anchor.W-W,Anchor.Y+Anchor.H-H)};break;
    }
    auto Clamp=[&](FVector2D P){return FVector2D(FMath::Clamp(P.X,4.f,FMath::Max(4.f,ViewW-W-4)),FMath::Clamp(P.Y,4.f,FMath::Max(4.f,ViewH-H-4)));};
    // Never cover the reticle / screen centre or an armed ground-aim footprint.
    TArray<FBox2D> Avoid;
    if(UISettings.bTooltipAvoidCenter)
    {
        Avoid.Emplace(FVector2D(ViewW*.5f-95,ViewH*.5f-85),FVector2D(ViewW*.5f+95,ViewH*.5f+85));
        if(const auto* C=Cast<ACireController>(PlayerOwner);C&&CireTargeting::Snapshot(C).bActive&&MX>=0)
            Avoid.Emplace(FVector2D(MX-150,MY-120),FVector2D(MX+150,MY+120));
    }
    auto Clear=[&](FVector2D P){const FBox2D Box(P,P+FVector2D(W,H));for(const auto& A:Avoid)if(Box.Intersect(A))return false;return true;};
    for(const FVector2D& C:Candidates)if(Clear(Clamp(C))){const FVector2D P=Clamp(C);return {static_cast<float>(P.X),static_cast<float>(P.Y),W,H};}
    // Slide the first choice sideways/vertically out of the protected areas.
    const FVector2D First=Clamp(Candidates[0]);
    for(const auto& A:Avoid)
    {
        for(const FVector2D Try:{FVector2D(A.Max.X+8,First.Y),FVector2D(A.Min.X-8-W,First.Y),FVector2D(First.X,A.Max.Y+8),FVector2D(First.X,A.Min.Y-8-H)})
            if(Clear(Clamp(Try))){const FVector2D P=Clamp(Try);return {static_cast<float>(P.X),static_cast<float>(P.Y),W,H};}
    }
    return {static_cast<float>(First.X),static_cast<float>(First.Y),W,H};
}
bool ACireHUD::DrawUnitTooltip(AActor* Unit,FVector2D Cursor)
{
    if(!IsValid(Unit))return false;
    auto* Self=Cast<ACireHero>(PlayerOwner?PlayerOwner->GetPawn():nullptr);
    const FInsight U=Describe(GetWorld(),Unit,Self);
    const float S=FMath::Clamp(UISettings.TooltipScale,.6f,1.4f)*1.1f;
    const float W=300*S,Pad=9*S;
    struct FRow{FString Text;float Size;FLinearColor Color;ECireFont Font;int32 Kind=0;}; // Kind 1: health bar, 2: spacer
    TArray<FRow> Rows;
    auto Wrap=[&](const FString& Text,float Size,FLinearColor Color,ECireFont Font,float Indent=0)
    {
        TArray<FString> Words;Text.ParseIntoArrayWS(Words);FString Row;
        for(const FString& Word:Words)
        {
            const FString Next=Row.IsEmpty()?Word:Row+TEXT(" ")+Word;
            if(!Row.IsEmpty()&&TextWidthFont(Next,Size,Font)>W-2*Pad-Indent){Rows.Add({Row,Size,Color,Font});Row=Word;}else Row=Next;
        }
        if(!Row.IsEmpty())Rows.Add({Row,Size,Color,Font});
    };
    Rows.Add({U.Name,15*S,ReactionColor(U),ECireFont::Bold});
    if(!U.Subtitle.IsEmpty())Rows.Add({U.Subtitle,10.5f*S,FLinearColor(.86f,.86f,.86f,1),ECireFont::Body});
    Rows.Add({U.ClassName,10.5f*S,U.Class==3?Hostile:U.Class==2?WowGold:U.Class==1?Silver:FLinearColor::White,ECireFont::Body});
    Rows.Add({FString(),12*S,HealthGreen,ECireFont::Numbers,1});
    if(!U.VictimLine.IsEmpty())Rows.Add({(U.bMonster&&!IsValid(U.Victim)?FString():FString(TEXT("Target: ")))+U.VictimLine,10.5f*S,U.Victim&&U.Victim==Self?Hostile:FLinearColor::White,ECireFont::Body});
    if(!U.Casting.IsEmpty())Rows.Add({FString::Printf(TEXT("Casting %s (%.1fs)"),*U.Casting,U.CastRemaining),10.5f*S,CastGold,ECireFont::Bold});
    if(!U.Status.IsEmpty())Wrap(U.Status,10*S,Neutral,ECireFont::Body);
    if(U.Role!=ERole::None)Wrap(RoleExplain(U.Role,U.bMonster),10*S,RoleTint(U.Role),ECireFont::Body);
    if(const auto* M=Cast<ACireMonster>(Unit);M&&Self)
    {
        bool bKnown=false;const float Pct=ThreatPercent(M,Self,bKnown);
        if(bKnown&&(IsValid(M->Victim)||Pct>0))Rows.Add({M->Victim==Self?FString(TEXT("You have aggro (100%)")):
            FString::Printf(TEXT("Your threat: %.0f%%  (pull: %.0f%%)"),Pct,PullPercent(M,Self)),10.5f*S,ThreatColor(PullPercent(M,Self)),ECireFont::Bold});
    }
    if(U.Abilities.Num()>0&&(U.bMonster||U.Reaction==0))
    {
        Rows.Add({FString(),4*S,FLinearColor::White,ECireFont::Body,2});
        Rows.Add({TEXT("ABILITIES"),9*S,WowGold,ECireFont::Heading});
        for(const auto& A:U.Abilities)
        {
            Rows.Add({A.Name,10.5f*S,FLinearColor::White,ECireFont::Bold});
            Wrap(A.Text,9.5f*S,FLinearColor(.72f,.76f,.78f,1),ECireFont::Body);
        }
    }
    else if(U.bHero&&U.Abilities.Num()>0)
    {
        FString List;for(const auto& A:U.Abilities)List+=(List.IsEmpty()?TEXT(""):TEXT(", "))+A.Name;
        Rows.Add({FString(),4*S,FLinearColor::White,ECireFont::Body,2});
        Wrap(TEXT("Abilities: ")+List,9.5f*S,FLinearColor(.72f,.76f,.78f,1),ECireFont::Body);
    }
    float H=2*Pad;for(const FRow& R:Rows)H+=R.Kind==1?R.Size+5*S:R.Size+4*S;
    H=FMath::Min(H,ViewH-8);
    const FCireUIRect Box=PlaceTooltip(W,H,Cursor);
    ResetTransform();TooltipBox(Box.X,Box.Y,W,H,U.Class==3?Hostile*.9f:U.Class==2?WowGold*.85f:FLinearColor(.55f,.58f,.64f,1));
    float Y=Box.Y+Pad;
    for(const FRow& R:Rows)
    {
        if(Y+R.Size>Box.Y+H-Pad*.5f)break;
        if(R.Kind==1)
        {
            const float HF=Frac(U.HP,U.MaxHP);
            Panel(Box.X+Pad,Y,W-2*Pad,R.Size,FLinearColor(0,0,0,.9f));Panel(Box.X+Pad+1,Y+1,(W-2*Pad-2)*HF,R.Size-2,U.bDead?Muted:HealthGreen);
            const FString T=FString::Printf(TEXT("%.0f / %.0f  (%.0f%%)"),U.HP,U.MaxHP,HF*100);
            TextFx(T,Box.X+(W-TextWidthFont(T,9.5f*S,ECireFont::Numbers))*.5f,Y-S*.5f,9.5f*S,FLinearColor::White,ECireFont::Numbers,true,false);
            Y+=R.Size+5*S;continue;
        }
        if(R.Kind!=2)TextFx(R.Text,Box.X+Pad,Y,R.Size,R.Color,R.Font,false,true);
        Y+=R.Size+4*S;
    }
#if !UE_BUILD_SHIPPING
    LastTooltipRect=Box;LastTooltipBodyLines=Rows.Num();LastTooltipBodyFontSize=10.5f*S;
#endif
    return true;
}

// ---------------------------------------------------------------------------
// Text entry points used across the HUD
// ---------------------------------------------------------------------------
void ACireHUD::Label(const FString& Text,float X,float Y,float Size,FLinearColor Color)
{
    const ECireFont Font=NextFont;NextFont=ECireFont::Auto;
    TextFx(Text,X,Y,Size,Color,Font,false,true);
}
float ACireHUD::TextWidth(const FString& Text,float Size) const
{
    return TextWidthFont(Text,Size,ECireFont::Auto);
}

// ---------------------------------------------------------------------------
// Scrolling combat text (floating numbers over targets + personal lanes)
// ---------------------------------------------------------------------------
namespace
{
FLinearColor SchoolColor(const FString& Ability)
{
    const FString N=Ability.ToLower();
    auto Any=[&](std::initializer_list<const TCHAR*> Words){for(const TCHAR* W:Words)if(N.Contains(W))return true;return false;};
    if(Any({TEXT("ember"),TEXT("fire"),TEXT("flame"),TEXT("burn"),TEXT("cataclysm"),TEXT("meteor")}))return FLinearColor(1.f,.46f,.14f,1);
    if(Any({TEXT("frost"),TEXT("ice"),TEXT("cold"),TEXT("glacial"),TEXT("chill")}))return FLinearColor(.45f,.82f,1.f,1);
    if(Any({TEXT("venom"),TEXT("poison"),TEXT("blight"),TEXT("nature"),TEXT("thorn"),TEXT("plague")}))return FLinearColor(.58f,1.f,.26f,1);
    if(Any({TEXT("shadow"),TEXT("void"),TEXT("soul"),TEXT("curse"),TEXT("dark"),TEXT("spectral")}))return FLinearColor(.74f,.46f,1.f,1);
    if(Any({TEXT("light"),TEXT("holy"),TEXT("sanct"),TEXT("dawn"),TEXT("purif"),TEXT("radian"),TEXT("smite")}))return FLinearColor(1.f,.95f,.58f,1);
    if(Any({TEXT("spark"),TEXT("lightning"),TEXT("storm"),TEXT("thunder"),TEXT("chain")}))return FLinearColor(.55f,.78f,1.f,1);
    if(Any({TEXT("arcane"),TEXT("rune"),TEXT("star"),TEXT("astral")}))return FLinearColor(1.f,.55f,.95f,1);
    return FLinearColor(1.f,.84f,.42f,1); // physical
}
}
void ACireHUD::DrawCombatText(ACireHero* Hero,ACireController* Controller)
{
    if(!UISettings.bCombatTextEnabled||!Controller||!Hero)return;
    UsePanel(TEXT("CombatText"),430,220);
    const FVector2D AnchoredOrigin=Origin,AnchoredStretch=Stretch;
    const float Now=GetWorld()->GetTimeSeconds();
    const float Life=UISettings.SCTFadeSeconds,FloatLife=FMath::Clamp(Life*.66f,1.2f,3.5f),Speed=UISettings.SCTSpeed;
    auto Eligible=[&](const FCireCombatEvent& E)
    {
        if(!E.bLocalTarget&&!E.bLocalSource)return false;
        if(!(E.bLocalTarget&&UISettings.bShowIncoming)&&!(E.bLocalSource&&UISettings.bShowOutgoing))return false;
        if(E.Outcome!=ECireHitOutcome::Hit&&!UISettings.bShowMisses)return false;
        return E.bHealing?UISettings.bShowHealing:UISettings.bShowDamage;
    };
    auto ColorFor=[&](const FCireCombatEvent& E,bool bIncomingLane)
    {
        if(E.Outcome!=ECireHitOutcome::Hit)return FLinearColor(.78f,.83f,.9f,1);
        if(E.bHealing)return FLinearColor(.35f,1.f,.55f,1);
        if(bIncomingLane)return FLinearColor(1.f,.28f,.24f,1);
        if(UISettings.bSchoolColors)return E.bCritical?SchoolColor(E.AbilityName)*1.1f+FLinearColor(.08f,.05f,0,0):SchoolColor(E.AbilityName);
        return E.bCritical?FLinearColor(1.f,.53f,.08f,1):FLinearColor(1.f,.84f,.42f,1);
    };
    auto NumberFor=[](const FCireCombatEvent& E,float Amount,bool bIncoming)
    {
        if(E.Outcome!=ECireHitOutcome::Hit)return FString(E.Outcome==ECireHitOutcome::Miss?TEXT("Miss"):TEXT("Dodge"));
        return FString::Printf(TEXT("%s%.0f"),E.bHealing?TEXT("+"):bIncoming?TEXT("-"):TEXT(""),Amount);
    };
    // Crit "pop": starts large and settles, WoW style.
    auto CritScale=[&](const FCireCombatEvent& E,float Age)
    {
        if(!E.bCritical)return 1.f+.18f*FMath::Clamp(1.f-Age/.14f,0.f,1.f);
        if(!UISettings.bCritPop)return 1.15f;
        const float T=FMath::Clamp(Age/.18f,0.f,1.f);
        return 1.3f+.95f*(1.f-T)*(1.f-T);
    };

    // The impact position and personal attribution are snapshots. A lethal hit
    // remains visible even after the affected actor has left the client world.
    TArray<FBox2D> Occupied,ProtectedPanels;
    for(FName Id:VisiblePanels)if(Id!=TEXT("CombatText")&&Id!=TEXT("Tooltip"))
    {
        const auto R=PanelRect(Id);
        ProtectedPanels.Emplace(FVector2D(R.X-5,R.Y-5),FVector2D(R.X+R.W+5,R.Y+R.H+5));
    }
    ResetTransform();
    for(int32 I=Controller->CombatEvents.Num()-1;UISettings.bShowFloatingNumbers&&I>=0&&Occupied.Num()<32;--I)
    {
        const auto& E=Controller->CombatEvents[I];const float Age=Now-E.TimeSeconds;
        if(Age<0||Age>FloatLife||!Eligible(E))continue;
        FVector2D Screen;
        if(!PlayerOwner->ProjectWorldLocationToScreen(E.Location,Screen,false))continue;
        const float Font=UISettings.WorldNumberFontSize*CritScale(E,Age);
        const FString Amount=NumberFor(E,E.Amount,E.bLocalTarget&&!E.bLocalSource);
        const float W=TextWidthFont(Amount,Font,ECireFont::Numbers),H=Font+3;
        const float Drift=static_cast<int32>(E.Sequence%3)-1;
        float X=Screen.X/Scale-W*.5f+Drift*(8.f+Age*9.f);
        float Y=Screen.Y/Scale-H-10.f-Age*38.f*Speed;
        if(X+W<0||X>ViewW||Y+H<0||Y>ViewH)continue;
        X=FMath::Clamp(X,3.f,FMath::Max(3.f,ViewW-W-3.f));
        // Keep floating numbers near their target while avoiding previous numbers and
        // user-positioned frames. The crit marker is part of the box.
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
        FLinearColor C=ColorFor(E,E.bLocalTarget&&!E.bLocalSource);C.A=FMath::Clamp((FloatLife-Age)/.65f,0.f,1.f);
        TextFx(Amount,X,Y,Font,C,ECireFont::Numbers,true,true);
        if(Marker)
        {
            TextFx(TEXT("CRIT"),X+W*.5f-11,Y-13,9,C,ECireFont::Heading,true,false);
            const float StarX=X-14,StarY=Y+Font*.45f;
            for(int32 Ray=0;Ray<8;++Ray){const float A=Ray*PI/4;const float R=Ray%2?5.f:9.f;
                Line(StarX+FMath::Cos(A)*2,StarY+FMath::Sin(A)*2,StarX+FMath::Cos(A)*R,StarY+FMath::Sin(A)*R,C,1.5f);}
        }
    }

    // Combine each ability's simultaneous area/burst hits into one SCT entry (optional).
    // Every affected target still gets its own world number.
    struct FScrollingEntry{const FCireCombatEvent* Event=nullptr;float Amount=0;int32 Hits=0,Lane=0;TSet<FName> Targets;};
    TArray<FScrollingEntry> Entries;
    for(int32 I=Controller->CombatEvents.Num()-1;UISettings.bShowScrollingText&&I>=0;--I)
    {
        const auto& E=Controller->CombatEvents[I];const float Age=Now-E.TimeSeconds;
        if(Age<0||Age>Life||!Eligible(E))continue;
        const int32 Lane=E.bLocalTarget&&UISettings.bShowIncoming?0:1;
        FScrollingEntry* Group=UISettings.bMergeAoE?Entries.FindByPredicate([&](const FScrollingEntry& Entry){
            return E.Outcome==ECireHitOutcome::Hit&&Entry.Event->Outcome==E.Outcome&&Entry.Lane==Lane&&Entry.Event->AbilityName==E.AbilityName&&
                Entry.Event->SourceId==E.SourceId&&Entry.Event->bHealing==E.bHealing&&Entry.Event->bCritical==E.bCritical&&
                FMath::Abs(Entry.Event->ServerTime-E.ServerTime)<=.12f;}):nullptr;
        if(!Group){FScrollingEntry Entry;Entry.Event=&E;Entry.Lane=Lane;Entries.Add(MoveTemp(Entry));Group=&Entries.Last();}
        Group->Amount+=E.Amount;++Group->Hits;Group->Targets.Add(E.TargetId);
    }
    // Personal lanes (incoming left, outgoing right) inside the movable CombatText panel.
    Origin=AnchoredOrigin;Stretch=AnchoredStretch;
    int32 Rows[2]={0,0};
    const float RowHeight=FMath::Max(39.f,UISettings.CombatTextFontSize+17.f);
    const int32 MaxRows=FMath::Max(1,FMath::FloorToInt(194.f/RowHeight));
    const int32 Direction=UISettings.SCTDirection;
    for(const auto& Entry:Entries)
    {
        const auto& E=*Entry.Event;const float Age=Now-E.TimeSeconds;
        const int32 Lane=Entry.Lane;if(Rows[Lane]>=MaxRows)continue;
        const float Drift=FMath::Min(Age*14.f*Speed,RowHeight*.5f);
        const int32 Row=++Rows[Lane];
        float Y=Direction==1?4.f+(Row-1)*RowHeight+Drift:191.f-Row*RowHeight-Drift;
        const float Arc=Direction==2?FMath::Min(Age*30.f*Speed,70.f)*(Lane==0?-1.f:1.f):0.f;
        FLinearColor C=ColorFor(E,Lane==0);C.A=FMath::Clamp((Life-Age)/(Life*.25f),0.f,1.f);
        const FString Amount=NumberFor(E,Entry.Amount,Lane==0&&!E.bHealing);
        const float SCTFont=UISettings.CombatTextFontSize*(E.bCritical?(UISettings.bCritPop?FMath::Lerp(1.55f,1.18f,FMath::Clamp(Age/.2f,0.f,1.f)):1.13f):1.f);
        const float NW=TextWidthFont(Amount,SCTFont,ECireFont::Numbers);
        const float NumberX=(Lane==0?182.f-NW:246.f)+Arc;
        if(E.bCritical&&UISettings.bShowCriticalSymbol)
        {
            const float SX=NumberX+NW*.5f,SY=Y+SCTFont*.55f;FLinearColor G=C;G.A*=.22f;
            for(int32 Ray=0;Ray<12;++Ray){const float A=Ray*PI/6+Age;const float R=Ray%2?SCTFont*.55f:SCTFont*.95f;
                Tri(FVector2D(SX,SY),FVector2D(SX+FMath::Cos(A-.14f)*R,SY+FMath::Sin(A-.14f)*R),FVector2D(SX+FMath::Cos(A+.14f)*R,SY+FMath::Sin(A+.14f)*R),G);}
        }
        TextFx(Amount,NumberX,Y,SCTFont,C,ECireFont::Numbers,true,true);
        const FString Recipient=Entry.Targets.Num()>1?FString::Printf(TEXT("%d targets"),Entry.Targets.Num()):
            Entry.Hits>1?FString::Printf(TEXT("%d hits"),Entry.Hits):Short(Lane==0?E.SourceName:E.TargetName,14);
        const FString Detail=(E.bCritical?TEXT("Critical  "):TEXT(""))+Short(E.AbilityName,17)+TEXT("  /  ")+Recipient;
        const float DW=TextWidthFont(Detail,10,ECireFont::Body);
        const float DetailX=(Lane==0?182.f-DW:258.f)+Arc;
        FLinearColor School=E.Outcome==ECireHitOutcome::Hit&&!E.bHealing?SchoolColor(E.AbilityName):C;School.A=C.A;
        if(Lane==1)Disc(250.f+Arc,Y+SCTFont+7.f,3.f,School,10);else Disc(188.f+Arc,Y+SCTFont+7.f,3.f,School,10);
        TextFx(Detail,DetailX,Y+SCTFont+1.f,10,FLinearColor(.9f,.92f,.92f,C.A*.95f),ECireFont::Body,true,false);
    }
    if(Rows[0]>0)TextFx(TEXT("INCOMING"),125,201,9,FLinearColor(1.f,.52f,.48f,.8f),ECireFont::Heading,true,false);
    if(Rows[1]>0)TextFx(TEXT("OUTGOING"),246,201,9,FLinearColor(1.f,.84f,.42f,.8f),ECireFont::Heading,true,false);
    ResetTransform();
}

// ---------------------------------------------------------------------------
// Nameplates: reaction-coloured plates, aggro glow, cast bars, WoW-style selection
// ---------------------------------------------------------------------------
void ACireHUD::DrawNameplates(ACireHero* Hero)
{
    ResetTransform();
    const auto* State=GetWorld()->GetGameState<ACireGameState>();const bool bArena=State&&State->Phase==2;int32 Count=0;
    const auto* Controller=Cast<ACireController>(PlayerOwner);
    const bool bTank=IsTank(Hero);const float Now=ServerNow(GetWorld());
    const float Pulse=.5f+.5f*FMath::Sin(GetWorld()->GetRealTimeSeconds()*5.f);
    auto Plate=[&](AActor* Actor,const FString& Name,float HP,float MaxHP,FLinearColor Color,float Lift,const ACireMonster* Mob)
    {
        if(!IsValid(Actor)||Actor==Hero||HP<=0||MaxHP<=0)return;
        const bool Selected=Hero->Target==Actor,Focused=Controller&&Controller->FocusTarget==Actor;
        const float Dist=FVector::Dist(Hero->GetActorLocation(),Actor->GetActorLocation());
        if(!Selected&&(Count>=32||Dist>2400.f))return;
        FVector2D Screen;if(!PlayerOwner->ProjectWorldLocationToScreen(Actor->GetActorLocation()+FVector(0,0,Lift),Screen,false))return;
        const float X=Screen.X/Scale,Y=Screen.Y/Scale;if(X<35||X>ViewW-35||Y<105||Y>ViewH-185)return;
        ++Count;
        // Aggro state: DPS/healers are warned when an enemy is on them or about to be;
        // tanks are warned when an engaged enemy is NOT on them.
        FLinearColor Glow(0,0,0,0);
        if(Mob&&IsValid(Mob->Victim))
        {
            const float Pull=PullPercent(Mob,Hero);
            if(Mob->Victim==Hero)Glow=bTank?Friendly*FLinearColor(1,1,1,.55f):Hostile;
            else if(bTank)Glow=Orange;
            else if(Pull>=UISettings.ThreatWarningPercent)Glow=Orange*FLinearColor(1,1,1,.85f);
        }
        const float Fade=Selected?1.f:FMath::Clamp(1.4f-Dist/2400.f,.55f,1.f);
        const float PW=Selected?150.f:70.f,PH=Selected?11.f:6.f,PX=X-PW*.5f;
        // Name above the bar (reaction coloured; white when selected).
        const float NS=Selected?12.f:8.5f;
        if(Selected||Dist<1700.f||Glow.A>0)
        {
            const FString Label=Short(Name,Selected?26:20);
            TextFx(Label,X-TextWidthFont(Label,NS,ECireFont::Bold)*.5f,Y-NS-5.f,NS,(Selected?FLinearColor::White:Color)*FLinearColor(1,1,1,Fade),ECireFont::Bold,true,false);
        }
        if(Glow.A>0){const float G=Selected?3.f:2.f;Panel(PX-G,Y-G,PW+2*G,PH+2*G,Glow*FLinearColor(1,1,1,.55f+.35f*Pulse));}
        Panel(PX-1,Y-1,PW+2,PH+2,FLinearColor(0,0,0,.9f*Fade));
        const float HF=Frac(HP,MaxHP);
        Panel(PX,Y,PW*HF,PH,Color*FLinearColor(1,1,1,Fade));Panel(PX,Y,PW*HF,PH*.4f,FLinearColor(1,1,1,.14f*Fade));
        if(Selected)
        {
            const FString Pct=FString::Printf(TEXT("%.0f%%"),HF*100);
            TextFx(Pct,X-TextWidthFont(Pct,8.5f,ECireFont::Numbers)*.5f,Y-.5f,8.5f,FLinearColor::White,ECireFont::Numbers,true,false);
            // Gold selection chevrons that breathe, and a marker arrow above the name.
            const float Off=6.f+2.f*Pulse;const FLinearColor Ch(1.f,.84f,.3f,.9f);
            Tri(FVector2D(PX-Off-7,Y+PH*.5f),FVector2D(PX-Off,Y-2),FVector2D(PX-Off,Y+PH+2),Ch);
            Tri(FVector2D(PX+PW+Off+7,Y+PH*.5f),FVector2D(PX+PW+Off,Y-2),FVector2D(PX+PW+Off,Y+PH+2),Ch);
            const float AY=Y-NS-14.f-3.f*Pulse;
            Tri(FVector2D(X-7,AY-8),FVector2D(X+7,AY-8),FVector2D(X,AY),Ch);
            Line(PX,Y-1,PX+PW,Y-1,FLinearColor(1,1,1,.9f),1.f);Line(PX,Y+PH+1,PX+PW,Y+PH+1,FLinearColor(1,1,1,.9f),1.f);
        }
        if(Focused&&!Selected)Tri(FVector2D(PX-9,Y+PH*.5f),FVector2D(PX-3,Y-2),FVector2D(PX-3,Y+PH+2),FLinearColor(.4f,.8f,1.f,.9f));
        if(Mob)
        {
            // Elite / boss marker on the right end of the plate.
            if(IsEliteOrBoss(Mob))
            {
                const FLinearColor D=IsBossClass(Mob)?Hostile:WowGold;const float EX=PX+PW+3.f;
                Tri(FVector2D(EX,Y-3),FVector2D(EX+7,Y+PH*.5f),FVector2D(EX,Y+PH+3),D);
            }
            if(Mob->Victim==Hero&&!bTank)
            {
                // "Focusing you" marker: a red diamond at the left end.
                const float DX=PX-(Selected?24.f:8.f),DY=Y+PH*.5f;
                Tri(FVector2D(DX,DY-6),FVector2D(DX+5,DY),FVector2D(DX-5,DY),Hostile);Tri(FVector2D(DX,DY+6),FVector2D(DX+5,DY),FVector2D(DX-5,DY),Hostile);
            }
            const FCireNPCCastInfo Cast=Mob->NPCState?Mob->NPCState->CastInfo():FCireNPCCastInfo();
            if(Cast.bCasting&&Cast.Remaining>0)
            {
                const float CY=Y+PH+3.f,CH=Selected?7.f:4.f;
                Panel(PX-1,CY-1,PW+2,CH+2,FLinearColor(0,0,0,.9f));Panel(PX,CY,PW*Cast.Progress,CH,Cast.bInterruptible?CastGold:FLinearColor(.58f,.6f,.66f,1));
                if(Selected)TextFx(Short(Cast.Name,22),PX+2,CY+CH-1,8,FLinearColor::White,ECireFont::Bold,true,false);
            }
        }
        const int32 Poisoned=Mob?Mob->PoisonAreaCount:0;
        if(Poisoned>0&&Selected){const FString P=FString::Printf(TEXT("POISON x%d"),Poisoned);TextFx(P,X-TextWidthFont(P,8,ECireFont::Heading)*.5f,Y+PH+10,8,FLinearColor(.61f,.83f,.27f,1),ECireFont::Heading,true,false);}
    };
    for(TActorIterator<ACireHero> It(GetWorld());It;++It)if(bArena||It->TeamId==Hero->TeamId)
        Plate(*It,It->HeroName,It->Health,It->MaxHealth,It->TeamId==Hero->TeamId?Friendly*.85f:Hostile,120,nullptr);
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It)if(!bArena&&It->Lane==Hero->TeamId)
        Plate(*It,It->GetNPCDisplayName(),It->Health,It->MaxHealth,It->bArmoredEscort?Silver*.8f:Hostile*.9f,100,*It);
    for(TActorIterator<ACireConstruct> It(GetWorld());It;++It)if(It->CanObserve(PlayerOwner))
        Plate(*It,It->GetDisplayName(),It->Health,It->MaxHealth,It->OriginTeam==Hero->TeamId?Friendly*.85f:Hostile,It->ConstructSpec.Height*.5f+25,nullptr);
}

// ---------------------------------------------------------------------------
// Transition banners: detected client-side from replicated state (works on LAN
// clients too), queued through CireBanners so they never overlap.
// ---------------------------------------------------------------------------
void ACireHUD::UpdateBanners(ACireHero* Hero,ACireGameState* State)
{
    if(!Hero||!State)return;
    const bool bFirst=BannerSeenPhase<0;
    if(!bFirst&&State->Phase!=BannerSeenPhase)
    {
        const int32 Seconds=FMath::Max(0,FMath::RoundToInt(State->SecondsLeft));
        switch(State->Phase)
        {
        case 0:CireBanners::Show(ECireBanner::WaveIncoming,TEXT("Survival"),TEXT("Hold your lane. Three cleared waves lead back to town."),TEXT("THE GATES OPEN"));break;
        case 1:CireBanners::Show(ECireBanner::PrepPhase,TEXT("Prep Phase"),FString::Printf(TEXT("%d seconds to buy gear and tomes before the portal opens."),Seconds));break;
        case 2:CireBanners::Show(ECireBanner::Arena,TEXT("Arena"),TEXT("Both companies meet in the portal battlefield."));break;
        case 4:CireBanners::Show(ECireBanner::Recovery,TEXT("Recovery"),FString::Printf(TEXT("%d seconds to regroup and resupply."),Seconds));break;
        case 3:
        {
            const int32 Mine=Hero->TeamId==0?State->EmberLives:State->DuskLives,Theirs=Hero->TeamId==0?State->DuskLives:State->EmberLives;
            const int32 MyWins=Hero->TeamId==0?State->EmberWins:State->DuskWins,TheirWins=Hero->TeamId==0?State->DuskWins:State->EmberWins;
            const bool bWon=Mine!=Theirs?Mine>Theirs:MyWins>=TheirWins;
            CireBanners::Show(bWon?ECireBanner::Victory:ECireBanner::Defeat,bWon?TEXT("Victory"):TEXT("Defeat"),State->Announcement);
            break;
        }
        default:break;
        }
    }
    if(State->Phase==0&&!bFirst)
    {
        // Telegraph the next wave five seconds ahead; otherwise announce it as it spawns.
        const int32 Next=State->Wave+1;
        if(State->NextWaveSeconds>.05f&&State->NextWaveSeconds<=5.f&&BannerCountdownWave!=Next)
        {
            BannerCountdownWave=Next;
            CireBanners::Show(ECireBanner::WaveIncoming,FString::Printf(TEXT("Wave %d"),Next),TEXT("Incoming in 5 seconds."));
        }
        if(State->Wave>BannerSeenWave&&BannerCountdownWave!=State->Wave)
            CireBanners::Show(ECireBanner::WaveIncoming,FString::Printf(TEXT("Wave %d"),State->Wave),TEXT("The horde is at the breach."));
        if(State->CycleWavesDone>BannerSeenCleared&&State->CycleWavesDone>0)
            CireBanners::Show(ECireBanner::WaveCleared,TEXT("Wave Cleared"),FString::Printf(TEXT("%d of %d waves this cycle."),State->CycleWavesDone,State->WavesPerCycle));
    }
    BannerSeenPhase=State->Phase;BannerSeenWave=State->Wave;BannerSeenCleared=State->CycleWavesDone;
    // Zone text: entering a town district ("Market District"), once it has been held for a moment.
    const FName District=CireEnvironmentProps::DistrictAt(GetWorld(),Hero->TeamId,Hero->GetActorLocation());
    const double Now=GetWorld()->GetRealTimeSeconds();
    if(District!=BannerPendingDistrict){BannerPendingDistrict=District;BannerDistrictSince=Now;}
    if(!District.IsNone()&&District!=BannerShownDistrict&&Now-BannerDistrictSince>.8)
    {
        if(!BannerShownDistrict.IsNone()||!bFirst)CireBanners::Show(ECireBanner::Custom,CireEnvironmentProps::DistrictName(District),FString(),TEXT("ENTERING"));
        BannerShownDistrict=District;
    }
    // Bosses and challenge tiers appearing in your lane.
    for(auto It=BannerSeenBosses.CreateIterator();It;++It)if(!It->IsValid())It.RemoveCurrent();
    for(TActorIterator<ACireMonster> It(GetWorld());It;++It)
    {
        ACireMonster* M=*It;
        if(M->Health<=0||M->Lane!=Hero->TeamId||M->GetNPCClassification()!=ECireNPCClass::Boss||BannerSeenBosses.Contains(M))continue;
        BannerSeenBosses.Add(M);
        if(bFirst)continue;
        if(M->IsLaneBoss())CireBanners::Show(ECireBanner::BossSpawned,M->GetNPCDisplayName(),TEXT("A boss marches on your keep. If it leaks, you lose 10 lives."));
        else if(M->Tier>BannerSeenChallengeTier)
            CireBanners::Show(ECireBanner::ChallengeUnlocked,FString::Printf(TEXT("Challenge Tier %d"),M->Tier),M->GetNPCDisplayName()+TEXT(" guards the outpost. Clear the pack for rare rewards."));
        else CireBanners::Show(ECireBanner::BossSpawned,M->GetNPCDisplayName(),TEXT("A pack leader has appeared."),TEXT("PACK LEADER"));
        BannerSeenChallengeTier=FMath::Max(BannerSeenChallengeTier,M->Tier);
    }
}
void ACireHUD::DrawBanners()
{
    ResetTransform();
    ECireBanner Started=ECireBanner::Custom;
    // Banners sit just below the target frame so they never cover the frame being read.
    const FCireUIRect Target=PanelRect(TEXT("Target"));
    const float Y=FMath::Clamp(FMath::Max(ViewH*.15f,Target.Y+Target.H+34.f),40.f,ViewH*.42f);
    if(CireBanners::Draw(Painter(),ViewW,ViewH,Started,Y))
    {
        if(!UISettings.bMuteAudio&&CireAudio::PlayBanner(this,static_cast<uint8>(Started)))return; // audio: horn / bell / war drums
        switch(Started)
        {
        case ECireBanner::LevelUp:break; // the level-up burst plays its own chime
        case ECireBanner::Victory:PlayWowSound(0,1.f);break;
        case ECireBanner::Defeat:PlayWowSound(3,1.f);break;
        case ECireBanner::WaveCleared:case ECireBanner::PrepPhase:case ECireBanner::Recovery:PlayWowSound(6,.9f);break;
        default:PlayWowSound(5,.9f);break;
        }
    }
}
