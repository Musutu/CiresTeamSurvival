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
#include "CireCrowdControl.h" // waves-modes: armored stun / slow
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
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
        Check(D.Waves.Num() == 25 && D.Cycles == 5 && D.WavesPerCycle == 5 && D.bCampaignOrder && D.Waves[0].Type == ECireWaveType::Normal && D.Waves[1].Type == ECireWaveType::Normal &&
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
        for (auto& X : Scaled.Waves) X.Packs = 0; // legacy rows: extra units per cycle apply
        const FCireWaveDef Late = ResolveWave(Scaled, 0, 2);
        Check(FMath::IsNearlyEqual(Late.Units[0].HealthScale, D.Waves[WaveIndex(D, 0, 2)].Units[0].HealthScale * 2.f) && Late.Units[0].Count == D.Waves[WaveIndex(D, 0, 2)].Units[0].Count + 2 &&
            ResolveWave(Scaled, 7, 0).Label == D.Waves[7].Label && ResolveWave(Scaled, 1, 3).Label == D.Waves[16].Label, TEXT("cycle scaling and campaign order resolve deterministically"));
        FCireWaveConfig Huge = D; Huge.CycleExtraUnits = 5; for (auto& X : Huge.Waves) X.Packs = 0;
        Check(ResolveWave(Huge, 1, 60).UnitsPerLane() <= 30, TEXT("looping growth never exceeds the per-lane spawn budget"));
        // ------------------------------------------------------------ waves-modes: packs, schedule, Sudden Death
        auto PackStats = [](const FCireWaveDef& W, int32& Packs, int32& MinSize, int32& MaxSize, int32& Total)
        {
            TMap<int32, int32> Size; Total = 0;
            for (const auto& U : W.Units) if (U.Pack > 0) { Size.FindOrAdd(U.Pack) += U.Count; Total += U.Count; }
            Packs = Size.Num(); MinSize = 99; MaxSize = 0;
            for (const auto& P : Size) { MinSize = FMath::Min(MinSize, P.Value); MaxSize = FMath::Max(MaxSize, P.Value); }
        };
        {
            bool bEarly = true, bLate = true; int32 P, Lo, Hi, Total;
            for (int32 G = 1; G <= 25; ++G)
            {
                PackStats(ResolveWave(D, (G - 1) % 5, (G - 1) / 5), P, Lo, Hi, Total);
                if (G <= 5) bEarly &= P == 5 && Lo == 5 && Hi == 5 && Total == 25;
                else bLate &= P == 7 && Lo >= 5 && Hi <= 7 && Total >= 35 && Total <= 49;
            }
            Check(bEarly, TEXT("waves 1-5 are 5 packs of 5 (25 monsters per lane)"));
            Check(bLate, TEXT("waves 6-25 are 7 packs of 5-7 (35-49 monsters per lane)"));
            FCireWaveConfig Bigger = D; Bigger.PackSizeBonus = 1;
            PackStats(ResolveWave(Bigger, 0, 0), P, Lo, Hi, Total);
            Check(P == 5 && Lo == 6 && Hi == 6 && Total == 30, TEXT("the pack-size modifier grows every pack"));
            const FCireWaveDef Boss = ResolveWave(D, 4, 0), Escort = ResolveWave(D, 3, 0);
            int32 Bosses = 0; for (const auto& U : Boss.Units) Bosses += U.bBoss ? U.Count : 0;
            Check(Bosses == 1 && Boss.Units.Last().bBoss && Escort.Units[0].bEscortee && Escort.Units[0].Count == 1, TEXT("pack waves keep their boss (last) and escortee (first) as authored"));
            bool bArmoredPacks = true; for (const auto& U : ResolveWave(D, 2, 0).Units) bArmoredPacks &= U.bNonAttacking && U.Pack > 0;
            Check(bArmoredPacks, TEXT("armored waves come in packs of armored marchers"));
            const FCireWaveDef Again = ResolveWave(D, 1, 2);
            Check(Again == ResolveWave(D, 1, 2), TEXT("pack sizes are deterministic per wave"));
        }
        {
            Check(D.Match.TotalWaves == 25 && D.Match.PvpAfterWaves == TArray<int32>({5, 10, 15, 20}) && FMath::IsNearlyEqual(D.Match.SuddenDeathHealth, 2.f) &&
                FMath::IsNearlyEqual(D.Match.SuddenDeathDamage, 2.f), TEXT("match schedule: 25 waves, PvP after 5/10/15/20, Sudden Death x2"));
            Check(GlobalWaveOf(D, 0, 5) == 26 && !IsSuddenDeath(D, 25) && IsSuddenDeath(D, 26), TEXT("waves after 25 are Sudden Death"));
            FCireWaveConfig Plain = D; for (auto& X : Plain.Waves) X.Packs = 0;
            const FCireWaveDef W21 = ResolveWave(Plain, 0, 4), SD = ResolveWave(Plain, 0, 5), SD6 = ResolveWave(Plain, 0, 6);
            const float Ratio = 2.f * (1.f + Plain.CycleHealthGrowth * 5) / (1.f + Plain.CycleHealthGrowth * 4);
            Check(SD.Label.StartsWith(TEXT("Sudden Death")) && SD.Label.Contains(D.Waves[20].Label) && SD6.Label.Contains(D.Waves[20].Label) &&
                FMath::IsNearlyEqual(SD.Units[0].HealthScale, W21.Units[0].HealthScale * Ratio, .01f), TEXT("Sudden Death replays waves 21-25 with health x2"));
            const float DRatio = 2.f * (1.f + Plain.CycleDamageGrowth * 5) / (1.f + Plain.CycleDamageGrowth * 4);
            Check(FMath::IsNearlyEqual(SD.Units[0].DamageScale, W21.Units[0].DamageScale * DRatio, .01f), TEXT("Sudden Death doubles damage"));
            FCireWaveConfig Bad = D; Bad.Match.PvpAfterWaves = {20, 5, 5, -1, 10}; Validate(Bad, nullptr, true);
            Check(Bad.Match.PvpAfterWaves == TArray<int32>({5, 10, 20}), TEXT("the PvP schedule is sorted, unique and positive"));
            Check(RollWaveType(D, D.Waves[0], 1, 7) == D.Waves[0], TEXT("the default wave-type roll keeps the planned wave (bonus-loot hook)"));
        }
        {
            // Presets: shipped game types, JSON round trip, apply / capture.
            const TArray<FCireWavePreset> Built = BuiltInPresets();
            Check(Built.Num() == 3 && Built[0].Id == TEXT("standard") && Built[1].Id == TEXT("hero_td") && Built[2].Id == TEXT("hybrid"), TEXT("Standard, Hero TD / PvP and Hybrid ship as presets"));
            TArray<FCireWavePreset> Round2; FString PErr;
            Check(ParsePresets(PresetsToJson(Built), Round2, PErr) && Round2 == Built, TEXT("presets round-trip through WavePresets.json"));
            TArray<FCireWavePreset> FromFile; FString Json;
            Check(FFileHelper::LoadFileToString(Json, *PresetsPath()) && ParsePresets(Json, FromFile, PErr) && FromFile.Num() >= 3, TEXT("Content/Data/WavePresets.json parses"));
            FCireWaveConfig Td = D; ApplyPreset(Td, Built[1]);
            bool bNone = true; for (const auto& X : Td.Waves) bNone &= !X.bDealsDamage && X.FightBackPacks.IsEmpty();
            Check(bNone && Td.Preset == TEXT("hero_td"), TEXT("Hero TD: no wave attacks"));
            FCireWaveConfig Hy = D; ApplyPreset(Hy, Built[2]);
            Check(Hy.Waves[4].bDealsDamage && !Hy.Waves[5].bDealsDamage && Hy.Waves[5].FightBackPacks == TArray<int32>({1, 4, 7}), TEXT("Hybrid: boss waves fight, other waves only packs 1, 4, 7"));
            FCireWaveConfig Std = Hy; ApplyPreset(Std, Built[0]);
            bool bAll = true; for (const auto& X : Std.Waves) bAll &= X.bDealsDamage;
            Check(bAll, TEXT("Standard: every wave attacks (armored units never do)"));
            FCireWaveConfig Mixed = D; Mixed.Waves[3].bDealsDamage = false; Mixed.Waves[3].FightBackPacks = {2}; Mixed.Live.Health = 1.5f; Mixed.PackSizeBonus = -1;
            const FCireWavePreset Cap = CapturePreset(Mixed, TEXT("My Mode!"), TEXT("My mode"));
            FCireWaveConfig Back = D; ApplyPreset(Back, Cap);
            Check(Cap.Id == TEXT("my_mode_") && Back.Waves == Mixed.Waves && Back.Live == Mixed.Live && Back.PackSizeBonus == -1, TEXT("a captured preset re-applies the same per-wave damage, scale and pack modifier"));
            const FString Temp = FPaths::ProjectSavedDir() / TEXT("WavePresetsTest.json");
            IFileManager::Get().Delete(*Temp);
            TArray<FCireWavePreset> Saved;
            Check(SavePreset(Cap, &PErr, Temp) && FFileHelper::LoadFileToString(Json, *Temp) && ParsePresets(Json, Saved, PErr) && Saved.Num() == 1 && Saved[0] == Cap,
                TEXT("SavePreset writes a preset file that loads back"));
            FCireWavePreset Cap2 = Cap; Cap2.Label = TEXT("Renamed");
            Check(SavePreset(Cap2, &PErr, Temp) && FFileHelper::LoadFileToString(Json, *Temp) && ParsePresets(Json, Saved, PErr) && Saved.Num() == 1 && Saved[0].Label == TEXT("Renamed"),
                TEXT("saving the same id replaces the preset"));
            IFileManager::Get().Delete(*Temp);
        }
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
    const FName SavedPreset = State->WavePreset; // waves-modes
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        for (auto* M : Mode->Monsters) if (IsValid(M) && !SavedMonsters.Contains(M)) Actors.AddUnique(M);
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = SavedClock; Mode->Monsters = SavedMonsters; Mode->Heroes = SavedHeroes; Mode->CycleWavesSpawned = SavedSpawned;
        State->CycleWavesDone = SavedDone; State->Wave = SavedWave; State->EmberLives = SavedLives[0]; State->DuskLives = SavedLives[1]; State->Announcement = SavedAnnouncement;
        State->WavePreset = SavedPreset;
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
            Check(Cruise >= C.MarchSpeedMultiplier && Guard && FMath::IsNearlyEqual(MarchSpeed(Guard), Marcher * SpeedFactor(Guard)) && FMath::IsNearlyEqual(MarchSpeed(Escortee), Marcher * SpeedFactor(Escortee)),
                TEXT("escorts march faster while not fighting (the escortee and its guards keep the marcher pace)"));
        }
        FActorSpawnParameters HeroParams; HeroParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero = Mode->GetWorld()->SpawnActor<ACireHero>(Escortee ? Escortee->GetActorLocation() + FVector(-150, 0, 0) : FVector::ZeroVector, FRotator::ZeroRotator, HeroParams);
        if (Hero) { Actors.Add(Hero); Hero->SetActorTickEnabled(false); Hero->TeamId = 0; Hero->Draft(2); Hero->CriticalChance = 0; Mode->Heroes.Add(Hero); }
        if (Escortee && Hero)
        {
            ACireMonster* Near = nullptr; for (auto* M : Lane0(0)) if (!M->bArmoredEscort) { Near = M; break; }
            if (Near) Hero->SetActorLocation(Near->GetActorLocation() + FVector(-150, 0, 0));
            Check(Near && FMath::IsNearlyEqual(MarchSpeed(Near), FMath::Max(C.MarchSpeedMultiplier, C.MarcherSpeed) * SpeedFactor(Near)) &&
                FMath::IsNearlyEqual(MarchSpeed(Escortee), FMath::Max(C.MarchSpeedMultiplier, C.MarcherSpeed) * SpeedFactor(Escortee)),
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
        Check(Boss && BI.bValid && BI.bBoss && !BI.bArmored && BI.Type == ECireWaveType::Boss && FMath::IsNearlyEqual(MarchSpeed(Boss), FMath::Max(C.MarchSpeedMultiplier, C.RallySpeed) * SpeedFactor(Boss)), TEXT("boss flag reported; the boss marches at the pacing speed too"));
        if (Boss)
        {
            // world-scale: with a defender within rallyRadius the column drops from the rally pace to the normal march.
            FActorSpawnParameters NearParams; NearParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            if (auto* Defender = Mode->GetWorld()->SpawnActor<ACireHero>(Boss->GetActorLocation() + FVector(-300, 0, 0), FRotator::ZeroRotator, NearParams))
            {
                Defender->SetActorTickEnabled(false); Defender->TeamId = 0; Defender->Draft(2); Mode->Heroes.Add(Defender);
                Check(FMath::IsNearlyEqual(MarchSpeed(Boss), C.MarchSpeedMultiplier * SpeedFactor(Boss)), TEXT("world-scale: a marching column slows to the march pace near a defender"));
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
    // ---------------------------------------------------------------- waves-modes: packs, armored traits, damage toggle, live scale, schedule
    {
        const bool SavedSmoke = Mode->bSmoke; Mode->bSmoke = false; // live pack expansion (smoke runs keep authored rows)
        ON_SCOPE_EXIT { Mode->bSmoke = SavedSmoke; };
        FCireWaveConfig C = Defaults(); C.WavesPerCycle = 3;
        FCireWaveDef P; P.Label = TEXT("Pack probe"); P.Type = ECireWaveType::Custom; P.SpawnInterval = 0; P.Units = {Unit(TEXT("hollow_infantry"), 1)};
        P.Packs = 3; P.PackSizeMin = P.PackSizeMax = 2;
        FCireWaveDef A = Template(ECireWaveType::Armored); A.SpawnInterval = 0;
        FCireWaveDef Off = P; Off.Label = TEXT("Damage-off probe"); Off.Packs = 2; Off.PackSizeMin = Off.PackSizeMax = 1; Off.bDealsDamage = false; Off.FightBackPacks = {2};
        C.Waves = {P, A, Off};
        Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
        FString Error;
        Check(ApplyLive(Mode, C, &Error) && StartWave(Mode), *(TEXT("pack probe wave starts: ") + Error));
        SpawnAll();
        TArray<ACireMonster*> Units = Lane0(0);
        TMap<int32, TSet<int32>> Paths; int32 Normal = 0;
        for (auto* M : Units) { Paths.FindOrAdd(UnitFlags(M).Pack).Add(M->LanePath); Normal += FMath::IsNearlyEqual(SpeedFactor(M), C.Monsters.Speed) && !IsSlowImmune(M); }
        bool bOnePath = Paths.Num() == 3; for (const auto& Pair : Paths) bOnePath &= Pair.Key > 0 && Pair.Value.Num() == 1;
        Check(Units.Num() == 6 && bOnePath, *FString::Printf(TEXT("3 packs of 2 spawn, each pack on one path (%d units, %d packs)"), Units.Num(), Paths.Num()));
        Check(Normal == Units.Num() && FMath::IsNearlyEqual(C.Monsters.Speed, .8f), TEXT("every wave monster moves at -20% and can be slowed"));
        // Live scale: living units rescale at once, later spawns carry it.
        ACireMonster* U0 = Units.Num() ? Units[0] : nullptr;
        const float Hp = U0 ? U0->MaxHealth : 0, Dmg = U0 ? U0->Damage : 0;
        FCireWaveScale Scale; Scale.Health = 2; Scale.Damage = .5f; Scale.Speed = 1.5f;
        Check(SetLiveScale(Mode, Scale, &Error) && U0 && FMath::IsNearlyEqual(U0->MaxHealth, Hp * 2, 1.f) && FMath::IsNearlyEqual(U0->Damage, Dmg * .5f, 1.f) &&
            FMath::IsNearlyEqual(SpeedFactor(U0), C.Monsters.Speed * 1.5f), TEXT("the live wave scale reaches the monsters already on the road"));
        Check(SetLiveScale(Mode, FCireWaveScale()) && U0 && FMath::IsNearlyEqual(U0->MaxHealth, Hp, 1.f), TEXT("the live scale returns to 1"));
        KillWaves();
        // Armored: -50% on top of -20%, slow immune, stunned twice as long.
        Check(StartWave(Mode), TEXT("armored probe wave starts"));
        SpawnAll();
        ACireMonster* Arm = Lane0(0).Num() ? Lane0(0)[0] : nullptr;
        Check(Arm && UnitFlags(Arm).bArmored && FMath::IsNearlyEqual(SpeedFactor(Arm), C.Monsters.Speed * C.Monsters.ArmoredSpeed) && FMath::IsNearlyEqual(SpeedFactor(Arm), .4f),
            TEXT("armored marchers move at 0.8 x 0.5 of their pace"));
        if (Arm)
        {
            Arm->SlowUntil = 0;
            Check(IsSlowImmune(Arm) && CireCrowdControl::Slow(Arm, 3.f, nullptr) == 0.f && Arm->SlowUntil <= 0.f, TEXT("armored marchers cannot be slowed"));
            Check(FMath::IsNearlyEqual(StunMultiplier(Arm), 2.f) && FMath::IsNearlyEqual(CireCrowdControl::Stun(Arm, 1.f, nullptr), 2.f, .01f), TEXT("armored marchers stay stunned twice as long"));
            Check(FMath::IsNearlyEqual(StunMultiplier(U0), 1.f), TEXT("other units keep the normal stun"));
        }
        KillWaves();
        // Damage off: pack 1 is passive (marches, never aggroes), pack 2 fights back (Hybrid).
        Check(StartWave(Mode), TEXT("damage-off probe wave starts"));
        SpawnAll();
        ACireMonster* Passive = nullptr; ACireMonster* Fighter = nullptr;
        for (auto* M : Lane0(0)) (UnitFlags(M).Pack == 1 ? Passive : Fighter) = M;
        Check(Passive && Fighter && IsPassive(Passive) && UnitFlags(Passive).bPassive && AggroSuppressed(Passive) && !UnitFlags(Passive).bArmored &&
            !IsPassive(Fighter) && !AggroSuppressed(Fighter), TEXT("damage off: pack 1 only marches, fight-back pack 2 still fights"));
        Check(Passive && !IsSlowImmune(Passive) && FMath::IsNearlyEqual(SpeedFactor(Passive), C.Monsters.Speed), TEXT("passive units are not armored: normal pace, slowable"));
        KillWaves();
        // Schedule (arena-flow interface) on the live config.
        UWorld* World = Mode->GetWorld();
        Check(IsPvpAfterWave(World, 5) && !IsPvpAfterWave(World, 6) && PvpRoundAfterWave(World, 20) == 4 && NextPvpWave(World, 6) == 10 && NextPvpWave(World, 21) == 0 &&
            Schedule(World).TotalWaves == 25, TEXT("the PvP schedule answers after which waves the arena runs"));
        // Game type: host picks before the first wave; the preset reaches the live config and the replicated state.
        const int32 WaveNow = State->Wave; State->Wave = 0;
        bool bOff = true;
        Check(SelectPreset(Mode, TEXT("hero_td"), &Error) && State->WavePreset == TEXT("hero_td"), *(TEXT("the host selects the Hero TD game type: ") + Error));
        for (const auto& X : Config(World).Waves) bOff &= !X.bDealsDamage;
        Check(bOff && Config(World).Preset == TEXT("hero_td"), TEXT("Hero TD turns every wave's damage off"));
        State->Wave = 1;
        Check(!SelectPreset(Mode, TEXT("standard"), &Error), TEXT("the game type is locked once the first wave starts"));
        Check(!SelectPreset(Mode, TEXT("no_such_mode"), &Error), TEXT("unknown game types are rejected"));
        State->Wave = WaveNow; State->WavePreset = SavedPreset;
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
