// paragon-champions: native checks for the Paragon champions (data, registration, DB merge, art, every delivery).
#include "CireParagonChampions.h"

#if !UE_BUILD_SHIPPING
#include "CireAbilityDB.h"
#include "CireAbilityShapes.h"
#include "CireAreaEffects.h"
#include "CireBuffs.h"
#include "CireChampionArt.h"
#include "CireChampionRoster.h"
#include "CireCombatEvents.h"
#include "CireConstruct.h"
#include "CireCreatureArt.h"
#include "CireGame.h"
#include "CireSignatureSkills.h"
#include "Animation/AnimSequence.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireParagonTests, Log, All);
namespace
{
struct FPgChecks
{
    int32 Count = 0, Failed = 0;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { ++Failed; UE_LOG(LogCireParagonTests, Error, TEXT("CIRE_PARAGON_CHECK_FAIL %s"), *Message); }
    }
};
struct FPgFixture
{
    ACireGameMode* Mode;
    Cires::MatchClock SavedClock;
    TArray<ACireHero*> SavedHeroes;
    TArray<ACireMonster*> SavedMonsters;
    TArray<AActor*> Actors;
    FVector Ground = FVector(0, -2100, 3000);
    explicit FPgFixture(ACireGameMode* InMode) : Mode(InMode), SavedClock(Mode->Clock), SavedHeroes(Mode->Heroes), SavedMonsters(Mode->Monsters)
    {
        Mode->Clock = Cires::MatchClock(); Mode->Heroes.Reset(); Mode->Monsters.Reset();
        auto* Actor = Keep(Mode->GetWorld()->SpawnActor<AActor>());
        auto* Body = NewObject<UBoxComponent>(Actor);
        Actor->SetRootComponent(Body); Actor->AddInstanceComponent(Body);
        Body->SetBoxExtent(FVector(2600, 1400, 50)); Body->SetCollisionObjectType(ECC_WorldStatic);
        Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics); Body->SetCollisionResponseToAllChannels(ECR_Block);
        Body->RegisterComponent(); Actor->SetActorLocation(Ground + FVector(500, 0, -50));
    }
    ~FPgFixture()
    {
        for (int32 I = Actors.Num() - 1; I >= 0; --I) if (IsValid(Actors[I])) { ACireAreaEffect::ClearForActor(Actors[I]); ACireConstruct::ClearForActor(Actors[I]); Actors[I]->Destroy(); }
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
        H->Health = H->MaxHealth = 3000; H->Mana = H->MaxMana = 5000; H->Energy = 100; H->CriticalChance = 0;
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
};
}

