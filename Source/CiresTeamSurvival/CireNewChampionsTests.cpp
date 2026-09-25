// new-champions: native checks for the new kits, the Aetheri Constructs, the art bindings and the Aetheri race.
#include "CireSignatureSkills.h"
#include "CireTechConstructs.h"

#if !UE_BUILD_SHIPPING
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireAttackSystem.h"
#include "CireBuffs.h"
#include "CireChampionArt.h"
#include "CireChampionRoster.h"
#include "CireClassTraits.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCreatureArt.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireMonsterAnim.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireSkillshot.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireNewChampionTests, Log, All);
namespace
{
struct FChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireNewChampionTests, Error, TEXT("CIRE_NEW_CHAMPIONS_CHECK_FAIL %s"), *Message); }
    }
    bool Finish(const TCHAR* Group) const
    {
        UE_LOG(LogCireNewChampionTests, Display, TEXT("CIRE_%s_%s checks=%d"), Group, bPassed ? TEXT("PASS") : TEXT("FAIL"), Count);
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
        UWorld* World = Mode->GetWorld();
        for (TActorIterator<ACireConstruct> It(World); It; ++It) if (It->IsTech()) It->Destroy();
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) { ACireAreaEffect::ClearForActor(Actors[I]); ACireConstruct::ClearForActor(Actors[I]); Actors[I]->Destroy(); }
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
    ACireHero* Hero(int32 Team, FVector Offset, const TCHAR* Profile)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Hero = Keep(Mode->GetWorld()->SpawnActor<ACireHero>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (!Hero) return nullptr;
        Hero->TeamId = Team;
        if (!Hero->DraftProfile(Profile)) Hero->Draft(0);
        Hero->Health = Hero->MaxHealth = 2000; Hero->Mana = Hero->MaxMana = 5000; Hero->Energy = 100; Hero->CriticalChance = 0;
        Hero->Offers.Reset(); Mode->Heroes.Add(Hero);
        return Hero;
    }
    ACireMonster* Monster(int32 Lane, FVector Offset, float Health = 5000)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Monster = Keep(Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (Monster) { Monster->Lane = Lane; Monster->Health = Monster->MaxHealth = Health; Mode->Monsters.Add(Monster); }
        return Monster;
    }
    FVector At(FVector Offset) const { return Ground + Offset; }
};
void Learn(ACireHero* H, const FString& Id)
{
    H->Skills.Reset(); H->Cooldowns.Reset(); H->Skills.Add(Id); H->Cooldowns.Add(0);
    H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100; H->Notice.Reset();
}
TArray<ACireConstruct*> Owned(ACireHero* H, FName Recipe)
{
    TArray<ACireConstruct*> Out;
    for (TActorIterator<ACireConstruct> It(H->GetWorld()); It; ++It)
        if (!It->IsActorBeingDestroyed() && It->GetSourceActor() == H && (Recipe.IsNone() || It->ConstructSpec.Recipe == Recipe)) Out.Add(*It);
    return Out;
}
void Clean(UWorld* World) { for (TActorIterator<ACireConstruct> It(World); It; ++It) if (It->IsTech()) It->Destroy(); }
}

