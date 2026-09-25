// new-champions: signature kits of the Gunblade, Witch Slayer, Huntress, Aetheri Artificer and Aetheri Warden.
#include "CireSignatureSkills.h"
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireDeveloperTools.h"
#include "CireGame.h"
#include "CireNPCState.h"
#include "CireRaces.h"
#include "CireSkillRuntime.h"
#include "CireSkillShop.h"
#include "CireSkillshot.h"
#include "CireTechConstructs.h"
#include "CireThreat.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireSignature, Log, All);

namespace
{
enum class EDelivery : uint8
{
    Projectile, Pierce, Cone, Circle, SelfBurst, Flurry, Strike, Lunge, Chain, Dash, Pounce, Mark, AreaMark,
    SelfBuff, AllyHeal, Construct, Banish, Execute, Storm, Overcharge, Passive
};
struct FSig
{
    const TCHAR* Id;
    EDelivery D;
    float Scaling = 0;       // x primary attribute, added to the Ability DB base effect (damage and healing)
    float Angle = 0;         // cone degrees
    float Warning = 0;       // telegraph seconds before a ground circle resolves
    int32 Hits = 1;          // chain targets, pierce limit, flurry slashes
    const TCHAR* Style = TEXT("arcane"); // skillshot visual style (school runes on the corridor)
    const TCHAR* Record = nullptr;       // buff record written by marks / self buffs
};
const FSig Sigs[] = {
    // ---- Gunblade (Bounty Hunter) ----
    {TEXT("silver_shot"), EDelivery::Projectile, 1.6f, 0, 0, 1, TEXT("holy")},
    {TEXT("hex_mark"), EDelivery::Mark, 0, 0, 0, 1, nullptr, TEXT("bounty_mark")},
    {TEXT("powder_flask"), EDelivery::Circle, 1.0f, 0, .35f},
    {TEXT("blade_flurry"), EDelivery::Flurry, .8f, 0, 0, 3},
    {TEXT("hunters_stride"), EDelivery::Dash, 0, 0, 0, 1, nullptr, TEXT("hunters_stride")},
    {TEXT("warding_talisman"), EDelivery::SelfBuff, 0, 0, 0, 1, nullptr, TEXT("warding_talisman")},
    {TEXT("price_on_every_soul"), EDelivery::Passive},
    {TEXT("collect_the_bounty"), EDelivery::Execute, 3.0f},
    // ---- Witch Slayer ----
    {TEXT("arcane_blunderbuss"), EDelivery::Cone, 1.8f, 60},
    {TEXT("spirit_lantern"), EDelivery::Construct},
    {TEXT("purge"), EDelivery::Strike, 1.0f},
    {TEXT("banishment"), EDelivery::Banish, 1.5f, 0, 0, 1, nullptr, TEXT("banished")},
    {TEXT("witchfinders_mark"), EDelivery::Mark, 0, 0, 0, 1, nullptr, TEXT("witch_mark")},
    {TEXT("spectral_blade"), EDelivery::Lunge, 1.4f},
    {TEXT("witchbane"), EDelivery::Passive},
    {TEXT("hexbane_judgment"), EDelivery::Circle, 3.0f, 0, 1.0f},
    // ---- Huntress (mounted glaive thrower) ----
    {TEXT("bouncing_glaive"), EDelivery::Chain, 1.5f, 0, 0, 5},
    {TEXT("sabercat_pounce"), EDelivery::Pounce, 1.0f},
    {TEXT("owl_scout"), EDelivery::AreaMark, 0, 0, 0, 1, nullptr, TEXT("tracked")},
    {TEXT("moonlit_sprint"), EDelivery::SelfBuff, 0, 0, 0, 1, nullptr, TEXT("moonlit_sprint")},
    {TEXT("crescent_volley"), EDelivery::Pierce, 1.4f, 0, 0, 5, TEXT("arrow")},
    {TEXT("sabercat_rake"), EDelivery::Cone, 1.2f, 70},
    {TEXT("moon_glaive"), EDelivery::Passive},
    {TEXT("glaive_storm"), EDelivery::Storm, .5f},
    // ---- Aetheri Artificer ----
    {TEXT("photon_turret"), EDelivery::Construct},
    {TEXT("skitter_swarm"), EDelivery::Construct},
    {TEXT("arc_mine"), EDelivery::Construct},
    {TEXT("disruption_pylon"), EDelivery::Construct},
    {TEXT("phase_lance"), EDelivery::Projectile, 1.8f, 0, 0, 1, TEXT("arcane")},
    {TEXT("overcharge"), EDelivery::Overcharge, 0, 0, 0, 1, nullptr, TEXT("overcharge")},
    {TEXT("aether_engineering"), EDelivery::Passive},
    {TEXT("warp_obelisk"), EDelivery::Construct},
    // ---- Aetheri Warden ----
    {TEXT("aegis_pylon"), EDelivery::Construct},
    {TEXT("haste_pylon"), EDelivery::Construct},
    {TEXT("gravity_pylon"), EDelivery::Construct},
    {TEXT("stasis_snare"), EDelivery::Construct},
    {TEXT("aether_mend"), EDelivery::AllyHeal, 2.5f},
    {TEXT("repulsor_pulse"), EDelivery::SelfBurst, 1.0f},
    {TEXT("resonant_lattice"), EDelivery::Passive},
    {TEXT("aether_nexus"), EDelivery::Construct},
};
const FSig* FindSig(const FString& Id) { for (const FSig& S : Sigs) if (Id == S.Id) return &S; return nullptr; }

const FName BountyId(TEXT("bounty_mark")), WitchMarkId(TEXT("witch_mark")), TrackedId(TEXT("tracked")), BanishedId(TEXT("banished")),
    StrideId(TEXT("hunters_stride")), TalismanId(TEXT("warding_talisman")), SprintId(TEXT("moonlit_sprint")), OverchargeId(TEXT("overcharge")),
    AegisId(TEXT("aether_aegis")), HasteId(TEXT("aether_haste")), WeakenedId(TEXT("aether_weakened")), NexusId(TEXT("aether_nexus")), EmpoweredId(TEXT("npc_aether_empowered"));

const FCireBuffEntry* Active(const AActor* Unit, FName Id)
{
    if (!Unit || !CireBuffs::IsActive(Unit, Id)) return nullptr;
    const auto* State = CireBuffs::Get(Unit); return State ? State->Find(Id) : nullptr;
}
float Fraction(const AActor* Unit, FName Id) { const auto* E = Active(Unit, Id); return E ? E->Stacks / 100.f : 0.f; }
bool IsCasting(const AActor* Unit)
{
    if (const auto* M = Cast<ACireMonster>(Unit)) return !M->CastingAbility.IsEmpty();
    if (const auto* H = Cast<ACireHero>(Unit)) return CireCrowdControl::IsCasting(H);
    return false;
}
bool IsBoss(const AActor* Unit) { const auto* M = Cast<ACireMonster>(Unit); return M && (M->IsLaneBoss() || M->bBoss); }
bool IsUndeadOrVoid(const AActor* Unit)
{
    const auto* M = Cast<ACireMonster>(Unit);
    if (!M || !M->NPCState) return false;
    const FName Race = CireRaces::RaceOf(M->NPCState->ArchetypeId);
    if (Race == TEXT("hollow") || Race == TEXT("voidborn")) return true;
    const FString Arch = M->NPCState->ArchetypeId.ToString();
    return Race.IsNone() && (Arch.Contains(TEXT("hollow")) || Arch.Contains(TEXT("grave")) || Arch.Contains(TEXT("barbed")) || Arch.Contains(TEXT("blight")));
}
float HealthOf(const AActor* U) { if (const auto* H = Cast<ACireHero>(U)) return H->Health; if (const auto* M = Cast<ACireMonster>(U)) return M->Health; if (const auto* C = Cast<ACireConstruct>(U)) return C->Health; return 0; }
float MaxHealthOf(const AActor* U) { if (const auto* H = Cast<ACireHero>(U)) return H->MaxHealth; if (const auto* M = Cast<ACireMonster>(U)) return M->MaxHealth; if (const auto* C = Cast<ACireConstruct>(U)) return C->MaxHealth; return 0; }
bool Sight(const AActor* From, const AActor* To)
{
    if (!IsValid(From) || !IsValid(To)) return false;
    FHitResult Hit; FCollisionQueryParams Q(SCENE_QUERY_STAT(CireSignatureSight), false, From);
    const bool bBlocked = From->GetWorld()->LineTraceSingleByChannel(Hit, From->GetActorLocation() + FVector(0, 0, 45), To->GetActorLocation() + FVector(0, 0, 35), ECC_Visibility, Q);
    return !bBlocked || Hit.GetActor() == To;
}
TArray<AActor*> Enemies(ACireHero* Hero, FVector Center, float Radius, bool bConstructs = true)
{
    TArray<AActor*> Out;
    UWorld* World = Hero->GetWorld();
    auto Consider = [&](AActor* U)
    {
        if (!U || U == Hero || !CireCombat::AreHostile(Hero, U)) return;
        const auto* C = Cast<ACharacter>(U);
        const float Body = C ? C->GetCapsuleComponent()->GetScaledCapsuleRadius() : 30.f;
        if (FVector::DistSquared2D(Center, U->GetActorLocation()) <= FMath::Square(Radius + Body) && FMath::Abs(U->GetActorLocation().Z - Center.Z) < 400.f) Out.Add(U);
    };
    if (auto* Mode = World->GetAuthGameMode<ACireGameMode>()) for (auto* M : Mode->Monsters) if (IsValid(M)) Consider(M);
    for (TActorIterator<ACireHero> It(World); It; ++It) Consider(*It);
    if (bConstructs) for (TActorIterator<ACireConstruct> It(World); It; ++It) if (!It->IsActorBeingDestroyed()) Consider(*It);
    return Out;
}
bool GroundAim(ACireHero* Hero, FVector& Aim)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireSignatureGround), false, Hero);
    FHitResult Hit;
    if (!Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Aim + FVector(0, 0, 300), Aim - FVector(0, 0, 500), FCollisionObjectQueryParams(ECC_WorldStatic), Query) || Hit.ImpactNormal.Z < .75f) return false;
    Aim = Hit.ImpactPoint; return true;
}
bool GroundSight(ACireHero* Hero, FVector Ground)
{
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireSignaturePlacement), false, Hero);
    FHitResult Hit;
    return !Hero->GetWorld()->LineTraceSingleByObjectType(Hit, Hero->GetActorLocation() + FVector(0, 0, 35), Ground + FVector(0, 0, 60), FCollisionObjectQueryParams(ECC_WorldStatic), Query) &&
        !ACireConstruct::FindBlockingConstruct(Hero, Ground);
}
FLinearColor SchoolTint(const FCireAbilityDef& D)
{
    ECireSchool S = ECireSchool::Arcane; CireAbilityShapes::ParseSchool(D.School, S);
    const FLinearColor C = CireAbilityShapes::SchoolColor(S);
    return FLinearColor(FMath::Min(C.R, 1.f), FMath::Min(C.G, 1.f), FMath::Min(C.B, 1.f), .38f);
}
FCireAreaSpec Area(const FCireAbilityDef& D, ECireAreaShape Shape, float Radius, float Warning, float Burst)
{
    FCireAreaSpec A; A.Shape = Shape; A.Radius = FMath::Max(10.f, Radius); A.ConeAngleDegrees = 60; A.WarningSeconds = Warning;
    A.DurationSeconds = .1f; A.TickInterval = .5f; A.DamagePerSecond = 0; A.BurstDamage = Burst; A.bPersistent = false; A.bPoison = false;
    A.VerticalTolerance = 90; A.Color = SchoolTint(D); A.AbilityName = D.Name; return A;
}
int32 BountyGold(const ACireMonster* M)
{
    if (IsBoss(M)) return 150;
    const ECireNPCRank Rank = CireRaces::RankOf(M);
    return Rank >= ECireNPCRank::Warlord ? 150 : Rank >= ECireNPCRank::Elite ? 60 : 25;
}
int32 Purge(AActor* Target)
{
    static const TSet<FName> Positive = {TEXT("iron_guard"), TEXT("war_cry"), TEXT("challenge_of_iron"), TEXT("sanctuary"), TEXT("bastion_of_dawn"),
        TEXT("mass_aegis"), TEXT("wellspring"), TEXT("blood_rage"), TEXT("frost_weapon"), TEXT("blessing"), TEXT("regeneration"), TEXT("oathshield"),
        TEXT("borrowed_time"), TEXT("npc_bloodlust"), TEXT("npc_scaleward"), TEXT("aether_aegis"), TEXT("aether_haste"), TEXT("aether_nexus"),
        TEXT("npc_aether_empowered"), TEXT("moonlit_sprint"), TEXT("warding_talisman"), TEXT("hunters_stride"), TEXT("overcharge")};
    int32 Removed = 0;
    if (const auto* State = CireBuffs::Get(Target))
    {
        TArray<FName> Ids; for (const auto& B : State->Buffs) if (Positive.Contains(B.Id)) Ids.AddUnique(B.Id);
        for (const FName Id : Ids) Removed += CireBuffs::Remove(Target, Id) ? 1 : 0;
    }
    if (auto* H = Cast<ACireHero>(Target)) { if (H->ShieldUntil > 0) ++Removed; H->ShieldUntil = 0; H->ForceNetUpdate(); }
    if (auto* M = Cast<ACireMonster>(Target); M && M->NPCState)
    {
        auto* S = M->NPCState.Get();
        if (S->RallyUntil > 0 || S->ShieldWallUntil > 0 || S->GuardUntil > 0) ++Removed;
        S->RallyUntil = S->ShieldWallUntil = S->GuardUntil = 0;
    }
    return Removed;
}
void Later(UWorld* World, float Seconds, TFunction<void()> Work)
{
    if (!World) return;
    FTimerHandle Handle;
    World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Work)), FMath::Max(.01f, Seconds), false);
}
bool DashTo(ACireHero* Hero, FVector Ground, float MaxRange)
{
    const FVector From = Hero->GetActorLocation();
    FVector To = Ground; To.Z = Ground.Z + Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
    const FVector Delta = (To - From); const FVector Flat = Delta.GetSafeNormal2D();
    if (FVector::Dist2D(From, To) > MaxRange) To = FVector(From.X, From.Y, To.Z) + Flat * MaxRange;
    if (ACireConstruct::FindBlockingConstruct(Hero, To)) return false;
    const bool bMoved = Hero->SetActorLocation(To, true, nullptr, ETeleportType::TeleportPhysics);
    if (bMoved) { Hero->SetActorRotation(Flat.IsNearlyZero() ? Hero->GetActorRotation() : Flat.Rotation()); ACireAreaEffect::ClearForActor(Hero); }
    return bMoved && FVector::DistSquared2D(From, Hero->GetActorLocation()) > FMath::Square(40.f);
}
}

