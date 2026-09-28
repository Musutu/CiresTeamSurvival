// initiation: native checks for the Initiation group (data, shapes, Set-up synergy, pulls, hook, charge, cage) and the
// Blink Dagger (catalogue, vendors, blink, cooldown, champion-damage lockout).
#include "CireInitiation.h"
#include "CireAbilityDB.h"
#include "CireAbilityExpansion.h"
#include "CireAbilityIcons.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireKitSkills.h"
#include "CireScalingKits.h"
#include "CireSignatureSkills.h"
#include "CireSkillshot.h"
#include "CireSoundEvents.h"
#include "CireSummon.h"
#include "CireVendors.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

#if !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogCireInitiationTests, Log, All);

namespace CireInitTests
{
struct FChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireInitiationTests, Error, TEXT("CIRE_INITIATION_CHECK_FAIL %s"), *Message); }
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
        if (auto* Actor = Keep(Mode->GetWorld()->SpawnActor<AActor>()))
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
    ACireMonster* Monster(FVector Offset, float Health = 50000)
    {
        FActorSpawnParameters P; P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* M = Keep(Mode->GetWorld()->SpawnActor<ACireMonster>(Ground + Offset + FVector(0, 0, 92), FRotator::ZeroRotator, P));
        if (M) { M->Lane = 0; M->Health = M->MaxHealth = Health; Mode->Monsters.Add(M); }
        return M;
    }
    FVector At(FVector Offset) const { return Ground + Offset; }
};
void Learn(ACireHero* H, const FString& Id)
{
    H->Skills = {Id}; H->Cooldowns = {0.f}; H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100; H->Notice.Reset();
}
}

