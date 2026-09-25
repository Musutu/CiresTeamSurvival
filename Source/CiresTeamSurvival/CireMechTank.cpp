#include "CireMechTank.h"
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireChampionArt.h"
#include "CireCombatEvents.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireScalingKits.h"
#include "CireSkillRuntime.h"
#include "CireThreat.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace K = Cires::Kits;

ACireMechTank::ACireMechTank() {}

ACireMechTank* ACireMechTank::SpawnFor(ACireHero* Owner, FVector Point, FString* Why)
{
    auto Fail = [&](const TCHAR* Text) { if (Why) *Why = Text; return static_cast<ACireMechTank*>(nullptr); };
    if (!CireSkillRuntime::Alive(Owner) || !Owner->HasAuthority() || Point.ContainsNaN()) return Fail(TEXT("owner unavailable"));
    UWorld* World = Owner->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase()) return Fail(TEXT("not in combat"));
    // Ground and space check (same approach as ACireSummon::SpawnGroup).
    FCollisionQueryParams Params(SCENE_QUERY_STAT(CireMechPlacement), false);
    FVector P = Point;
    FHitResult Floor;
    if (!World->LineTraceSingleByObjectType(Floor, P + FVector(0, 0, 300), P - FVector(0, 0, 600), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
        return Fail(TEXT("no ground"));
    P.Z = Floor.ImpactPoint.Z + 120.f;
    const FCireAbilityDef* D = CireAbilityDB::Find(TEXT("mechanical_tank"));
    FCireSummonSpec Spec;
    Spec.Count = 1; Spec.ArchetypeVisual = 0; Spec.bCommandable = false;
    Spec.Health = FMath::Min(100000.f, (D ? D->Base.Effect : 800.f) + 12.f * Owner->PrimaryAttribute());
    Spec.Damage = CireKits::Amount(Owner, TEXT("mechanical_tank"), 30.f, .6f) * Mode->Power(Owner->TeamId);
    Spec.DurationSeconds = D && D->Duration > 0 ? D->Duration : 25.f;
    Spec.ManaCost = 0; Spec.EnergyCost = 0; Spec.CooldownSeconds = 0; Spec.CastRange = 3000.f;
    Spec.LeashRange = 2200.f; Spec.MoveSpeed = 440.f; Spec.AttackRange = 220.f;
    CireDeveloperTools::AdjustSummon(World, Spec);
    if (!ACireSummon::ValidateSpec(Spec)) return Fail(TEXT("invalid spec"));
    const FTransform Transform(Owner->GetActorRotation(), P, FVector(1.3f));
    auto* Mech = World->SpawnActorDeferred<ACireMechTank>(StaticClass(), Transform, Owner, Owner, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
    if (!Mech) return Fail(TEXT("spawn failed"));
    Mech->OwnerHero = Owner; Mech->TeamId = Owner->TeamId; Mech->SummonSpec = Spec; Mech->bCommandable = false;
    Mech->OriginPhase = CireSkillRuntime::Phase(World); Mech->ExpiresServerTime = World->GetTimeSeconds() + Spec.DurationSeconds;
    Mech->Draft(Spec.ArchetypeVisual);
    Mech->HeroName = TEXT("Mechanical Tank");
    Mech->MaxHealth = Mech->Health = Spec.Health;
    Mech->Gold = 0; Mech->Skills.Reset(); Mech->Offers.Reset(); Mech->Cooldowns.Reset();
    Mech->Target = nullptr; Mech->CurrentCommand = ECireSummonCommand::Follow; Mech->MoveDestination = P;
    Mech->TauntCooldown = 1.f; Mech->SlamCooldown = 2.f;
    Mech->FinishSpawning(Transform);
    Mech->SetActorScale3D(FVector(1.3f));
    return Mech;
}

AActor* ACireMechTank::ChooseAttackTarget() const
{
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !OwnerHero) return nullptr;
    std::vector<K::MechCandidate> Candidates; TArray<AActor*> Units;
    auto Add = [&](AActor* U, AActor* Victim)
    {
        if (!CireCombat::AreHostile(const_cast<ACireMechTank*>(this), U)) return;
        K::MechCandidate C; C.Id = Units.Num(); C.Distance = FVector::Dist2D(GetActorLocation(), U->GetActorLocation());
        C.AttackingSummoner = Victim == OwnerHero;
        const auto* Ally = ::Cast<ACireHero>(Victim);
        C.AttackingAlly = Ally && Ally != OwnerHero && Ally != this && Ally->TeamId == TeamId;
        Candidates.push_back(C); Units.Add(U);
    };
    for (auto* M : Mode->Monsters) if (IsValid(M)) Add(M, M->Victim);
    for (TActorIterator<ACireHero> It(GetWorld()); It; ++It) if (!It->IsA<ACireSummon>()) Add(*It, It->Target);
    const int32 Index = K::SelectMechAttackTarget(Candidates, SummonSpec.LeashRange);
    return Units.IsValidIndex(Index) ? Units[Index] : nullptr;
}

AActor* ACireMechTank::TryTaunt()
{
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !OwnerHero || bDead) return nullptr;
    std::vector<K::MechCandidate> Candidates; TArray<AActor*> Units;
    const float Now = GetWorld()->GetTimeSeconds();
    for (auto* M : Mode->Monsters)
    {
        if (!IsValid(M) || !CireCombat::AreHostile(this, M)) continue;
        K::MechCandidate C; C.Id = Units.Num(); C.Distance = FVector::Dist2D(GetActorLocation(), M->GetActorLocation());
        C.AttackingSummoner = M->Victim == OwnerHero;
        C.AttackingAlly = M->Victim && M->Victim != OwnerHero && M->Victim != this && M->Victim->TeamId == TeamId;
        C.AlreadyTaunted = M->ForcedVictim.Get() == this && M->ForcedVictimUntil > Now;
        Candidates.push_back(C); Units.Add(M);
    }
    for (TActorIterator<ACireHero> It(GetWorld()); It; ++It)
    {
        ACireHero* E = *It;
        if (E->IsA<ACireSummon>() || !CireCombat::AreHostile(this, E)) continue;
        K::MechCandidate C; C.Id = Units.Num(); C.Distance = FVector::Dist2D(GetActorLocation(), E->GetActorLocation());
        C.AttackingSummoner = E->Target == OwnerHero;
        const auto* Ally = ::Cast<ACireHero>(E->Target);
        C.AttackingAlly = Ally && Ally != OwnerHero && Ally != this && Ally->TeamId == TeamId;
        C.AlreadyTaunted = E->Target == this;
        Candidates.push_back(C); Units.Add(E);
    }
    const int32 Index = K::SelectMechTauntTarget(Candidates, TauntRange);
    if (!Units.IsValidIndex(Index)) return nullptr;
    AActor* Victim = Units[Index];
    if (auto* M = ::Cast<ACireMonster>(Victim)) CireThreat::Taunt(M, this, TauntSeconds);
    else if (auto* E = ::Cast<ACireHero>(Victim)) { if (E->bBot) E->Target = this; CireBuffs::Apply(E, TEXT("taunted"), TauntSeconds, this); }
    TauntUntil = Now + TauntSeconds; // enemy bots in the arena prefer a taunting unit
    CireBuffs::Apply(this, TEXT("war_cry"), TauntSeconds, this);
    CireCombat::PlayCue(this, Victim, TEXT("war_cry"), GetActorLocation(), Victim->GetActorLocation(), ECireSpellCue::Cast);
    LastTaunted = Victim; ForceNetUpdate();
    return Victim;
}

