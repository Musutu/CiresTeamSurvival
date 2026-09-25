#include "CirePolymorph.h"
// progression-shop: see CirePolymorph.h.
#include "CireAbilityDB.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireCrowdControl.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireSkillShop.h"
#include "CireThreat.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/DamageEvents.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCirePolymorph, Log, All);

namespace
{
const FName PolymorphedId(TEXT("polymorphed"));

// CC0 critters (Quaternius via poly.pizza; Art/Creatures/Free/PROVENANCE_Critters.md).
struct FCritterDef { const TCHAR* Name; const TCHAR* Mesh; const TCHAR* Idle; const TCHAR* Walk; float HeightCm; float Yaw; FLinearColor Tint; };
const FCritterDef Critters[] = {
    {TEXT("Chicken"), TEXT("/Game/Free/Critters/Chicken/Chicken/SkeletalMeshes/Chicken.Chicken"),
        TEXT("/Game/Free/Critters/Chicken/Chicken/SkeletalMeshes/ChickenCharacterArmature_Idle.ChickenCharacterArmature_Idle"),
        TEXT("/Game/Free/Critters/Chicken/Chicken/SkeletalMeshes/ChickenCharacterArmature_Walk.ChickenCharacterArmature_Walk"),
        70.f, -90.f, FLinearColor::White},
    {TEXT("Piglet"), TEXT("/Game/Free/Creatures/Pig/Pig/SkeletalMeshes/Pig.Pig"),
        TEXT("/Game/Free/Creatures/Pig/Pig/SkeletalMeshes/PigAnimalArmature_AnimalArmature_AnimalArmature_Idle.PigAnimalArmature_AnimalArmature_AnimalArmature_Idle"),
        TEXT("/Game/Free/Creatures/Pig/Pig/SkeletalMeshes/PigAnimalArmature_AnimalArmature_AnimalArmature_Walk.PigAnimalArmature_AnimalArmature_AnimalArmature_Walk"),
        62.f, -90.f, FLinearColor(1.f, .52f, .58f, 1)},
    {TEXT("Frog"), TEXT("/Game/Free/Critters/Frog/Frog/SkeletalMeshes/Frog.Frog"),
        TEXT("/Game/Free/Critters/Frog/Frog/SkeletalMeshes/FrogFrogArmature_Frog_Idle.FrogFrogArmature_Frog_Idle"),
        TEXT("/Game/Free/Critters/Frog/Frog/SkeletalMeshes/FrogFrogArmature_Frog_Jump.FrogFrogArmature_Frog_Jump"),
        50.f, -90.f, FLinearColor::White},
};

template <typename T> T* LoadSoft(const TCHAR* Path)
{
    return Path && *Path ? LoadObject<T>(nullptr, Path, nullptr, LOAD_NoWarn | LOAD_Quiet) : nullptr;
}

struct FVisual
{
    TWeakObjectPtr<USkeletalMeshComponent> Critter;
    TArray<TWeakObjectPtr<USceneComponent>> Hidden;
    int32 Kind = -1;
    bool bWalking = false;
};
TMap<TWeakObjectPtr<AActor>, FVisual>& Visuals() { static TMap<TWeakObjectPtr<AActor>, FVisual> Map; return Map; }

struct FWander { FVector Direction = FVector::ForwardVector; float Change = 0; };
TMap<TWeakObjectPtr<AActor>, FWander>& Wanders() { static TMap<TWeakObjectPtr<AActor>, FWander> Map; return Map; }

void Poof(AActor* Unit)
{
    if (!IsValid(Unit) || !Unit->HasAuthority()) return;
    const FVector At = Unit->GetActorLocation();
    CireCombat::PlayCue(Unit, Unit, FName(TEXT("polymorph")), At, At, ECireSpellCue::Impact, 1.1f, true);
}

void EndVisual(AActor* Unit, FVisual& V)
{
    if (V.Critter.IsValid()) V.Critter->DestroyComponent();
    for (const auto& Component : V.Hidden) if (Component.IsValid()) Component->SetHiddenInGame(false, true);
    V = FVisual();
}
} // namespace

FName CirePolymorph::BuffId() { return PolymorphedId; }
bool CirePolymorph::IsPolymorphed(const AActor* Unit) { return Unit && CireBuffs::IsActive(Unit, PolymorphedId); }

