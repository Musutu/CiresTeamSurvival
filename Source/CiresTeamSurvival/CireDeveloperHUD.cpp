#include "CireHUD.h"
#include "CireGame.h"
#include "CireDeveloperTools.h"
#include "CireBalanceLab.h"
#include "CireReplay.h"
#include "CireWeaponPresentation.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

namespace
{
const FLinearColor Card(.034f,.046f,.055f,.98f),Hover(.075f,.106f,.116f,1),Gold(.77f,.61f,.34f,1),Parchment(.91f,.9f,.83f,1),Muted(.5f,.57f,.59f,1),Teal(.2f,.71f,.59f,1);
}
FCireUIRect ACireHUD::DeveloperLauncherRect()const
{
    const auto R=UISettings.GetRect(TEXT("Minimap"),FVector2D(ViewW,ViewH));
    return {FMath::Clamp(R.X+R.W-220,4.f,ViewW-224),FMath::Clamp(R.Y+R.H+8,4.f,ViewH-38),220,30};
}
void ACireHUD::DrawDeveloperLauncher()
{
    if(!CireDeveloperTools::CanEdit(GetWorld())||bEditLayout||bSettings)return;
    ResetTransform();const auto R=DeveloperLauncherRect();const bool Over=Hit(R.X,R.Y,R.W,R.H);
    Frame(R.X,R.Y,R.W,R.H,Over?Teal:Gold);Label(TEXT("DEVELOPER TOOLS  [")+UISettings.Keybindings.Label(TEXT("ToggleDeveloperTools"))+TEXT("]"),R.X+14,R.Y+8,12,Over?Parchment:Gold);
    Tip(TEXT("Developer tools / F8"),TEXT("Quick test kit, weapons, movement, wave controls, effect tuning, match simulations and replays. Opening this panel does not change match settings."),R.X,R.Y,R.W,R.H);
    if(Over&&Clicked){Clicked=false;ToggleDeveloperTools();PlayUIFeedback();}
}
void ACireHUD::DrawDeveloperPanel(float X,float Y)
{
#if !UE_BUILD_SHIPPING
    auto* Mode=GetWorld()->GetAuthGameMode<ACireGameMode>();if(!Mode||!CireDeveloperTools::CanEdit(GetWorld()))return;
    if(!bDeveloperLoaded){DeveloperDraft=CireDeveloperTools::Get(GetWorld());bDeveloperLoaded=true;}
    auto Button=[&](const FString& Title,float BX,float BY,float W,const FString& Help=FString())
    {
        const bool Over=Hit(BX,BY,W,25);Panel(BX,BY,W,25,Over?Hover:Card);Label(Title,BX+7,BY+5,10,Over?Parchment:Gold);
        Tip(Title,Help.IsEmpty()?Title:Help,BX,BY,W,25);
        if(Over&&Clicked){Clicked=false;PlayUIFeedback();return true;}return false;
    };
    auto Slider=[&](const FString& Title,float& Value,float Min,float Max,float Step,float BX,float BY,const FString& Help)
    {
        Label(Title,BX,BY,10,Parchment);Label(FString::Printf(TEXT("%.2f"),Value),BX+230,BY,10,Gold);
        Panel(BX,BY+21,282,4,Card);const float T=FMath::Clamp((Value-Min)/(Max-Min),0.f,1.f);Panel(BX,BY+21,282*T,4,Gold);Panel(BX+278*T,BY+16,5,14,Parchment);
        Tip(Title,Help,BX,BY,285,34);
        if(Hit(BX,BY+12,285,25)&&PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton)){Value=FMath::Clamp(FMath::RoundToFloat((Min+(Max-Min)*FMath::Clamp((MX-BX)/282,0.f,1.f))/Step)*Step,Min,Max);Clicked=false;}
    };
    auto Integer=[&](const FString& Title,int32& Value,int32 Min,int32 Max,float BX,float BY,const FString& Help)
    {float V=Value;Slider(Title,V,Min,Max,1,BX,BY,Help);Value=FMath::RoundToInt(V);};
    auto Toggle=[&](const FString& Title,bool& Value,float BX,float BY,const FString& Help)
    {if(Button(FString(Value?TEXT("[ON] "):TEXT("[OFF] "))+Title,BX,BY,285,Help))Value=!Value;};
    const TCHAR* Pages[]={TEXT("Quick start"),TEXT("Match"),TEXT("Spawn/stats"),TEXT("Effects"),TEXT("Movement"),TEXT("Balance lab"),TEXT("Replays")};
    const int32 PageIds[]={5,0,1,2,6,3,4};
    for(int32 I=0;I<7;++I){if(DeveloperPage==PageIds[I])Panel(X+I*87-2,Y-3,87,31,Hover);if(Button(Pages[I],X+I*87,Y,83))DeveloperPage=PageIds[I];}
    const float L=X,R=X+310,T=Y+48;
    FString Error;
    if(DeveloperPage<3)
    {
        if(Button(FString(DeveloperDraft.bEnabled?TEXT("OVERRIDES: ENABLED"):TEXT("OVERRIDES: DISABLED")),L,T,285,TEXT("Development standalone only. Enable, edit values, then Apply. Changes affect real gameplay, not a mock simulation.")))DeveloperDraft.bEnabled=!DeveloperDraft.bEnabled;
        if(Button(TEXT("RESTORE ORIGINAL TUNING"),R,T,285,TEXT("Restores the timing, wave count and simulation speed saved before the first Apply. It does not rewind combat, revive units or restore spent resources.")))
        {if(CireDeveloperTools::Restore(Mode,&Error)){DeveloperDraft=CireDeveloperTools::Get(GetWorld());DeveloperMessage=TEXT("Original tuning restored.");}else DeveloperMessage=Error;}
        const float A=T+46;
        if(DeveloperPage==0)
        {
            Slider(TEXT("Prep duration (s)"),DeveloperDraft.PrepSeconds,1,600,1,L,A,TEXT("Live preparation duration. Elapsed time stays elapsed; shortening below elapsed transitions on the next clock tick."));
            Slider(TEXT("Arena duration (s)"),DeveloperDraft.ArenaSeconds,1,900,1,R,A,TEXT("Arena time limit used by the authoritative match clock."));
            Slider(TEXT("Recovery duration (s)"),DeveloperDraft.RecoverySeconds,1,180,1,L,A+47,TEXT("Pause after arena before the next PvE cycle."));
            Slider(TEXT("Between-wave pause (s)"),DeveloperDraft.WaveBreatherSeconds,0,120,1,R,A+47,TEXT("Delay after a cleared wave before spawning the next. Prep still waits for all wave monsters to die or leak."));
            Integer(TEXT("Waves per cycle"),DeveloperDraft.WavesPerCycle,1,10,L,A+94,TEXT("Number of cleared waves before preparation and PvP."));
            Slider(TEXT("Simulation speed"),DeveloperDraft.SimulationSpeed,.1f,3,.1f,R,A+94,TEXT("Unreal world time dilation for this standalone test, affecting gameplay and effects together. Original speed is restored with the tuning snapshot."));
            Toggle(TEXT("Pause auto wave spawns"),DeveloperDraft.bPauseWaveSpawns,L,A+141,TEXT("Stops automatic new-wave spawning while existing units continue fighting."));
            Toggle(TEXT("Freeze phase clock"),DeveloperDraft.bFreezePhaseClock,R,A+141,TEXT("Prevents the match phase timer advancing. Combat and projectiles still run."));
            Integer(TEXT("Team life reset value"),DeveloperDraft.TeamLives,1,1000,L,A+181,TEXT("Used only by the Reset lives button. Applying unrelated tuning never grants lives."));
            if(Button(TEXT("RESET BOTH TEAMS' LIVES"),R,A+188,285)){DeveloperMessage=CireDeveloperTools::SetLives(Mode,DeveloperDraft.TeamLives,&Error)?TEXT("Team lives reset."):Error;}
            if(Button(TEXT("ADVANCE TO NEXT PHASE"),L,A+238,595,TEXT("Dev transition: clears remaining wave creeps without rewards, completes preparation, resolves arena, or ends recovery. Normal phase entry and cleanup still run.")))
                DeveloperMessage=CireDeveloperTools::AdvancePhase(Mode,&Error)?TEXT("Advanced through normal phase orchestration."):Error;
        }
        else if(DeveloperPage==1)
        {
            Integer(TEXT("Wave units (0 = normal)"),DeveloperDraft.WaveUnitsOverride,0,40,L,A,TEXT("Overrides regular unit count per team on future waves; the final-wave boss is additional."));
            Slider(TEXT("Monster health multiplier"),DeveloperDraft.MonsterHealthScale,.1f,10,.1f,R,A,TEXT("Applies to newly configured monsters. Existing health is not silently refilled by a tuning change."));
            Slider(TEXT("Monster damage multiplier"),DeveloperDraft.MonsterDamageScale,.05f,5,.05f,L,A+47,TEXT("Multiplies the fixed damage assigned to newly configured NPCs. Default 1 preserves authored static damage."));
            Slider(TEXT("Spawn origin X (cm)"),DeveloperDraft.SpawnX,-1000,13000,100,R,A+47,TEXT("World X coordinate of the next wave's origin. Future spawn rows clamp within the editable lane bounds."));
            Slider(TEXT("Lane offset Y (cm)"),DeveloperDraft.SpawnOffsetY,-500,500,25,L,A+94,TEXT("Shifts both wave formations within their separate lane without moving the enemy into the friendly realm."));
            Slider(TEXT("Formation half-width"),DeveloperDraft.SpawnSpread,0,600,20,R,A+94,TEXT("Distance of alternating spawn columns from the lane center."));
            Slider(TEXT("Row spacing (cm)"),DeveloperDraft.SpawnSpacing,30,240,10,L,A+141,TEXT("Spacing between rows of two monsters in the spawn formation."));
            Slider(TEXT("Ability cooldown multiplier"),DeveloperDraft.CooldownScale,.1f,5,.1f,R,A+141,TEXT("Applies when an ability starts a new cooldown. It does not rewrite cooldowns already ticking."));
            if(Button(TEXT("SPAWN NEXT WAVE NOW"),L,A+212,285,TEXT("Calls the actual wave spawner during PvE if another wave remains in this cycle."))){if(CireBalanceLab::IsActive(Mode))DeveloperMessage=TEXT("Stop the balance lab before spawning match waves.");else if(CireDeveloperTools::Get(GetWorld()).bEnabled){Mode->SpawnWave();DeveloperMessage=TEXT("Wave spawn requested.");}else DeveloperMessage=TEXT("Enable and apply overrides first.");}
            Wrapped(TEXT("Spawn and health changes affect future units. Use the balance lab for repeated scenarios with a clean combat roster."),R,A+211,283,11,Muted,4);
        }
        else
        {
            Slider(TEXT("Projectile speed multiplier"),DeveloperDraft.ProjectileSpeedScale,.1f,5,.1f,L,A,TEXT("Scales actual authoritative skillshot travel speed; targeting projectiles and fixed-direction skillshots remain distinct."));
            Slider(TEXT("Collision radius multiplier"),DeveloperDraft.ProjectileCollisionScale,.1f,5,.1f,R,A,TEXT("Scales the sphere sweep used by newly spawned skillshots. The modeled head follows the actual collision radius."));
            Slider(TEXT("Effect lifetime multiplier"),DeveloperDraft.EffectDurationScale,.1f,5,.1f,L,A+47,TEXT("Scales new ground areas, projectiles, walls, summons and effect durations within each system's validated maximum."));
            Slider(TEXT("Telegraph duration multiplier"),DeveloperDraft.TelegraphScale,.1f,5,.1f,R,A+47,TEXT("Scales warning time before new areas and skillshots activate. It does not change their harmful boundary."));
            Slider(TEXT("Construct health multiplier"),DeveloperDraft.ConstructHealthScale,.1f,10,.1f,L,A+94,TEXT("Scales the destructible health of newly summoned walls and protection objects."));
            Slider(TEXT("Summon health multiplier"),DeveloperDraft.SummonHealthScale,.1f,10,.1f,R,A+94,TEXT("Scales health of newly summoned combat units."));
            Toggle(TEXT("Override collision policies"),DeveloperDraft.bOverrideCollisionPolicies,L,A+143,TEXT("When enabled, every new skillshot uses these category-specific test policies. Saved authored abilities remain unchanged."));
            const TCHAR* Choices[]={TEXT("Ignore"),TEXT("Stop"),TEXT("Pierce"),TEXT("Reflect")};
            const TCHAR* Names[]={TEXT("World"),TEXT("Player"),TEXT("Monster"),TEXT("Protection"),TEXT("Wall")};
            int32* Values[]={&DeveloperDraft.WorldPolicy,&DeveloperDraft.PlayerPolicy,&DeveloperDraft.MonsterPolicy,&DeveloperDraft.ProtectionPolicy,&DeveloperDraft.WallPolicy};
            for(int32 I=0;I<5;++I)if(Button(FString(Names[I])+TEXT(": ")+Choices[FMath::Clamp(*Values[I],0,3)],L+I*120,A+191,113,TEXT("Cycle Ignore / Stop / Pierce / Reflect for this collision category. Reflection and hit budgets still apply.")))*Values[I]=(*Values[I]+1)%4;
            Wrapped(TEXT("Policies apply to world objects, characters, protection spells and walls. Invalid values fail validation."),L,A+234,594,10,Muted,2);
            if(Button(TEXT("LOAD CHAMPION TEST KIT"),L,A+262,285,TEXT("Replaces your current match skills with this champion's six implemented abilities, passive and ultimate. Development standalone only; requires enabled overrides. Available for Warden, Ranger, Scholar, Lancer and Summoner.")))
            {
                auto* Hero=Cast<ACireHero>(PlayerOwner->GetPawn());
                if(!CireDeveloperTools::Get(GetWorld()).bEnabled)DeveloperMessage=TEXT("Enable and apply overrides first.");
                else if(!Hero||!Hero->bDrafted)DeveloperMessage=TEXT("Select a champion first.");
                else DeveloperMessage=Hero->LoadThematicBuild()?TEXT("Champion test kit loaded for this match."):TEXT("This champion's complete implemented test kit is not ready yet.");
            }
        }
        if(Button(TEXT("APPLY LIVE"),L,Y+386,190)){DeveloperMessage=CireDeveloperTools::Apply(Mode,DeveloperDraft,&Error)?TEXT("Applied. Spawn changes affect future units."):Error;}
        if(Button(TEXT("SAVE PROFILE"),L+202,Y+386,190)){DeveloperMessage=CireDeveloperTools::SaveProfile(GetWorld(),DeveloperDraft,&Error)?TEXT("Profile saved. Activation remains explicit each session."):Error;}
        if(Button(TEXT("LOAD PROFILE"),L+404,Y+386,190)){DeveloperMessage=CireDeveloperTools::LoadProfile(GetWorld(),DeveloperDraft,&Error)?TEXT("Profile loaded for review. Apply to activate."):Error;}
    }
    else if(DeveloperPage==3)
    {
        const bool Active=CireBalanceLab::IsActive(Mode);
        Toggle(TEXT("Player participates"),bLabPlayerMode,L,T,TEXT("Player mode temporarily possesses the fixture tank. AI mode leaves both sides computer controlled. Stop restores the original pawn."));
        Toggle(TEXT("Arena 5v5 scenario"),bLabArena,R,T,TEXT("Arena scenario uses two hero teams. Wave scenario uses your role team against NPC waves."));
        Integer(TEXT("Scenario wave"),LabWave,1,30,L,T+44,TEXT("Select the health progression tier for the wave scenario."));
        Integer(TEXT("Heroes per team"),LabTeamSize,1,5,R,T+44,TEXT("Number of actual hero bots/players in each fixture team."));
        Integer(TEXT("NPC count"),LabEnemyCount,1,20,L,T+90,TEXT("Number of NPC enemies in the wave scenario."));
        Slider(TEXT("Simulation limit (s)"),LabSeconds,5,300,5,R,T+90,TEXT("The lab stops automatically at this time limit and writes its measured result."));
        const TCHAR* Kinds[]={TEXT("Mixed"),TEXT("Basic"),TEXT("Bruiser"),TEXT("Caster"),TEXT("Ranged")};
        if(Button(FString(TEXT("NPC role: "))+Kinds[FMath::Clamp(LabEnemyKind+1,0,4)],L,T+140,285))LabEnemyKind=LabEnemyKind==3?-1:LabEnemyKind+1;
        if(Button(Active?TEXT("STOP / RESTORE MATCH"):TEXT("START LIVE SIMULATION"),R,T+140,285))
        {
            if(Active){CireBalanceLab::Stop(Mode);DeveloperMessage=TEXT("Lab stopped; original match actors restored.");}
            else {const bool OK=bLabArena?CireBalanceLab::StartArena(Mode,bLabPlayerMode,LabTeamSize,LabSeconds):CireBalanceLab::Start(Mode,LabWave,LabEnemyKind,bLabPlayerMode,LabTeamSize,LabEnemyCount,LabSeconds);DeveloperMessage=OK?TEXT("Lab running with real combat systems."):TEXT("Lab could not start; stop the active run first.");}
        }
        const auto S=CireBalanceLab::Snapshot(Mode);
        Label(FString::Printf(TEXT("%s   %.1f / %.1f s"),*S.Scenario,S.ElapsedSeconds,S.LimitSeconds),L,T+190,12,Gold);
        Label(FString::Printf(TEXT("Damage %.0f   DPS %.1f   Healing %.0f   HPS %.1f"),S.Damage,S.DPS,S.Healing,S.HPS),L,T+222,12,Parchment);
        Label(FString::Printf(TEXT("Allies alive %d   Enemies alive %d   Target switches %d"),S.AlliesAlive,S.EnemiesAlive,S.VictimSwitches),L,T+250,11,Parchment);
        Label(FString::Printf(TEXT("Tank target share %.0f%%   Threat lead %.2fx"),S.TankTargetShare*100,S.ThreatLeadRatio),L,T+278,11,Teal);
        Wrapped(S.Result.IsEmpty()?TEXT("Measured scenarios suspend and preserve normal match actors. Reports are written under Saved/BalanceLab."):S.Result+TEXT(" ")+S.ReportPath,L,T+315,590,11,Muted,4);
    }
    else if(DeveloperPage==5)
    {
        Label(TEXT("TEST YOUR CHAMPION"),L,T,15,Gold);
        Wrapped(TEXT("Start here for equipment and a complete skill kit. Other tabs control match timing, monsters, spells, movement, simulations and saved replays."),L,T+33,594,12,Muted,3);
        auto* H=Cast<ACireHero>(PlayerOwner->GetPawn());
        if(Button(TEXT("LOAD COMPLETE CHAMPION KIT"),L,T+101,285,TEXT("Replaces learned skills for this match with a full implemented level-24 kit. Select a core champion first.")))
            DeveloperMessage=H&&H->LoadThematicBuild()?TEXT("Full champion kit loaded. Close this panel to test it."):TEXT("Select a core champion: Warden, Ranger, Scholar, Lancer or Summoner.");
        if(Button(TEXT("CYCLE EQUIPPED WEAPON"),R,T+101,285,TEXT("Preview class-compatible weapon variants locally. Ranger cycles bow and crossbow. Gameplay role and damage remain unchanged.")))
        {if(H)CireWeapons::CyclePreview(*H,DeveloperMessage);}
        if(Button(TEXT("RESTORE CLASS WEAPONS"),R,T+144,285,TEXT("Return to the authored weapon and shield set for this class."))){if(H)CireWeapons::ResetPreview(*H,DeveloperMessage);}
        if(Button(TEXT("REFILL MY HEALTH / MANA / ENERGY"),L,T+144,285,TEXT("Developer refill for this standalone test. Does not revive eliminated heroes.")))
        {if(H&&!H->bDead){H->Health=H->MaxHealth;H->Mana=H->MaxMana;H->Energy=100;DeveloperMessage=TEXT("Resources refilled.");}}
        if(Button(TEXT("OPEN EFFECT TUNING"),L,T+208,285))DeveloperPage=2;
        if(Button(TEXT("OPEN MATCH SIMULATOR"),R,T+208,285))DeveloperPage=3;
        if(Button(TEXT("OPEN MOVEMENT / DODGE TUNING"),L,T+251,285))DeveloperPage=6;
        if(Button(TEXT("OPEN SAVED REPLAYS"),R,T+251,285))DeveloperPage=4;
        Wrapped(FString::Printf(TEXT("Movement: %s jump, %s/%s strafe, %s dodge, %s walk/run. Hold RMB to steer. %s selects yourself. %s closes developer tools."),*UISettings.Keybindings.FullLabel(TEXT("Jump")),*UISettings.Keybindings.Label(TEXT("StrafeLeft")),*UISettings.Keybindings.Label(TEXT("StrafeRight")),*UISettings.Keybindings.Label(TEXT("DodgeRoll")),*UISettings.Keybindings.FullLabel(TEXT("ToggleWalk")),*UISettings.Keybindings.Label(TEXT("TargetSelf")),*UISettings.Keybindings.Label(TEXT("ToggleDeveloperTools"))),L,T+306,594,12,Parchment,3);
    }
    else if(DeveloperPage==6)
    {
        if(!bMovementLoaded){MovementDraft=CireMovement::Tuning();bMovementLoaded=true;}
        Slider(TEXT("Run speed (cm/s)"),MovementDraft.RunSpeed,300,800,10,L,T,TEXT("Normal character run speed. Applies live."));
        Slider(TEXT("Walk speed (cm/s)"),MovementDraft.WalkSpeed,100,520,10,R,T,TEXT("Caps Lock toggles walking. Must not exceed run speed."));
        Slider(TEXT("Jump velocity"),MovementDraft.JumpVelocity,200,650,10,L,T+49,TEXT("Initial upward speed for a jump. Uses normal collision and gravity."));
        Slider(TEXT("Roll speed (cm/s)"),MovementDraft.RollSpeed,300,1400,20,R,T+49,TEXT("Server-authorized roll uses character movement sweeps; walls and units still block it."));
        Slider(TEXT("Roll duration (s)"),MovementDraft.RollDuration,.25f,.9f,.05f,L,T+98,TEXT("Total dodge movement/animation duration. Attacks cannot be cast during the roll."));
        Slider(TEXT("Roll cooldown (s)"),MovementDraft.RollCooldown,1,15,.25f,R,T+98,TEXT("Time between successful rolls. Failed attempts consume no energy."));
        Slider(TEXT("Roll energy cost"),MovementDraft.RollEnergy,5,80,5,L,T+147,TEXT("Energy spent only when the server starts the roll."));
        Slider(TEXT("Invulnerability starts (s)"),MovementDraft.InvulnerableStart,0,.5f,.01f,R,T+147,TEXT("Time from roll start until damage avoidance begins."));
        Slider(TEXT("Invulnerability ends (s)"),MovementDraft.InvulnerableEnd,.05f,.9f,.01f,L,T+196,TEXT("Must fit within roll duration, last at most 0.4 seconds, and follow its start. Avoided hits display DODGE."));
        Wrapped(TEXT("Jump / dodge / walk use your keybindings. Apply changes for this session; Save defaults writes MovementTuning.json. Existing rolls retain their original timing."),R,T+200,282,11,Muted,5);
        if(Button(TEXT("APPLY MOVEMENT"),L,T+282,190))DeveloperMessage=CireMovement::Apply(MovementDraft,Error)?TEXT("Movement tuning applied."):Error;
        if(Button(TEXT("SAVE DEFAULTS"),L+202,T+282,190))DeveloperMessage=CireMovement::Apply(MovementDraft,Error)&&CireMovement::Save(Error)?TEXT("Movement defaults saved."):Error;
        if(Button(TEXT("RELOAD DEFAULTS"),L+404,T+282,190)){const bool OK=CireMovement::Reload(Error);if(OK)MovementDraft=CireMovement::Tuning();DeveloperMessage=OK?TEXT("Movement defaults reloaded."):Error;}
    }
    else
    {
        auto* Replay=CireReplay::Get(GetWorld());if(!Replay){Label(TEXT("Replay subsystem unavailable."),L,T,12,Muted);return;}
        if(!bReplayListLoaded){Replay->RefreshList();bReplayListLoaded=true;}
        if(Button(Replay->IsRecording()?TEXT("STOP RECORDING"):TEXT("RECORD CURRENT MATCH"),L,T,285))
        {if(Replay->IsRecording())Replay->StopRecording();else Replay->StartRecording(TEXT("Cire combat test"));}
        if(Button(Replay->IsBusy()?TEXT("LOADING..."):TEXT("REFRESH SAVED REPLAYS"),R,T,285)&&!Replay->IsBusy())Replay->RefreshList();
        Label(Replay->Status,L,T+39,11,Gold);
        const auto& Entries=Replay->GetEntries();ReplayOffset=FMath::Clamp(ReplayOffset,0,FMath::Max(0,Entries.Num()-5));
        for(int32 I=ReplayOffset;I<FMath::Min(Entries.Num(),ReplayOffset+5);++I)
        {
            const auto& E=Entries[I];const float Row=T+77+(I-ReplayOffset)*45;
            const FString Text=E.Title+FString::Printf(TEXT(" / %.0fs / %.1f MB"),E.DurationSeconds,E.Bytes/1048576.0);
            Label(Text.Left(64),L,Row,11,Parchment);Label(E.RecordedUtc.ToString(TEXT("%Y-%m-%d %H:%M UTC")),L,Row+17,9,Muted);
            if(Button(E.bLive?TEXT("LIVE"):TEXT("PLAY"),R+210,Row,74,TEXT("Play this local Unreal replay. Playback travels to the recording and uses a spectator camera; it is rejected during live multiplayer."))&&!E.bLive&&!Replay->IsBusy())Replay->Play(E.Id);
        }
        if(Entries.IsEmpty())Wrapped(TEXT("No saved local replays. Record a match or a balance simulation, stop recording, then refresh this list."),L,T+94,586,12,Muted,4);
        if(Button(TEXT("PREVIOUS"),L,T+314,135))ReplayOffset=FMath::Max(0,ReplayOffset-5);
        if(Button(TEXT("NEXT"),L+146,T+314,135))ReplayOffset=FMath::Min(FMath::Max(0,Entries.Num()-5),ReplayOffset+5);
        Wrapped(TEXT("Playback controls: Space pause, arrows seek, +/- speed, WASD/QE spectator movement, RMB look, Escape return."),L,T+358,590,11,Muted,3);
    }
    if(!DeveloperMessage.IsEmpty())Wrapped(DeveloperMessage,L,Y+418,595,10,Gold,2);