// ============================================================================================ identity
const TArray<FString>& CireSignatureSkills::AllIds() { static TArray<FString> Ids = [] { TArray<FString> Out; for (const FSig& S : Sigs) Out.Add(S.Id); return Out; }(); return Ids; }
bool CireSignatureSkills::Knows(const FString& Id) { return FindSig(Id) != nullptr; }
bool CireSignatureSkills::Handles(const FString& Id) { const FSig* S = FindSig(Id); return S && S->D != EDelivery::Passive; }
bool CireSignatureSkills::IsPassive(const FString& Id) { const FSig* S = FindSig(Id); return S && S->D == EDelivery::Passive; }
bool CireSignatureSkills::IsUltimate(const FString& Id) { const auto* D = Knows(Id) ? CireAbilityDB::Find(Id) : nullptr; return D && D->IsUltimate(); }
FString CireSignatureSkills::Name(const FString& Id) { const auto* D = CireAbilityDB::Find(Id); return D ? D->Name : Id; }
FString CireSignatureSkills::Description(const FString& Id)
{
    const auto* D = CireAbilityDB::Find(Id); if (!D) return TEXT("Recipe unavailable.");
    const FString Prefix = D->IsUltimate() ? TEXT("ULTIMATE: ") : D->IsPassive() ? TEXT("PASSIVE: ") : CireTechConstructs::IsConstructSkill(Id) ? TEXT("CONSTRUCT: ") : TEXT("");
    return Prefix + CireAbilityDB::Describe(Id, 1);
}
const TArray<FName>& CireSignatureSkills::BuffIds()
{
    static const TArray<FName> Ids = {BountyId, WitchMarkId, TrackedId, BanishedId, StrideId, TalismanId, SprintId, OverchargeId};
    return Ids;
}

