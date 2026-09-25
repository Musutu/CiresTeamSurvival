#include "CireDeveloperTools.h"
#include "CireSkillShop.h" // progression-shop: game mode
#include "CireCrowdControl.h" // champion-draft: crowd control, timed casts, execute skills
#include "CireRaces.h" // monster-races
#include "CireGame.h"
#include "CirePolymorph.h" // progression-shop: Polymorph
#include "CireCombatEvents.h"
#include "CireRealm.h"
#include "CireTownGoal.h"
#include "CireChampionArt.h"
#include "CireMobility.h"
#include "CireChampionProfiles.h"
#include "CireClassTraits.h"
#include "CireAttackSystem.h"
#include "CireAreaEffects.h"
#include "CireAbilityLibrary.h"
#include "CireSkillTuning.h"
#include "CireSkillCasting.h"
#include "CireSignatureSkills.h" // new-champions
#include "CireRoleSkills.h"
#include "CireConstruct.h"
#include "CireSkillshot.h"
#include "CireSummon.h"
#include "CireThreat.h"
#include "CireNPCCombat.h"
#include "CireWaves.h" // wave-director
#include "CireNav.h" // nav-paths
#include "CireNPCState.h"
#include "CireStatusVisual.h"
#include "CireBuffs.h" // aura-vfx
#include "CireItems.h" // progression-shop
#include "CireMonsterArt.h" // creature-anim
#include "EngineUtils.h"

#include "Camera/CameraComponent.h"
#include "Animation/AnimInstance.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DamageEvents.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
ACireGameMode* ModeFor(const AActor* Actor)
{
    return Actor && Actor->GetWorld() ? Actor->GetWorld()->GetAuthGameMode<ACireGameMode>() : nullptr;
}

bool ClearSight(const AActor* From, const AActor* To)
{
    if (!IsValid(From) || !IsValid(To)) return false;
    FHitResult Hit;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(CireCombatSight), false, From);
    const bool bBlocked = From->GetWorld()->LineTraceSingleByChannel(Hit,
        From->GetActorLocation() + FVector(0, 0, 45),
        To->GetActorLocation() + FVector(0, 0, 35), ECC_Visibility, Query);
    return !bBlocked || Hit.GetActor() == To;
}

float BasicRange(const ACireHero* Hero) {
    return Hero->BasicAttackRange();
}

void Slow(AActor* Actor, float Until)
{
    if (auto* Hero = ::Cast<ACireHero>(Actor)) Hero->SlowUntil = FMath::Max(Hero->SlowUntil, Until);
    else if (auto* Monster = ::Cast<ACireMonster>(Actor)) Monster->SlowUntil = FMath::Max(Monster->SlowUntil, Until);
}
} // namespace

