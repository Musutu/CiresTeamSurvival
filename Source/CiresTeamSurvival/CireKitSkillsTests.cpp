// kits-complete: native checks for the 63 roster signature skills, universal potency scaling, heal cast times,
// shield-gated offers, the Mech Tank's protection priority and the big Bear.
#include "CireKitSkills.h"

#if !UE_BUILD_SHIPPING
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireChampionRoster.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCrowdControl.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireMobility.h"
#include "CireScalingKits.h"
#include "CireSignatureSkills.h"
#include "CireSkillshot.h"
#include "CireSummon.h"
#include "CireTargeting.h"
#include "CireTechConstructs.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Rules/CiresRules.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitSkillsTests, Log, All);

namespace
{
struct FChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireKitSkillsTests, Error, TEXT("CIRE_KIT_SKILLS_CHECK_FAIL %s"), *Message); }
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
        if (auto* S = UCireKitSkillsSubsystem::Get(World)) { S->Zones.Reset(); S->Hots.Reset(); S->Burns.Reset(); S->Motes.Reset(); S->Channels.Reset(); S->Dragons.Reset(); }
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
struct FProfileKit { const TCHAR* Profile; TArray<const TCHAR*> Ids; };
}

bool CireKitSkills::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FChecks T; FFixture F(Mode);
    UWorld* World = Mode->GetWorld();
    auto* Sub = UCireKitSkillsSubsystem::Get(World);
    T.Check(Sub != nullptr, TEXT("kit subsystem exists"));
    if (!Sub) return false;
    // ---- 1. data: all 63 rows implemented, sold to their champion, shaped, scaled ----
    T.Check(AllIds().Num() == 63, FString::Printf(TEXT("63 roster kit skills (%d)"), AllIds().Num()));
    int32 Planned = 0; for (const FCireAbilityDef& D : CireAbilityDB::All()) Planned += !D.IsImplemented();
    T.Check(Planned == 0, FString::Printf(TEXT("no planned abilities remain (%d)"), Planned));
    for (const FString& Id : AllIds())
    {
        const FCireAbilityDef* D = CireAbilityDB::Find(Id);
        T.Check(D && D->IsImplemented() && !D->Section.IsEmpty(), Id + TEXT(": implemented row with a Skill Shop section"));
        if (!D) continue;
        T.Check(D->IsPassive() == IsPassive(Id) && ACireHero::IsPassive(Id) == D->IsPassive() && ACireHero::IsUltimate(Id) == D->IsUltimate(), Id + TEXT(": kind agrees with the runtime"));
        T.Check(D->ScalePrimary > 0 || D->PotencyPerPoint > 0, Id + TEXT(": scales with the primary stat"));
        T.Check(D->IsPassive() || !D->Level15Bonus.IsNone() || !D->Level15Special.IsEmpty(), Id + TEXT(": level-15 bonus"));
        T.Check(!D->IsPassive() || !D->Aura15.IsNone(), Id + TEXT(": level-15 team aura"));
        T.Check(D->SignatureOf.Num() > 0 && D->Champions.Num() > 0, Id + TEXT(": owned and purchasable"));
        for (const FString& C : D->SignatureOf) T.Check(CireAbilityDB::CanLearn(C, Id), C + TEXT(" can buy ") + Id);
        if (D->ScaleComponent == TEXT("heal") && !D->IsPassive())
            T.Check(D->CastTime > 0 || D->EffectLabel.Contains(TEXT("per ")), Id + TEXT(": a heal has a cast time (or is a timed heal over time)"));
        const FCireHitShape Shape = CireAbilityShapes::Describe(FName(*Id));
        T.Check(D->IsPassive() ? Shape.Kind == ECireHitShape::None : Shape.Kind != ECireHitShape::None, Id + TEXT(": telegraph shape"));
        if (Shape.HasGroundShape()) T.Check(ACireAreaEffect::ValidateSpec(Shape.AsArea()), Id + TEXT(": telegraph is a valid ground boundary"));
        if (Shape.bGroundAim)
        {
            const auto Desc = CireTargeting::Describe(Id);
            T.Check(Desc.Kind == ECireTargetKind::Ground && Desc.bHasFootprint, Id + TEXT(": aims on the ground with its true footprint"));
        }
    }
    // Every roster champion's full kit is implemented and purchasable.
    for (const auto& P : CireChampionRoster::All())
    {
        const auto* Kit = CireAbilityDB::Kit(P.Id);
        T.Check(Kit && Kit->Purchasable.Num() == Kit->PurchasableImplemented.Num(), P.Id + TEXT(": every purchasable skill is implemented"));
        for (const auto& A : P.Actives) T.Check(A.IsImplemented() && CireAbilityDB::CanLearn(P.Id, A.Id), P.Id + TEXT(" buys ") + A.Id);
        T.Check(P.Passive.IsImplemented() && P.Ultimate.IsImplemented(), P.Id + TEXT(": passive and ultimate implemented"));
    }
    // ---- 2. every active and ultimate casts for its champion ----
    auto* M = F.Monster(FVector(260, 0, 0)); auto* M2 = F.Monster(FVector(420, 160, 0)); auto* M3 = F.Monster(FVector(600, -120, 0));
    if (!M || !M2 || !M3) { T.Check(false, TEXT("monster fixtures spawned")); return false; }
    TArray<FString> ProfilesSeen;
    for (const FString& Id : AllIds())
    {
        const FCireAbilityDef* D = CireAbilityDB::Find(Id);
        if (!D || D->IsPassive()) continue;
        const FString Profile = D->SignatureOf.Num() ? D->SignatureOf[0] : FString();
        auto* H = F.Hero(0, FVector(-200, 0, 0), *Profile);
        auto* Ally = F.Hero(0, FVector(-200, 200, 0), TEXT("knight"));
        if (!H || !Ally) { T.Check(false, Id + TEXT(": fixtures")); continue; }
        Ally->Health = Ally->MaxHealth * .5f;
        for (auto* Mon : {M, M2, M3}) { Mon->Health = Mon->MaxHealth; CireBuffs::ClearAll(Mon); Mon->SetActorLocation(F.At(FVector(Mon == M ? 260 : Mon == M2 ? 420 : 600, Mon == M2 ? 160 : Mon == M3 ? -120 : 0, 92))); }
        Learn(H, Id); H->Target = D->Targeting == TEXT("ally") ? static_cast<AActor*>(Ally) : static_cast<AActor*>(M); H->bHasCastAim = true;
        FVector Aim = M->GetActorLocation() - FVector(0, 0, 92);
        if (D->Category == TEXT("construct") || Id == TEXT("troll_blood_leap") || Id == TEXT("centaur_herd_call")) Aim = F.At(FVector(80, -300, 0));
        if (Id == TEXT("golem_moss_bloom") || Id == TEXT("dryad_grove_renewal") || Id == TEXT("keeper_beacon")) Aim = Ally->GetActorLocation() - FVector(0, 0, 92);
        if (Id == TEXT("paladin_pilgrim_light") || Id == TEXT("whisp_guiding_mote")) Aim = Ally->GetActorLocation() + (Ally->GetActorLocation() - H->GetActorLocation()) - FVector(0, 0, 92);
        H->CastAimPoint = Aim;
        if (D->Targeting == TEXT("enemy") && D->Range > 0 && D->Range <= 320.f) H->SetActorLocation(M->GetActorLocation() - FVector(170, 0, 0)); // melee reach
        const bool bCast = CireSignatureSkills::Cast(H, 0, Id);
        H->bHasCastAim = false;
        T.Check(bCast && H->Cooldowns[0] > 0 && H->GlobalCooldown > 0, Profile + TEXT(" casts ") + Id + TEXT(" (") + H->Notice + TEXT(")"));
        Sub->Tick(1.05f); Sub->Tick(1.05f); // zones, heals over time, channels
        ACireAreaEffect::ClearForActor(H); ACireConstruct::ClearForActor(H); ACireSkillshot::ClearForActor(H); ACireSummon::ClearForActor(H);
        Sub->Zones.Reset(); Sub->Hots.Reset(); Sub->Burns.Reset(); Sub->Motes.Reset(); Sub->Channels.Reset(); Sub->Dragons.Reset();
        for (TActorIterator<ACireConstruct> It(World); It; ++It) if (It->GetSourceActor() == H) It->Destroy();
        Mode->Heroes.Remove(H); Mode->Heroes.Remove(Ally); H->Destroy(); Ally->Destroy();
    }
    for (auto* Mon : {M, M2, M3}) { Mon->Health = Mon->MaxHealth; CireBuffs::ClearAll(Mon); Mon->SlowUntil = 0; Mon->ForcedVictim.Reset(); }
    // ---- 3. behaviour spot checks ----
    {
        // Gravewood Maul: triple threat + taunt.
        auto* Bear = F.Hero(0, FVector(40, 0, 0), TEXT("bear"));
        Learn(Bear, TEXT("bear_maul")); Bear->Target = M;
        T.Check(CireSignatureSkills::Cast(Bear, 0, TEXT("bear_maul")) && M->ForcedVictim.Get() == Bear && M->Health < M->MaxHealth, TEXT("Gravewood Maul hits and taunts"));
        // Ironroot Slumber: a channel that heals and guards; a heavy hit breaks it.
        Bear->Health = 1000; Learn(Bear, TEXT("bear_hibernate"));
        T.Check(CireSignatureSkills::Cast(Bear, 0, TEXT("bear_hibernate")) && CireBuffs::IsActive(Bear, TEXT("bear_hibernate")), TEXT("Ironroot Slumber starts a channel"));
        Sub->Tick(.6f);
        T.Check(Bear->Health > 1000.f, TEXT("the slumber heals"));
        const float Guarded = CireSignatureSkills::ModifyOutgoingDamage(M, Bear, 100.f, TEXT("Test"));
        T.Check(Guarded < 85.f, FString::Printf(TEXT("the slumber reduces damage (%.0f)"), Guarded));
        CireSignatureSkills::ModifyOutgoingDamage(M, Bear, Bear->MaxHealth * .3f, TEXT("Test"));
        T.Check(!CireBuffs::IsActive(Bear, TEXT("bear_hibernate")), TEXT("a heavy hit breaks the slumber"));
        // Ancient Hide: five hits in four seconds harden the hide.
        Bear->Skills = {TEXT("bear_ancient_hide")}; Bear->Cooldowns = {0.f};
        for (int32 I = 0; I < 5; ++I) CireSignatureSkills::ModifyOutgoingDamage(M, Bear, 10.f, TEXT("Test"));
        T.Check(CireBuffs::IsActive(Bear, TEXT("ancient_hide")), TEXT("Ancient Hide hardens after five hits"));
        // Big Bear: clearly larger than any human champion; the Elder form grows it further.
        T.Check(CireMovement::BodyScaleFor(*Bear) >= 1.4f && CireMovement::BodyScaleFor(*Bear) > CireMovement::Tuning().TankBodyScale, TEXT("the Bear is bigger than other tanks"));
        CireBuffs::Apply(Bear, TEXT("bear_colossus"), 5.f, Bear, 30);
        T.Check(CireMovement::BodyScaleFor(*Bear) > 1.8f, TEXT("Elder of the Deepwood grows the bear"));
        CireMovement::ApplyToHero(*Bear);
        T.Check(Bear->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() > 150.f, TEXT("the capsule follows the bear's size"));
        CireBuffs::ClearAll(Bear);
        Mode->Heroes.Remove(Bear); Bear->Destroy();
    }
    {
        // Relic Vow: the bound ally takes 30% less, the paladin takes it.
        auto* Pal = F.Hero(0, FVector(-300, 0, 0), TEXT("paladin_righteous"));
        auto* Ally = F.Hero(0, FVector(-300, 200, 0), TEXT("ranger"));
        Learn(Pal, TEXT("paladin_relic_vow")); Pal->Target = Ally;
        T.Check(CireSignatureSkills::Cast(Pal, 0, TEXT("paladin_relic_vow")) && CireBuffs::IsActive(Ally, TEXT("relic_vow")), TEXT("Relic Vow binds the ally"));
        const float PalBefore = Pal->Health;
        const float Taken = CireSignatureSkills::ModifyOutgoingDamage(M, Ally, 100.f, TEXT("Test"));
        T.Check(FMath::IsNearlyEqual(Taken, 70.f, 1.f) && Pal->Health < PalBefore, FString::Printf(TEXT("30%% of the damage is redirected to the paladin (%.0f)"), Taken));
        const auto* Inv = CireItems::InventoryOf(Ally);
        T.Check(Inv && Inv->BarrierHP > 0, TEXT("Relic Vow grants a barrier"));
        // Seed Mend: blooms after its delay (smart cast on the wounded ally).
        auto* Dryad = F.Hero(0, FVector(-500, 0, 0), TEXT("dryad"));
        Ally->Health = 400; Learn(Dryad, TEXT("dryad_seed_mend")); Dryad->Target = M;
        T.Check(CireSignatureSkills::Cast(Dryad, 0, TEXT("dryad_seed_mend")) && CireBuffs::IsActive(Ally, TEXT("seed_mend")), TEXT("Seed Mend smart-casts onto the wounded ally"));
        // Spirit Tether: heals per second while in range, snaps out of range.
        auto* Whisp = F.Hero(0, FVector(-500, -200, 0), TEXT("whisp"));
        Learn(Whisp, TEXT("whisp_spirit_tether")); Whisp->Target = Ally;
        const float Before = Ally->Health;
        T.Check(CireSignatureSkills::Cast(Whisp, 0, TEXT("whisp_spirit_tether")), TEXT("Spirit Tether links"));
        Sub->Tick(.3f);
        T.Check(Ally->Health > Before, TEXT("the tether heals"));
        Ally->SetActorLocation(Whisp->GetActorLocation() + FVector(1500, 0, 0));
        Sub->Tick(1.1f);
        T.Check(!CireBuffs::IsActive(Ally, TEXT("spirit_tether")), TEXT("the tether snaps beyond 9 m"));
        Ally->SetActorLocation(F.At(FVector(-300, 200, 92)));
        // Verdant Bloom: a heal zone ticks each second for allies inside.
        auto* Golem = F.Hero(0, FVector(-700, 0, 0), TEXT("ether_golem_support"));
        Ally->Health = 400; Learn(Golem, TEXT("golem_moss_bloom")); Golem->bHasCastAim = true; Golem->CastAimPoint = Ally->GetActorLocation() - FVector(0, 0, 92);
        T.Check(CireSignatureSkills::Cast(Golem, 0, TEXT("golem_moss_bloom")) && Sub->Zones.Num() > 0, TEXT("Verdant Bloom opens a heal zone"));
        Golem->bHasCastAim = false;
        Sub->Tick(1.1f);
        T.Check(Ally->Health > 400.f, TEXT("allies in the bloom regenerate"));
        Sub->Zones.Reset(); Sub->Hots.Reset();
        for (ACireHero* X : {Pal, Ally, Dryad, Whisp, Golem}) { Mode->Heroes.Remove(X); ACireAreaEffect::ClearForActor(X); X->Destroy(); }
    }
    {
        // Ether Anchor / Root Snare roots; Deepwood Roar weakens.
        auto* Tank = F.Hero(0, FVector(40, 0, 0), TEXT("ether_golem_tank"));
        FCireAbilityDef Anchor = *CireAbilityDB::Find(TEXT("golem_ether_anchor"));
        OnAbilityHit(Tank, M, Anchor.Name, 10.f);
        T.Check(CireBuffs::IsActive(M, TEXT("npc_rooted")), TEXT("Ether Anchor roots its targets"));
        OnAbilityHit(Tank, M2, CireAbilityDB::Find(TEXT("bear_roar"))->Name, 10.f);
        T.Check(CireSignatureSkills::ModifyOutgoingDamage(M2, Tank, 100.f, TEXT("Test")) < 80.f, TEXT("Deepwood Roar weakens the monster's attacks"));
        // Construct pylons: the banner rallies, the lantern guards.
        Learn(Tank, TEXT("chieftain_banner")); // any hero may deploy the recipe in the test
        const auto Banner = CireTechConstructs::Deploy(Tank, TEXT("chieftain_banner"), F.At(FVector(-100, -300, 0)));
        T.Check(Banner.Num() == 1, TEXT("Blood-Oath Banner deploys as a construct"));
        if (Banner.Num()) { T.Check(PylonPulse(Banner[0], Tank, true, false, 1.f) && CireBuffs::IsActive(Tank, TEXT("blood_oath_banner")), TEXT("the banner rallies allies")); Banner[0]->Destroy(); }
        CireBuffs::ClearAll(M); CireBuffs::ClearAll(M2);
        Mode->Heroes.Remove(Tank); Tank->Destroy();
    }
    // ---- 4. universal primary scaling (potency), CC scaling, heal cast times, shield gating, openings ----
    {
        auto* Knight = F.Hero(0, FVector(-300, 0, 0), TEXT("knight"));
        Knight->Strength = 50;
        T.Check(FMath::IsNearlyEqual(CireKits::Potency(Knight, TEXT("iron_guard")), 1.2f, .001f), TEXT("potency: +0.4% per primary point (STR 50 = x1.20)"));
        Knight->Strength = 500;
        T.Check(FMath::IsNearlyEqual(CireKits::Potency(Knight, TEXT("iron_guard")), 1.4f, .001f), TEXT("potency caps at +40%"));
        T.Check(FMath::IsNearlyEqual(CireKits::ControlScale(Knight), 1.25f, .001f), TEXT("crowd-control durations cap at +25%"));
        Knight->Strength = 50;
        T.Check(FMath::IsNearlyEqual(CireKits::Potency(Knight, TEXT("shield_slam")), 1.f), TEXT("damage skills keep their own primary coefficient"));
        const FString Tip = CireKits::DescribeFor(Knight, TEXT("iron_guard"), 1);
        T.Check(Tip.Contains(TEXT("Potency: x1.20")), TEXT("the tooltip shows the potency line: ") + Tip);
        int32 Unscaled = 0; for (const FCireAbilityDef& D : CireAbilityDB::All()) Unscaled += D.ScalePrimary <= 0 && D.PotencyPerPoint <= 0;
        T.Check(Unscaled == 0, FString::Printf(TEXT("every ability scales with the primary stat (%d do not)"), Unscaled));
        for (const TCHAR* Id : {TEXT("second_wind"), TEXT("bastion_of_dawn"), TEXT("last_stand")})
        { const auto* D = CireAbilityDB::Find(Id); T.Check(D && D->CastTime > 0, FString(Id) + TEXT(" has a cast time")); }
        const auto* Slam = CireAbilityDB::Find(TEXT("shield_slam"));
        T.Check(Slam && Slam->Requires == TEXT("shield") && !CireAbilityDB::CanLearn(TEXT("bear"), TEXT("shield_slam")) && CireAbilityDB::CanLearn(TEXT("knight"), TEXT("shield_slam")),
            TEXT("Shield Slam requires a shield"));
        for (const auto& P : CireChampionRoster::All())
            for (const FString& Id : CireAbilityDB::OpeningSkills(P.Id))
            {
                const auto* D = CireAbilityDB::Find(Id);
                T.Check(D && (D->Requires != TEXT("shield") || CireAbilityDB::CanLearn(P.Id, Id)), P.Id + TEXT(": no shield skill in a shieldless opening (") + Id + TEXT(")"));
            }
        for (const auto& S : Cires::OpeningSkillPool(Cires::SkillDraftRole::Support))
        { const auto* D = CireAbilityDB::Find(UTF8_TO_TCHAR(S.Id.c_str())); T.Check(D && D->ScaleComponent == TEXT("heal"), FString(UTF8_TO_TCHAR(S.Id.c_str())) + TEXT(": the support opening offers heals only")); }
        // A shieldless tank's opening offer never contains a shield skill.
        auto* Bear = F.Hero(0, FVector(-500, 0, 0), TEXT("bear"));
        T.Check(Bear && Bear->Offers.Num() == 0, TEXT("fixture offers cleared"));
        Bear->Skills.Reset(); Bear->Progression.LearnedSkills.clear(); Bear->Progression.NextAugmentLevel = 1; Bear->RefreshOffer();
        T.Check(!Bear->Offers.Contains(TEXT("shield_slam")), TEXT("the Bear is never offered Shield Slam"));
        for (ACireHero* X : {Knight, Bear}) { Mode->Heroes.Remove(X); X->Destroy(); }
    }
    UE_LOG(LogCireKitSkillsTests, Display, TEXT("CIRE_KIT_SKILLS_%s checks=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count);
    return T.bPassed;
}
#endif
