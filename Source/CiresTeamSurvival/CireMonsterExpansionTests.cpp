// monster-expansion: native checks for the Bestiary creatures, race variants, Rare Spawns and the Bonus Loot Wave.
// Run by CireNPCCombat::RunSmoke (-CireCombatExpansionProbe; Tools/RunExpansionChecks.py, Tools/RunNPCChecks.py).
#include "CireMonsterExpansion.h"
#include "CireAudio.h"
#include "CireGame.h"
#include "CireLoot.h"
#include "CireMonsterArt.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireThreat.h"
#include "CireWaves.h"
#include "Rules/CireItemRules.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireExpansionTests, Log, All);

bool CireMonsterExpansion::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bValue, const FString& Why) { ++Checks; if (!bValue) { bPass = false; UE_LOG(LogCireExpansionTests, Error, TEXT("CIRE_EXPANSION_CHECK_FAIL %s"), *Why); } };

    // ------------------------------------------------------------ data: ten creatures, fallbacks, sounds, variants
    static const FName Ids[] = {TEXT("treasure_goblin"), TEXT("gilded_stag"), TEXT("rotting_shambler"), TEXT("bone_archer"), TEXT("centaur_blademaster"),
        TEXT("horned_brute"), TEXT("lich_revenant"), TEXT("storm_griffon"), TEXT("cinder_drake"), TEXT("frostfang_alpha")};
    Check(Creatures().Num() == UE_ARRAY_COUNT(Ids), FString::Printf(TEXT("ten bestiary creatures load (%d)"), Creatures().Num()));
    int32 FabBodies = 0;
    for (const FName Id : Ids)
    {
        const FCireNPCArchetype* A = CireNPCArchetypes::Find(Id);
        const FCreature* C = Find(Id);
        Check(A && C, Id.ToString() + TEXT(" is a merged archetype"));
        if (!A || !C) continue;
        Check(!A->FallbackBody.IsNone() && CireNPCArchetypes::Find(A->FallbackBody), Id.ToString() + TEXT(" names a valid fallback body"));
        const CireMonsterArt::FArchetypeArt* Art = CireMonsterArt::Find(Id);
        Check(Art && !Art->Bodies.IsEmpty(), Id.ToString() + TEXT(" draws a body (Fab art or its fallback)"));
        if (Art && !Art->Bodies.IsEmpty() && Art->Bodies[0].bFab)
        {
            ++FabBodies;
            for (const TCHAR* Role : {TEXT("idle"), TEXT("walk"), TEXT("run"), TEXT("attack"), TEXT("hit"), TEXT("death")})
                Check(Art->Bodies[0].Roles.Contains(Role), Id.ToString() + TEXT(" Fab body has its ") + Role + TEXT(" clip"));
        }
        for (const TCHAR* Role : {TEXT("attack"), TEXT("hit"), TEXT("death")})
            Check(C->Sounds.Contains(Role) && CireAudio::HasCue(C->Sounds[Role]), Id.ToString() + TEXT(" has a ") + Role + TEXT(" cue"));
        for (const FCireNPCAbility& Ab : A->Abilities) if (!Ab.bBasic && !Ab.Cue.IsNone()) Check(CireAudio::HasCue(Ab.Cue), Ab.Id.ToString() + TEXT(" cue exists"));
    }
    const bool bFab = !FParse::Param(FCommandLine::Get(), TEXT("CireNoFab")) && !FParse::Param(FCommandLine::Get(), TEXT("CireNoFabCreatures")) &&
        FPackageName::DoesPackageExist(TEXT("/Game/UndeadPack/EnemyGoblin/Mesh/SM_EnemyGoblin"));
    if (bFab) Check(FabBodies == UE_ARRAY_COUNT(Ids), FString::Printf(TEXT("with the creature packs installed every creature draws its Fab body (%d)"), FabBodies));
    for (const TCHAR* Cue : {TEXT("sting.rare"), TEXT("sting.bonus_wave"), TEXT("bonus.escape"), TEXT("bonus.caught")}) Check(CireAudio::HasCue(Cue), FString(TEXT("cue ")) + Cue);
    Check(!SpecialColor(1).Equals(SpecialColor(2)) && !SpecialColor(1).Equals(CireRaces::Rank(ECireNPCRank::Elite).Color), TEXT("rare and bonus colours are distinct from the rank colours"));
    {
        TMap<FName, int32> Counters;
        const FName A1 = VariantFor(TEXT("hollow_infantry"), Counters), A2 = VariantFor(TEXT("hollow_infantry"), Counters), A3 = VariantFor(TEXT("hollow_infantry"), Counters);
        Check(A1 == TEXT("hollow_infantry") && A2 == TEXT("hollow_infantry") && A3 == TEXT("rotting_shambler"), TEXT("every third hollow line unit is a rotting shambler"));
        Check(VariantFor(TEXT("barbed_hunter"), Counters) == TEXT("barbed_hunter") && VariantFor(TEXT("barbed_hunter"), Counters) == TEXT("bone_archer"), TEXT("every second hollow archer is a bone archer"));
        Check(VariantFor(TEXT("tidecaller"), Counters) == TEXT("tidecaller"), TEXT("races without variants are unchanged"));
    }

    // ------------------------------------------------------------ rules: deterministic rare roll, eligibility, caps
    const FCireWaveConfig D = CireWaveDirector::Defaults();
    Check(D.Rare.bEnabled && D.Rare.Pool.Num() == 5 && D.Bonus.bEnabled && D.Bonus.Wave.Type == ECireWaveType::BonusLoot && !D.Bonus.Wave.bMustClear,
        TEXT("defaults: five rare creatures, an optional bonus loot wave"));
    {
        FCireWaveDef W = CireWaveDirector::Template(ECireWaveType::Normal);
        int32 Hits = 0; TSet<FName> Drawn;
        for (int32 Wave = 2; Wave < 202; ++Wave)
        {
            FCireWaveDef A = W, B = W;
            const bool bA = CireWaveDirector::RollRare(D, A, Wave, 77, 0), bB = CireWaveDirector::RollRare(D, B, Wave, 77, 0);
            Check(bA == bB && A.Units.Num() == B.Units.Num(), TEXT("the rare roll is deterministic per seed and wave"));
            if (bA) { ++Hits; Drawn.Add(A.Units.Last().Archetype); Check(A.Units.Last().bRare && A.Units.Last().Count == 1, TEXT("a rare joins as one rare unit")); }
        }
        Check(Hits > 30 && Hits < 90, FString::Printf(TEXT("about 30%% of eligible waves roll a rare (%d / 200)"), Hits));
        Check(Drawn.Num() >= 4, TEXT("the rare pool is drawn broadly"));
        FCireWaveDef One = W;
        Check(!CireWaveDirector::RollRare(D, One, 1, 77, 0, false) || D.Rare.FromWave <= 1, TEXT("no rares before rareSpawn.fromWave"));
        FCireWaveDef Armored = CireWaveDirector::Template(ECireWaveType::Armored), Boss = CireWaveDirector::Template(ECireWaveType::Boss);
        FCireWaveConfig Always = D; Always.Rare.Chance = 1.f;
        Check(!CireWaveDirector::RollRare(Always, Armored, 5, 1, 0) && !CireWaveDirector::RollRare(Always, Boss, 5, 1, 0), TEXT("armored, escort and boss waves never roll a rare"));
        FCireWaveDef Capped = W;
        Check(!CireWaveDirector::RollRare(Always, Capped, 5, 1, Always.Rare.MaxPerCycle), TEXT("rares are capped per cycle"));
    }

    // ------------------------------------------------------------ runtime fixtures
    auto* State = Mode->GetGameState<ACireGameState>();
    if (!State) return false;
    const auto SavedClock = Mode->Clock; const auto SavedMonsters = Mode->Monsters; const auto SavedHeroes = Mode->Heroes;
    const int32 SavedWave = State->Wave, SavedSpawned = Mode->CycleWavesSpawned, SavedDone = State->CycleWavesDone, SavedPerCycle = State->WavesPerCycle;
    const int32 SavedLives[2] = {State->EmberLives, State->DuskLives};
    const float SavedBreather = Mode->WaveBreatherSeconds, SavedTimer = Mode->WaveTimer;
    const FString SavedAnnouncement = State->Announcement;
    const bool SavedSmoke = Mode->bSmoke;
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        for (auto* M : Mode->Monsters) if (IsValid(M) && !SavedMonsters.Contains(M)) Actors.AddUnique(M);
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = SavedClock; Mode->Monsters = SavedMonsters; Mode->Heroes = SavedHeroes; Mode->CycleWavesSpawned = SavedSpawned; Mode->bSmoke = SavedSmoke;
        State->Wave = SavedWave; State->CycleWavesDone = SavedDone; State->EmberLives = SavedLives[0]; State->DuskLives = SavedLives[1]; State->Announcement = SavedAnnouncement;
        CireWaveDirector::Initialize(Mode);
        State->WavesPerCycle = SavedPerCycle; Mode->WaveBreatherSeconds = SavedBreather; Mode->WaveTimer = SavedTimer;
    };
    Mode->Clock = Cires::MatchClock(); Mode->Monsters.Reset(); Mode->Heroes.Reset(); Mode->bSmoke = false;
    UWorld* World = Mode->GetWorld();
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto SpawnAll = [&]() { for (int32 I = 0; I < 64 && CireWaveDirector::HasPendingSpawns(Mode); ++I) CireWaveDirector::TickSurvival(Mode, 1.f);
        for (auto* M : Mode->Monsters) if (IsValid(M)) { Actors.AddUnique(M); M->SetActorTickEnabled(false); } };
    auto Lane0 = [&](uint8 Special) { TArray<ACireMonster*> Out; for (auto* M : Mode->Monsters) if (IsValid(M) && M->Lane == 0 && M->Health > 0 && M->SpecialSpawn == Special) Out.Add(M); return Out; };
    ACireHero* Hero = World->SpawnActor<ACireHero>(FVector(0, -2100, 3200), FRotator::ZeroRotator, Params);
    Check(Hero != nullptr, TEXT("hero fixture spawns"));
    if (!Hero) return false;
    Actors.Add(Hero); Hero->SetActorTickEnabled(false); Hero->TeamId = 0; Hero->bBot = false; Hero->Draft(0); Hero->Health = Hero->MaxHealth = 100000; Mode->Heroes.Add(Hero);
    auto KillAll = [&]() { for (auto* M : Mode->Monsters) if (IsValid(M)) { M->Health = 0; Actors.AddUnique(M); } Mode->Monsters.Reset(); CireWaveDirector::TickSurvival(Mode, 0.f); };
    auto Drops = [&]() { int32 N = 0; for (TActorIterator<ACireLootDrop> It(World); It; ++It) if (It->OwnerHero == Hero && !It->IsActorBeingDestroyed()) { ++N; Actors.AddUnique(*It); } return N; };
    FString Error;

    // ------------------------------------------------------------ Rare Spawn in a live wave (+ race variants)
    {
        FCireWaveConfig C = D; C.WavesPerCycle = 1; C.Rare.Chance = 1.f; C.Rare.FromWave = 1;
        FCireWaveDef W = CireWaveDirector::Template(ECireWaveType::Normal); W.SpawnInterval = 0; W.Race = TEXT("hollow");
        C.Waves = {W};
        State->Wave = 4; Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
        Check(CireWaveDirector::ApplyLive(Mode, C, &Error) && CireWaveDirector::StartWave(Mode, true), TEXT("live wave with a forced rare starts: ") + Error);
        SpawnAll();
        const TArray<ACireMonster*> Rares = Lane0(1);
        Check(Rares.Num() == 1, FString::Printf(TEXT("exactly one rare joins the wave per lane (%d)"), Rares.Num()));
        int32 Shamblers = 0, Plain = 0;
        for (auto* M : Lane0(0)) { Shamblers += M->NPCState && M->NPCState->ArchetypeId == TEXT("rotting_shambler"); Plain += M->NPCState && M->NPCState->ArchetypeId == TEXT("hollow_infantry"); }
        Check(Shamblers == 1 && Plain == W.Units[0].Count - 1, FString::Printf(TEXT("live hollow waves field a rotting shambler as every third line unit (%d / %d)"), Shamblers, Plain));
        if (Rares.Num() == 1)
        {
            ACireMonster* R = Rares[0];
            Check(R->GetNPCDisplayName().StartsWith(TEXT("Rare ")) && D.Rare.Pool.Contains(R->NPCState->ArchetypeId), TEXT("the rare wears a Rare name plate and comes from the pool"));
            Check(CireRaces::RankOf(R) >= ECireNPCRank::Elite && CireRaces::RankColor(R).Equals(SpecialColor(1)), TEXT("the rare is at least elite and wears the rare colour"));
            const FCireWaveUnitInfo Info = CireWaveDirector::UnitFlags(R);
            Check(Info.bValid && Info.bRare && !Info.bBonus, TEXT("economy hook flags the rare"));
            ACireMonster* Baseline = World->SpawnActor<ACireMonster>(FVector(0, 2100, 3200), FRotator::ZeroRotator, Params);
            if (Baseline) { Actors.Add(Baseline); Baseline->SetActorTickEnabled(false); CireNPCCombat::ConfigureArchetype(Baseline, R->NPCState->ArchetypeId, State->Wave, 0, 1); }
            Check(Baseline && R->MaxHealth > Baseline->MaxHealth * 2.f && R->Damage > Baseline->Damage, TEXT("the rare is much tougher than its plain archetype"));
            const int32 Mob = Cires::Items::MobValue(CireLoot::Get().Economy, State->Wave);
            Check(CireLoot::KillBounty(Mode, R) == FMath::RoundToInt(Mob * D.Rare.Bounty), FString::Printf(TEXT("the rare pays %.0f mob values (%d)"), D.Rare.Bounty, CireLoot::KillBounty(Mode, R)));
            R->SetActorLocation(Hero->GetActorLocation() + FVector(200, 0, 0));
            const int32 Before = Drops();
            CireLoot::NoteContribution(Hero, R);
            CireLoot::OnMonsterKilled(Mode, R, Hero, false);
            Check(Drops() > Before, TEXT("a rare kill drops a personal chest for the player"));
            ACireMonster* Ordinary = Lane0(0).Num() ? Lane0(0)[0] : nullptr;
            const int32 Mid = Drops();
            if (Ordinary) { Ordinary->SetActorLocation(Hero->GetActorLocation() + FVector(200, 0, 0)); CireLoot::OnMonsterKilled(Mode, Ordinary, Hero, false); }
            Check(Ordinary && Drops() == Mid, TEXT("an ordinary wave mob drops no chest"));
            if (auto* P = World->GetSubsystem<UCireExpansionPresenter>())
            {
                const int32 BannersBefore = P->RareBanners;
                P->Tick(.1f);
                Check(World->GetNetMode() == NM_DedicatedServer || P->RareBanners > BannersBefore, TEXT("the rare is announced with a banner and sting"));
            }
        }
        KillAll(); Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0;
        Check(CireWaveDirector::StartWave(Mode), TEXT("a developer start runs"));
        SpawnAll();
        Check(Lane0(1).Num() == 0, TEXT("test / developer starts never roll rares"));
        KillAll();
    }

    // ------------------------------------------------------------ Bonus Loot Wave
    {
        FCireWaveConfig C = D; C.WavesPerCycle = 3; C.Bonus.Chance = 1.f; C.Bonus.FromWave = 1; C.Bonus.MaxPerCycle = 1;
        Check(CireWaveDirector::ApplyLive(Mode, C, &Error), TEXT("bonus config applies"));
        Mode->CycleWavesSpawned = 1; State->CycleWavesDone = 1; State->Wave = 4; State->WavesPerCycle = 3;
        Mode->bSmoke = true;
        Check(CireWaveDirector::OnWaveCleared(Mode, 1) == 0.f, TEXT("no bonus waves in smoke runs"));
        Mode->bSmoke = false;
        Check(CireWaveDirector::OnWaveCleared(Mode, 3) == 0.f, TEXT("no bonus wave after the cycle's last wave (prep follows)"));
        const int32 Lives0 = State->EmberLives;
        const float Extra = CireWaveDirector::OnWaveCleared(Mode, 1);
        Check(FMath::IsNearlyEqual(Extra, C.Bonus.ExtraBreatherSeconds), TEXT("a bonus wave adds only its extra breather seconds"));
        Check(CireWaveDirector::OnWaveCleared(Mode, 2) == 0.f, TEXT("bonus waves are capped per cycle"));
        Check(State->Announcement.StartsWith(TEXT("BONUS LOOT WAVE")), TEXT("the bonus wave is announced"));
        SpawnAll();
        TArray<ACireMonster*> Bonus = Lane0(2);
        Check(Bonus.Num() == C.Bonus.Wave.UnitsPerLane(), FString::Printf(TEXT("the bonus wave spawns its creatures per lane (%d)"), Bonus.Num()));
        Check(!CireWaveDirector::BlocksNextWave(Mode), TEXT("bonus creatures never block the next wave"));
        int32 RareCount = 0, BonusCount = 0; CireWaveDirector::SpecialCounts(Mode, RareCount, BonusCount);
        Check(BonusCount >= 1, TEXT("the director counts bonus waves"));
        if (auto* P = World->GetSubsystem<UCireExpansionPresenter>())
        {
            const int32 BannersBefore = P->BonusBanners;
            P->Tick(.1f);
            Check(World->GetNetMode() == NM_DedicatedServer || P->BonusBanners > BannersBefore, TEXT("the bonus wave is announced with a banner and sting"));
        }
        if (Bonus.Num() >= 3)
        {
            ACireMonster* G = Bonus[0];
            const FCireWaveUnitInfo Info = CireWaveDirector::UnitFlags(G);
            Check(Info.bValid && Info.bBonus && !G->GetNPCDisplayName().StartsWith(TEXT("Rare")), TEXT("economy hook flags the bonus creature"));
            const int32 Mob = Cires::Items::MobValue(CireLoot::Get().Economy, Info.WaveNumber);
            Check(CireLoot::KillBounty(Mode, G) == FMath::RoundToInt(Mob * C.Bonus.Bounty), TEXT("a bonus creature pays its bounty in mob values"));
            // Flee: a champion close by makes it bolt away.
            Hero->SetActorLocation(G->GetActorLocation() + FVector(300, 0, 0));
            G->ConsumeMovementInputVector();
            Check(TickSpecial(G, Mode, .1f), TEXT("bonus creatures run their own behaviour"));
            const FVector Input = G->GetPendingMovementInputVector();
            Check(FVector::DotProduct(Input.GetSafeNormal2D(), (G->GetActorLocation() - Hero->GetActorLocation()).GetSafeNormal2D()) > .2f, TEXT("a bonus creature flees the nearest champion"));
            Check(G->Victim == nullptr && G->CastingAbility.IsEmpty(), TEXT("bonus creatures never attack"));
            // Escape after the clock: gone, no lives lost.
            const int32 Escaped = EscapedCount(World);
            G->SpecialEscapeAt = World->GetTimeSeconds() - 1.f;
            TickSpecial(G, Mode, .1f);
            Check(!Mode->Monsters.Contains(G) && EscapedCount(World) == Escaped + 1 && State->EmberLives == Lives0, TEXT("an uncaught bonus creature escapes without costing lives"));
            ACireMonster* L = Bonus[1];
            Mode->Leak(L);
            Check(!Mode->Monsters.Contains(L) && State->EmberLives == Lives0, TEXT("a bonus creature reaching the gate never costs lives"));
            ACireMonster* K = Bonus[2];
            K->SetActorLocation(Hero->GetActorLocation() + FVector(150, 0, 0));
            const int32 Before = Drops();
            CireLoot::NoteContribution(Hero, K);
            CireLoot::OnMonsterKilled(Mode, K, Hero, false);
            Check(Drops() > Before, TEXT("a caught bonus creature drops a personal purse"));
        }
        KillAll();
    }
    UE_LOG(LogCireExpansionTests, Display, TEXT("CIRE_MONSTER_EXPANSION_%s checks=%d creatures=%d fab_bodies=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, Creatures().Num(), FabBodies);
    return bPass;
}
#endif
