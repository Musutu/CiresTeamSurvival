// wave-director: native tests for the wave director. Run from CireCombatExpansion::Run
// (-CireCombatExpansionProbe, Tools/RunExpansionChecks.py --only native, Tools/RunNPCChecks.py).
#include "CireWaves.h"
#include "CireGame.h"
#include "CireCombatEvents.h"
#include "CireLanePath.h"
#include "CireLoot.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireThreat.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include <limits>

DEFINE_LOG_CATEGORY_STATIC(LogCireWaveTests, Log, All);

#if !UE_BUILD_SHIPPING
bool CireWaveDirector::RunTests(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bValue, const TCHAR* Why) { ++Checks; if (!bValue) { bPass = false; UE_LOG(LogCireWaveTests, Error, TEXT("CIRE_WAVES_CHECK_FAIL %s"), Why); } };
    auto* State = Mode->GetGameState<ACireGameState>();
    if (!State) return false;

    // ---------------------------------------------------------------- data validation
    {
        FCireWaveConfig D = Defaults(); FString Error;
        Check(Validate(D, &Error, false), TEXT("built-in defaults are valid without clamping"));
        Check(D.Waves.Num() == 15 && D.WavesPerCycle == 5 && D.bCampaignOrder && D.Waves[0].Type == ECireWaveType::Normal && D.Waves[1].Type == ECireWaveType::Normal &&
            D.Waves[2].Type == ECireWaveType::Armored && D.Waves[3].Type == ECireWaveType::ArmoredEscort && D.Waves[4].Type == ECireWaveType::Boss,
            TEXT("default cycle 1 is normal, normal, armored, armored escort, boss"));
        // rules-conformance: the default match plays every wave type, with an Armored Escort and a boss in every cycle.
        {
            TSet<int32> Types; bool bEscortAndBossEachCycle = true;
            for (int32 Cycle = 0; Cycle < D.Cycles; ++Cycle)
            {
                bool bEscort = false, bBoss = false;
                for (int32 Wave = 0; Wave < D.WavesPerCycle; ++Wave)
                {
                    const ECireWaveType Type = ResolveWave(D, Wave, Cycle).Type;
                    Types.Add(static_cast<int32>(Type)); bEscort |= Type == ECireWaveType::ArmoredEscort; bBoss |= Type == ECireWaveType::Boss;
                }
                bEscortAndBossEachCycle &= bEscort && bBoss;
            }
            bool bAllTypes = true;
            for (const ECireWaveType Type : {ECireWaveType::Normal, ECireWaveType::Armored, ECireWaveType::ArmoredEscort, ECireWaveType::Boss, ECireWaveType::CasterPack,
                                             ECireWaveType::MeleePack, ECireWaveType::RangedPack, ECireWaveType::HybridPack})
                bAllTypes &= Types.Contains(static_cast<int32>(Type));
            Check(bAllTypes && bEscortAndBossEachCycle, TEXT("the default match plays Caster/Melee/Ranged/Hybrid packs, and every cycle an Armored Escort and a boss"));
            Check(FMath::IsNearlyEqual(D.SpawnAlongRoute, 0.f), TEXT("waves spawn at the rift"));
            FCireWaveConfig Loop = D; Loop.bCampaignOrder = false;
            Check(ResolveWave(Loop, 0, 1).Label == D.Waves[0].Label && ResolveWave(D, 0, 1).Label == D.Waves[5].Label, TEXT("waveOrder: cycle replays the list, campaign continues it"));
        }
        FCireWaveConfig Round; Check(ParseJson(ToJson(D), Round, Error) && Round == D, TEXT("JSON round trip preserves every field"));
        FCireWaveConfig File; Check(LoadFile(File, &Error) && File == D, TEXT("Content/Data/Waves.json matches the built-in defaults"));
        FCireWaveConfig Out = D;
        Check(!ParseJson(TEXT("{broken"), Out, Error) && Out == D, TEXT("broken JSON is rejected and leaves the target untouched"));
        Check(!ParseJson(TEXT("{\"schemaVersion\":2,\"waves\":[]}"), Out, Error), TEXT("unknown schema rejected"));
        Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"waves\":[]}"), Out, Error), TEXT("empty wave list rejected"));
        Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"waves\":[{\"type\":\"dragons\",\"units\":[{\"archetype\":\"hollow_infantry\"}]}]}"), Out, Error), TEXT("unknown wave type rejected"));
        Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"waves\":[{\"type\":\"custom\",\"units\":[{\"archetype\":\"no_such_mob\"}]}]}"), Out, Error), TEXT("unknown archetype rejected"));
        Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"waves\":[{\"type\":\"custom\",\"units\":[{\"archetype\":\"hollow_infantry\",\"count\":20},{\"archetype\":\"ironbound_bruiser\",\"count\":20}]}]}"), Out, Error),
            TEXT("more than 30 units per lane rejected"));
        Check(!ParseJson(TEXT("{\"schemaVersion\":1,\"failsafe\":{\"action\":\"explode\"},\"waves\":[{\"type\":\"custom\",\"units\":[{\"archetype\":\"hollow_infantry\"}]}]}"), Out, Error),
            TEXT("unknown failsafe action rejected"));
        FCireWaveConfig C = D;
        C.WavesPerCycle = 99; C.BreatherSeconds = -5; C.MaxWaveSeconds = 1; C.StuckSeconds = std::numeric_limits<float>::quiet_NaN();
        C.Waves[0].Units[0].Count = 500; C.Waves[0].Units[0].HealthScale = std::numeric_limits<float>::infinity(); C.Waves[0].SpawnInterval = 60;
        Check(!Validate(C, &Error, false), TEXT("strict validation reports out-of-range values"));
        Check(Validate(C, &Error, true) && C.WavesPerCycle == 10 && C.BreatherSeconds == 0 && C.MaxWaveSeconds == 30 && C.StuckSeconds == 5 &&
            C.Waves[0].Units[0].Count == 20 && C.Waves[0].Units[0].HealthScale == 1 && C.Waves[0].SpawnInterval == 5,
            TEXT("clamping pulls every value into its sane limit"));
        FCireWaveConfig Scaled = D; Scaled.CycleHealthGrowth = .5f; Scaled.CycleExtraUnits = 1;
        const FCireWaveDef Late = ResolveWave(Scaled, 0, 2);
        Check(FMath::IsNearlyEqual(Late.Units[0].HealthScale, D.Waves[WaveIndex(D, 0, 2)].Units[0].HealthScale * 2.f) && Late.Units[0].Count == D.Waves[WaveIndex(D, 0, 2)].Units[0].Count + 2 &&
            ResolveWave(Scaled, 7, 0).Label == D.Waves[7].Label && ResolveWave(Scaled, 1, 3).Label == D.Waves[1].Label, TEXT("cycle scaling and wave wrap-around resolve deterministically"));
        FCireWaveConfig Huge = D; Huge.CycleExtraUnits = 5;
        Check(ResolveWave(Huge, 1, 60).UnitsPerLane() <= 30, TEXT("looping growth never exceeds the per-lane spawn budget"));
    }
    // ---------------------------------------------------------------- templates
    {
        const FCireWaveDef Escort = Template(ECireWaveType::ArmoredEscort);
        int32 Escortees = 0, Attackers = 0, Marchers = 0;
        for (const auto& U : Escort.Units) { if (U.bEscortee) Escortees += U.Count; if (U.bNonAttacking) Marchers += U.Count; else Attackers += U.Count; }
        Check(Escortees == 1 && Marchers == 1 && Attackers == 4 && Escort.Units[0].bEscortee && Escort.Units[0].Archetype == TEXT("hollow_shieldbearer"),
            TEXT("armored escort = 1 non-attacking tank escortee + 4 attackers"));
        int32 Bosses = 0; for (const auto& U : Template(ECireWaveType::Boss).Units) if (U.bBoss) Bosses += U.Count;
        Check(Bosses == 1 && Template(ECireWaveType::Boss).UnitsPerLane() > 1, TEXT("boss wave = regular mobs + exactly one boss"));
        bool bAllMarch = true; for (const auto& U : Template(ECireWaveType::Armored).Units) bAllMarch &= U.bNonAttacking;
        Check(bAllMarch, TEXT("armored wave units never attack"));
        bool bTemplatesValid = true;
        for (int32 T = 0; T < static_cast<int32>(ECireWaveType::Count); ++T)
        { FCireWaveConfig One; One.Waves = {Template(static_cast<ECireWaveType>(T))}; ECireWaveType Parsed; bTemplatesValid &= Validate(One, nullptr, false) && ParseType(TypeName(static_cast<ECireWaveType>(T)), Parsed) && Parsed == static_cast<ECireWaveType>(T); }
        Check(bTemplatesValid, TEXT("every wave type template is valid and its id round-trips"));
    }

    // ---------------------------------------------------------------- runtime fixtures
    const auto SavedClock = Mode->Clock; const auto SavedMonsters = Mode->Monsters; const auto SavedHeroes = Mode->Heroes;
    const int32 SavedSpawned = Mode->CycleWavesSpawned, SavedDone = State->CycleWavesDone, SavedPerCycle = State->WavesPerCycle, SavedWave = State->Wave;
    const int32 SavedLives[2] = {State->EmberLives, State->DuskLives};
    const float SavedBreather = Mode->WaveBreatherSeconds, SavedTimer = Mode->WaveTimer;
    const FString SavedAnnouncement = State->Announcement;
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        for (auto* M : Mode->Monsters) if (IsValid(M) && !SavedMonsters.Contains(M)) Actors.AddUnique(M);
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = SavedClock; Mode->Monsters = SavedMonsters; Mode->Heroes = SavedHeroes; Mode->CycleWavesSpawned = SavedSpawned;
        State->CycleWavesDone = SavedDone; State->Wave = SavedWave; State->EmberLives = SavedLives[0]; State->DuskLives = SavedLives[1]; State->Announcement = SavedAnnouncement;
        Initialize(Mode);
        State->WavesPerCycle = SavedPerCycle; Mode->WaveBreatherSeconds = SavedBreather; Mode->WaveTimer = SavedTimer;
    };
    Mode->Clock = Cires::MatchClock(); Mode->Monsters.Reset(); Mode->Heroes.Reset(); Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
    auto Collect = [&]() { for (auto* M : Mode->Monsters) if (IsValid(M)) { Actors.AddUnique(M); M->SetActorTickEnabled(false); } };
    auto SpawnAll = [&]() { for (int32 I = 0; I < 64 && HasPendingSpawns(Mode); ++I) TickSurvival(Mode, 1.f); Collect(); };
    auto Lane0 = [&](int32 Serial) { TArray<ACireMonster*> Out; for (auto* M : Mode->Monsters) if (IsValid(M) && M->Lane == 0 && M->PackId < 0 && M->Health > 0) Out.Add(M); return Out; };
    auto KillWaves = [&]() { for (auto* M : Mode->Monsters) if (IsValid(M) && M->PackId < 0) M->Health = 0; Mode->Monsters.RemoveAll([](ACireMonster* M) { return !IsValid(M) || M->Health <= 0; }); TickSurvival(Mode, 0.f); };
    auto Unit = [](const TCHAR* Id, int32 Count) { FCireWaveUnit U; U.Archetype = Id; U.Count = Count; return U; };

    // ---------------------------------------------------------------- live edits apply next wave
    {
        FCireWaveConfig C = Defaults(); C.WavesPerCycle = 2; C.BreatherSeconds = 3;
        FCireWaveDef A; A.Label = TEXT("Test A"); A.Type = ECireWaveType::Custom; A.SpawnInterval = 0; A.Units = {Unit(TEXT("hollow_infantry"), 2)};
        FCireWaveDef B = A; B.Label = TEXT("Test B"); B.Units = {Unit(TEXT("barbed_hunter"), 3)};
        C.Waves = {A, B};
        FString Error;
        Check(ApplyLive(Mode, C, &Error) && State->WavesPerCycle == 2 && Mode->WaveBreatherSeconds == 3, TEXT("apply live sets waves per cycle and breather on the server"));
        Check(StartWave(Mode), TEXT("first wave starts"));
        SpawnAll();
        TArray<ACireMonster*> First = Lane0(0);
        Check(First.Num() == 2 && First[0]->NPCState && First[0]->NPCState->ArchetypeId == TEXT("hollow_infantry"), TEXT("wave 1 spawns its authored composition per lane"));
        Check(State->WaveLabel.Contains(TEXT("Test A")) && State->NextWaveLabel.Contains(TEXT("Test B")), TEXT("replicated labels show the current and next wave"));
        const float HealthBefore = First.Num() ? First[0]->MaxHealth : 0;
        FCireWaveConfig Edited = C; Edited.Waves[1].Units = {Unit(TEXT("ironbound_bruiser"), 1)}; Edited.Waves[0].Units[0].HealthScale = 5;
        Check(ApplyLive(Mode, Edited, &Error), TEXT("mid-wave edit accepted"));
        Check(First.Num() && IsValid(First[0]) && First[0]->MaxHealth == HealthBefore, TEXT("live edits never rewrite units already on the field"));
        Check(State->NextWaveLabel.Contains(TEXT("Test B")), TEXT("next-wave label refreshes after an edit"));
        for (auto* M : First) { M->Health = 0; Mode->Monsters.Remove(M); }
        Check(StartWave(Mode), TEXT("second wave starts"));
        SpawnAll();
        TArray<ACireMonster*> Second = Lane0(0);
        Check(Second.Num() == 1 && Second[0]->NPCState && Second[0]->NPCState->ArchetypeId == TEXT("ironbound_bruiser"), TEXT("the edit takes effect from the next wave"));
        Check(!StartWave(Mode), TEXT("no wave beyond waves-per-cycle"));
        for (auto* M : Second) { M->Health = 0; Mode->Monsters.Remove(M); }
        Check(SkipTo(Mode, 2, true, &Error) && Lane0(0).Num() == 0 && HasPendingSpawns(Mode), TEXT("skip to wave N queues that wave now"));
        SpawnAll();
        for (auto* M : Lane0(0)) { M->Health = 0; Mode->Monsters.Remove(M); }
        Check(SpawnNow(Mode, Template(ECireWaveType::RangedPack), &Error), TEXT("spawn-this-wave-now queues a test wave"));
        SpawnAll();
        Check(Lane0(0).Num() == Template(ECireWaveType::RangedPack).UnitsPerLane(), TEXT("test wave spawns its full composition"));
        KillWaves();
    }
    // ---------------------------------------------------------------- escort composition at runtime
    {
        FCireWaveConfig C = Defaults(); C.WavesPerCycle = 1; C.Waves = {Template(ECireWaveType::ArmoredEscort)};
        C.Waves[0].SpawnInterval = 0;
        Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
        Check(ApplyLive(Mode, C) && StartWave(Mode), TEXT("escort wave starts"));
        SpawnAll();
        ACireMonster* Escortee = nullptr; int32 Guards = 0, Marchers = 0, Total = 0;
        for (auto* M : Lane0(0)) { ++Total; if (M->bArmoredEscort) { ++Marchers; Escortee = M; } }
        for (auto* M : Lane0(0)) if (!M->bArmoredEscort && EscortCharge(M) == Escortee && Escortee) ++Guards;
        Check(Total == 5 && Marchers == 1 && Guards == 4, TEXT("runtime escort wave: 1 non-attacking escortee defended by 4 attackers"));
        Check(Escortee && Escortee->Damage > 0 && Escortee->LeakCostOverride == 5, TEXT("escortee keeps positive damage (no NPC pause) and its leak cost"));
        // pacing + economy hooks: waves appear SpawnAlongRoute down the road; unit flags survive to death.
        {
            const FCireWaveUnitInfo EI = UnitFlags(Escortee);
            ACireMonster* Guard = nullptr; for (auto* M : Lane0(0)) if (!M->bArmoredEscort) { Guard = M; break; }
            const FCireWaveUnitInfo GI = UnitFlags(Guard);
            Check(EI.bValid && EI.bArmored && EI.bEscortee && !EI.bBoss && EI.WaveNumber == State->Wave && EI.WaveInCycle == 1 && EI.Type == ECireWaveType::ArmoredEscort &&
                GI.bValid && !GI.bArmored && !GI.bBoss && CurrentWaveIndex(Mode) == State->Wave, TEXT("UnitFlags reports wave number, type and armored/escortee flags"));
            const float Progress = Guard ? CireLanePath::RouteProgress(Mode->GetWorld(), 0, Guard->GetActorLocation()) : -1.f;
            Check(FMath::Abs(Progress - C.SpawnAlongRoute) < .06f, TEXT("waves spawn SpawnAlongRoute of the way down the road"));
            const float Cruise = FMath::Max(C.MarchSpeedMultiplier, C.RallySpeed); // world-scale: no defender near yet
            const float Marcher = FMath::Max(C.MarchSpeedMultiplier, C.MarcherSpeed);
            Check(Cruise >= C.MarchSpeedMultiplier && Guard && FMath::IsNearlyEqual(MarchSpeed(Guard), Marcher) && FMath::IsNearlyEqual(MarchSpeed(Escortee), Marcher),
                TEXT("escorts march faster while not fighting (the escortee and its guards keep the marcher pace)"));
        }
        FActorSpawnParameters HeroParams; HeroParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero = Mode->GetWorld()->SpawnActor<ACireHero>(Escortee ? Escortee->GetActorLocation() + FVector(-150, 0, 0) : FVector::ZeroVector, FRotator::ZeroRotator, HeroParams);
        if (Hero) { Actors.Add(Hero); Hero->SetActorTickEnabled(false); Hero->TeamId = 0; Hero->Draft(2); Hero->CriticalChance = 0; Mode->Heroes.Add(Hero); }
        if (Escortee && Hero)
        {
            ACireMonster* Near = nullptr; for (auto* M : Lane0(0)) if (!M->bArmoredEscort) { Near = M; break; }
            if (Near) Hero->SetActorLocation(Near->GetActorLocation() + FVector(-150, 0, 0));
            Check(Near && FMath::IsNearlyEqual(MarchSpeed(Near), FMath::Max(C.MarchSpeedMultiplier, C.MarcherSpeed)) &&
                FMath::IsNearlyEqual(MarchSpeed(Escortee), FMath::Max(C.MarchSpeedMultiplier, C.MarcherSpeed)),
                TEXT("world-scale: the escort keeps its formation pace near a defender"));
            Hero->SetActorLocation(Escortee->GetActorLocation() + FVector(-150, 0, 0));
        }
        if (Escortee && Hero)
        {
            for (auto* M : Lane0(0)) if (M != Escortee) M->SetActorLocation(Escortee->GetActorLocation() + FVector(0, 200, 0));
            CireCombat::ApplyDamage(Hero, Escortee, 10, TEXT("Escort probe"));
            int32 Defending = 0;
            for (auto* M : Lane0(0)) if (M != Escortee && M->Threat.Contains(Hero)) ++Defending;
            Check(Defending == 4 && Escortee->Threat.IsEmpty(), TEXT("guards turn on the escortee's attacker; the escortee never retaliates"));
            // rules-conformance: the escort wave's rewardMultiplier (x1.25) scales XP only; gold is exactly the bounty ruling (armored x2).
            Hero->Gold = 0;
            const int32 Bounty = CireLoot::KillBounty(Mode, Escortee);
            Check(FMath::IsNearlyEqual(RewardMultiplier(Escortee), 1.25f) && Bounty == 2 * CireLoot::MobValueNow(Mode->GetWorld()), TEXT("escortee bounty is 2x the mob value"));
            Mode->MonsterKilled(Escortee, Hero);
            Check(Hero->Gold == Bounty, *FString::Printf(TEXT("the wave reward multiplier never stacks on kill gold (%d of %d)"), Hero->Gold, Bounty));
            Escortee->Health = 0; Escortee->Destroy();
        }
        KillWaves();
        Mode->Heroes.Reset();
    }
    // ---------------------------------------------------------------- pacing: clock, boss flags, breather ready
    {
        FCireWaveConfig C = Defaults(); C.WavesPerCycle = 2; C.PrepSeconds = 33; C.ArenaSeconds = 44; C.RecoverySeconds = 7; C.BreatherSeconds = 16;
        C.Waves = {Template(ECireWaveType::Boss)}; C.Waves[0].SpawnInterval = 0;
        Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
        Check(ApplyLive(Mode, C) && Mode->Clock.GetDurations().Intermission == 33 && Mode->Clock.GetDurations().Arena == 44 &&
            Mode->Clock.GetDurations().Recovery == 7 && Mode->RecoverySeconds == 7 && Mode->WaveBreatherSeconds == 16, TEXT("pacing block drives the phase clock and breather"));
        Check(StartWave(Mode), TEXT("boss wave starts"));
        SpawnAll();
        ACireMonster* Boss = nullptr; for (auto* M : Lane0(0)) if (M->bBoss) Boss = M;
        const FCireWaveUnitInfo BI = UnitFlags(Boss);
        Check(Boss && BI.bValid && BI.bBoss && !BI.bArmored && BI.Type == ECireWaveType::Boss && FMath::IsNearlyEqual(MarchSpeed(Boss), FMath::Max(C.MarchSpeedMultiplier, C.RallySpeed)), TEXT("boss flag reported; the boss marches at the pacing speed too"));
        if (Boss)
        {
            // world-scale: with a defender within rallyRadius the column drops from the rally pace to the normal march.
            FActorSpawnParameters NearParams; NearParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (auto* Defender = Mode->GetWorld()->SpawnActor<ACireHero>(Boss->GetActorLocation() + FVector(-300, 0, 0), FRotator::ZeroRotator, NearParams))
            {
                Defender->SetActorTickEnabled(false); Defender->TeamId = 0; Defender->Draft(2); Mode->Heroes.Add(Defender);
                Check(FMath::IsNearlyEqual(MarchSpeed(Boss), C.MarchSpeedMultiplier), TEXT("world-scale: a marching column slows to the march pace near a defender"));
                Mode->Heroes.Remove(Defender); Defender->Destroy();
            }
        }
        KillWaves();
        State->CycleWavesDone = Mode->CycleWavesSpawned; // what the match tick does on a clear
        Check(IsBreather(Mode), TEXT("a cleared mid-cycle wave opens the breather"));
        FActorSpawnParameters HeroParams; HeroParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Human = Mode->GetWorld()->SpawnActor<ACireHero>(FVector(0, -2100, 3200), FRotator::ZeroRotator, HeroParams);
        auto* Bot = Mode->GetWorld()->SpawnActor<ACireHero>(FVector(0, -1900, 3200), FRotator::ZeroRotator, HeroParams);
        if (Human && Bot)
        {
            Actors.Add(Human); Actors.Add(Bot);
            for (auto* H : {Human, Bot}) { H->SetActorTickEnabled(false); H->TeamId = 0; H->Draft(1); Mode->Heroes.Add(H); }
            Human->bBot = false; Bot->bBot = true;
            Check(!UpdateBreatherReady(Mode) && State->BreatherPlayers == 1 && State->BreatherReady == 0, TEXT("breather waits for the human player"));
            Check(!SetPlayerReady(Bot, true), TEXT("bots cannot press Ready"));
            Check(SetPlayerReady(Human, true) && UpdateBreatherReady(Mode) && State->BreatherReady == 1, TEXT("every human ready ends the breather early"));
            SetPlayerReady(Human, false);
            Check(!UpdateBreatherReady(Mode), TEXT("un-ready restores the full breather"));
            Human->bBot = true;
            Check(!UpdateBreatherReady(Mode) && State->BreatherPlayers == 0, TEXT("bots-only matches keep the full breather"));
            FCireWaveConfig Off = C; Off.bEarlyContinue = false; ApplyLive(Mode, Off); Human->bBot = false; SetPlayerReady(Human, true);
            Check(!UpdateBreatherReady(Mode), TEXT("ready-up can be switched off"));
        }
        if (Human) Human->Destroy();
        if (Bot) Bot->Destroy();
        Mode->Heroes.Reset();
    }
    // ---------------------------------------------------------------- stuck nudge and stall failsafe
    {
        FCireWaveConfig C = Defaults(); C.WavesPerCycle = 1; C.MaxWaveSeconds = 30; C.FailsafeGraceSeconds = 5; C.StuckSeconds = 2;
        FCireWaveDef W; W.Label = TEXT("Stall probe"); W.Type = ECireWaveType::Custom; W.SpawnInterval = 0; W.Units = {Unit(TEXT("hollow_infantry"), 1)};
        C.Waves = {W};
        Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
        Check(ApplyLive(Mode, C) && StartWave(Mode), TEXT("stall probe wave starts"));
        SpawnAll();
        ACireMonster* M = Lane0(0).Num() ? Lane0(0)[0] : nullptr;
        Check(M && IsWaveActive(Mode) && BlocksNextWave(Mode), TEXT("a live must-clear unit blocks the next wave"));
        if (M)
        {
            const FVector Start = M->GetActorLocation();
            const float Progress = CireLanePath::RouteProgress(Mode->GetWorld(), 0, Start);
            for (int32 I = 0; I < 4; ++I) { DebugAge(Mode, 1.1f); TickSurvival(Mode, .01f); }
            Check(CireLanePath::RouteProgress(Mode->GetWorld(), 0, M->GetActorLocation()) > Progress && CireLanePath::Contains(Mode->GetWorld(), 0, M->GetActorLocation()),
                TEXT("a unit making no progress is nudged forward along its route"));
            M->SetActorLocation(FVector(20000, -9000, 110), false, nullptr, ETeleportType::TeleportPhysics);
            TickSurvival(Mode, .01f);
            Check(CireLanePath::Contains(Mode->GetWorld(), 0, M->GetActorLocation()), TEXT("a unit outside its realm is returned to its route"));
            DebugAge(Mode, 31.f); TickSurvival(Mode, .01f);
            Check(IsForcedMarch(M) && AggroSuppressed(M) && M->Threat.IsEmpty(), TEXT("after the stall limit leftovers stop fighting and march"));
            DebugAge(Mode, 6.f); TickSurvival(Mode, .01f);
            Check(!IsValid(M) || M->IsActorBeingDestroyed(), TEXT("after the grace period leftovers despawn"));
            Check(!IsWaveActive(Mode) && !BlocksNextWave(Mode) && !Mode->Monsters.Contains(M), TEXT("the stall failsafe always releases the cycle"));
        }
    }
    // ---------------------------------------------------------------- rules-conformance: threat is never dropped
    // (Eric: threat is lost only when a unit dies or an ability says so; no leash, no distance drop, no stuck drop;
    // the stall failsafe only moves units that hold no threat).
    {
        FCireWaveConfig C = Defaults(); C.WavesPerCycle = 1; C.MaxWaveSeconds = 30; C.FailsafeGraceSeconds = 5; C.StuckSeconds = 2;
        FCireWaveDef W; W.Label = TEXT("Threat probe"); W.Type = ECireWaveType::Custom; W.SpawnInterval = 0; W.Units = {Unit(TEXT("hollow_infantry"), 1)};
        C.Waves = {W};
        Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
        Check(ApplyLive(Mode, C) && StartWave(Mode), TEXT("threat probe wave starts"));
        SpawnAll();
        ACireMonster* M = Lane0(0).Num() ? Lane0(0)[0] : nullptr;
        FActorSpawnParameters HeroParams; HeroParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero = M ? Mode->GetWorld()->SpawnActor<ACireHero>(M->GetActorLocation() + FVector(-200, 0, 0), FRotator::ZeroRotator, HeroParams) : nullptr;
        if (M && Hero)
        {
            Actors.Add(Hero); Hero->SetActorTickEnabled(false); Hero->TeamId = 0; Hero->Draft(2); Mode->Heroes.Add(Hero);
            CireThreat::AddRaw(M, Hero, 50.f);
            Check(M->Victim == Hero, TEXT("threat probe: the unit targets the hero"));
            Hero->SetActorLocation(M->GetActorLocation() + FVector(-6000, 2500, 0), false, nullptr, ETeleportType::TeleportPhysics);
            for (int32 I = 0; I < 5; ++I) { DebugAge(Mode, 1.1f); TickSurvival(Mode, .01f); }
            Check(M->Victim == Hero && M->Threat.Contains(Hero) && !AggroSuppressed(M), TEXT("no lane leash or stuck drop: a victim 65 m away keeps its threat"));
            DebugAge(Mode, 31.f); TickSurvival(Mode, .01f);
            Check(!IsForcedMarch(M) && M->Victim == Hero && IsValid(M) && !M->IsActorBeingDestroyed(), TEXT("a unit holding threat keeps fighting past the stall limit"));
            Hero->bDead = true; CireThreat::Select(M);
            Check(M->Threat.IsEmpty() && M->Victim == nullptr, TEXT("threat is lost when the threat holder dies"));
            TickSurvival(Mode, .01f);
            Check(IsForcedMarch(M), TEXT("the stall failsafe marches only units that hold no threat"));
            Hero->bDead = false;
            Hero->SetActorLocation(M->GetActorLocation() + FVector(-200, 0, 0), false, nullptr, ETeleportType::TeleportPhysics);
            CireThreat::Damage(M, Hero, 20.f);
            TickSurvival(Mode, .01f);
            Check(!IsForcedMarch(M) && M->Threat.Contains(Hero), TEXT("an attacked marcher keeps the new threat and turns to fight"));
            // Explicit ability hooks are the only other way to lose threat.
            const float Held = M->Threat.FindRef(Hero);
            CireThreat::ScaleAll(Hero, .5f);
            Check(Held > 0 && FMath::IsNearlyEqual(M->Threat.FindRef(Hero), Held * .5f, .01f), TEXT("an ability can reduce threat by a percent (ScaleAll)"));
            CireThreat::ScaleAll(Hero, 0.f);
            Check(!M->Threat.Contains(Hero) && M->Victim != Hero, TEXT("an ability can drop threat entirely (ScaleAll 0)"));
        }
        KillWaves();
        Mode->Heroes.Reset();
    }
    // ---------------------------------------------------------------- neutral challenge packs and bots
    {
        Mode->Monsters.Reset(); Mode->Heroes.Reset();
        const FVector Ground(500, -2100, 3000);
        auto* Floor = Mode->GetWorld()->SpawnActor<AActor>();
        if (Floor)
        {
            Actors.Add(Floor); auto* Box = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
            Box->SetBoxExtent(FVector(2500, 900, 50)); Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent(); Floor->SetActorLocation(Ground - FVector(0, 0, 50));
        }
        FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto MakePack = [&](FVector Offset)
        {
            auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 88), FRotator::ZeroRotator, Params);
            if (!M) return M;
            Actors.Add(M); M->SetActorTickEnabled(false); M->Lane = 0; M->PackId = 4242;
            CireNPCCombat::ConfigureArchetype(M, TEXT("hollow_shieldbearer"), 1, 1, 1);
            M->SpawnPosition = M->GetActorLocation(); Mode->Monsters.Add(M); return M;
        };
        auto MakeHero = [&](FVector Offset, bool bBot, int32 Kind)
        {
            auto* H = Mode->GetWorld()->SpawnActor<ACireHero>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, Params);
            if (!H) return H;
            Actors.Add(H); H->SetActorTickEnabled(false); H->TeamId = 0; H->Draft(Kind); H->bBot = bBot; H->CriticalChance = 0;
            H->Health = H->MaxHealth = 5000; Mode->Heroes.Add(H); return H;
        };
        ACireMonster* A = MakePack(FVector(0, 0, 0)); ACireMonster* B = MakePack(FVector(0, 160, 0));
        ACireHero* Player = MakeHero(FVector(-150, 0, 0), false, 2); ACireHero* Bot = MakeHero(FVector(-150, 150, 0), true, 0);
        if (!Floor || !A || !B || !Player || !Bot) Check(false, TEXT("neutral fixture actors spawn"));
        else
        {
            TickSurvival(Mode, .01f);
            Check(A->bNeutral && B->bNeutral, TEXT("challenge packs start neutral"));
            CireNPCCombat::Tick(A, .01f);
            Check(!A->Victim && A->Threat.IsEmpty() && !A->bEngaged, TEXT("a neutral pack never attacks first, even with a player in melee range"));
            Check(ChooseBotTarget(Bot) == nullptr, TEXT("bots ignore neutral packs"));
            const float Before = A->Health;
            CireCombat::ApplyDamage(Bot, A, 50, TEXT("Bot probe"));
            Check(A->Health == Before && A->bNeutral && B->bNeutral, TEXT("a bot's hit on a neutral pack is rejected and does not provoke it"));
            CireCombat::ApplyDamage(Player, A, 50, TEXT("Player probe"));
            Check(A->Health < Before && !A->bNeutral && !B->bNeutral && B->Threat.Contains(Player) && A->Threat.Contains(Player),
                TEXT("a player's attack aggroes the whole pack together"));
            CireThreat::Select(A); CireThreat::Select(B);
            Check(ChooseBotTarget(Bot) == A || ChooseBotTarget(Bot) == B, TEXT("with no wave threat, bots help a teammate the provoked pack is fighting"));
            auto* Wave = Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + FVector(1200, 0, 88), FRotator::ZeroRotator, Params);
            if (Wave)
            {
                Actors.Add(Wave); Wave->SetActorTickEnabled(false); Wave->Lane = 0; CireNPCCombat::ConfigureArchetype(Wave, TEXT("hollow_infantry"), 1);
                Mode->Monsters.Add(Wave);
                Check(ChooseBotTarget(Bot) == Wave, TEXT("with a pack in range and a wave spawning, bots choose the wave"));
                Wave->Health = 0; Mode->Monsters.Remove(Wave);
            }
            Player->bDead = true; Bot->bDead = true; CireThreat::Remove(Player); CireThreat::Remove(Bot);
            CireNPCCombat::Tick(A, .01f);
            Check(A->LeashTimer > 0 && !A->bNeutral, TEXT("a pack with no living threat holder heads home"));
            A->SetActorLocation(A->SpawnPosition); CireNPCCombat::Tick(A, .01f);
            Check(A->bNeutral && A->Health == A->MaxHealth, TEXT("a reset pack returns to neutral at full health"));
            Player->bDead = false; Bot->bDead = false;
            // Low-health bots fall back instead of dying in place.
            B->Victim = Bot; Bot->Health = Bot->MaxHealth * .2f;
            Check(ShouldBotRetreat(Bot), TEXT("a threatened bot under 28% health retreats"));
            Player->SetActorLocation(Ground + FVector(1500, 0, 92));
            FVector Fallback; Check(BotDestination(Bot, Fallback) && Fallback.Equals(Player->GetActorLocation(), 1.f), TEXT("a retreating bot regroups on a living healer"));
            Bot->Health = Bot->MaxHealth; DebugAge(Mode, 10.f);
            Check(!ShouldBotRetreat(Bot), TEXT("a healed bot rejoins the defence"));
        }
    }
    UE_LOG(LogCireWaveTests, Display, TEXT("CIRE_WAVES_TESTS_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks);
    return bPass;
}
#endif
