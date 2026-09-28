// world-editor: the WORLD tab of the map layout editor (-CireRouteEdit / RouteEditor.cmd; Docs/WorldEditor.md).
// Choose which town pieces a match removes: hover highlights a whole building (its pack Level Instance) or a prop, click
// selects, Shift-click adds, drag draws a box; Delete removes (protected pieces stay), Ctrl+Z / Ctrl+Y undo and redo, the
// REMOVED list restores, ghosts show what is gone, mirror removes the twin in the other realm. SAVE writes the named set
// and makes it the one matches load. In the editor removal only hides: nothing is destroyed.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLayoutEditorState.h"
#include "CireLayoutRuntime.h"
#include "CireMapLayout.h"
#include "CireNav.h"
#include "CireRouteEditMode.h"
#include "CireRouteEditor.h"
#include "CireTownMap.h"
#include "CireUIStyle.h"
#include "CireWorldEdit.h"
#include "CireWorldEditorState.h"
#include "Camera/CameraActor.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/Paths.h"

namespace
{
namespace ML = CireMapLayout;
const FLinearColor WERealmColor[2] = {FLinearColor(.25f, .56f, 1.f, 1.f), FLinearColor(.96f, .3f, .24f, 1.f)};
const FLinearColor WEGhost(1.f, .25f, .2f, .9f), WEHoverC(1.f, .86f, .35f, .85f), WESelC(1.f, .95f, .6f, 1.f), WEProtC(1.f, .2f, .15f, 1.f);
FString WEMeters(const FVector& Size) { return FString::Printf(TEXT("%.1f x %.1f x %.1f m"), Size.X / 100., Size.Y / 100., Size.Z / 100.); }
FString WEShortLabel(const FString& Label) { FString L = Label; L.RemoveFromStart(TEXT("LI_")); L.RemoveFromStart(TEXT("SM_")); L.RemoveFromEnd(TEXT("_Fix")); return L.Replace(TEXT("_"), TEXT(" ")).Left(34); }
FLinearColor WEGuardColor(ECireWorldEditGuard G) { return G == ECireWorldEditGuard::Protected ? WEProtC : G == ECireWorldEditGuard::Warn ? CireUIColors::Orange : CireUIColors::Teal; }
}