int32 CirePolymorph::CritterOf(const AActor* Unit)
{
    if (!IsPolymorphed(Unit)) return -1;
    const UCireBuffState* State = CireBuffs::Get(Unit);
    const FCireBuffEntry* Entry = State ? State->Find(PolymorphedId) : nullptr;
    return Entry ? FMath::Clamp(static_cast<int32>(Entry->Stacks) - 1, 0, static_cast<int32>(ECritter::Count) - 1) : 0;
}

FString CirePolymorph::CritterName(int32 Critter) { return Critter >= 0 && Critter < UE_ARRAY_COUNT(Critters) ? Critters[Critter].Name : FString(); }

bool CirePolymorph::IsImmune(const AActor* Unit)
{
    const auto* M = Cast<ACireMonster>(Unit);
    return M && (M->IsLaneBoss() || M->GetNPCClassification() == ECireNPCClass::Boss);
}

float CirePolymorph::Apply(AActor* Target, float Seconds, AActor* Source, int32 Critter)
{
    if (!IsValid(Target) || !Target->HasAuthority() || Seconds <= 0 || IsImmune(Target)) return 0.f;
    float Duration = Seconds;
    if (const auto* M = Cast<ACireMonster>(Target))
    {
        if (M->Health <= 0) return 0.f;
        if (M->GetNPCClassification() == ECireNPCClass::Elite) Duration *= .5f;
    }
    else if (auto* H = Cast<ACireHero>(Target))
    {
        if (H->bDead) return 0.f;
        Duration = CireCrowdControl::DiminishedSeconds(Target, Source, PolymorphedId, FMath::Min(Duration, ChampionMaxSeconds));
        if (Duration <= 0) return 0.f;
        CireCrowdControl::CancelCast(H, TEXT("Polymorphed"));
        H->PendingAttackTarget.Reset();
        H->Notice = TEXT("Polymorphed!");
    }
    if (Critter < 0) Critter = FMath::RandRange(0, static_cast<int32>(ECritter::Count) - 1);
    Critter = FMath::Clamp(Critter, 0, static_cast<int32>(ECritter::Count) - 1);
    CireBuffs::Remove(Target, PolymorphedId); // a recast re-rolls the critter
    CireBuffs::Apply(Target, PolymorphedId, Duration, Source, Critter + 1);
    if (auto* M = Cast<ACireMonster>(Target))
    {
        CireNPCCombat::Interrupt(M);
        CireThreat::Clear(M);
        M->GetCharacterMovement()->StopMovementImmediately();
    }
    Poof(Target);
    UE_LOG(LogCirePolymorph, Display, TEXT("CIRE_POLYMORPH %s -> %s for %.1fs"), *Target->GetName(), *CritterName(Critter), Duration);
    return Duration;
}

bool CirePolymorph::Break(AActor* Unit)
{
    if (!IsValid(Unit) || !Unit->HasAuthority() || !IsPolymorphed(Unit)) return false;
    CireBuffs::Remove(Unit, PolymorphedId);
    Wanders().Remove(Unit);
    UE_LOG(LogCirePolymorph, Display, TEXT("CIRE_POLYMORPH_BREAK %s"), *Unit->GetName());
    if (auto* M = Cast<ACireMonster>(Unit)) M->GetCharacterMovement()->MaxWalkSpeed = M->BaseMoveSpeed;
    Poof(Unit);
    return true;
}

bool CirePolymorph::TickMonster(ACireMonster* M, float DeltaSeconds)
{
    if (!M || !M->HasAuthority()) return false;
    if (!IsPolymorphed(M))
    {
        if (Wanders().Remove(M) > 0) M->GetCharacterMovement()->MaxWalkSpeed = M->BaseMoveSpeed;
        return false;
    }
    // A harmless critter: no threat, no attacks, no casts; it pecks/hops around slowly.
    CireThreat::Clear(M);
    M->bEngaged = false;
    FWander& W = Wanders().FindOrAdd(M);
    W.Change -= DeltaSeconds;
    if (W.Change <= 0)
    {
        W.Change = FMath::FRandRange(1.2f, 2.6f);
        const float Angle = FMath::FRandRange(0.f, 2.f * PI);
        W.Direction = FMath::FRand() < .3f ? FVector::ZeroVector : FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0);
    }
    M->GetCharacterMovement()->MaxWalkSpeed = WanderSpeed;
    if (!W.Direction.IsNearlyZero()) M->AddMovementInput(W.Direction);
    return true;
}