ACireHero::ACireHero()
{
    PrimaryActorTick.bCanEverTick = true;
    ChampionArt = CreateDefaultSubobject<UCireChampionArt>(TEXT("ChampionArt"));
    Mobility = CreateDefaultSubobject<UCireMobility>(TEXT("Mobility"));
    CreateDefaultSubobject<UCireBuffState>(TEXT("BuffState")); // aura-vfx: replicated named-effect records for signature visuals
    Inventory = CreateDefaultSubobject<UCireInventory>(TEXT("Inventory")); // progression-shop
    bReplicates = true;
    SetReplicateMovement(true);
    GetCapsuleComponent()->InitCapsuleSize(40.f, 92.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCharacterMovement()->MaxWalkSpeed = 520.f;
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    GetCharacterMovement()->RotationRate = FRotator(0, 600, 0);
    GetCharacterMovement()->JumpZVelocity = 520.f;
    bUseControllerRotationYaw = false;
    Arm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    Arm->SetupAttachment(RootComponent);
    Arm->TargetArmLength = 650.f;
    Arm->SocketOffset = FVector(0, 0, 90);
    Arm->bUsePawnControlRotation = true;
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(Arm, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(
        TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    if (Body.Succeeded())
    {
        GetMesh()->SetSkeletalMesh(Body.Object);
        GetMesh()->SetRelativeLocation(FVector(0, 0, -92));
        GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    }
    static ConstructorHelpers::FClassFinder<UAnimInstance> Animation(
        TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    if (Animation.Succeeded()) GetMesh()->SetAnimInstanceClass(Animation.Class);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ACireHero::BeginPlay()
{
    Super::BeginPlay();
    HomePosition = GetActorLocation();
    CireStatusVisual::Attach(this);
}

void ACireHero::FellOutOfWorld(const UDamageType& DamageType)
{
    if (!HasAuthority()) return;
    auto* Mode = ModeFor(this);
    if (!Mode) { Super::FellOutOfWorld(DamageType); return; }
    // Preserve the possessed actor and build after an unexpected geometry failure.
    if (Mode->Clock.Phase() != Cires::MatchPhase::Arena)
    {
        ReviveAt(Mode->BasePosition(TeamId));
        Notice = TEXT("Recovered at town after leaving the battlefield.");
    }
    else
    {
        bDead = true; Health = 0; Target = nullptr; bAutoAttack = false;
        PendingAttackTarget.Reset();
        ACireAreaEffect::ClearForActor(this);
        CireThreat::Remove(this);ACireSummon::ClearForActor(this);ACireSkillshot::ClearForActor(this);ACireConstruct::ClearForActor(this);
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        SetActorLocation(Mode->ArenaPosition(TeamId, 2), false, nullptr, ETeleportType::TeleportPhysics);
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mode->HeroKilled(this);
        Notice = TEXT("Eliminated: left the arena bounds.");
    }
}

void ACireHero::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireHero, TeamId);
    DOREPLIFETIME(ACireHero, Archetype);
    DOREPLIFETIME(ACireHero, CastSkill); DOREPLIFETIME(ACireHero, CastStartTime); DOREPLIFETIME(ACireHero, CastEndTime); // champion-draft
    DOREPLIFETIME(ACireHero, ChampionProfileId);
    DOREPLIFETIME(ACireHero, StatPrimaryOverride);
    DOREPLIFETIME(ACireHero, ProfileBasicAttackRange);
    DOREPLIFETIME(ACireHero, ProfileAttackSeconds);
    DOREPLIFETIME(ACireHero, ProfileAttackStyle);
    DOREPLIFETIME(ACireHero, ProfileThreatRole);
    DOREPLIFETIME(ACireHero, ProfileRoles);
    DOREPLIFETIME(ACireHero, bBot);
    DOREPLIFETIME(ACireHero, bDrafted);
    DOREPLIFETIME(ACireHero, DraftDeadline);
    DOREPLIFETIME(ACireHero, DraftTimerTotal);
    DOREPLIFETIME(ACireHero, DraftHoverId);
    DOREPLIFETIME(ACireHero, bDead);
    DOREPLIFETIME(ACireHero, bAutoAttack);
    DOREPLIFETIME(ACireHero, HeroName);
    DOREPLIFETIME(ACireHero, Health);
    DOREPLIFETIME(ACireHero, MaxHealth);
    DOREPLIFETIME(ACireHero, Mana);
    DOREPLIFETIME(ACireHero, MaxMana);
    DOREPLIFETIME(ACireHero, Energy);
    DOREPLIFETIME(ACireHero, Level);
    DOREPLIFETIME(ACireHero, Strength);
    DOREPLIFETIME(ACireHero, Agility);
    DOREPLIFETIME(ACireHero, Intelligence);
    DOREPLIFETIME(ACireHero, Gold);
    DOREPLIFETIME(ACireHero, Experience);
    DOREPLIFETIME(ACireHero, GearRank);
    DOREPLIFETIME(ACireHero, CDR);
    DOREPLIFETIME(ACireHero, Skills);
    DOREPLIFETIME(ACireHero, Offers);
    DOREPLIFETIME(ACireHero, Cooldowns);
    DOREPLIFETIME(ACireHero, Target);
    DOREPLIFETIME(ACireHero, Notice);
    DOREPLIFETIME(ACireHero, SlowUntil);
    DOREPLIFETIME(ACireHero, ShieldUntil);
    DOREPLIFETIME(ACireHero, TauntUntil);
    DOREPLIFETIME(ACireHero, DamageDone);
    DOREPLIFETIME(ACireHero, HealingDone);
    DOREPLIFETIME(ACireHero, PoisonAreaCount); DOREPLIFETIME(ACireHero, PoisonEndsAt); DOREPLIFETIME(ACireHero, CriticalChance); DOREPLIFETIME(ACireHero, CriticalMultiplier);
    DOREPLIFETIME(ACireHero, AttackSerial);
    DOREPLIFETIME(ACireHero, AttackAimLocation);
    DOREPLIFETIME(ACireHero, AttackStartedServerTime);
    DOREPLIFETIME(ACireHero, AttackDuration);
}

void ACireHero::Draft(int32 Choice)
{
    if (!HasAuthority() || bDrafted || bDead || Choice < 0 || Choice > 4) return;
    if (DraftProfile(CireChampionProfiles::LegacyProfileId(Choice))) return;
    // A missing/invalid catalogue must not remove the five original choices.
    ChampionProfileId.Reset();StatPrimaryOverride=INDEX_NONE;ProfileBasicAttackRange=0;ProfileAttackSeconds=0;
    ProfileAttackStyle.Reset();ProfileThreatRole.Reset();ProfileRoles.Reset();
    Skills.Reset();Cooldowns.Reset();Offers.Reset();CurrentOffer={};
    Archetype = Choice;
    Progression = Cires::Progression{};
    Progression.Primary = static_cast<Cires::PrimaryStat>(Choice == 3 ? 1 : Choice==4?2:Choice);
    Progression.Stats = {10, 10, 10};
    if (Choice == 0) Progression.Stats.Strength = 20;
    if (Choice == 1 || Choice == 3) Progression.Stats.Agility = 20;
    if (Choice == 2 || Choice == 4) Progression.Stats.Intelligence = 20;
    HeroName = Choice == 0 ? TEXT("Iron Warden") : Choice == 1 ? TEXT("Ash Ranger") : Choice == 3 ? TEXT("Lancer") : Choice==4?TEXT("Rift Summoner"):TEXT("Veil Scholar");
    bDrafted = true;
    Recalculate(true);
    Notice = TEXT("Champion bound. Choose your opening ability.");
    RefreshOffer(); // champion-draft: one starting skill point
}

void ACireHero::Recalculate(bool bFill)
{
    if (!HasAuthority()) return;
    const float OldMaxHealth = MaxHealth;
    const float OldMaxMana = MaxMana;
    CriticalChance=CireSkillTuning::Get().CritChance;
    CriticalChance=CireClassTraits::CriticalChance(this,CriticalChance); // champion-draft: DPS class trait (10% base crit)
    CriticalMultiplier=CireSkillTuning::Get().CritMultiplier;
    Level = Progression.Level;
    // progression-shop: equipment attributes, health/mana, CDR and crit join the single stat pipeline.
    Cires::StatBlock Attributes = Progression.Stats;
    CireItems::AddAttributes(this, Attributes);
    Strength = Attributes.Strength;
    Agility = Attributes.Agility;
    Intelligence = Attributes.Intelligence;
    Cires::CombatTuning Tuning;
    Tuning.WeaponDamage = 12;
    Tuning.PureCooldownReduction = CireItems::CooldownReductionFor(this, CDR);
    const auto Stats = Cires::CalculateStats(Attributes, Progression.Primary, Tuning);
    MaxHealth = static_cast<float>(Stats.MaxHealth);
    MaxMana = static_cast<float>(Stats.MaxMana);
    CDR = static_cast<float>(1.0 - Stats.CooldownMultiplier);
    CireItems::ApplyDerived(this); // progression-shop: +health, +mana, +crit
    Health = bFill ? MaxHealth : FMath::Clamp(Health + MaxHealth - OldMaxHealth, 0.f, MaxHealth);
    Mana = bFill ? MaxMana : FMath::Clamp(Mana + MaxMana - OldMaxMana, 0.f, MaxMana);
    if (bFill) Energy = 100;
}

void ACireHero::GrantExperience(int32 Amount)
{
    if (!HasAuthority() || !bDrafted || Amount <= 0) return;
    Experience = static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Experience) + Amount));
    bool bLeveled = false;
    while (Progression.Level < 10000 && static_cast<int64>(Experience) >= 120LL + Progression.Level * 60LL)
    {
        const int32 Cost = 120 + Progression.Level * 60;
        if (!Cires::GainLevels(Progression)) break;
        Experience -= Cost;
        bLeveled = true;
    }
    if (bLeveled)
    {
        Recalculate(false);
        Notice = FString::Printf(TEXT("Level %d: +2 primary, +1 other stats. New skills: Skill Shop between waves."), Level);
        RefreshOffer();
    }
}

void ACireHero::RefreshOffer()
{
    if (!HasAuthority() || !bDrafted || !Offers.IsEmpty() || !Cires::HasPendingAugment(Progression)) return;
    // progression-shop: after the free opening role pick, skills come from the Skill Shop
    // (Eric's playtest-2 ruling); level-ups only raise stats.
    if (!Skills.IsEmpty() && CireSkillShop::IsSkillShopMode(GetWorld())) return;
    const std::uint64_t Seed = static_cast<std::uint64_t>(FMath::Rand()) ^
        (static_cast<std::uint64_t>(GetUniqueID()) << 32) ^
        static_cast<std::uint64_t>(Progression.NextAugmentLevel);
    Progression.DraftRole = CireChampionProfiles::DraftRole(this);
    // champion-draft: hybrids draw from primary + secondary role tags; the rules filter the full catalog.
    Progression.SecondaryRoles = CireChampionProfiles::SecondaryRoles(this);
    CurrentOffer = Cires::GenerateAugmentOffer(Progression, Cires::StarterSkillPool(), Seed);
    if (!CurrentOffer.IsValid())
    {
        Notice = UTF8_TO_TCHAR(CurrentOffer.Error.c_str());
        return;
    }
    Offers.Empty();
    for (const auto& Option : CurrentOffer.Choices) Offers.Add(UTF8_TO_TCHAR(Option.Id.c_str()));
    Notice = FString::Printf(TEXT("Level %d skill choice ready."), CurrentOffer.BreakpointLevel);
}

void ACireHero::Learn(int32 Choice)
{
    if (!HasAuthority() || !bDrafted || !Offers.IsValidIndex(Choice)) return;
    const FString Chosen = Offers[Choice];
    if (!Cires::LearnSkill(Progression, CurrentOffer, TCHAR_TO_UTF8(*Chosen))) return;
    Skills.Add(Chosen);
    Cooldowns.Add(0);
    Offers.Empty();
    CurrentOffer = {};
    Notice = TEXT("Learned ") + SkillName(Chosen);
    RefreshOffer();
}

bool ACireHero::HasSkill(const FString& Id) const { return Skills.Contains(Id); }