// ============================================================================================ constructs
bool CireTechConstructs::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    UWorld* World = Mode->GetWorld();
    auto* Hero = F.Hero(0, FVector(-500, 0, 0), TEXT("aetheri_artificer"));
    auto* Ally = F.Hero(0, FVector(-500, 300, 0), TEXT("gunblade"));
    auto* M = F.Monster(0, FVector(400, 0, 0));
    if (!Hero || !Ally || !M) { T.Check(false, TEXT("construct fixture spawned")); return T.Finish(TEXT("TECH_CONSTRUCTS")); }
    // Recipes: every champion construct skill has a recipe and a valid spec; every recipe is a DB construct.
    for (const FCireTechRecipe& X : Recipes())
    {
        FCireConstructSpec S;
        if (!X.bMonster)
        {
            T.Check(BuildSpec(Hero, X.Id, S) && S.IsTech() && S.Recipe == X.Id, X.Id.ToString() + TEXT(" builds a valid champion spec"));
            const auto* D = CireAbilityDB::Find(X.Id.ToString());
            T.Check(D && D->IsConstruct() && D->IsImplemented(), X.Id.ToString() + TEXT(" is an implemented Constructs skill in the Ability DB"));
        }
        else T.Check(BuildSpec(M, X.Id, S, 1.f) && S.AttackDamage >= 0, X.Id.ToString() + TEXT(" builds a valid monster spec"));
    }
    // Turret: places on ground-aim, fires energy bolts, respects the per-owner limit, dies, expires.
    FString Why;
    auto Turrets = Deploy(Hero, TEXT("photon_turret"), F.At(FVector(0, 0, 0)), &Why);
    T.Check(Turrets.Num() == 1 && Turrets[0]->ConstructSpec.Kind == ECireConstructKind::Turret && Turrets[0]->GetIsReplicated() && Turrets[0]->OriginTeam == 0,
        TEXT("photon turret placed, replicated, team 0: ") + Why);
    if (Turrets.Num() == 1)
    {
        auto* Turret = Turrets[0];
        const float Before = M->Health;
        Turret->Tick(.5f);
        T.Check(M->Health < Before && Turret->ShotSerial > 0, TEXT("turret auto-attacks the monster in range"));
        T.Check(Turret->CanBeDamagedBy(M) && !Turret->CanBeDamagedBy(Ally), TEXT("own-lane monster can destroy the turret, allies cannot"));
        Deploy(Hero, TEXT("photon_turret"), F.At(FVector(0, 250, 0)), &Why);
        Deploy(Hero, TEXT("photon_turret"), F.At(FVector(0, -250, 0)), &Why);
        T.Check(CountOwned(Hero, TEXT("photon_turret")) == 2 && Turret->IsActorBeingDestroyed(), TEXT("third turret replaces the oldest (limit 2)"));
        for (auto* C : Owned(Hero, TEXT("photon_turret")))
        {
            CireCombat::ApplyDamage(M, C, 100000, TEXT("Smash construct"));
            T.Check(C->IsActorBeingDestroyed(), TEXT("turret destroyed by monster damage"));
        }
    }
    auto Expiring = Deploy(Hero, TEXT("photon_turret"), F.At(FVector(0, 0, 0)), &Why);
    if (Expiring.Num() == 1) { Expiring[0]->Tick(Expiring[0]->ConstructSpec.LifetimeSeconds + .1f); T.Check(Expiring[0]->IsActorBeingDestroyed(), TEXT("turret expires after its lifetime")); }
    Clean(World);
    // Arc mine: arms, then detonates on the first enemy inside its trigger radius.
    auto Mines = Deploy(Hero, TEXT("arc_mine"), F.At(FVector(330, 0, 0)), &Why);
    T.Check(Mines.Num() == 1, TEXT("arc mine placed: ") + Why);
    if (Mines.Num() == 1)
    {
        const float Before = M->Health;
        Mines[0]->Tick(.1f);
        T.Check(!Mines[0]->IsActorBeingDestroyed() && M->Health == Before, TEXT("mine stays harmless while arming"));
        Mines[0]->Tick(.7f);
        T.Check(Mines[0]->IsActorBeingDestroyed() && M->Health < Before, TEXT("armed mine detonates on the monster"));
    }
    // Stasis snare: stuns (non-boss) through the Ability DB effect.
    auto* Warden = F.Hero(0, FVector(-500, -300, 0), TEXT("aetheri_warden"));
    auto Snares = Warden ? Deploy(Warden, TEXT("stasis_snare"), F.At(FVector(380, 0, 0)), &Why) : TArray<ACireConstruct*>();
    T.Check(Snares.Num() == 1, TEXT("stasis snare placed: ") + Why);
    if (Snares.Num() == 1) { Snares[0]->Tick(.1f); Snares[0]->Tick(.7f); T.Check(Snares[0]->IsActorBeingDestroyed() && CireCrowdControl::IsStunned(M), TEXT("stasis snare locks the monster in stasis")); }
    M->SlowUntil = 0;
    // Pylons: fields apply every pulse; the field is a replicated arcane ground area.
    if (Warden)
    {
        auto Gravity = Deploy(Warden, TEXT("gravity_pylon"), F.At(FVector(400, 120, 0)), &Why);
        T.Check(Gravity.Num() == 1, TEXT("gravity pylon placed: ") + Why);
        bool bField = false;
        for (TActorIterator<ACireAreaEffect> It(World); It; ++It) if (Gravity.Num() && !It->IsActorBeingDestroyed() && FVector::Dist2D(It->GetActorLocation(), Gravity[0]->GetActorLocation()) < 5) bField = true;
        T.Check(bField, TEXT("pylon field ground rune area spawned"));
        if (Gravity.Num()) { Gravity[0]->Tick(.1f); T.Check(M->SlowUntil > World->GetTimeSeconds(), TEXT("gravity field slows the monster")); }
        auto Haste = Deploy(Warden, TEXT("haste_pylon"), F.At(FVector(-500, 150, 0)), &Why);
        if (Haste.Num()) { Haste[0]->Tick(.1f); }
        T.Check(Haste.Num() == 1 && CireBuffs::IsActive(Ally, TEXT("aether_haste")) && CireSignatureSkills::MoveSpeedMultiplier(Ally) > 1.2f &&
            CireSignatureSkills::AttackSpeedBonus(Ally) > .2f, TEXT("haste field speeds up allies"));
        auto Aegis = Deploy(Warden, TEXT("aegis_pylon"), F.At(FVector(-500, 450, 0)), &Why);
        Ally->Health = 1000;
        if (Aegis.Num()) Aegis[0]->Tick(.1f);
        T.Check(Aegis.Num() == 1 && Ally->Health > 1000 && CireBuffs::IsActive(Ally, TEXT("aether_aegis")), TEXT("aegis field regenerates allies"));
        const float Guarded = CireSignatureSkills::ModifyOutgoingDamage(M, Ally, 100.f, TEXT("Test"));
        T.Check(Guarded < 90.f, TEXT("aegis field reduces damage taken"));
    }
    auto Disruption = Deploy(Hero, TEXT("disruption_pylon"), F.At(FVector(420, -120, 0)), &Why);
    if (Disruption.Num()) Disruption[0]->Tick(.1f);
    T.Check(Disruption.Num() == 1 && CireSignatureSkills::ModifyOutgoingDamage(M, Ally, 100.f, TEXT("Test")) < 80.f, TEXT("disruption field weakens the monster's damage"));
    Clean(World);
    // Skitter bombs: path to the enemy and explode.
    {
        auto Skitters = Deploy(Hero, TEXT("skitter_swarm"), F.At(FVector(-250, 0, 0)), &Why);
        T.Check(Skitters.Num() == 3, FString::Printf(TEXT("three skitter bombs deployed (%d): %s"), Skitters.Num(), *Why));
        const float Before = M->Health; const FVector Start = Skitters.Num() ? Skitters[0]->GetActorLocation() : FVector::ZeroVector;
        bool bMoved = false;
        for (int32 Step = 0; Step < 60; ++Step)
            for (auto* S : Skitters) if (IsValid(S) && !S->IsActorBeingDestroyed()) { S->Tick(.05f); if (!S->IsActorBeingDestroyed() && FVector::Dist2D(S->GetActorLocation(), Start) > 150) bMoved = true; }
        int32 Alive = 0; for (auto* S : Skitters) if (IsValid(S) && !S->IsActorBeingDestroyed()) ++Alive;
        T.Check(bMoved || Alive == 0, TEXT("skitter bombs run toward the enemy"));
        T.Check(Alive == 0 && M->Health < Before, TEXT("skitter bombs explode on contact"));
    }
    // Monster engineers deploy hostile constructs that attack champions.
    {
        auto Enemy = Deploy(M, TEXT("npc_photon_turret"), F.At(FVector(0, 0, 0)), &Why, 1.f);
        T.Check(Enemy.Num() == 1 && Enemy[0]->bMonsterOwned && Enemy[0]->CanBeDamagedBy(Hero) && !Enemy[0]->CanBeDamagedBy(M), TEXT("monster turret placed, hostile to champions: ") + Why);
        if (Enemy.Num())
        {
            Hero->Health = Hero->MaxHealth; Ally->Health = Ally->MaxHealth;
            Enemy[0]->Tick(.5f);
            T.Check(Hero->Health < Hero->MaxHealth || Ally->Health < Ally->MaxHealth, TEXT("monster turret fires at a champion"));
            Enemy[0]->Destroy();
        }
        // Monster AI walks to and smashes a champion's turret when it has no victim.
        auto Mine = Deploy(Hero, TEXT("photon_turret"), F.At(FVector(250, 0, 0)), &Why);
        M->Victim = nullptr; M->AttackTimer = 0;
        T.Check(Mine.Num() == 1 && MonsterHandleConstructs(M, false), TEXT("monster engages a nearby champion construct"));
        if (Mine.Num()) { const float H = Mine[0]->Health; for (int32 I = 0; I < 3 && Mine[0]->Health == H; ++I) { M->SetActorLocation(Mine[0]->GetActorLocation() - FVector(100, 0, -20)); M->AttackTimer = 0; MonsterHandleConstructs(M, false); }
            T.Check(Mine[0]->Health < H, TEXT("monster smashes the construct in reach")); }
        Clean(World);
    }
    // Arena PvP: a turret shoots the opposing champion.
    {
        Mode->Clock.BeginIntermission(); Mode->Clock.Advance(61);
        F.Ground.Y = Mode->ArenaPosition(0, 2).Y;
        F.Box(F.Ground + FVector(500, 0, -50), FVector(2000, 1000, 50));
        Hero->SetActorLocation(F.At(FVector(-500, 0, 92)), false, nullptr, ETeleportType::TeleportPhysics);
        auto* Rival = F.Hero(1, FVector(300, 0, 0), TEXT("ranger"));
        auto Arena = Deploy(Hero, TEXT("photon_turret"), F.At(FVector(-100, 0, 0)), &Why);
        T.Check(Arena.Num() == 1 && Rival && Arena[0]->CanObserve(Rival) && Arena[0]->CanBeDamagedBy(Rival), TEXT("arena turret placed and visible/destructible to the rival: ") + Why);
        if (Arena.Num() && Rival) { const float Before = Rival->Health; Arena[0]->Tick(.5f); T.Check(Rival->Health < Before, TEXT("arena turret fires on the opposing champion")); }
        Clean(World);
    }
    return T.Finish(TEXT("TECH_CONSTRUCTS"));
}

