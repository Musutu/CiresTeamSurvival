#include "CireSkillCasting.h"
#include "CireSignatureSkills.h" // new-champions
#include "CireSkillShop.h" // progression-shop: per-level cast scaling
#include "CireRoleSkills.h"
#include "CireDeveloperTools.h"
#include "CireSkillshot.h"
#include "CireConstruct.h"
#include "CireSummon.h"
#include "CireSkillRuntime.h"
#include "CireCombatEvents.h"
#include "Components/PrimitiveComponent.h"

namespace
{
bool PlayerShot(const FString& Id) { return Id == TEXT("ember_lance") || Id == TEXT("frost_bind") || Id == TEXT("piercing_shot"); }
bool PlayerConstruct(const FString& Id) { return Id == TEXT("summoned_wall") || Id == TEXT("protection_dome"); }
bool PlayerSummon(const FString& Id) { return Id == TEXT("oathbound_guardian") || Id == TEXT("spectral_pack"); }
struct FCost { float Mana = 0, Energy = 0, Cooldown = 0, Range = 0; };
bool CostFor(const FString& Id, FCost& Cost)
{
    if (PlayerShot(Id)) { if (const auto* S = CireSkillTuning::FindSkillshot(Id)) { Cost = {S->ManaCost, S->EnergyCost, S->CooldownSeconds, S->CastRange}; return true; } }
    if (PlayerConstruct(Id)) { if (const auto* S = CireSkillTuning::FindConstruct(Id)) { Cost = {S->ManaCost, S->EnergyCost, S->CooldownSeconds, S->CastRange}; return true; } }
    if (PlayerSummon(Id)) { if (const auto* S = CireSkillTuning::FindSummon(Id)) { Cost = {S->ManaCost, S->EnergyCost, S->CooldownSeconds, S->CastRange}; return true; } }
    return false;
}
bool GroundAim(ACireHero* Hero, FVector& Aim)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireGroundCast), false, Hero);
    FHitResult Hit;
    if (!Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Aim + FVector(0, 0, 300), Aim - FVector(0, 0, 500),
        FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Hit.ImpactNormal.Z < .8f) return false;
    Aim = Hit.ImpactPoint;
    return true;
}
bool PlacementSight(ACireHero* Hero, FVector Ground)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CirePlacementSight), false, Hero);
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic); Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    TArray<FHitResult> Hits;
    Hero->GetWorld()->LineTraceMultiByObjectType(Hits, Hero->GetActorLocation(), Ground + FVector(0, 0, 92), Objects, Query);
    for (const auto& Hit : Hits)
    {
        if (const auto* Construct = ::Cast<ACireConstruct>(Hit.GetActor()))
        { if (Construct->IsWall() && Construct->ConstructSpec.bBlockMovement) return false; }
        else if (Hit.GetComponent() && Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block) return false;
    }
    return true;
}
}

