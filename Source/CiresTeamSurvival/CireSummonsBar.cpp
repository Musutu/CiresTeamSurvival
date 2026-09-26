// fix/summons: summons-bar data (see CireSummonsBar.h).
#include "CireSummonsBar.h"
#include "CireActorIterator.h" // town-perf: fast actor iteration in editor-binary -game
#include "CireAbilityDB.h"
#include "CireConstruct.h"
#include "CireGame.h"
#include "CirePets.h"
#include "CireSummon.h"
#include "EngineUtils.h"

namespace
{
FString StateOf(const ACireSummon* S)
{
    switch (S->CurrentCommand)
    {
    case ECireSummonCommand::Move: return TEXT("MOVING");
    case ECireSummonCommand::Hold: return IsValid(S->Target) ? TEXT("ATTACKING") : TEXT("HOLDING");
    default: return IsValid(S->Target) ? TEXT("ATTACKING") : S->bCommandable ? TEXT("FOLLOWING") : TEXT("GUARDING");
    }
}
FString StateOf(const ACireConstruct* C)
{
    switch (C->ConstructSpec.Kind)
    {
    case ECireConstructKind::Turret: return TEXT("FIRING");
    case ECireConstructKind::Trap: return TEXT("ARMED");
    case ECireConstructKind::Pylon: return TEXT("FIELD");
    case ECireConstructKind::Skitter: return TEXT("HUNTING");
    default: return TEXT("STANDING");
    }
}
bool OwnedBy(const ACireConstruct* C, const ACireHero* Owner)
{
    if (!IsValid(C) || C->IsActorBeingDestroyed() || C->bMonsterOwned || C->Health <= 0) return false;
    return C->GetSourceActor() == Owner || C->GetOwner() == Owner; // the owner pointer is what replicates
}
}

FString CireSummonsBar::TimerText(float Seconds)
{
    const int32 S = FMath::Max(0, FMath::CeilToInt(Seconds));
    return S >= 60 ? FString::Printf(TEXT("%d:%02d"), S / 60, S % 60) : FString::Printf(TEXT("%ds"), S);
}

FString CireSummonsBar::IconFor(const ACireSummon* S)
{
    if (!S) return FString();
    if (!S->SourceSkill.IsNone()) return S->SourceSkill.ToString();
    return S->bCommandable ? TEXT("oathbound_guardian") : TEXT("spectral_pack");
}

FString CireSummonsBar::IconFor(const ACireConstruct* C)
{
    if (!C) return FString();
    if (C->IsTech() && !C->ConstructSpec.Recipe.IsNone()) return C->ConstructSpec.Recipe.ToString();
    const FString Name = NameFor(C);
    if (const FCireAbilityDef* D = CireAbilityDB::FindByName(Name)) return D->Id;
    if (Name.Contains(TEXT("Pavise"))) return TEXT("pavise");
    return C->IsProtection() ? TEXT("protection_dome") : TEXT("summoned_wall");
}

FString CireSummonsBar::NameFor(const ACireConstruct* C)
{
    if (!C) return FString();
    FString Name = C->GetDisplayName();
    Name.RemoveFromStart(TEXT("Construct: "));
    if (C->IsTech() && Name == C->ConstructSpec.Recipe.ToString())
        if (const FCireAbilityDef* D = CireAbilityDB::Find(Name)) Name = D->Name;
    return Name.IsEmpty() ? FString(TEXT("Construct")) : Name;
}

bool CireSummonsBar::HasCommandable(const ACireHero* Owner)
{
    if (!IsValid(Owner)) return false;
    for (TCireActorIterator<ACireSummon> It(Owner->GetWorld()); It; ++It)
        if (It->GetOwnerHero() == Owner && It->bCommandable && !It->bDead && !It->IsActorBeingDestroyed() && !It->IsA<ACirePet>()) return true;
    return false;
}

TArray<FCireSummonBarEntry> CireSummonsBar::Collect(const ACireHero* Owner, float ServerNow)
{
    TArray<FCireSummonBarEntry> Out;
    if (!IsValid(Owner) || !Owner->GetWorld()) return Out;
    for (TCireActorIterator<ACireSummon> It(Owner->GetWorld()); It; ++It)
    {
        const ACireSummon* S = *It;
        if (S->GetOwnerHero() != Owner || S->bDead || S->IsActorBeingDestroyed() || S->IsA<ACirePet>()) continue;
        FCireSummonBarEntry E;
        E.Unit = const_cast<ACireSummon*>(S); E.Kind = ECireSummonBarKind::Summon;
        E.Name = S->HeroName; E.IconId = IconFor(S);
        E.Health = S->Health; E.MaxHealth = S->MaxHealth;
        E.Duration = S->SummonSpec.DurationSeconds;
        E.Remaining = S->ExpiresServerTime > 0 ? FMath::Clamp(S->ExpiresServerTime - ServerNow, 0.f, FMath::Max(E.Duration, 0.f)) : -1.f;
        E.bCommandable = S->bCommandable; E.bFights = true; E.State = StateOf(S);
        Out.Add(E);
    }
    for (TCireActorIterator<ACireConstruct> It(Owner->GetWorld()); It; ++It)
    {
        const ACireConstruct* C = *It;
        if (!OwnedBy(C, Owner)) continue;
        FCireSummonBarEntry E;
        E.Unit = const_cast<ACireConstruct*>(C); E.Kind = ECireSummonBarKind::Construct;
        E.Name = NameFor(C); E.IconId = IconFor(C);
        E.Health = C->Health; E.MaxHealth = C->MaxHealth;
        E.Duration = C->ConstructSpec.LifetimeSeconds;
        E.Remaining = C->ExpiresServerTime > 0 ? FMath::Clamp(C->ExpiresServerTime - ServerNow, 0.f, FMath::Max(E.Duration, 0.f))
            : FMath::Max(0.f, C->ConstructSpec.LifetimeSeconds - C->GetAge());
        const auto Kind = C->ConstructSpec.Kind;
        E.bFights = Kind == ECireConstructKind::Turret || Kind == ECireConstructKind::Skitter || Kind == ECireConstructKind::Trap;
        E.State = StateOf(C);
        Out.Add(E);
    }
    // Commandable summons first, then summons, then constructs; same kinds group together, soonest to expire last.
    Out.StableSort([](const FCireSummonBarEntry& A, const FCireSummonBarEntry& B)
    {
        if (A.bCommandable != B.bCommandable) return A.bCommandable;
        if (A.Kind != B.Kind) return A.Kind == ECireSummonBarKind::Summon;
        if (A.IconId != B.IconId) return A.IconId < B.IconId;
        return A.Remaining > B.Remaining;
    });
    return Out;
}