// ================================================================================================= actions
void CireWorldEditor::Load(FCireWorldEditorState& W)
{
    FCireWorldEditSet Set;
    const FCireWorldEditSettings& S = CireWorldEdit::Settings();
    W.bMirror = S.bMirror; W.bGhosts = S.bGhosts;
    if (!W.bNoFiles && CireWorldEdit::Load(Set, CireWorldEdit::DraftPath())) {}
    else if (!S.Active.IsEmpty() && CireWorldEdit::Load(Set, CireWorldEdit::NamedPath(S.Active))) { if (Set.Name.IsEmpty()) Set.Name = S.Active; }
    else Set = FCireWorldEditSet();
    W.Doc = FCireWorldEditDoc(); W.Doc.Set = Set; ++W.Doc.Revision;
    W.bLoaded = true;
}
void CireWorldEditor::RefreshIndex(UWorld* World, FCireWorldEditorState& W, bool bForce)
{
    if (!World) return;
    const int32 Levels = World->GetLevels().Num();
    if (!bForce && Levels == W.IndexLevels) return;
    W.IndexLevels = Levels;
    CireWorldEdit::BuildIndex(World, W.Units);
    W.ByKey.Reset();
    for (int32 I = 0; I < W.Units.Num(); ++I) W.ByKey.Add(W.Units[I].Key(), I);
    W.AppliedRevision = -1; // re-sync against the new index
}
void CireWorldEditor::Sync(UWorld* World, FCireWorldEditorState& W)
{
    if (!World || (W.AppliedRevision == W.Doc.Revision && W.bAppliedGhosts == W.bGhosts)) return;
    TSet<FString> Want;
    for (const FCireWorldEditEntry& E : W.Doc.Set.Removed) Want.Add(E.Key());
    for (auto It = W.Applied.CreateIterator(); It; ++It)
    {
        if (Want.Contains(It->Key)) continue;
        if (const int32* I = W.ByKey.Find(It->Key)) if (AActor* A = W.Units[*I].Actor.Get()) CireWorldEdit::SetUnitRemoved(World, A, false);
        It.RemoveCurrent();
    }
    for (const FString& Key : Want)
    {
        const bool* Was = W.Applied.Find(Key);
        if (Was && *Was == W.bGhosts) continue;
        const int32* I = W.ByKey.Find(Key);
        AActor* A = I ? W.Units[*I].Actor.Get() : nullptr;
        if (!A) continue; // not in this world (trimmed, or destroyed when a match loaded): the list still shows it
        CireWorldEdit::SetUnitRemoved(World, A, true, W.bGhosts);
        W.Applied.Add(Key, W.bGhosts);
    }
    W.AppliedRevision = W.Doc.Revision; W.bAppliedGhosts = W.bGhosts;
}
void CireWorldEditor::Unsync(UWorld* World, FCireWorldEditorState& W)
{
    for (const auto& P : W.Applied)
        if (const int32* I = W.ByKey.Find(P.Key)) if (AActor* A = W.Units[*I].Actor.Get()) CireWorldEdit::SetUnitRemoved(World, A, false);
    W.Applied.Reset(); W.AppliedRevision = -1;
}
FString CireWorldEditor::RemoveKeys(FCireWorldEditorState& W, const FCireMapLayout& Layout, const TArray<FString>& Keys)
{
    const FCireWorldEditSettings& S = CireWorldEdit::Settings();
    TArray<FCireWorldEditEntry> Go;
    int32 Protected = 0, Warned = 0; FString FirstProtected, FirstWarn;
    auto Consider = [&](const FCireWorldEditEntry& E, bool bTwin)
    {
        const FCireWorldEditGuard G = CireWorldEdit::Guard(Layout, E, S);
        if (G.Level == ECireWorldEditGuard::Protected) { if (!bTwin) { ++Protected; if (FirstProtected.IsEmpty()) FirstProtected = WEShortLabel(E.Label) + TEXT(": ") + G.Why; } return false; }
        if (G.Level == ECireWorldEditGuard::Warn && !bTwin) { ++Warned; if (FirstWarn.IsEmpty()) FirstWarn = WEShortLabel(E.Label) + TEXT(": ") + G.Why; }
        return true;
    };
    for (const FString& K : Keys)
    {
        const int32* I = W.ByKey.Find(K);
        if (!I || W.Doc.Set.Contains(K)) continue;
        const FCireWorldEditEntry E = CireWorldEdit::EntryOf(W.Units[*I]);
        if (Consider(E, false)) Go.Add(E);
    }
    int32 Twins = 0;
    if (W.bMirror)
    {
        TArray<FCireWorldEditEntry> Mine = Go;
        for (const FCireWorldEditEntry& E : Mine)
        {
            const FCireWorldEditEntry T = CireWorldEdit::TwinOf(E);
            const int32* I = W.ByKey.Find(T.Key());
            if (!I || W.Doc.Set.Contains(T.Key()) || Go.ContainsByPredicate([&](const FCireWorldEditEntry& X) { return X.Key() == T.Key(); })) continue;
            const FCireWorldEditEntry Twin = CireWorldEdit::EntryOf(W.Units[*I]);
            if (Consider(Twin, true)) { Go.Add(Twin); ++Twins; }
        }
    }
    const int32 N = CireWorldEdit::Remove(W.Doc, Go, FString::Printf(TEXT("remove %d"), Go.Num()));
    FString Msg = N > 0 ? FString::Printf(TEXT("Removed %d piece%s%s."), N - Twins, N - Twins == 1 ? TEXT("") : TEXT("s"), Twins ? *FString::Printf(TEXT(" and %d mirrored twin%s"), Twins, Twins == 1 ? TEXT("") : TEXT("s")) : TEXT(""))
                        : FString(TEXT("Nothing removed."));
    if (Protected) Msg += FString::Printf(TEXT(" %d PROTECTED kept (%s)."), Protected, *FirstProtected);
    if (Warned) Msg += FString::Printf(TEXT(" Check: %s."), *FirstWarn);
    if (N > 0) Msg += TEXT(" Ctrl+Z undoes.");
    return Msg;
}
FString CireWorldEditor::RestoreKeys(FCireWorldEditorState& W, const TArray<FString>& Keys)
{
    TArray<FString> All = Keys;
    if (W.bMirror)
        for (const FString& K : Keys)
            if (const FCireWorldEditEntry* E = W.Doc.Set.Find(K)) { const FString T = CireWorldEdit::TwinOf(*E).Key(); if (W.Doc.Set.Contains(T)) All.AddUnique(T); }
    const int32 N = CireWorldEdit::Restore(W.Doc, All, FString::Printf(TEXT("restore %d"), All.Num()));
    return N > 0 ? FString::Printf(TEXT("Restored %d piece%s (Ctrl+Z removes %s again)."), N, N == 1 ? TEXT("") : TEXT("s"), N == 1 ? TEXT("it") : TEXT("them")) : FString(TEXT("Nothing to restore."));
}
void CireWorldEditor::Validate(UWorld* World, FCireWorldEditorState& W, const FCireMapLayout& Layout, bool bNav)
{
    const FCireWorldEditSettings& S = CireWorldEdit::Settings();
    W.Issues = CireWorldEdit::Issues(Layout, W.Doc.Set, S);
    W.bValidated = true;
    if (!bNav || !World || !CireNav::HasNavigation(World)) return;
    CireNav::FlushBuild(World); // the removals' navmesh first
    // Markers that stood in a removed piece: still on the navmesh?
    for (const FCireWorldEditEntry& E : W.Doc.Set.Removed)
    {
        const FBox2D Box = E.Footprint(200.f);
        for (const FCireMapMarker& M : Layout.Markers)
        {
            if (!ML::ShownInRealm(M, E.Realm) || M.Type == ML::PlayBounds || M.Type == ML::Zone || M.Type == ML::Blocker || M.Type == ML::MonsterPath) continue;
            if (!Box.IsInside(M.Position) || CireLayoutEditor::MarkerOnNavmesh(World, E.Realm, M.Position)) continue;
            W.Issues.Add({true, E.Realm + 1, M.Id, FString::Printf(TEXT("%s is off the navmesh now that %s is gone (%s): move it or restore the piece"), *ML::DisplayLabel(Layout, M), *WEShortLabel(E.Label), *ML::RealmName(E.Realm))});
        }
    }
    // Monster paths: every segment's reach now, against the town as it was when the WORLD tab opened.
    W.LastReach.Reset();
    int32 Blocked = 0, Opened = 0;
    for (const FCireMapMarker& M : Layout.Markers)
    {
        if (M.Type != ML::MonsterPath) continue;
        const TArray<FVector2D> Walk = ML::WalkPolyline(Layout, M.Id);
        for (int32 Realm = 0; Realm < 2; ++Realm)
        {
            if (!ML::ShownInRealm(M, Realm)) continue;
            for (int32 I = 0; I + 1 < Walk.Num(); ++I)
            {
                float Length = 0;
                const ECireRouteReach R = CireRouteEditor::Reach(World, CireLanePath::ToWorld(Realm, Walk[I], 60.f), CireLanePath::ToWorld(Realm, Walk[I + 1], 60.f), Length);
                const FString Key = FString::Printf(TEXT("%d|%s|%d"), Realm, *M.Id, I);
                W.LastReach.Add(Key, R);
                const ECireRouteReach* Was = W.Baseline.Find(Key);
                if (!Was || *Was == R) continue;
                const bool bWasOk = *Was == ECireRouteReach::Direct || *Was == ECireRouteReach::Detour, bNowOk = R == ECireRouteReach::Direct || R == ECireRouteReach::Detour;
                const FString Where = FString::Printf(TEXT("%s point %d-%d (%s)"), *ML::DisplayLabel(Layout, M), I + 1, I + 2, *ML::RealmName(Realm));
                if (bWasOk && !bNowOk) { ++Blocked; W.Issues.Add({true, Realm + 1, M.Id, FString::Printf(TEXT("BLOCKED: %s no longer walks through (%s)"), *Where, CireRouteEditor::ReachLabel(R))}); }
                else if (!bWasOk && bNowOk) { ++Opened; W.Issues.Add({false, Realm + 1, M.Id, FString::Printf(TEXT("UNBLOCKED: %s now walks through (%s)"), *Where, CireRouteEditor::ReachLabel(R))}); }
                else if (*Was == ECireRouteReach::Detour && R == ECireRouteReach::Direct) W.Issues.Add({false, Realm + 1, M.Id, FString::Printf(TEXT("Shortcut: %s now goes straight through where a piece stood"), *Where)});
                else if (*Was == ECireRouteReach::Direct && R == ECireRouteReach::Detour) W.Issues.Add({false, Realm + 1, M.Id, FString::Printf(TEXT("Detour: %s now walks around something"), *Where)});
            }
        }
    }
    if (W.Baseline.IsEmpty()) W.Baseline = W.LastReach;
}
void CireWorldEditor::Autosave(FCireWorldEditorState& W, double Now, bool bForce)
{
    if (!W.bUnsaved || W.bNoFiles || (!bForce && Now - W.EditedAt < .75)) return;
    if (CireWorldEdit::Save(W.Doc.Set, CireWorldEdit::DraftPath())) { W.bUnsaved = false; W.SavedAt = Now; }
}
bool CireWorldEditor::SaveAndActivate(FCireWorldEditorState& W, const FString& InName, FString& Out)
{
    const FString Name = CireWorldEdit::SanitizeName(InName);
    if (Name.IsEmpty()) { Out = TEXT("Give the set a name first (SAVE AS)."); return false; }
    W.Doc.Set.Name = Name;
    FString Error;
    if (!CireWorldEdit::Save(W.Doc.Set, CireWorldEdit::NamedPath(Name), &Error) || !CireWorldEdit::SaveActiveName(Name, &Error)) { Out = Error; return false; }
    const FCireWorldEditSavings Sv = CireWorldEdit::Savings(W.Doc.Set);
    Out = FString::Printf(TEXT("SAVED \"%s\" (Content/Data/WorldEdits/%s.json) and made it ACTIVE: matches load without these %d pieces (~%d actors, ~%d draw calls less)."), *Name, *Name, Sv.Units, Sv.Actors, Sv.DrawCalls);
    W.bUnsaved = true;
    return true;
}
bool CireWorldEditor::Escape(FCireLayoutEditorState& E)
{
    if (!E.bWorldTab || !E.WorldEdit.IsValid()) return false;
    FCireWorldEditorState& W = *E.WorldEdit;
    if (E.Naming == 3) { E.Naming = 0; return true; }
    if (W.bLoadList) { W.bLoadList = false; return true; }
    if (!W.Sel.IsEmpty()) { W.Sel.Reset(); return true; }
    return false;
}

// ================================================================================================= HUD
void ACireHUD::DrawEditorTabs()
{
#if !UE_BUILD_SHIPPING
    if (!LayoutEditor.IsValid()) return;
    FCireLayoutEditorState& E = *LayoutEditor;
    const float W = 150.f, H = 26.f, X = ViewW * .5f - W - 3.f, Y = 6.f;
    LayoutUIRects.Add({X, Y, W * 2 + 6, H});
    for (int32 K = 0; K < 2; ++K)
    {
        const bool bWorld = K == 1, bOn = E.bWorldTab == bWorld;
        const float BX = X + K * (W + 6);
        const bool bOver = Hit(BX, Y, W, H);
        CireUIStyle::Button(Painter(), BX, Y, W, H, bWorld ? TEXT("WORLD  B") : TEXT("MAP LAYOUT  B"), bOn ? ECireButtonState::Selected : bOver ? ECireButtonState::Hover : ECireButtonState::Normal,
            bWorld ? CireUIColors::Red : CireUIColors::Gold, 9.5f);
        Tip(bWorld ? TEXT("WORLD") : TEXT("MAP LAYOUT"), bWorld ? TEXT("Choose which town pieces matches remove: buildings and props you only need for looks. Hover highlights a whole building, click / Shift-click / drag a box to select, Delete removes, Ctrl+Z / Ctrl+Y undo and redo. B switches tabs.")
            : TEXT("Place the gameplay markers: spawns, paths, packs, vendors, bounds. B switches tabs."), BX, Y, W, H);
        if (bOver && Clicked && !bOn) { Clicked = false; PlayUIFeedback(); E.bWorldTab = bWorld; E.Armed = NAME_None; E.bReplace = false; E.ChainId.Reset(); }
    }
#endif
}