// ============================================================================================ casting
bool CireSignatureSkills::Cast(ACireHero* Hero, int32 Slot, const FString& Id)
{
    const FSig* Sig = FindSig(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!IsValid(Hero) || !Hero->HasAuthority() || !Sig || !Def || Sig->D == EDelivery::Passive || !CireSkillRuntime::Alive(Hero) ||
        !Hero->Skills.IsValidIndex(Slot) || Hero->Skills[Slot] != Id || !Hero->Cooldowns.IsValidIndex(Slot) || Hero->Cooldowns[Slot] > 0 || Hero->GlobalCooldown > 0) return false;
    UWorld* World = Hero->GetWorld();
    auto* Mode = World->GetAuthGameMode<ACireGameMode>();
    if (!Mode || !Mode->IsCombatPhase()) return false;
    auto Fail = [&](const FString& Message) { Hero->Notice = Message; return false; };
    const float Mana = Def->Base.ManaCost, Energy = Def->Base.EnergyCost;
    if (!CireSkillShop::CanPayCast(Hero, Id, Mana, Energy)) return Fail(TEXT("Not enough mana or energy."));
    const float Range = Def->Range > 0 ? Def->Range : 900.f;
    const float Power = Mode->Power(Hero->TeamId);
    const float Amount = FMath::Min(10000.f, (Def->Base.Effect + Sig->Scaling * Hero->PrimaryAttribute()) * Power);
    AActor* Target = Hero->Target;
    const bool bHostile = CireCombat::AreHostile(Hero, Target);
    FVector Aim = Hero->bHasCastAim ? Hero->CastAimPoint : bHostile ? Target->GetActorLocation() :
        Hero->GetActorLocation() + Hero->GetActorForwardVector().GetSafeNormal2D() * FMath::Min(500.f, Range);
    if (Aim.ContainsNaN()) return Fail(TEXT("Invalid aim."));
    const FVector Origin = Hero->GetActorLocation();
    const FVector Feet = Origin - FVector(0, 0, Hero->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
    const FVector Direction = (Aim - Origin).GetSafeNormal2D().IsNearlyZero() ? Hero->GetActorForwardVector().GetSafeNormal2D() : (Aim - Origin).GetSafeNormal2D();
    auto NeedEnemy = [&](float Reach) { return bHostile && Hero->InRange(Target, Reach + 40.f) && Sight(Hero, Target); };
    auto NeedGround = [&](bool bSight) -> bool
    {
        if (!CireSkillRuntime::InRealmBounds(Mode, Hero->TeamId, Aim) || FVector::DistSquared2D(Origin, Aim) > FMath::Square(Range + 60.f)) { Hero->Notice = TEXT("Aim within your realm and casting range."); return false; }
        if (!GroundAim(Hero, Aim)) { Hero->Notice = TEXT("Aim at supported battlefield ground."); return false; }
        if (bSight && !GroundSight(Hero, Aim)) { Hero->Notice = TEXT("A wall or world object blocks that spot."); return false; }
        return true;
    };
    float CooldownFactor = 1.f;
    bool bCue = true;
    switch (Sig->D)
    {
    case EDelivery::Projectile:
    case EDelivery::Pierce:
    {
        FCireSkillshotSpec S;
        S.Speed = Sig->D == EDelivery::Pierce ? 2200.f : 2600.f; S.Radius = FMath::Clamp(Def->Radius > 0 ? Def->Radius : 28.f, 12.f, 80.f);
        S.MaxRange = Range; S.LifetimeSeconds = Range / S.Speed + .3f; S.Damage = Amount; S.WarningSeconds = .05f; S.CastRange = Range;
        S.HitLimit = FMath::Max(1, Sig->Hits);
        S.PlayerCollision = S.MonsterCollision = Sig->D == EDelivery::Pierce ? ECireProjectileCollision::Pierce : ECireProjectileCollision::Stop;
        S.VisualStyle = Sig->Style ? Sig->Style : TEXT("arcane"); S.Color = SchoolTint(*Def); S.Color.A = .9f; S.AbilityName = Def->Name;
        const FVector ShotAim = Origin + Direction * Range;
        if (!ACireSkillshot::Spawn(Hero, S, ShotAim, Def->Name)) return Fail(TEXT("Cannot fire from here."));
        Aim = ShotAim; break;
    }
    case EDelivery::Cone:
    {
        FCireAreaSpec A = Area(*Def, ECireAreaShape::Cone, Def->Radius, .12f, Amount); A.ConeAngleDegrees = FMath::Clamp(Sig->Angle, 10.f, 170.f);
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot unleash that here."));
        Hero->SetActorRotation(Direction.Rotation()); Aim = Origin + Direction * Def->Radius; break;
    }
    case EDelivery::Circle:
    {
        if (!NeedGround(true)) return false;
        if (!ACireAreaEffect::Spawn(Hero, Area(*Def, ECireAreaShape::Circle, Def->Radius, Sig->Warning, Amount), Aim, FRotator::ZeroRotator)) return Fail(TEXT("Cannot create that area here."));
        break;
    }
    case EDelivery::Storm:
    {
        FCireAreaSpec A = Area(*Def, ECireAreaShape::Circle, Def->Radius, 0.f, 0.f);
        A.bPersistent = true; A.DurationSeconds = FMath::Clamp(Def->Duration, .5f, 20.f); A.TickInterval = .5f; A.DamagePerSecond = Amount / .5f;
        if (!ACireAreaEffect::Spawn(Hero, A, Feet, Direction.Rotation())) return Fail(TEXT("Cannot raise the storm here."));
        Aim = Feet; break;
    }
    case EDelivery::SelfBurst:
    {
        for (AActor* U : Enemies(Hero, Origin, Def->Radius))
        {
            CireCombat::ApplyDamage(Hero, U, Amount, Def->Name);
            if (auto* M = Cast<ACireMonster>(U)) CireThreat::Taunt(M, Hero, 2.f);
        }
        Aim = Feet; break;
    }
    case EDelivery::Flurry:
    {
        // Three falchion slashes a beat apart around wherever the hunter stands at each beat.
        TWeakObjectPtr<ACireHero> Weak(Hero); const FString Name = Def->Name; const float Radius = Def->Radius; const int32 Hits = FMath::Max(1, Sig->Hits);
        for (int32 Index = 0; Index < Hits; ++Index)
            Later(World, .02f + Index * .18f, [Weak, Name, Radius, Amount]()
            {
                ACireHero* H = Weak.Get(); if (!CireSkillRuntime::Alive(H)) return;
                for (AActor* U : Enemies(H, H->GetActorLocation(), Radius)) CireCombat::ApplyStrike(H, U, Amount, Name);
                CireCombat::PlayCue(H, nullptr, TEXT("blade_flurry"), H->GetActorLocation(), H->GetActorLocation() + H->GetActorForwardVector() * 120, ECireSpellCue::Impact, .8f, false);
            });
        Aim = Feet; break;
    }
    case EDelivery::Strike:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        CireCombat::ApplyDamage(Hero, Target, Amount, Def->Name);
        Aim = Target->GetActorLocation(); break;
    }
    case EDelivery::Lunge:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        const FVector Toward = (Target->GetActorLocation() - Origin).GetSafeNormal2D();
        FVector Ground = Target->GetActorLocation() - Toward * 150.f; GroundAim(Hero, Ground);
        if (FVector::Dist2D(Origin, Target->GetActorLocation()) > 260.f) DashTo(Hero, Ground, Range);
        CireCombat::ApplyStrike(Hero, Target, Amount, Def->Name);
        Aim = Target->GetActorLocation(); break;
    }
    case EDelivery::Chain:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        TArray<AActor*> Hit; AActor* Current = Target; FVector From = Origin; float Damage = Amount;
        for (int32 Bounce = 0; Bounce < FMath::Max(1, Sig->Hits) && IsValid(Current); ++Bounce)
        {
            CireCombat::PlayCue(Hero, Current, FName(*Id), From, Current->GetActorLocation(), ECireSpellCue::Launch, .9f, Bounce == 0);
            CireCombat::ApplyStrike(Hero, Current, Damage, Def->Name);
            Hit.Add(Current); From = Current->GetActorLocation(); Damage *= .8f;
            AActor* Next = nullptr; double Best = TNumericLimits<double>::Max();
            for (AActor* U : Enemies(Hero, From, Def->Radius > 0 ? Def->Radius : 500.f))
            {
                if (Hit.Contains(U) || !Sight(Current, U)) continue;
                const double D = FVector::DistSquared2D(From, U->GetActorLocation()); if (D < Best) { Best = D; Next = U; }
            }
            Current = Next;
        }
        bCue = false; break;
    }
    case EDelivery::Dash:
    case EDelivery::Pounce:
    {
        if (!NeedGround(false)) return false;
        if (!DashTo(Hero, Aim, Range)) return Fail(TEXT("The path is blocked."));
        if (Sig->D == EDelivery::Pounce)
            for (AActor* U : Enemies(Hero, Hero->GetActorLocation(), Def->Radius)) CireCombat::ApplyDamage(Hero, U, Amount, Def->Name);
        if (Sig->Record) CireBuffs::Apply(Hero, Sig->Record, FMath::Max(.5f, Def->Duration), Hero, static_cast<int32>(FMath::Clamp(Def->Base.Effect, 1.f, 250.f)));
        Aim = Hero->GetActorLocation(); break;
    }
    case EDelivery::Mark:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        CireBuffs::Apply(Target, Sig->Record, FMath::Max(1.f, Def->Duration), Hero, static_cast<int32>(FMath::Clamp(Def->Base.Effect, 1.f, 250.f)));
        Aim = Target->GetActorLocation(); break;
    }
    case EDelivery::AreaMark:
    {
        if (!NeedGround(false)) return false;
        for (AActor* U : Enemies(Hero, Aim, Def->Radius, false))
            CireBuffs::Apply(U, Sig->Record, FMath::Max(1.f, Def->Duration), Hero, static_cast<int32>(FMath::Clamp(Def->Base.Effect, 1.f, 250.f)));
        break;
    }
    case EDelivery::SelfBuff:
    {
        CireBuffs::Apply(Hero, Sig->Record, FMath::Max(1.f, Def->Duration), Hero, static_cast<int32>(FMath::Clamp(Def->Base.Effect, 1.f, 250.f)));
        Hero->SlowUntil = 0; Aim = Feet; break;
    }
    case EDelivery::Overcharge:
    {
        if (CireTechConstructs::Overcharge(Hero, Origin, Def->Radius > 0 ? Def->Radius : 1200.f, FMath::Max(1.f, Def->Duration), .25f) == 0)
            return Fail(TEXT("No constructs of yours within range to overcharge."));
        CireBuffs::Apply(Hero, OverchargeId, FMath::Max(1.f, Def->Duration), Hero); Aim = Feet; break;
    }
    case EDelivery::AllyHeal:
    {
        ACireHero* Ally = Cast<ACireHero>(Target);
        if (!Ally || Ally->TeamId != Hero->TeamId || Ally->bDead || !Ally->bDrafted) Ally = Hero;
        if (!Hero->InRange(Ally, Range + 40.f) || (Ally != Hero && !Sight(Hero, Ally))) return Fail(TEXT("Ally is unavailable or out of range."));
        CireCombat::ApplyHealing(Hero, Ally, Amount, Def->Name); Aim = Ally->GetActorLocation(); break;
    }
    case EDelivery::Construct:
    {
        if (!NeedGround(true)) return false;
        FString Why;
        if (CireTechConstructs::Deploy(Hero, FName(*Id), Aim, &Why).IsEmpty()) return Fail(Why);
        break;
    }
    case EDelivery::Banish:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        TWeakObjectPtr<ACireHero> Weak(Hero); TWeakObjectPtr<AActor> Victim(Target); const FString Name = Def->Name;
        const float Seconds = FMath::Max(.5f, Def->Duration);
        if (IsBoss(Target)) { CireCrowdControl::Silence(Target, Seconds * .75f, Hero); CireCrowdControl::Slow(Target, Seconds, Hero); CireCombat::ApplyDamage(Hero, Target, Amount, Name); }
        else
        {
            CireBuffs::Apply(Target, BanishedId, Seconds, Hero);
            CireCrowdControl::Stun(Target, Seconds, Hero);
            Later(World, Seconds, [Weak, Victim, Name, Amount]()
            {
                ACireHero* H = Weak.Get(); AActor* V = Victim.Get();
                if (!CireSkillRuntime::Alive(H) || !IsValid(V)) return;
                CireBuffs::Remove(V, BanishedId);
                CireCombat::ApplyDamage(H, V, Amount, Name);
            });
        }
        Aim = Target->GetActorLocation(); break;
    }
    case EDelivery::Execute:
    {
        if (!NeedEnemy(Range)) return Fail(TEXT("Select a hostile target in range and line of sight."));
        const float Health = HealthOf(Target), Max = MaxHealthOf(Target);
        const bool bMarked = Active(Target, BountyId) != nullptr;
        float Damage = Amount + FMath::Min(400.f, .4f * FMath::Max(0.f, Max - Health));
        if (!IsBoss(Target) && Cast<ACireMonster>(Target) && Max > 0 && Health / Max < .2f) Damage = FMath::Max(Damage, Health * 20.f + 1.f);
        CireCombat::ApplyStrike(Hero, Target, Damage, Def->Name);
        if (!CireCombat::IsAlive(Target))
        {
            CooldownFactor = .5f;
            if (bMarked) if (const auto* M = Cast<ACireMonster>(Target)) { Hero->Gold += BountyGold(M); Hero->Notice = TEXT("Bounty collected twice."); }
        }
        Aim = Target->GetActorLocation(); break;
    }
    default: return false;
    }
    Hero->Mana -= Mana; Hero->Energy -= Energy;
    Hero->Cooldowns[Slot] = static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(World, Def->Base.Cooldown), Hero->CDR));
    CireSkillShop::ApplyCastLevel(Hero, Slot, Id, Mana, Energy);
    Hero->Cooldowns[Slot] *= CooldownFactor;
    Hero->GlobalCooldown = .9f;
    if (!Hero->Notice.StartsWith(TEXT("Bounty"))) Hero->Notice = Def->Name;
    Hero->ForceNetUpdate();
    if (bCue) CireCombat::PlayCue(Hero, bHostile ? Target : nullptr, FName(*Id), Origin, Aim, ECireSpellCue::Cast);
    UE_LOG(LogCireSignature, Verbose, TEXT("CIRE_SIGNATURE_CAST %s by %s"), *Id, *Hero->HeroName);
    return true;
}

