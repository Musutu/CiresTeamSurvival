// ability-expansion: native checks for the expansion pool (data, shapes, icons, sounds, Fab signatures, every cast,
// riders, buffs, passives, summons, constructs).
#include "CireAbilityExpansion.h"
#include "CireAbilityDB.h"
#include "CireAbilityIcons.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireKitSkills.h"
#include "CireScalingKits.h"
#include "CireSignatureSkills.h"
#include "CireSkillShop.h"
#include "CireSkillshot.h"
#include "CireSoundEvents.h"
#include "CireSummon.h"
#include "CireTargeting.h"
#include "CireTechConstructs.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogCireExpansionTests, Log, All);

namespace CireXpTests
{
struct FChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireExpansionTests, Error, TEXT("CIRE_ABILITY_EXPANSION_CHECK_FAIL %s"), *Message); }
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
        if (Actor)
        {
            auto* Body = NewObject<UBoxComponent>(Actor);
            Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body);
            Body->SetBoxExtent(FVector(2600, 1400, 50)); Body->SetCollisionObjectType(ECC_WorldStatic);
            Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Body->SetCollisionResponseToAllChannels(ECR_Block);
            Body->RegisterComponent(); Actor->SetActorLocation(Ground + FVector(500, 0, -50));
        }
    }
    ~FFixture()
    {
        UWorld* World = Mode->GetWorld();
        if (auto* S = UCireKitSkillsSubsystem::Get(World)) { S->Zones.Reset(); S->Hots.Reset(); S->Burns.Reset(); }
        for (TActorIterator<ACireConstruct> It(World); It; ++It) It->Destroy();
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I]))
        { ACireAreaEffect::ClearForActor(Actors[I]); ACireConstruct::ClearForActor(Actors[I]); ACireSkillshot::ClearForActor(Actors[I]); ACireSummon::ClearForActor(Actors[I]); Actors[I]->Destroy(); }
        Mode->Clock = SavedClock; Mode->Heroes = SavedHeroes; Mode->Monsters = SavedMonsters;
    }
    template<class T> T* Keep(T* Actor) { if (Actor) { Actors.Add(Actor); Actor->SetActorTickEnabled(false); } return Actor; }
    ACireHero* Hero(int32 Team, FVector Offset, const FString& Profile)
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
    ACireMonster* Monster(FVector Offset, float Health = 50000)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = Keep(Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (M) { M->Lane = 0; M->Health = M->MaxHealth = Health; Mode->Monsters.Add(M); }
        return M;
    }
    FVector At(FVector Offset) const { return Ground + Offset; }
};
void Learn(ACireHero* H, const TArray<FString>& Ids)
{
    H->Skills = Ids; H->Cooldowns.Init(0.f, Ids.Num()); H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100; H->Notice.Reset();
}
}

