// monster-races: native checks for races, ranks, skill schedule and draws, riders, summons, race waves and the
// F8 editor race operations. Run by CireNPCCombat::RunSmoke (-CireCombatExpansionProbe; Tools/RunNPCChecks.py).
#include "CireRaces.h"
#include "CireGame.h"
#include "CireAudio.h"
#include "CireBuffs.h"
#include "CireAuraVisuals.h"
#include "CireMonsterArt.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireThreat.h"
#include "CireWaves.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"
#include "Misc/ScopeExit.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCireRaceTests, Log, All);

namespace
{
const TMap<FName, TArray<FString>>& ThemePrefixes()
{
    // "On theme": every race skill id carries its race's prefix (hollow keeps its original npc_/boss_ kit too).
    static const TMap<FName, TArray<FString>> P = {
        {TEXT("drowned_deep"), {TEXT("drowned_")}}, {TEXT("blightwood"), {TEXT("blight_")}},
        {TEXT("hollow"), {TEXT("hollow_"), TEXT("npc_"), TEXT("boss_")}}, {TEXT("ironhide"), {TEXT("ironhide_")}},
        {TEXT("drakkari"), {TEXT("drakkari_")}}, {TEXT("stoneborn"), {TEXT("stoneborn_")}}, {TEXT("feral_kin"), {TEXT("feral_")}},
        {TEXT("fallen_order"), {TEXT("fallen_")}}, {TEXT("voidborn"), {TEXT("void_")}}, {TEXT("aetheri"), {TEXT("aether_")}}}; // new-champions: Aetheri
    return P;
}
}