#endif
}

bool ACireHUD::DrawReplayScreen()
{
    auto* Replay=CireReplay::Get(GetWorld());if(!Replay||!Replay->IsPlaying())return false;
    ResetTransform();const float X=ViewW*.5f-360,Y=ViewH-110;
    Frame(X,Y,720,93,Gold);
    Label(FString::Printf(TEXT("REPLAY   %.1f / %.1fs   %.2fx"),Replay->CurrentSeconds(),Replay->DurationSeconds(),Replay->PlaybackSpeed()),X+14,Y+10,14,Parchment);
    Bar(X+14,Y+34,692,4,Replay->DurationSeconds()>0?Replay->CurrentSeconds()/Replay->DurationSeconds():0,Gold);
    auto Button=[&](const FString& Text,float BX,float W){const bool Over=Hit(BX,Y+49,W,27);Panel(BX,Y+49,W,27,Over?Hover:Card);Label(Text,BX+8,Y+55,11,Gold);if(Clicked&&Over&&!bSettings){Clicked=false;PlayUIFeedback();return true;}return false;};
    if(Button(Replay->IsPaused()?TEXT("RESUME"):TEXT("PAUSE"),X+14,113))Replay->SetPaused(!Replay->IsPaused());
    if(Button(TEXT("-10 SECONDS"),X+138,127))Replay->Seek(FMath::Max(0.f,Replay->CurrentSeconds()-10));
    if(Button(TEXT("+10 SECONDS"),X+276,127))Replay->Seek(FMath::Min(Replay->DurationSeconds(),Replay->CurrentSeconds()+10));
    if(Button(TEXT("SPEED"),X+414,105)){const float S=Replay->PlaybackSpeed();Replay->SetSpeed(S<.75f?1:S<1.5f?2:S<3?4:.5f);}
    if(Button(TEXT("EXIT REPLAY"),X+532,174))Replay->ExitPlayback();
    Label(TEXT("WASD / QE move   RMB look   Space pause   F9 options   Escape exit"),X+14,Y-23,11,Muted);
    Label(Replay->Status,X+14,Y+82,9,Muted);return true;
}
