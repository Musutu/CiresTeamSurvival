// kit-editor: native checks for the Hero Creator data (CireKitEditor). Run by the combat expansion probe.
#include "CireKitEditor.h"

#if !UE_BUILD_SHIPPING
#include "CireAbilityDB.h"
#include "CireChampionRoster.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireItems.h"
#include "CireShopUI.h"
#include "CireSkillShop.h"
#include "CireWaves.h"
#include "Rules/CiresRules.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
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
FCireKitLoadout KitTestLoadout(const FString& Name, const TArray<FString>& Actives, const FString& Ult, const FString& Passive)
{
    FCireKitLoadout L; L.Name = Name;
    for (int32 I = 0; I < Actives.Num() && I < FCireKitLoadout::ActiveSlots; ++I) L.Slots[I] = Actives[I];
    L.Slots[FCireKitLoadout::UltimateSlot] = Ult;
    L.Slots[FCireKitLoadout::PassiveSlot] = Passive;
    return L;
}
}

bool CireKitEditor::RunTests(ACireGameMode* Mode)
{
    FKitChecks T;
    if (!IsValid(Mode) || !Mode->GetWorld()) return false;
    UWorld* World = Mode->GetWorld();
    TArray<AActor*> Spawned;
    FCireKitData Fake;
    ON_SCOPE_EXIT
    {
        for (AActor* A : Spawned) if (IsValid(A)) A->Destroy();
        DebugOverride(nullptr);
        DebugForceProfile(FString());
        CireAbilityDB::Reload();
    };

    // ---------------- every ability, in the Skill Shop's sections ----------------
    const TArray<FCireAbilityDef>& All = CireAbilityDB::All();
    T.Check(All.Num() >= 100, TEXT("ability database loaded"));
    {
        int32 Listed = 0; TSet<FString> Seen; bool bSorted = true, bSections = true;
        for (const auto& G : Pool(FPoolFilter()))
            for (int32 I = 0; I < G.Value.Num(); ++I)
            {
                ++Listed; Seen.Add(G.Value[I]->Id);
                bSections &= CireShopUI::SkillSectionOf(G.Value[I]->Id) == G.Key;
                if (I > 0 && G.Value[I - 1]->Name > G.Value[I]->Name) bSorted = false;
            }
        T.Check(Listed == All.Num() && Seen.Num() == All.Num(), FString::Printf(TEXT("every ability listed exactly once, for any champion (%d / %d)"), Listed, All.Num()));
        T.Check(bSections && bSorted, TEXT("grouped by the Skill Shop section, sorted by name"));
        FPoolFilter Ult; Ult.Kind = static_cast<int32>(EKind::Ultimate);
        for (const auto& G : Pool(Ult)) for (const FCireAbilityDef* D : G.Value) T.Check(D->IsUltimate(), D->Id + TEXT(": kind filter"));
        FPoolFilter Hidden; Hidden.HiddenSections = 1u << 0;
        for (const auto& G : Pool(Hidden)) T.Check(G.Key != 0, TEXT("hidden section omitted"));
        FPoolFilter Role; Role.Role = TEXT("TANK");
        for (const auto& G : Pool(Role)) for (const FCireAbilityDef* D : G.Value) T.Check(D->Types.Contains(TEXT("TANK")), D->Id + TEXT(": role chip"));
        const FCireAbilityDef& Probe = All[All.Num() / 2];
        T.Check(MatchesSearch(Probe, Probe.Name.ToUpper()) && MatchesSearch(Probe, TEXT("  ")) && !MatchesSearch(Probe, TEXT("zzqqxx_no_such")), TEXT("search: name, blank, miss"));
    }

    // ---------------- skill buttons ----------------
    TArray<FString> Actives, Ults, Passives;
    for (const FCireAbilityDef& D : All) if (D.IsImplemented()) (D.IsUltimate() ? Ults : D.IsPassive() ? Passives : Actives).Add(D.Id);
    T.Check(Actives.Num() >= 8 && Ults.Num() >= 2 && Passives.Num() >= 2, TEXT("pool has every kind"));
    if (Actives.Num() < 8 || Ults.Num() < 2 || Passives.Num() < 2) return false;
    {
        FCireKitLoadout L;
        T.Check(TargetSlot(L, Actives[0]) == 0 && TargetSlot(L, Ults[0]) == FCireKitLoadout::UltimateSlot && TargetSlot(L, Passives[0]) == FCireKitLoadout::PassiveSlot, TEXT("click targets: key 1, R, passive"));
        FString Why;
        T.Check(Assign(L, 3, Actives[0], &Why) && L.Slots[0] == Actives[0] && L.Slots[3].IsEmpty(), TEXT("key buttons pack from 1"));
        T.Check(Assign(L, 1, Actives[1]) && Assign(L, 0, Actives[1]) && L.Slots[0] == Actives[1] && L.Slots.FilterByPredicate([&](const FString& S) { return S == Actives[1]; }).Num() == 1, TEXT("moving a skill keeps one copy"));
        T.Check(!Assign(L, 2, Ults[0], &Why) && !Why.IsEmpty(), TEXT("an ultimate is refused on a key button"));
        T.Check(Assign(L, FCireKitLoadout::UltimateSlot, Actives[2], &Why) && L.Slots[FCireKitLoadout::UltimateSlot] == Actives[2] && !Why.IsEmpty(), TEXT("a non-ultimate on R is allowed with a warning"));
        T.Check(Assign(L, FCireKitLoadout::UltimateSlot, Ults[0], &Why) && Why.IsEmpty(), TEXT("an ultimate on R, no warning"));
        for (int32 I = 0; I < 6; ++I) { const int32 Target = TargetSlot(L, Actives[I + 2]); if (Target != INDEX_NONE) Assign(L, Target, Actives[I + 2]); }
        T.Check(L.Slots[5].Len() > 0 && TargetSlot(L, TEXT("kit_editor_not_in_loadout")) != 0, TEXT("six key buttons fill up"));
        T.Check(TargetSlot(L, Actives[7], 4) == 4, TEXT("a selected button is the target"));
        FCireKitLoadout Messy; Messy.Slots = {TEXT(""), Actives[0], TEXT("no_such_ability"), Actives[0], TEXT(""), TEXT(""), Ults[0], Passives[0]};
        Compact(Messy);
        T.Check(Messy.Slots[0] == Actives[0] && Messy.Slots[1].IsEmpty() && Messy.Count() == 3, TEXT("compact: unknown / duplicate removed, packed"));
        T.Check(Messy.Skills() == TArray<FString>({Actives[0], Ults[0], Passives[0]}), TEXT("grant order: keys, R, passive"));
    }

    // ---------------- save / load round trip, legacy file, profiles ----------------
    {
        FCireKitData D;
        FString Error;
        T.Check(ParseJson(TEXT("{}"), D, Error) && D.Profiles.Num() == 1 && D.Profiles[0].Name == StandardProfile, TEXT("an empty file has the Standard profile"));
        FCireKitProfile Std; Std.Name = StandardProfile;
        FCireKitChampion C; C.bGrantOnDraft = false;
        C.Loadouts.Add(KitTestLoadout(TEXT("Frontline"), {Actives[0], Actives[1]}, Ults[0], Passives[0]));
        C.Loadouts.Add(KitTestLoadout(TEXT("Burst"), {Actives[2]}, Ults[1], FString()));
        C.DefaultLoadout = TEXT("Burst");
        Std.Champions.Add(TEXT("knight"), C);
        FCireKitProfile Td; Td.Name = TEXT("Hero TD");
        Td.Champions.Add(TEXT("ranger"), C);
        D.Profiles = {Td, Std}; // Standard is moved first on parse
        FCireKitEffectPlacement Pl; Pl.Attach = TEXT("hand_r"); Pl.Offset = FVector(12, -4, 30); Pl.Scale = 1.35f; Pl.Tint = FLinearColor(.2f, .9f, 1.f, 1.f); Pl.TintStrength = .75f;
        D.Effects.FindOrAdd(TEXT("knight")).Add(Actives[0], Pl);
        FCireKitData Back;
        T.Check(ParseJson(ToJson(D), Back, Error), TEXT("round trip parses: ") + Error);
        T.Check(Back.Profiles.Num() == 2 && Back.Profiles[0].Name == StandardProfile && Back.FindProfile(TEXT("hero td")), TEXT("round trip: profiles (Standard first, names case-insensitive)"));
        const FCireKitChampion* K = Back.FindProfile(StandardProfile) ? Back.FindProfile(StandardProfile)->Champions.Find(TEXT("knight")) : nullptr;
        T.Check(K && K->Loadouts.Num() == 2 && K->Default() && K->Default()->Name == TEXT("Burst") && !K->bGrantOnDraft, TEXT("round trip: presets, default, grant flag"));
        T.Check(K && K->Find(TEXT("Frontline")) && K->Find(TEXT("Frontline"))->Slots == C.Loadouts[0].Slots, TEXT("round trip: every button"));
        T.Check(Back.Effects.Contains(TEXT("knight")) && Back.Effects[TEXT("knight")].Contains(Actives[0]) && Back.Effects[TEXT("knight")][Actives[0]] == Pl, TEXT("round trip: effect placement"));
        FCireKitData Legacy;
        const FString V1 = FString::Printf(TEXT("{\"champions\":{\"knight\":{\"baseKit\":[\"%s\",\"%s\",\"%s\"],\"grantOnDraft\":true,\"effects\":{\"%s\":{\"attach\":\"head\",\"scale\":99}}}}}"), *Ults[0], *Actives[0], *Passives[0], *Actives[0]);
        T.Check(ParseJson(V1, Legacy, Error), TEXT("schemaVersion 1 parses"));
        const FCireKitChampion* LK = Legacy.FindProfile(StandardProfile) ? Legacy.FindProfile(StandardProfile)->Champions.Find(TEXT("knight")) : nullptr;
        T.Check(LK && LK->Default() && LK->Default()->Slots[0] == Actives[0] && LK->Default()->Slots[FCireKitLoadout::UltimateSlot] == Ults[0] && LK->Default()->Slots[FCireKitLoadout::PassiveSlot] == Passives[0], TEXT("v1 baseKit becomes a Default preset on its buttons"));
        T.Check(Legacy.Effects.Contains(TEXT("knight")) && FMath::IsNearlyEqual(Legacy.Effects[TEXT("knight")][Actives[0]].Scale, 5.f), TEXT("v1 effects kept, values clamped"));
        FCireKitData Bad;
        T.Check(!ParseJson(TEXT("[1,2]"), Bad, Error) && !ParseJson(TEXT("{\"profiles\":{\"x\":3}}"), Bad, Error), TEXT("malformed input rejected"));
    }

    // ---------------- profile fallback and game mode -> profile ----------------
    const FCireChampionProfile* Knight = CireChampionRoster::Find(TEXT("knight"));
    if (!Knight && CireChampionRoster::Count()) Knight = &CireChampionRoster::All()[0];
    const FCireChampionProfile* Other = nullptr;
    for (const FCireChampionProfile& P : CireChampionRoster::All()) if (Knight && P.Id != Knight->Id) { Other = &P; break; }
    T.Check(Knight && Other, TEXT("two roster champions to test with"));
    if (!Knight || !Other) return false;
    {
        FCireKitProfile Std; Std.Name = StandardProfile;
        FCireKitProfile Td; Td.Name = TEXT("Hero TD");
        FCireKitChampion KStd; KStd.Loadouts.Add(KitTestLoadout(TEXT("Std"), {Actives[0], Actives[1], Actives[2]}, Ults[0], Passives[0])); KStd.DefaultLoadout = TEXT("Std");
        FCireKitChampion KTd; KTd.Loadouts.Add(KitTestLoadout(TEXT("TD"), {Actives[3], Actives[4]}, Ults[1], Passives[1])); KTd.DefaultLoadout = TEXT("TD");
        Std.Champions.Add(Knight->Id, KStd);
        Std.Champions.Add(Other->Id, KStd);
        Td.Champions.Add(Knight->Id, KTd);
        Fake.Profiles = {Std, Td};
        FCireKitEffectPlacement Pl; Pl.Attach = TEXT("hand_r"); Pl.Offset = FVector(0, 0, 25); Pl.Scale = 1.5f;
        Fake.Effects.FindOrAdd(Knight->Id).Add(Actives[3], Pl);
        DebugOverride(&Fake);
        FString From;
        const FCireKitLoadout* L1 = ResolveLoadout(TEXT("Hero TD"), Knight->Id, &From);
        T.Check(L1 && L1->Name == TEXT("TD") && From == TEXT("Hero TD"), TEXT("profile loadout used"));
        const FCireKitLoadout* L2 = ResolveLoadout(TEXT("Hero TD"), Other->Id, &From);
        T.Check(L2 && L2->Name == TEXT("Std") && From == StandardProfile, TEXT("missing in the profile: falls back to Standard"));
        T.Check(!ResolveLoadout(TEXT("Hero TD"), TEXT("kit_editor_nobody")), TEXT("missing everywhere: the built-in kit"));
        T.Check(ProfileForMode(TEXT("hero td")) == TEXT("Hero TD") && ProfileForMode(FString()) == StandardProfile && ProfileForMode(TEXT("No Such Profile")) == StandardProfile, TEXT("mode -> profile (unknown = Standard)"));
        const FCireWavePreset* HeroTd = CireWaveDirector::FindPreset(TEXT("hero_td"));
        T.Check(HeroTd && HeroTd->KitProfile == TEXT("Hero TD"), TEXT("WavePresets.json hero_td names the Hero TD kit profile"));
        if (ACireGameState* GS = Mode->GetGameState<ACireGameState>())
        {
            const FName Saved = GS->WavePreset;
            GS->WavePreset = TEXT("hero_td");
            T.Check(ActiveProfile(World) == TEXT("Hero TD"), TEXT("the host's game type selects its kit profile"));
            GS->WavePreset = TEXT("standard");
            T.Check(ActiveProfile(World) == StandardProfile, TEXT("standard game type -> Standard kits"));
            GS->WavePreset = Saved;
        }
        CireAbilityDB::Reload();
        const FCireChampionKit* NewKit = CireAbilityDB::Kit(Knight->Id);
        T.Check(NewKit && NewKit->Purchasable.Contains(Actives[3]) && NewKit->Purchasable.Contains(Actives[0]), TEXT("every loadout skill joins the purchasable list"));
    }

    // ---------------- draft grant (server) ----------------
    FActorSpawnParameters SP; SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    const auto MakeHero = [&]()
    {
        ACireHero* H = World->SpawnActor<ACireHero>(FVector(0, -2400, 4800), FRotator::ZeroRotator, SP);
        if (H) { Spawned.Add(H); H->SetActorTickEnabled(false); H->SetActorEnableCollision(false); H->SetActorHiddenInGame(true); H->TeamId = 0; H->GetCharacterMovement()->DisableMovement(); }
        return H;
    };
    DebugForceProfile(TEXT("Hero TD"));
    ACireHero* H = MakeHero();
    T.Check(H && H->DraftProfile(Knight->Id), TEXT("templated hero drafts"));
    const TArray<FString> Expected = Fake.Profiles[1].Champions[Knight->Id].Default()->Skills();
    if (H)
    {
        T.Check(H->Skills == Expected, FString::Printf(TEXT("draft grants the Hero TD loadout in button order (%d / %d)"), H->Skills.Num(), Expected.Num()));
        T.Check(H->Cooldowns.Num() == H->Skills.Num() && H->Offers.IsEmpty(), TEXT("cooldown per skill, no opening offer"));
        T.Check(static_cast<int32>(H->Progression.LearnedSkills.size()) == H->Skills.Num(), TEXT("progression knows the learned skills"));
        T.Check(!H->Inventory || H->Inventory->SkillRanks.FilterByPredicate([&](const FCireSkillRank& R) { return Expected.Contains(R.Id) && R.Level == 1; }).Num() == H->Skills.Num(), TEXT("every granted skill is level 1 in the Skill Shop"));
        T.Check(H->Skills.IsEmpty() || CireSkillShop::Level(H, H->Skills[0]) == 1, TEXT("Skill Shop sees the granted level"));
    }
    ACireHero* H2 = MakeHero();
    T.Check(H2 && H2->DraftProfile(Other->Id) && H2->Skills == Fake.Profiles[0].Champions[Other->Id].Default()->Skills(), TEXT("a champion missing from Hero TD drafts with its Standard loadout"));
    Fake.Profiles[1].Champions[Knight->Id].bGrantOnDraft = false;
    ACireHero* H3 = MakeHero();
    T.Check(H3 && H3->DraftProfile(Knight->Id) && H3->Skills.IsEmpty(), TEXT("grant off: no skills at draft"));
    T.Check(!H3 || H3->Offers.Num() == 4 || !CireSkillShop::IsSkillShopMode(World), TEXT("grant off: the opening offer is kept"));
    Fake.Profiles[1].Champions[Knight->Id].bGrantOnDraft = true;
    DebugForceProfile(FString());

    // ---------------- effect placement ----------------
    T.Check(Placement(Knight->Id, Actives[3]) && Placement(Knight->Id, Actives[3])->Attach == TEXT("hand_r"), TEXT("placement lookup"));
    T.Check(!Placement(Knight->Id, TEXT("no_such_ability")) && !Placement(TEXT("nobody"), Actives[3]), TEXT("no placement = default presentation"));
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
    UFXSystemAsset* System = CastSystem(Actives[3], &DataScale);
    // -nullrhi (the native probe): Niagara / Cascade never spawn, so these need a rendering run (the gallery covers them).
    if (System && H && Mesh && FApp::CanEverRender())
    {
        const FCireKitEffectPlacement Pl = *Placement(Knight->Id, Actives[3]);
        UFXSystemComponent* C = SpawnPlaced(H, System, Pl, 2.f, false);
        T.Check(C && C->GetAttachParent() == Mesh && C->GetAttachSocketName() == ResolveAttach(Mesh, Pl.Attach), TEXT("placed effect attaches to the resolved socket"));
        T.Check(C && C->IsUsingAbsoluteScale() && FMath::IsNearlyEqual(C->GetComponentScale().X, 3.f, .01f), TEXT("placed effect scale = base x placement"));
        if (C) C->DestroyComponent();
        UFXSystemComponent* Placed = SpawnPlacedCast(World, FName(*Actives[3]), H->GetActorLocation() + FVector(40, 0, 0), System, 1.f);
        T.Check(Placed && Placed->GetAttachParent() == Mesh, TEXT("cast hook spawns on the caster's body"));
        if (Placed) Placed->DestroyComponent();
        T.Check(!SpawnPlacedCast(World, FName(*Actives[3]), H->GetActorLocation() + FVector(5000, 0, 0), System, 1.f), TEXT("cast hook ignores far casters"));
    }
    else UE_LOG(LogCireKitEditorTests, Display, TEXT("CIRE_KIT_EDITOR_NOTE placement spawn checks skipped (no renderer or no Fab cast system for %s)"), *Actives[3]);

    UE_LOG(LogCireKitEditorTests, Display, TEXT("CIRE_KIT_EDITOR_TESTS_%s checks=%d"), T.bPassed ? TEXT("PASS") : TEXT("FAIL"), T.Count);
    return T.bPassed;
}
#endif