int32 ACireHero::ActiveSkillSlot(int32 Ordinal) const
{
    if (Ordinal < 0 || Ordinal >= Cires::MaxActiveSkills) return INDEX_NONE;
    int32 Active = 0;
    for (int32 Slot = 0; Slot < Skills.Num(); ++Slot)
        if (!IsPassive(Skills[Slot]) && !IsUltimate(Skills[Slot]) && Active++ == Ordinal) return Slot;
    return INDEX_NONE;
}

int32 ACireHero::UltimateSkillSlot() const
{
    for (int32 Slot = 0; Slot < Skills.Num(); ++Slot) if (IsUltimate(Skills[Slot])) return Slot;
    return INDEX_NONE;
}

bool ACireHero::IsUltimate(const FString& Id)
{
    return CireSignatureSkills::IsUltimate(Id) || CireRoleSkills::IsUltimate(Id) || Id == TEXT("bastion_of_dawn") || Id == TEXT("cataclysm") ||
        Id == TEXT("executioners_verdict") || Id == TEXT("renewal");
}

bool ACireHero::IsPassive(const FString& Id)
{
    return CireSignatureSkills::IsPassive(Id) || Id == TEXT("stone_skin") || Id == TEXT("battle_rhythm") ||
        Id == TEXT("deep_reserves") || Id == TEXT("soul_conduit") || Id == TEXT("executioner"); // champion-draft: Executioner passive
}

bool ACireHero::IsHostile(AActor* Other) const
{
    if (!IsValid(Other) || Other == this || bDead || !bDrafted) return false;
    if(auto* Construct=::Cast<ACireConstruct>(Other))return Construct->CanBeDamagedBy(const_cast<ACireHero*>(this));
    const auto* Mode = ModeFor(this);
    const auto* State = GetWorld()->GetGameState<ACireGameState>();
    const auto Phase = Mode ? Mode->Clock.Phase() : State ? static_cast<Cires::MatchPhase>(State->Phase) : Cires::MatchPhase::Finished;
    if (auto* Hero = ::Cast<ACireHero>(Other))
        return Phase == Cires::MatchPhase::Arena && Hero->TeamId != TeamId && Hero->bDrafted && !Hero->bDead;
    if (auto* Monster = ::Cast<ACireMonster>(Other))
        return Phase == Cires::MatchPhase::Survival && Monster->Lane == TeamId && Monster->Health > 0;
    return false;
}

bool ACireHero::InRange(AActor* Other, float Distance) const
{
    return IsValid(Other) && FVector::DistSquared2D(GetActorLocation(), Other->GetActorLocation()) <= FMath::Square(Distance);
}

float ACireHero::AttackDamage() const
{
    // Replicated fields make client-side tooltips agree with authoritative damage.
    Cires::CombatTuning Tuning;
    Tuning.WeaponDamage = 12;
    const auto Stats = Cires::CalculateStats({Strength, Agility, Intelligence},
        PrimaryStat(), Tuning);
    const auto* Mode = ModeFor(this);
    float Power = Mode ? Mode->Power(TeamId) : 1.f;
    if (!Mode)
    {
        if (const auto* State = GetWorld()->GetGameState<ACireGameState>())
            Power += FMath::Min(TeamId == 0 ? State->EmberWins : State->DuskWins, 4) * 0.03f;
    }
    return (static_cast<float>(Stats.BasicAttackDamage) + CireItems::AttackDamageBonus(this)) * Power; // progression-shop: item attack damage
}

void ACireHero::BasicAttack()
{
    if(Mobility&&Mobility->IsRolling())return;
    if(CireCrowdControl::IsStunned(this))return; // champion-draft: stunned units cannot attack
    const auto* Mode = ModeFor(this);
    if (!HasAuthority() || !Mode || !Mode->IsCombatPhase() || bDead || !bDrafted || BasicTimer > 0 ||
        !IsHostile(Target) || !InRange(Target, BasicRange(this)) || !ClearSight(this, Target)) return;
    const float PassiveSpeed = HasSkill(TEXT("battle_rhythm")) ? 1.20f : 1.f;
    BasicTimer = BaseAttackSeconds() / ((1.f + Agility * 0.01f + CireItems::AttackSpeedBonus(this) + CireClassTraits::AttackSpeedBonus(this) + CireSignatureSkills::AttackSpeedBonus(this)) * PassiveSpeed); // new-champions: haste pylons // progression-shop: item attack speed; champion-draft: Support +10%
    AttackDuration = FMath::Min(.65f, BasicTimer);
    AttackReleaseTimer = AttackDuration * (.25f / .65f);
    PendingAttackTarget = Target;
    PendingAttackDamage = AttackDamage();
    AttackAimLocation = Target->GetActorLocation();
    AttackStartedServerTime = GetWorld()->GetTimeSeconds();
    SetActorRotation((Target->GetActorLocation()-GetActorLocation()).GetSafeNormal2D().Rotation());
    if (++AttackSerial == 0) ++AttackSerial;
    ForceNetUpdate();
}

