#include "CireOptionsGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireHUD.h"
#include "CireUISettings.h"
#include "CireDeveloperTools.h"
#include "CireChampionRoster.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
namespace
{
struct FGallery
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireHUD> HUD;
    TWeakObjectPtr<ACireController> PC;
    TWeakObjectPtr<ACireHero> Hero;
    TWeakObjectPtr<ACireMonster> Monster;
    TArray<TWeakObjectPtr<ACireHero>> Allies;
    FString Directory;
    TArray<FString> Files;
    double Start=0,Ready=-1;
    int32 Captured=-1;
    bool bDone=false,bPass=true;
};
FGallery G;
void Finish(bool Pass)
{
    if(G.bDone)return;G.bDone=true;
    UE_LOG(LogTemp,Display,TEXT("CIRE_OPTIONS_GALLERY_%s captures=%d directory=%s"),Pass?TEXT("PASS"):TEXT("FAIL"),G.Files.Num(),*G.Directory);
    FPlatformMisc::RequestExitWithStatus(false,Pass?0:1);
}
bool Setup(ACireGameMode* Mode,ACireController* PC,ACireHUD* HUD)
{
    G.PC=PC;G.HUD=HUD;G.Hero=Cast<ACireHero>(PC->GetPawn());if(!G.Hero.IsValid())return false;
    // Isolated profile path: capture settings never write to the player's INI.
    HUD->UISettings.Load(FPaths::Combine(G.Directory,TEXT("preview-profile.ini")));
    HUD->UISettings.bShowFPS=true;HUD->UISettings.bShowNetwork=true;
    auto* H=G.Hero.Get();H->Draft(0);H->Offers.Reset();H->bBot=false;H->bAutoAttack=false;H->HeroName=TEXT("Iron Warden");
    H->Health=1250;H->MaxHealth=1650;H->Mana=320;H->MaxMana=420;H->Level=12;
    H->Skills={TEXT("shield_slam"),TEXT("war_cry"),TEXT("frost_bind"),TEXT("ember_lance"),TEXT("restoring_light"),TEXT("runic_wall"),TEXT("stone_skin"),TEXT("bastion_of_dawn")};
    H->Cooldowns.Init(0,H->Skills.Num());
    PC->bShop=false;PC->bHelp=false;
    const FVector P=H->GetActorLocation();
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const TCHAR* Names[]={TEXT("Ash Ranger"),TEXT("Veil Scholar"),TEXT("Lancer"),TEXT("Rift Summoner")};
    for(int32 I=0;I<4;++I)
    {
        auto* A=Mode->GetWorld()->SpawnActor<ACireHero>(P+FVector(160,180+I*105,0),FRotator::ZeroRotator,Params);
        if(!A)return false;A->Draft(I+1);A->Offers.Reset();A->TeamId=H->TeamId;A->HeroName=Names[I];A->bBot=false;
        A->bAutoAttack=false;A->SetActorTickEnabled(false);A->GetCharacterMovement()->DisableMovement();
        A->MaxHealth=900;A->Health=540+I*60;G.Allies.Add(A);
    }
    PC->FocusTarget=G.Allies[1].Get();
    G.Monster=Mode->GetWorld()->SpawnActor<ACireMonster>(P+FVector(350,0,0),FRotator(0,180,0),Params);
    if(!G.Monster.IsValid())return false;
    G.Monster->Health=1850;G.Monster->MaxHealth=2400;G.Monster->MonsterName=TEXT("Grave Caster");G.Monster->Lane=H->TeamId;
    G.Monster->Victim=H;G.Monster->CastingAbility=TEXT("npc_shadow_bolt");G.Monster->SetActorTickEnabled(false);
    G.Monster->GetCharacterMovement()->DisableMovement();H->Target=G.Monster.Get();
    // Check every page/key mapping without issuing a draft or changing skills.
    G.bPass &= HUD->DraftRosterPageCount()==FMath::DivideAndRoundUp(CireChampionRoster::Count(),6);
    for(int32 Page=0;Page<HUD->DraftRosterPageCount();++Page)
    {
        HUD->DebugDraftRosterPage(Page);
        for(int32 Slot=0;Slot<6;++Slot)
        {
            const auto* Profile=CireChampionRoster::FindByIndex(Page*6+Slot);
            G.bPass &= HUD->DraftRosterIdForSlot(Slot)==(Profile?Profile->Id:FString());
        }
    }
    G.bPass &= HUD->DraftRosterIdForSlot(-1).IsEmpty()&&HUD->DraftRosterIdForSlot(6).IsEmpty();
    HUD->DebugDraftRosterPage(0);
    G.bPass &= CireOptions::RunSettingsSmoke();G.bPass &= CireDeveloperTools::RunRuntimeSmoke(Mode);G.Ready=FPlatformTime::Seconds();return true;
}
}
bool CireOptionsGallery::Initialize(ACireGameMode* Mode)
{
    G={};if(!FParse::Param(FCommandLine::Get(),TEXT("CireOptionsGallery")))return false;
    G.Mode=Mode;G.Start=FPlatformTime::Seconds();
    G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("OptionsGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!IFileManager::Get().MakeDirectory(*G.Directory,true)){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;return true;
}
bool CireOptionsGallery::Tick(ACireGameMode* Mode)
{
    if(G.Mode.Get()!=Mode)return false;if(G.bDone)return true;
    if(FPlatformTime::Seconds()-G.Start>60){Finish(false);return true;}
    if(G.Ready<0)
    {
        auto* PC=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());auto* HUD=PC?Cast<ACireHUD>(PC->GetHUD()):nullptr;
        if(PC&&HUD&&PC->GetPawn()&&!Setup(Mode,PC,HUD))Finish(false);return true;
    }
    const float Now=Mode->GetWorld()->GetTimeSeconds();
    G.Hero->ShieldUntil=Now+25;G.Hero->SlowUntil=Now+9;G.Hero->PoisonAreaCount=1;G.Hero->PoisonEndsAt=Now+8;
    for(int32 I=0;I<G.Allies.Num();++I){G.Allies[I]->ShieldUntil=Now+95-I*11;G.Allies[I]->SlowUntil=I%2?Now+14:0;}
    G.Monster->SlowUntil=Now+5;G.Monster->PoisonAreaCount=1;G.Monster->PoisonEndsAt=Now+7;G.Monster->CastStartedAt=Now-1;G.Monster->CastEndsAt=Now+2;
    const double Age=FPlatformTime::Seconds()-G.Ready;
    const int32 Stage=FMath::Clamp(FMath::FloorToInt((Age-3)/3),0,15);
    const int32 Tabs[]={0,1,1,1,2,3,4,0,5,5,5,5,5,0,5,5},Pages[]={0,0,1,2,0,0,0,0,0,1,2,3,4,0,5,6};
    G.HUD->DebugOptionsPage(Tabs[Stage],Pages[Stage],Stage!=7&&Stage!=13);
    if(Stage==13){G.Hero->bDrafted=false;G.Hero->Offers.Reset();G.HUD->DebugDraftRosterPage(1);}
    if(Stage>=14)G.Hero->bDrafted=true;
    if(Stage==7)
    {
        G.PC->CombatEvents.Reset();
        FCireCombatEvent Event;Event.SourceName=TEXT("Iron Warden");Event.TargetName=TEXT("Grave Caster");Event.SourceId=TEXT("Warden");Event.TargetId=TEXT("Caster");
        Event.AbilityName=TEXT("Shield Slam");Event.Amount=248;Event.bCritical=true;Event.bLocalSource=true;Event.TimeSeconds=Now-.45f;
        Event.ServerTime=Now-.45f;Event.Sequence=1;Event.Location=G.Monster->GetActorLocation()+FVector(0,0,115);G.PC->CombatEvents.Add(Event);
        Event.Amount=62;Event.bCritical=false;Event.TimeSeconds=Now-.9f;Event.Sequence=2;Event.AbilityName=TEXT("Sword strike");G.PC->CombatEvents.Add(Event);
        Event.Amount=0;Event.Outcome=ECireHitOutcome::Miss;Event.TimeSeconds=Now-1.4f;Event.Sequence=3;G.PC->CombatEvents.Add(Event);
        Event.Amount=129;Event.Outcome=ECireHitOutcome::Hit;Event.bHealing=true;Event.bLocalTarget=true;Event.TargetName=TEXT("Iron Warden");
        Event.AbilityName=TEXT("Restoring Light");Event.TimeSeconds=Now-.7f;Event.Sequence=4;Event.Location=G.Hero->GetActorLocation()+FVector(0,0,115);G.PC->CombatEvents.Add(Event);
    }
    if(Age>=5+Stage*3&&Stage>G.Captured)
    {
        int32 W=0,H=0;G.PC->GetViewportSize(W,H);G.bPass &= W>=1280&&H>=720;
        const TCHAR* Names[]={TEXT("controls"),TEXT("combat_text"),TEXT("tooltips_status"),TEXT("chat_layout"),TEXT("video"),TEXT("audio"),TEXT("system"),TEXT("status_critical_hud"),TEXT("dev_match"),TEXT("dev_spawn"),TEXT("dev_effects"),TEXT("balance_lab"),TEXT("replays"),TEXT("champion_roster"),TEXT("dev_quickstart"),TEXT("dev_movement")};
        const FString File=FPaths::Combine(G.Directory,FString::Printf(TEXT("%02d_%s.png"),Stage+1,Names[Stage]));
        FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);G.Files.Add(File);G.Captured=Stage;
    }
    if(Age>53)
    {for(const auto& File:G.Files)G.bPass &= IFileManager::Get().FileSize(*File)>10000;Finish(G.bPass&&G.Files.Num()==16);}
    return true;
}
#endif
