// pets: native checks for the companion system (part of -CireCombatExpansionProbe, Docs/Pets.md).
#include "CirePets.h"
#include "CireAbilityDB.h"
#include "CireChampionArt.h"
#include "CireChampionRoster.h"
#include "CireClassTraits.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCreatureArt.h"
#include "CireGame.h"
#include "CireKeybindings.h"
#include "CireSignatureSkills.h"
#include "CireSkillshot.h"
#include "CireAreaEffects.h"
#include "CireThreat.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if !UE_BUILD_SHIPPING
DEFINE_LOG_CATEGORY_STATIC(LogCirePetTests, Log, All);
namespace
{
struct FChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCirePetTests, Error, TEXT("CIRE_PETS_CHECK_FAIL %s"), *Message); }
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
        auto* Actor = Keep(Mode->GetWorld()->SpawnActor<AActor>());
        auto* Body = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body);
        Body->SetBoxExtent(FVector(3200, 1400, 50)); Body->SetCollisionObjectType(ECC_WorldStatic);
        Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Body->SetCollisionResponseToAllChannels(ECR_Block);
        Body->RegisterComponent(); Actor->SetActorLocation(Ground + FVector(500, 0, -50));
    }
    ~FFixture()
    {
        for (int32 I = Actors.Num() - 1; I >= 0; --I)
            if (IsValid(Actors[I])) { ACireSummon::ClearForActor(Actors[I]); ACireAreaEffect::ClearForActor(Actors[I]); Actors[I]->Destroy(); }
        for (TActorIterator<ACirePet> It(Mode->GetWorld()); It; ++It) It->Destroy();
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters;
    }
    template<class T> T* Keep(T* Actor) { if (Actor) { Actors.Add(Actor); Actor->SetActorTickEnabled(false); } return Actor; }
    ACireHero* Hero(int32 Team, FVector Offset, const TCHAR* Profile)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* H = Keep(Mode->GetWorld()->SpawnActor<ACireHero>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (!H) return nullptr;
        H->TeamId = Team;
        if (!H->DraftProfile(Profile)) H->Draft(0);
        H->Health = H->MaxHealth = 2000; H->Mana = H->MaxMana = 5000; H->Energy = 100; H->CriticalChance = 0;
        H->Offers.Reset(); Mode->Heroes.Add(H);
        return H;
    }
    ACireMonster* Monster(int32 Lane, FVector Offset, float Health = 50000)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = Keep(Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (M) { M->Lane = Lane; M->Health = M->MaxHealth = Health; Mode->Monsters.Add(M); }
        return M;
    }
    FVector At(FVector Offset) const { return Ground + Offset; }
};
void Place(AActor* A, FVector At) { A->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics); }
void Step(ACirePet* Pet, float Dt = .1f) { Pet->ConsumeMovementInputVector(); Pet->BasicTimer = 0; Pet->Tick(Dt); }
FVector Wish(ACirePet* Pet) { return Pet->GetPendingMovementInputVector().GetSafeNormal2D(); }
void Learn(ACireHero* H, const FString& Id)
{
    H->Skills.Reset(); H->Cooldowns.Reset(); H->Skills.Add(Id); H->Cooldowns.Add(0);
    H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100; H->Notice.Reset();
}
void Reset(ACireMonster* M) { M->Threat.Reset(); M->Victim = nullptr; M->ForcedVictim.Reset(); M->ForcedVictimUntil = 0; M->bEngaged = false; M->SlowUntil = 0; M->Health = M->MaxHealth; }
}