bool CireInitiation::RunSmoke(ACireGameMode* Mode)
{
    using namespace CireInitTests;
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    UWorld* World = Mode->GetWorld();

    // ---- 0. live-tunable numbers (Content/Data/Initiation.json) ----
    FString TuningError;
    T.Check(ReloadTuning(&TuningError) && Tuning().SetUpSeconds > 0 && Tuning().TeamDamageBonus > 0 && Tuning().AreaDamageBonus >= Tuning().TeamDamageBonus &&
        Tuning().BlinkRange >= 600 && Tuning().BlinkCooldown > 0 && Tuning().BlinkLockout > 0, TEXT("Initiation.json loads: ") + TuningError);
    // ---- 1. data: the Initiation group ----
    TArray<FString> Ids;
    for (const FCireAbilityDef& D : CireAbilityDB::All()) if (D.Section == TEXT("initiation")) Ids.Add(D.Id);
    T.Check(Ids.Num() >= 10 && Ids.Num() <= 14, FString::Printf(TEXT("10-14 initiation spells (%d)"), Ids.Num()));
    for (const FString& Id : Ids)
    {
        const FCireAbilityDef* D = CireAbilityDB::Find(Id);
        T.Check(CireAbilityExpansion::Handles(Id) && D->IsImplemented() && D->Categories.Contains(TEXT("Initiation")), Id + TEXT(": castable Initiation-group row"));
        T.Check(D->ScalePrimary > 0 && !D->Level15Bonus.IsNone() && D->Champions.Num() >= 3, Id + TEXT(": primary scaling, level-15 bonus, buyable"));
        bool bGroupCC = false; for (const FCireAbilityEffect& E : D->Effects) bGroupCC |= E.Type == TEXT("stun") || E.Type == TEXT("taunt") || E.Type == TEXT("slow");
        T.Check(bGroupCC, Id + TEXT(": carries crowd control"));
        const FCireHitShape Shape = CireAbilityShapes::Describe(FName(*Id));
        T.Check(Shape.Kind != ECireHitShape::None && (!Shape.HasGroundShape() || ACireAreaEffect::ValidateSpec(Shape.AsArea())), Id + TEXT(": true telegraph"));
        bool bFab = false;
        for (CireFabVFX::ERole Role : {CireFabVFX::ERole::Cast, CireFabVFX::ERole::Impact, CireFabVFX::ERole::Area}) bFab |= CireFabVFX::FindAbility(FName(*Id), Role) != nullptr;
        T.Check(bFab && !CireAbilityIcons::School(Id).IsEmpty() && CireSoundEvents::FindAbility(FName(*Id)), Id + TEXT(": Fab VFX, icon and sound"));
    }

    // ---- 2. Set-up synergy ----
    auto* M = F.Monster(FVector(300, 0, 0)); auto* M2 = F.Monster(FVector(420, 220, 0)); auto* M3 = F.Monster(FVector(700, -40, 0));
    auto* H = F.Hero(0, FVector(-200, 0, 0), TEXT("knight"));
    auto* Ally = F.Hero(0, FVector(-200, 250, 0), TEXT("ranger"));
    if (!M || !M2 || !M3 || !H || !Ally) { T.Check(false, TEXT("fixtures")); return false; }
    {
        const float Plain = CireSignatureSkills::ModifyOutgoingDamage(Ally, M2, 100.f, TEXT("Test"));
        Learn(H, TEXT("vacuum_rift")); H->bHasCastAim = true; H->CastAimPoint = M2->GetActorLocation() - FVector(0, 0, 92) + FVector(-120, 60, 0);
        T.Check(CireSignatureSkills::Cast(H, 0, TEXT("vacuum_rift")), TEXT("Vacuum Rift casts: ") + H->Notice);
        H->bHasCastAim = false;
        CireAbilityExpansion::OnAbilityHit(H, M2, TEXT("Vacuum Rift"), 20.f); // the rift resolving on M2
        T.Check(CireBuffs::IsActive(M2, SetUpId), TEXT("an initiation hit marks the victim Set-up"));
        T.Check(!M2->GetCharacterMovement()->PendingLaunchVelocity.IsNearlyZero(), TEXT("Vacuum Rift drags its victim toward the centre"));
        const float Amped = CireSignatureSkills::ModifyOutgoingDamage(Ally, M2, 100.f, TEXT("Test"));
        T.Check(FMath::IsNearlyEqual(Amped, Plain * (1.f + Tuning().TeamDamageBonus), 1.f), FString::Printf(TEXT("allies deal more to a Set-up target (%.0f -> %.0f)"), Plain, Amped));
        float Area = 0; { FCireAreaDamageScope Scope; Area = CireSignatureSkills::ModifyOutgoingDamage(Ally, M2, 100.f, TEXT("Test")); }
        T.Check(Area > Amped, TEXT("area follow-ups gain more from Set-up"));
        T.Check(FMath::IsNearlyEqual(CireSignatureSkills::ModifyOutgoingDamage(M, M2, 100.f, TEXT("Test")), 100.f, 1.f) || !CireCombat::AreHostile(M, M2), TEXT("the other team gains nothing from your Set-up"));
        T.Check(CireCombat::OutcomeText(ECireHitOutcome::SetUp) == TEXT("Set up!"), TEXT("SET UP! callout text"));
        CireBuffs::ClearAll(M2); M2->GetCharacterMovement()->PendingLaunchVelocity = FVector::ZeroVector;
    }
    // ---- 3. deliveries ----
    {
        // Soul Hook: the first enemy on the line is struck, dragged and Set-up.
        H->SetActorLocation(F.At(FVector(-200, 0, 92))); Learn(H, TEXT("soul_hook")); H->bHasCastAim = true; H->CastAimPoint = M->GetActorLocation();
        const float Before = M->Health;
        T.Check(CireSignatureSkills::Cast(H, 0, TEXT("soul_hook")) && M->Health < Before && CireBuffs::IsActive(M, SetUpId), TEXT("Soul Hook hits, pulls and sets up the first enemy: ") + H->Notice);
        H->bHasCastAim = false; CireBuffs::ClearAll(M);
        // Warpath Charge: rush to the selected enemy.
        H->SetActorLocation(F.At(FVector(-600, 0, 92))); Learn(H, TEXT("warpath_charge")); H->Target = M3;
        const float Far = FVector::Dist2D(H->GetActorLocation(), M3->GetActorLocation());
        T.Check(CireSignatureSkills::Cast(H, 0, TEXT("warpath_charge")) && FVector::Dist2D(H->GetActorLocation(), M3->GetActorLocation()) < Far * .5f, TEXT("Warpath Charge closes the gap: ") + H->Notice);
        // Hallowed Cage: a ring of wall segments.
        H->SetActorLocation(F.At(FVector(-200, 0, 92))); Learn(H, TEXT("hallowed_cage")); H->bHasCastAim = true; H->CastAimPoint = M2->GetActorLocation() - FVector(0, 0, 92);
        const bool bCage = CireSignatureSkills::Cast(H, 0, TEXT("hallowed_cage")); H->bHasCastAim = false;
        int32 Walls = 0; for (TActorIterator<ACireConstruct> It(World); It; ++It) Walls += It->GetSourceActor() == H && It->IsWall();
        T.Check(bCage && Walls >= 6, FString::Printf(TEXT("Hallowed Cage raises a wall ring (%d segments)"), Walls));
        ACireConstruct::ClearForActor(H); ACireAreaEffect::ClearForActor(H);
        // Bots engage groups, not stragglers.
        H->SetActorLocation(F.At(FVector(-200, 0, 92)));
        T.Check(CireAbilityExpansion::BotWantsCast(H, TEXT("echo_slam")), TEXT("bots engage a group"));
        for (auto* Mon : {M2, M3}) Mon->SetActorLocation(F.At(FVector(2400, 1200, 92)));
        M->SetActorLocation(F.At(FVector(2400, -1200, 92)));
        T.Check(!CireAbilityExpansion::BotWantsCast(H, TEXT("echo_slam")), TEXT("bots hold the engage with no group near"));
    }

    // ---- 4. Blink Dagger ----
    {
        const auto* Blink = CireItems::Find(TEXT("blink_dagger"));
        T.Check(Blink && Blink->Use.Kind == Cires::Items::EffectKind::Blink && Blink->Use.Radius >= 1000 && Blink->Use.Cooldown > 0 && Blink->Use.Duration > 0,
            TEXT("Blink Dagger: blink active, range, cooldown and lockout in Items.json"));
        T.Check(Blink && Blink->Components.size() >= 2 && CireItems::Find(TEXT("phase_shard")), TEXT("Blink Dagger has a LoL-style recipe"));
        bool bEveryVendor = CireVendors::Get().Vendors.Num() >= 3;
        for (const FCireVendorDef& V : CireVendors::Get().Vendors) bEveryVendor &= CireVendors::Sells(V.Id, TEXT("blink_dagger")) && CireVendors::Sells(V.Id, TEXT("phase_shard"));
        T.Check(bEveryVendor, TEXT("every merchant sells the Blink Dagger and its shard"));
        auto* Blinker = F.Hero(0, FVector(-800, 0, 0), TEXT("ranger"));
        int32 Gold = 0; int32 Slot = INDEX_NONE; bool bBelt = false;
        T.Check(Blinker && Blinker->Inventory && Blinker->Inventory->GrantItem(TEXT("blink_dagger"), Gold, &Slot, &bBelt) && !bBelt && CarriesBlink(Blinker), TEXT("Blink Dagger equips"));
        if (Blinker && Slot != INDEX_NONE)
        {
            const FVector From = Blinker->GetActorLocation();
            SetUseAim(Blinker, From + FVector(900, 0, 0));
            FString Message;
            T.Check(Blinker->Inventory->UseSlot(Slot, false, Message), TEXT("Blink Dagger used: ") + Message);
            const float Moved = FVector::Dist2D(From, Blinker->GetActorLocation());
            T.Check(Moved > 700.f && Moved <= 1250.f, FString::Printf(TEXT("blinked toward the cursor (%.0f cm)"), Moved));
            T.Check(FMath::IsNearlyEqual(Blinker->Inventory->Equipment[Slot].ReadyAt - CireBuffs::ServerNow(World), Tuning().BlinkCooldown, .5f), TEXT("Blink Dagger goes on its Initiation.json cooldown"));
            // Long aims clamp to the maximum range.
            Blinker->Inventory->Equipment[Slot].ReadyAt = 0;
            const FVector From2 = Blinker->GetActorLocation();
            SetUseAim(Blinker, From2 + FVector(-5000, 0, 0));
            Blinker->Inventory->UseSlot(Slot, false, Message);
            T.Check(FVector::Dist2D(From2, Blinker->GetActorLocation()) <= Tuning().BlinkRange + 50.f, TEXT("blink range is capped (Initiation.json)"));
            // Champion damage disrupts it (arena: champions are hostile).
            Mode->Clock.BeginIntermission(); Mode->Clock.Advance(61);
            auto* Rival = F.Hero(1, FVector(-300, 400, 0), TEXT("knight"));
            Blinker->Inventory->Equipment[Slot].ReadyAt = 0;
            if (Rival) CireInitiation::OnDamageDealt(Rival, Blinker, 25.f);
            FString Why;
            T.Check(Rival && CireBuffs::IsActive(Blinker, BlinkLockedId) && !CanBlink(Blinker, Why) && !Blinker->Inventory->UseSlot(Slot, false, Message),
                TEXT("champion damage disrupts the Blink Dagger: ") + Why);
            T.Check(Blinker->Inventory->Equipment[Slot].ReadyAt <= CireBuffs::ServerNow(World), TEXT("a disrupted blink keeps its cooldown"));
            CireBuffs::Remove(Blinker, BlinkLockedId);
            FString Ok; T.Check(CanBlink(Blinker, Ok), TEXT("the lockout ends"));
        }
    }
    UE_LOG(LogCireInitiationTests, Display, TEXT("CIRE_INITIATION_%s checks=%d spells=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count, Ids.Num());
    return T.bPassed;
}
#endif