// ============================================================================================ shapes
bool CireSignatureSkills::DescribeShape(const FString& Id, FCireHitShape& R)
{
    const FSig* Sig = FindSig(Id);
    const FCireAbilityDef* Def = CireAbilityDB::Find(Id);
    if (!Sig || !Def) return false;
    const float Range = Def->Range > 0 ? Def->Range : 900.f;
    auto Circle = [&](float Radius, bool bSelf) { R.Kind = ECireHitShape::Circle; R.Radius = FMath::Max(40.f, Radius); R.bFromCaster = bSelf; R.bAtTarget = !bSelf; };
    R.LingerSeconds = .6f;
    switch (Sig->D)
    {
    case EDelivery::Projectile: case EDelivery::Pierce:
        R.Kind = ECireHitShape::Line; R.bFromCaster = true; R.bProjectile = true; R.bGroundAim = true;
        R.Width = FMath::Max(24.f, (Def->Radius > 0 ? Def->Radius : 28.f) * 2.f); R.Length = Range; R.Speed = Sig->D == EDelivery::Pierce ? 2200.f : 2600.f; R.WarningSeconds = .05f; R.LingerSeconds = .4f; break;
    case EDelivery::Cone:
        R.Kind = ECireHitShape::Cone; R.bFromCaster = true; R.bGroundAim = true; R.Radius = Def->Radius; R.Angle = Sig->Angle; R.WarningSeconds = .12f; break;
    case EDelivery::Circle: Circle(Def->Radius, false); R.bGroundAim = true; R.WarningSeconds = Sig->Warning; R.LingerSeconds = .4f; break;
    case EDelivery::Storm: Circle(Def->Radius, true); R.LingerSeconds = Def->Duration; break;
    case EDelivery::SelfBurst: case EDelivery::Flurry: Circle(Def->Radius, true); break;
    case EDelivery::Chain: R.Kind = ECireHitShape::Chain; R.Radius = Def->Radius > 0 ? Def->Radius : 500.f; R.bAtTarget = true; break;
    case EDelivery::Dash: Circle(70.f, false); R.bGroundAim = true; R.bHostileOnly = false; break;
    case EDelivery::Pounce: Circle(Def->Radius, false); R.bGroundAim = true; break;
    case EDelivery::AreaMark: Circle(Def->Radius, false); R.bGroundAim = true; R.LingerSeconds = 1.f; break;
    case EDelivery::SelfBuff: case EDelivery::Overcharge: R.Kind = ECireHitShape::Self; R.bHostileOnly = false; R.LingerSeconds = 1.f; break;
    case EDelivery::AllyHeal: R.Kind = ECireHitShape::Unit; R.bHostileOnly = false; R.bHeal = true; R.LingerSeconds = .8f; break;
    case EDelivery::Construct:
    {
        const FCireTechRecipe* X = CireTechConstructs::FindRecipe(FName(*Id));
        const float Radius = !X ? 80.f : X->Kind == ECireConstructKind::Pylon ? X->Radius : X->Kind == ECireConstructKind::Turret ? X->Range :
            X->Kind == ECireConstructKind::Skitter ? 180.f : FMath::Max(X->Trigger, X->Radius);
        Circle(Radius, false); R.bGroundAim = true;
        R.bHostileOnly = X && X->Kind == ECireConstructKind::Pylon ? (X->Effect == TEXT("weaken") || X->Effect == TEXT("slow")) : true;
        R.LingerSeconds = FMath::Min(2.f, X ? X->Lifetime : 2.f); break;
    }
    case EDelivery::Passive: R.Kind = ECireHitShape::None; break;
    default: R.Kind = ECireHitShape::Unit; break; // strike, lunge, mark, banish, execute
    }
    return true;
}

