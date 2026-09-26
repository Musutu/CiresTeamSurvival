// fix/summons: native checks that every fighting summon and construct engages (deals damage within a few
// simulated seconds of an enemy entering its range), that monsters can target summons, that the guardian
// holds threat, that summons scale off their owner, and the summons-bar data. Part of RunExpansionChecks.
#include "CireSummon.h"
#if !UE_BUILD_SHIPPING
#include "CireAttackSystem.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireMechTank.h"
#include "CireNPCCombat.h"
#include "CireRoleSkills.h"
#include "CireScalingKits.h"
#include "CireSkillCasting.h"
#include "CireSkillTuning.h"
#include "CireSummonsBar.h"
#include "CireTechConstructs.h"
#include "CireThreat.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSummonTests, Log, All);
namespace
{
struct FChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireSummonTests, Error, TEXT("CIRE_SUMMON_ENGAGE_CHECK_FAIL %s"), *Message); }
    }
    bool Finish() const
    {
        UE_LOG(LogCireSummonTests, Display, TEXT("CIRE_SUMMON_ENGAGE_%s checks=%d"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Count);
        return bPassed;
    }
};
struct FFixture
{
    ACireGameMode* Mode;
    Cires::MatchClock SavedClock;
    TArray<ACireHero*> SavedHeroes;
    TArray<ACireMonster*> SavedMonsters;
    TArray<AActor*> Actors;
    FVector Ground = FVector(0, -2100, 3000);
    explicit FFixture(ACireGameMode* InMode) : Mode(InMode), SavedClock(Mode->Clock), SavedHeroes(Mode->Heroes), SavedMonsters(Mode->Monsters)
    {
        Mode->Clock = Cires::MatchClock(); Mode->Heroes.Reset(); Mode->Monsters.Reset();
        auto* Floor = Keep(Mode->GetWorld()->SpawnActor<AActor>());
        if (Floor)
        {
            auto* Body = NewObject<UBoxComponent>(Floor);
            Floor->SetRootComponent(Body); Floor->AddInstanceComponent(Body);
            Body->SetBoxExtent(FVector(2600, 1000, 50)); Body->SetCollisionObjectType(ECC_WorldStatic);
            Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Body->SetCollisionResponseToAllChannels(ECR_Block);
            Body->RegisterComponent(); Floor->SetActorLocation(Ground + FVector(500, 0, -50));
        }
    }
    ~FFixture()
    {
        Clean();
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters;
    }
    void Clean()
    {
        for (AActor* A : Actors) if (IsValid(A)) { ACireSummon::ClearForActor(A); ACireConstruct::ClearForActor(A); }
        for (TActorIterator<ACireTargetProjectile> It(Mode->GetWorld()); It; ++It) It->Destroy();
    }
    template<class T> T* Keep(T* Actor) { if (Actor) { Actors.Add(Actor); Actor->SetActorTickEnabled(false); } return Actor; }
    ACireHero* Hero(FVector Offset, const TCHAR* Profile)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = Keep(Mode->GetWorld()->SpawnActor<ACireHero>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (!H) return nullptr;
        H->TeamId = 0;
        if (!Profile || !H->DraftProfile(Profile)) H->Draft(0);
        H->Health = H->MaxHealth = 5000; H->Mana = H->MaxMana = 5000; H->Energy = 100; H->CriticalChance = 0; H->Offers.Reset();
        Mode->Heroes.Add(H);
        return H;
    }
    ACireMonster* Monster(FVector Offset, float Health = 5000)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = Keep(Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (M) { M->Lane = 0; M->Health = M->MaxHealth = Health; Mode->Monsters.Add(M); }
        return M;
    }
    void Move(AActor* A, FVector Offset) { A->SetActorLocation(Ground + Offset + FVector(0, 0, 92), false, nullptr, ETeleportType::TeleportPhysics); }
    FVector At(FVector Offset) const { return Ground + Offset; }
};
void Learn(ACireHero* H, const FString& Id)
{
    H->Skills = {Id}; H->Cooldowns = {0.f}; H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100; H->Notice.Reset();
}
/**
 * Advances the units Dt at a time for up to Seconds: summons think, then walk by the movement input they
 * produced (at their walk speed), constructs think, basic projectiles fly. Returns the simulated time at
 * which Victim first lost health, or -1.
 */