// ============================================================================================ kits
bool CireSignatureSkills::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    UWorld* World = Mode->GetWorld();
    // ---- Ability DB + roster ----
    static const TCHAR* Profiles[] = {TEXT("gunblade"), TEXT("witch_slayer"), TEXT("huntress"), TEXT("aetheri_artificer"), TEXT("aetheri_warden")};
    for (const FString& Id : AllIds())
    {
        const FCireAbilityDef* D = CireAbilityDB::Find(Id);
        T.Check(D && D->IsImplemented() && !D->Name.IsEmpty() && !D->School.IsEmpty() && D->Types.Num() > 0, Id + TEXT(": implemented Ability DB row"));
        if (!D) continue;
        T.Check(D->IsPassive() == IsPassive(Id) && D->IsUltimate() == IsUltimate(Id), Id + TEXT(": kind agrees with the runtime"));
        T.Check(D->Base.Cooldown >= 0 && (D->IsPassive() || D->Base.ManaCost > 0 || D->Base.EnergyCost > 0), Id + TEXT(": has a cost"));
        FCireHitShape Shape = CireAbilityShapes::Describe(FName(*Id));
        T.Check(D->IsPassive() || Shape.Kind != ECireHitShape::None, Id + TEXT(": has a targeting shape"));
        ECireSchool School; T.Check(CireAbilityShapes::ParseSchool(D->School, School) && Shape.School == School, Id + TEXT(": school runes follow the Ability DB school"));
    }
    for (const TCHAR* P : Profiles)
    {
        const auto* Profile = CireChampionRoster::Find(P);
        const auto* Kit = CireAbilityDB::Kit(P);
        T.Check(Profile && Kit && !Profile->Quote.IsEmpty() && !Profile->Lore.IsEmpty(), FString(P) + TEXT(": roster profile with lore and quote"));
        if (!Profile || !Kit) continue;
        TArray<FString> Signature; for (const auto& A : Profile->Actives) Signature.Add(A.Id); Signature.Add(Profile->Passive.Id); Signature.Add(Profile->Ultimate.Id);
        for (const FString& S : Signature) T.Check(Kit->PurchasableImplemented.Contains(S) && Knows(S), FString(P) + TEXT(" can buy ") + S);
        for (const FString& S : AllIds()) if (!Signature.Contains(S)) T.Check(!Kit->Purchasable.Contains(S), FString(P) + TEXT(" cannot buy another champion's ") + S);
    }
    // ---- casting every active and ultimate ----
    auto* M = F.Monster(0, FVector(300, 0, 0));
    auto* M2 = F.Monster(0, FVector(550, 150, 0));
    auto* M3 = F.Monster(0, FVector(800, -100, 0));
    for (const TCHAR* P : Profiles)
    {
        auto* H = F.Hero(0, FVector(-300, 0, 0), P);
        T.Check(H && H->ChampionProfileId == P, FString(P) + TEXT(" drafts"));
        if (!H) continue;
        T.Check(CireClassTraits::Role(H) == (FString(P) == TEXT("aetheri_warden") ? Cires::SkillDraftRole::Support : Cires::SkillDraftRole::Damage),
            FString(P) + TEXT(" class trait role (DPS Keen Edge / Support Mending Strikes)"));
        const auto* Profile = CireChampionRoster::Find(P);
        TArray<FString> Kit; for (const auto& A : Profile->Actives) Kit.Add(A.Id); Kit.Add(Profile->Ultimate.Id);
        // Overcharge needs a construct of the caster's.
        if (Kit.Contains(TEXT("overcharge"))) Kit.Swap(Kit.IndexOfByKey(TEXT("overcharge")), Kit.Num() - 1);
        for (const FString& Id : Kit)
        {
            for (auto* Mon : {M, M2, M3}) { Mon->Health = Mon->MaxHealth; CireBuffs::ClearAll(Mon); }
            H->SetActorLocation(F.At(FVector(-300, 0, 92)), false, nullptr, ETeleportType::TeleportPhysics);
            Learn(H, Id); H->Target = M; H->bHasCastAim = true;
            const auto* D = CireAbilityDB::Find(Id);
            FVector Aim = M->GetActorLocation() - FVector(0, 0, 92);
            if (D && (D->Targeting == TEXT("aim")) && CireTechConstructs::IsConstructSkill(Id)) Aim = F.At(FVector(100, -250, 0)); // clear ground beside the monsters
            if (Id == TEXT("hunters_stride") || Id == TEXT("sabercat_pounce")) Aim = F.At(FVector(150, 0, 0));
            H->CastAimPoint = Aim;
            const bool bCast = Cast(H, 0, Id);
            H->bHasCastAim = false;
            T.Check(bCast && H->Cooldowns[0] > 0 && H->GlobalCooldown > 0, FString(P) + TEXT(" casts ") + Id + TEXT(" (") + H->Notice + TEXT(")"));
            ACireAreaEffect::ClearForActor(H);
            for (auto* Mon : {M, M2, M3}) { CireBuffs::Remove(Mon, TEXT("banished")); }
        }
        ACireConstruct::ClearForActor(H); ACireSkillshot::ClearForActor(H);
        H->Destroy(); Mode->Heroes.Remove(H);
    }
    for (auto* Mon : {M, M2, M3}) { Mon->Health = Mon->MaxHealth; CireBuffs::ClearAll(Mon); Mon->SlowUntil = 0; }
    // ---- Gunblade: the basic attack switches between pistol and falchion by range ----
    {
        auto* G = F.Hero(0, FVector(-300, 0, 0), TEXT("gunblade"));
        T.Check(G && G->BasicAttackRange() >= 900 && G->IsRangedBasicAttack(), TEXT("gunblade basic reaches mid range as a ranged attack"));
        if (G)
        {
            G->SetActorLocation(M->GetActorLocation() - FVector(200, 0, 0));
            bool bRanged = true; FString Name;
            const float Close = ModifyBasicAttack(G, M, 100.f, bRanged, Name);
            T.Check(IsCloseQuarters(G, M) && !bRanged && Name == TEXT("Falchion slash") && Close > 100.f, TEXT("close target: falchion slash (melee, +15%)"));
            G->SetActorLocation(M->GetActorLocation() - FVector(800, 0, 0));
            bRanged = true; Name.Reset();
            const float Far = ModifyBasicAttack(G, M, 100.f, bRanged, Name);
            T.Check(!IsCloseQuarters(G, M) && bRanged && Name == TEXT("Pistol shot") && FMath::IsNearlyEqual(Far, 100.f), TEXT("mid-range target: pistol shot"));
            G->SetActorLocation(M->GetActorLocation() - FVector(180, 0, 0));
            const float Before = M->Health;
            for (int32 Try = 0; Try < 6 && M->Health == Before; ++Try) CireAttacks::Release(G, M, 50.f);
            T.Check(M->Health < Before, TEXT("close-quarters release resolves as an immediate falchion strike"));
            // Hex mark: +damage taken and bounty gold on kill.
            G->SetActorLocation(F.At(FVector(-300, 0, 92)));
            Learn(G, TEXT("hex_mark")); G->Target = M;
            T.Check(Cast(G, 0, TEXT("hex_mark")) && CireBuffs::IsActive(M, TEXT("bounty_mark")), TEXT("hex mark places the bounty"));
            T.Check(ModifyOutgoingDamage(G, M, 100.f, TEXT("Test")) > 110.f, TEXT("marked target takes more damage"));
            const int32 Gold = G->Gold;
            M->Health = 5; CireCombat::ApplyDamage(G, M, 500.f, TEXT("Bounty test"));
            T.Check(G->Gold >= Gold + 25, TEXT("killing a marked monster pays the bounty"));
            M = F.Monster(0, FVector(300, 0, 0));
            // Silver shot against undead.
            M->NPCState->ArchetypeId = TEXT("hollow_infantry");
            T.Check(ModifyOutgoingDamage(G, M, 100.f, TEXT("Silver Shot")) > 150.f, TEXT("silver shot +60% against the undead"));
            M->NPCState->ArchetypeId = NAME_None;
            G->Destroy(); Mode->Heroes.Remove(G);
        }
    }
    // ---- Huntress: glaives bounce between enemies ----
    {
        auto* Hn = F.Hero(0, FVector(-300, 0, 0), TEXT("huntress"));
        if (Hn)
        {
            for (auto* Mon : {M, M2, M3}) Mon->Health = Mon->MaxHealth;
            Learn(Hn, TEXT("bouncing_glaive")); Hn->Target = M;
            T.Check(Cast(Hn, 0, TEXT("bouncing_glaive")), TEXT("bouncing glaive thrown"));
            T.Check(M->Health < M->MaxHealth && M2->Health < M2->MaxHealth && M3->Health < M3->MaxHealth, TEXT("bouncing glaive chains to every nearby enemy"));
            for (auto* Mon : {M, M2, M3}) Mon->Health = Mon->MaxHealth;
            Hn->Skills = {TEXT("moon_glaive")}; Hn->Cooldowns = {0};
            OnBasicProjectileHit(Hn, M, 100.f, true);
            T.Check(M->Health == M->MaxHealth && M2->Health < M2->MaxHealth && M3->Health < M3->MaxHealth, TEXT("Moon Glaive basic throws bounce to two more enemies"));
            Hn->Destroy(); Mode->Heroes.Remove(Hn);
        }
    }
    // ---- Witch Slayer: purge, banishment ----
    {
        auto* W = F.Hero(0, FVector(-300, 0, 0), TEXT("witch_slayer"));
        if (W && M->NPCState)
        {
            M->NPCState->RallyUntil = World->GetTimeSeconds() + 30; CireBuffs::Apply(M, TEXT("npc_bloodlust"), 30, M);
            Learn(W, TEXT("purge")); W->Target = M;
            T.Check(Cast(W, 0, TEXT("purge")) && M->NPCState->RallyUntil == 0 && !CireBuffs::IsActive(M, TEXT("npc_bloodlust")) && CireCrowdControl::IsSilenced(M),
                TEXT("purge strips buffs and silences"));
            Learn(W, TEXT("banishment")); W->Target = M;
            T.Check(Cast(W, 0, TEXT("banishment")) && CireBuffs::IsActive(M, TEXT("banished")) && CireCombat::ApplyDamage(W, M, 100.f, TEXT("During exile")) == 0,
                TEXT("banished enemy cannot be harmed"));
            CireBuffs::Remove(M, TEXT("banished"));
            W->Destroy(); Mode->Heroes.Remove(W);
        }
    }
    // ---- art: temporary bodies and the mount ----
    {
        const bool bForce = GCireForceTripoChampionArt; GCireForceTripoChampionArt = true;
        auto* Hn = F.Hero(0, FVector(-300, 400, 0), TEXT("huntress"));
        if (Hn && Hn->ChampionArt)
        {
            const bool bApplied = Hn->ChampionArt->DebugApply(*Hn);
            UCireCreatureArt* Creature = Hn->ChampionArt->GetCreature();
            USkeletalMeshComponent* Rider = Creature ? Creature->GetRider() : nullptr;
            T.Check(bApplied && Creature && Creature->GetNativeBody() == Hn->GetMesh() && Cast<UCireMonsterAnimInstance>(Hn->GetMesh()->GetAnimInstance()) != nullptr,
                TEXT("huntress mount body applied with native clips"));
            T.Check(Rider && Rider->GetAttachParent() == Hn->GetMesh() && Rider->GetAttachSocketName() == Creature->GetSeatBone() && !Creature->GetSeatBone().IsNone(),
                TEXT("rider attached to the mount's seat bone"));
            if (Rider)
            {
                const float Feet = static_cast<float>(Hn->GetActorLocation().Z - Hn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
                T.Check(Rider->GetComponentLocation().Z > Feet + 20.f, FString::Printf(TEXT("rider sits above the ground (%.0f cm)"), Rider->GetComponentLocation().Z - Feet));
            }
        }
        auto* G = F.Hero(0, FVector(-300, -400, 0), TEXT("gunblade"));
        if (G && G->ChampionArt)
        {
            const bool bApplied = G->ChampionArt->DebugApply(*G);
            T.Check(bApplied && G->GetMesh()->GetSkeletalMeshAsset() && G->GetMesh()->GetSkeletalMeshAsset()->GetName().Contains(TEXT("BarbedHunterB")),
                TEXT("gunblade drawn with the scarecrow-hatted BarbedHunterB body"));
            UE_LOG(LogCireNewChampionTests, Display, TEXT("CIRE_NEW_CHAMPIONS_ART gunblade_props=%d"), G->ChampionArt->GetCreature() ? G->ChampionArt->GetCreature()->GetPropCount() : -1);
        }
        GCireForceTripoChampionArt = bForce;
    }
    // ---- Aetheri monster race ----
    {
        const FCireRace* Race = CireRaces::FindRace(TEXT("aetheri"));
        T.Check(Race && Race->Units.Num() == 8, TEXT("Aetheri race: six units and two bosses"));
        int32 Deploys = 0;
        if (Race) for (const FName Unit : Race->Units)
        {
            const FCireNPCArchetype* A = CireNPCArchetypes::Find(Unit);
            T.Check(A != nullptr, TEXT("Aetheri unit is an NPC archetype: ") + Unit.ToString());
            if (A) for (const auto& Ab : A->Abilities) if (Ab.Kind == ECireNPCAbilityKind::Deploy) { ++Deploys; T.Check(CireTechConstructs::FindRecipe(Ab.DeployRecipe) != nullptr, Ab.Id.ToString() + TEXT(" deploys a known construct")); }
        }
        T.Check(Deploys >= 5, TEXT("Aetheri engineers and bosses deploy constructs"));
        auto* Engineer = F.Monster(0, FVector(600, 300, 0));
        if (Engineer && CireNPCCombat::ConfigureArchetype(Engineer, TEXT("aetheri_engineer"), 5))
        {
            const FCireNPCArchetype* A = CireNPCArchetypes::Find(TEXT("aetheri_engineer"));
            const FCireNPCAbility* Turret = A ? A->FindAbility(TEXT("aether_deploy_turret")) : nullptr;
            T.Check(Turret && CireRaces::OnAbilityReleased(Engineer, *Turret, Engineer->GetActorLocation() + FVector(-200, 0, -92)) == 1,
                TEXT("an Aetheri engineer's cast warps in a turret"));
        }
        else T.Check(false, TEXT("Aetheri engineer configured"));
    }
    return T.Finish(TEXT("NEW_CHAMPIONS"));
}
#endif