// ============================================================================================ hooks
float CireSignatureSkills::ModifyOutgoingDamage(AActor* Source, AActor* Target, float Amount, const FString& AbilityName)
{
    if (!IsValid(Target) || Amount <= 0) return Amount;
    if (Active(Target, BanishedId) && AbilityName != TEXT("Banishment")) return 0.f; // exiled: out of reach until it returns
    float M = 1.f;
    if (const auto* E = Active(Target, BountyId)) M *= 1.f + E->Stacks / 100.f;
    if (const auto* E = Active(Target, WitchMarkId)) M *= 1.f + E->Stacks / 100.f * (IsCasting(Target) ? 3.f : 1.f);
    if (const auto* E = Active(Target, TrackedId); E && E->Source == Source) M *= 1.f + E->Stacks / 100.f;
    if (const auto* E = Active(Target, TalismanId)) M *= 1.f - FMath::Clamp(E->Stacks / 100.f, 0.f, .8f);
    if (const auto* E = Active(Target, AegisId))
    {
        M *= 1.f - FMath::Clamp(E->Stacks / 100.f, 0.f, .8f);
        if (const auto* Warden = Cast<ACireHero>(E->Source.Get()); Warden && Warden->HasSkill(TEXT("resonant_lattice"))) M *= .9f;
    }
    if (const auto* E = Active(Target, NexusId))
    {
        M *= 1.f - FMath::Clamp(E->Stacks / 100.f, 0.f, .8f);
        if (const auto* Warden = Cast<ACireHero>(E->Source.Get()); Warden && Warden->HasSkill(TEXT("resonant_lattice"))) M *= .9f;
    }
    if (Cast<ACireMonster>(Target) && Active(Target, EmpoweredId)) M *= .85f;
    if (Source)
    {
        if (const auto* E = Active(Source, WeakenedId)) M *= 1.f - FMath::Clamp(E->Stacks / 100.f, 0.f, .8f);
        if (const auto* E = Active(Source, EmpoweredId)) M *= 1.f + E->Stacks / 100.f;
    }
    if (AbilityName == TEXT("Silver Shot") && IsUndeadOrVoid(Target)) M *= 1.6f;
    if (AbilityName == TEXT("Arcane Blunderbuss") && IsCasting(Target)) M *= 1.4f;
    if (const auto* Slayer = Cast<ACireHero>(Source); Slayer && Slayer->HasSkill(TEXT("witchbane")) &&
        (IsCasting(Target) || CireCrowdControl::IsSilenced(Target) || Active(Target, WitchMarkId))) M *= 1.2f;
    return Amount * M;
}