bool CireAbilityExpansion::RunSmoke(ACireGameMode* Mode)
{
    using namespace CireXpTests;
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    UWorld* World = Mode->GetWorld();
    auto* Sub = UCireKitSkillsSubsystem::Get(World);
    T.Check(Sub != nullptr, TEXT("kit subsystem exists (zones, heals, bleeds)"));
    if (!Sub) return false;
    CireAbilityDB::Reload(); Reload();

    // ---- 1. data: every expansion row is implemented, sold, sectioned, scaled, shaped, iconed, voiced and (actives) Fab-signed ----
    const TArray<FString>& Ids = AllIds();
    T.Check(Ids.Num() >= 100, FString::Printf(TEXT("100+ expansion abilities (%d)"), Ids.Num()));
    int32 Ultimates = 0, Passives = 0, Summons = 0, Constructs = 0;
    static const TSet<FString> Sections = {TEXT("spell"), TEXT("attack"), TEXT("defensive"), TEXT("control"), TEXT("summon"), TEXT("construct"), TEXT("passive"), TEXT("ultimate"), TEXT("initiation")};
    for (const FString& Id : Ids)
    {
        const FCireAbilityDef* D = CireAbilityDB::Find(Id);
        T.Check(D && D->IsImplemented(), Id + TEXT(": merged, implemented Ability DB row"));
        if (!D) continue;
        Ultimates += D->IsUltimate(); Passives += D->IsPassive(); Summons += Delivery(Id) == TEXT("summon"); Constructs += Delivery(Id) == TEXT("construct");
        T.Check(Sections.Contains(D->Section) && D->EffectTags.Num() <= 4 && D->Categories.Num() > 0, Id + TEXT(": Skill Shop section and group"));
        T.Check(D->Types.Num() > 0, Id + TEXT(": role types"));
        T.Check(D->ScalePrimary > 0 || D->PotencyPerPoint > 0, Id + TEXT(": scales with the primary stat"));
        T.Check(D->IsPassive() ? !D->Aura15.IsNone() : !D->Level15Bonus.IsNone(), Id + TEXT(": level-15 bonus / team aura"));
        T.Check(!D->IsUltimate() || D->Upgrade.bValid, Id + TEXT(": ultimate upgrade"));
        T.Check(D->Champions.Num() >= 3, Id + TEXT(": purchasable by 3+ champions"));
        for (const FString& C : D->Champions) T.Check(CireAbilityDB::CanLearn(C, Id) && CireAbilityDB::PurchasableSkills(C).Contains(Id), C + TEXT(" can buy ") + Id);
        T.Check(CireSignatureSkills::Knows(Id) && CireSignatureSkills::IsPassive(Id) == D->IsPassive() && ACireHero::IsPassive(Id) == D->IsPassive() &&
            ACireHero::IsUltimate(Id) == D->IsUltimate(), Id + TEXT(": routed, kind agrees with the runtime"));
        T.Check(!CireAbilityIcons::School(Id).IsEmpty(), Id + TEXT(": icon row"));
        T.Check(CireSoundEvents::FindAbility(FName(*Id)) != nullptr, Id + TEXT(": sound row"));
        const FCireHitShape Shape = CireAbilityShapes::Describe(FName(*Id));
        T.Check(D->IsPassive() ? Shape.Kind == ECireHitShape::None : Shape.Kind != ECireHitShape::None, Id + TEXT(": telegraph shape"));
        if (Shape.HasGroundShape()) T.Check(ACireAreaEffect::ValidateSpec(Shape.AsArea()), Id + TEXT(": telegraph is a valid ground boundary"));
        if (Shape.bGroundAim)
        {
            const auto Desc = CireTargeting::Describe(Id);
            T.Check(Desc.Kind == ECireTargetKind::Ground && Desc.bHasFootprint, Id + TEXT(": aims on the ground with its true footprint"));
        }
        if (!D->IsPassive())
        {
            bool bFab = false;
            for (CireFabVFX::ERole Role : {CireFabVFX::ERole::Cast, CireFabVFX::ERole::Projectile, CireFabVFX::ERole::Impact, CireFabVFX::ERole::Area})
                bFab |= CireFabVFX::FindAbility(FName(*Id), Role) != nullptr;
            T.Check(bFab, Id + TEXT(": Fab Niagara signature"));
        }
        if (Delivery(Id) == TEXT("construct")) T.Check(CireTechConstructs::FindRecipe(FName(*Id)) != nullptr && CireTechConstructs::IsConstructSkill(Id), Id + TEXT(": construct recipe"));
    }
    T.Check(Ultimates >= 10 && Passives >= 10 && Summons >= 6 && Constructs >= 6,
        FString::Printf(TEXT("pool mix: %d ultimates, %d passives, %d summons, %d constructs"), Ultimates, Passives, Summons, Constructs));
    for (const FName Id : BuffIds()) T.Check(CireBuffs::KnownIds().Contains(Id) && !CireAbilityDB::ModifierSummary(Id).IsEmpty(), Id.ToString() + TEXT(": known buff with a modifier row"));

    // ---- 2. every active and ultimate casts for a champion that can buy it ----
    auto* M = F.Monster(FVector(260, 0, 0)); auto* M2 = F.Monster(FVector(420, 160, 0)); auto* M3 = F.Monster(FVector(600, -120, 0));
    if (!M || !M2 || !M3) { T.Check(false, TEXT("monster fixtures spawned")); return false; }
    for (const FString& Id : Ids)
    {
        const FCireAbilityDef* D = CireAbilityDB::Find(Id);
        if (!D || D->IsPassive() || D->Champions.IsEmpty()) continue;
        auto* H = F.Hero(0, FVector(-200, 0, 0), D->Champions[0]);
        auto* Ally = F.Hero(0, FVector(-200, 200, 0), TEXT("knight"));
        if (!H || !Ally) { T.Check(false, Id + TEXT(": fixtures")); continue; }
        Ally->Health = Ally->MaxHealth * .5f;
        for (auto* Mon : {M, M2, M3}) { Mon->Health = Mon->MaxHealth; CireBuffs::ClearAll(Mon); Mon->SlowUntil = 0; Mon->SetActorLocation(F.At(FVector(Mon == M ? 260 : Mon == M2 ? 420 : 600, Mon == M2 ? 160 : Mon == M3 ? -120 : 0, 92))); }
        Learn(H, {Id}); H->Target = D->Targeting == TEXT("ally") ? static_cast<AActor*>(Ally) : static_cast<AActor*>(M); H->bHasCastAim = true;
        FVector Aim = M->GetActorLocation() - FVector(0, 0, 92);
        const FString Del = Delivery(Id);
        if (Del == TEXT("construct") || Del == TEXT("wall") || Del == TEXT("summon")) Aim = F.At(FVector(80, -300, 0));
        if (Del == TEXT("healZone")) Aim = Ally->GetActorLocation() - FVector(0, 0, 92);
        H->CastAimPoint = Aim;
        if (D->Targeting == TEXT("enemy") && D->Range > 0 && D->Range <= 320.f) H->SetActorLocation(M->GetActorLocation() - FVector(170, 0, 0)); // melee reach
        const float Before = M->Health + M2->Health + M3->Health;
        const bool bCast = CireSignatureSkills::Cast(H, 0, Id);
        H->bHasCastAim = false;
        T.Check(bCast && H->Cooldowns[0] > 0 && H->GlobalCooldown > 0, D->Champions[0] + TEXT(" casts ") + Id + TEXT(" (") + H->Notice + TEXT(")"));
        if (bCast && (Del == TEXT("strike") || Del == TEXT("chain") || Del == TEXT("dash")))
            T.Check(M->Health + M2->Health + M3->Health < Before, Id + TEXT(": the instant hit lands"));
        if (bCast && Del == TEXT("summon"))
        {
            int32 Units = 0; for (TActorIterator<ACireSummon> It(World); It; ++It) if (It->GetOwnerHero() == H && It->SourceSkill == FName(*Id) && !It->HeroName.IsEmpty()) ++Units;
            T.Check(Units > 0, Id + TEXT(": summons appear, named, tagged with the skill"));
        }
        if (bCast && Del == TEXT("construct")) T.Check(CireTechConstructs::CountOwned(H, FName(*Id)) > 0, Id + TEXT(": construct placed"));
        Sub->Tick(1.05f); Sub->Tick(1.05f);
        ACireAreaEffect::ClearForActor(H); ACireConstruct::ClearForActor(H); ACireSkillshot::ClearForActor(H); ACireSummon::ClearForActor(H);
        Sub->Zones.Reset(); Sub->Hots.Reset(); Sub->Burns.Reset();
        for (TActorIterator<ACireConstruct> It(World); It; ++It) if (It->GetSourceActor() == H) It->Destroy();
        CireBuffs::ClearAll(H);
        Mode->Heroes.Remove(H); Mode->Heroes.Remove(Ally); H->Destroy(); Ally->Destroy();
    }
    for (auto* Mon : {M, M2, M3}) { Mon->Health = Mon->MaxHealth; CireBuffs::ClearAll(Mon); Mon->SlowUntil = 0; }

    // ---- 3. behaviour spot checks ----
    {
        auto* H = F.Hero(0, FVector(-200, 0, 0), TEXT("ranger"));
        if (H)
        {
            // Bleed rider: Hemorrhage leaves a bleed on its victim.
            Learn(H, {TEXT("hemorrhage")}); H->Target = M; H->SetActorLocation(M->GetActorLocation() - FVector(170, 0, 0));
            T.Check(CireSignatureSkills::Cast(H, 0, TEXT("hemorrhage")) && Sub->Burns.Num() > 0 && CireBuffs::IsActive(M, TEXT("xp_bleeding")), TEXT("Hemorrhage bleeds its victim"));
            Sub->Burns.Reset(); CireBuffs::ClearAll(M);
            // Self buff: Bloodletting empowers skill damage.
            const float Plain = CireSignatureSkills::ModifyOutgoingDamage(H, M, 100.f, TEXT("Test"));
            Learn(H, {TEXT("bloodletting")});
            T.Check(CireSignatureSkills::Cast(H, 0, TEXT("bloodletting")) && CireBuffs::IsActive(H, TEXT("xp_empowered")), TEXT("Bloodletting empowers the caster"));
            T.Check(CireSignatureSkills::ModifyOutgoingDamage(H, M, 100.f, TEXT("Test")) > Plain * 1.1f, TEXT("empowered damage is higher"));
            CireBuffs::ClearAll(H);
            // Mark: Void Bolt exposes the victim (through the hit pipeline).
            CireAbilityExpansion::OnAbilityHit(H, M, TEXT("Void Bolt"), 50.f);
            T.Check(CireBuffs::IsActive(M, TEXT("xp_exposed")) && CireSignatureSkills::ModifyOutgoingDamage(H, M, 100.f, TEXT("Test")) > Plain * 1.05f, TEXT("Void Bolt exposes: more damage taken"));
            CireBuffs::ClearAll(M);
            // Root rider: Ring of Ruin roots.
            CireAbilityExpansion::OnAbilityHit(H, M, TEXT("Ring of Ruin"), 50.f);
            T.Check(CireBuffs::IsActive(M, TEXT("npc_rooted")), TEXT("Ring of Ruin roots"));
            CireBuffs::ClearAll(M);
            // Passives: Reaper's Instinct vs a low-health target; Fleetfoot and Battle Trance speeds.
            Learn(H, {TEXT("reapers_instinct"), TEXT("fleetfoot"), TEXT("battle_trance")});
            M->Health = M->MaxHealth * .2f;
            T.Check(CireSignatureSkills::ModifyOutgoingDamage(H, M, 100.f, TEXT("Test")) > Plain * 1.1f, TEXT("Reaper's Instinct: more damage to a low-health target"));
            M->Health = M->MaxHealth;
            T.Check(CireSignatureSkills::MoveSpeedMultiplier(H) > 1.05f && CireSignatureSkills::AttackSpeedBonus(H) > .08f, TEXT("Fleetfoot and Battle Trance speed the champion up"));
            // Iron Hide on a defender.
            auto* Tank = F.Hero(0, FVector(-200, 250, 0), TEXT("knight"));
            if (Tank)
            {
                const float Raw = CireSignatureSkills::ModifyOutgoingDamage(M, Tank, 100.f, TEXT("Test"));
                Learn(Tank, {TEXT("iron_hide")});
                T.Check(CireSignatureSkills::ModifyOutgoingDamage(M, Tank, 100.f, TEXT("Test")) < Raw * .95f, TEXT("Iron Hide: less damage taken"));
            }
            // Party buff: Tailwind hastes allies in range.
            Learn(H, {TEXT("wind_ward")});
            T.Check(CireSignatureSkills::Cast(H, 0, TEXT("wind_ward")) && CireBuffs::IsActive(H, TEXT("xp_hastened")), TEXT("Tailwind hastes the party"));
            // Heal zone: Mending Rain heals a wounded ally standing in it.
            auto* Hurt = F.Hero(0, FVector(-100, 0, 0), TEXT("scholar"));
            if (Hurt)
            {
                Hurt->Health = 500.f; Learn(H, {TEXT("mending_rain")}); H->bHasCastAim = true; H->CastAimPoint = Hurt->GetActorLocation() - FVector(0, 0, 92);
                const bool bRain = CireSignatureSkills::Cast(H, 0, TEXT("mending_rain")); H->bHasCastAim = false;
                Sub->Tick(1.05f);
                T.Check(bRain && Hurt->Health > 500.f, TEXT("Mending Rain heals allies inside"));
            }
            // Bot wisdom: no heal zone with nobody hurt; damage skills always.
            for (ACireHero* A : Mode->Heroes) if (IsValid(A)) A->Health = A->MaxHealth;
            T.Check(!BotWantsCast(H, TEXT("mending_rain")) && BotWantsCast(H, TEXT("blood_bolt")), TEXT("bots skip wasted heals"));
        }
    }
    UE_LOG(LogCireExpansionTests, Display, TEXT("CIRE_ABILITY_EXPANSION_%s checks=%d abilities=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count, Ids.Num());
    return T.bPassed;
}
#endif