int32 ACireMechTank::Slam()
{
    if (!OwnerHero || bDead) return 0;
    const float Damage = SummonSpec.Damage * 1.5f;
    const bool bLevel15 = K::Level15Unlocked(CireKits::SkillLevel(OwnerHero, TEXT("mechanical_tank")));
    TArray<AActor*> Hits;
    if (auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>())
        for (auto* M : Mode->Monsters) if (IsValid(M) && CireCombat::AreHostile(this, M) && FVector::DistSquared2D(M->GetActorLocation(), GetActorLocation()) <= FMath::Square(SlamRadius)) Hits.Add(M);
    for (TActorIterator<ACireHero> It(GetWorld()); It; ++It)
        if (CireCombat::AreHostile(this, *It) && FVector::DistSquared2D(It->GetActorLocation(), GetActorLocation()) <= FMath::Square(SlamRadius)) Hits.Add(*It);
    for (AActor* U : Hits)
    {
        const float Applied = CireCombat::ApplyDamage(this, U, Damage, TEXT("Mech Slam"));
        if (auto* M = ::Cast<ACireMonster>(U)) CireThreat::AddRaw(M, this, FMath::Max(Applied, Damage) * 3.f); // slams generate threat
        if (bLevel15) CireBuffs::Apply(U, TEXT("mech_weakened"), 4.f, this);
    }
    CireCombat::PlayCue(this, nullptr, TEXT("seismic_reprisal"), GetActorLocation(), GetActorLocation(), ECireSpellCue::Impact, .7f, true);
    ++SlamCount; ForceNetUpdate();
    return Hits.Num();
}

void ACireMechTank::Tick(float Delta)
{
    if (HasAuthority() && !bDead && CireSkillRuntime::Alive(OwnerHero))
    {
        TauntCooldown -= Delta; SlamCooldown -= Delta;
        if (AActor* Pick = ChooseAttackTarget()) { Target = Pick; CurrentCommand = ECireSummonCommand::Attack; }
        if (TauntCooldown <= 0 && TryTaunt()) TauntCooldown = CireKits::InheritedCooldown(this, TauntBaseCooldown);
        if (SlamCooldown <= 0)
        {
            bool bNear = false;
            if (auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>())
                for (auto* M : Mode->Monsters) bNear = bNear || (IsValid(M) && CireCombat::AreHostile(this, M) && FVector::DistSquared2D(M->GetActorLocation(), GetActorLocation()) <= FMath::Square(SlamRadius));
            if (bNear) { Slam(); SlamCooldown = CireKits::InheritedCooldown(this, SlamBaseCooldown); }
        }
    }
    Super::Tick(Delta); // ACireSummon::Tick moves, swings and applies the owner's attack speed
}
