#include "CireSkillshot.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireSkillCasting.h"

#if !UE_BUILD_SHIPPING
#include "CireCombatEvents.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSkillTests, Log, All);
namespace
{
struct FChecks
{
    int32 Count = 0;
    bool bPassed = true;
    void Check(bool bCondition, const TCHAR* Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireSkillTests, Error, TEXT("CIRE_SKILL_CHECK_FAIL %s"), Message); }
    }
    bool Finish(const TCHAR* Group) const
    {
        UE_LOG(LogCireSkillTests, Display, TEXT("CIRE_%s_%s checks=%d"), Group, bPassed ? TEXT("PASS") : TEXT("FAIL"), Count);
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
        Box(Ground + FVector(500, 0, -50), FVector(2600, 1000, 50));
    }
    ~FFixture()
    {
        // Destroy dependants before their owners, then restore the live roster.
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) Actors[I]->Destroy();
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters;
    }
    template<class T> T* Keep(T* Actor) { if (Actor) { Actors.Add(Actor); Actor->SetActorTickEnabled(false); } return Actor; }
    AActor* Box(FVector Center, FVector Half)
    {
        auto* Actor = Keep(Mode->GetWorld()->SpawnActor<AActor>());
        if (!Actor) return nullptr;
        auto* Body = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body);
        Body->SetBoxExtent(Half); Body->SetCollisionObjectType(ECC_WorldStatic);
        Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Body->SetCollisionResponseToAllChannels(ECR_Block);
        Body->RegisterComponent(); Actor->SetActorLocation(Center);
        return Actor;
    }
    ACireHero* Hero(int32 Team, FVector Offset)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero = Keep(Mode->GetWorld()->SpawnActor<ACireHero>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (Hero) { Hero->TeamId = Team; Hero->Draft(0); Hero->Health = Hero->MaxHealth = 1000; Hero->CriticalChance = 0; Mode->Heroes.Add(Hero); }
        return Hero;
    }
    ACireMonster* Monster(int32 Lane, FVector Offset)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Monster = Keep(Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (Monster) { Monster->Lane = Lane; Monster->Health = Monster->MaxHealth = 1000; Mode->Monsters.Add(Monster); }
        return Monster;
    }
    void Move(AActor* Actor, FVector Offset) { Actor->SetActorLocation(Ground + Offset + FVector(0, 0, 92), false, nullptr, ETeleportType::TeleportPhysics); }
};
}

