// kit-editor: native checks for the champion kit templates (CireKitEditor). Run by the combat expansion probe.
#include "CireKitEditor.h"

#if !UE_BUILD_SHIPPING
#include "CireAbilityDB.h"
#include "CireChampionRoster.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireScalingKits.h"
#include "CireSkillShop.h"
#include "Rules/CiresRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/ScopeExit.h"
#include "NiagaraComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitEditorTests, Log, All);

namespace
{
struct FKitChecks
{
    int32 Count = 0; bool bPassed = true;
    void Check(bool bCondition, const FString& Message)
    {
        ++Count;
        if (!bCondition) { bPassed = false; UE_LOG(LogCireKitEditorTests, Error, TEXT("CIRE_KIT_EDITOR_CHECK_FAIL %s"), *Message); }
    }
};
}

bool CireKitEditor::RunTests(ACireGameMode* Mode)
{
    FKitChecks T;
    if (!IsValid(Mode) || !Mode->GetWorld()) return false;
    UWorld* World = Mode->GetWorld();
    TArray<AActor*> Spawned;
    TMap<FString, FCireKitTemplate> Fake;
    ON_SCOPE_EXIT
    {
        for (AActor* A : Spawned) if (IsValid(A)) A->Destroy();
        DebugOverride(nullptr);
        CireAbilityDB::Reload();
    };

    // ---------------- the pool is the whole Ability Database, grouped like the Skill Shop ----------------
    const TArray<FCireAbilityDef>& All = CireAbilityDB::All();
    T.Check(All.Num() >= 100, TEXT("ability database loaded"));
    {
        const auto Groups = Pool(FPoolFilter());
        int32 Listed = 0; TSet<FString> Seen; bool bSorted = true;
        for (const auto& G : Groups)
        {
            for (int32 I = 0; I < G.Value.Num(); ++I)
            {
                ++Listed; Seen.Add(G.Value[I]->Id);
                T.Check(SectionOf(*G.Value[I]) == G.Key.Id, G.Value[I]->Id + TEXT(" listed in its own section"));
                if (I > 0 && G.Value[I - 1]->Name > G.Value[I]->Name) bSorted = false;
            }
        }
        T.Check(Listed == All.Num() && Seen.Num() == All.Num(), FString::Printf(TEXT("every ability listed exactly once (%d / %d)"), Listed, All.Num()));
        T.Check(bSorted, TEXT("sections sorted by name"));
        const TArray<FPoolSection> Secs = Sections();
        static const TCHAR* Known[] = {TEXT("spell"), TEXT("attack"), TEXT("defensive"), TEXT("control"), TEXT("summon"), TEXT("construct"), TEXT("passive"), TEXT("ultimate")};
        for (int32 I = 0; I < UE_ARRAY_COUNT(Known); ++I) T.Check(Secs.IsValidIndex(I) && Secs[I].Id == Known[I], FString::Printf(TEXT("periodic-table order %s"), Known[I]));
        for (const FCireAbilityDef& D : All) T.Check(Secs.ContainsByPredicate([&](const FPoolSection& S) { return S.Id == SectionOf(D); }), D.Id + TEXT(": section present"));

        FPoolFilter Ult; Ult.Kind = static_cast<int32>(EKind::Ultimate);
        for (const auto& G : Pool(Ult)) for (const FCireAbilityDef* D : G.Value) T.Check(D->IsUltimate(), D->Id + TEXT(": kind filter"));
        FPoolFilter Hidden; Hidden.HiddenSections.Add(TEXT("passive"));
        for (const auto& G : Pool(Hidden)) T.Check(G.Key.Id != TEXT("passive"), TEXT("hidden section omitted"));
        const FCireAbilityDef& Probe = All[All.Num() / 2];
        T.Check(MatchesSearch(Probe, Probe.Name.ToUpper()) && MatchesSearch(Probe, TEXT("  ")) && !MatchesSearch(Probe, TEXT("zzqqxx_no_such")), TEXT("search: name, blank, miss"));
        T.Check(MatchesSearch(Probe, Probe.Name.Left(3).ToLower() + TEXT(" ") + SectionOf(Probe)), TEXT("search: every word must match"));
        if (CireChampionRoster::Count())
        {
            FPoolFilter Only; Only.OnlyChampion = CireChampionRoster::All()[0].Id;
            const FCireChampionKit* K = CireAbilityDB::Kit(Only.OnlyChampion);
            for (const auto& G : Pool(Only)) for (const FCireAbilityDef* D : G.Value) T.Check(K && K->Purchasable.Contains(D->Id), D->Id + TEXT(": only-champion filter"));
        }
    }

    // ---------------- kit rules ----------------
    TArray<FString> ActiveIds, UltIds, PassiveIds;
    for (const FCireAbilityDef& D : All) (D.IsUltimate() ? UltIds : D.IsPassive() ? PassiveIds : ActiveIds).Add(D.Id);
    T.Check(ActiveIds.Num() >= 7 && UltIds.Num() >= 2 && PassiveIds.Num() >= 2, TEXT("pool has every kind"));
    if (ActiveIds.Num() >= 7 && UltIds.Num() >= 2 && PassiveIds.Num() >= 2)
    {
        TArray<FString> Messy = {PassiveIds[0], UltIds[0], ActiveIds[0], TEXT("no_such_ability"), ActiveIds[0], UltIds[1], PassiveIds[1]};
        for (int32 I = 1; I < 7; ++I) Messy.Add(ActiveIds[I]);
        TArray<FString> Dropped;
        const TArray<FString> Clean = Normalize(Messy, &Dropped);
        T.Check(Clean.Num() == 8, FString::Printf(TEXT("normalize keeps 6 + 1 + 1 (got %d)"), Clean.Num()));
        T.Check(Dropped.Contains(TEXT("no_such_ability")) && Dropped.Contains(UltIds[1]) && Dropped.Contains(PassiveIds[1]) && Dropped.Contains(ActiveIds[6]), TEXT("normalize drops unknown, duplicate and over-capacity"));
        T.Check(Clean.Num() == 8 && Clean[0] == ActiveIds[0] && Clean[6] == UltIds[0] && Clean[7] == PassiveIds[0], TEXT("normalize order: actives, ultimate, passive"));
        T.Check(AddBlocker(Clean, UltIds[1]).Contains(TEXT("ultimate")) && AddBlocker(Clean, ActiveIds[6]).Contains(TEXT("active")), TEXT("add blocked when the kind is full"));
        T.Check(AddBlocker({}, TEXT("no_such_ability")).Len() > 0 && AddBlocker({ActiveIds[0]}, ActiveIds[0]).Len() > 0 && AddBlocker({ActiveIds[0]}, UltIds[0]).IsEmpty(), TEXT("add: unknown / duplicate / ok"));
    }

    // ---------------- JSON round trip ----------------
    {
        FCireKitTemplate A;
        A.ChampionId = TEXT("knight");
        A.BaseKit = {ActiveIds[0], UltIds[0]};
        A.bGrantOnDraft = false;
        FCireKitEffectPlacement Pl; Pl.Attach = TEXT("hand_r"); Pl.Offset = FVector(12, -4, 30); Pl.Scale = 1.35f; Pl.Tint = FLinearColor(.2f, .9f, 1.f, 1.f); Pl.TintStrength = .75f;
        A.Effects.Add(ActiveIds[0], Pl);
        A.Effects.Add(UltIds[0], FCireKitEffectPlacement()); // default: not written
        TMap<FString, FCireKitTemplate> In; In.Add(A.ChampionId, A);
        TMap<FString, FCireKitTemplate> Out; FString Error;
        T.Check(ParseJson(ToJson(In), Out, Error) && Out.Contains(TEXT("knight")), TEXT("round trip parses: ") + Error);
        if (const FCireKitTemplate* B = Out.Find(TEXT("knight")))
        {
            T.Check(B->BaseKit == A.BaseKit && !B->bGrantOnDraft, TEXT("round trip: kit + grant flag"));
            T.Check(B->Effects.Num() == 1 && B->Effects.Contains(ActiveIds[0]) && B->Effects[ActiveIds[0]] == Pl, TEXT("round trip: placement (defaults dropped)"));
        }
        TMap<FString, FCireKitTemplate> Bad;
        T.Check(!ParseJson(TEXT("[1,2]"), Bad, Error) && !ParseJson(TEXT("{\"champions\":{\"x\":3}}"), Bad, Error), TEXT("malformed input rejected"));
        T.Check(ParseJson(TEXT("{\"champions\":{\"x\":{\"baseKit\":[\"a\"],\"effects\":{\"a\":{\"scale\":99,\"offset\":[1,2,9999]}}}}}"), Bad, Error) &&
            Bad.Contains(TEXT("x")) && FMath::IsNearlyEqual(Bad[TEXT("x")].Effects[TEXT("a")].Scale, 5.f) && Bad[TEXT("x")].Effects[TEXT("a")].Offset.Z <= 400.f && Bad[TEXT("x")].bGrantOnDraft,
            TEXT("values clamped, grantOnDraft defaults on"));
        T.Check(ParseJson(TEXT("{}"), Bad, Error) && Bad.IsEmpty(), TEXT("empty file = no templates"));
    }

    // ---------------- game integration: purchasable merge + draft grant ----------------
    const FCireChampionProfile* Knight = CireChampionRoster::Find(TEXT("knight"));
    if (!Knight && CireChampionRoster::Count()) Knight = &CireChampionRoster::All()[0];
    T.Check(Knight != nullptr, TEXT("a roster champion to test with"));
    if (Knight)
    {
        FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const auto MakeHero = [&]()
        {
            ACireHero* H = World->SpawnActor<ACireHero>(FVector(0, -2400, 4800), FRotator::ZeroRotator, SP);
            if (H) { Spawned.Add(H); H->SetActorTickEnabled(false); H->SetActorEnableCollision(false); H->SetActorHiddenInGame(true); H->TeamId = 0; H->GetCharacterMovement()->DisableMovement(); }
            return H;
        };
        // Probe hero: which abilities this body can actually use (shield / ranged requirements).
        ACireHero* Probe = MakeHero();
        T.Check(Probe && Probe->DraftProfile(Knight->Id), TEXT("probe hero drafts"));
        TArray<FString> Kit;
        const FCireChampionKit* Own = CireAbilityDB::Kit(Knight->Id);
        FString Foreign; // an ability the champion cannot learn without a template
        for (const FCireAbilityDef& D : All)
        {
            if (!D.IsImplemented() || !Probe || !CireKits::MeetsRequirement(Probe, D.Id, nullptr)) continue;
            if (Foreign.IsEmpty() && !D.IsPassive() && !D.IsUltimate() && !(Own && Own->Purchasable.Contains(D.Id))) { Foreign = D.Id; Kit.Add(D.Id); continue; }
            if (AddBlocker(Kit, D.Id).IsEmpty()) Kit.Add(D.Id);
        }
        Kit = Normalize(Kit);
        T.Check(Kit.Num() == Cires::MaxSkills, FString::Printf(TEXT("a full usable kit exists (%d)"), Kit.Num()));
        FCireKitTemplate Tmpl; Tmpl.ChampionId = Knight->Id; Tmpl.BaseKit = Kit; Tmpl.bGrantOnDraft = true;
        FCireKitEffectPlacement Pl; Pl.Attach = TEXT("hand_r"); Pl.Offset = FVector(0, 0, 25); Pl.Scale = 1.5f;
        if (Kit.Num()) Tmpl.Effects.Add(Kit[0], Pl);
        Fake.Add(Tmpl.ChampionId, Tmpl);
        FCireKitTemplate NewRow; NewRow.ChampionId = TEXT("kit_editor_test_champion"); NewRow.BaseKit = {Kit.Num() ? Kit[0] : FString()};
        Fake.Add(NewRow.ChampionId, NewRow);
        DebugOverride(&Fake);
        CireAbilityDB::Reload();
        T.Check(!Foreign.IsEmpty() && CireAbilityDB::CanLearn(Knight->Id, Foreign), TEXT("template skill joins the purchasable list: ") + Foreign);
        T.Check(Foreign.IsEmpty() || CireAbilityDB::PurchasableSkills(Knight->Id, true).Contains(Foreign), TEXT("... and the implemented shop list"));
        T.Check(CireAbilityDB::Kit(TEXT("kit_editor_test_champion")) && Kit.Num() && CireAbilityDB::CanLearn(TEXT("kit_editor_test_champion"), Kit[0]), TEXT("a champion without a DB kit gets one from its template"));

        ACireHero* H = MakeHero();
        T.Check(H && H->DraftProfile(Knight->Id), TEXT("templated hero drafts"));
        if (H)
        {
            T.Check(H->Skills == Kit, FString::Printf(TEXT("draft grants the base kit in order (%d / %d)"), H->Skills.Num(), Kit.Num()));
            T.Check(H->Cooldowns.Num() == H->Skills.Num() && H->Offers.IsEmpty(), TEXT("cooldown per skill, no opening offer"));
            T.Check(static_cast<int32>(H->Progression.LearnedSkills.size()) == Kit.Num(), TEXT("progression knows the learned skills"));
            int32 Ranked = 0;
            if (H->Inventory) for (const FCireSkillRank& R : H->Inventory->SkillRanks) Ranked += Kit.Contains(R.Id) && R.Level == 1;
            T.Check(!H->Inventory || Ranked == Kit.Num(), TEXT("every granted skill is level 1 in the Skill Shop"));
            T.Check(Kit.Num() == 0 || CireSkillShop::Level(H, Kit[0]) == 1, TEXT("Skill Shop sees the granted level"));
            T.Check(Kit.Num() == 0 || !CireSkillShop::BuyBlocker(H, Kit.Last()).IsEmpty(), TEXT("a granted skill cannot be bought twice"));
        }
        // grantOnDraft off: the default opening flow stays.
        Fake[Knight->Id].bGrantOnDraft = false;
        ACireHero* H2 = MakeHero();
        T.Check(H2 && H2->DraftProfile(Knight->Id) && H2->Skills.IsEmpty(), TEXT("grant off: no skills at draft"));
        T.Check(!H2 || H2->Offers.Num() == 4 || !CireSkillShop::IsSkillShopMode(World), TEXT("grant off: the opening offer is kept"));
        Fake[Knight->Id].bGrantOnDraft = true;

        // ---------------- effect placement ----------------
        T.Check(Kit.Num() && Placement(Knight->Id, Kit[0]) && Placement(Knight->Id, Kit[0])->Attach == TEXT("hand_r"), TEXT("placement lookup"));
        T.Check(!Placement(Knight->Id, TEXT("no_such_ability")) && !Placement(TEXT("nobody"), Kit.Num() ? Kit[0] : FString()), TEXT("no placement = default presentation"));
        USkeletalMeshComponent* Mesh = H ? H->GetMesh() : nullptr;
        const TArray<FName> Names = Mesh ? Mesh->GetAllSocketNames() : TArray<FName>();
        T.Check(ResolveAttach(Mesh, FString()) == NAME_None && ResolveAttach(Mesh, TEXT("no_such_bone_xyz")) == NAME_None, TEXT("empty / unknown attach = origin"));
        if (Names.Num())
        {
            T.Check(ResolveAttach(Mesh, Names.Last().ToString()) == Names.Last(), TEXT("literal bone names resolve"));
            const FName Hand = ResolveAttach(Mesh, TEXT("hand_r"));
            T.Check(!Hand.IsNone() && Mesh->DoesSocketExist(Hand), TEXT("hand_r anchor resolves on the champion body: ") + Hand.ToString());
            T.Check(ResolveAttach(Mesh, TEXT("HEAD")) != NAME_None, TEXT("anchors are case-insensitive"));
        }
        float DataScale = 1.f;
        UFXSystemAsset* System = Kit.Num() ? CastSystem(Kit[0], &DataScale) : nullptr;
        if (System && H && Mesh)
        {
            UFXSystemComponent* C = SpawnPlaced(H, System, Pl, 2.f, false);
            T.Check(C && C->GetAttachParent() == Mesh && C->GetAttachSocketName() == ResolveAttach(Mesh, Pl.Attach), TEXT("placed effect attaches to the resolved socket"));
            T.Check(C && C->IsUsingAbsoluteScale() && FMath::IsNearlyEqual(C->GetComponentScale().X, 3.f, .01f), TEXT("placed effect scale = base x placement"));
            if (C)
            {
                const FVector Expected = H->GetActorRotation().RotateVector(Pl.Offset * H->GetActorScale3D().Z);
                const FVector Actual = C->GetComponentLocation() - Mesh->GetSocketLocation(C->GetAttachSocketName());
                T.Check(Actual.Equals(Expected, 1.f), FString::Printf(TEXT("offset in champion space (%s vs %s)"), *Actual.ToString(), *Expected.ToString()));
                C->DestroyComponent();
            }
            // The spell presentation hook: a hero that knows the skill and owns a placement gets the effect on its body.
            UFXSystemComponent* Placed = SpawnPlacedCast(World, FName(*Kit[0]), H->GetActorLocation() + FVector(40, 0, 0), System, 1.f);
            T.Check(Placed && Placed->GetAttachParent() == Mesh, TEXT("cast hook spawns on the caster's body"));
            if (Placed) Placed->DestroyComponent();
            T.Check(!SpawnPlacedCast(World, FName(*Kit[0]), H->GetActorLocation() + FVector(5000, 0, 0), System, 1.f), TEXT("cast hook ignores far casters"));
            T.Check(!SpawnPlacedCast(World, FName(TEXT("no_such_ability")), H->GetActorLocation(), System, 1.f), TEXT("cast hook ignores unplaced skills"));
        }
        else UE_LOG(LogCireKitEditorTests, Display, TEXT("CIRE_KIT_EDITOR_NOTE no Fab cast system for %s (placement spawn checks skipped)"), Kit.Num() ? *Kit[0] : TEXT("-"));
    }

    UE_LOG(LogCireKitEditorTests, Display, TEXT("CIRE_KIT_EDITOR_TESTS_%s checks=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count);
    return T.bPassed;
}
#endif