bool CireSkillCasting::Handles(const FString& Id) { return CireSignatureSkills::Handles(Id)||CireRoleSkills::Handles(Id)||PlayerShot(Id) || PlayerConstruct(Id) || PlayerSummon(Id); }
FString CireSkillCasting::Name(const FString& Id)
{
    if(CireSignatureSkills::Knows(Id))return CireSignatureSkills::Name(Id); // new-champions
    if(CireRoleSkills::Handles(Id))return CireRoleSkills::Name(Id);
    if (Id == TEXT("ember_lance")) return TEXT("Ember Lance");
    if (Id == TEXT("frost_bind")) return TEXT("Frost Bind");
    if (Id == TEXT("piercing_shot")) return TEXT("Piercing Shot");
    if (Id == TEXT("summoned_wall")) return TEXT("Summoned Wall");
    if (Id == TEXT("protection_dome")) return TEXT("Protection Dome");
    if (Id == TEXT("oathbound_guardian")) return TEXT("Oathbound Guardian");
    if (Id == TEXT("spectral_pack")) return TEXT("Spectral Pack");
    return Id;
}
FString CireSkillCasting::Description(const FString& Id)
{
    if(CireSignatureSkills::Knows(Id))return CireSignatureSkills::Description(Id); // new-champions
    if(CireRoleSkills::Handles(Id))return CireRoleSkills::Description(Id);
    FCost Cost;
    if (!CostFor(Id, Cost)) return TEXT("Combat recipe unavailable.");
    FString Detail;
    if (PlayerShot(Id))
    {
        const auto& S = *CireSkillTuning::FindSkillshot(Id);
        Detail = FString::Printf(TEXT("Aim a projectile: %.0f damage, %.0f cm/s, %.0f cm radius, %.0f cm travel. Can be dodged; collision rules apply."), S.Damage, S.Speed, S.Radius, S.MaxRange);
    }
    else if (PlayerConstruct(Id))
    {
        const auto& S = *CireSkillTuning::FindConstruct(Id);
        Detail = FString::Printf(TEXT("Place %s: %.0f health for %.0fs; %.0f x %.0f cm footprint. Requires clear ground."),
            S.Kind == ECireConstructKind::Wall ? TEXT("a wall blocking units") : TEXT("projectile protection"), S.MaxHealth, S.LifetimeSeconds, S.Width, S.Depth);
    }
    else
    {
        const auto& S = *CireSkillTuning::FindSummon(Id);
        Detail = FString::Printf(TEXT("Summon %d %s for %.0fs: %.0f health, %.0f damage each. %s"), S.Count,
            S.bCommandable ? TEXT("guardian") : TEXT("companions"), S.DurationSeconds, S.Health, S.Damage,
            S.bCommandable ? TEXT("Command follow, move, attack, or hold.") : TEXT("Requires a hostile target; companions pursue it."));
    }
    return Detail + FString::Printf(TEXT(" %.0f mana / %.0f energy; %.1fs base cooldown; %.0f cm cast range."), Cost.Mana, Cost.Energy, Cost.Cooldown, Cost.Range);
}
bool CireSkillCasting::Cast(ACireHero* Hero, int32 Slot, const FString& Id)
{
    if(CireSignatureSkills::Handles(Id))return CireSignatureSkills::Cast(Hero,Slot,Id); // new-champions
    if(CireRoleSkills::Handles(Id))return CireRoleSkills::Cast(Hero,Slot,Id);
    if (!IsValid(Hero) || !Hero->HasAuthority() || !CireSkillRuntime::Alive(Hero) || !Handles(Id) ||
        !Hero->Skills.IsValidIndex(Slot) || Hero->Skills[Slot] != Id || !Hero->Cooldowns.IsValidIndex(Slot) ||
        Hero->Cooldowns[Slot] > 0 || Hero->GlobalCooldown > 0) return false;
    auto* Mode = Hero->GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase()) return false;
    auto Fail = [&](const TCHAR* Message) { Hero->Notice = Message; return false; };
    FCost Cost;
    if (!CostFor(Id, Cost)) return Fail(TEXT("This combat recipe is unavailable."));
    if (!FMath::IsFinite(Cost.Mana) || Cost.Mana < 0 || !FMath::IsFinite(Cost.Energy) || Cost.Energy < 0 ||
        !FMath::IsFinite(Cost.Cooldown) || Cost.Cooldown < 0 || !FMath::IsFinite(Cost.Range) || Cost.Range < 0) return Fail(TEXT("Combat recipe has invalid costs or range."));
    if (!CireSkillShop::CanPayCast(Hero, Id, Cost.Mana, Cost.Energy)) return Fail(TEXT("Not enough mana or energy.")); // progression-shop: Skill Shop level (Ability DB curve)
    const bool bTargetHostile = CireCombat::AreHostile(Hero, Hero->Target);
    FVector Aim = Hero->bHasCastAim ? Hero->CastAimPoint : bTargetHostile ? Hero->Target->GetActorLocation() :
        Hero->GetActorLocation() + Hero->GetActorForwardVector().GetSafeNormal2D() * FMath::Min(500.f, Cost.Range);
    if (Aim.ContainsNaN() || !CireSkillRuntime::InRealmBounds(Mode, Hero->TeamId, Aim) ||
        FVector::DistSquared2D(Hero->GetActorLocation(), Aim) > FMath::Square(Cost.Range)) return Fail(TEXT("Aim within your realm and casting range."));
    if (!PlayerShot(Id))
    {
        if (!GroundAim(Hero, Aim)) return Fail(TEXT("Aim at supported battlefield ground."));
        if (!PlacementSight(Hero, Aim)) return Fail(TEXT("A wall or world object blocks that placement."));
    }
    const FString AbilityName = ACireHero::SkillName(Id);
    bool bSpawned = false;
    const float Power = Mode->Power(Hero->TeamId);
    if (PlayerShot(Id))
    {
        auto Spec = *CireSkillTuning::FindSkillshot(Id);
        Spec.Damage = FMath::Min(10000.f, Spec.Damage * Power);
        Spec.AbilityName = AbilityName;
        bSpawned = ACireSkillshot::Spawn(Hero, Spec, Aim, AbilityName) != nullptr;
    }
    else if (PlayerConstruct(Id))
    {
        const auto Spec = *CireSkillTuning::FindConstruct(Id);
        const FVector Direction = (Aim - Hero->GetActorLocation()).GetSafeNormal2D();
        bSpawned = ACireConstruct::Spawn(Hero, Spec, Aim, Direction.IsNearlyZero() ? Hero->GetActorRotation() : Direction.Rotation(), AbilityName) != nullptr;
    }
    else
    {
        auto Spec = *CireSkillTuning::FindSummon(Id);
        if (!Spec.bCommandable && !bTargetHostile) return Fail(TEXT("Select a hostile target for your companions."));
        Spec.Damage = FMath::Min(10000.f, Spec.Damage * Power);
        const auto Units = ACireSummon::SpawnGroup(Hero, Spec, bTargetHostile ? Hero->Target : nullptr, Aim);
        bSpawned = Units.Num() == Spec.Count;
        if (!bSpawned) for (auto* Unit : Units) if (IsValid(Unit)) Unit->Destroy();
    }
    if (!bSpawned) return Fail(TEXT("Cannot place or launch this ability here; check space and active summon limits."));
    Hero->Mana -= Cost.Mana; Hero->Energy -= Cost.Energy;
    Hero->Cooldowns[Slot] = static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(Hero->GetWorld(),Cost.Cooldown), Hero->CDR));
    CireSkillShop::ApplyCastLevel(Hero, Slot, Id, Cost.Mana, Cost.Energy); // progression-shop: Skill Shop level (Ability DB curve)
    Hero->GlobalCooldown = .9f;
    Hero->Notice = AbilityName; Hero->ForceNetUpdate();
    CireCombat::PlayCue(Hero, bTargetHostile ? Hero->Target : nullptr, FName(*Id), Hero->GetActorLocation(), Aim, ECireSpellCue::Cast);
    return true;
}