float Simulate(UWorld* World, const TArray<AActor*>& Units, ACireMonster* Victim, float Seconds, float Dt = .1f)
{
    const float Start = Victim->Health;
    for (float T = 0; T < Seconds; T += Dt)
    {
        for (AActor* U : Units)
        {
            if (!IsValid(U) || U->IsActorBeingDestroyed()) continue;
            if (auto* S = Cast<ACireSummon>(U))
            {
                S->Tick(Dt);
                if (!IsValid(S) || S->IsActorBeingDestroyed()) continue;
                const FVector In = S->ConsumeMovementInputVector().GetClampedToMaxSize(1.f);
                if (!In.IsNearlyZero()) S->SetActorLocation(S->GetActorLocation() + In * S->GetCharacterMovement()->MaxWalkSpeed * Dt, false, nullptr, ETeleportType::TeleportPhysics);
            }
            else if (auto* C = Cast<ACireConstruct>(U)) C->Tick(Dt);
        }
        TArray<ACireTargetProjectile*> Shots;
        for (TActorIterator<ACireTargetProjectile> It(World); It; ++It) Shots.Add(*It);
        for (auto* Shot : Shots) if (IsValid(Shot) && !Shot->IsActorBeingDestroyed()) Shot->Tick(Dt);
        if (!IsValid(Victim) || Victim->Health < Start) return T + Dt;
    }
    return -1.f;
}
template<class T> TArray<AActor*> AsActors(const TArray<T*>& In) { TArray<AActor*> Out; for (T* A : In) Out.Add(A); return Out; }
ACireSummon* OwnedSummon(ACireHero* Owner)
{
    for (TActorIterator<ACireSummon> It(Owner->GetWorld()); It; ++It) if (It->GetOwnerHero() == Owner && !It->IsActorBeingDestroyed()) return *It;
    return nullptr;
}
}

