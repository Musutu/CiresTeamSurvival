#include "CireHUD.h"
#include "CireGame.h"
#include "CireSummon.h"
#include "CireBuffs.h"
#include "CireEffects.h"
#include "CireUIStyle.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
const FLinearColor Gold(.77f,.61f,.34f,1),Muted(.50f,.57f,.59f,1),Red(.75f,.20f,.23f,1),Blue(.23f,.46f,.8f,1),Purple(.66f,.46f,.83f,1),Poison(.61f,.83f,.27f,1);
FString Duration(float Seconds)
{
    if(Seconds<0)return TEXT("PASSIVE");
    return Seconds>=60?FString::Printf(TEXT("%dm"),FMath::CeilToInt(Seconds/60)):FString::Printf(TEXT("%ds"),FMath::Max(1,FMath::CeilToInt(Seconds)));
}
}
void ACireHUD::DrawStatuses(AActor* Actor,float X,float Y,float Size,int32 MaxIcons)
{
    // WoW buff/debuff row from the modifier registry (CireEffects): coloured borders by
    // type (gold buff, blue magic, green poison, purple curse, red physical), a darkening
    // duration sweep, stack counts and concise symbol tooltips ("DEF +40%  ·  8s").
    if(!IsValid(Actor)||MaxIcons<=0)return;
    const float Now=CireBuffs::ServerNow(GetWorld());
    const AActor* Local=PlayerOwner?PlayerOwner->GetPawn():nullptr;
    TArray<FCireActiveEffect> Effects;CireEffects::Gather(Actor,Now,Effects,Local);
    Effects.RemoveAll([&](const FCireActiveEffect& E){
        const FCireEffectInfo* I=CireEffects::Find(E.Id);if(!I)return true;
        const bool bDebuff=I->IsHarmful(),bDispel=I->Dispel==ECireDispel::Magic||I->Dispel==ECireDispel::Poison||I->Dispel==ECireDispel::Curse||I->Dispel==ECireDispel::Disease;
        return (UISettings.StatusFilter==1&&bDebuff)||(UISettings.StatusFilter==2&&!bDebuff)||(UISettings.bDispellableOnly&&!bDispel);});
    const int32 Visible=FMath::Min(Effects.Num(),Effects.Num()>MaxIcons?FMath::Max(0,MaxIcons-1):MaxIcons);
    FCireUIPainter P=Painter();
    for(int32 Index=0;Index<Visible;++Index)
    {
        const FCireActiveEffect& E=Effects[Index];const FCireEffectInfo& I=*CireEffects::Find(E.Id);
        const float At=X+Index*(Size+3);
        const float Remaining=E.End>Now?E.End-Now:-1.f,Total=E.End>E.Start?E.End-E.Start:0.f;
        DrawEffectIcon(E,I,At,Y,Size,Remaining,Total);
        FString Body=CireEffects::Symbols(I,Remaining);
        if(Remaining>0)Body+=(Body.IsEmpty()?TEXT(""):TEXT("  ·  "))+CireEffects::DurationText(Remaining)+TEXT(" left");
        else if(I.Kind==ECireEffectKind::Passive)Body+=(Body.IsEmpty()?TEXT(""):TEXT("  ·  "))+FString(TEXT("Passive"));
        if(E.Stacks>1)Body+=FString::Printf(TEXT("  ·  %d stacks"),E.Stacks);
        Body+=TEXT("\n")+I.Line;
        const TCHAR* Types[]={TEXT(""),TEXT("Magic"),TEXT("Poison"),TEXT("Curse"),TEXT("Disease"),TEXT("Physical")};
        if(I.Dispel!=ECireDispel::None)Body+=FString(TEXT("\n"))+Types[static_cast<int32>(I.Dispel)]+(I.Dispel==ECireDispel::Physical?TEXT(" (cannot be dispelled)"):TEXT(" (dispellable)"));
        if(E.Id==TEXT("poisoned"))Body+=TEXT("\nLeave the poisoned area to remove it.");
        if(E.bFromLocalPlayer)Body+=TEXT("\nApplied by you.");
        Tip(I.Name,Body,At,Y,Size,Size);
    }
    if(Effects.Num()>Visible)
    {
        const float At=X+Visible*(Size+3);P.Text(FString::Printf(TEXT("+%d"),Effects.Num()-Visible),At,Y+Size*.25f,FMath::Max(8.f,Size*.45f),Muted,ECireFont::Numbers,true,false);
        FString Extra;for(int32 Index=Visible;Index<Effects.Num();++Index)if(const auto* I=CireEffects::Find(Effects[Index].Id))Extra+=I->Name+TEXT(": ")+CireEffects::Symbols(*I)+TEXT("\n");
        Tip(TEXT("More effects"),Extra,At,Y,22,Size);
    }
}
FString ACireHUD::StatusIconId(const FCireEffectInfo& I)
{
    // Painted status icons (/Game/UI/Abilities/T_status_<name>, generated for Eric via ChatGPT) for
    // effects without their own ability art; empty -> the procedural sigil below.
    switch(I.Control)
    {
    case ECireControl::Stun:return TEXT("status_stun");
    case ECireControl::Silence:return TEXT("status_silence");
    case ECireControl::Root:return TEXT("status_root");
    case ECireControl::HealCut:return TEXT("status_heal_cut");
    case ECireControl::Slow:return TEXT("status_slow");
    case ECireControl::Taunt:return TEXT("status_taunt");
    case ECireControl::Disarm:return TEXT("status_disarm");
    case ECireControl::Fear:return TEXT("status_fear");
    default:break;
    }
    if(I.Mods.Num()&&I.Mods[0].Value<0&&(I.Mods[0].Stat==TEXT("DEF")||I.Mods[0].Stat==TEXT("Armor")))return TEXT("status_armor_break");
    if(I.IsHarmful()&&I.Dispel==ECireDispel::Poison)return TEXT("status_poison");
    if(I.IsHarmful()&&I.Dispel==ECireDispel::Curse)return TEXT("status_curse");
    return FString();
}
FString ACireHUD::EffectSigil(FName Id,const FCireEffectInfo& I)
{
    // Distinct symbols for effects without painted art: by control, then the leading stat,
    // then the dispel type. Ability ids reuse their own ability sigil.
    static const TSet<FName> OwnSigil={TEXT("iron_guard"),TEXT("war_cry"),TEXT("sanctuary"),TEXT("bastion_of_dawn"),TEXT("frost_bind"),TEXT("shield_slam"),TEXT("battle_rhythm"),TEXT("soul_conduit")};
    if(OwnSigil.Contains(Id))return Id.ToString();
    switch(I.Control)
    {
    case ECireControl::Stun:return TEXT("chain_spark");
    case ECireControl::Silence:return TEXT("role_caster");
    case ECireControl::Root:return TEXT("runic_wall");
    case ECireControl::HealCut:return TEXT("cataclysm");
    case ECireControl::Slow:return TEXT("frost_bind");
    case ECireControl::Taunt:return TEXT("war_cry");
    default:break;
    }
    if(I.Mods.Num())
    {
        const FString& S=I.Mods[0].Stat;const bool bUp=I.Mods[0].Value>=0;
        if(S==TEXT("DEF")||S==TEXT("Armor"))return bUp?TEXT("iron_guard"):TEXT("cleaving_strike");
        if(S==TEXT("ATK")||S==TEXT("Damage Dealt"))return bUp?TEXT("executioners_verdict"):TEXT("cleaving_strike");
        if(S==TEXT("Move"))return TEXT("shadow_step");
        if(S==TEXT("Haste"))return TEXT("battle_rhythm");
        if(S.Contains(TEXT("Heal"))||S==TEXT("HP Regen"))return TEXT("restoring_light");
        if(S==TEXT("Mana Regen"))return TEXT("deep_reserves");
        if(S==TEXT("HP"))return TEXT("venom_ground");
    }
    switch(I.Dispel)
    {
    case ECireDispel::Poison:return TEXT("venom_ground");
    case ECireDispel::Curse:return TEXT("cataclysm");
    case ECireDispel::Magic:return I.IsHarmful()?TEXT("frost_bind"):TEXT("sanctuary");
    default:return I.IsHarmful()?TEXT("cleaving_strike"):TEXT("renewal");
    }
}
void ACireHUD::DrawEffectIcon(const FCireActiveEffect& E,const FCireEffectInfo& I,float X,float Y,float Size,float Remaining,float Total)
{
    FCireUIPainter P=Painter();
    const FLinearColor Border=CireEffects::BorderColor(I);
    const float Pulse=Remaining>0&&Remaining<3.f?.55f+.45f*FMath::Sin(static_cast<float>(GetWorld()->GetRealTimeSeconds())*9.f):1.f; // expiring blink
    P.Rect(X-1,Y-1,Size+2,Size+2,FLinearColor(0,0,0,.9f));
    UTexture2D* Tex=CireUIStyle::FindAbilityIcon(E.Id.ToString());
    if(!Tex)Tex=CireUIStyle::FindAbilityIcon(StatusIconId(I)); // painted crowd-control / armor-break art
    if(!Tex)Tex=CireUIStyle::FindAbilityIcon(EffectSigil(E.Id,I)); // painted art of the ability whose symbol it borrows
    if(Tex)P.Tex(Tex,X+1,Y+1,Size-2,Size-2,FLinearColor(1,1,1,Pulse));
    else
    {
        P.Rect(X+1,Y+1,Size-2,Size-2,Border*FLinearColor(.25f,.25f,.25f,1));
        if(const auto& Kit=CireUIStyle::Assets();Kit.IconBg)P.Tex(Kit.IconBg,X+1,Y+1,Size-2,Size-2,Border*FLinearColor(.45f,.45f,.45f,1));
        CireUIStyle::Sigil(P,EffectSigil(E.Id,I),X+Size*.12f,Y+Size*.12f,Size*.76f,FLinearColor(1,1,1,.95f*Pulse));
    }
    if(Total>0&&Remaining>0)CireUIStyle::CooldownSweep(P,X+1,Y+1,Size-2,1.f-Remaining/Total);
    const float W=E.bFromLocalPlayer?2.f:1.3f;
    P.Line(X,Y,X+Size,Y,Border,W);P.Line(X,Y+Size,X+Size,Y+Size,Border,W);P.Line(X,Y,X,Y+Size,Border,W);P.Line(X+Size,Y,X+Size,Y+Size,Border,W);
    if(E.Stacks>1)P.Text(FString::FromInt(E.Stacks),X+Size-P.TextWidth(FString::FromInt(E.Stacks),Size*.42f,ECireFont::Numbers)-1,Y+Size*.5f,Size*.42f,FLinearColor::White,ECireFont::Numbers,true,false);
    if(UISettings.bShowStatusDurations&&Remaining>0&&Size>=15)
    {
        const FString T=CireEffects::DurationText(Remaining);const float TS=FMath::Max(7.f,Size*.36f);
        P.Text(T,X+(Size-P.TextWidth(T,TS,ECireFont::Numbers))*.5f,Y+Size-TS*.35f,TS,Remaining<3.f?FLinearColor(1.f,.4f,.35f,1):FLinearColor(1.f,.95f,.8f,1),ECireFont::Numbers,true,false);
    }
}
void ACireHUD::DrawPet(ACireHero* Hero,ACireController* Controller)
{
    if(!Hero||!Controller)return;
    TArray<ACireSummon*> Pets;
    for(TActorIterator<ACireSummon> It(GetWorld());It;++It)if(It->GetOwnerHero()==Hero&&!It->bDead)Pets.Add(*It);
    if(Pets.IsEmpty()&&!bEditLayout)return;
    UsePanel(TEXT("Pet"),250,90);Frame(0,0,250,90,Purple);
    Label(FString::Printf(TEXT("SUMMONS / %d ACTIVE"),Pets.Num()),9,7,10,Gold);
    int32 Commandable=0;float HP=0,MaxHP=0;
    for(auto* P:Pets){if(P->bCommandable)++Commandable;HP+=P->Health;MaxHP+=P->MaxHealth;}
    Bar(9,27,230,10,MaxHP>0?HP/MaxHP:0,Purple);
    Label(Commandable>0?TEXT("GUARDIAN COMMANDS"):TEXT("AUTONOMOUS ALLIES"),9,42,8,Muted);
    if(bEditLayout)Tip(TEXT("Summoned units"),TEXT("Temporary allied units can be targeted like other characters. Guardians accept commands; spectral packs choose their own targets. Summons do not count as player lives or team elimination."),0,0,250,42);
    const TCHAR* Names[]={TEXT("FOLLOW"),TEXT("MOVE"),TEXT("ATTACK"),TEXT("HOLD")};
    const TCHAR* Details[]={TEXT("Your commandable guardian returns to follow you."),TEXT("Click this command, then click the ground to move controlled summons. Escape cancels. Shift + left click on ground also issues a move command."),TEXT("Order your guardian to attack your selected hostile target."),TEXT("Hold the guardian at its current position.")};
    for(int32 I=0;I<4;++I)
    {
        const float X=8+I*60;Frame(X,59,56,23,Commandable>0?Purple:Muted*.5f);Label(Names[I],X+5,65,8,Commandable>0?Gold:Muted);
        Tip(Names[I],Details[I],X,59,56,23);
        if(Commandable>0&&Clicked&&Hit(X,59,56,23)&&!bModal&&!bSettings&&!bEditLayout)
        {
            if(I==1){Controller->bSummonMoveTargeting=true;Hero->Notice=TEXT("Click ground to move controlled summons. Escape cancels.");}
            else Controller->ServerSummonCommand(I,Hero->Target,Hero->GetActorLocation());
            Clicked=false;PlayUIFeedback();
        }
    }
}
