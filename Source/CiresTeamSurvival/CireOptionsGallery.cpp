#include "CireOptionsGallery.h"
#if !UE_BUILD_SHIPPING
#include "CireGame.h"
#include "CireHUD.h"
#include "CireUISettings.h"
#include "CireDeveloperTools.h"
#include "CireChampionRoster.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRealm.h"
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
// ---------------------------------------------------------------------------
// -CireWowUIGallery: deterministic 1080p captures of the WoW-style interface
// (target/focus/boss frames, unit + text tooltips, threat meter and alerts,
// scrolling combat text, level-up burst, 0.7 / 1.15 interface scale, options).
// Uses an isolated profile; never writes the player's CireUI.ini.
// ---------------------------------------------------------------------------
namespace CireWowUIGallery
{
struct FStage { const TCHAR* Name; };
const FStage Stages[]={
    {TEXT("01_hud_target_tank")},{TEXT("02_unit_tooltip_npc")},{TEXT("03_unit_tooltip_boss_cursor_avoid")},{TEXT("04_text_tooltip_avoid_center")},
    {TEXT("05_sct_schools_crits")},{TEXT("06_aggro_alert_dps")},{TEXT("07_threat_warning_dps")},{TEXT("08_lost_aggro_tank")},
    {TEXT("09_level_up")},{TEXT("10_scale_070")},{TEXT("11_scale_115")},{TEXT("12_options_scale_threat")},
    {TEXT("13_options_combat_text")},{TEXT("14_options_tooltips")},{TEXT("15_layout_editor")}};
constexpr int32 StageCount=UE_ARRAY_COUNT(Stages);
struct FState
{
    TWeakObjectPtr<ACireGameMode> Mode;
    TWeakObjectPtr<ACireHUD> HUD;
    TWeakObjectPtr<ACireController> PC;
    TWeakObjectPtr<ACireHero> Hero;
    TArray<TWeakObjectPtr<ACireHero>> Allies;
    TWeakObjectPtr<ACireMonster> Elite,Boss,Bruiser,Hunter,Leader;
    FString Directory;
    TArray<FString> Files,Failures;
    double Start=0,Ready=-1;
    int32 Stage=-1,Captured=-1,Checks=0;
    bool bDone=false,bLevelFired=false,bEditing=false;
} W;
void Check(bool bValue,const FString& Why){++W.Checks;if(!bValue){W.Failures.Add(Why);UE_LOG(LogTemp,Error,TEXT("CIRE_WOWUI_CHECK_FAIL %s"),*Why);}}
void Finish()
{
    if(W.bDone)return;W.bDone=true;
    for(const auto& File:W.Files)Check(IFileManager::Get().FileSize(*File)>10000,TEXT("capture saved: ")+File);
    Check(W.Files.Num()==StageCount,TEXT("all stages captured"));
    const bool bPass=W.Failures.IsEmpty();
    UE_LOG(LogTemp,Display,TEXT("CIRE_WOWUI_GALLERY_%s captures=%d checks=%d directory=%s"),bPass?TEXT("PASS"):TEXT("FAIL"),W.Files.Num(),W.Checks,*W.Directory);
    FPlatformMisc::RequestExitWithStatus(false,bPass?0:1);
}
ACireMonster* Spawn(ACireGameMode* Mode,FVector At,const TCHAR* Archetype,int32 Tier,bool bLaneBoss,float HealthFraction)
{
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* M=Mode->GetWorld()->SpawnActor<ACireMonster>(At,FRotator(0,180,0),Params);
    if(!M)return nullptr;
    M->Lane=W.Hero->TeamId;
    // Real data-driven configuration (role, classification, abilities, visuals).
    Check(CireNPCCombat::ConfigureArchetype(M,FName(Archetype),4,Tier,1,bLaneBoss),FString(TEXT("archetype configured: "))+Archetype);
    M->SetActorTickEnabled(false);M->GetCharacterMovement()->DisableMovement();M->Damage=0;M->Lane=W.Hero->TeamId;
    M->Health=FMath::RoundToFloat(M->MaxHealth*HealthFraction);CireRealm::UpdateVisibility(M);
    return M;
}
bool Setup(ACireGameMode* Mode,ACireController* PC,ACireHUD* HUD)
{
    W.PC=PC;W.HUD=HUD;W.Hero=Cast<ACireHero>(PC->GetPawn());if(!W.Hero.IsValid())return false;
    HUD->UISettings.Load(FPaths::Combine(W.Directory,TEXT("wowui-profile.ini")));
    HUD->UISettings.bShowFPS=false;HUD->UISettings.bShowNetwork=false;HUD->UISettings.TooltipDelay=0;
    auto* H=W.Hero.Get();H->Draft(0);H->Offers.Reset();H->bBot=false;H->bAutoAttack=false;H->HeroName=TEXT("Iron Warden");
    H->Health=1320;H->MaxHealth=1650;H->Mana=300;H->MaxMana=420;H->Level=12;H->Notice.Reset();
    H->Skills={TEXT("shield_slam"),TEXT("war_cry"),TEXT("frost_bind"),TEXT("ember_lance"),TEXT("restoring_light"),TEXT("runic_wall"),TEXT("stone_skin"),TEXT("bastion_of_dawn")};
    H->Cooldowns.Init(0,H->Skills.Num());H->SetActorTickEnabled(false);H->GetCharacterMovement()->DisableMovement();
    PC->bShop=false;PC->bHelp=false;
    const FVector P=H->GetActorLocation();
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const TCHAR* Names[]={TEXT("Ash Ranger"),TEXT("Veil Scholar"),TEXT("Lancer"),TEXT("Rift Summoner")};
    for(int32 I=0;I<4;++I)
    {
        auto* A=Mode->GetWorld()->SpawnActor<ACireHero>(P+FVector(I<2?-60.f:-60.f,I<2?-330.f+I*120.f:210.f+(I-2)*120.f,0),FRotator::ZeroRotator,Params);
        if(!A)return false;A->Draft(I+1);A->Offers.Reset();A->TeamId=H->TeamId;A->HeroName=Names[I];A->bBot=false;
        A->bAutoAttack=false;A->SetActorTickEnabled(false);A->GetCharacterMovement()->DisableMovement();A->MaxHealth=900;A->Health=560+I*70;A->Level=11+I%2;
        W.Allies.Add(A);
    }
    W.Elite=Spawn(Mode,P+FVector(430,-170,0),TEXT("blight_caster"),2,false,.69f);
    W.Boss=Spawn(Mode,P+FVector(900,330,0),TEXT("hollow_siegebreaker"),0,true,.77f);
    W.Bruiser=Spawn(Mode,P+FVector(380,200,0),TEXT("ironbound_bruiser"),0,false,.62f);
    W.Hunter=Spawn(Mode,P+FVector(640,-430,0),TEXT("barbed_hunter"),0,false,.6f);
    W.Leader=Spawn(Mode,P+FVector(700,40,0),TEXT("gravemaw_pack_leader"),2,false,.54f);
    if(!W.Elite.IsValid()||!W.Boss.IsValid()||!W.Bruiser.IsValid()||!W.Hunter.IsValid()||!W.Leader.IsValid())return false;
    W.Elite->PackId=3;W.Bruiser->PackId=3;W.Leader->PackId=3;
    Check(W.Leader->GetNPCClassification()==ECireNPCClass::Boss&&W.Elite->GetNPCClassification()==ECireNPCClass::Elite,TEXT("classification read API"));
    Check(W.Elite->GetNPCRole()==ECireNPCRole::Caster&&W.Hunter->GetNPCRole()==ECireNPCRole::Ranged&&W.Boss->IsLaneBoss(),TEXT("role read API"));
    PC->FocusTarget=W.Boss.Get();H->Target=W.Elite.Get();
    Check(HUD->DebugFontsReady(),TEXT("OFL font faces loaded and runtime fonts built"));
    W.Ready=FPlatformTime::Seconds();return true;
}
void SetThreat(ACireMonster* M,ACireHero* Victim,std::initializer_list<TPair<ACireHero*,float>> Rows)
{
    M->Threat.Reset();M->NPCState->ThreatTable.Reset();
    for(const auto& Row:Rows){M->Threat.Add(Row.Key,Row.Value);FCireThreatEntry E;E.Hero=Row.Key;E.Threat=Row.Value;M->NPCState->ThreatTable.Add(E);}
    M->NPCState->ThreatTable.Sort([](const FCireThreatEntry& A,const FCireThreatEntry& B){return A.Threat>B.Threat;});
    M->Victim=Victim;M->bEngaged=true;
}
void Configure(int32 Stage)
{
    auto* HUD=W.HUD.Get();auto* H=W.Hero.Get();auto* PC=W.PC.Get();auto* E=W.Elite.Get();auto* B=W.Boss.Get();
    auto* Tank=H;auto* Ranger=W.Allies[0].Get();auto* Scholar=W.Allies[1].Get();
    const float Now=W.Mode->GetWorld()->GetTimeSeconds();
    HUD->DebugTooltipClear();HUD->DebugUnitTooltip(nullptr,FVector2D::ZeroVector);HUD->DebugOptionsPage(0,0,false);
    if(W.bEditing){HUD->ToggleLayoutEditor();W.bEditing=false;}
    HUD->UISettings.bAutoUIScale=true;HUD->UISettings.TooltipMode=3;
    H->ProfileThreatRole=TEXT("tank");H->Target=E;PC->CombatEvents.Reset();
    SetThreat(E,Ranger,{{Tank,880.f},{Ranger,1000.f},{Scholar,420.f}});
    SetThreat(B,Tank,{{Tank,5200.f},{Ranger,3900.f},{Scholar,2100.f},{W.Allies[2].Get(),1500.f}});
    SetThreat(W.Bruiser.Get(),Tank,{{Tank,300.f}});W.Hunter->Victim=Scholar;
    SetThreat(W.Leader.Get(),Tank,{{Tank,2600.f},{W.Allies[2].Get(),1900.f}});
    E->CastingAbility=TEXT("npc_caster_mend");E->NPCState->CastAbilityId=TEXT("npc_caster_mend");E->NPCState->bCastInterruptible=true;
    E->CastStartedAt=Now-.55f;E->CastEndsAt=Now+.45f+3.f;E->SlowUntil=Now+6;
    B->CastingAbility=TEXT("boss_siege_cleave");B->NPCState->CastAbilityId=TEXT("boss_siege_cleave");B->NPCState->bCastInterruptible=false;
    B->CastStartedAt=Now-.3f;B->CastEndsAt=Now+3.f;
    W.Leader->NPCState->StatusFlags=CireNPCStatus::Enraged|CireNPCStatus::Rallied;
    switch(Stage)
    {
    case 0: break;
    case 1: HUD->DebugUnitTooltip(E,FVector2D(900,430));break;
    case 2: HUD->UISettings.TooltipMode=0;HUD->DebugUnitTooltip(B,FVector2D(600,330));break;
    case 3: HUD->UISettings.TooltipMode=0;HUD->DebugTooltip(TEXT("Shield Slam"),TEXT("Bash your target with your shield for heavy damage and generate high threat. Interrupts the target's cast. The tooltip slides aside so it never covers the reticle at the centre of the screen."),FVector2D(560,330));break;
    case 4:
    {
        const float Base=Now+2.f; // capture time: ages below are as seen in the screenshot
        FCireCombatEvent Ev;Ev.SourceName=TEXT("Iron Warden");Ev.SourceId=TEXT("Warden");Ev.bLocalSource=true;Ev.Location=E->GetActorLocation()+FVector(0,0,115);
        auto Add=[&](const TCHAR* Ability,const TCHAR* Target,float Amount,bool bCrit,float Age,ECireHitOutcome Out=ECireHitOutcome::Hit,FName TargetId=TEXT("Elite"))
        {Ev.AbilityName=Ability;Ev.TargetName=Target;Ev.TargetId=TargetId;Ev.Amount=Amount;Ev.bCritical=bCrit;Ev.Outcome=Out;Ev.TimeSeconds=Base-Age;Ev.ServerTime=Base-Age;Ev.Sequence=PC->CombatEvents.Num()+1;PC->CombatEvents.Add(Ev);};
        Add(TEXT("Ember Lance"),TEXT("Blight Caster"),412,true,.08f);
        Add(TEXT("Frost Bind"),TEXT("Blight Caster"),168,false,.6f);
        Ev.Location=W.Bruiser->GetActorLocation()+FVector(0,0,115);
        for(int32 I=0;I<4;++I)Add(TEXT("Cataclysm"),TEXT("Bruiser"),96+I*7,false,1.1f,ECireHitOutcome::Hit,FName(*FString::Printf(TEXT("T%d"),I)));
        Add(TEXT("Sword strike"),TEXT("Ironbound Bruiser"),0,false,1.5f,ECireHitOutcome::Miss);
        Ev.bLocalSource=false;Ev.bLocalTarget=true;Ev.SourceName=TEXT("Blight Caster");Ev.SourceId=TEXT("Elite");Ev.TargetName=TEXT("Iron Warden");Ev.Location=H->GetActorLocation()+FVector(0,0,115);
        Add(TEXT("Shadow Bolt"),TEXT("Iron Warden"),95,false,.4f);
        Ev.bHealing=true;Ev.SourceName=TEXT("Veil Scholar");Ev.SourceId=TEXT("Scholar");Add(TEXT("Restoring Light"),TEXT("Iron Warden"),210,true,.9f);
        break;
    }
    case 5: H->ProfileThreatRole=TEXT("damage");SetThreat(E,H,{{H,1100.f},{Ranger,1000.f}});
        HUD->DebugAlert(TEXT("AGGRO!"),TEXT("Blight Caster turned on you. Stop and let your tank take it back."),FLinearColor(.95f,.20f,.16f,1));break;
    case 6: H->ProfileThreatRole=TEXT("damage");SetThreat(E,Ranger,{{H,940.f},{Ranger,1000.f}});
        HUD->DebugAlert(TEXT("THREAT 94%"),TEXT("Ease off Blight Caster or you will pull it from Ash Ranger."),FLinearColor(1.f,.55f,.10f,1));break;
    case 7: SetThreat(E,Scholar,{{H,800.f},{Scholar,1000.f}});
        HUD->DebugAlert(TEXT("LOST AGGRO"),TEXT("Blight Caster is attacking Veil Scholar. Taunt it back!"),FLinearColor(1.f,.55f,.10f,1));break;
    case 8: H->Target=nullptr;W.bLevelFired=false;break;
    case 9: HUD->UISettings.bAutoUIScale=false;HUD->UISettings.UIScale=.7f;break;
    case 10: HUD->UISettings.bAutoUIScale=false;HUD->UISettings.UIScale=1.15f;break;
    case 11: HUD->DebugOptionsPage(1,3,true);break;
    case 12: HUD->DebugOptionsPage(1,0,true);break;
    case 13: HUD->DebugOptionsPage(1,1,true);break;
    case 14: HUD->ToggleLayoutEditor();W.bEditing=true;break;
    default: break;
    }
    W.Stage=Stage;
}
void Capture(int32 Stage)
{
    auto* HUD=W.HUD.Get();
    int32 VW=0,VH=0;W.PC->GetViewportSize(VW,VH);Check(VW==1920&&VH==1080,TEXT("1080p viewport"));
    const FVector2D View=HUD->DebugTooltipViewport();
    const float Expected=Stage==9?1.5f*.7f:Stage==10?1.5f*1.15f:1.5f;
    Check(FMath::IsNearlyEqual(HUD->DebugScale(),Expected,.01f),FString::Printf(TEXT("%s: interface scale %.3f (expected %.3f)"),Stages[Stage].Name,HUD->DebugScale(),Expected));
    if(Stage==9||Stage==10)
    {
        const FCireUIRect Map=HUD->UISettings.GetRect(TEXT("Minimap"),View),Chat=HUD->UISettings.GetRect(TEXT("Chat"),View),Skills=HUD->UISettings.GetRect(TEXT("Skills"),View);
        Check(FMath::IsNearlyEqual(Map.X+Map.W,static_cast<float>(View.X)-20.f,.6f)&&FMath::IsNearlyEqual(Map.W,220.f,.6f),TEXT("minimap stays anchored top-right at its designed size"));
        Check(FMath::IsNearlyEqual(Chat.Y+Chat.H,static_cast<float>(View.Y)-20.f,.6f),TEXT("chat stays anchored bottom-left"));
        Check(FMath::IsNearlyEqual((Skills.X+Skills.W*.5f)/static_cast<float>(View.X),636.f/1280.f,.003f),TEXT("action bar stays centred"));
        const FCireUIRect Chat2=HUD->PanelRectForTest(TEXT("Chat")),Meter2=HUD->PanelRectForTest(TEXT("Meter"));
        Check(Chat2.X+Chat2.W<=Skills.X+.5f&&Meter2.X>=Skills.X+Skills.W-.5f,TEXT("chat and meter give way to the action bar"));
    }
    if(Stage>=1&&Stage<=3)
    {
        const FCireUIRect R=HUD->DebugTooltipRect();
        const FBox2D Box(FVector2D(R.X,R.Y),FVector2D(R.X+R.W,R.Y+R.H));
        const FBox2D Center(FVector2D(View.X*.5f-95,View.Y*.5f-85),FVector2D(View.X*.5f+95,View.Y*.5f+85));
        Check(R.W>0&&R.H>0,FString(Stages[Stage].Name)+TEXT(": tooltip drawn"));
        Check(!Box.Intersect(Center),FString(Stages[Stage].Name)+TEXT(": tooltip keeps clear of the reticle"));
        Check(R.X>=3.5f&&R.Y>=3.5f&&R.X+R.W<=View.X-3.5f&&R.Y+R.H<=View.Y-3.5f,FString(Stages[Stage].Name)+TEXT(": tooltip inside viewport"));
        if(Stage==1)
        {
            const FCireUIRect A=HUD->UISettings.GetRect(TEXT("Tooltip"),View);
            Check(FMath::IsNearlyEqual(R.X+R.W,A.X+A.W,.6f)&&FMath::IsNearlyEqual(R.Y+R.H,A.Y+A.H,.6f),TEXT("WoW anchor: tooltip grows from the anchor's lower-right corner"));
        }
    }
    const FString File=FPaths::Combine(W.Directory,FString(Stages[Stage].Name)+TEXT(".png"));
    FScreenshotRequest::RequestScreenshot(File,false,false,false,FIntRect(),true);W.Files.Add(File);W.Captured=Stage;
}
bool Initialize(ACireGameMode* Mode)
{
    W={};
    W.Mode=Mode;W.Start=FPlatformTime::Seconds();
    W.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("WowUIGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!IFileManager::Get().MakeDirectory(*W.Directory,true)){W.Failures.Add(TEXT("setup"));Finish();return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;return true;
}
bool Tick(ACireGameMode* Mode)
{
    if(W.Mode.Get()!=Mode)return false;if(W.bDone)return true;
    if(FPlatformTime::Seconds()-W.Start>120){W.Failures.Add(TEXT("timeout"));Finish();return true;}
    if(W.Ready<0)
    {
        auto* PC=Cast<ACireController>(Mode->GetWorld()->GetFirstPlayerController());auto* HUD=PC?Cast<ACireHUD>(PC->GetHUD()):nullptr;
        if(PC&&HUD&&PC->GetPawn()&&!Setup(Mode,PC,HUD)){W.Failures.Add(TEXT("spawn"));Finish();}
        return true;
    }
    if(!W.HUD.IsValid()||!W.Hero.IsValid()||!W.Elite.IsValid()){W.Failures.Add(TEXT("lost actors"));Finish();return true;}
    const double Age=FPlatformTime::Seconds()-W.Ready;
    const int32 Stage=FMath::Clamp(FMath::FloorToInt((Age-3)/3),0,StageCount-1);
    if(W.Stage!=Stage)Configure(Stage);
    // Keep the caster's cast bar alive and fire the level-up just before its capture.
    const float Now=Mode->GetWorld()->GetTimeSeconds();
    if(W.Elite->CastEndsAt<Now+.5f){W.Elite->CastStartedAt=Now-.9f;W.Elite->CastEndsAt=Now+1.1f;}
    if(W.Boss->CastEndsAt<Now+.3f){W.Boss->CastStartedAt=Now-.5f;W.Boss->CastEndsAt=Now+.8f;}
    if(Stage==8&&!W.bLevelFired&&Age>=5+Stage*3-.8){W.Hero->Level=13;W.HUD->DebugLevelUp(W.Hero.Get(),true);W.bLevelFired=true;}
    if(Age>=5+Stage*3&&Stage>W.Captured)Capture(Stage);
    if(Age>=5+StageCount*3)Finish();
    return true;
}
}

bool CireOptionsGallery::Initialize(ACireGameMode* Mode)
{
    if(FParse::Param(FCommandLine::Get(),TEXT("CireWowUIGallery")))return CireWowUIGallery::Initialize(Mode);
    G={};if(!FParse::Param(FCommandLine::Get(),TEXT("CireOptionsGallery")))return false;
    G.Mode=Mode;G.Start=FPlatformTime::Seconds();
    G.Directory=FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("OptionsGallery"),FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
    if(!Mode||Mode->GetNetMode()!=NM_Standalone||!IFileManager::Get().MakeDirectory(*G.Directory,true)){Finish(false);return true;}
    Mode->bBotsFilled=true;Mode->BotFillTimer=MAX_flt;Mode->WaveTimer=MAX_flt;return true;
}
bool CireOptionsGallery::Tick(ACireGameMode* Mode)
{
    if(CireWowUIGallery::Tick(Mode))return true;
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