bool CireSummons::RunEngagementSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    UWorld* World = Mode->GetWorld();
    constexpr float N = 6.f; // seconds a fighting summon may take to land its first hit once an enemy is in range
    auto* Hero = F.Hero(FVector(-500, 0, 0), TEXT("summoner"));
    auto* M = F.Monster(FVector(400, 0, 0));
    if (!Hero || !M) { T.Check(false, TEXT("fixture spawned")); return T.Finish(); }
    auto Report = [&](const TCHAR* What, float At) { T.Check(At >= 0 && At <= N, FString::Printf(TEXT("%s deals damage within %.0fs (took %.1fs)"), What, N, At)); };

    // ---- Oathbound Guardian, cast the way a player casts it: aimed at the ground, no target selected ----
    // The playtest bug: the guardian only fought a target ordered from the HUD, so it followed its owner forever.
    Learn(Hero, TEXT("oathbound_guardian")); Hero->Target = nullptr;
    Hero->bHasCastAim = true; Hero->CastAimPoint = F.At(FVector(-300, 0, 0));
    T.Check(CireSkillCasting::Cast(Hero, 0, TEXT("oathbound_guardian")), TEXT("guardian cast succeeds without a target"));
    Hero->bHasCastAim = false;
    ACireSummon* Guardian = OwnedSummon(Hero);
    T.Check(Guardian && Guardian->bCommandable && Guardian->SourceSkill == TEXT("oathbound_guardian"), TEXT("guardian spawned, commandable, tagged with its skill"));
    if (!Guardian) return T.Finish();
    F.Keep(Guardian);
    // Scaling: health grows with the owner's primary stat, damage is the Ability DB base + coefficient x primary.
    const FCireSummonSpec* GuardSpec = CireSkillTuning::FindSummon(TEXT("oathbound_guardian"));
    T.Check(GuardSpec && CireKits::PrimaryOf(Hero) > 0 && Guardian->MaxHealth > GuardSpec->Health * CireItems::SummonMultiplier(Hero) + 1.f,
        FString::Printf(TEXT("guardian health scales with primary (%.0f vs base %.0f)"), Guardian->MaxHealth, GuardSpec ? GuardSpec->Health : 0.f));
    T.Check(FMath::IsNearlyEqual(Guardian->SummonSpec.Damage, CireKits::Amount(Hero, TEXT("oathbound_guardian"), 22.f) * Mode->Power(0), .5f),
        TEXT("guardian damage is base + coefficient x primary"));
    T.Check(Guardian->CurrentCommand == ECireSummonCommand::Follow && Guardian->ChooseFightTarget() == M, TEXT("guardian picks the enemy near its owner on its own"));
    Report(TEXT("guardian (no target, enemy near owner)"), Simulate(World, {Guardian}, M, N));
    T.Check(Guardian->BasicTimer > 0 && Guardian->BasicTimer <= CireKits::InheritedInterval(Guardian, Guardian->BaseAttackSeconds()) + .01f,
        TEXT("guardian swing timer uses the owner's attack speed"));

    // Threat: a guardian is a tank summon. Its hits pull a monster that was on its owner, and it holds it.
    CireThreat::Clear(M); CireThreat::Engage(M, Hero); CireThreat::Select(M);
    T.Check(M->Victim == Hero, TEXT("monster starts on the owner"));
    CireCombat::ApplyStrike(Guardian, M, 40, TEXT("Guardian test strike"));
    T.Check(M->Victim == Guardian, TEXT("guardian hits pull the monster off its owner"));
    CireThreat::Damage(M, Hero, 40);
    T.Check(M->Victim == Guardian, TEXT("guardian holds threat against equal owner damage"));

    // Targetable: monsters pick summons up by proximity and can hurt them.
    CireThreat::Clear(M); M->bEngaged = false;
    CireNPCCombat::Configure(M, 0, 1); M->Health = M->MaxHealth = 5000; M->AttackTimer = 10; M->AbilityTimer = 10;
    F.Move(Hero, FVector(-1500, 0, 0)); F.Move(Guardian, FVector(150, 0, 0));
    CireNPCCombat::Tick(M, .01f);
    T.Check(M->Victim == Guardian, TEXT("a monster with no threat acquires the nearby summon"));
    const float GuardianBefore = Guardian->Health;
    T.Check(CireCombat::AreHostile(M, Guardian) && CireCombat::ApplyDamage(M, Guardian, 25, TEXT("Monster test strike")) > 0 && Guardian->Health < GuardianBefore,
        TEXT("monsters can damage summons"));
    CireNPCCombat::Interrupt(M); CireThreat::Clear(M); F.Move(Hero, FVector(-500, 0, 0));

    // Defend: an enemy hitting the owner, outside the guard radius of both, still draws the guardian.
    auto* Raider = F.Monster(FVector(-500, 1000, 0));
    F.Move(Guardian, FVector(-500, -250, 0)); F.Move(M, FVector(1900, 900, 0));
    Guardian->Target = nullptr; Guardian->CurrentCommand = ECireSummonCommand::Follow;
    if (Raider) { Raider->Victim = Hero; Report(TEXT("guardian defending its owner"), Simulate(World, {Guardian}, Raider, N)); }
    // Assist: the owner's selected enemy far beyond the guard radius.
    if (Raider) { F.Move(Raider, FVector(1900, -900, 0)); Raider->Victim = nullptr; }
    F.Move(M, FVector(1000, 0, 0)); F.Move(Guardian, FVector(-450, 0, 0));
    Guardian->Target = nullptr; Hero->Target = M;
    T.Check(Guardian->ChooseFightTarget() == M, TEXT("guardian assists the owner's selected target"));
    Report(TEXT("guardian assisting its owner's target"), Simulate(World, {Guardian}, M, N));
    // Hold: stays on its spot, fights only what reaches it.
    Hero->Target = nullptr; F.Move(M, FVector(300, 0, 0)); F.Move(Guardian, FVector(-450, 0, 0));
    T.Check(Guardian->Command(ECireSummonCommand::Hold, Guardian->GetActorLocation()), TEXT("hold command accepted"));
    const FVector HoldSpot = Guardian->GetActorLocation();
    T.Check(Simulate(World, {Guardian}, M, 1.f) < 0 && FVector::Dist2D(HoldSpot, Guardian->GetActorLocation()) < 5.f, TEXT("holding guardian ignores enemies out of reach"));
    F.Move(M, FVector(-300, 0, 0));
    Report(TEXT("holding guardian when an enemy walks up"), Simulate(World, {Guardian}, M, N));
    // Retaliation: attacked from behind with nothing else around.
    Guardian->Command(ECireSummonCommand::Follow, Guardian->GetActorLocation());
    F.Move(M, FVector(-450, 1000, 0)); CireCombat::ApplyDamage(M, Guardian, 5, TEXT("Monster test strike"));
    T.Check(Guardian->ChooseFightTarget() == M, TEXT("guardian retaliates against its attacker"));
    F.Clean();

    // ---- Spectral Pack: after its first target dies it keeps fighting ----
    auto* Second = F.Monster(FVector(300, 350, 0));
    auto* Prey = F.Monster(FVector(300, 0, 0));
    F.Move(M, FVector(1900, 900, 0)); M->Health = M->MaxHealth;
    if (Raider) F.Move(Raider, FVector(1900, -900, 0));
    if (!Prey) return T.Finish();
    Learn(Hero, TEXT("spectral_pack")); Hero->Target = Prey; Hero->bHasCastAim = true; Hero->CastAimPoint = F.At(FVector(-250, 0, 0));
    T.Check(CireSkillCasting::Cast(Hero, 0, TEXT("spectral_pack")), TEXT("spectral pack cast"));
    Hero->bHasCastAim = false;
    TArray<ACireSummon*> Pack;
    for (TActorIterator<ACireSummon> It(World); It; ++It) if (It->GetOwnerHero() == Hero && !It->IsActorBeingDestroyed()) Pack.Add(F.Keep(*It));
    T.Check(Pack.Num() == 3, TEXT("three spectral hunters"));
    Report(TEXT("spectral pack on its cast target"), Simulate(World, AsActors(Pack), Prey, N));
    CireCombat::ApplyDamage(Hero, Prey, 1.e6f, TEXT("Test kill"));
    Hero->Target = nullptr;
    if (Second) Report(TEXT("spectral pack after its target died"), Simulate(World, AsActors(Pack), Second, N));
    F.Clean();

    // ---- Spectral Hunt (ultimate) through the role-skill caster ----
    if (Second)
    {
        Second->Health = Second->MaxHealth; F.Move(Second, FVector(200, 0, 0));
        Learn(Hero, TEXT("spectral_hunt")); Hero->Target = Second;
        T.Check(CireRoleSkills::Cast(Hero, 0, TEXT("spectral_hunt")), TEXT("spectral hunt cast"));
        TArray<ACireSummon*> Hunt;
        for (TActorIterator<ACireSummon> It(World); It; ++It) if (It->GetOwnerHero() == Hero && !It->IsActorBeingDestroyed()) Hunt.Add(F.Keep(*It));
        T.Check(Hunt.Num() == 3 && Hunt[0]->SourceSkill == TEXT("spectral_hunt"), TEXT("three hunters tagged spectral_hunt"));
        Report(TEXT("spectral hunt"), Simulate(World, AsActors(Hunt), Second, N));
        F.Clean(); Hero->Target = nullptr;
    }

    // ---- Mechanical Tank: guards the owner and taunts ----
    {
        auto* Knight = F.Hero(FVector(-500, 300, 0), TEXT("knight"));
        auto* Brute = F.Monster(FVector(200, 300, 0));
        if (Knight && Brute)
        {
            Brute->Victim = Knight; CireThreat::Engage(Brute, Knight); CireThreat::Select(Brute);
            ACireMechTank* Mech = ACireMechTank::SpawnFor(Knight, F.At(FVector(-300, 300, 0)));
            T.Check(Mech && Mech->SourceSkill == TEXT("mechanical_tank"), TEXT("mechanical tank spawned and tagged"));
            if (Mech) { F.Keep(Mech); Report(TEXT("mechanical tank"), Simulate(World, {Mech}, Brute, N)); ACireSummon::ClearForActor(Knight); }
            F.Move(Brute, FVector(1900, 0, 0)); Brute->Victim = nullptr; CireThreat::Clear(Brute); // out of the construct cases' reach
        }
    }

    // ---- Artificer constructs: turret, obelisk, skitters, arc mine; Huntress lantern trap ----
    auto* Artificer = F.Hero(FVector(-500, -300, 0), TEXT("aetheri_artificer"));
    if (Raider) F.Move(Raider, FVector(1900, -900, 0));
    if (Second) F.Move(Second, FVector(1900, 900, 0));
    struct FCase { const TCHAR* Recipe; FVector Aim, Enemy; };
    const FCase Cases[] = {
        {TEXT("photon_turret"), FVector(-200, -300, 0), FVector(400, -300, 0)},
        {TEXT("warp_obelisk"), FVector(-200, -300, 0), FVector(600, -300, 0)},
        {TEXT("skitter_swarm"), FVector(-200, -300, 0), FVector(300, -300, 0)},
        {TEXT("arc_mine"), FVector(250, -300, 0), FVector(250, -300, 0)}};
    for (const FCase& C : Cases)
    {
        if (!Artificer) break;
        F.Move(M, C.Enemy); M->Health = M->MaxHealth; M->Victim = nullptr;
        FString Why;
        const auto Built = CireTechConstructs::Deploy(Artificer, C.Recipe, F.At(C.Aim), &Why);
        T.Check(!Built.IsEmpty(), FString(C.Recipe) + TEXT(" placed: ") + Why);
        for (auto* B : Built) T.Check(CireCombat::AreHostile(M, B), FString(C.Recipe) + TEXT(" is targetable by monsters"));
        if (!Built.IsEmpty()) Report(C.Recipe, Simulate(World, AsActors(Built), M, N));
        F.Clean(); ACireConstruct::ClearForActor(Artificer);
    }
    if (auto* Huntress = F.Hero(FVector(-500, -600, 0), TEXT("huntress")))
    {
        ACireSummon::ClearForActor(Huntress); // no companion in this case
        F.Move(M, FVector(250, -600, 0)); M->Health = M->MaxHealth;
        FString Why;
        const auto Built = CireTechConstructs::Deploy(Huntress, TEXT("spirit_lantern"), F.At(FVector(250, -600, 0)), &Why);
        T.Check(!Built.IsEmpty(), TEXT("spirit lantern placed: ") + Why);
        if (!Built.IsEmpty()) Report(TEXT("spirit_lantern"), Simulate(World, AsActors(Built), M, N));
        ACireConstruct::ClearForActor(Huntress);
    }

    // ---- Summons bar data ----
    F.Move(M, FVector(400, 0, 0)); M->Health = M->MaxHealth; Hero->Target = M;
    Learn(Hero, TEXT("oathbound_guardian")); Hero->bHasCastAim = true; Hero->CastAimPoint = F.At(FVector(-300, 0, 0));
    CireSkillCasting::Cast(Hero, 0, TEXT("oathbound_guardian"));
    Learn(Hero, TEXT("spectral_pack")); Hero->CastAimPoint = F.At(FVector(-300, 250, 0));
    CireSkillCasting::Cast(Hero, 0, TEXT("spectral_pack"));
    Hero->bHasCastAim = false;
    FString Why;
    const auto Turret = CireTechConstructs::Deploy(Hero, TEXT("photon_turret"), F.At(FVector(-300, -250, 0)), &Why);
    auto* Other = F.Hero(FVector(-500, 600, 0), TEXT("knight"));
    if (Other) { Other->Target = M; Learn(Other, TEXT("spectral_pack")); Other->bHasCastAim = true; Other->CastAimPoint = F.At(FVector(-300, 600, 0)); CireSkillCasting::Cast(Other, 0, TEXT("spectral_pack")); Other->bHasCastAim = false; }
    const float Now = World->GetTimeSeconds();
    const TArray<FCireSummonBarEntry> Bar = CireSummonsBar::Collect(Hero, Now);
    T.Check(Bar.Num() == 5, FString::Printf(TEXT("summons bar lists the guardian, three hunters and the turret (got %d)"), Bar.Num()));
    if (Bar.Num() == 5)
    {
        T.Check(Bar[0].bCommandable && Bar[0].IconId == TEXT("oathbound_guardian") && Bar[0].Name == TEXT("Oathbound Guardian") && Bar[0].Kind == ECireSummonBarKind::Summon,
            TEXT("commandable guardian listed first with its icon"));
        T.Check(Bar[0].Remaining > 0 && Bar[0].Remaining <= Bar[0].Duration && Bar[0].MaxHealth > 0 && Bar[0].Health == Bar[0].MaxHealth, TEXT("guardian entry has health and a timer"));
        T.Check(Bar[1].IconId == TEXT("spectral_pack") && !Bar[1].bCommandable && Bar[3].IconId == TEXT("spectral_pack"), TEXT("hunters follow the guardian"));
        T.Check(Bar[4].Kind == ECireSummonBarKind::Construct && Bar[4].IconId == TEXT("photon_turret") && Bar[4].bFights && Bar[4].Remaining > 0 &&
            !Bar[4].Name.IsEmpty() && Bar[4].Name != TEXT("photon_turret"), FString(TEXT("turret listed last with its DB name: ")) + (Bar.IsValidIndex(4) ? Bar[4].Name : FString()));
    }
    T.Check(CireSummonsBar::HasCommandable(Hero) && (!Other || !CireSummonsBar::HasCommandable(Other)), TEXT("commandable detection per owner"));
    T.Check(!Other || CireSummonsBar::Collect(Other, Now).Num() == 3, TEXT("another champion's summons stay off this bar"));
    T.Check(CireSummonsBar::TimerText(65.f) == TEXT("1:05") && CireSummonsBar::TimerText(4.2f) == TEXT("5s"), TEXT("timer text"));
    ACireSummon::ClearForActor(Hero);
    T.Check(CireSummonsBar::Collect(Hero, Now).Num() == 1, TEXT("dismissed summons leave the bar"));
    if (Other) ACireSummon::ClearForActor(Other);
    F.Clean();
    return T.Finish();
}
#endif