bool CirePolymorph::HasCritterVisual(const AActor* Unit)
{
    const FVisual* V = Visuals().Find(Unit);
    return V && V->Critter.IsValid();
}

void CirePolymorph::TickVisual(AActor* Unit)
{
    if (!IsValid(Unit) || !Unit->GetWorld() || Unit->GetNetMode() == NM_DedicatedServer) return;
    const int32 Kind = CritterOf(Unit);
    FVisual* V = Visuals().Find(Unit);
    if (Kind < 0)
    {
        if (V) { EndVisual(Unit, *V); Visuals().Remove(Unit); }
        return;
    }
    if (!V) V = &Visuals().Add(Unit);
    if (V->Kind != Kind || !V->Critter.IsValid())
    {
        EndVisual(Unit, *V);
        const FCritterDef& Def = Critters[Kind];
        USkeletalMesh* Mesh = LoadSoft<USkeletalMesh>(Def.Mesh);
        if (!Mesh) return;
        auto* Component = NewObject<USkeletalMeshComponent>(Unit, NAME_None, RF_Transient);
        Component->SetSkeletalMesh(Mesh);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->SetupAttachment(Unit->GetRootComponent());
        Component->RegisterComponent();
        const float MeshHeight = FMath::Max(1.f, Mesh->GetBounds().BoxExtent.Z * 2.f);
        const float Scale = Def.HeightCm / MeshHeight;
        const auto* Capsule = Cast<UCapsuleComponent>(Unit->GetRootComponent());
        const float Half = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 88.f;
        const float Bottom = Mesh->GetBounds().Origin.Z - Mesh->GetBounds().BoxExtent.Z;
        Component->SetRelativeScale3D(FVector(Scale));
        Component->SetRelativeLocation(FVector(0, 0, -Half - Bottom * Scale));
        Component->SetRelativeRotation(FRotator(0, Def.Yaw, 0));
        if (Def.Tint != FLinearColor::White)
            for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
                if (UMaterialInstanceDynamic* MID = Component->CreateDynamicMaterialInstance(Slot))
                { MID->SetVectorParameterValue(TEXT("RaceTint"), Def.Tint); MID->SetScalarParameterValue(TEXT("RaceTintStrength"), 1.f); MID->SetVectorParameterValue(TEXT("Paint Tint"), Def.Tint); }
        if (UAnimSequence* Idle = LoadSoft<UAnimSequence>(Def.Idle)) Component->PlayAnimation(Idle, true);
        // Hide the monster's own body, weapons and props while it is a critter.
        TArray<USceneComponent*> Children;
        Unit->GetRootComponent()->GetChildrenComponents(true, Children);
        for (USceneComponent* Child : Children)
            if (Child && Child != Component && Child->IsA<UPrimitiveComponent>() && !Child->bHiddenInGame && Child->IsVisible())
            { Child->SetHiddenInGame(true, false); V->Hidden.Add(Child); }
        V->Critter = Component; V->Kind = Kind; V->bWalking = false;
        Component->UpdateBounds();
        UE_LOG(LogCirePolymorph, Display, TEXT("CIRE_POLYMORPH_VISUAL %s critter=%s scale=%.3f world_bounds=%s actor=%s"), *Unit->GetName(), Def.Name, Scale,
            *Component->Bounds.GetBox().ToString(), *Unit->GetActorLocation().ToString());
    }
    // Walk while moving, idle otherwise.
    const bool bWalking = Unit->GetVelocity().Size2D() > 20.f;
    if (bWalking != V->bWalking && V->Critter.IsValid())
    {
        const FCritterDef& Def = Critters[Kind];
        if (UAnimSequence* Clip = LoadSoft<UAnimSequence>(bWalking ? Def.Walk : Def.Idle)) V->Critter->PlayAnimation(Clip, true);
        V->bWalking = bWalking;
    }
    // Keep the body hidden, including parts other presentation code adds or re-shows later.
    TArray<USceneComponent*> Children;
    Unit->GetRootComponent()->GetChildrenComponents(true, Children);
    for (USceneComponent* Child : Children)
        if (Child && V->Critter.Get() != Child && !Child->IsAttachedTo(V->Critter.Get()) && Child->IsA<UPrimitiveComponent>() && !Child->bHiddenInGame)
        { Child->SetHiddenInGame(true, false); V->Hidden.AddUnique(Child); }
}