void CireSignatureSkills::OnAbilityHit(AActor* Source, AActor* Target, const FString& AbilityName, float Applied)
{
    if (!IsValid(Source) || !Source->HasAuthority() || !IsValid(Target) || Applied <= 0) return;
    const FCireAbilityDef* D = CireAbilityDB::FindByName(AbilityName);
    if (!D || !Knows(D->Id)) return;
    for (const FCireAbilityEffect& E : D->Effects)
    {
        if (E.Zone == TEXT("self")) continue;
        if (E.Type == TEXT("slow")) CireCrowdControl::Slow(Target, E.Duration, Source);
        else if (E.Type == TEXT("purge")) Purge(Target);
    }
}

void CireSignatureSkills::OnMonsterKilled(ACireMonster* M, ACireHero* Killer)
{
    if (!IsValid(M) || !M->HasAuthority()) return;
    if (const auto* E = Active(M, BountyId))
        if (auto* Hunter = Cast<ACireHero>(E->Source.Get()); Hunter && !Hunter->bDead && Hunter->bDrafted)
        {
            const int32 Gold = BountyGold(M);
            Hunter->Gold += Gold; Hunter->Notice = FString::Printf(TEXT("Bounty collected: +%d gold"), Gold); Hunter->ForceNetUpdate();
            UE_LOG(LogCireSignature, Display, TEXT("CIRE_BOUNTY_PAID hunter=%s gold=%d"), *Hunter->HeroName, Gold);
        }
    if (Killer && Killer->HasSkill(TEXT("price_on_every_soul")))
    {
        const auto* D = CireAbilityDB::Find(TEXT("price_on_every_soul"));
        const int32 Base = D ? FMath::RoundToInt(D->Base.Effect) : 5;
        Killer->Gold += Base * (IsBoss(M) ? 8 : CireRaces::RankOf(M) >= ECireNPCRank::Elite ? 3 : 1);
        Killer->Energy = FMath::Min(100.f, Killer->Energy + 15.f); Killer->ForceNetUpdate();
    }
}