bool CireSkillshots::RunSkillshotSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    auto* Hero = F.Hero(0, FVector(-500, 0, 0));
    auto* Other = F.Hero(1, FVector(-500, 700, 0));
    auto* First = F.Monster(0, FVector(0, 0, 0));
    auto* Second = F.Monster(0, FVector(400, 0, 0));
    if (!Hero || !Other || !First || !Second) { T.Check(false, TEXT("skillshot fixture spawned")); return T.Finish(TEXT("SKILLSHOT")); }
    FCireSkillshotSpec S; S.WarningSeconds = 0; S.Damage = 50; S.bCanCrit = false; S.Speed = 10000; S.MaxRange = 1500;
    T.Check(ACireSkillshot::ValidateSpec(S), TEXT("valid high-speed projectile accepted"));
    auto Invalid = S; Invalid.Speed = 0; T.Check(!ACireSkillshot::ValidateSpec(Invalid), TEXT("zero speed rejected"));
    Invalid = S; Invalid.ReflectionLimit = 9; T.Check(!ACireSkillshot::ValidateSpec(Invalid), TEXT("unbounded reflection rejected"));
    Invalid = S; Invalid.WorldCollision = static_cast<ECireProjectileCollision>(255); T.Check(!ACireSkillshot::ValidateSpec(Invalid), TEXT("unknown policy rejected"));
    auto Shoot = [&](AActor* Source, const FCireSkillshotSpec& Spec, FVector Aim) { return F.Keep(ACireSkillshot::Spawn(Source, Spec, Aim, TEXT("Fixture bolt"))); };
    auto* Fast = Shoot(Hero, S, F.Ground + FVector(1000, 0, 0));
    T.Check(Fast != nullptr, TEXT("free aim spawns without a selected target"));
    if (!Fast) return T.Finish(TEXT("SKILLSHOT"));
    T.Check(Fast->CanObserve(Hero) && !Fast->CanObserve(Other), TEXT("projectile hidden in opposing survival realm"));
    Fast->Tick(.2f);
    T.Check(FMath::IsNearlyEqual(First->Health, 950.f) && Second->Health == 1000 && Fast->IsActorBeingDestroyed(), TEXT("swept high-speed stop hits first unit only"));
    First->Health = Second->Health = 1000;
    auto* Miss = Shoot(Hero, S, F.Ground + FVector(1000, 0, 0));
    F.Move(First, FVector(0, 250, 0)); F.Move(Second, FVector(400, 250, 0));
    if (Miss) Miss->Tick(.2f);
    T.Check(Miss && First->Health == 1000 && Second->Health == 1000 && Miss->IsActorBeingDestroyed(), TEXT("moving out of fixed flight line dodges projectile"));
    F.Move(First, FVector(0, 0, 0)); F.Move(Second, FVector(400, 0, 0));
    S.Speed = 1000; S.MonsterCollision = ECireProjectileCollision::Pierce; S.HitLimit = 3;
    auto* Pierce = Shoot(Hero, S, F.Ground + FVector(1000, 0, 0));
    if (Pierce) { Pierce->Tick(.5f); Pierce->Tick(.03f); Pierce->Tick(.5f); }
    T.Check(Pierce && First->Health == 950 && Second->Health == 950 && Pierce->GetHitCount() == 2, TEXT("pierce resolves each unit once across frames"));
    if (Pierce) Pierce->Destroy();
    First->Health = Second->Health = 1000;
    S.HitLimit = 1;
    auto* Capped = Shoot(Hero, S, F.Ground + FVector(1000, 0, 0));
    if (Capped) Capped->Tick(1);
    T.Check(Capped && Capped->IsActorBeingDestroyed() && First->Health == 950 && Second->Health == 1000, TEXT("pierce obeys total hit limit"));
    S.HitLimit = 3; S.WarningSeconds = .5f;
    auto* Warning = Shoot(Hero, S, F.Ground + FVector(1000, 0, 0));
    const float BeforeWarning = First->Health;
    if (Warning) Warning->Tick(.49f);
    T.Check(Warning && !Warning->bReleased && First->Health == BeforeWarning && Warning->GetActorLocation().Equals(Hero->GetActorLocation()), TEXT("warning cannot travel or damage"));
    if (Warning) { Warning->Tick(.6f); Warning->Destroy(); }
    T.Check(Warning && Warning->bReleased && First->Health < BeforeWarning, TEXT("warning releases after its deadline"));
    S.WarningSeconds = 0; S.Speed = 10000; S.MonsterCollision = ECireProjectileCollision::Stop; S.HitLimit = 1;
    First->Health = Second->Health = 1000;
    F.Move(First, FVector(500, 0, 0)); F.Move(Second, FVector(600, 250, 0));
    AActor* Obstacle = F.Box(F.Ground + FVector(0, 0, 100), FVector(20, 150, 100));
    auto* Stopped = Shoot(Hero, S, F.Ground + FVector(1000, 0, 0)); if (Stopped) Stopped->Tick(.2f);
    T.Check(Stopped && Stopped->IsActorBeingDestroyed() && First->Health == 1000, TEXT("world geometry blocks stopped projectile"));
    S.WorldCollision = ECireProjectileCollision::Pierce;
    auto* Through = Shoot(Hero, S, F.Ground + FVector(1000, 0, 0)); if (Through) Through->Tick(.2f);
    T.Check(Through && First->Health == 950, TEXT("world pierce reaches unit behind geometry"));
    if (Obstacle) Obstacle->Destroy();
    S.WorldCollision = ECireProjectileCollision::Stop;
    F.Move(First, FVector(-500, 0, 0)); F.Move(Hero, FVector(1000, 0, 0)); F.Move(Second, FVector(200, 250, 0));
    FCireConstructSpec ShieldSpec; ShieldSpec.Kind = ECireConstructKind::Protection; ShieldSpec.bBlockMovement = false;
    ShieldSpec.Width = 400; ShieldSpec.ProtectionResponse = ECireProjectileCollision::Reflect;
    auto* Shield = F.Keep(ACireConstruct::Spawn(Hero, ShieldSpec, F.Ground + FVector(300, 0, 0), FRotator::ZeroRotator, TEXT("Reflect fixture")));
    First->Health = Hero->Health = 1000;
    auto* Reflected = Shoot(First, S, Hero->GetActorLocation()); if (Reflected) Reflected->Tick(.2f);
    T.Check(Shield && Reflected && Reflected->GetReflectionCount() == 1 && Reflected->GetSourceActor() == Hero && First->Health == 950 && Hero->Health == 1000,
        TEXT("protection reflects NPC projectile and transfers damage credit to protector"));
    if (Shield) Shield->Destroy();
    S.WarningSeconds = 1;
    auto* OwnerDied = Shoot(Hero, S, First->GetActorLocation()); Hero->bDead = true; if (OwnerDied) OwnerDied->Tick(.2f);
    T.Check(OwnerDied && OwnerDied->IsActorBeingDestroyed(), TEXT("dead caster cancels pending projectile")); Hero->bDead = false;
    auto* PhaseChanged = Shoot(Hero, S, First->GetActorLocation()); Mode->Clock.BeginIntermission(); if (PhaseChanged) PhaseChanged->Tick(.2f);
    T.Check(PhaseChanged && PhaseChanged->IsActorBeingDestroyed(), TEXT("phase change cancels projectile"));
    Mode->Clock.Advance(61);
    F.Ground.Y = Mode->ArenaPosition(0, 2).Y;
    F.Box(F.Ground + FVector(500, 0, -50), FVector(2000, 1000, 50));
    F.Move(Hero, FVector(-500, 0, 0)); F.Move(Other, FVector(300, 0, 0));
    S.WarningSeconds = 0; Hero->Health = Other->Health = 1000;
    auto* PvP = Shoot(Hero, S, Other->GetActorLocation()); if (PvP) PvP->Tick(.2f);
    T.Check(PvP && PvP->CanObserve(Other) && Other->Health == 950, TEXT("arena projectile visible to and damages opposing player"));
    S.PlayerCollision = ECireProjectileCollision::Ignore;
    auto* IgnorePlayer = Shoot(Hero, S, Other->GetActorLocation()); if (IgnorePlayer) IgnorePlayer->Tick(.2f);
    T.Check(IgnorePlayer && Other->Health == 950, TEXT("player ignore policy causes no damage"));
    return T.Finish(TEXT("SKILLSHOT"));
}