void ACireHUD::TickWorldEditor()
{
#if !UE_BUILD_SHIPPING
    if (!LayoutEditor.IsValid() || !Canvas || !PlayerOwner) return;
    FCireLayoutEditorState& E = *LayoutEditor;
    if (!E.WorldEdit.IsValid()) E.WorldEdit = MakeShared<FCireWorldEditorState>();
    FCireWorldEditorState& W = *E.WorldEdit;
    UWorld* World = GetWorld();
    const FCireMapLayout& L = E.Layout;
    const double Now = World->GetRealTimeSeconds();
    ACireHero* Hero = Cast<ACireHero>(PlayerOwner->GetPawn());
    auto Pressed = [&](FKey K) { return PlayerOwner->WasInputKeyJustPressed(K); };
    const bool bCtrl = PlayerOwner->IsInputKeyDown(EKeys::LeftControl) || PlayerOwner->IsInputKeyDown(EKeys::RightControl);
    const bool bShift = PlayerOwner->IsInputKeyDown(EKeys::LeftShift) || PlayerOwner->IsInputKeyDown(EKeys::RightShift);
    auto Say = [&](const FString& Text) { CireLayoutEditor::Say(E, Text, Now); };
    const FCireWorldEditSettings& S = CireWorldEdit::Settings();

    // ---- the document, the unit index and what the world shows ----------------------------------------------------------
    if (!W.bLoaded)
    {
        CireWorldEditor::Load(W);
        CireWorldEditor::RefreshIndex(World, W, true);
        // The baseline for "blocked / unblocked": the whole town, before the set is shown.
        if (CireNav::HasNavigation(World) && CireNav::IsReady(World)) { FCireWorldEditSet Keep = W.Doc.Set; W.Doc.Set.Removed.Reset(); CireWorldEditor::Validate(World, W, L, true); W.Doc.Set = Keep; W.Issues.Reset(); W.bValidated = false; }
        Say(FString::Printf(TEXT("WORLD: %d removable pieces in both realms; %s. Hover a building, click to select, Delete removes."), W.Units.Num(),
            W.Doc.Set.Removed.IsEmpty() ? TEXT("nothing removed yet") : *FString::Printf(TEXT("%d already removed (%s)"), W.Doc.Set.Removed.Num(), W.Doc.Set.Name.IsEmpty() ? TEXT("draft") : *W.Doc.Set.Name)));
    }
    CireWorldEditor::RefreshIndex(World, W);
    const int32 RevisionBefore = W.Doc.Revision;
    CireWorldEditor::Sync(World, W);

    // ---- layout ----------------------------------------------------------------------------------------------------------
    const float SlotSize = 44.f, SlotGap = 8.f;
    const int32 SlotCount = 10;
    const float BarW = SlotCount * (SlotSize + SlotGap) - SlotGap, BarX = ViewW * .5f - BarW * .5f, BarY = ViewH - SlotSize - 24.f;
    const float CmdY = BarY - 34.f, CmdH = 24.f;
    const float ListX = 12.f, ListY = 12.f, ListW = 320.f, ListH = CmdY - 24.f;
    const float InspW = 330.f, InspX = ViewW - InspW - 12.f, InspY = 12.f, InspH = CmdY - 24.f;
    const float CmdW = 7 * 96.f + 6 * 4.f, CmdX = ViewW * .5f - CmdW * .5f;
    LayoutUIRects.Add({BarX - 10, BarY - 8, BarW + 20, SlotSize + 32});
    LayoutUIRects.Add({ListX, ListY, ListW, ListH});
    LayoutUIRects.Add({InspX, InspY, InspW, InspH});
    LayoutUIRects.Add({CmdX, CmdY, CmdW, CmdH});
    DrawEditorTabs();
    if (E.Naming == 3) LayoutUIRects.Add({ViewW * .5f - 190.f, ViewH * .35f, 380.f, 74.f}); // the overlays below
    if (W.bLoadList) LayoutUIRects.Add({ViewW * .5f - 170.f, ViewH * .25f, 340.f, 46.f + FMath::Max(1, CireWorldEdit::ListNamed().Num()) * 26.f});
    bool bOverUI = false;
    for (const FCireUIRect& R : LayoutUIRects) bOverUI |= MX >= R.X && MX <= R.X + R.W && MY >= R.Y && MY <= R.Y + R.H;

    // ---- projection helpers ------------------------------------------------------------------------------------------------
    auto Project = [&](const FVector& P, FVector2D& Out)
    {
        const FVector Sc = Canvas->Project(P);
        if (Sc.Z <= 0.f) return false;
        Out = FVector2D(Sc.X / Scale, Sc.Y / Scale);
        return Out.X > -600 && Out.Y > -600 && Out.X < ViewW + 600 && Out.Y < ViewH + 600;
    };
    auto DrawBox = [&](const FBox& B, FLinearColor C, float Width)
    {
        const FVector Mn = B.Min, Mx = B.Max;
        const FVector V[8] = {{Mn.X, Mn.Y, Mn.Z}, {Mx.X, Mn.Y, Mn.Z}, {Mx.X, Mx.Y, Mn.Z}, {Mn.X, Mx.Y, Mn.Z}, {Mn.X, Mn.Y, Mx.Z}, {Mx.X, Mn.Y, Mx.Z}, {Mx.X, Mx.Y, Mx.Z}, {Mn.X, Mx.Y, Mx.Z}};
        FVector2D P[8]; bool bOk[8];
        for (int32 I = 0; I < 8; ++I) bOk[I] = Project(V[I], P[I]);
        static const int32 Edges[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (const auto& Ed : Edges) if (bOk[Ed[0]] && bOk[Ed[1]]) Line(P[Ed[0]].X, P[Ed[0]].Y, P[Ed[1]].X, P[Ed[1]].Y, C, Width);
    };
    auto Caption = [&](const FVector& At, const FString& Text, FLinearColor C, float Size = 9.5f)
    {
        FVector2D P; if (!Project(At, P)) return;
        const float TW = TextWidth(Text, Size);
        Painter().Rect(P.X - TW * .5f - 6, P.Y - 4, TW + 12, Size + 9, FLinearColor(0, 0, 0, .6f));
        TextFx(Text, P.X - TW * .5f, P.Y - 2, Size, C, ECireFont::Bold, true);
    };
    auto UnitOf = [&](const FString& Key) -> const FCireWorldUnit* { const int32* I = W.ByKey.Find(Key); return I ? &W.Units[*I] : nullptr; };
    auto EntryFor = [&](const FString& Key) -> FCireWorldEditEntry
    {
        if (const FCireWorldEditEntry* E2 = W.Doc.Set.Find(Key)) return *E2;
        if (const FCireWorldUnit* U = UnitOf(Key)) return CireWorldEdit::EntryOf(*U);
        return FCireWorldEditEntry();
    };
    auto BoundsOf = [&](const FString& Key, FBox& Out)
    {
        if (const FCireWorldUnit* U = UnitOf(Key)) { Out = U->Bounds; return true; }
        if (const FCireWorldEditEntry* E2 = W.Doc.Set.Find(Key))
        {
            const FVector C = CireLanePath::ToWorld(E2->Realm, FVector2D(E2->Local), 0.f) + FVector(0, 0, CireTownMap::RealmOrigin(E2->Realm).Z + E2->Local.Z);
            Out = FBox(C - E2->Size * .5, C + E2->Size * .5); return true;
        }
        return false;
    };

    // ---- hover: the building under the pointer (or a ghost near it) ---------------------------------------------------------
    W.Hover.Reset(); W.bHoverGhost = false;
    if (!bOverUI && MX >= 0 && E.Naming == 0 && !W.bDragging)
    {
        FVector RayFrom, RayDir;
        if (PlayerOwner->DeprojectScreenPositionToWorld(MX * Scale, MY * Scale, RayFrom, RayDir))
        {
            FHitResult HitResult; FCollisionQueryParams Params(TEXT("CireWorldEditPick"), true, Hero);
            if (World->LineTraceSingleByChannel(HitResult, RayFrom, RayFrom + RayDir * 80000.f, ECC_Visibility, Params))
                if (AActor* UnitActor = CireWorldEdit::UnitActorOf(World, HitResult.GetActor()))
                {
                    int32 Realm = 0; const FString Id = CireWorldEdit::ActorId(UnitActor, &Realm);
                    const FString Key = FString::Printf(TEXT("%d|%s"), Realm, *Id);
                    if (W.ByKey.Contains(Key) && !W.Doc.Set.Contains(Key)) W.Hover = Key;
                }
        }
        if (W.Hover.IsEmpty() && W.bGhosts)
        {
            float Best = 34.f;
            for (const FCireWorldEditEntry& R : W.Doc.Set.Removed)
            {
                FBox B; FVector2D P;
                if (!BoundsOf(R.Key(), B) || !Project(B.GetCenter(), P)) continue;
                const float D = FVector2D::Distance(P, FVector2D(MX, MY));
                if (D < Best) { Best = D; W.Hover = R.Key(); W.bHoverGhost = true; }
            }
        }
    }

    // ---- world drawing: ghosts, selection, hover ----------------------------------------------------------------------------
    if (W.bGhosts)
        for (const FCireWorldEditEntry& R : W.Doc.Set.Removed)
        {
            FBox B; if (!BoundsOf(R.Key(), B)) continue;
            DrawBox(B, WEGhost, 1.5f);
            if (W.Doc.Set.Removed.Num() < 60 || W.Hover == R.Key()) Caption(B.GetCenter() + FVector(0, 0, B.GetExtent().Z + 60), TEXT("REMOVED  ") + WEShortLabel(R.Label), WEGhost, 8.5f);
        }
    for (const FString& K : W.Sel)
    {
        FBox B; if (!BoundsOf(K, B)) continue;
        DrawBox(B.ExpandBy(20.f), WESelC, 3.f + FMath::Sin(Now * 6.) * .8f);
    }
    if (!W.Hover.IsEmpty())
    {
        FBox B;
        if (BoundsOf(W.Hover, B))
        {
            const FCireWorldEditEntry HE = EntryFor(W.Hover);
            const FCireWorldEditGuard G = W.bHoverGhost ? FCireWorldEditGuard() : CireWorldEdit::Guard(L, HE, S);
            DrawBox(B.ExpandBy(10.f), W.bHoverGhost ? WEGhost : G.Level == ECireWorldEditGuard::Protected ? WEProtC : WEHoverC, 2.f);
            FString Text = FString::Printf(TEXT("%s  |  %s  |  %s  |  %d pieces"), *WEShortLabel(HE.Label), *HE.Kind, *WEMeters(HE.Size), HE.Actors);
            if (W.bHoverGhost) Text = TEXT("REMOVED  ") + Text + TEXT("  |  click, then R restores");
            else if (G.Level == ECireWorldEditGuard::Protected) Text += TEXT("  |  PROTECTED");
            TextFx(Text, MX + 18, MY + 12, 9.5f, W.bHoverGhost ? WEGhost : G.Level == ECireWorldEditGuard::Protected ? WEProtC : CireUIColors::Parchment, ECireFont::Bold, true);
            if (G.Level != ECireWorldEditGuard::Ok && !W.bHoverGhost) Wrapped(G.Why, MX + 18, MY + 28, 420.f, 8.5f, WEGuardColor(G.Level), 2);
        }
    }

    // ---- mouse: click / Shift-click / box ---------------------------------------------------------------------------------
    const bool bDown = PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
    if (Clicked && !bOverUI && E.Naming == 0 && !bSettings) { W.bPressing = true; W.bDragging = false; W.PressAt = FVector2D(MX, MY); Clicked = false; }
    if (W.bPressing && bDown && FVector2D::Distance(W.PressAt, FVector2D(MX, MY)) > 8.f) W.bDragging = true;
    if (W.bPressing && W.bDragging && bDown)
    {
        const float X0 = FMath::Min<float>(W.PressAt.X, MX), Y0 = FMath::Min<float>(W.PressAt.Y, MY), X1 = FMath::Max<float>(W.PressAt.X, MX), Y1 = FMath::Max<float>(W.PressAt.Y, MY);
        Painter().Rect(X0, Y0, X1 - X0, Y1 - Y0, FLinearColor(1.f, .85f, .4f, .08f));
        Line(X0, Y0, X1, Y0, WEHoverC, 1.5f); Line(X1, Y0, X1, Y1, WEHoverC, 1.5f); Line(X1, Y1, X0, Y1, WEHoverC, 1.5f); Line(X0, Y1, X0, Y0, WEHoverC, 1.5f);
    }
    if (W.bPressing && !bDown)
    {
        W.bPressing = false;
        if (W.bDragging)
        {
            // Box select: every live piece of the viewed realm whose bounds project entirely inside the box.
            W.bDragging = false;
            const float X0 = FMath::Min<float>(W.PressAt.X, MX), Y0 = FMath::Min<float>(W.PressAt.Y, MY), X1 = FMath::Max<float>(W.PressAt.X, MX), Y1 = FMath::Max<float>(W.PressAt.Y, MY);
            if (!bShift) W.Sel.Reset();
            int32 Added = 0, Skipped = 0;
            for (const FCireWorldUnit& U : W.Units)
            {
                if (U.Realm != E.Realm || W.Doc.Set.Contains(U.Key()) || !U.Actor.IsValid()) continue;
                const FVector Mn = U.Bounds.Min, Mx = U.Bounds.Max;
                bool bIn = true;
                for (int32 C = 0; C < 8 && bIn; ++C)
                {
                    const FVector V((C & 1) ? Mx.X : Mn.X, (C & 2) ? Mx.Y : Mn.Y, (C & 4) ? Mx.Z : Mn.Z);
                    FVector2D P; bIn = Project(V, P) && P.X >= X0 && P.X <= X1 && P.Y >= Y0 && P.Y <= Y1;
                }
                if (!bIn) continue;
                if (FMath::Max(U.Bounds.GetSize().X, U.Bounds.GetSize().Y) > S.BoxSelectMaxSize) { ++Skipped; continue; }
                if (!W.Sel.Contains(U.Key())) { W.Sel.Add(U.Key()); ++Added; }
            }
            Say(FString::Printf(TEXT("Box: %d pieces selected%s. Delete removes them as one step."), W.Sel.Num(), Skipped ? *FString::Printf(TEXT(" (%d larger than %.0f m skipped)"), Skipped, S.BoxSelectMaxSize / 100.f) : TEXT("")));
        }
        else if (!W.Hover.IsEmpty())
        {
            if (bShift) { if (W.Sel.Contains(W.Hover)) W.Sel.Remove(W.Hover); else W.Sel.Add(W.Hover); }
            else { W.Sel.Reset(); W.Sel.Add(W.Hover); }
        }
        else if (!bShift) W.Sel.Reset();
    }

    // ---- actions -------------------------------------------------------------------------------------------------------------
    TArray<FString> LiveSel, GhostSel;
    for (const FString& K : W.Sel) (W.Doc.Set.Contains(K) ? GhostSel : LiveSel).Add(K);
    auto DoRemove = [&]() { if (LiveSel.IsEmpty()) { Say(TEXT("Select pieces first: click, Shift-click or drag a box.")); return; } Say(CireWorldEditor::RemoveKeys(W, L, LiveSel)); W.Sel.Reset(); };
    auto DoRestore = [&]() { if (GhostSel.IsEmpty()) { Say(TEXT("Select a removed piece (its ghost, or a row of the REMOVED list) to restore it.")); return; } Say(CireWorldEditor::RestoreKeys(W, GhostSel)); W.Sel.Reset(); };
    auto DoUndo = [&]() { FString What; Say(CireWorldEdit::Undo(W.Doc, &What) ? FString::Printf(TEXT("Undone: %s."), *What) : FString(TEXT("Nothing to undo."))); };
    auto DoRedo = [&]() { FString What; Say(CireWorldEdit::Redo(W.Doc, &What) ? FString::Printf(TEXT("Redone: %s."), *What) : FString(TEXT("Nothing to redo."))); };
    auto DoMirror = [&]() { W.bMirror = !W.bMirror; Say(W.bMirror ? TEXT("Mirror ON: removing (or restoring) a piece does the same to its twin in the other realm.") : TEXT("Mirror OFF: only the realm you pick in.")); };
    auto DoGhosts = [&]() { W.bGhosts = !W.bGhosts; Say(W.bGhosts ? TEXT("Ghosts ON: removed pieces show with a red outline; click one and press R to restore it.") : TEXT("Ghosts OFF: removed pieces are hidden, as in a match.")); };
    auto DoValidate = [&]()
    {
        CireWorldEditor::Validate(World, W, L, true);
        int32 Errors = 0; for (const FCireLayoutIssue& I : W.Issues) Errors += I.bError ? 1 : 0;
        Say(Errors == 0 ? FString::Printf(TEXT("VALID: no marker lost its ground, no path blocked%s."), W.Issues.Num() ? *FString::Printf(TEXT(" (%d notes on the right)"), W.Issues.Num()) : TEXT(""))
            : FString::Printf(TEXT("%d problems (listed on the right): restore those pieces or move the markers."), Errors));
    };
    auto DoSave = [&]()
    {
        if (W.Doc.Set.Name.IsEmpty()) { E.Naming = 3; E.NameBuffer.Reset(); Say(TEXT("Name the set (Enter saves).")); return; }
        FString Out; CireWorldEditor::SaveAndActivate(W, W.Doc.Set.Name, Out); Say(Out);
    };
    auto DoTest = [&]()
    {
        if (W.Doc.Set.Name.IsEmpty()) { DoSave(); return; }
        FString Out;
        if (!CireWorldEditor::SaveAndActivate(W, W.Doc.Set.Name, Out)) { Say(Out); return; }
        FString Launch; CireLayoutRuntime::LaunchTestMatch(Launch);
        Say(Out + TEXT(" ") + Launch);
    };
    auto DoOff = [&]()
    {
        FString Error;
        Say(CireWorldEdit::SaveActiveName(FString(), &Error) ? FString(TEXT("No world edit active: matches load the whole town. Your draft stays here (SAVE makes it active again).")) : Error);
    };
    auto DoRestoreAll = [&]()
    {
        TArray<FString> All; for (const FCireWorldEditEntry& R : W.Doc.Set.Removed) All.Add(R.Key());
        const int32 N = CireWorldEdit::Restore(W.Doc, All, TEXT("restore all"));
        Say(N ? FString::Printf(TEXT("Restored all %d pieces (Ctrl+Z removes them again)."), N) : FString(TEXT("Nothing removed.")));
    };
    auto FlyTo = [&](const FString& Key)
    {
        FBox B; if (!BoundsOf(Key, B)) return;
        const int32 Realm = FCString::Atoi(*Key.Left(1));
        const FVector C = B.GetCenter();
        const float ViewYaw = E.bWalk ? PlayerOwner->GetControlRotation().Yaw : E.Yaw;
        if (E.bWalk && Hero)
        {
            const FVector2D Local = CireLanePath::ToLocal(Realm, C);
            const double R = FMath::DegreesToRadians(ViewYaw);
            CireRouteEditMode::TeleportTo(Hero, Realm, Local - FVector2D(FMath::Cos(R), FMath::Sin(R)) * (FMath::Max(B.GetExtent().X, B.GetExtent().Y) + 700.0), ViewYaw);
        }
        else { E.Realm = Realm; E.Focus = FVector(C.X, C.Y, B.Min.Z); }
    };

    // ---- keyboard ------------------------------------------------------------------------------------------------------------
    if (E.Naming == 3 && !bSettings)
    {
        const FKey Letters[] = {EKeys::A, EKeys::B, EKeys::C, EKeys::D, EKeys::E, EKeys::F, EKeys::G, EKeys::H, EKeys::I, EKeys::J, EKeys::K, EKeys::L, EKeys::M,
            EKeys::N, EKeys::O, EKeys::P, EKeys::Q, EKeys::R, EKeys::S, EKeys::T, EKeys::U, EKeys::V, EKeys::W, EKeys::X, EKeys::Y, EKeys::Z};
        const FKey Digits[] = {EKeys::Zero, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
        for (int32 I = 0; I < 26; ++I) if (Pressed(Letters[I]) && E.NameBuffer.Len() < 40) E.NameBuffer.AppendChar(static_cast<TCHAR>((bShift ? 'A' : 'a') + I));
        for (int32 I = 0; I < 10; ++I) if (Pressed(Digits[I]) && E.NameBuffer.Len() < 40) E.NameBuffer.AppendChar(static_cast<TCHAR>('0' + I));
        if (Pressed(EKeys::SpaceBar) && E.NameBuffer.Len() < 40) E.NameBuffer.AppendChar(TEXT(' '));
        if (Pressed(EKeys::Hyphen) && E.NameBuffer.Len() < 40) E.NameBuffer.AppendChar(bShift ? TEXT('_') : TEXT('-'));
        if (Pressed(EKeys::BackSpace) && !E.NameBuffer.IsEmpty()) E.NameBuffer.LeftChopInline(1);
        if (Pressed(EKeys::Enter))
        {
            FString Out; CireWorldEditor::SaveAndActivate(W, E.NameBuffer, Out); Say(Out);
            E.Naming = 0;
        }
    }
    else if (!bSettings && E.Naming == 0)
    {
        if (Pressed(EKeys::Delete) || Pressed(EKeys::X)) DoRemove();
        if (Pressed(EKeys::R)) DoRestore();
        if ((bCtrl && Pressed(EKeys::Z) && !bShift) || (Pressed(EKeys::BackSpace) && !bCtrl)) DoUndo();
        if (bCtrl && (Pressed(EKeys::Y) || (bShift && Pressed(EKeys::Z)))) DoRedo();
        if (Pressed(EKeys::Y) && !bCtrl) DoMirror();
        if (Pressed(EKeys::H)) DoGhosts();
        if (Pressed(EKeys::V)) DoValidate();
        if (bCtrl && Pressed(EKeys::S)) DoSave();
        if (bCtrl && Pressed(EKeys::T)) DoTest();
        if (Pressed(EKeys::F) && W.Sel.Num() > 0) FlyTo(W.Sel.Last());
        if (Pressed(EKeys::B)) { E.bWorldTab = false; Say(TEXT("MAP LAYOUT tab (B: back to WORLD).")); }
        if (Pressed(EKeys::M))
        {
            E.bWalk = !E.bWalk;
            if (!E.bWalk && Hero) { E.Focus = Hero->GetActorLocation(); E.Yaw = PlayerOwner->GetControlRotation().Yaw; }
            Say(E.bWalk ? TEXT("Walk view: walk the town; hover and click buildings.") : TEXT("Map view: WASD pan, Q/E rotate, wheel zoom; drag a box to select many."));
        }
        if (Pressed(EKeys::G))
        {
            if (E.bWalk && Hero) CireRouteEditMode::SwitchRealm(Hero);
            else { const FVector2D Local = CireLanePath::ToLocal(E.Realm, E.Focus); E.Realm = 1 - E.Realm; E.Focus = CireLanePath::ToWorld(E.Realm, Local, 0.f); }
            Say(TEXT("Other realm: the same town, the other team's copy."));
        }
        if (!E.bWalk && !bOverUI)
        {
            if (Pressed(EKeys::MouseScrollUp)) E.Distance = FMath::Max(900.f, E.Distance * .85f);
            if (Pressed(EKeys::MouseScrollDown)) E.Distance = FMath::Min(30000.f, E.Distance * 1.18f);
        }
    }

    // ---- panel: action bar (icons) ------------------------------------------------------------------------------------------
    CireUIStyle::Frame(Painter(), BarX - 10, BarY - 8, BarW + 20, SlotSize + 32, CireUIColors::Red, ECireFrame::Panel);
    struct FWEAction { const TCHAR* Icon; const TCHAR* Caption; const TCHAR* Key; FLinearColor Tint; bool bEnabled, bOn; const TCHAR* Help; TFunction<void()> Run; };
    const FWEAction Actions[SlotCount] = {
        {TEXT("cataclysm"), TEXT("REMOVE"), TEXT("Del"), CireUIColors::Red, LiveSel.Num() > 0, false, TEXT("Remove the selected pieces (one undo step). Protected pieces stay; with MIRROR on their twins in the other realm go too."), DoRemove},
        {TEXT("renewal"), TEXT("RESTORE"), TEXT("R"), CireUIColors::Teal, GhostSel.Num() > 0, false, TEXT("Bring back the selected removed pieces (select a ghost or a REMOVED row)."), DoRestore},
        {TEXT("restoring_light"), TEXT("UNDO"), TEXT("^Z"), CireUIColors::Gold, W.Doc.Undo.Num() > 0, false, TEXT("Undo the last remove / restore / load (Ctrl+Z, Backspace). A box remove is one step."), DoUndo},
        {TEXT("chain_spark"), TEXT("REDO"), TEXT("^Y"), CireUIColors::Gold, W.Doc.Redo.Num() > 0, false, TEXT("Redo (Ctrl+Y or Ctrl+Shift+Z)."), DoRedo},
        {TEXT("warp_obelisk"), TEXT("MIRROR"), TEXT("Y"), CireUIColors::Purple, true, W.bMirror, TEXT("Mirror: removing or restoring a piece does the same to its twin in the other realm (on by default; the realms are copies of one town)."), DoMirror},
        {TEXT("spectral_pack"), TEXT("GHOSTS"), TEXT("H"), WEGhost, true, W.bGhosts, TEXT("Show removed pieces as ghosts (red outline) so you can find them again and restore them."), DoGhosts},
        {TEXT("purify"), TEXT("VALIDATE"), TEXT("V"), CireUIColors::Teal, true, W.bValidated, TEXT("Check the layout against the removals: protected pieces, markers that lost their ground or the navmesh, monster paths blocked or unblocked since this tab opened."), DoValidate},
        {TEXT("keeper_beacon"), TEXT("SAVE"), TEXT("^S"), CireUIColors::Gold, true, false, TEXT("Save the set (Content/Data/WorldEdits/<name>.json) and make it the ACTIVE set every match loads."), DoSave},
        {TEXT("chieftain_banner"), TEXT("TEST"), TEXT("^T"), CireUIColors::Gold, true, false, TEXT("Save, make it active and launch a real match on it in a new window."), DoTest},
        {TEXT("banishment"), TEXT("RESTORE ALL"), TEXT(""), CireUIColors::Teal, W.Doc.Set.Removed.Num() > 0, false, TEXT("Bring every removed piece back (one undo step)."), DoRestoreAll},
    };
    for (int32 I = 0; I < SlotCount; ++I)
    {
        const FWEAction& A = Actions[I];
        const float SX = BarX + I * (SlotSize + SlotGap);
        FCireIconSlot Slot;
        Slot.IconId = A.Icon; Slot.IconTexture = CireUIStyle::FindAbilityIcon(A.Icon); Slot.Tint = A.Tint;
        Slot.KeyLabel = A.Key; Slot.bHover = Hit(SX, BarY, SlotSize, SlotSize); Slot.bGlow = A.bOn; Slot.bPressed = A.bOn; Slot.bNoResource = !A.bEnabled;
        CireUIStyle::IconSlot(Painter(), SX, BarY, SlotSize, Slot, Now);
        const FString Cap = A.Caption;
        TextFx(Cap, SX + SlotSize * .5f - TextWidth(Cap, 7.5f) * .5f, BarY + SlotSize + 3, 7.5f, A.bOn ? CireUIColors::BrightGold : A.bEnabled ? CireUIColors::Parchment : CireUIColors::Muted, ECireFont::Bold, true);
        Tip(A.Caption, A.Help, SX, BarY, SlotSize, SlotSize);
        if (Slot.bHover && Clicked) { Clicked = false; PlayUIFeedback(); if (A.bEnabled) A.Run(); }
    }
    // Command row.
    {
        auto Button = [&](const FString& Title, float BX, float W2, const FString& Help, bool bEnabled = true, bool bSelected = false, FLinearColor Accent = CireUIColors::Gold)
        {
            const bool Over = bEnabled && Hit(BX, CmdY, W2, CmdH);
            CireUIStyle::Button(Painter(), BX, CmdY, W2, CmdH, Title, !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : Over ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 8.5f);
            Tip(Title, Help, BX, CmdY, W2, CmdH);
            if (Over && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
            return false;
        };
        float X = CmdX; const float BW = 96.f;
        if (Button(TEXT("SAVE AS"), X, BW, TEXT("Save this set under a new name and make it active."))) { E.Naming = 3; E.NameBuffer = W.Doc.Set.Name; } X += BW + 4;
        if (Button(TEXT("LOAD"), X, BW, TEXT("Load a named set into the editor (undoable)."), true, W.bLoadList)) W.bLoadList = !W.bLoadList; X += BW + 4;
        if (Button(TEXT("SET OFF"), X, BW, TEXT("No active set: matches load the whole town. Your draft stays in the editor."), !S.Active.IsEmpty(), false, CireUIColors::Red)) DoOff(); X += BW + 4;
        if (Button(TEXT("FLY TO  F"), X, BW, TEXT("Go to the selection."), W.Sel.Num() > 0)) FlyTo(W.Sel.Last()); X += BW + 4;
        if (Button(TEXT("RESCAN"), X, BW, TEXT("Rebuild the list of removable pieces (after late streaming)."))) { CireWorldEditor::RefreshIndex(World, W, true); Say(FString::Printf(TEXT("%d removable pieces."), W.Units.Num())); } X += BW + 4;
        if (Button(E.bWalk ? TEXT("MAP VIEW  M") : TEXT("WALK  M"), X, BW, TEXT("Walk view (your champion) or map view (top-down camera; drag boxes over whole blocks)."))) { E.bWalk = !E.bWalk; if (!E.bWalk && Hero) { E.Focus = Hero->GetActorLocation(); E.Yaw = PlayerOwner->GetControlRotation().Yaw; } } X += BW + 4;
        if (Button(TEXT("REALM  G"), X, BW, TEXT("Go to the other realm (the same town, the other team's copy).")))
        {
            if (E.bWalk && Hero) CireRouteEditMode::SwitchRealm(Hero);
            else { const FVector2D Local = CireLanePath::ToLocal(E.Realm, E.Focus); E.Realm = 1 - E.Realm; E.Focus = CireLanePath::ToWorld(E.Realm, Local, 0.f); }
        }
    }

    // ---- panel: REMOVED list -------------------------------------------------------------------------------------------------
    CireUIStyle::Frame(Painter(), ListX, ListY, ListW, ListH, CireUIColors::Red, ECireFrame::Panel);
    CireUIStyle::Header(Painter(), ListX + 12, ListY + 10, ListW - 24, FString::Printf(TEXT("REMOVED  (%d)"), W.Doc.Set.Removed.Num()), CireUIColors::Gold, 10.5f);
    {
        const float RowH = 24.f, Top = ListY + 40;
        const int32 Visible = FMath::Max(1, static_cast<int32>((ListH - 60) / RowH));
        const TArray<FCireWorldEditEntry>& Rows = W.Doc.Set.Removed;
        if (Hit(ListX, Top, ListW, ListH - 50))
        {
            if (Pressed(EKeys::MouseScrollUp)) W.ListScroll = FMath::Max(0, W.ListScroll - 3);
            if (Pressed(EKeys::MouseScrollDown)) W.ListScroll += 3;
        }
        W.ListScroll = FMath::Clamp(W.ListScroll, 0, FMath::Max(0, Rows.Num() - Visible));
        if (Rows.Num() == 0)
            Wrapped(TEXT("Nothing removed. Hover a building (the whole building lights up), click to select, Shift-click to add, drag a box in map view (M) for many, then Delete."), ListX + 14, Top + 4, ListW - 28, 9.f, CireUIColors::Muted, 6);
        FString RestoreKey;
        for (int32 I = 0; I < Visible && W.ListScroll + I < Rows.Num(); ++I)
        {
            const FCireWorldEditEntry& R = Rows[W.ListScroll + I];
            const FString Key = R.Key();
            const float RY = Top + I * RowH;
            const bool bSel = W.Sel.Contains(Key), bOver = Hit(ListX + 8, RY, ListW - 16, RowH - 2);
            Painter().Rect(ListX + 8, RY, ListW - 16, RowH - 2, bSel ? FLinearColor(.3f, .1f, .08f, .8f) : bOver ? FLinearColor(1, 1, 1, .06f) : FLinearColor(0, 0, 0, .25f));
            Painter().Rect(ListX + 10, RY + 3, 4, RowH - 8, WERealmColor[FMath::Clamp(R.Realm, 0, 1)]);
            Label(WEShortLabel(R.Label).Left(22), ListX + 20, RY + 4, 9.f, bSel ? CireUIColors::BrightGold : CireUIColors::Parchment);
            Label(R.Kind.ToUpper(), ListX + ListW - 80 - TextWidth(R.Kind.ToUpper(), 7.5f), RY + 6, 7.5f, CireUIColors::Muted);
            const float BX = ListX + ListW - 74, BY = RY + 2, BWd = 60, BH = RowH - 6;
            const bool bOverBtn = Hit(BX, BY, BWd, BH);
            CireUIStyle::Button(Painter(), BX, BY, BWd, BH, TEXT("RESTORE"), bOverBtn ? ECireButtonState::Hover : ECireButtonState::Normal, CireUIColors::Teal, 7.5f);
            if (bOverBtn && Clicked) { Clicked = false; PlayUIFeedback(); RestoreKey = Key; continue; }
            Tip(WEShortLabel(R.Label), FString::Printf(TEXT("%s, %s. %s. %d pieces, ~%d draw calls. Click selects (its ghost); double-click flies there."), *R.Kind, *ML::RealmName(R.Realm), *WEMeters(R.Size), R.Actors, R.DrawCalls), ListX + 8, RY, ListW - 90, RowH - 2);
            if (bOver && Clicked)
            {
                Clicked = false; PlayUIFeedback();
                const bool bDouble = W.LastRowKey == Key && Now - W.LastRowClick < .4;
                if (bShift) { if (bSel) W.Sel.Remove(Key); else W.Sel.Add(Key); } else { W.Sel.Reset(); W.Sel.Add(Key); }
                if (bDouble || !E.bWalk) FlyTo(Key);
                W.LastRowKey = Key; W.LastRowClick = Now;
            }
        }
        if (!RestoreKey.IsEmpty()) Say(CireWorldEditor::RestoreKeys(W, {RestoreKey}));
        if (Rows.Num() > Visible) Label(FString::Printf(TEXT("%d-%d of %d  (wheel scrolls)"), W.ListScroll + 1, FMath::Min(Rows.Num(), W.ListScroll + Visible), Rows.Num()), ListX + 12, ListY + ListH - 18, 8.f, CireUIColors::Muted);
    }

    // ---- panel: inspector ----------------------------------------------------------------------------------------------------
    CireUIStyle::Frame(Painter(), InspX, InspY, InspW, InspH, CireUIColors::Red, ECireFrame::Panel);
    const bool bActiveSet = !W.Doc.Set.Name.IsEmpty() && S.Active == W.Doc.Set.Name;
    CireUIStyle::Header(Painter(), InspX + 12, InspY + 10, InspW - 24, FString::Printf(TEXT("WORLD EDIT  |  %s"), W.Doc.Set.Name.IsEmpty() ? TEXT("(unnamed draft)") : *W.Doc.Set.Name.Left(20)), CireUIColors::Gold, 10.5f);
    float Y = InspY + 36;
    const float IX = InspX + 14, IW = InspW - 28;
    Label(FString::Printf(TEXT("%s view  |  %s  |  %s"), E.bWalk ? TEXT("Walk") : TEXT("Map"), *ML::RealmName(E.Realm), bActiveSet ? TEXT("ACTIVE in matches") : S.Active.IsEmpty() ? TEXT("no set active") : *FString::Printf(TEXT("active: %s"), *S.Active)),
        IX, Y, 8.5f, bActiveSet ? CireUIColors::Teal : CireUIColors::Muted); Y += 18;
    if (CireNav::HasNavigation(World) && !CireNav::IsReady(World)) { Label(TEXT("navmesh rebuilding around the changes..."), IX, Y, 8.5f, CireUIColors::Orange); Y += 16; }
    // Selection.
    Painter().Rect(IX - 4, Y, IW + 8, 1, FLinearColor(1, 1, 1, .12f)); Y += 6;
    if (W.Sel.Num() > 0)
    {
        FBox All(ForceInit); int32 Pieces = 0, Calls = 0, Prot = 0, Warn = 0, Removed = 0;
        TMap<FString, int32> Kinds;
        FCireWorldEditGuard FirstGuard;
        for (const FString& K : W.Sel)
        {
            const FCireWorldEditEntry SE = EntryFor(K);
            FBox B; if (BoundsOf(K, B)) All += B;
            Pieces += SE.Actors; Calls += SE.DrawCalls; ++Kinds.FindOrAdd(SE.Kind);
            if (W.Doc.Set.Contains(K)) { ++Removed; continue; }
            const FCireWorldEditGuard G = CireWorldEdit::Guard(L, SE, S);
            Prot += G.Level == ECireWorldEditGuard::Protected ? 1 : 0; Warn += G.Level == ECireWorldEditGuard::Warn ? 1 : 0;
            if (G.Level > FirstGuard.Level) FirstGuard = G;
        }
        if (W.Sel.Num() == 1)
        {
            const FCireWorldEditEntry SE = EntryFor(W.Sel[0]);
            TextFx(WEShortLabel(SE.Label), IX, Y, 11.f, Removed ? WEGhost : CireUIColors::BrightGold, ECireFont::Bold, true); Y += 20;
            Label(FString::Printf(TEXT("%s  |  %s%s"), *SE.Kind, *ML::RealmName(SE.Realm), Removed ? TEXT("  |  REMOVED") : TEXT("")), IX, Y, 8.5f, CireUIColors::Muted); Y += 15;
            Label(SE.Id.Left(44), IX, Y, 7.5f, CireUIColors::Muted); Y += 15;
        }
        else
        {
            TArray<FString> Parts; for (const auto& P : Kinds) Parts.Add(FString::Printf(TEXT("%d %s"), P.Value, *P.Key.ToLower()));
            TextFx(FString::Printf(TEXT("%d PIECES SELECTED"), W.Sel.Num()), IX, Y, 11.f, CireUIColors::BrightGold, ECireFont::Bold, true); Y += 20;
            Wrapped(FString::Join(Parts, TEXT(", ")) + (Removed ? FString::Printf(TEXT("  (%d removed)"), Removed) : FString()), IX, Y, IW, 8.5f, CireUIColors::Muted, 2); Y += 18;
        }
        Label(FString::Printf(TEXT("SIZE  %s"), *WEMeters(All.IsValid ? All.GetSize() : FVector::ZeroVector)), IX, Y, 9.f, CireUIColors::Parchment); Y += 16;
        Label(FString::Printf(TEXT("%d pieces  |  ~%d draw calls"), Pieces, Calls), IX, Y, 9.f, CireUIColors::Parchment); Y += 18;
        if (Prot || Warn)
        {
            Wrapped(FString::Printf(TEXT("%s%s"), Prot ? *FString::Printf(TEXT("%d PROTECTED (stays): "), Prot) : TEXT(""), *FirstGuard.Why.Replace(TEXT("PROTECTED: "), TEXT(""))), IX, Y, IW, 8.5f, WEGuardColor(FirstGuard.Level), 3);
            Y += 40;
        }
        else if (Removed < W.Sel.Num()) { Label(TEXT("Free to remove: no marker depends on it."), IX, Y, 8.5f, CireUIColors::Teal); Y += 16; }
        auto Btn = [&](const FString& T, float BX, float BW2, bool bEnabled, FLinearColor Accent, const FString& Help)
        {
            const bool Over = bEnabled && Hit(BX, Y, BW2, 24);
            CireUIStyle::Button(Painter(), BX, Y, BW2, 24, T, !bEnabled ? ECireButtonState::Disabled : Over ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 8.5f);
            Tip(T, Help, BX, Y, BW2, 24);
            if (Over && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
            return false;
        };
        if (Btn(TEXT("REMOVE  Del"), IX, IW * .5f - 2, LiveSel.Num() > Prot, CireUIColors::Red, TEXT("Remove the selection (protected pieces stay)."))) DoRemove();
        if (Btn(TEXT("RESTORE  R"), IX + IW * .5f + 2, IW * .5f - 2, GhostSel.Num() > 0, CireUIColors::Teal, TEXT("Bring back the selected removed pieces."))) DoRestore();
        Y += 28;
        if (Btn(TEXT("DESELECT  Esc"), IX, IW, true, CireUIColors::Gold, TEXT("Clear the selection."))) W.Sel.Reset();
        Y += 30;
    }
    else
    {
        Wrapped(TEXT("Nothing selected. Hover a building: the whole building (every wall, roof and prop of it) lights up. Click selects, Shift-click adds, drag a box (map view, M) selects many. Delete removes; Ctrl+Z / Ctrl+Y undo and redo; H ghosts; Y mirror; V validate; Ctrl+S save."),
            IX, Y, IW, 9.f, CireUIColors::Parchment, 7);
        Y += 104;
    }
    // The set: what it saves.
    {
        Painter().Rect(IX - 4, Y, IW + 8, 1, FLinearColor(1, 1, 1, .12f)); Y += 6;
        const FCireWorldEditSavings Sv = CireWorldEdit::Savings(W.Doc.Set);
        TextFx(TEXT("THIS SET SAVES"), IX, Y, 9.f, CireUIColors::Gold, ECireFont::Bold, true); Y += 18;
        Label(FString::Printf(TEXT("%d pieces removed  |  ~%s actors  |  ~%s draw calls"), Sv.Units, *FText::AsNumber(Sv.Actors).ToString(), *FText::AsNumber(Sv.DrawCalls).ToString()), IX, Y, 9.f, Sv.Units ? CireUIColors::Teal : CireUIColors::Muted); Y += 16;
        int32 Realm[2] = {0, 0}; for (const FCireWorldEditEntry& R : W.Doc.Set.Removed) ++Realm[FMath::Clamp(R.Realm, 0, 1)];
        Label(FString::Printf(TEXT("DAYLIGHT %d  |  DARKNIGHT %d  |  mirror %s  |  ghosts %s"), Realm[0], Realm[1], W.bMirror ? TEXT("on") : TEXT("off"), W.bGhosts ? TEXT("on") : TEXT("off")), IX, Y, 8.f, CireUIColors::Muted); Y += 18;
    }
    // Validation.
    {
        Painter().Rect(IX - 4, Y, IW + 8, 1, FLinearColor(1, 1, 1, .12f)); Y += 6;
        int32 Errors = 0; for (const FCireLayoutIssue& I : W.Issues) Errors += I.bError ? 1 : 0;
        TextFx(!W.bValidated ? FString(TEXT("NOT VALIDATED  (V)")) : Errors == 0 ? FString::Printf(TEXT("VALID%s"), W.Issues.Num() ? *FString::Printf(TEXT("  (%d notes)"), W.Issues.Num()) : TEXT("")) : FString::Printf(TEXT("%d PROBLEMS"), Errors),
            IX, Y, 9.f, !W.bValidated ? CireUIColors::Muted : Errors == 0 ? CireUIColors::Teal : CireUIColors::Orange, ECireFont::Bold, true);
        Y += 18;
        const float Bottom = InspY + InspH - 70;
        for (int32 I = 0; I < W.Issues.Num() && Y < Bottom; ++I)
        {
            const FCireLayoutIssue& Issue = W.Issues[I];
            Painter().Rect(IX, Y + 2, 3, 20, Issue.bError ? WEProtC : CireUIColors::Orange);
            Wrapped(Issue.Message, IX + 8, Y, IW - 8, 8.5f, CireUIColors::Parchment, 2);
            Y += 28;
        }
    }
    // Status.
    {
        const float SY = InspY + InspH - 64;
        Painter().Rect(IX - 4, SY - 4, IW + 8, 1, FLinearColor(1, 1, 1, .12f));
        if (!E.Message.IsEmpty() && Now - E.MessageAt < 20) Wrapped(E.Message, IX, SY, IW, 8.5f, CireUIColors::Gold, 3);
        Label(FString::Printf(TEXT("%s  |  %d undo, %d redo"), W.SavedAt >= 0 ? *FString::Printf(TEXT("Draft autosaved %.0f s ago"), Now - W.SavedAt) : TEXT("Draft"), W.Doc.Undo.Num(), W.Doc.Redo.Num()), IX, InspY + InspH - 18, 8.f, CireUIColors::Muted);
    }
    // Save As / Load overlays.
    if (E.Naming == 3)
    {
        const float OW = 380, OH = 74, OX = ViewW * .5f - OW * .5f, OY = ViewH * .35f;
        LayoutUIRects.Add({OX, OY, OW, OH});
        CireUIStyle::Frame(Painter(), OX, OY, OW, OH, CireUIColors::Gold, ECireFrame::Panel);
        Label(TEXT("SAVE WORLD EDIT SET AS  (Enter saves + activates, Esc cancels)"), OX + 14, OY + 10, 8.5f, CireUIColors::Gold);
        Painter().Rect(OX + 14, OY + 34, OW - 28, 26, FLinearColor(0, 0, 0, .5f));
        Label(E.NameBuffer + (FMath::Fmod(Now, 1.) < .5 ? TEXT("_") : TEXT("")), OX + 20, OY + 39, 10.5f, CireUIColors::Parchment);
    }
    if (W.bLoadList)
    {
        const TArray<FString> Names = CireWorldEdit::ListNamed();
        const float OW = 340, RowH = 26, OH = 46 + FMath::Max(1, Names.Num()) * RowH, OX = ViewW * .5f - OW * .5f, OY = ViewH * .25f;
        LayoutUIRects.Add({OX, OY, OW, OH});
        CireUIStyle::Frame(Painter(), OX, OY, OW, OH, CireUIColors::Gold, ECireFrame::Panel);
        Label(TEXT("LOAD WORLD EDIT SET  (Undo brings the draft back)"), OX + 14, OY + 10, 8.5f, CireUIColors::Gold);
        if (Names.Num() == 0) Label(TEXT("No saved sets yet (SAVE AS)."), OX + 14, OY + 34, 9.f, CireUIColors::Muted);
        for (int32 I = 0; I < Names.Num(); ++I)
        {
            const float BY = OY + 32 + I * RowH;
            const bool Over = Hit(OX + 14, BY, OW - 28, RowH - 4);
            CireUIStyle::Button(Painter(), OX + 14, BY, OW - 28, RowH - 4, Names[I] + (Names[I] == S.Active ? TEXT("  (active)") : TEXT("")), Over ? ECireButtonState::Hover : ECireButtonState::Normal, CireUIColors::Gold, 8.5f);
            if (Over && Clicked)
            {
                Clicked = false; PlayUIFeedback();
                FCireWorldEditSet Set; FString Error;
                if (CireWorldEdit::Load(Set, CireWorldEdit::NamedPath(Names[I]), &Error)) { if (Set.Name.IsEmpty()) Set.Name = Names[I]; CireWorldEdit::ReplaceSet(W.Doc, Set, FString::Printf(TEXT("load %s"), *Names[I])); W.Sel.Reset(); Say(FString::Printf(TEXT("Loaded set %s (%d pieces). SAVE makes it active."), *Names[I], Set.Removed.Num())); }
                else Say(Error);
                W.bLoadList = false;
            }
        }
    }
    // Help strip.
    Label(E.bWalk ? TEXT("WORLD / WALK: hover = the whole building  |  click select  |  Shift-click add  |  Del remove  |  ^Z undo  ^Y redo  |  H ghosts  Y mirror  |  M map view  G realm  B layout")
                  : TEXT("WORLD / MAP: drag = box select  |  Shift adds  |  Del remove  |  ^Z / ^Y  |  WASD pan  Q/E rotate  wheel zoom  |  M walk  G realm  B layout"),
        BarX - 60, ViewH - 15, 8.f, CireUIColors::Muted);

    // ---- keep the world, the draft and the list in step ----------------------------------------------------------------------
    if (W.Doc.Revision != RevisionBefore) { W.bUnsaved = true; W.EditedAt = Now; W.bValidated = false; W.Sel.RemoveAll([&](const FString& K) { return !W.ByKey.Contains(K) && !W.Doc.Set.Contains(K); }); }
    CireWorldEditor::Sync(World, W);
    CireWorldEditor::Autosave(W, Now);
#endif
}