float CireSignatureSkills::MoveSpeedMultiplier(const ACireHero* Hero)
{
    if (!Hero) return 1.f;
    return (1.f + Fraction(Hero, SprintId)) * (1.f + Fraction(Hero, HasteId));
}
float CireSignatureSkills::AttackSpeedBonus(const ACireHero* Hero) { return Hero ? Fraction(Hero, HasteId) : 0.f; }

bool CireSignatureSkills::IsCloseQuarters(const ACireHero* Hero, const AActor* Target)
{
    return Hero && IsValid(Target) && Hero->BasicAttackStyle() == TEXT("gunblade") && Hero->InRange(const_cast<AActor*>(Target), GunbladeMeleeRange);
}

float CireSignatureSkills::ModifyBasicAttack(ACireHero* Hero, AActor* Target, float Damage, bool& bRanged, FString& Name)
{
    if (!Hero) return Damage;
    const FString Style = Hero->BasicAttackStyle();
    if (Style == TEXT("gunblade"))
    {
        if (IsCloseQuarters(Hero, Target)) { bRanged = false; Damage *= 1.15f; Name = TEXT("Falchion slash"); }
        else Name = TEXT("Pistol shot");
    }
    if (const auto* E = Active(Hero, StrideId)) { Damage *= 1.f + E->Stacks / 100.f; CireBuffs::Remove(Hero, StrideId); }
    return Damage;
}