bool CireConstructs::RunConstructSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    auto* Hero = F.Hero(0, FVector(-500, 0, 0));
    auto* Other = F.Hero(1, FVector(-500, 700, 0));
    auto* Monster = F.Monster(0, FVector(700, 0, 0));
    auto* WrongLane = F.Monster(1, FVector(700, 500, 0));
    if (!Hero || !Other || !Monster || !WrongLane) { T.Check(false, TEXT("construct fixture spawned")); return T.Finish(TEXT("CONSTRUCT")); }
    FCireConstructSpec S; S.CastRange = 1600;
    T.Check(ACireConstruct::ValidateSpec(S), TEXT("default wall spec valid"));
    auto Bad = S; Bad.Height = -1; T.Check(!ACireConstruct::ValidateSpec(Bad), TEXT("negative wall height rejected"));
    FVector P = F.Ground + FVector(100, 0, 0);
    T.Check(ACireConstruct::ValidatePlacement(Hero, S, P, FRotator::ZeroRotator), TEXT("empty supported wall footprint valid"));
    auto* Wall = F.Keep(ACireConstruct::Spawn(Hero, S, P, FRotator::ZeroRotator, TEXT("Fixture wall")));
    T.Check(Wall != nullptr, TEXT("validated wall spawns")); if (!Wall) return T.Finish(TEXT("CONSTRUCT"));
    T.Check(Wall->CanObserve(Hero) && !Wall->CanObserve(Other), TEXT("construct visibility respects lane realm"));
    T.Check(Wall->CollisionBox->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block && Wall->BlocksMovementOf(Monster), TEXT("wall blocks pawn collision"));
    T.Check(ACireConstruct::FindBlockingConstruct(Monster, Hero) == Wall && ACireConstruct::FindBlockingConstruct(Monster, Hero->GetActorLocation()) == Wall, TEXT("actor and destination obstruction helpers identify wall"));
    T.Check(!Wall->BlocksMovementOf(WrongLane), TEXT("other lane units do not interact with wall"));
    FVector Overlap = P;
    T.Check(!ACireConstruct::ValidatePlacement(Hero, S, Overlap, FRotator::ZeroRotator), TEXT("existing wall overlap rejected"));
    FVector Unit = Hero->GetActorLocation() - FVector(0, 0, 92);
    T.Check(!ACireConstruct::ValidatePlacement(Hero, S, Unit, FRotator::ZeroRotator), TEXT("hero overlap rejected"));
    FVector EnemyUnit = Monster->GetActorLocation() - FVector(0, 0, 92);
    T.Check(!ACireConstruct::ValidatePlacement(Hero, S, EnemyUnit, FRotator::ZeroRotator), TEXT("monster overlap rejected"));
    FVector Boundary = F.Ground + FVector(0, 1060, 0);
    T.Check(!ACireConstruct::ValidatePlacement(Hero, S, Boundary, FRotator::ZeroRotator), TEXT("footprint crossing lane boundary rejected"));
    FVector Town = F.Ground + FVector(-1500, 0, 0);
    T.Check(!ACireConstruct::ValidatePlacement(Hero, S, Town, FRotator(0, 35, 0)), TEXT("rotated town overlap rejected"));
    T.Check(CireCombat::ApplyDamage(Hero, Wall, 100, TEXT("Friendly")) == 0 && CireCombat::ApplyDamage(Other, Wall, 100, TEXT("Other realm")) == 0 &&
        CireCombat::ApplyDamage(WrongLane, Wall, 100, TEXT("Other lane")) == 0, TEXT("friendly and unauthorized survival damage rejected"));
    T.Check(CireCombat::ApplyDamage(Monster, Wall, 100, TEXT("Wall strike")) == 100 && Wall->Health == 150, TEXT("own-lane NPC damages wall through combat pipeline"));
    CireCombat::ApplyDamage(Monster, Wall, 1000, TEXT("Wall break"));
    T.Check(Wall->IsActorBeingDestroyed() && Wall->CollisionBox->GetCollisionEnabled() == ECollisionEnabled::NoCollision && !ACireConstruct::FindBlockingConstruct(Monster, Hero), TEXT("wall destruction immediately opens unit route"));
    S.bBlockFriendly = false;
    auto* PassFriendly = F.Keep(ACireConstruct::Spawn(Hero, S, P, FRotator::ZeroRotator, TEXT("Friendly pass")));
    T.Check(PassFriendly && !PassFriendly->BlocksMovementOf(Hero) && PassFriendly->BlocksMovementOf(Monster), TEXT("friendly movement exception preserves hostile blocking"));
    if (PassFriendly) PassFriendly->Destroy();
    S.Kind = ECireConstructKind::Protection; S.bBlockMovement = false; S.ProtectionResponse = ECireProjectileCollision::Stop;
    auto* Shield = F.Keep(ACireConstruct::Spawn(Hero, S, P, FRotator::ZeroRotator, TEXT("Protection")));
    T.Check(Shield && Shield->IsProtection() && !Shield->BlocksMovementOf(Monster) && Shield->BlocksProjectilesFrom(Monster) && !Shield->BlocksProjectilesFrom(Hero), TEXT("protection separately blocks hostile projectiles without blocking units"));
    if (Shield) { Shield->Tick(S.LifetimeSeconds); T.Check(Shield->IsActorBeingDestroyed(), TEXT("construct lifetime expires")); }
    S.Kind = ECireConstructKind::Wall; S.bBlockMovement = true;
    auto* OwnerDead = F.Keep(ACireConstruct::Spawn(Hero, S, P, FRotator::ZeroRotator, TEXT("Owner lifecycle")));
    Hero->bDead = true; if (OwnerDead) OwnerDead->Tick(.01f);
    T.Check(OwnerDead && OwnerDead->IsActorBeingDestroyed(), TEXT("owner death removes constructs")); Hero->bDead = false;
    auto* Phase = F.Keep(ACireConstruct::Spawn(Hero, S, P, FRotator::ZeroRotator, TEXT("Phase lifecycle")));
    Mode->Clock.BeginIntermission(); if (Phase) Phase->Tick(.01f);
    T.Check(Phase && Phase->IsActorBeingDestroyed(), TEXT("phase change removes constructs"));
    Mode->Clock.Advance(61);
    F.Ground.Y = Mode->ArenaPosition(0, 2).Y;
    F.Box(F.Ground + FVector(500, 0, -50), FVector(2000, 1000, 50));
    F.Move(Hero, FVector(-500, 0, 0)); F.Move(Other, FVector(700, 0, 0));
    auto* ArenaWall = F.Keep(ACireConstruct::Spawn(Hero, S, F.Ground + FVector(100, 0, 0), FRotator::ZeroRotator, TEXT("Arena wall")));
    T.Check(ArenaWall && ArenaWall->CanObserve(Other) && CireCombat::ApplyDamage(Other, ArenaWall, 25, TEXT("PvP wall hit")) == 25, TEXT("opposing player can destroy arena construct"));
    T.Check(ArenaWall && CireCombat::ApplyDamage(Hero, ArenaWall, 25, TEXT("PvP friendly hit")) == 0, TEXT("arena friendly construct damage rejected"));
    return T.Finish(TEXT("CONSTRUCT"));
}

