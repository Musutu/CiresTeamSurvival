// world-editor: native checks for the world edit sets (CireWorldEdit.h). Part of -CireCombatExpansionProbe; logs
// CIRE_WORLD_EDIT_TESTS_PASS / _FAIL. Pure data (save / load round trip, stable ids, undo / redo, mirror twins, protection,
// validation, settings) plus a small world fixture (reversible removal of an actor with an attached child, the nav cache key).
#include "CireWorldEdit.h"
#include "CireGame.h"
#include "CireMapLayout.h"
#include "CireNavCache.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireWorldEditTests, Log, All);

namespace
{
struct FWEChecker
{
    int32 Count = 0; bool bPass = true;
    void operator()(bool bValue, const FString& Why) { ++Count; if (!bValue) { bPass = false; UE_LOG(LogCireWorldEditTests, Error, TEXT("CIRE_WORLD_EDIT_CHECK_FAIL %s"), *Why); } }
};
FCireWorldEditEntry WETestEntry(const TCHAR* Id, int32 Realm, FVector2D At, FVector Size, const TCHAR* Label = TEXT("LI_House_01"))
{
    FCireWorldEditEntry E; E.Id = Id; E.Realm = Realm; E.Label = Label; E.Kind = CireWorldEdit::KindOf(Label, true);
    E.Local = FVector(At.X, At.Y, 120); E.Size = Size; E.Actors = 40; E.DrawCalls = 95;
    return E;
}
}