bool CireRaces::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    bool bPass = true; int32 Checks = 0;
    auto Check = [&](bool bValue, const FString& Why) { ++Checks; if (!bValue) { bPass = false; UE_LOG(LogCireRaceTests, Error, TEXT("CIRE_RACE_CHECK_FAIL %s"), *Why); } };
    const FCireRaceDatabase& D = Get();

    // ------------------------------------------------------------ data: 9 races x (6 units + 2 bosses), fallbacks, pools
    Check(D.bValid && D.Races.Num() == 10, FString::Printf(TEXT("ten races load (%d)"), D.Races.Num())); // new-champions: + the Aetheri Remnant
    int32 Units = 0, Skills = 0;
    for (const FName RaceId : D.Order)
    {
        const FCireRace& R = D.Races[RaceId];
        const FString Tag = RaceId.ToString();
        Check(R.Slots.Num() == 6 && R.Bosses.Num() == 2 && R.Units.Num() == 8, Tag + TEXT(" has 6 unit types and 2 bosses"));
        Check(R.Variants.Num() >= 3, Tag + TEXT(" has at least three reskin palettes"));
        TSet<FName> Distinct(R.Units);
        Check(Distinct.Num() == 8, Tag + TEXT(" units are distinct"));
        for (const FName Id : R.Units)
        {
            ++Units;
            const FCireNPCArchetype* A = CireNPCArchetypes::Find(Id);
            Check(A && A->RaceId == RaceId, Tag + TEXT(" unit ") + Id.ToString() + TEXT(" is an archetype of its race"));
            if (!A) continue;
            const bool bBoss = A->Slot == TEXT("warlord") || A->Slot == TEXT("colossus");
            Check(bBoss == (A->Classification == ECireNPCClass::Boss), Id.ToString() + TEXT(" boss slot matches boss classification"));
            // Every unit draws a body today: its own Tripo art or its fallback's.
            const CireMonsterArt::FArchetypeArt* Art = CireMonsterArt::Find(Id);
            Check(Art && !Art->Bodies.IsEmpty(), Id.ToString() + TEXT(" resolves a Tripo body (own art or fallback)"));
            Check(!A->FallbackBody.IsNone() && CireNPCArchetypes::Find(A->FallbackBody), Id.ToString() + TEXT(" names a valid fallback body"));
            const TArray<FName> Pool = MatchOrder(1, *A);
            Check(Pool.Num() >= (bBoss ? 3 : 2), Id.ToString() + TEXT(" has a skill pool"));
            for (const FCireNPCAbility& Ab : A->Abilities)
            {
                if (Ab.bBasic) continue;
                ++Skills;
                bool bTheme = false;
                for (const FString& Prefix : ThemePrefixes().FindRef(RaceId)) bTheme |= Ab.Id.ToString().StartsWith(Prefix);
                Check(bTheme, Id.ToString() + TEXT(" skill on theme: ") + Ab.Id.ToString());
                const bool bRaceSkill = !Ab.Id.ToString().StartsWith(TEXT("npc_")) && !Ab.Id.ToString().StartsWith(TEXT("boss_"));
                if (bRaceSkill)
                {
                    Check(!Ab.Buff.IsNone() && CireAuraData::Find(Ab.Buff), Ab.Id.ToString() + TEXT(" names a BuffVisuals.json visual"));
                    Check(!Ab.Cue.IsNone() && CireAudio::HasCue(Ab.Cue), Ab.Id.ToString() + TEXT(" names an AudioCues.json cue"));
                }
                if (Ab.Kind == ECireNPCAbilityKind::Summon) Check(RaceOf(Ab.SummonId) == RaceId, Ab.Id.ToString() + TEXT(" summons its own race"));
            }
        }
    }
    Check(Units == 80, FString::Printf(TEXT("80 race units (%d)"), Units)); // new-champions: + 8 Aetheri
    Check(CireRaces::UnitFor(TEXT("hollow"), TEXT("boss"), 0) == TEXT("hollow_siegebreaker") && CireRaces::UnitFor(TEXT("hollow"), TEXT("boss"), 1) == TEXT("gravemaw_pack_leader"),
        TEXT("boss slot alternates colossus / warlord by cycle"));

    // ------------------------------------------------------------ schedule: no skills before the configured wave
    FCireSkillProgression Rules;
    for (int32 W = 1; W < Rules.FirstSkillWave; ++W)
        for (int32 R = 0; R < static_cast<int32>(ECireNPCRank::Count); ++R)
            Check(Plan(Rules, W, static_cast<ECireNPCRank>(R)).Count == 0, FString::Printf(TEXT("no skills in wave %d at rank %d"), W, R));
    Check(Plan(Rules, 4, ECireNPCRank::Normal).Count == 1 && Plan(Rules, 4, ECireNPCRank::Normal).Tier == 1, TEXT("wave 4: one tier-I skill"));
    Check(Plan(Rules, 7, ECireNPCRank::Normal).Count == 2 && Plan(Rules, 10, ECireNPCRank::Normal).Count == 3 && Plan(Rules, 30, ECireNPCRank::Normal).Count == 3,
        TEXT("one more skill every three waves, capped at three"));
    Check(Plan(Rules, 9, ECireNPCRank::Normal).Tier == 2 && Plan(Rules, 14, ECireNPCRank::Normal).Tier == 3 && Plan(Rules, 40, ECireNPCRank::Normal).Tier == 3,
        TEXT("tier II from wave 9, tier III from wave 14 (max 3)"));
    Check(Plan(Rules, 4, ECireNPCRank::Elite).Count == 2 && Plan(Rules, 4, ECireNPCRank::Warlord).Count == 3 && Plan(Rules, 4, ECireNPCRank::Mythic).Count == 4,
        TEXT("ranks add skills"));
    FCireSkillProgression Late = Rules; Late.FirstSkillWave = 8;
    Check(Plan(Late, 7, ECireNPCRank::Mythic).Count == 0 && Plan(Late, 8, ECireNPCRank::Normal).Count == 1, TEXT("the first skill wave is configurable"));

    // ------------------------------------------------------------ per-match draws: deterministic per seed, vary across seeds, on theme
    {
        const FCireNPCArchetype* Tide = CireNPCArchetypes::Find(TEXT("tidecaller"));
        Check(Tide != nullptr, TEXT("tidecaller exists"));
        if (Tide)
        {
            Check(MatchOrder(4242, *Tide) == MatchOrder(4242, *Tide), TEXT("same seed, same draw order"));
            TSet<FString> Orders; TSet<FName> Hands;
            for (int32 Seed = 1; Seed <= 40; ++Seed)
            {
                const TArray<FName> O = MatchOrder(Seed, *Tide);
                TArray<FString> S; for (FName N : O) S.Add(N.ToString());
                Orders.Add(FString::Join(S, TEXT(",")));
                const TArray<FName> Hand = Loadout(Seed, *Tide, ECireNPCRank::Normal, 99);
                Check(Hand.Num() == FMath::Min(Tide->PoolDraw, O.Num()), TEXT("normal units stay inside the match hand (poolDraw)"));
                for (FName N : Hand) { Hands.Add(N); const auto* Ab = Tide->FindAbility(N); Check(Ab && !Ab->bBasic, TEXT("drawn skills come from the pool")); }
            }
            Check(Orders.Num() >= 6, FString::Printf(TEXT("draws vary across seeds (%d distinct orders)"), Orders.Num()));
            Check(Hands.Num() == MatchOrder(1, *Tide).Num(), TEXT("across seeds every pool skill gets drawn"));
            Check(Loadout(7, *Tide, ECireNPCRank::Champion, 99).Num() == MatchOrder(7, *Tide).Num(), TEXT("champions reach the whole pool"));
            Check(Loadout(7, *Tide, ECireNPCRank::Normal, 0).IsEmpty(), TEXT("zero count means no skills (core ones included)"));
        }
        const FCireNPCArchetype* Prophet = CireNPCArchetypes::Find(TEXT("drowned_prophet"));
        if (Prophet)
        {
            const TArray<FName> L = Loadout(3, *Prophet, ECireNPCRank::Warlord, 2);
            Check(L.Contains(TEXT("drowned_prophet_madness")) && L.Num() == 3, TEXT("core boss skills join once skills unlock"));
        }
    }

    // ------------------------------------------------------------ runtime fixtures
    const auto SavedClock = Mode->Clock; const auto SavedMonsters = Mode->Monsters; const auto SavedHeroes = Mode->Heroes;
    auto* State = Mode->GetGameState<ACireGameState>();
    const int32 SavedWave = State ? State->Wave : 0, SavedSpawned = Mode->CycleWavesSpawned, SavedDone = State ? State->CycleWavesDone : 0;
    const int32 SavedPerCycle = State ? State->WavesPerCycle : 5, SavedSeed = State ? State->MonsterSkillSeed : 0;
    const int32 SavedLives[2] = {State ? State->EmberLives : 0, State ? State->DuskLives : 0};
    const float SavedBreather = Mode->WaveBreatherSeconds, SavedTimer = Mode->WaveTimer;
    const FString SavedAnnouncement = State ? State->Announcement : FString();
    TArray<AActor*> Actors;
    ON_SCOPE_EXIT
    {
        for (auto* M : Mode->Monsters) if (IsValid(M) && !SavedMonsters.Contains(M)) Actors.AddUnique(M);
        for (auto* M : Mode->Monsters) if (IsValid(M)) CireNPCCombat::Interrupt(M);
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = SavedClock; Mode->Monsters = SavedMonsters; Mode->Heroes = SavedHeroes; Mode->CycleWavesSpawned = SavedSpawned;
        if (State)
        {
            State->Wave = SavedWave; State->CycleWavesDone = SavedDone; State->EmberLives = SavedLives[0]; State->DuskLives = SavedLives[1];
            State->Announcement = SavedAnnouncement;
        }
        CireWaveDirector::Initialize(Mode);
        if (State) { State->WavesPerCycle = SavedPerCycle; State->MonsterSkillSeed = SavedSeed; }
        Mode->WaveBreatherSeconds = SavedBreather; Mode->WaveTimer = SavedTimer;
    };
    Mode->Clock = Cires::MatchClock(); Mode->Monsters.Reset(); Mode->Heroes.Reset();
    UWorld* World = Mode->GetWorld();
    const FVector Ground(0, -2100, 3000);
    if (auto* Floor = World->SpawnActor<AActor>())
    {
        Actors.Add(Floor);
        auto* Box = NewObject<UBoxComponent>(Floor); Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
        Box->SetBoxExtent(FVector(2400, 1200, 50)); Box->SetCollisionObjectType(ECC_WorldStatic);
        Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Box->SetCollisionResponseToAllChannels(ECR_Block);
        Box->RegisterComponent(); Floor->SetActorLocation(Ground - FVector(0, 0, 50));
    }
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto Hero = [&](FVector Offset)
    {
        auto* H = World->SpawnActor<ACireHero>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, Params);
        if (H) { Actors.Add(H); H->SetActorTickEnabled(false); H->TeamId = 0; H->Draft(0); H->Health = H->MaxHealth = 100000; Mode->Heroes.Add(H); }
        return H;
    };
    auto Monster = [&](FName Id, FVector Offset)
    {
        auto* M = World->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 95), FRotator::ZeroRotator, Params);
        if (!M) return M;
        Actors.Add(M); M->SetActorTickEnabled(false); M->Lane = 0; Mode->Monsters.Add(M);
        CireNPCCombat::ConfigureArchetype(M, Id, 1, 0, 1);
        M->SetActorLocation(Ground + Offset + FVector(0, 0, M->GetCapsuleComponent()->GetScaledCapsuleHalfHeight())); M->SpawnPosition = M->GetActorLocation();
        return M;
    };

    // ------------------------------------------------------------ rank stats, classification, tint parameters
    {
        ACireMonster* M = Monster(TEXT("tidecaller"), FVector(0, 0, 0));
        Check(M && M->NPCState, TEXT("race unit spawns"));
        if (M && M->NPCState)
        {
            const float Health = M->MaxHealth, Damage = M->Damage;
            ApplyRank(M, ECireNPCRank::Champion, 1);
            Check(FMath::IsNearlyEqual(M->MaxHealth, Health * Rank(ECireNPCRank::Champion).Health, 1.f) && FMath::IsNearlyEqual(M->Damage, Damage * Rank(ECireNPCRank::Champion).Damage, .01f),
                TEXT("champion rank scales health and damage"));
            Check(M->GetNPCClassification() == ECireNPCClass::Elite && RankOf(M) == ECireNPCRank::Champion && M->NPCState->PaletteIndex == 1,
                TEXT("champion reads as elite classification with its palette"));
            Check(M->MonsterName.StartsWith(TEXT("Champion |")), TEXT("rank label on the monster name"));
            if (M->GetNetMode() != NM_DedicatedServer && M->MonsterArt && M->MonsterArt->IsTripoApplied())
            {
                auto* MID = Cast<UMaterialInstanceDynamic>(M->GetMesh()->GetMaterial(0));
                FLinearColor RankColorValue, RaceTint; float RaceStrength = 0, RimStrength = 0;
                const bool bParams = MID && MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("RankColor")), RankColorValue) &&
                    MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("RaceTint")), RaceTint) &&
                    MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("RaceTintStrength")), RaceStrength) &&
                    MID->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("RimStrength")), RimStrength);
                Check(HasSkin(M) && bParams, TEXT("race skin MID on the Tripo body"));
                Check(RankColorValue.Equals(Rank(ECireNPCRank::Champion).Color, .01f) && RimStrength > 0, TEXT("rank colour and rim parameters applied"));
                Check(RaceTint.Equals(FindRace(TEXT("drowned_deep"))->Palette(1).Base, .01f) && RaceStrength > .5f, TEXT("race palette variant parameters applied to a borrowed body"));
                UTexture* Base = nullptr;
                Check(MID && MID->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColorTex")), Base) && Base && Base->GetName().Contains(TEXT("BlightCaster")),
                    TEXT("skin keeps the body's own base colour texture"));
                ApplyRank(M, ECireNPCRank::Veteran, 0);
                FLinearColor Again; MID = Cast<UMaterialInstanceDynamic>(M->GetMesh()->GetMaterial(0));
                Check(MID && MID->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("RankColor")), Again) && Again.Equals(Rank(ECireNPCRank::Veteran).Color, .01f),
                    TEXT("rank changes update the same skin"));
            }
            else UE_LOG(LogCireRaceTests, Display, TEXT("CIRE_RACE_SKIN_SKIPPED (no rendered Tripo body in this process)"));
        }
        ACireMonster* Boss = Monster(TEXT("hollow_siegebreaker"), FVector(600, 0, 0));
        if (Boss)
        {
            Check(RankOf(Boss) == ECireNPCRank::Warlord, TEXT("boss classification maps to the warlord rank"));
            const float H = Boss->MaxHealth; ApplyRank(Boss, ECireNPCRank::Warlord, 0);
            Check(Boss->MaxHealth == H && Boss->GetNPCClassification() == ECireNPCClass::Boss, TEXT("warlord keeps the boss's own stats and frame"));
            ApplyRank(Boss, ECireNPCRank::Mythic, 0);
            Check(Boss->MaxHealth > H && Boss->MonsterName.StartsWith(TEXT("MYTHIC")), TEXT("mythic bosses are stronger"));
        }
        const float Base = Boss ? RankSize(Boss) : 0;
        Check(Rank(ECireNPCRank::Champion).Size > Rank(ECireNPCRank::Elite).Size && Rank(ECireNPCRank::Elite).Size > Rank(ECireNPCRank::Normal).Size && Base > 1.f,
            TEXT("size bump per rank"));
    }

    // ------------------------------------------------------------ loadouts: none before the wave, unlock on schedule, only active skills cast
    {
        ACireHero* Target = Hero(FVector(500, 0, 0));
        ACireMonster* M = Monster(TEXT("tidecaller"), FVector(0, 400, 0));
        if (Target && M && M->NPCState)
        {
            ApplyLoadout(M, Rules, 2);
            Check(M->NPCState->bLoadoutSet && M->NPCState->Loadout.IsEmpty() && M->NPCState->SkillTier == 0, TEXT("wave 2: no skills"));
            Check(M->NPCState->Abilities().Num() == 1 && M->NPCState->Abilities()[0].bBasic, TEXT("ability insight shows only the basic attack"));
            CireThreat::Engage(M, Target); M->NPCState->ReadyAt.Reset(); M->AbilityTimer = 0; M->AttackTimer = 0;
            Target->SetActorLocation(M->GetActorLocation() + FVector(500, 0, 0));
            CireNPCCombat::Tick(M, .01f);
            Check(M->NPCState->CastAbilityId.IsNone() || M->NPCState->CastAbilityId == TEXT("npc_bolt"), TEXT("a skill-less caster only casts its basic bolt"));
            CireNPCCombat::Interrupt(M);
            ApplyLoadout(M, Rules, 7);
            Check(M->NPCState->Loadout.Num() == 2 && M->NPCState->SkillTier == 1, TEXT("wave 7: two tier-I skills"));
            ApplyLoadout(M, Rules, 14);
            Check(M->NPCState->Loadout.Num() == 3 && M->NPCState->SkillTier == 3, TEXT("wave 14: three tier-III skills"));
            Check(M->NPCState->Abilities().ContainsByPredicate([](const FCireNPCAbilityInfo& I) { return I.Name.EndsWith(TEXT(" III")); }), TEXT("tier numeral in the insight"));
            Check(TierDamage(M) > 1.3f && TierCooldown(M) < .85f && TierDuration(M) > 1.25f, TEXT("tier III is stronger"));
            // Only drawn skills are ever attempted.
            const TArray<FName> Active = M->NPCState->Loadout;
            for (const FCireNPCAbility& Ab : M->NPCState->Archetype()->Abilities)
                if (!Ab.bBasic) Check(M->NPCState->IsAbilityActive(Ab.Id) == Active.Contains(Ab.Id), TEXT("inactive pool skills are gated: ") + Ab.Id.ToString());
            M->NPCState->Loadout = {TEXT("drowned_riptide")}; M->NPCState->ReadyAt.Reset(); M->AbilityTimer = 0; M->AttackTimer = 10;
            CireThreat::Engage(M, Target); CireNPCCombat::Tick(M, .01f);
            Check(M->NPCState->CastAbilityId == TEXT("drowned_riptide"), TEXT("the drawn skill is the one cast"));
            CireNPCCombat::Interrupt(M);
        }
    }

    // ------------------------------------------------------------ riders: root, silence, slow, knockback, pull, summon
    {
        ACireHero* H = Hero(FVector(-600, 0, 0));
        ACireMonster* M = Monster(TEXT("tidecaller"), FVector(-600, 700, 0));
        const FCireNPCArchetype* Tide = CireNPCArchetypes::Find(TEXT("tidecaller"));
        const FCireNPCArchetype* Leech = CireNPCArchetypes::Find(TEXT("mind_leech"));
        const FCireNPCArchetype* Stalker = CireNPCArchetypes::Find(TEXT("abyssal_stalker"));
        if (H && M && Tide && Leech && Stalker)
        {
            const FVector At = H->GetActorLocation();
            Check(OnAbilityReleased(M, *Tide->FindAbility(TEXT("drowned_whirlpool")), At) == 1 && IsRooted(H), TEXT("whirlpool roots the champion inside"));
            Check(CireBuffs::IsActive(H, TEXT("npc_rooted")), TEXT("root shows its buff visual"));
            Check(OnAbilityReleased(M, *Leech->FindAbility(TEXT("drowned_dread_whisper")), At) == 1 && IsSilenced(H), TEXT("dread whisper silences"));
            H->Skills = {TEXT("iron_guard")}; H->Cooldowns = {0.f}; H->Notice.Reset(); H->GlobalCooldown = 0;
            H->Cast(0);
            Check(H->Notice.Contains(TEXT("Silenced")) && H->Cooldowns[0] == 0.f, TEXT("a silenced champion cannot cast"));
            H->SlowUntil = 0;
            OnAbilityReleased(M, *Tide->FindAbility(TEXT("drowned_riptide")), At);
            Check(H->SlowUntil > World->GetTimeSeconds() && CireBuffs::IsActive(H, TEXT("npc_tide")), TEXT("riptide slows and marks the champion"));
            Check(OnAbilityReleased(M, *Tide->FindAbility(TEXT("drowned_whirlpool")), At + FVector(2000, 0, 0)) == 0, TEXT("champions outside the telegraph are untouched"));
            // Knockback from a cone: pushed away from the caster.
            M->SetActorLocation(H->GetActorLocation() + FVector(-250, 0, 0));
            H->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
            OnAbilityReleased(M, *Tide->FindAbility(TEXT("drowned_crashing_wave")), H->GetActorLocation());
            Check(H->GetCharacterMovement()->PendingLaunchVelocity.X > 300.f, TEXT("crashing wave knocks the champion back"));
            // Pull: dragged toward the caster.
            M->SetActorLocation(H->GetActorLocation() + FVector(-700, 0, 0));
            H->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
            OnAbilityReleased(M, *Stalker->FindAbility(TEXT("drowned_undertow")), H->GetActorLocation());
            Check(H->GetCharacterMovement()->PendingLaunchVelocity.X < -300.f, TEXT("undertow grab pulls the champion in"));
        }
        ACireMonster* Prophet = Monster(TEXT("drowned_prophet"), FVector(900, -500, 0));
        const FCireNPCArchetype* PA = CireNPCArchetypes::Find(TEXT("drowned_prophet"));
        if (Prophet && PA && H)
        {
            const FCireNPCAbility* Call = PA->FindAbility(TEXT("drowned_call_of_the_deep"));
            const int32 Before = Mode->Monsters.Num();
            Check(Call && SpawnSummons(Prophet, *Call) == 2 && Mode->Monsters.Num() == Before + 2, TEXT("call of the deep summons two stalkers"));
            int32 Stalkers = 0;
            for (auto* S : Mode->Monsters) if (IsValid(S) && S->NPCState && S->NPCState->ArchetypeId == TEXT("abyssal_stalker") && S->Lane == Prophet->Lane) { ++Stalkers; Actors.AddUnique(S); }
            Check(Stalkers == 2, TEXT("summons are the race's stalkers on the caster's lane"));
            Check(Call && SpawnSummons(Prophet, *Call) == 2 && !CanSummon(Prophet, *Call), TEXT("summons are capped at twice the count"));
            for (auto* S : Mode->Monsters) if (IsValid(S) && !Actors.Contains(S)) Actors.Add(S);
        }
    }

    // ------------------------------------------------------------ waves: the chosen race spawns, skills follow the schedule
    if (State)
    {
        Mode->Monsters.Reset(); Mode->CycleWavesSpawned = 0; State->CycleWavesDone = 0; State->Wave = 0;
        auto SpawnAll = [&]() { for (int32 I = 0; I < 64 && CireWaveDirector::HasPendingSpawns(Mode); ++I) CireWaveDirector::TickSurvival(Mode, 1.f);
            for (auto* M : Mode->Monsters) if (IsValid(M)) { Actors.AddUnique(M); M->SetActorTickEnabled(false); } };
        auto Lane0 = [&]() { TArray<ACireMonster*> Out; for (auto* M : Mode->Monsters) if (IsValid(M) && M->Lane == 0 && M->PackId < 0 && M->Health > 0) Out.Add(M); return Out; };
        auto KillAll = [&]() { for (auto* M : Mode->Monsters) if (IsValid(M)) M->Health = 0; Mode->Monsters.Reset(); };
        FCireWaveConfig C = CireWaveDirector::Defaults(); C.WavesPerCycle = 3;
        FCireWaveDef W = CireWaveDirector::Template(ECireWaveType::HybridPack); W.SpawnInterval = 0; W.Label = TEXT("Race probe");
        W.Race = TEXT("drowned_deep");
        W.Units[0].Rank = ECireNPCRank::Champion;
        C.Waves = {W, W, W};
        C.Waves[1].Race = TEXT("blightwood");
        FString Error;
        Check(CireWaveDirector::ApplyLive(Mode, C, &Error) && CireWaveDirector::StartWave(Mode), TEXT("race wave applies and starts: ") + Error);
        SpawnAll();
        TArray<ACireMonster*> First = Lane0();
        bool bAllDrowned = !First.IsEmpty(); int32 Champions = 0, WithSkills = 0;
        for (auto* M : First)
        {
            bAllDrowned &= M->NPCState && M->NPCState->Archetype() && M->NPCState->Archetype()->RaceId == TEXT("drowned_deep");
            Champions += RankOf(M) == ECireNPCRank::Champion;
            WithSkills += M->NPCState && !M->NPCState->Loadout.IsEmpty();
        }
        Check(bAllDrowned && First.Num() == W.UnitsPerLane(), FString::Printf(TEXT("a Drowned Deep wave spawns only drowned units (%d)"), First.Num()));
        Check(Champions == W.Units[0].Count, TEXT("the row rank spawns champions"));
        Check(WithSkills == 0 && State->Wave == 1, TEXT("wave 1 monsters have no skills"));
        Check(State->WaveRace == TEXT("drowned_deep") && State->WaveLabel.Contains(TEXT("Drowned")), TEXT("the wave's race is replicated and labelled"));
        KillAll();
        State->Wave = 9; // the next wave is global wave 10
        Check(CireWaveDirector::StartWave(Mode), TEXT("second race wave starts"));
        SpawnAll();
        TArray<ACireMonster*> Second = Lane0();
        bool bAllBlight = !Second.IsEmpty(), bSkilled = !Second.IsEmpty();
        for (auto* M : Second)
        {
            bAllBlight &= M->NPCState && M->NPCState->Archetype() && M->NPCState->Archetype()->RaceId == TEXT("blightwood");
            bSkilled &= M->NPCState && M->NPCState->bLoadoutSet && M->NPCState->Loadout.Num() >= 1 && M->NPCState->SkillTier == 2;
        }
        Check(bAllBlight, TEXT("the editor's per-wave race picks the Blightwood"));
        Check(bSkilled, TEXT("wave 10 monsters have tier-II skills"));
        KillAll();
        // Default campaign: hollow first, then Blightwood and the Drowned Deep, each cycle ending on a race boss.
        const FCireWaveConfig Def = CireWaveDirector::Defaults();
        auto BossOf = [&](int32 Cycle) { for (const auto& U : CireWaveDirector::ResolveWave(Def, 4, Cycle).Units) if (U.bBoss) return U.Archetype; return FName(); };
        Check(CireWaveDirector::ResolveWave(Def, 0, 0).Units[0].Archetype == TEXT("hollow_infantry"), TEXT("cycle 1 starts on the hollow basics"));
        Check(CireRaces::RaceOf(CireWaveDirector::ResolveWave(Def, 0, 1).Units[0].Archetype) == TEXT("blightwood") &&
            CireRaces::RaceOf(CireWaveDirector::ResolveWave(Def, 0, 2).Units[0].Archetype) == TEXT("drowned_deep"), TEXT("cycles 2 and 3 bring the Blightwood and the Drowned Deep"));
        Check(BossOf(0) == TEXT("hollow_siegebreaker") && BossOf(1) == TEXT("withered_matron") && BossOf(2) == TEXT("maw_of_the_deep"), TEXT("each cycle ends on one of its race's bosses"));
        const FCireWaveDef Mixed = CireWaveDirector::ResolveWave(Def, 0, 4);
        Check(CireRaces::RaceOf(Mixed.Units[0].Archetype) == TEXT("hollow") && CireRaces::RaceOf(Mixed.Units[1].Archetype) == TEXT("blightwood"), TEXT("mixed cycles alternate races row by row"));
        Check(CireWaveDirector::ResolveWave(Def, 0, 12).Units[0].Palette == 1, TEXT("the second rotation lap reskins with palette 1"));
        bool bMythic = false; for (const auto& U : CireWaveDirector::ResolveWave(Def, 4, 2).Units) bMythic |= U.bBoss && U.Rank == ECireNPCRank::Mythic;
        Check(bMythic, TEXT("cycle 3 bosses are mythic"));
    }

    // ------------------------------------------------------------ F8 editor race edits
    {
        FCireWaveConfig C = CireWaveDirector::Defaults();
        FCireWaveDef& W = C.Waves[0];
        CireWaveDirector::CycleWaveRace(W);
        Check(W.Race == Get().Order[0], TEXT("editor: race cycles from the rotation to the first race"));
        while (W.Race != TEXT("voidborn")) CireWaveDirector::CycleWaveRace(W);
        FCireWaveUnit& Row = W.Units[0];
        const FName SlotBefore = Row.Slot;
        CireWaveDirector::CycleRowUnit(Row, W.Race);
        Check(Row.Slot == TEXT("bruiser") && SlotBefore == TEXT("line") && Row.Archetype == TEXT("void_ravager"), TEXT("editor: unit cycles through the race slots"));
        for (int32 I = 0; I < 8; ++I) CireWaveDirector::CycleRowUnit(Row, W.Race);
        Check(Row.Slot.IsNone() && Row.Archetype == TEXT("rift_stalker"), TEXT("editor: after the slots come the race's explicit units"));
        CireWaveDirector::CycleRowRank(Row); CireWaveDirector::CycleRowRank(Row);
        Check(Row.Rank == ECireNPCRank::Elite, TEXT("editor: rank cycles"));
        Row.SkillTier = 2; Row.SkillCount = 1;
        FString Error;
        Check(CireWaveDirector::ApplyLive(Mode, C, &Error), TEXT("editor race edits apply live: ") + Error);
        const FCireWaveConfig& Live = CireWaveDirector::Config(World);
        Check(Live.Waves[0].Race == TEXT("voidborn") && Live.Waves[0].Units[0].Rank == ECireNPCRank::Elite && Live.Waves[0].Units[0].SkillTier == 2,
            TEXT("the applied config carries race, rank and skill overrides"));
        FCireWaveConfig Round; Check(CireWaveDirector::ParseJson(CireWaveDirector::ToJson(Live), Round, Error) && Round == Live, TEXT("race fields round-trip through Waves.json"));
        const FCireWaveDef R0 = CireWaveDirector::ResolveWave(Live, 0, 0);
        bool bVoid = true; for (const auto& U : R0.Units) bVoid &= CireRaces::RaceOf(U.Archetype) == TEXT("voidborn");
        Check(bVoid, TEXT("the edited wave resolves to Voidborn units"));
        FCireWaveConfig Bad = C; Bad.Waves[0].Race = TEXT("gnomes");
        Check(!CireWaveDirector::Validate(Bad, &Error, true), TEXT("unknown races are rejected"));
        Bad = C; Bad.Campaign.RaceRotation = {TEXT("hollow+gnomes")};
        Check(!CireWaveDirector::Validate(Bad, &Error, true), TEXT("unknown rotation races are rejected"));
    }
    UE_LOG(LogCireRaceTests, Display, TEXT("CIRE_RACE_SMOKE_%s checks=%d races=%d units=%d skills=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Checks, D.Races.Num(), Units, Skills);
    return bPass;
}
#endif