bool CireSummons::RunSummonSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    auto* Hero = F.Hero(0, FVector(-500, 0, 0));
    auto* Monster = F.Monster(0, FVector(700, 0, 0));
    if (!Hero || !Monster) { T.Check(false, TEXT("summon fixture spawned")); return T.Finish(TEXT("SUMMON")); }
    FCireSummonSpec S;
    T.Check(ACireSummon::ValidateSpec(S), TEXT("default summon specification valid"));
    auto Bad = S; Bad.Count = 7; T.Check(!ACireSummon::ValidateSpec(Bad), TEXT("unbounded summon count rejected"));
    auto Make = [&](const FCireSummonSpec& Spec, AActor* Target, FVector Offset)
    {
        auto Units = ACireSummon::SpawnGroup(Hero, Spec, Target, F.Ground + Offset);
        for (auto* Unit : Units) F.Keep(Unit);
        return Units;
    };
    auto Guardian = Make(S, nullptr, FVector(-100, 0, 0));
    T.Check(Guardian.Num() == 1 && Guardian[0]->GetOwnerHero() == Hero && Guardian[0]->bCommandable, TEXT("guardian creates one controllable owned unit"));
    if (Guardian.IsEmpty()) return T.Finish(TEXT("SUMMON"));
    auto* Unit = Guardian[0];
    T.Check(!Mode->Heroes.Contains(Unit) && Unit->TeamId == Hero->TeamId, TEXT("summon is allied but absent from team roster"));
    T.Check(Unit->Command(ECireSummonCommand::Move, F.Ground + FVector(100, 300, 0)) && Unit->CurrentCommand == ECireSummonCommand::Move, TEXT("guardian accepts server move command"));
    T.Check(Unit->Command(ECireSummonCommand::Hold, Unit->GetActorLocation()) && Unit->CurrentCommand == ECireSummonCommand::Hold, TEXT("guardian accepts hold command"));
    T.Check(!Unit->Command(ECireSummonCommand::Attack, Monster->GetActorLocation(), Hero), TEXT("guardian rejects friendly attack target"));
    T.Check(Unit->Command(ECireSummonCommand::Attack, Monster->GetActorLocation(), Monster), TEXT("guardian accepts hostile attack target"));
    T.Check(!Unit->Command(ECireSummonCommand::Move, F.Ground + FVector(0, 4200, 0)), TEXT("guardian rejects cross-realm destination"));
    T.Check(CireCombat::ApplyDamage(Hero, Unit, 100, TEXT("Friendly")) == 0, TEXT("summon rejects friendly damage"));
    CireCombat::ApplyDamage(Monster, Unit, 10000, TEXT("Summon defeat"));
    T.Check(Unit->bDead && Unit->IsActorBeingDestroyed(), TEXT("defeated summon is destroyed without hero revival"));
    S.bCommandable = false; S.Count = 3; S.CastRange = 800;
    T.Check(Make(S, nullptr, FVector(0, 0, 0)).IsEmpty(), TEXT("AI pack requires a valid hostile target"));
    auto Pack = Make(S, Monster, FVector(0, 0, 0));
    T.Check(Pack.Num() == 3, TEXT("AI pack spawns exactly three companions"));
    if (Pack.Num() == 3)
    {
        T.Check(!Pack[0]->Command(ECireSummonCommand::Hold, Pack[0]->GetActorLocation()), TEXT("AI pack rejects direct player commands"));
        T.Check(Pack[0]->Target == Monster && Pack[0]->CurrentCommand == ECireSummonCommand::Attack, TEXT("AI pack pursues selected hostile target"));
        for (auto* Companion : Pack) Companion->Tick(S.DurationSeconds);
        T.Check(Pack[0]->IsActorBeingDestroyed() && Pack[1]->IsActorBeingDestroyed() && Pack[2]->IsActorBeingDestroyed(), TEXT("all AI companions expire at duration"));
    }
    S.Count = 1; S.bCommandable = true;
    auto Death = Make(S, nullptr, FVector(0, 0, 0)); Hero->bDead = true;
    if (!Death.IsEmpty()) Death[0]->Tick(.01f);
    T.Check(!Death.IsEmpty() && Death[0]->IsActorBeingDestroyed(), TEXT("owner death removes companion")); Hero->bDead = false;
    auto Phase = Make(S, nullptr, FVector(0, 0, 0)); Mode->Clock.BeginIntermission();
    if (!Phase.IsEmpty()) Phase[0]->Tick(.01f);
    T.Check(!Phase.IsEmpty() && Phase[0]->IsActorBeingDestroyed(), TEXT("phase transition removes companion"));
    return T.Finish(TEXT("SUMMON"));
}