bool CirePets::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    UWorld* World = Mode->GetWorld();
    // ---- data ----
    FString Error;
    T.Check(Reload(&Error), TEXT("Pets.json loads: ") + Error);
    const FCirePetDef* Cat = Find(TEXT("sabercat"));
    T.Check(Cat && Cat->DisplayName == TEXT("Ashfang") && Cat->Abilities.Num() == 3, TEXT("sabercat Ashfang with pounce, maul and roar"));
    if (!Cat) return false;
    for (const auto& A : Cat->Abilities)
    {
        const FCireAbilityDef* Row = CireAbilityDB::Find(A.Id);
        T.Check(Row && Row->IsPet() && Row->Status == TEXT("implemented"), A.Id + TEXT(" is an implemented pet-category Ability DB row"));
        T.Check(CireSignatureSkills::Handles(A.Id) && IsPetSkill(A.Id), A.Id + TEXT(" is castable by the owner and commands the pet"));
    }
    T.Check(Cat->Abilities.IsValidIndex(Cat->SpecialIndex()) && Cat->Abilities[Cat->SpecialIndex()].Id == TEXT("sabercat_roar"), TEXT("the roar is the special command"));
    for (uint8 C = 0; C < static_cast<uint8>(ECirePetCommand::Count); ++C)
        T.Check(CireKeybindings::Find(CommandAction(static_cast<ECirePetCommand>(C))) != nullptr, TEXT("keybinding action for ") + CommandAction(static_cast<ECirePetCommand>(C)).ToString());

    // ---- the Huntress fights on foot; her kit commands the pet ----
    {
        const FCireChampionProfile* P = CireChampionRoster::Find(TEXT("huntress"));
        TArray<FString> Kit; if (P) for (const auto& A : P->Actives) Kit.Add(A.Id);
        T.Check(P && Kit.Contains(TEXT("sabercat_pounce")) && Kit.Contains(TEXT("sabercat_maul")) && !Kit.Contains(TEXT("sabercat_rake")) && !P->ClassType.Contains(TEXT("Mounted")),
            TEXT("huntress roster: on-foot class, pounce and maul are companion commands"));
        const FCireChampionKit* K = CireAbilityDB::Kit(TEXT("huntress"));
        T.Check(K && K->Signature.Contains(TEXT("sabercat_roar")) && K->Purchasable.Contains(TEXT("sabercat_roar")), TEXT("the roar is in her Skill Shop identity kit"));
        FString Bindings; FFileHelper::LoadFileToString(Bindings, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/ChampionArtBindings.json")));
        const int32 Row = Bindings.Find(TEXT("\"profileId\": \"huntress\""));
        const int32 Next = Row == INDEX_NONE ? INDEX_NONE : Bindings.Find(TEXT("\"profileId\""), ESearchCase::CaseSensitive, ESearchDir::FromStart, Row + 10);
        const FString HuntressRow = Row == INDEX_NONE ? FString() : Bindings.Mid(Row, Next == INDEX_NONE ? 4000 : Next - Row);
        T.Check(!HuntressRow.IsEmpty() && !HuntressRow.Contains(TEXT("mounted")) && !HuntressRow.Contains(TEXT("\"rider\"")) && !HuntressRow.Contains(TEXT("Wolf")),
            TEXT("huntress art binding has no mount, rider or wolf"));
        T.Check(!UCireChampionArt::IsCreatureProfile(TEXT("huntress")), TEXT("huntress is drawn as a humanoid, not a creature/mount"));
        const bool bForce = GCireForceTripoChampionArt; GCireForceTripoChampionArt = true;
        if (auto* Hn = F.Hero(0, FVector(-300, 600, 0), TEXT("huntress")); Hn && Hn->ChampionArt)
        {
            const bool bApplied = Hn->ChampionArt->DebugApply(*Hn);
            const USkeletalMesh* Body = Hn->GetMesh()->GetSkeletalMeshAsset();
            T.Check(bApplied && Body && Body->GetName().Contains(TEXT("Huntress")) && !Hn->ChampionArt->GetCreature(),
                TEXT("huntress wears her Tripo body with normal locomotion: ") + (Body ? Body->GetName() : FString(TEXT("none"))));
            Hn->Destroy(); Mode->Heroes.Remove(Hn);
        }
        GCireForceTripoChampionArt = bForce;
    }

    // ---- summon, identity, scaling ----
    ACireHero* H = F.Hero(0, FVector(-300, 0, 0), TEXT("huntress"));
    T.Check(H && ForOwner(H) == Cat, TEXT("huntress owns the sabercat"));
    if (!H) return false;
    H->Level = 1; H->Agility = 20;
    TickOwner(H, .1f);
    ACirePet* Pet = PetOf(H);
    T.Check(Pet && Pet->IsA<ACireSummon>() && Pet->OwnerHero == H && Pet->TeamId == 0 && Pet->PetId == TEXT("sabercat") && Pet->HeroName == TEXT("Ashfang") &&
        Pet->ChampionProfileId == TEXT("pet:sabercat") && Pet->bDrafted && !Pet->bDead, TEXT("the owner's tick summons Ashfang beside her"));
    if (!Pet) return T.bPassed;
    Pet->SetActorTickEnabled(false); Pet->CriticalChance = 0;
    T.Check(CireClassTraits::Role(Pet) == Cires::SkillDraftRole::Any, TEXT("a pet is not a roster champion (no class trait)"));
    TickOwner(H, .1f);
    int32 Count = 0; for (TActorIterator<ACirePet> It(World); It; ++It) if (It->OwnerHero == H && !It->IsActorBeingDestroyed()) ++Count;
    T.Check(Count == 1, TEXT("one persistent companion per owner"));
    const float Health1 = Pet->MaxHealth, Damage1 = Pet->CurrentDamage();
    T.Check(FMath::IsNearlyEqual(Health1, Cat->Health + Cat->OwnerHealthShare * H->MaxHealth, 1.f), FString::Printf(TEXT("level-1 health %.0f follows the owner"), Health1));
    H->Level = 10; H->Agility = 60; H->MaxHealth = 3000; Pet->Rescale(false);
    T.Check(Pet->Level == 10 && Pet->MaxHealth > Health1 + 800 && Pet->CurrentDamage() > Damage1 + 40, FString::Printf(TEXT("scales with owner level and stats (hp %.0f dmg %.0f)"), Pet->MaxHealth, Pet->CurrentDamage()));
    Pet->Health = Pet->MaxHealth;

    // ---- follow, teleport, stay ----
    Place(Pet, F.At(FVector(-300, 200, Cat->CapsuleHalfHeight + 4)));
    Place(H, F.At(FVector(600, 0, 92)));
    Step(Pet);
    T.Check(Pet->Order == ECirePetOrder::Follow && FVector::DotProduct(Wish(Pet), (H->GetActorLocation() - Pet->GetActorLocation()).GetSafeNormal2D()) > .8f, TEXT("follows its owner"));
    Place(H, F.At(FVector(-300, 0, 92)) + FVector(4200, 0, 0));
    Step(Pet);
    T.Check(FVector::Dist2D(Pet->GetActorLocation(), H->GetActorLocation()) < 600, TEXT("teleports back when left far behind"));
    Place(H, F.At(FVector(-300, 0, 92)));
    Place(Pet, F.At(FVector(-300, 250, Cat->CapsuleHalfHeight + 4)));
    T.Check(Command(H, ECirePetCommand::Stay, nullptr) && Pet->Order == ECirePetOrder::Stay, TEXT("stay command"));
    Place(H, F.At(FVector(900, 0, 92)));
    Step(Pet);
    T.Check(Wish(Pet).IsNearlyZero() && Pet->Order == ECirePetOrder::Stay, TEXT("a staying pet does not follow"));
    T.Check(Command(H, ECirePetCommand::Follow, nullptr) && Pet->Order == ECirePetOrder::Follow, TEXT("follow command"));
    Place(H, F.At(FVector(-300, 0, 92)));

    // ---- attack command, basic attacks, threat ----
    ACireMonster* M = F.Monster(0, FVector(200, 0, 0));
    ACireMonster* M2 = F.Monster(0, FVector(200, 500, 0));
    if (!M || !M2) return false;
    H->Target = nullptr;
    T.Check(!Command(H, ECirePetCommand::Attack, nullptr) && !H->Notice.IsEmpty(), TEXT("attack needs a hostile target"));
    T.Check(Command(H, ECirePetCommand::Attack, M) && Pet->Order == ECirePetOrder::Attack && Pet->Target == M, TEXT("attack my target"));
    Place(Pet, M->GetActorLocation() - FVector(150, 0, 92 - Cat->CapsuleHalfHeight - 4));
    const uint32 Serial = Pet->AttackSerial;
    for (float& Ready : Pet->AbilityReadyAt) Ready = World->GetTimeSeconds() + 100.f; // basic attacks only (no autocast)
    Step(Pet);
    T.Check(Pet->AttackSerial != Serial && Pet->PendingAttackTarget.Get() == M && FMath::IsNearlyEqual(Pet->PendingAttackDamage, Pet->CurrentDamage()),
        TEXT("the pet attacks the ordered target with its scaled damage"));
    Pet->PendingAttackTarget.Reset();
    for (float& Ready : Pet->AbilityReadyAt) Ready = 0;
    Reset(M);
    CireCombat::ApplyStrike(Pet, M, 100, TEXT("Pet test strike"));
    T.Check(FMath::IsNearlyEqual(M->Threat.FindRef(Pet), 100.f * Cat->ThreatMultiplier, 1.f) && M->Victim == Pet, TEXT("pet damage threat uses its threat multiplier"));
    // Threat share: the owner's damage threat partly lands on the engaged pet, which holds aggro.
    const float PetBefore = M->Threat.FindRef(Pet);
    CireThreat::Damage(M, H, 300);
    const float OwnerThreat = 300 * H->DamageThreatMultiplier();
    T.Check(FMath::IsNearlyEqual(M->Threat.FindRef(H), OwnerThreat * (1 - Rules().OwnerThreatShare), 1.f) &&
        FMath::IsNearlyEqual(M->Threat.FindRef(Pet), PetBefore + OwnerThreat * Rules().OwnerThreatShare, 1.f), TEXT("owner threat share moves to the pet"));
    T.Check(M->Victim == Pet, TEXT("the pet holds aggro against its owner's damage"));
    Reset(M);
    CireThreat::Damage(M, H, 300);
    T.Check(FMath::IsNearlyEqual(M->Threat.FindRef(H), OwnerThreat, 1.f) && !M->Threat.Contains(Pet), TEXT("no share while the pet is not engaged"));
    Reset(M);
    Pet->SetStance(ECirePetStance::Passive);
    CireThreat::Engage(M, Pet); CireThreat::Damage(M, H, 300);
    T.Check(FMath::IsNearlyEqual(M->Threat.FindRef(H), OwnerThreat, 1.f), TEXT("a passive pet takes no share"));
    Reset(M);

    // ---- stances ----
    Command(H, ECirePetCommand::Follow, nullptr);
    Place(Pet, F.At(FVector(-100, 0, Cat->CapsuleHalfHeight + 4)));
    H->Target = M; H->bAutoAttack = true;
    Command(H, ECirePetCommand::StancePassive, nullptr); Step(Pet);
    T.Check(Pet->Stance == ECirePetStance::Passive && H->PetStance == static_cast<uint8>(ECirePetStance::Passive) && !Pet->Target, TEXT("passive: ignores the owner's fight"));
    Command(H, ECirePetCommand::StanceDefensive, nullptr); Step(Pet);
    T.Check(Pet->Stance == ECirePetStance::Defensive && Pet->Target == M, TEXT("defensive: assists the owner's target"));
    H->bAutoAttack = false; H->Target = nullptr; Pet->Order_Follow();
    M2->Victim = H; Step(Pet);
    T.Check(Pet->Target == M2, TEXT("defensive: defends its owner from an attacker"));
    M2->Victim = nullptr; Pet->Order_Follow(); Step(Pet);
    T.Check(!Pet->Target, TEXT("defensive: leaves idle enemies alone"));
    Place(M2, Pet->GetActorLocation() + FVector(0, 400, 92 - Cat->CapsuleHalfHeight - 4));
    Command(H, ECirePetCommand::StanceAggressive, nullptr); Pet->Order_Follow(); Step(Pet);
    T.Check(Pet->Stance == ECirePetStance::Aggressive && (Pet->Target == M2 || Pet->Target == M), TEXT("aggressive: engages enemies near it"));
    M->bNeutral = M2->bNeutral = true; Pet->Order_Follow(); Step(Pet);
    T.Check(!Pet->Target, TEXT("aggressive: never opens on a neutral pack"));
    M->bNeutral = M2->bNeutral = false; Place(M2, F.At(FVector(200, 500, 92)));
    Command(H, ECirePetCommand::StanceDefensive, nullptr); Pet->Order_Follow();

    // ---- special (roar) and owner skills (maul, pounce) ----
    Reset(M); Reset(M2);
    Place(Pet, M->GetActorLocation() - FVector(150, 0, 92 - Cat->CapsuleHalfHeight - 4));
    const int32 Roar = Cat->SpecialIndex();
    T.Check(Command(H, ECirePetCommand::Special, nullptr) && M->SlowUntil > World->GetTimeSeconds() && M->Victim == Pet &&
        Pet->AbilityReadyAt[Roar] > World->GetTimeSeconds(), TEXT("special: the roar slows and taunts nearby enemies onto the pet"));
    T.Check(!Command(H, ECirePetCommand::Special, nullptr) && H->Notice.Contains(TEXT("not ready")), TEXT("special respects its cooldown"));
    Reset(M);
    const int32 Maul = Cat->AbilityIndex(TEXT("sabercat_maul"));
    Learn(H, TEXT("sabercat_maul")); H->Target = M;
    H->Cast(0);
    T.Check(H->Cooldowns[0] > 0 && Pet->DebugPendingAbility() == Maul && Pet->Order == ECirePetOrder::Attack, TEXT("the huntress casts Maul: queued on her companion (") + H->Notice + TEXT(")"));
    Step(Pet);
    T.Check(M->Health < M->MaxHealth && Pet->AbilityReadyAt[Maul] > World->GetTimeSeconds() && Pet->DebugPendingAbility() == INDEX_NONE &&
        M->Threat.FindRef(Pet) > (M->MaxHealth - M->Health) * Cat->ThreatMultiplier * 1.4f,
        FString::Printf(TEXT("maul lands with bonus threat (damage %.0f threat %.0f pending %d ready %.1f now %.1f)"), M->MaxHealth - M->Health, M->Threat.FindRef(Pet),
            Pet->DebugPendingAbility(), Pet->AbilityReadyAt[Maul], World->GetTimeSeconds()));
    Reset(M);
    const int32 Pounce = Cat->AbilityIndex(TEXT("sabercat_pounce"));
    Learn(H, TEXT("sabercat_pounce")); H->Target = M;
    Place(Pet, M->GetActorLocation() - FVector(550, 0, 92 - Cat->CapsuleHalfHeight - 4));
    H->Cast(0);
    Step(Pet);
    T.Check(H->Cooldowns[0] > 0 && Pet->IsLeaping() && Pet->AbilityReadyAt[Pounce] > World->GetTimeSeconds(), TEXT("pounce: the pet leaps at the target"));
    Place(Pet, M->GetActorLocation() - FVector(120, 0, 92 - Cat->CapsuleHalfHeight - 4));
    Pet->DebugLand();
    T.Check(!Pet->IsLeaping() && M->Health < M->MaxHealth && M->SlowUntil > World->GetTimeSeconds(), TEXT("pounce landing mauls and slows"));

    // ---- death, revive, resummon ----
    Reset(M);
    CireCombat::ApplyStrike(Pet, M, 10, TEXT("Pet engage"));
    Pet->TakeDamage(Pet->MaxHealth * 10, FDamageEvent(), nullptr, M);
    const float Now = World->GetTimeSeconds();
    T.Check(Pet->bDead && Pet->IsCorpse() && PetOf(H) == Pet && !M->Threat.Contains(Pet) && M->Victim != Pet &&
        FMath::IsNearlyEqual(H->PetResummonAt, Now + Rules().ResummonCooldown, .1f), TEXT("death leaves a corpse, drops threat and starts the return timer"));
    Step(Pet); TickOwner(H, .1f);
    T.Check(Pet->bDead && PetOf(H) == Pet, TEXT("a corpse is neither revived at base nor resummoned early"));
    T.Check(!Command(H, ECirePetCommand::Attack, M), TEXT("a dead pet takes no orders"));
    Place(H, Pet->GetActorLocation() + FVector(Rules().ReviveRange + 400, 0, 30));
    T.Check(!Command(H, ECirePetCommand::Revive, nullptr) && H->Notice.Contains(TEXT("closer")), TEXT("revive needs the owner nearby"));
    Place(H, F.At(FVector(-300, 0, 92)));
    T.Check(Command(H, ECirePetCommand::Revive, nullptr) && !Pet->bDead && FMath::IsNearlyEqual(Pet->Health, Pet->MaxHealth * Rules().ReviveHealthFraction, 1.f) &&
        H->PetResummonAt == 0 && H->PetReviveReadyAt > Now, TEXT("revive: the corpse stands up at half health"));
    Pet->TakeDamage(Pet->MaxHealth * 10, FDamageEvent(), nullptr, M);
    T.Check(!Command(H, ECirePetCommand::Revive, nullptr) && Pet->bDead, TEXT("revive respects its cooldown"));
    H->PetResummonAt = Now - 1;
    TickOwner(H, .1f);
    ACirePet* Fresh = PetOf(H);
    T.Check(Fresh && Fresh != Pet && !Fresh->bDead && FMath::IsNearlyEqual(Fresh->Health, Fresh->MaxHealth) && (!IsValid(Pet) || Pet->IsActorBeingDestroyed()),
        TEXT("after the return timer the pet is resummoned at full health"));
    if (Fresh) Fresh->SetActorTickEnabled(false);
    T.Check(Fresh && Fresh->Stance == ECirePetStance::Defensive, TEXT("the resummoned pet keeps the owner's stance"));

    // ---- the companion follows its owner's life, team and entitlement ----
    if (Fresh)
    {
        H->bDead = true; Step(Fresh);
        T.Check(Fresh->IsActorBeingDestroyed(), TEXT("the pet leaves when its owner dies"));
        H->bDead = false; TickOwner(H, .1f);
        T.Check(PetOf(H) != nullptr, TEXT("and returns with the revived owner"));
    }
    // Pet talent hook: any champion can be granted a companion.
    ACireHero* G = F.Hero(0, FVector(-300, -500, 0), TEXT("gunblade"));
    if (G)
    {
        T.Check(!ForOwner(G), TEXT("a gunblade has no companion of his own"));
        G->PetGrant = TEXT("sabercat"); TickOwner(G, .1f);
        T.Check(PetOf(G) && PetOf(G)->PetId == TEXT("sabercat"), TEXT("a granted companion (pet talent) is summoned for any champion"));
        G->PetGrant = NAME_None; if (ACirePet* Granted = PetOf(G)) { Step(Granted); T.Check(Granted->IsActorBeingDestroyed(), TEXT("revoking the grant dismisses it")); }
    }
    // ---- Eric's owner-power rules: targetable, primary-stat damage, owner CDR and attack speed ----
    if (ACirePet* P2 = PetOf(H))
    {
        P2->SetActorTickEnabled(false); P2->CriticalChance = 0;
        ACireMonster* T3 = F.Monster(0, FVector(1200, -600, 0));
        if (T3)
        {
            CireThreat::Engage(T3, P2);
            T.Check(CireThreat::Select(T3) == P2 && T3->Victim == P2, TEXT("targetable: a monster takes the pet as its victim"));
            T.Check(CireCombat::AreHostile(T3, P2) && CireCombat::ApplyDamage(T3, P2, 10, TEXT("Pet target test")) > 0, TEXT("targetable: monsters can hit the pet"));
            CireThreat::Clear(T3);
        }
        // Damage follows the owner's primary stat, not the others.
        H->Level = 5; const int32 SavedAgi = H->Agility, SavedStr = H->Strength;
        P2->Rescale(false); const float Base = P2->CurrentDamage();
        H->Strength += 50; P2->Rescale(false);
        T.Check(FMath::IsNearlyEqual(P2->CurrentDamage(), Base, .01f), TEXT("a non-primary stat does not scale the pet (AGI huntress, +STR)"));
        H->Agility += 50; P2->Rescale(false);
        T.Check(P2->CurrentDamage() > Base + 50 * Cat->OwnerPrimaryScale * .9f && FMath::IsNearlyEqual(OwnerPrimaryScale(H), static_cast<float>(H->Agility)), TEXT("the owner's primary stat scales pet damage"));
        H->Agility = SavedAgi; H->Strength = SavedStr; P2->Rescale(false);
        if (ACireHero* Caster = F.Hero(0, FVector(-300, -900, 0), TEXT("aetheri_warden")))
        {
            T.Check(OwnerPrimaryScale(Caster) == Caster->Intelligence, TEXT("an INT champion's pets scale with INT"));
            Caster->Destroy(); Mode->Heroes.Remove(Caster);
        }
        // Cooldown reduction and attack speed come from the owner.
        const float Plain = OwnerCooldown(H, 16.f);
        H->CDR = .3f;
        T.Check(OwnerCooldown(H, 16.f) < Plain - 1.f, FString::Printf(TEXT("owner CDR shortens pet cooldowns (%.1f -> %.1f)"), Plain, OwnerCooldown(H, 16.f)));
        ACireMonster* T4 = F.Monster(0, FVector(1200, 600, 0));
        if (T4)
        {
            Place(P2, T4->GetActorLocation() - FVector(150, 0, 92 - Cat->CapsuleHalfHeight - 4));
            Command(H, ECirePetCommand::StanceDefensive, nullptr);
            P2->AbilityReadyAt.Init(0, Cat->Abilities.Num());
            T.Check(Command(H, ECirePetCommand::Special, nullptr), TEXT("roar for the CDR check"));
            const int32 Roar2 = Cat->SpecialIndex();
            const float Expected = OwnerCooldown(H, CireAbilityDB::Find(Cat->Abilities[Roar2].Id)->Base.Cooldown);
            T.Check(FMath::IsNearlyEqual(P2->AbilityReadyAt[Roar2] - World->GetTimeSeconds(), Expected, .05f), FString::Printf(TEXT("pet ability cooldown uses the owner's CDR (%.2f)"), Expected));
            CireThreat::Clear(T4);
            // Attack speed: the swing timer is the pet's attack time divided by the owner's attack speed.
            for (float& Ready : P2->AbilityReadyAt) Ready = World->GetTimeSeconds() + 100.f;
            H->Agility = 100; Command(H, ECirePetCommand::Attack, T4);
            Step(P2);
            T.Check(OwnerAttackSpeed(H) >= 1.99f && FMath::IsNearlyEqual(P2->BasicTimer, Cat->AttackSeconds / OwnerAttackSpeed(H), .01f),
                FString::Printf(TEXT("owner attack speed drives the pet's swings (timer %.2f, speed %.2f)"), P2->BasicTimer, OwnerAttackSpeed(H)));
            H->Agility = SavedAgi; P2->PendingAttackTarget.Reset();
        }
        H->CDR = 0;
    }
    // ---- replication and realm privacy ----
    {
        Pet = PetOf(H);
        auto Replicates = [&](UClass* Class, const TCHAR* Name)
        {
            const FProperty* P = FindFProperty<FProperty>(Class, Name);
            return P && P->HasAnyPropertyFlags(CPF_Net);
        };
        for (const TCHAR* Name : {TEXT("PetId"), TEXT("Stance"), TEXT("Order"), TEXT("AbilityReadyAt"), TEXT("DiedAt"), TEXT("AbilitySerial")})
            T.Check(Replicates(ACirePet::StaticClass(), Name), FString(TEXT("pet replicates ")) + Name);
        for (const TCHAR* Name : {TEXT("PetResummonAt"), TEXT("PetReviveReadyAt"), TEXT("PetStance"), TEXT("PetGrant")})
            T.Check(Replicates(ACireHero::StaticClass(), Name), FString(TEXT("owner replicates ")) + Name);
        T.Check(Pet && Pet->GetIsReplicated(), TEXT("the pet actor replicates"));
    }
    UE_LOG(LogCirePetTests, Display, TEXT("CIRE_PETS_%s checks=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count);
    return T.bPassed;
}
#endif