bool CirePolymorph::Handles(const FString& Id) { return Id == TEXT("polymorph"); }

bool CirePolymorph::CastSkill(ACireHero* H, int32 Slot, const FString& Id)
{
    if (!H || !H->HasAuthority() || !H->Cooldowns.IsValidIndex(Slot)) return false;
    const FCireAbilityDef* D = CireAbilityDB::Find(Id);
    if (!D) return false;
    AActor* Target = H->Target;
    if (!H->IsHostile(Target) || !H->InRange(Target, D->Range > 0 ? D->Range : 1100.f)) { H->Notice = TEXT("Select a hostile target in range."); return false; }
    if (IsImmune(Target)) { H->Notice = TEXT("Bosses and Pack Leaders cannot be polymorphed."); return false; }
    if (!CireSkillShop::CanPayCast(H, Id, D->Base.ManaCost, D->Base.EnergyCost)) { H->Notice = TEXT("Not enough mana or energy."); return false; }
    H->Mana -= D->Base.ManaCost; H->Energy -= D->Base.EnergyCost;
    H->Cooldowns[Slot] = static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(H->GetWorld(), D->Base.Cooldown), H->CDR));
    CireSkillShop::ApplyCastLevel(H, Slot, Id, D->Base.ManaCost, D->Base.EnergyCost);
    H->GlobalCooldown = .9f;
    const float Seconds = CireAbilityDB::EffectiveStats(Id, FMath::Max(1, CireSkillShop::Level(H, Id))).Effect;
    const float Applied = Apply(Target, CireDeveloperTools::EffectSeconds(H->GetWorld(), Seconds), H);
    H->Notice = Applied > 0 ? FString::Printf(TEXT("Polymorph: %s for %.0fs"), *CritterName(CritterOf(Target)), Applied) : TEXT("The target resisted Polymorph.");
    return true;
}