bool CireSkillCasting::RunCastSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    auto* Hero = F.Hero(0, FVector(-500, 0, 0));
    auto* Monster = F.Monster(0, FVector(700, 0, 0));
    if (!Hero || !Monster) { T.Check(false, TEXT("casting fixture spawned")); return T.Finish(TEXT("SKILL_CAST")); }
    ON_SCOPE_EXIT { ACireSkillshot::ClearForActor(Hero); ACireConstruct::ClearForActor(Hero); ACireSummon::ClearForActor(Hero); };
    const auto* Shot = CireSkillTuning::FindSkillshot(TEXT("ember_lance"));
    const auto* Wall = CireSkillTuning::FindConstruct(TEXT("summoned_wall"));
    const auto* Guardian = CireSkillTuning::FindSummon(TEXT("oathbound_guardian"));
    const auto* Pack = CireSkillTuning::FindSummon(TEXT("spectral_pack"));
    T.Check(Shot && Wall && Guardian && Pack, TEXT("casting recipes are loaded"));
    if (!Shot || !Wall || !Guardian || !Pack) return T.Finish(TEXT("SKILL_CAST"));
    Hero->Skills = {TEXT("ember_lance"), TEXT("summoned_wall"), TEXT("oathbound_guardian"), TEXT("spectral_pack"), TEXT("piercing_shot")};
    Hero->Cooldowns.Init(0, Hero->Skills.Num()); Hero->Mana = 1000; Hero->Energy = 100;
    Hero->bHasCastAim = true; Hero->CastAimPoint = F.Ground + FVector(300, 0, 0);
    auto Reset = [&] { Hero->Cooldowns.Init(0, Hero->Skills.Num()); Hero->GlobalCooldown = 0; };
    auto CountOwned = [&]
    {
        int32 Count = 0;
        for (TActorIterator<ACireSummon> It(Mode->GetWorld()); It; ++It)
            if (!It->IsActorBeingDestroyed() && It->GetOwnerHero() == Hero) ++Count;
        return Count;
    };
    T.Check(Handles(TEXT("ember_lance")) && Handles(TEXT("summoned_wall")) && Handles(TEXT("protection_dome")) &&
        Handles(TEXT("oathbound_guardian")) && Handles(TEXT("spectral_pack")) && !Handles(TEXT("basic_arrow")) && !Handles(TEXT("npc_shadow_bolt")), TEXT("player casting excludes NPC and basic projectile recipes"));
    T.Check(!CireSkillCasting::Cast(Hero, 0, TEXT("spectral_pack")) && Hero->Mana == 1000, TEXT("casting refuses slot identity mismatch"));
    Hero->Mana = 0;
    T.Check(!CireSkillCasting::Cast(Hero, 0, TEXT("ember_lance")) && Hero->Cooldowns[0] == 0, TEXT("insufficient mana does not start cooldown")); Hero->Mana = 1000;
    Hero->Energy = 0;
    T.Check(!CireSkillCasting::Cast(Hero, 4, TEXT("piercing_shot")) && Hero->Mana == 1000, TEXT("insufficient energy spends no resources")); Hero->Energy = 100;
    Hero->CastAimPoint = F.Ground + FVector(0, 4200, 0);
    T.Check(!CireSkillCasting::Cast(Hero, 0, TEXT("ember_lance")) && Hero->Mana == 1000 && Hero->GlobalCooldown == 0, TEXT("cross-realm aim is rejected without cost"));
    Hero->CastAimPoint = F.Ground + FVector(1500, 0, 0);
    T.Check(!CireSkillCasting::Cast(Hero, 0, TEXT("ember_lance")) && Hero->Mana == 1000, TEXT("out-of-range aim is rejected without cost"));
    Hero->CastAimPoint = F.Ground + FVector(300, 0, 0); Hero->Target = nullptr;
    T.Check(CireSkillCasting::Cast(Hero, 0, TEXT("ember_lance")) && Hero->Mana == 1000 - Shot->ManaCost && Hero->Cooldowns[0] > 0 && Hero->GlobalCooldown > 0, TEXT("free-aim cast succeeds and charges once after spawn"));
    const float ChargedMana = Hero->Mana;
    T.Check(!CireSkillCasting::Cast(Hero, 0, TEXT("ember_lance")) && Hero->Mana == ChargedMana, TEXT("cooldown prevents duplicate cast and duplicate charge"));
    Reset(); Hero->GlobalCooldown = 1;
    T.Check(!CireSkillCasting::Cast(Hero, 1, TEXT("summoned_wall")) && Hero->Mana == ChargedMana, TEXT("global cooldown blocks another ability")); Reset();
    Hero->CastAimPoint = F.Ground + FVector(100, 0, 0);
    T.Check(CireSkillCasting::Cast(Hero, 1, TEXT("summoned_wall")) && Hero->Mana == ChargedMana - Wall->ManaCost, TEXT("clear supported wall cast charges authored mana"));
    const float AfterWall = Hero->Mana; Reset();
    T.Check(!CireSkillCasting::Cast(Hero, 1, TEXT("summoned_wall")) && Hero->Mana == AfterWall && Hero->Cooldowns[1] == 0, TEXT("overlapping wall placement leaves resources and cooldown unchanged"));
    Hero->CastAimPoint = F.Ground + FVector(200, 0, 0);
    T.Check(!CireSkillCasting::Cast(Hero, 1, TEXT("summoned_wall")) && Hero->Mana == AfterWall, TEXT("ground placement cannot cross an existing wall"));
    ACireConstruct::ClearForActor(Hero); Reset();
    Hero->CastAimPoint = F.Ground + FVector(-100, 0, 0);
    T.Check(CireSkillCasting::Cast(Hero, 2, TEXT("oathbound_guardian")) && CountOwned() == 1 && Hero->Mana == AfterWall - Guardian->ManaCost, TEXT("guardian cast creates one unit and charges once"));
    Reset(); Hero->CastAimPoint = F.Ground + FVector(0, 200, 0); Hero->Target = nullptr;
    const float BeforePack = Hero->Mana;
    T.Check(!CireSkillCasting::Cast(Hero, 3, TEXT("spectral_pack")) && Hero->Mana == BeforePack && CountOwned() == 1, TEXT("pack without hostile target creates nothing and spends nothing"));
    Hero->Target = Monster;
    T.Check(CireSkillCasting::Cast(Hero, 3, TEXT("spectral_pack")) && CountOwned() == 4 && Hero->Mana == BeforePack - Pack->ManaCost, TEXT("pack cast atomically creates three units for one resource charge"));
    Reset(); const float BeforeCap = Hero->Mana;
    Hero->CastAimPoint = F.Ground + FVector(0, -200, 0);
    T.Check(!CireSkillCasting::Cast(Hero, 3, TEXT("spectral_pack")) && Hero->Mana == BeforeCap && CountOwned() == 4, TEXT("per-owner summon cap rejects entire pack without charge"));
    Reset(); Hero->bDead = true;
    T.Check(!CireSkillCasting::Cast(Hero, 0, TEXT("ember_lance")), TEXT("dead hero cannot bypass caller guard")); Hero->bDead = false;
    Mode->Clock.BeginIntermission();
    T.Check(!CireSkillCasting::Cast(Hero, 0, TEXT("ember_lance")), TEXT("noncombat phase cannot bypass caller guard"));
    return T.Finish(TEXT("SKILL_CAST"));
}
#endif
