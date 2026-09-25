// balance: bot companion commands (see CireBotPets.h).
#include "CireBotPets.h"
#include "CireAbilityDB.h"
#include "CireChampionProfiles.h"
#include "CireCombatEvents.h"
#include "CireGame.h"
#include "CirePets.h"
#include "CireSummon.h"
#include "Engine/World.h"

namespace
{
TMap<TWeakObjectPtr<const ACireHero>, float> NextThink;
}

ACireMonster* CireBotPets::RoarTarget(const ACireHero* Bot, float Radius)
{
    const ACirePet* Pet = CirePets::PetOf(Bot);
    const auto* Mode = Bot && Bot->GetWorld() ? Bot->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
    if (!Pet || Pet->bDead || !Mode) return nullptr;
    ACireMonster* Best = nullptr; float BestUrgency = 0.f;
    for (ACireMonster* M : Mode->Monsters)
    {
        if (!IsValid(M) || M->Health <= 0 || M->bNeutral || !IsValid(M->Victim)) continue;
        const ACireHero* Victim = M->Victim;
        if (Victim == Pet || Victim->TeamId != Bot->TeamId || Victim->bDead || Victim->IsA<ACireSummon>()) continue;
        if (FVector::DistSquared2D(M->GetActorLocation(), Pet->GetActorLocation()) > FMath::Square(Radius)) continue;
        const float Fraction = Victim->Health / FMath::Max(1.f, Victim->MaxHealth);
        const bool bTank = CireChampionProfiles::DraftRole(Victim) == Cires::SkillDraftRole::Tank;
        if (bTank && Fraction >= .5f) continue; // the tank is doing its job
        const float Urgency = (1.f - Fraction) + (bTank ? 0.f : .5f);
        if (Urgency > BestUrgency) { BestUrgency = Urgency; Best = M; }
    }
    return Best;
}

void CireBotPets::Think(ACireHero* Bot, float DeltaSeconds)
{
    if (!IsValid(Bot) || !Bot->HasAuthority() || !Bot->bBot || Bot->bDead || Bot->IsA<ACireSummon>()) return;
    const FCirePetDef* Def = CirePets::ForOwner(Bot);
    const auto* Mode = Bot->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Def || !Mode || !Mode->IsCombatPhase()) return;
    const float Now = Bot->GetWorld()->GetTimeSeconds();
    float& Next = NextThink.FindOrAdd(Bot, 0.f);
    if (Now < Next) return;
    Next = Now + .5f;
    if (NextThink.Num() > 64) for (auto It = NextThink.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    ACirePet* Pet = CirePets::PetOf(Bot);
    const FString Notice = Bot->Notice; // bot orders must not spam the owner's notice line
    if (Pet && Pet->bDead)
    {
        if (Now >= Bot->PetReviveReadyAt && FVector::Dist2D(Pet->GetActorLocation(), Bot->GetActorLocation()) <= CirePets::Rules().ReviveRange)
            CirePets::Command(Bot, ECirePetCommand::Revive, nullptr);
        Bot->Notice = Notice; return;
    }
    if (!Pet) { Bot->Notice = Notice; return; }
    // Dread Roar (the special) peels a monster off a threatened ally.
    const int32 Special = Def->SpecialIndex();
    if (Special != INDEX_NONE && (!Pet->AbilityReadyAt.IsValidIndex(Special) || Pet->AbilityReadyAt[Special] <= Pet->ServerNow()))
    {
        const FCireAbilityDef* Row = CireAbilityDB::Find(Def->Abilities[Special].Id);
        const float Radius = Row && Row->Radius > 0 ? Row->Radius : 450.f;
        if (ACireMonster* Peel = RoarTarget(Bot, Radius)) CirePets::Command(Bot, ECirePetCommand::Special, Peel);
    }
    // Attack: the pet fights what its owner fights.
    AActor* Wanted = Bot->Target;
    if (IsValid(Wanted) && CireCombat::AreHostile(Bot, Wanted) && CireCombat::IsAlive(Wanted) && Pet->GetFightTarget() != Wanted)
        CirePets::Command(Bot, ECirePetCommand::Attack, Wanted);
    Bot->Notice = Notice;
}