#if !UE_BUILD_SHIPPING
// Native checks (run by CireSkillShop::RunSmoke -> CireProgression::RunSmoke).
bool CirePolymorph::RunSmoke(ACireGameMode* Mode)
{
    if (!Mode || !Mode->HasAuthority()) return false;
    int32 Count = 0; bool bPass = true;
    auto Check = [&](bool bValue, const TCHAR* Label) { ++Count; if (!bValue) { bPass = false; UE_LOG(LogCirePolymorph, Error, TEXT("CIRE_POLYMORPH_CHECK_FAIL %s"), Label); } };
    TArray<AActor*> Spawned;
    const Cires::MatchClock SavedClock = Mode->Clock;
    const TArray<ACireHero*> SavedHeroes = Mode->Heroes;
    const TArray<ACireMonster*> SavedMonsters = Mode->Monsters;
    ON_SCOPE_EXIT { for (AActor* A : Spawned) if (IsValid(A)) A->Destroy(); Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters; };
    Mode->Clock = Cires::MatchClock(); // survival: combat allowed
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const FVector Base = Mode->BasePosition(0) + FVector(1800, 0, 0);
    auto Monster = [&](FVector Offset)
    {
        auto* M = Mode->GetWorld()->SpawnActor<ACireMonster>(Base + Offset, FRotator::ZeroRotator, Params);
        Spawned.Add(M); M->SetActorTickEnabled(false); M->Lane = 0; M->Health = M->MaxHealth = 5000; Mode->Monsters.Add(M);
        return M;
    };
    auto Hero = [&](int32 Team, FVector Offset)
    {
        auto* H = Mode->GetWorld()->SpawnActor<ACireHero>(Mode->BasePosition(Team) + Offset, FRotator::ZeroRotator, Params);
        Spawned.Add(H); H->SetActorTickEnabled(false); H->TeamId = Team; H->Draft(2); H->Offers.Reset(); Mode->Heroes.Add(H);
        return H;
    };
    ACireHero* Mage = Hero(0, FVector(1600, 0, 0));
    ACireMonster* M = Monster(FVector(150, 0, 0));
    if (!Mage || !M) { Check(false, TEXT("fixture")); return false; }
    // Apply, random critter.
    const float Applied = Apply(M, 8.f, Mage);
    Check(Applied == 8.f && IsPolymorphed(M) && CritterOf(M) >= 0 && CritterOf(M) < static_cast<int32>(ECritter::Count) && !CritterName(CritterOf(M)).IsEmpty(),
        TEXT("normal monster polymorphed for the full duration into a critter"));
    TSet<int32> Seen;
    for (int32 I = 0; I < 40; ++I) { Apply(M, 8.f, Mage); Seen.Add(CritterOf(M)); }
    Check(Seen.Num() == static_cast<int32>(ECritter::Count), TEXT("the critter is picked at random per cast (all three seen)"));
    Apply(M, 8.f, Mage, 1);
    Check(CritterOf(M) == 1 && CritterName(1) == TEXT("Piglet"), TEXT("critter is carried by the replicated buff record"));
    // No attacks or casts while polymorphed; it wanders and has no threat.
    Mage->SetActorLocation(M->GetActorLocation() + FVector(120, 0, 0));
    M->Damage = 80;
    const float HealthBefore = Mage->Health;
    for (int32 Step = 0; Step < 40; ++Step) M->Tick(.1f);
    Check(FMath::IsNearlyEqual(Mage->Health, HealthBefore) && M->CastingAbility.IsEmpty() && !M->bEngaged, TEXT("a polymorphed monster neither attacks nor casts"));
    Check(CireCrowdControl::IsStunned(M), TEXT("polymorph incapacitates like a stun"));
    // Any damage breaks it.
    const float Taken = M->TakeDamage(10.f, FDamageEvent(), nullptr, Mage);
    Check(Taken > 0 && !IsPolymorphed(M), TEXT("damage breaks polymorph"));
    // Immunities and elites.
    ACireMonster* Boss = Monster(FVector(0, 400, 0)); Boss->bBoss = true;
    Check(Apply(Boss, 8.f, Mage) == 0.f && !IsPolymorphed(Boss), TEXT("lane bosses are immune"));
    ACireMonster* Leader = Monster(FVector(0, -400, 0));
    CireNPCCombat::ConfigureArchetype(Leader, CireNPCArchetypes::Get().PackLeader, 3, 2, 1);
    Leader->PackId = 9911; Leader->Lane = 0;
    Check(Leader->GetNPCClassification() != ECireNPCClass::Boss || (Apply(Leader, 8.f, Mage) == 0.f && IsImmune(Leader)), TEXT("Pack Leaders are immune"));
    ACireMonster* Elite = Monster(FVector(0, 800, 0));
    if (!CireNPCArchetypes::Get().PackMembers.IsEmpty()) CireNPCCombat::ConfigureArchetype(Elite, CireNPCArchetypes::Get().PackMembers[0], 3, 2, 1);
    Elite->PackId = 9912; Elite->Lane = 0;
    if (Elite->GetNPCClassification() == ECireNPCClass::Elite) Check(FMath::IsNearlyEqual(Apply(Elite, 8.f, Mage), 4.f), TEXT("elites get half the duration"));
    // PvP: short, with diminishing returns.
    ACireHero* Foe = Hero(1, FVector(1650, 200, 0));
    const float First = Apply(Foe, 8.f, Mage);
    Break(Foe);
    const float Second = Apply(Foe, 8.f, Mage);
    Check(First > 0 && First <= ChampionMaxSeconds && Second < First, TEXT("champions: at most 3 s with diminishing returns"));
    Break(Foe);
    // The skill itself: a real cast through the hero cast path (1.5 s cast time).
    Mage->Skills = {TEXT("polymorph")}; Mage->Cooldowns.Init(0, 1); Mage->Mana = Mage->MaxMana = 400; Mage->GlobalCooldown = 0;
    ACireMonster* Victim = Monster(FVector(300, 150, 0));
    Mage->Target = Victim;
    Mage->SetActorLocation(Victim->GetActorLocation() + FVector(500, 0, 0));
    Mage->Cast(0);
    CireCrowdControl::CompleteCastNow(Mage);
    Check(IsPolymorphed(Victim) && Mage->Cooldowns[0] > 0 && Mage->Mana < 400, TEXT("Polymorph cast: pays mana, starts its cooldown, turns the target into a critter"));
    Check(CireBuffs::KnownIds().Contains(PolymorphedId), TEXT("polymorphed buff is registered"));
    UE_LOG(LogCirePolymorph, Display, TEXT("CIRE_POLYMORPH_%s checks=%d"), bPass ? TEXT("PASS") : TEXT("FAIL"), Count);
    return bPass;
}
#endif