bool CireWorldEdit::RunTests(ACireGameMode* Mode)
{
    FWEChecker Check;
    ON_SCOPE_EXIT { SetOverride(nullptr); };
    namespace ML = CireMapLayout;

    // ---- stable ids -----------------------------------------------------------------------------------------------------
    {
        FString Sub; int32 Realm = -1;
        Check(ParseRealmLevel(TEXT("SL_Houses_CireRealm0"), Sub, Realm) && Sub == TEXT("SL_Houses") && Realm == 0, TEXT("SL_Houses_CireRealm0 -> SL_Houses realm 0"));
        Check(ParseRealmLevel(TEXT("UEDPIE_0_SL_Town_Props_CireRealm1"), Sub, Realm) && Sub == TEXT("SL_Town_Props") && Realm == 1, TEXT("a PIE prefix is stripped"));
        Check(ParseRealmLevel(TEXT("PL_CastleTown_CireRealm1"), Sub, Realm) && Sub == TEXT("PL_CastleTown"), TEXT("the persistent pack level streamed as a realm copy"));
        Check(!ParseRealmLevel(TEXT("SL_Houses"), Sub, Realm) && !ParseRealmLevel(TEXT("LI_House_01_LevelInstance_3"), Sub, Realm) && !ParseRealmLevel(TEXT("SL_X_CireRealm12"), Sub, Realm),
            TEXT("other levels (the pack's own names, Level Instance levels, bad suffixes) are not realm levels"));
        Check(ActorId(nullptr).IsEmpty(), TEXT("no actor: no id"));
        if (Mode && Mode->GetWorld())
        {
            FActorSpawnParameters P; P.ObjectFlags |= RF_Transient;
            AActor* Loose = Mode->GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform::Identity, P);
            Check(Loose && ActorId(Loose).IsEmpty(), TEXT("an actor outside the realm sublevels (game actors) has no id: it can never be removed"));
            if (Loose) Loose->Destroy();
        }
        const FCireWorldEditEntry E = WETestEntry(TEXT("SL_Houses/LevelInstance_17"), 1, {0, 0}, {800, 600, 900});
        Check(E.Key() == TEXT("1|SL_Houses/LevelInstance_17"), TEXT("key = realm|id"));
        Check(KindOf(TEXT("LI_Castle_Tower_02"), true) == TEXT("Fortification") && KindOf(TEXT("SM_Tree_Oak"), false) == TEXT("Nature") && KindOf(TEXT("SM_Barrel"), false) == TEXT("Prop")
            && KindOf(TEXT("LI_House_03"), true) == TEXT("Building"), TEXT("kinds from names"));
    }

    // ---- save / load round trip -----------------------------------------------------------------------------------------
    FCireWorldEditSet Set; Set.Name = TEXT("Test Set");
    Set.Removed.Add(WETestEntry(TEXT("SL_Houses/LevelInstance_17"), 0, {1200, -300}, {900, 700, 1100}));
    Set.Removed.Add(WETestEntry(TEXT("SL_Town_Props/StaticMeshActor_88"), 1, {-40, 55}, {120, 90, 150}, TEXT("SM_Barrel")));
    {
        FCireWorldEditSet Back; FString Error;
        Check(FromJson(ToJson(Set), Back, &Error), TEXT("the JSON reads back"));
        bool bSame = Back.Name == Set.Name && Back.Removed.Num() == 2;
        for (int32 I = 0; bSame && I < 2; ++I)
        {
            const FCireWorldEditEntry& A = Set.Removed[I]; const FCireWorldEditEntry& B = Back.Removed[I];
            bSame = A.Key() == B.Key() && A.Label == B.Label && A.Kind == B.Kind && A.Local.Equals(B.Local, 1.0) && A.Size.Equals(B.Size, 1.0) && A.Actors == B.Actors && A.DrawCalls == B.DrawCalls;
        }
        Check(bSame, TEXT("round trip keeps ids, realms, labels, kinds, positions, sizes and savings"));
        const FString Folder = FPaths::ProjectSavedDir() / TEXT("WorldEditTests");
        const FString File = Folder / TEXT("roundtrip.json");
        FCireWorldEditSet Disk;
        Check(Save(Set, File, &Error) && Load(Disk, File, &Error) && Hash(Disk) == Hash(Set), TEXT("save then load from disk keeps the set (same hash)"));
        IFileManager::Get().DeleteDirectory(*Folder, false, true);
        FCireWorldEditSet Dirty;
        Check(FromJson(TEXT("{\"removed\":[{\"id\":\"SL_A/X\",\"realm\":0},{\"id\":\"SL_A/X\",\"realm\":0},{\"id\":\"nolevel\",\"realm\":1},{\"realm\":1},{\"id\":\"SL_B/Y\",\"realm\":7}]}"), Dirty)
            && Dirty.Removed.Num() == 2 && Dirty.Removed[1].Realm == 1, TEXT("duplicates collapse, ids without a level are dropped, realms clamp"));
        Check(!FromJson(TEXT("not json"), Dirty), TEXT("malformed JSON is refused"));
        FCireWorldEditSet Swapped = Set; Swap(Swapped.Removed[0], Swapped.Removed[1]);
        Check(Hash(Swapped) == Hash(Set) && !Hash(Set).IsEmpty() && Hash(FCireWorldEditSet()).IsEmpty(), TEXT("the hash ignores order; an empty set hashes to nothing"));
        FCireWorldEditSet Other = Set; Other.Removed[1].Realm = 0;
        Check(Hash(Other) != Hash(Set), TEXT("the hash follows the realm"));
        const FCireWorldEditSavings Sv = Savings(Set);
        Check(Sv.Units == 2 && Sv.Actors == 80 && Sv.DrawCalls == 190, TEXT("savings add up"));
        Check(SanitizeName(TEXT("off")).IsEmpty() && SanitizeName(TEXT(" Fewer houses! ")) == TEXT("Fewer_houses"), TEXT("names: off is reserved, spaces become _"));
    }

    // ---- undo / redo ----------------------------------------------------------------------------------------------------
    {
        FCireWorldEditDoc Doc;
        const FCireWorldEditEntry A = WETestEntry(TEXT("SL_Houses/LevelInstance_1"), 0, {0, 0}, {500, 500, 500});
        const FCireWorldEditEntry B = WETestEntry(TEXT("SL_Houses/LevelInstance_2"), 0, {900, 0}, {500, 500, 500});
        const FCireWorldEditEntry C = WETestEntry(TEXT("SL_Town_Props/StaticMeshActor_3"), 1, {0, 900}, {100, 100, 100});
        Check(Remove(Doc, {A, B, A}, TEXT("box")) == 2 && Doc.Undo.Num() == 1, TEXT("a batch remove is one undo step (duplicates ignored)"));
        Check(Remove(Doc, {A}, TEXT("again")) == 0 && Doc.Undo.Num() == 1, TEXT("removing a removed unit adds no step"));
        Check(Remove(Doc, {C}, TEXT("c")) == 1 && Doc.Set.Removed.Num() == 3, TEXT("second step"));
        FString What;
        Check(Undo(Doc, &What) && What == TEXT("c") && Doc.Set.Removed.Num() == 2 && !Doc.Set.Contains(C.Key()), TEXT("undo the last step"));
        Check(Undo(Doc) && Doc.Set.Removed.IsEmpty(), TEXT("undo the batch: both come back at once"));
        Check(!Undo(Doc), TEXT("nothing left to undo"));
        Check(Redo(Doc) && Doc.Set.Removed.Num() == 2 && Redo(Doc) && Doc.Set.Removed.Num() == 3 && !Redo(Doc), TEXT("redo both steps"));
        Check(Restore(Doc, {A.Key(), TEXT("0|nope/x")}, TEXT("restore a")) == 1 && !Doc.Set.Contains(A.Key()) && Doc.Redo.IsEmpty(), TEXT("restore is a step and clears redo"));
        Check(Undo(Doc) && Doc.Set.Contains(A.Key()), TEXT("undo a restore removes it again"));
        Check(Undo(Doc) && !Doc.Set.Contains(C.Key()) && Doc.Redo.Num() == 2, TEXT("multi-step undo"));
        Check(Remove(Doc, {C}, TEXT("new")) == 1 && Doc.Redo.IsEmpty(), TEXT("a new edit clears redo"));
        const int32 Rev = Doc.Revision;
        FCireWorldEditSet Loaded; Loaded.Name = TEXT("Other"); Loaded.Removed = {C};
        ReplaceSet(Doc, Loaded, TEXT("load"));
        Check(Doc.Set.Name == TEXT("Other") && Doc.Set.Removed.Num() == 1 && Doc.Revision > Rev, TEXT("LOAD replaces the set"));
        Check(Undo(Doc) && Doc.Set.Removed.Num() == 3 && Doc.Set.Contains(A.Key()) && Doc.Set.Contains(C.Key()), TEXT("LOAD is undoable"));
    }

    // ---- mirror twins ---------------------------------------------------------------------------------------------------
    {
        const FCireWorldEditEntry A = WETestEntry(TEXT("SL_Houses/LevelInstance_1"), 0, {0, 0}, {500, 500, 500});
        const FCireWorldEditEntry B = WETestEntry(TEXT("SL_Houses/LevelInstance_2"), 1, {0, 0}, {500, 500, 500});
        int32 Twins = 0;
        const TArray<FCireWorldEditEntry> Out = WithTwins({A, B}, [](const FString& Key) { return Key == TEXT("1|SL_Houses/LevelInstance_1"); }, &Twins);
        Check(Twins == 1 && Out.Num() == 3 && Out[2].Realm == 1 && Out[2].Id == A.Id, TEXT("the twin is the same id in the other realm, added only where it exists"));
        const TArray<FCireWorldEditEntry> Both = WithTwins({A, TwinOf(A)}, [](const FString&) { return true; }, &Twins);
        Check(Both.Num() == 2 && Twins == 0, TEXT("a twin already selected is not added twice"));
    }

    // ---- protection and validation ------------------------------------------------------------------------------------------
    {
        FCireMapLayout L;
        const FString Vendor = ML::Place(L, ML::Vendor, {0, 0}, ECireMarkerOwner::Shared, 0.f, false);
        const FString Path = ML::ChainPoint(L, ML::MonsterPath, FString(), {-3000, 3000}, ECireMarkerOwner::Team1);
        ML::ChainPoint(L, ML::MonsterPath, Path, {3000, 3000}, ECireMarkerOwner::Team1);
        const FString Spawn = ML::Place(L, ML::PlayerSpawn, {0, -6000}, ECireMarkerOwner::Team1, 0.f, false);
        FCireWorldEditSettings S; S.ProtectPad = 100.f;
        const FCireWorldEditEntry OnVendor = WETestEntry(TEXT("SL_Houses/LevelInstance_1"), 0, {100, 50}, {800, 800, 600});
        const FCireWorldEditEntry OnPath = WETestEntry(TEXT("SL_Houses/LevelInstance_2"), 0, {0, 3000}, {800, 800, 600});
        const FCireWorldEditEntry GateOnPath = WETestEntry(TEXT("SL_Castle/LevelInstance_3"), 0, {1000, 3000}, {800, 800, 600}, TEXT("LI_Castle_Gate_01"));
        const FCireWorldEditEntry Far = WETestEntry(TEXT("SL_Houses/LevelInstance_4"), 0, {9000, -9000}, {800, 800, 600});
        const FCireWorldEditEntry SpawnOtherRealm = WETestEntry(TEXT("SL_Houses/LevelInstance_5"), 1, {0, -6000}, {800, 800, 600});
        const FCireWorldEditEntry SpawnSameRealm = WETestEntry(TEXT("SL_Houses/LevelInstance_5"), 0, {0, -6000}, {800, 800, 600});
        Check(Guard(L, OnVendor, S).Level == ECireWorldEditGuard::Protected, TEXT("a vendor stall / sign in the footprint protects the unit"));
        Check(Guard(L, OnPath, S).Level == ECireWorldEditGuard::Warn, TEXT("a monster path through a house: a clear warning, still removable"));
        Check(Guard(L, GateOnPath, S).Level == ECireWorldEditGuard::Protected, TEXT("a castle gate on a monster path is protected"));
        Check(Guard(L, Far, S).Level == ECireWorldEditGuard::Ok, TEXT("a house far from every marker is free to go"));
        Check(Guard(L, SpawnSameRealm, S).Level == ECireWorldEditGuard::Protected && Guard(L, SpawnOtherRealm, S).Level == ECireWorldEditGuard::Ok,
            TEXT("a team marker protects only in its own realm (unmirrored T1 spawn: realm 0)"));
        FCireWorldEditEntry Castle = Far; Castle.Label = TEXT("LI_Castle_Keep");
        Check(Guard(L, Castle, S).Level == ECireWorldEditGuard::Warn, TEXT("castle pieces off the routes warn"));
        FCireWorldEditSet Bad; Bad.Removed = {OnVendor, OnPath, Far};
        const TArray<FCireLayoutIssue> Issues = CireWorldEdit::Issues(L, Bad, S);
        int32 Errors = 0, Notes = 0; for (const FCireLayoutIssue& I : Issues) { Errors += I.bError ? 1 : 0; Notes += I.bError ? 0 : 1; }
        Check(Errors == 1 && Notes >= 1, FString::Printf(TEXT("validate: a removed protected unit is an error, a path through a removed house a note (errors %d notes %d)"), Errors, Notes));
        (void)Spawn; (void)Vendor;
    }

    // ---- settings -----------------------------------------------------------------------------------------------------------
    {
        const FCireWorldEditSettings S = ParseSettings(TEXT("{\"active\":\"Fewer Houses\",\"mirror\":false,\"ghosts\":true,\"protectWords\":[\"Keep\"],\"protectPad\":50}"));
        Check(S.Active == TEXT("Fewer_Houses") && !S.bMirror && S.bGhosts && S.ProtectWords.Num() == 1 && FMath::IsNearlyEqual(S.ProtectPad, 50.f), TEXT("WorldEdit.json keys"));
        Check(ParseSettings(TEXT("{\"active\":\"off\"}")).Active.IsEmpty() && ParseSettings(TEXT("{}")).bMirror, TEXT("defaults: no set, mirror on"));
    }

    // ---- runtime: the active set and the nav cache key -----------------------------------------------------------------------
    {
        const FString KeyOff = CireNavCache::Key();
        Check(Signature().IsEmpty() || Active(), TEXT("no set (procedural town): no signature"));
        SetOverride(&Set);
        Check(Active() && Signature().Contains(Hash(Set)), TEXT("a set with entries is active and signs the navmesh"));
        Check(CireNavCache::Key() != KeyOff, TEXT("the nav cache key changes with the set: a new set rebuilds the navmesh"));
        FCireWorldEditSet Other = Set; Other.Removed.RemoveAt(1);
        SetOverride(&Other);
        const FString KeyOther = CireNavCache::Key();
        SetOverride(&Set);
        Check(KeyOther != CireNavCache::Key(), TEXT("a different set, a different key"));
        SetOverride(nullptr);
        Check(CireNavCache::Key() == KeyOff, TEXT("switched off: the old key"));
    }

    // ---- world fixture: reversible removal (the editor and the live switch) -----------------------------------------------------
    if (Mode && Mode->GetWorld())
    {
        UWorld* World = Mode->GetWorld();
        FActorSpawnParameters P; P.ObjectFlags |= RF_Transient;
        auto* Parent = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FVector(0, 0, -50000)), P);
        auto* Child = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FVector(100, 0, -50000)), P);
        auto* Hidden = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(FVector(200, 0, -50000)), P);
        if (Parent && Child && Hidden)
        {
            Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
            Hidden->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
            Hidden->SetActorHiddenInGame(true); Hidden->SetActorEnableCollision(false);
            int32 Visited = 0; ForEachUnitActor(World, Parent, [&](AActor*) { ++Visited; });
            Check(Visited == 3, TEXT("a unit is the actor and everything attached to it"));
            SetUnitRemoved(World, Parent, true);
            Check(Parent->IsHidden() && !Parent->GetActorEnableCollision() && Child->IsHidden() && !Child->GetActorEnableCollision(), TEXT("removed: hidden, no collision (attached pieces too)"));
            SetUnitRemoved(World, Parent, true, true);
            Check(!Parent->IsHidden() && !Parent->GetActorEnableCollision() && Hidden->IsHidden(), TEXT("ghost: drawn as before, still no collision"));
            SetUnitRemoved(World, Parent, false);
            Check(!Parent->IsHidden() && Parent->GetActorEnableCollision() && !Child->IsHidden() && Child->GetActorEnableCollision(), TEXT("restored: visible with collision"));
            Check(Hidden->IsHidden() && !Hidden->GetActorEnableCollision(), TEXT("restore brings back each piece's own state (a piece hidden before stays hidden)"));
        }
        else Check(false, TEXT("fixture actors spawned"));
        for (AActor* A : {static_cast<AActor*>(Hidden), static_cast<AActor*>(Child), static_cast<AActor*>(Parent)}) if (A) A->Destroy();
    }

    UE_LOG(LogCireWorldEditTests, Display, TEXT("CIRE_WORLD_EDIT_TESTS_%s checks=%d"), Check.bPass ? TEXT("PASS") : TEXT("FAIL"), Check.Count);
    return Check.bPass;
}