void CireSignatureSkills::OnBasicProjectileHit(ACireHero* Hero, AActor* Victim, float Damage, bool bHit)
{
    if (!bHit || !IsValid(Hero) || !Hero->HasAuthority() || !IsValid(Victim) || !Hero->HasSkill(TEXT("moon_glaive"))) return;
    const auto* D = CireAbilityDB::Find(TEXT("moon_glaive"));
    float Next = Damage * FMath::Clamp((D ? D->Base.Effect : 60.f) / 100.f, .1f, 1.f);
    TArray<AActor*> Hit = {Victim}; AActor* From = Victim;
    for (int32 Bounce = 0; Bounce < 2; ++Bounce)
    {
        AActor* Best = nullptr; double BestD = TNumericLimits<double>::Max();
        for (AActor* U : Enemies(Hero, From->GetActorLocation(), 450.f, false))
        {
            if (Hit.Contains(U) || !Sight(From, U)) continue;
            const double Dist = FVector::DistSquared2D(From->GetActorLocation(), U->GetActorLocation()); if (Dist < BestD) { BestD = Dist; Best = U; }
        }
        if (!Best) break;
        CireCombat::PlayCue(Hero, Best, TEXT("moon_glaive"), From->GetActorLocation(), Best->GetActorLocation(), ECireSpellCue::Launch, .7f, false);
        CireCombat::ApplyStrike(Hero, Best, Next, D ? D->Name : TEXT("Moon Glaive"));
        Hit.Add(Best); From = Best; Next *= .6f;
    }
}