void ACireHero::CastAt(int32 Slot,FVector Aim)
{
    if(!HasAuthority()||Aim.ContainsNaN()||FVector::DistSquared(Aim,GetActorLocation())>FMath::Square(6500.f))return;
    bHasCastAim=true;CastAimPoint=Aim;Cast(Slot);bHasCastAim=false;
}
void ACireHero::Cast(int32 Slot)
{
    auto* Mode = ModeFor(this);
    if (!HasAuthority() || !Mode || !Mode->IsCombatPhase() || bDead || !bDrafted ||
        (Mobility&&Mobility->IsRolling()) || GlobalCooldown > 0 || !Skills.IsValidIndex(Slot) || !Cooldowns.IsValidIndex(Slot) || Cooldowns[Slot] > 0) return;
    const FString Id = Skills[Slot];
    if (CireRaces::IsSilenced(this) && !IsPassive(Id)) { Notice = TEXT("Silenced: you cannot cast right now."); return; } // monster-races
    if (CireCrowdControl::GateCast(this, Slot, Id)) return; // champion-draft: stun/silence/lockout gates and timed casts
    if (CireCrowdControl::HandlesSkill(Id)) { CireCrowdControl::CastSkill(this, Slot, Id); return; } // champion-draft: Decimating Strike
    if(CireSkillCasting::Handles(Id)){CireSkillCasting::Cast(this,Slot,Id);return;}
    if (const auto* Authored = CireAbilityLibrary::Find(Id)) { CireAbilityLibrary::Cast(this, Slot, *Authored); return; }
    if (IsPassive(Id)) { Notice = TEXT("This passive is always active."); return; }
    float ManaCost = 0, EnergyCost = 0, Cooldown = 0, Range = 0;
    bool bNeedsEnemy = false;
    if (Id == TEXT("iron_guard")) { EnergyCost = 25; Cooldown = 14; }
    else if (Id == TEXT("shield_slam")) { EnergyCost = 25; Cooldown = 7; Range = 240; bNeedsEnemy = true; }
    else if (Id == TEXT("war_cry")) { EnergyCost = 30; Cooldown = 18; }
    else if (Id == TEXT("chain_spark")) { ManaCost = 45; Cooldown = 10; Range = 1200; bNeedsEnemy = true; }
    else if (Id == TEXT("ember_lance")) { ManaCost = 40; Cooldown = 6; Range = 1000; bNeedsEnemy = true; }
    else if (Id == TEXT("frost_bind")) { ManaCost = 35; Cooldown = 12; Range = 850; bNeedsEnemy = true; }
    else if (Id == TEXT("cleaving_strike")) { EnergyCost = 30; Cooldown = 8; Range = 300; bNeedsEnemy = true; }
    else if (Id == TEXT("piercing_shot")) { EnergyCost = 25; Cooldown = 9; Range = 1000; bNeedsEnemy = true; }
    else if (Id == TEXT("shadow_step")) { EnergyCost = 35; Cooldown = 14; Range = 850; bNeedsEnemy = true; }
    else if (Id == TEXT("restoring_light")) { ManaCost = 45; Cooldown = 6; Range = 1200; }
    else if (Id == TEXT("sanctuary")) { ManaCost = 70; Cooldown = 16; }
    else if (Id == TEXT("purify")) { ManaCost = 25; Cooldown = 8; Range = 1200; }
    else if (Id == TEXT("bastion_of_dawn")) { EnergyCost = 45; Cooldown = 75; }
    else if (Id == TEXT("cataclysm")) { ManaCost = 150; Cooldown = 80; Range = 1500; bNeedsEnemy = true; }
    else if (Id == TEXT("executioners_verdict")) { EnergyCost = 60; Cooldown = 60; Range = 1500; bNeedsEnemy = true; }
    else if (Id == TEXT("renewal")) { ManaCost = 140; Cooldown = 90; }
    else return;
    if (!CireSkillShop::CanPayCast(this, Id, ManaCost, EnergyCost)) { Notice = TEXT("Not enough mana or energy."); return; } // progression-shop: Skill Shop level (Ability DB curve)
    if (bNeedsEnemy && (!IsHostile(Target) || !InRange(Target, Range) || !ClearSight(this, Target)))
    { Notice = TEXT("Select a hostile target in range and line of sight."); return; }
    ACireHero* Ally = ::Cast<ACireHero>(Target);
    if (!Ally || Ally->TeamId != TeamId) Ally = this;
    if ((Id == TEXT("restoring_light") || Id == TEXT("purify")) &&
        (Ally->bDead || !Ally->bDrafted || !InRange(Ally, Range) || !ClearSight(this, Ally)))
    { Notice = TEXT("Ally is unavailable or out of healing range."); return; }
    Mana -= ManaCost;
    Energy -= EnergyCost;
    Cooldowns[Slot] = static_cast<float>(Cires::CooldownSeconds(CireDeveloperTools::CooldownSeconds(GetWorld(),Cooldown), CDR));
    CireSkillShop::ApplyCastLevel(this, Slot, Id, ManaCost, EnergyCost); // progression-shop: Skill Shop level (Ability DB curve)
    GlobalCooldown = 0.9f;
    const float Now = GetWorld()->GetTimeSeconds();
    const float Power = Mode->Power(TeamId);
    CireCombat::PlayCue(this,Target,FName(*Id),GetActorLocation(),IsValid(Target)?Target->GetActorLocation():GetActorLocation(),ECireSpellCue::Cast);
    const auto Hit = [&](AActor* Victim, float Amount, FLinearColor Color)
    {
        if (!IsHostile(Victim)) return;
        const FVector Destination = Victim->GetActorLocation();
        CireCombat::ApplyStrike(this, Victim, Amount * Power, SkillName(Id));
    };
    if (Id == TEXT("iron_guard"))
    {
        ShieldUntil = Now + CireDeveloperTools::EffectSeconds(GetWorld(),8.f); CireBuffs::Apply(this,TEXT("iron_guard"),CireDeveloperTools::EffectSeconds(GetWorld(),8.f),this); // aura-vfx
    }
    else if (Id == TEXT("shield_slam"))
    {
        Slow(Target, Now + CireDeveloperTools::EffectSeconds(GetWorld(),2.f)); CireBuffs::Apply(Target,TEXT("shield_slam"),CireDeveloperTools::EffectSeconds(GetWorld(),2.f),this); // aura-vfx
        if (auto* Monster = ::Cast<ACireMonster>(Target)) { CireThreat::Taunt(Monster,this,3);CireNPCCombat::InterruptCast(Monster,this); }
        Hit(Target, 35 + (12 + Strength) * 1.25f, FLinearColor(0.4f, 0.7f, 1.f));
    }
    else if (Id == TEXT("war_cry"))
    {
        TauntUntil = Now + CireDeveloperTools::EffectSeconds(GetWorld(),6.f); CireBuffs::Apply(this,TEXT("war_cry"),CireDeveloperTools::EffectSeconds(GetWorld(),6.f),this); // aura-vfx
        ShieldUntil = FMath::Max(ShieldUntil, Now + CireDeveloperTools::EffectSeconds(GetWorld(),3.f));
        for (auto* Monster : Mode->Monsters)
            if (IsHostile(Monster) && InRange(Monster, 850)) CireThreat::Taunt(Monster,this,6);
        for (auto* Enemy : Mode->Heroes)
            if (IsHostile(Enemy) && InRange(Enemy, 850) && Enemy->bBot) Enemy->Target = this;
    }
    else if (Id == TEXT("chain_spark"))
    {
        TArray<AActor*> Chain;
        Chain.Add(Target);
        const FVector Center = Target->GetActorLocation();
        for (auto* Monster : Mode->Monsters)
            if (IsHostile(Monster) && Monster != Target && FVector::DistSquared2D(Center, Monster->GetActorLocation()) < 250000 && Chain.Num() < 4)
                Chain.Add(Monster);
        for (auto* Enemy : Mode->Heroes)
            if (IsHostile(Enemy) && Enemy != Target && FVector::DistSquared2D(Center, Enemy->GetActorLocation()) < 250000 && Chain.Num() < 4)
                Chain.Add(Enemy);
        for (auto* Victim : Chain) if (ClearSight(this, Victim)) Hit(Victim, 40 + Intelligence * 1.5f, FLinearColor(0.45f, 0.6f, 1.f));
    }
    else if (Id == TEXT("ember_lance")) Hit(Target, 65 + Intelligence * 2.f, FLinearColor(1.f, 0.25f, 0.05f));
    else if (Id == TEXT("frost_bind"))
    {
        Slow(Target, Now + CireDeveloperTools::EffectSeconds(GetWorld(),4.f)); CireBuffs::Apply(Target,TEXT("frost_bind"),CireDeveloperTools::EffectSeconds(GetWorld(),4.f),this); // aura-vfx
        Hit(Target, 30 + Intelligence, FLinearColor(0.2f, 0.85f, 1.f));
    }
    else if (Id == TEXT("cleaving_strike"))
    {
        TArray<AActor*> Victims;
        for (auto* Monster : Mode->Monsters) if (IsHostile(Monster) && InRange(Monster, 320)) Victims.Add(Monster);
        for (auto* Enemy : Mode->Heroes) if (IsHostile(Enemy) && InRange(Enemy, 320)) Victims.Add(Enemy);
        for (auto* Victim : Victims) if (ClearSight(this, Victim)) Hit(Victim, 35 + (12 + PrimaryAttribute()) * 1.5f, FLinearColor(1.f, 0.7f, 0.3f));
    }
    else if (Id == TEXT("piercing_shot")) Hit(Target, 40 + (12 + Agility) * 1.75f, FLinearColor(1.f, 0.9f, 0.35f));
    else if (Id == TEXT("shadow_step"))
    {
        const FVector Direction = (GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
        if (SetActorLocation(Target->GetActorLocation() + Direction * 170, true))
            ACireAreaEffect::ClearForActor(this);
        if (InRange(Target, 240)) Hit(Target, 30 + Agility * 1.5f, FLinearColor(0.6f, 0.2f, 0.9f));
        if (IsValid(Target)) CireCrowdControl::VoidBurst(this, Target->GetActorLocation(), Id); // champion-draft: void rift (stun inside, slow the ring, self-mend)
    }
    else if (Id == TEXT("restoring_light")) CireCombat::ApplyHealing(this, Ally, (90 + Intelligence * 3.f) * Power, SkillName(Id));
    else if (Id == TEXT("sanctuary"))
    {
        for (auto* Friend : Mode->Heroes)
            if (IsValid(Friend) && !Friend->bDead && Friend->TeamId == TeamId && InRange(Friend, 600) && ClearSight(this, Friend))
            {
                CireCombat::ApplyHealing(this, Friend, (45 + Intelligence * 1.5f) * Power, SkillName(Id));
                Friend->ShieldUntil = FMath::Max(Friend->ShieldUntil, Now + CireDeveloperTools::EffectSeconds(GetWorld(),3.f)); CireBuffs::Apply(Friend,TEXT("sanctuary"),CireDeveloperTools::EffectSeconds(GetWorld(),3.f),this); // aura-vfx
            }
    }
    else if (Id == TEXT("purify"))
    {
        Ally->SlowUntil = 0;
        CireCombat::ApplyHealing(this, Ally, (35 + Intelligence * 1.4f) * Power, SkillName(Id));
    }
    else if (Id == TEXT("bastion_of_dawn"))
    {
        CireCombat::ApplyHealing(this, this, MaxHealth * 0.30f * Power, SkillName(Id));
        for (auto* Friend : Mode->Heroes)
            if (IsValid(Friend) && Friend->bDrafted && !Friend->bDead && Friend->TeamId == TeamId &&
                InRange(Friend, 650) && ClearSight(this, Friend))
            {
                Friend->ShieldUntil = FMath::Max(Friend->ShieldUntil, Now + CireDeveloperTools::EffectSeconds(GetWorld(),8.f)); CireBuffs::Apply(Friend,TEXT("bastion_of_dawn"),CireDeveloperTools::EffectSeconds(GetWorld(),8.f),this); // aura-vfx
            }
        // A caster is guarded even if its roster entry is temporarily being assigned.
        ShieldUntil = FMath::Max(ShieldUntil, Now + CireDeveloperTools::EffectSeconds(GetWorld(),8.f)); CireBuffs::Apply(this,TEXT("bastion_of_dawn"),CireDeveloperTools::EffectSeconds(GetWorld(),8.f),this); // aura-vfx
    }
    else if (Id == TEXT("cataclysm"))
    {
        const FVector Center = Target->GetActorLocation();
        TArray<AActor*> Victims;
        Victims.Add(Target);
        const auto Collect = [&](AActor* Candidate)
        {
            if (Candidate != Target && IsHostile(Candidate) && Victims.Num() < 12 &&
                FVector::DistSquared2D(Center, Candidate->GetActorLocation()) <= FMath::Square(550.f))
                Victims.Add(Candidate);
        };
        for (auto* Monster : Mode->Monsters) Collect(Monster);
        for (auto* Enemy : Mode->Heroes) Collect(Enemy);
        for (auto* Victim : Victims)
            if (ClearSight(this, Victim)) Hit(Victim, 160 + Intelligence * 3.5f, FLinearColor(1.f, 0.18f, 0.04f));
    }
    else if (Id == TEXT("executioners_verdict"))
    {
        float MissingHealth = 0;
        if (const auto* Enemy = ::Cast<ACireHero>(Target)) MissingHealth = Enemy->MaxHealth - Enemy->Health;
        else if (const auto* Monster = ::Cast<ACireMonster>(Target)) MissingHealth = Monster->MaxHealth - Monster->Health;
        const int32 Primary = PrimaryAttribute();
        Hit(Target, 100 + Primary * 3.f + FMath::Clamp(MissingHealth * 0.25f, 0.f, 300.f), FLinearColor(1.f, 0.12f, 0.5f));
    }
    else if (Id == TEXT("renewal"))
    {
        for (auto* Friend : Mode->Heroes)
            if (IsValid(Friend) && Friend->bDrafted && !Friend->bDead && Friend->TeamId == TeamId &&
                InRange(Friend, 1000) && ClearSight(this, Friend))
            {
                Friend->SlowUntil = 0;
                CireCombat::ApplyHealing(this, Friend, (200 + Intelligence * 4.f) * Power, SkillName(Id));
            }
    }
    Notice = SkillName(Id);
}

float ACireHero::TakeDamage(float Amount, FDamageEvent const& Event, AController* InstigatorController, AActor* Causer)
{
    auto* Mode = ModeFor(this);
    if (!HasAuthority() || !Mode || !Mode->IsCombatPhase() || bDead || !bDrafted ||
        !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    if (const auto* Enemy = ::Cast<ACireHero>(Causer))
    {
        if (!Mode->CanFight(Enemy, this)) return 0;
        CirePolymorph::Break(this); // progression-shop: PvP damage breaks Polymorph
    }
    else if (const auto* Monster = ::Cast<ACireMonster>(Causer))
    {
        if (Mode->Clock.Phase() != Cires::MatchPhase::Survival || Monster->Lane != TeamId) return 0;
    }
    else return 0;
    if(Mobility&&Mobility->IsInvulnerable())
    {
        const FString AttackName=Event.IsOfType(FCireDamageEvent::CireClassID)?static_cast<const FCireDamageEvent&>(Event).AbilityName:TEXT("Attack");
        CireCombat::BroadcastAvoidance(Causer,this,ECireHitOutcome::Dodge,AttackName);return 0;
    }
    if (HasSkill(TEXT("stone_skin"))) Amount *= 0.90f;
    if (ShieldUntil > GetWorld()->GetTimeSeconds()) Amount *= 0.60f;
    // progression-shop: armor (basic attacks) / spell ward (abilities) and item barriers.
    const FString IncomingName = Event.IsOfType(FCireDamageEvent::CireClassID) ? static_cast<const FCireDamageEvent&>(Event).AbilityName : TEXT("Basic attack");
    Amount = CireItems::ModifyIncomingDamage(this, Causer, IncomingName, Amount);
    Amount = CireClassTraits::ModifyIncomingDamage(this, Amount); // champion-draft: Tank Natural Defense, flat 10 after armor, floor 0
    const float Taken = FMath::Min(Health, Amount);
    Health -= Taken;
    CireCombat::BroadcastDamage(Causer, this, Taken, Event);
    CireItems::OnHeroDamaged(this, Causer, IncomingName, Taken); // progression-shop: teleport interrupt, thorns, Unbroken
    if (Health <= 0)
    {
        bDead = true;
        PendingAttackTarget.Reset();
        CireThreat::Remove(this);ACireSummon::ClearForActor(this);ACireSkillshot::ClearForActor(this);ACireConstruct::ClearForActor(this);
        ACireAreaEffect::ClearForActor(this);
        bAutoAttack = false;
        Target = nullptr;
        RespawnTimer = 10;
        GetCharacterMovement()->StopMovementImmediately();
        GetCharacterMovement()->DisableMovement();
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Notice = Mode->Clock.Phase() == Cires::MatchPhase::Arena ? TEXT("Eliminated. Team is still fighting.") : TEXT("Fallen. Reviving at base in 10 seconds.");
        Mode->HeroKilled(this);
    }
    return Taken;
}

void ACireHero::Purchase(int32 Item)
{
    // progression-shop: legacy indices 0..3 (XP tome, primary tome, longsword, sandglass) buy
    // catalog items through the authoritative inventory; rules and messages live in CireItems.
    if (!HasAuthority() || !Inventory) return;
    FString Message;
    Inventory->Buy(CireItems::LegacyItem(Item), Message);
}

void ACireHero::ReviveAt(FVector Location)
{
    if (!HasAuthority()) return;
    if(Mobility)Mobility->CancelRoll();
    CireThreat::Remove(this);ACireSummon::ClearForActor(this);ACireSkillshot::ClearForActor(this);ACireConstruct::ClearForActor(this);
    ACireAreaEffect::ClearForActor(this);
    PendingAttackTarget.Reset();
    bDead = false;
    RespawnTimer = 0;
    ShieldUntil = 0; CireBuffs::ClearAll(this); // aura-vfx
    TauntUntil = 0;
    SlowUntil = 0;
    BasicTimer = 0;
    GlobalCooldown = 0;
    Target = nullptr;
    bAutoAttack = false;
    for (float& Cooldown : Cooldowns) Cooldown = 0;
    GetCharacterMovement()->StopMovementImmediately();
    SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    Recalculate(true);
}

bool ACireHero::CanJumpInternal_Implementation()const
{
    return bDrafted&&!bDead&&(!Mobility||!Mobility->IsRolling())&&Super::CanJumpInternal_Implementation();
}

void ACireHero::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    CirePolymorph::TickVisual(this); // progression-shop: PvP Polymorph critter body
    if(Mobility)
    {
        CireMovement::ApplyToHero(*this); // tuning, facing mode and tank body scale
        if(bDead)Mobility->CancelRoll();
    }
    if (ChampionArt) ChampionArt->UpdateVisuals(*this, DeltaSeconds);
    CireRealm::UpdateVisibility(this);
    // bDead is replicated; clients apply collision locally to avoid dead pawn blockers.
    if (!HasAuthority())
    {
        GetCapsuleComponent()->SetCollisionEnabled(bDead ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
        const auto* State = GetWorld()->GetGameState<ACireGameState>();
        const double ServerTime = State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
        GetCharacterMovement()->MaxWalkSpeed = Mobility?Mobility->MovementSpeed(SlowUntil>ServerTime):(SlowUntil>ServerTime?338.f:520.f);
        GetCharacterMovement()->MaxWalkSpeed *= CireItems::MoveSpeedMultiplier(this); // progression-shop
    GetCharacterMovement()->MaxWalkSpeed *= CireSignatureSkills::MoveSpeedMultiplier(this); // new-champions: Moonlit Sprint, haste pylons
        if (CireRaces::IsRooted(this)) GetCharacterMovement()->MaxWalkSpeed = 0.f; // monster-races: rooted by a monster skill
        if (CireCrowdControl::IsStunned(this)) GetCharacterMovement()->MaxWalkSpeed = 0.f; // champion-draft: stunned
        return;
    }
    auto* Mode = ModeFor(this);
    if (!Mode || !bDrafted) return;
    if (bDead)
    {
        if (Mode->Clock.Phase() == Cires::MatchPhase::Survival)
        {
            RespawnTimer -= DeltaSeconds;
            if (RespawnTimer <= 0) ReviveAt(Mode->BasePosition(TeamId) + FVector(0, (static_cast<int32>(GetUniqueID() % 5) - 2) * 110, 100));
        }
        return;
    }
    if (PendingAttackTarget.IsValid()) {
        AttackReleaseTimer -= DeltaSeconds;
        if (AttackReleaseTimer <= 0) {
            AActor* ReleasedTarget = PendingAttackTarget.Get();
            PendingAttackTarget.Reset();
            CireAttacks::Release(this, ReleasedTarget, PendingAttackDamage);
        }
    }
    BasicTimer = FMath::Max(0.f, BasicTimer - DeltaSeconds);
    GlobalCooldown = FMath::Max(0.f, GlobalCooldown - DeltaSeconds);
    for (float& Cooldown : Cooldowns) Cooldown = FMath::Max(0.f, Cooldown - DeltaSeconds);
    const float Regen = HasSkill(TEXT("deep_reserves")) ? 1.5f : 1.f;
    Mana = FMath::Min(MaxMana, Mana + DeltaSeconds * MaxMana * 0.015f * Regen);
    Energy = FMath::Min(100.f, Energy + DeltaSeconds * 9.f * Regen);
    CireItems::ApplyRegen(this, DeltaSeconds); // progression-shop: item health/mana/energy regeneration
    if ((Mode->Clock.Phase() == Cires::MatchPhase::Intermission || Mode->Clock.Phase() == Cires::MatchPhase::Recovery) &&
        FVector::DistSquared2D(GetActorLocation(), Mode->BasePosition(TeamId)) < FMath::Square(750.f))
    {
        Health = FMath::Min(MaxHealth, Health + DeltaSeconds * MaxHealth * 0.15f);
        Mana = FMath::Min(MaxMana, Mana + DeltaSeconds * MaxMana * 0.15f);
    }
    GetCharacterMovement()->MaxWalkSpeed = Mobility?Mobility->MovementSpeed(SlowUntil>GetWorld()->GetTimeSeconds()):(SlowUntil>GetWorld()->GetTimeSeconds()?338.f:520.f);
    GetCharacterMovement()->MaxWalkSpeed *= CireItems::MoveSpeedMultiplier(this); // progression-shop
    if (CireRaces::IsRooted(this)) GetCharacterMovement()->MaxWalkSpeed = 0.f; // monster-races: rooted by a monster skill
    if (CireCrowdControl::IsStunned(this)) GetCharacterMovement()->MaxWalkSpeed = 0.f; // champion-draft: stunned
    CireCrowdControl::TickHero(this, DeltaSeconds); // champion-draft: completes timed casts, Executioner charge
    if (bBot) BotThink(DeltaSeconds);
    if (bAutoAttack) BasicAttack();
}

void ACireHero::BotThink(float DeltaSeconds)
{
    auto* Mode = ModeFor(this);
    if (!Mode || bDead || !bDrafted) return;
    if (!Offers.IsEmpty())
    {
        int32 Pick = 0;
        // Give support/tank bots useful role preferences while obeying the same random draft.
        for (int32 Index = 0; Index < Offers.Num(); ++Index)
        {
            if (HasChampionRole(TEXT("healer")) && (Offers[Index] == TEXT("restoring_light") || Offers[Index] == TEXT("sanctuary") || Offers[Index] == TEXT("purify"))) Pick = Index;
            if (HasChampionRole(TEXT("tank")) && (Offers[Index] == TEXT("war_cry") || Offers[Index] == TEXT("iron_guard") || Offers[Index] == TEXT("shield_slam"))) Pick = Index;
        }
        Learn(Pick);
    }
    if (Mode->Clock.Phase() == Cires::MatchPhase::Intermission || Mode->Clock.Phase() == Cires::MatchPhase::Recovery)
    {
        Target = nullptr;
        bAutoAttack = false;
        const FVector Destination = HomePosition;
        if (FVector::DistSquared2D(GetActorLocation(), Destination) > FMath::Square(160.f))
            AddMovementInput(CireWaveDirector::BotSteer(this, Destination)); // nav-paths: walk home on the navmesh
        BotDecisionTimer -= DeltaSeconds;
        if (BotDecisionTimer <= 0)
        {
            BotDecisionTimer = 2;
            CireItems::BotShop(this); // progression-shop: bots follow their role's recommended build
        }
        return;
    }
    if (!Mode->IsCombatPhase()) return;
    // wave-director: survival bots defend the lane first, never open on neutral packs,
    // and fall back to regroup when low instead of fighting to the death (CireWaves.h).
    const bool bSurvival = Mode->Clock.Phase() == Cires::MatchPhase::Survival;
    if (bSurvival && CireWaveDirector::ShouldBotRetreat(this))
    {
        Target = nullptr; bAutoAttack = false;
        FVector Fallback;
        if (CireWaveDirector::BotDestination(this, Fallback)) AddMovementInput(CireWaveDirector::BotSteer(this, Fallback));
        return;
    }
    BotDecisionTimer -= DeltaSeconds;
    if (BotDecisionTimer <= 0 || !IsHostile(Target))
    {
        BotDecisionTimer = 0.25f;
        AActor* Best = nullptr;
        double BestScore = TNumericLimits<double>::Max();
        const auto Consider = [&](AActor* Candidate, double Bias)
        {
            if (!IsHostile(Candidate)) return;
            const double Score = FVector::DistSquared2D(GetActorLocation(), Candidate->GetActorLocation()) + Bias;
            if (Score < BestScore) { Best = Candidate; BestScore = Score; }
        };
        if (Mode->Clock.Phase() == Cires::MatchPhase::Arena)
        {
            for (auto* Enemy : Mode->Heroes)
                if (IsValid(Enemy)) Consider(Enemy, Enemy->TauntUntil > GetWorld()->GetTimeSeconds() ? -100000000.0 : 0.0);
        }
        else Best = CireWaveDirector::ChooseBotTarget(this); // wave-director: lane-defence priorities
        Target = Best;
        if (Target)
        {
            for (int32 Slot = 0; Slot < Skills.Num(); ++Slot)
            {
                if (IsPassive(Skills[Slot]) || Cooldowns[Slot] > 0 || GlobalCooldown > 0) continue;
                if((Skills[Slot]==TEXT("second_wind")||Skills[Slot]==TEXT("last_stand"))&&Health>=MaxHealth*.8f)continue;
                if(Skills[Slot]==TEXT("challenge_of_iron")||Skills[Slot]==TEXT("seismic_reprisal"))
                {
                    const auto* Recipe=CireSkillTuning::FindRoleSkill(Skills[Slot]);
                    if(!Recipe||!InRange(Target,Recipe->Radius))continue;
                }
                if (Skills[Slot] == TEXT("restoring_light") || Skills[Slot] == TEXT("purify") || Skills[Slot] == TEXT("sanctuary") || Skills[Slot] == TEXT("renewal") || Skills[Slot]==TEXT("wellspring"))
                {
                    ACireHero* Wounded = nullptr;
                    float Lowest = 0.80f;
                    float HealingRange=Skills[Slot]==TEXT("sanctuary")?600.f:Skills[Slot]==TEXT("renewal")?1000.f:1200.f;
                    if(Skills[Slot]==TEXT("wellspring"))if(const auto* Recipe=CireSkillTuning::FindRoleSkill(Skills[Slot]))HealingRange=Recipe->CastRange;
                    for (auto* Friend : Mode->Heroes)
                        if (IsValid(Friend) && !Friend->bDead && Friend->bDrafted && Friend->TeamId == TeamId && InRange(Friend, HealingRange) &&
                            Friend->Health / FMath::Max(1.f, Friend->MaxHealth) < Lowest)
                        { Wounded = Friend; Lowest = Friend->Health / Friend->MaxHealth; }
                    if (Wounded)
                    {
                        AActor* Previous = Target;
                        Target = Wounded;
                        Cast(Slot);
                        Target = Previous;
                    }
                }
                else if ((Skills[Slot] != TEXT("iron_guard") || Health < MaxHealth * 0.85f) &&
                         (Skills[Slot] != TEXT("bastion_of_dawn") || Health < MaxHealth * 0.80f) &&
                         (Skills[Slot] != TEXT("war_cry") || InRange(Target, 750))) Cast(Slot);
            }
        }
    }
    if (IsHostile(Target))
    {
        const float Distance = FVector::Dist2D(GetActorLocation(), Target->GetActorLocation());
        const FVector Direction = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
        const bool bSight = ClearSight(this, Target);
        if (Distance > BasicRange(this) * 0.85f || !bSight)
        {
            // nav-paths: path to the target; a ranged bot without line of sight walks to the nearest
            // navmesh point that can see it (within range) instead of walking into the blocker.
            FVector Goal = Target->GetActorLocation(), Firing;
            if (!bSight && IsRangedBasicAttack() && CireNav::FiringPosition(this, Target, BasicRange(this) * 0.8f, Firing)) Goal = Firing;
            AddMovementInput(CireWaveDirector::BotSteer(this, Goal));
        }
        else SetActorRotation(Direction.Rotation());
        bAutoAttack = true;
    }
    else
    {
        bAutoAttack = false;
        // wave-director: with nothing to fight, hold the castle approach instead of idling where the last fight ended.
        FVector Hold;
        if (bSurvival && CireWaveDirector::BotDestination(this, Hold)) AddMovementInput(CireWaveDirector::BotSteer(this, Hold));
    }
}

void ACireHero::MulticastCombatFx_Implementation(FVector From, FVector To, FLinearColor Color)
{
    if (GetNetMode() == NM_DedicatedServer) return;
    DrawDebugLine(GetWorld(), From + FVector(0, 0, 45), To + FVector(0, 0, 45), Color.ToFColor(true), false, 0.20f, 0, 4.f);
    DrawDebugSphere(GetWorld(), To + FVector(0, 0, 35), 38.f, 12, Color.ToFColor(true), false, 0.25f, 0, 2.f);
}

FString ACireHero::SkillName(const FString& Id)
{
    if(CireSignatureSkills::Knows(Id))return CireSignatureSkills::Name(Id); // new-champions
    if(CireSkillCasting::Handles(Id))return CireSkillCasting::Name(Id);
    if(Id==TEXT("npc_shadow_bolt"))return TEXT("Shadow Bolt");
    if(Id==TEXT("npc_barbed_shot"))return TEXT("Barbed Shot");
    if(const auto* A=CireAbilityLibrary::Find(Id))return A->Name;
    for (const auto& Skill : Cires::StarterSkillPool()) if (Id == UTF8_TO_TCHAR(Skill.Id.c_str())) return UTF8_TO_TCHAR(Skill.Name.c_str());
    return Id;
}

FString ACireHero::SkillDescription(const FString& Id)
{
    if(CireSignatureSkills::Knows(Id))return CireSignatureSkills::Description(Id); // new-champions
    if(CireCrowdControl::HandlesSkill(Id))return CireCrowdControl::Description(Id); // champion-draft
    if(Id==TEXT("executioner"))return TEXT("PASSIVE: every 5 minutes your next basic attack is lethal. Bosses take a normal hit (the charge is kept); champions take 30% of their max health."); // champion-draft
    if(CireSkillCasting::Handles(Id))return CireSkillCasting::Description(Id);
    if(const auto* A=CireAbilityLibrary::Find(Id))return FString::Printf(TEXT("%.0f mana | %.0fs CD. Ground area: %.0f impact + %.0f damage/sec for %.1fs. Warning %.2fs. Aim at target, or forward when none selected."),A->ManaCost,A->CooldownSeconds,A->Area.BurstDamage,A->Area.bPersistent?A->Area.DamagePerSecond:0,A->Area.DurationSeconds,A->Area.WarningSeconds);
    if (Id == TEXT("iron_guard")) return TEXT("25 energy | 14s CD. Take 40% less damage for 8s.");
    if (Id == TEXT("shield_slam")) return TEXT("25 energy | 7s CD. Melee STR strike, 2s slow; draw monster attention.");
    if (Id == TEXT("war_cry")) return TEXT("30 energy | 18s CD. Taunt nearby monsters/bots for 6s; guard for 3s.");
    if (Id == TEXT("chain_spark")) return TEXT("45 mana | 10s CD. INT lightning hits up to 4 nearby enemies.");
    if (Id == TEXT("ember_lance")) return TEXT("40 mana | 6s CD. Long range fire: 65 + 2x INT damage.");
    if (Id == TEXT("frost_bind")) return TEXT("35 mana | 12s CD. Frost damage and 35% movement slow for 4s.");
    if (Id == TEXT("cleaving_strike")) return TEXT("30 energy | 8s CD. Primary-stat strike hits enemies around you.");
    if (Id == TEXT("piercing_shot")) return TEXT("25 energy | 9s CD. Long range AGI-scaled physical strike.");
    if (Id == TEXT("shadow_step")) return TEXT("35 energy | 14s CD. Dash to an enemy and strike with AGI damage.");
    if (Id == TEXT("restoring_light")) return TEXT("45 mana | 6s CD. Heal ally or self for 90 + 3x INT.");
    if (Id == TEXT("sanctuary")) return TEXT("70 mana | 16s CD. Heal nearby allies and guard them for 3s.");
    if (Id == TEXT("purify")) return TEXT("25 mana | 8s CD. Remove ally/self slow and restore health.");
    if (Id == TEXT("bastion_of_dawn")) return TEXT("ULTIMATE: 45 energy | 75s CD. Heal yourself for 30% max HP; nearby allies take 40% less damage for 8s.");
    if (Id == TEXT("cataclysm")) return TEXT("ULTIMATE: 150 mana | 80s CD. Blast up to 12 enemies near your target for 160 + 3.5x INT damage.");
    if (Id == TEXT("executioners_verdict")) return TEXT("ULTIMATE: 60 energy | 60s CD. Deal 100 + 3x primary stat, plus 25% of target's missing HP (bonus capped at 300).");
    if (Id == TEXT("renewal")) return TEXT("ULTIMATE: 140 mana | 90s CD. Cleanse nearby allies' slows and heal each for 200 + 4x INT. Cannot revive fallen allies.");
    if (Id == TEXT("stone_skin")) return TEXT("PASSIVE: take 10% less damage. Uses your only passive slot.");
    if (Id == TEXT("battle_rhythm")) return TEXT("PASSIVE: 20% faster basic attacks. Uses your only passive slot.");
    if (Id == TEXT("deep_reserves")) return TEXT("PASSIVE: 50% more mana/energy regeneration. Uses your only passive slot.");
    if (Id == TEXT("soul_conduit")) return TEXT("PASSIVE: your healing is 25% stronger. Uses your only passive slot.");
    return TEXT("Unknown skill");
}

ACireMonster::ACireMonster()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
    SetReplicateMovement(true);
    GetCapsuleComponent()->InitCapsuleSize(38.f, 88.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
    GetCharacterMovement()->MaxWalkSpeed = 210.f;
    GetCharacterMovement()->bOrientRotationToMovement = true;
    bUseControllerRotationYaw = false;
    static ConstructorHelpers::FObjectFinder<USkeletalMesh> Body(
        TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    if (Body.Succeeded())
    {
        GetMesh()->SetSkeletalMesh(Body.Object);
        GetMesh()->SetRelativeLocation(FVector(0, 0, -88));
        GetMesh()->SetRelativeRotation(FRotator(0, -90, 0));
    }
    static ConstructorHelpers::FClassFinder<UAnimInstance> Animation(
        TEXT("/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"));
    if (Animation.Succeeded()) GetMesh()->SetAnimInstanceClass(Animation.Class);
    GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NPCState = CreateDefaultSubobject<UCireNPCState>(TEXT("NPCState")); // npc-boss: role/boss/threat state
    CreateDefaultSubobject<UCireBuffState>(TEXT("BuffState")); // aura-vfx: replicated named-effect records for signature visuals
    MonsterArt = CreateDefaultSubobject<UCireMonsterArt>(TEXT("MonsterArt")); // creature-anim: Tripo body, swing timing, death
}

void ACireMonster::BeginPlay()
{
    Super::BeginPlay();
    SpawnPosition = GetActorLocation();
    CireStatusVisual::Attach(this);
}

void ACireMonster::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ACireMonster, Lane);
    DOREPLIFETIME(ACireMonster, Tier);
    DOREPLIFETIME(ACireMonster, PackId);
    DOREPLIFETIME(ACireMonster, bBoss);
    DOREPLIFETIME(ACireMonster, bArmoredEscort);
    DOREPLIFETIME(ACireMonster, bNeutral); // wave-director
    DOREPLIFETIME(ACireMonster, LeakCostOverride);
    DOREPLIFETIME(ACireMonster, Health);
    DOREPLIFETIME(ACireMonster, MaxHealth);
    DOREPLIFETIME(ACireMonster, PoisonAreaCount); DOREPLIFETIME(ACireMonster, PoisonEndsAt); DOREPLIFETIME(ACireMonster, SlowUntil); DOREPLIFETIME(ACireMonster, Victim); DOREPLIFETIME(ACireMonster, CombatArchetype); DOREPLIFETIME(ACireMonster, CastingAbility); DOREPLIFETIME(ACireMonster, CastEndsAt); DOREPLIFETIME(ACireMonster, CastStartedAt);
    DOREPLIFETIME(ACireMonster, MonsterName);
}

float ACireMonster::TakeDamage(float Amount, FDamageEvent const& Event, AController* InstigatorController, AActor* Causer)
{
    auto* Mode = ModeFor(this);
    auto* Attacker = ::Cast<ACireHero>(Causer);
    if (!HasAuthority() || !Mode || !Attacker || !Attacker->IsHostile(this) || Health <= 0 ||
        !FMath::IsFinite(Amount) || Amount <= 0) return 0;
    if (!CireWaveDirector::AllowDamage(this, Attacker)) return 0; // wave-director: neutral packs ignore bots; a player's hit aggroes the pack
    CirePolymorph::Break(this); // progression-shop: any damage breaks Polymorph
    Amount = CireNPCCombat::ModifyIncomingDamage(this, Attacker, Amount); // npc-boss: armor/guard/shield wall/provoke
    if (Amount <= 0 || Health <= 0) return 0;
    const float Taken = FMath::Min(Health, Amount);
    Health -= Taken;
    CireCombat::BroadcastDamage(Causer, this, Taken, Event);
    bEngaged = true;
    CireThreat::Damage(this,Attacker,Taken);
    CireWaveDirector::OnMonsterDamaged(this, Attacker); // wave-director: escort guards defend their escortee
    if (PackId >= 0)
        for (auto* Companion : Mode->Monsters)
            if (IsValid(Companion) && Companion->PackId == PackId && Companion->Lane == Lane)
            { CireThreat::Engage(Companion,Attacker);CireThreat::Select(Companion); }
    if (Health <= 0)
    {
        CireThreat::Clear(this);CireNPCCombat::Interrupt(this);
        ACireAreaEffect::ClearForActor(this);
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        CireSignatureSkills::OnMonsterKilled(this, Attacker); // new-champions: bounties
        Mode->MonsterKilled(this, Attacker);
        if (MonsterArt) MonsterArt->MulticastDeath(); // creature-anim: clients keep a falling corpse after the actor goes
        if (!IsActorBeingDestroyed()) Destroy();
    }
    return Taken;
}

void ACireMonster::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    CirePolymorph::TickVisual(this); // progression-shop: critter body while polymorphed
    if(CirePolymorph::TickMonster(this,DeltaSeconds))return; // progression-shop: polymorphed critters wander, no AI
    if(CireCrowdControl::TickMonster(this,DeltaSeconds))return; // champion-draft: stunned monsters skip their AI
    CireNPCCombat::Tick(this,DeltaSeconds);
}

