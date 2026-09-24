#include "CireHUD.h"
#include "CireGame.h"
#include "CireSummon.h"
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
    if(!IsValid(Actor))return;
    const auto* State=GetWorld()->GetGameState<ACireGameState>();
    const float Now=State?State->GetServerWorldTimeSeconds():GetWorld()->GetTimeSeconds();
    struct FStatus{FString Id,Name,Description;float Remaining;bool bDebuff,bDispel;FLinearColor Color;};
    TArray<FStatus> Effects;
    const auto* H=Cast<ACireHero>(Actor);const auto* M=Cast<ACireMonster>(Actor);
    if(H&&!H->bDead)
    {
        if(H->ShieldUntil>Now)Effects.Add({TEXT("iron_guard"),TEXT("Guarded"),TEXT("Incoming damage is reduced by 40%. This beneficial guard is dispellable."),H->ShieldUntil-Now,false,true,Blue});
        if(H->TauntUntil>Now)Effects.Add({TEXT("war_cry"),TEXT("Commanding presence"),TEXT("Nearby enemies have been compelled to focus this champion. This taunt effect cannot be dispelled."),H->TauntUntil-Now,false,false,Gold});
        for(const FString& Id:H->Skills)if(ACireHero::IsPassive(Id))Effects.Add({Id,ACireHero::SkillName(Id),ACireHero::SkillDescription(Id),-1,false,false,Purple});
    }
    const float Slow=H?H->SlowUntil:M?M->SlowUntil:0;
    if(Slow>Now)Effects.Add({TEXT("frost_bind"),TEXT("Slowed"),TEXT("Movement speed is reduced by 35%. This harmful effect can be cleansed."),Slow-Now,true,true,Purple});
    const int32 Count=H?H->PoisonAreaCount:M?M->PoisonAreaCount:0;
    if(Count>0)
    {
        const float End=H?H->PoisonEndsAt:M->PoisonEndsAt;
        Effects.Add({TEXT("venom_ground"),FString::Printf(TEXT("Ground poison x%d"),Count),TEXT("Standing in poisonous ground. Leave the affected area to remove the poison immediately. Each overlapping area acts independently. Ground poison cannot be dispelled."),FMath::Max(0.f,End-Now),true,false,Poison});
    }
    Effects.RemoveAll([&](const FStatus& E){return (UISettings.StatusFilter==1&&E.bDebuff)||(UISettings.StatusFilter==2&&!E.bDebuff)||(UISettings.bDispellableOnly&&!E.bDispel);});
    const int32 CountVisible=FMath::Min(Effects.Num(),Effects.Num()>MaxIcons?FMath::Max(0,MaxIcons-1):MaxIcons);
    for(int32 I=0;I<CountVisible;++I)
    {
        const auto& E=Effects[I];const float At=X+I*(Size+4);
        Frame(At,Y,Size,Size,E.bDebuff?Red:E.Color);Icon(E.Id,At+2,Y+2,Size-4,E.Color);
        if(E.bDispel)Panel(At+Size-4,Y+1,3,3,Gold);
        const FString Time=E.Remaining<0?TEXT(""):E.Remaining>0?Duration(E.Remaining):TEXT("AREA");
        if(UISettings.bShowStatusDurations&&!Time.IsEmpty())
        {Panel(At,Y+Size-7,Size,9,FLinearColor(0,0,0,.8f));Label(Time,At+1,Y+Size-8,8,FLinearColor::White);}
        Tip(E.Name,E.Description+TEXT(" Remaining: ")+(E.Remaining<0?TEXT("Permanent while learned."):Time+TEXT(".")),At,Y,Size,Size);
    }
    if(Effects.Num()>CountVisible)
    {
        const float At=X+CountVisible*(Size+4);Label(FString::Printf(TEXT("+%d"),Effects.Num()-CountVisible),At,Y+4,9,Muted);
        FString Extra;for(int32 I=CountVisible;I<Effects.Num();++I)Extra+=Effects[I].Name+TEXT(" ")+Duration(Effects[I].Remaining)+TEXT(". ");
        Tip(TEXT("Additional statuses"),Extra,At,Y,22,Size);
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