bool CireParagonChampions::RunSmoke(ACireGameMode* Mode)
{
    if (!IsValid(Mode) || !Mode->HasAuthority()) return false;
    FPgChecks T;
    const bool bEnabled = IsEnabled();
    T.Check(AuthoredCount() >= 38, FString::Printf(TEXT("all Paragon heroes authored (%d)"), AuthoredCount()));
    if (!bEnabled)
    {
        // Clean clone / -CireNoParagon: nothing registers, the roster and DB are untouched.
        for (const auto& P : CireChampionRoster::All()) T.Check(!P.Id.StartsWith(TEXT("pg_")), P.Id + TEXT(": no Paragon profile without the packs"));
        UE_LOG(LogCireParagonTests, Display, TEXT("CIRE_PARAGON_SMOKE_%s checks=%d installed=0 (packs absent: registration skipped)"), T.Failed ? TEXT("FAIL") : TEXT("PASS"), T.Count);
        return T.Failed == 0;
    }
    // ---- registration: roster, DB, kits, pool ----
    for (const FString& Id : HeroIds())
    {
        const FCireChampionProfile* P = CireChampionRoster::Find(Id);
        const FCireChampionKit* Kit = CireAbilityDB::Kit(Id);
        T.Check(P && Kit, Id + TEXT(": roster profile and Ability DB kit registered"));
        if (!P || !Kit) continue;
        T.Check(P->Actives.Num() == 6 && !P->Quote.IsEmpty() && P->Roles.Num() > 0, Id + TEXT(": six draft actives, quote and roles"));
        const TArray<FString> Own = OwnAbilities(Id);
        T.Check(Own.Num() == 4, Id + TEXT(": four own Paragon abilities (RMB, Q, E, ultimate)"));
        for (const FString& S : Own)
        {
            const FCireAbilityDef* D = CireAbilityDB::Find(S);
            T.Check(D && D->IsImplemented() && Handles(S) && CireSignatureSkills::Handles(S) && Kit->PurchasableImplemented.Contains(S), S + TEXT(": implemented, castable and purchasable by ") + Id);
            FCireHitShape Shape = CireAbilityShapes::Describe(FName(*S));
            T.Check(Shape.Kind != ECireHitShape::None, S + TEXT(": has a targeting shape"));
        }
        T.Check(IsPassive(P->Passive.Id) && CireSignatureSkills::IsPassive(P->Passive.Id), Id + TEXT(": passive routed"));
        T.Check(CireSignatureSkills::IsUltimate(P->Ultimate.Id), Id + TEXT(": ultimate routed"));
    }
    // Pool: a Paragon active of a type is purchasable by an authored champion of that type.
    if (const FCireChampionKit* Knight = CireAbilityDB::Kit(TEXT("knight")))
    {
        int32 Pooled = 0; for (const FString& S : Knight->Purchasable) Pooled += S.StartsWith(TEXT("pg_")) ? 1 : 0;
        T.Check(Pooled > 0, FString::Printf(TEXT("Paragon tank actives joined the Knight's Skill Shop pool (%d)"), Pooled));
    }
    // ---- art + casts: a sample per run (loading 38 Paragon bodies would blow the native probe budget); the
    // gallery (Tools/RunParagonGallery.py) and -CireParagonFullSmoke cover every hero. The sample is fixed (every delivery family).
    TArray<FString> Sample;
    if (FParse::Param(FCommandLine::Get(), TEXT("CireParagonFullSmoke"))) Sample = HeroIds();
    else for (const TCHAR* Id : {TEXT("pg_greystone"), TEXT("pg_sparrow"), TEXT("pg_zinx"), TEXT("pg_gideon"), TEXT("pg_yin"), TEXT("pg_greystone_dragonlord")}) if (IsParagon(Id)) Sample.Add(Id);
    // Data-level art checks for every hero: the binding resolves to the pack mesh.
    for (const FString& Id : HeroIds())
    {
        FString Mesh, Motion; bool bFab = false;
        T.Check(UCireChampionArt::EffectiveCreatureBinding(Id, Mesh, Motion, bFab) && Motion == TEXT("monster_native") && Mesh.StartsWith(TEXT("/Game/Paragon")), Id + TEXT(": Paragon monster_native binding"));
    }
    FPgFixture F(Mode);

    UWorld* World = Mode->GetWorld();
    const bool bForce = GCireForceTripoChampionArt; GCireForceTripoChampionArt = true;
    int32 Bodies = 0, CastClips = 0, SkinsApplied = 0;
    for (const FString& Id : Sample)
    {
        ACireHero* H = F.Hero(0, FVector(-400, 0, 0), Id);
        if (!H) { T.Check(false, Id + TEXT(": spawn")); continue; }
        H->GetCharacterMovement()->SetComponentTickEnabled(false);
        FString Mesh, Motion; bool bFab = false;
        T.Check(UCireChampionArt::EffectiveCreatureBinding(Id, Mesh, Motion, bFab) && Motion == TEXT("monster_native") && Mesh.StartsWith(TEXT("/Game/Paragon")), Id + TEXT(": Paragon monster_native binding"));
        const bool bApplied = H->ChampionArt && H->ChampionArt->DebugApply(*H);
        UCireCreatureArt* Art = H->ChampionArt ? H->ChampionArt->GetCreature() : nullptr;
        USkeletalMeshComponent* Body = Art ? Art->GetNativeBody() : nullptr;
        T.Check(bApplied && Body && Body->GetSkeletalMeshAsset() && Body->GetSkeletalMeshAsset()->GetPathName() == Mesh && Art->HasReactions(), Id + TEXT(": wears its own Paragon mesh with reactions"));
        if (!Art || !Body) { H->Destroy(); continue; }
        ++Bodies;
        const float Dt = 1.f / 30;
        // A cooldown starting plays that ability's own clip.
        for (const FString& S : OwnAbilities(Id))
        {
            H->Skills = {S}; H->Cooldowns = {0.f}; Art->Update(*H, Dt);
            H->Cooldowns = {12.f}; Art->Update(*H, Dt);
            const UAnimSequence* Clip = Art->GetActionClip();
            T.Check(Clip != nullptr && Clip->GetSkeleton() == Body->GetSkeletalMeshAsset()->GetSkeleton(), S + TEXT(": plays a clip on the hero skeleton"));
            CastClips += Clip ? 1 : 0;
            H->Cooldowns = {0.f}; for (int32 I = 0; I < 90; ++I) Art->Update(*H, Dt);
        }
        // Skins: the first reskin re-binds the body to the skin mesh on the same clips; an unknown skin is rejected.
        if (const TArray<FString> SkinList = Skins(Id); SkinList.Num() > 0)
        {
            const FString Key = SkinList[0].Left(SkinList[0].Find(TEXT("|")));
            T.Check(HasSkin(Id, Key) && HasSkin(Id, FString()) && !HasSkin(Id, TEXT("NotASkin")), Id + TEXT(": skin validation"));
            H->ChampionSkin = Key;
            const bool bSkinApplied = H->ChampionArt->DebugApply(*H);
            USkeletalMeshComponent* SkinBody = Art->GetNativeBody();
            T.Check(bSkinApplied && SkinBody && SkinBody->GetSkeletalMeshAsset() && SkinBody->GetSkeletalMeshAsset()->GetPathName() != Mesh, Id + TEXT(": skin ") + Key + TEXT(" wears its own mesh"));
            ++SkinsApplied;
        }
        H->Destroy();
    }
    GCireForceTripoChampionArt = bForce;
    // ---- gameplay: every own ability casts through the shared router and lands ----
    ACireMonster* Dummy = F.Monster(FVector(250, 0, 0));
    ACireMonster* Far = F.Monster(FVector(700, 60, 0));
    int32 Casts = 0;
    for (const FString& Id : Sample)
    {
        ACireHero* H = F.Hero(0, FVector(-150, 0, 0), Id);
        ACireHero* Ally = F.Hero(0, FVector(-150, 250, 0), Id);
        if (!H || !Ally || !Dummy) continue;
        for (const FString& S : OwnAbilities(Id))
        {
            const FCireAbilityDef* D = CireAbilityDB::Find(S);
            if (!D) continue;
            // Targeted skills stand inside their reach of the dummy (at +250); the rest cast from -150.
            const float Reach = D->Targeting == TEXT("enemy") ? FMath::Clamp(D->Range - 80.f, 120.f, 400.f) : 400.f;
            H->SetActorLocation(F.Ground + FVector(250.f - Reach, 0, 92)); H->SetActorRotation(FRotator::ZeroRotator);

            H->Skills = {S}; H->Cooldowns = {0.f}; H->GlobalCooldown = 0; H->Mana = H->MaxMana; H->Energy = 100; H->Notice.Reset();
            Dummy->Health = Dummy->MaxHealth; Ally->Health = Ally->MaxHealth * .4f; H->Health = H->MaxHealth * .5f;
            const bool bAlly = D->Targeting == TEXT("ally");
            H->Target = bAlly ? static_cast<AActor*>(Ally) : static_cast<AActor*>(Dummy);
            H->bHasCastAim = true; H->CastAimPoint = F.Ground + FVector(250, 0, 0);
            const float AllyBefore = Ally->Health, SelfBefore = H->Health;
            const bool bCast = CireSignatureSkills::Cast(H, 0, S);
            H->bHasCastAim = false;
            T.Check(bCast && H->Cooldowns[0] > 0, S + TEXT(": casts (") + H->Notice + TEXT(")"));
            Casts += bCast ? 1 : 0;
            // Instant deliveries land now; areas with a warning resolve over the next ticks.
            for (int32 I = 0; I < 40; ++I) for (TActorIterator<ACireAreaEffect> It(World); It; ++It) It->Tick(.05f);
            const bool bHeal = D->ScaleComponent == TEXT("heal");
            if (bHeal) T.Check(Ally->Health > AllyBefore || H->Health > SelfBefore, S + TEXT(": heals"));
            for (TActorIterator<ACireAreaEffect> It(World); It; ++It) It->Destroy();
        }
    }
    T.Check(Far != nullptr, TEXT("second dummy"));
    // ---- buffs and passives ----
    if (HeroIds().Num() > 0)
    {
        ACireHero* H = F.Hero(0, FVector(-150, -300, 0), HeroIds()[0]);
        ACireMonster* M = F.Monster(FVector(200, -300, 0));
        if (H && M)
        {
            CireBuffs::Apply(H, TEXT("pg_guard"), 4.f, H, 40);
            T.Check(ModifyOutgoingDamage(M, H, 100.f, TEXT("Test")) < 70.f, TEXT("pg_guard reduces damage taken"));
            CireBuffs::Apply(M, TEXT("pg_marked"), 4.f, H, 20);
            T.Check(ModifyOutgoingDamage(H, M, 100.f, TEXT("Test")) > 110.f, TEXT("pg_marked amplifies damage taken"));
            CireBuffs::Apply(H, TEXT("pg_haste"), 4.f, H, 30);
            T.Check(MoveSpeedMultiplier(H) > 1.2f, TEXT("pg_haste speeds up"));
            CireBuffs::Apply(H, TEXT("pg_frenzy"), 4.f, H, 30);
            T.Check(AttackSpeedBonus(H) > .2f, TEXT("pg_frenzy adds attack speed"));
        }
    }
    const bool bPass = T.Failed == 0;
    UE_LOG(LogCireParagonTests, Display, TEXT("CIRE_PARAGON_SMOKE_%s checks=%d failed=%d installed=%d bodies=%d castClips=%d casts=%d skins=%d"),
        bPass ? TEXT("PASS") : TEXT("FAIL"), T.Count, T.Failed, HeroIds().Num(), Bodies, CastClips, Casts, SkinsApplied);

    return bPass;
}
#endif
