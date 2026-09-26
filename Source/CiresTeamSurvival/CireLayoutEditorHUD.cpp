// dev-route-tools: the MAP LAYOUT EDITOR (-CireRouteEdit / RouteEditor.cmd; Docs/MapLayout.md).
// One generic setter tool over the data-driven marker table: each setter is an action-bar skill (bar 1 keys, so the
// player's own bindings apply) with its icon, colour and in-world gizmo. Place / select / remove / replace / move work
// the same for every setter; monster paths and play bounds chain points. Markers are realm-local and team-owned; a
// mirrored marker keeps its twin in the other team's realm. Panels: setter bar, marker list (team / type filters),
// inspector, validation. Walk view (the champion walks the town) or map view (top-down camera).
#include "CireHUD.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireLayoutEditorState.h"
#include "CireNav.h"
#include "CireNPCArchetypes.h"
#include "CireNPCCombat.h"
#include "CireRouteEditMode.h"
#include "CireRouteEditor.h"
#include "CireKeybindings.h"
#include "CireDeveloperTools.h"
#include "CireLayoutRuntime.h" // layout-wiring
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

namespace
{
namespace ML = CireMapLayout;
const FLinearColor Team1Color(.25f, .56f, 1.f, 1.f), Team2Color(.96f, .3f, .24f, 1.f), SharedColor(.9f, .78f, .42f, 1.f);
const FLinearColor Ink(0.f, 0.f, 0.f, .55f);
FLinearColor TeamColor(ECireMarkerOwner O) { return O == ECireMarkerOwner::Team1 ? Team1Color : O == ECireMarkerOwner::Team2 ? Team2Color : SharedColor; }
FLinearColor Faded(FLinearColor C, float A) { C.A *= A; return C; }
bool IsPointType(const FCireMarkerType* T) { return T && T->bPoints; }
/** Setter i's key: action bar 1 slots 1-6 then 9-12 (keys 1-6, 7-0 by default), then bar 2 slot 1 (Shift+1). */
FName SetterAction(int32 Index)
{
    if (Index < 6) return CireKeybindings::SlotAction(1, Index + 1);
    if (Index < 10) return CireKeybindings::SlotAction(1, Index + 3);
    if (Index < 22) return CireKeybindings::SlotAction(2, Index - 9);
    return NAME_None;
}
FVector2D FacingDir(float YawDeg) { const double R = FMath::DegreesToRadians(YawDeg); return FVector2D(FMath::Cos(R), FMath::Sin(R)); }
}

// ================================================================================================= actions
bool CireLayoutEditor::Edit(FCireLayoutEditorState& E, TFunctionRef<bool(FCireMapLayout&)> Change, double Now)
{
    FCireMapLayout Before = E.Layout;
    if (!Change(E.Layout)) { E.Layout = MoveTemp(Before); return false; }
    E.History.Add(MoveTemp(Before));
    if (E.History.Num() > 200) E.History.RemoveAt(0);
    E.bIssuesDirty = true; E.bNavChecked = false; E.bUnsaved = true; E.EditedAt = Now;
    return true;
}
bool CireLayoutEditor::Undo(FCireLayoutEditorState& E, double Now)
{
    if (E.History.Num() == 0) return false;
    E.Layout = E.History.Pop();
    if (E.Sel.IsSet() && !ML::Find(E.Layout, E.Sel.Id)) E.Sel.Reset();
    if (!E.ChainId.IsEmpty() && !ML::Find(E.Layout, E.ChainId)) E.ChainId.Reset();
    E.bIssuesDirty = true; E.bNavChecked = false; E.bUnsaved = true; E.EditedAt = Now;
    return true;
}
void CireLayoutEditor::Say(FCireLayoutEditorState& E, const FString& Message, double Now) { E.Message = Message; E.MessageAt = Now; }
void CireLayoutEditor::RunValidation(UWorld* World, FCireLayoutEditorState& E, bool bNav)
{
    E.WalkLength[0].Reset(); E.WalkLength[1].Reset();
    FCireLayoutChecks Checks;
    // layout-wiring: the runtime check on every validation: compile the layout exactly as a match loads it (over the route
    // file, the provisional default) and run the route rules the match enforces, so Validate flags what the game cannot use.
    Checks.Runtime = [World](const FCireMapLayout& Layout, TArray<FString>& Notes, FString& Error) { return CireLayoutEditor::CompileForRuntime(World, Layout, nullptr, Notes, Error); };
    if (bNav && World && CireNav::HasNavigation(World))
    {
        Checks.OnNavmesh = [World](int32 Realm, const FVector2D& Local)
        { FVector Out; return CireNav::Project(World, CireLanePath::ToWorld(Realm, Local, 60.f), Out, FVector(120, 120, 400), 40.f); };
        Checks.Walkable = [World](int32 Realm, const FVector2D& A, const FVector2D& B)
        {
            float Length = 0;
            const ECireRouteReach R = CireRouteEditor::Reach(World, CireLanePath::ToWorld(Realm, A, 60.f), CireLanePath::ToWorld(Realm, B, 60.f), Length);
            return R == ECireRouteReach::Direct || R == ECireRouteReach::Detour;
        };
        Checks.SignClear = [World](int32 Realm, const FVector2D& Local, float Height)
        {
            FCollisionQueryParams Params(TEXT("CireLayoutSign"), false);
            return !World->OverlapAnyTestByChannel(CireLanePath::ToWorld(Realm, Local, Height), FQuat::Identity, ECC_WorldStatic, FCollisionShape::MakeBox(FVector(12, 55, 35)), Params);
        };
    }
    E.Issues = ML::Validate(E.Layout, &Checks);
    E.bIssuesDirty = false; E.bNavChecked = bNav && Checks.OnNavmesh != nullptr;
    // Walk length per path and realm: navmesh path lengths when checked, straight segments otherwise.
    for (const FCireMapMarker& M : E.Layout.Markers)
    {
        if (M.Type != ML::MonsterPath) continue;
        const TArray<FVector2D> Walk = ML::WalkPolyline(E.Layout, M.Id);
        for (int32 Realm = 0; Realm < 2; ++Realm)
        {
            if (!ML::ShownInRealm(M, Realm)) continue;
            double Length = 0;
            for (int32 I = 0; I + 1 < Walk.Num(); ++I)
            {
                float Nav = 0;
                const bool bUseNav = E.bNavChecked && CireRouteEditor::Reach(World, CireLanePath::ToWorld(Realm, Walk[I], 60.f), CireLanePath::ToWorld(Realm, Walk[I + 1], 60.f), Nav) <= ECireRouteReach::Detour && Nav > 0;
                Length += bUseNav ? FMath::Max<double>(Nav, FVector2D::Distance(Walk[I], Walk[I + 1])) : FVector2D::Distance(Walk[I], Walk[I + 1]);
            }
            E.WalkLength[Realm].Add(M.Id, Length);
        }
    }
    if (World) E.ValidatedAt = World->GetRealTimeSeconds();
}
void CireLayoutEditor::StopPreview(FCireLayoutEditorState& E)
{
    for (FCireLayoutWalker& W : E.Walkers) if (W.Unit.IsValid()) W.Unit->Destroy();
    E.Walkers.Reset(); E.bPreview = false;
}
void CireLayoutEditor::StartPreview(UWorld* World, FCireLayoutEditorState& E)
{
    StopPreview(E);
    if (!World || World->GetNetMode() == NM_Client) { Say(E, TEXT("Preview needs the authoritative world (edit mode or standalone)."), World ? World->GetRealTimeSeconds() : 0); return; }
    E.PreviewSpeed = CireRouteEditor::MarchSpeed();
    const auto& Db = CireNPCArchetypes::Get();
    const FName Archetype = Db.WaveComposition.Num() > 0 ? Db.WaveComposition[0] : NAME_None;
    for (const FCireMapMarker& M : E.Layout.Markers)
    {
        if (M.Type != ML::MonsterPath) continue;
        const TArray<FVector2D> Walk = ML::WalkPolyline(E.Layout, M.Id);
        if (Walk.Num() < 2) continue;
        for (int32 Realm = 0; Realm < 2; ++Realm)
        {
            if (!ML::ShownInRealm(M, Realm)) continue;
            FCireLayoutWalker W; W.PathId = M.Id; W.Realm = Realm;
            // The navmesh path between each pair of points: what a marching unit really walks.
            for (int32 I = 0; I + 1 < Walk.Num(); ++I)
            {
                const FVector A = CireLanePath::ToWorld(Realm, Walk[I], 60.f), B = CireLanePath::ToWorld(Realm, Walk[I + 1], 60.f);
                FVector PA, PB; FCireNavPath Path;
                if (CireNav::Project(World, A, PA, FVector(150, 150, 400), 40.f) && CireNav::Project(World, B, PB, FVector(150, 150, 400), 40.f))
                    Path = CireNav::FindPath(World, PA, PB, 40.f, true);
                if (Path.bValid && Path.Points.Num() > 1) { for (int32 K = W.Points.Num() ? 1 : 0; K < Path.Points.Num(); ++K) W.Points.Add(Path.Points[K]); }
                else { if (W.Points.Num() == 0) W.Points.Add(A); W.Points.Add(B); }
            }
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            Params.ObjectFlags |= RF_Transient;
            ACireMonster* Unit = World->SpawnActor<ACireMonster>(ACireMonster::StaticClass(), W.Points[0] + FVector(0, 0, 60), FRotator::ZeroRotator, Params);
            if (!Unit) continue;
            Unit->Lane = Realm;
            if (!Archetype.IsNone()) CireNPCCombat::ConfigureArchetype(Unit, Archetype, 1);
            Unit->SetActorTickEnabled(false); // no AI: the preview steers it
            Unit->SetCanBeDamaged(false);
            Unit->MonsterName = FString::Printf(TEXT("Preview | %s"), *ML::DisplayLabel(E.Layout, M));
            Unit->BaseMoveSpeed = E.PreviewSpeed;
            if (Unit->GetCharacterMovement()) Unit->GetCharacterMovement()->MaxWalkSpeed = E.PreviewSpeed;
            W.Unit = Unit; W.Last = Unit->GetActorLocation();
            E.Walkers.Add(MoveTemp(W));
        }
    }
    E.bPreview = E.Walkers.Num() > 0;
    E.PreviewStarted = World->GetRealTimeSeconds();
    Say(E, E.bPreview ? FString::Printf(TEXT("Preview: %d monsters walk every path (P stops)."), E.Walkers.Num()) : FString(TEXT("Nothing to preview: place a monster spawn and chain a path from it.")), E.PreviewStarted);
}
void CireLayoutEditor::TickPreview(UWorld* World, FCireLayoutEditorState& E, float)
{
    if (!E.bPreview || !World) return;
    const double Now = World->GetRealTimeSeconds();
    bool bAny = false;
    for (FCireLayoutWalker& W : E.Walkers)
    {
        ACireMonster* U = W.Unit.Get();
        if (!U || W.Finished >= 0) { if (W.Finished >= 0 && Now - W.Finished > 3 && U) U->Destroy(); continue; }
        bAny = true;
        const FVector P = U->GetActorLocation();
        W.Walked += FVector::Dist2D(P, W.Last); W.Last = P;
        while (W.Next < W.Points.Num() && FVector::Dist2D(P, W.Points[W.Next]) < 90.) ++W.Next;
        if (W.Next >= W.Points.Num()) { W.Finished = Now; continue; }
        const FVector Dir = (W.Points[W.Next] - P).GetSafeNormal2D();
        U->AddMovementInput(Dir, 1.f);
    }
    if (!bAny && Now - E.PreviewStarted > 1) { for (FCireLayoutWalker& W : E.Walkers) if (W.Unit.IsValid() && Now - W.Finished > 3) W.Unit->Destroy(); }
}
bool CireLayoutEditor::CompileForRuntime(UWorld* World, const FCireMapLayout& Layout, FCireBattlefieldRoutes* OutRoutes, TArray<FString>& Notes, FString& Error)
{
    // The same base a match starts from: the route file (CastleTownRoutes.json in the town), else the live document.
    FCireBattlefieldRoutes Base;
    if (!CireLanePath::LoadFile(Base)) Base = CireLanePath::Get(World);
    FCireBattlefieldRoutes Compiled;
    if (!ML::CompileRoutes(Layout, Base, Compiled, Notes)) { Error = TEXT("a realm has no complete monster path (spawn -> path -> objective)"); return false; }
    if (!CireLanePath::Validate(Compiled, Error)) return false;
    // Every path, spawn and spot replicates to the clients in one float array; keep it inside the engine's array budget.
    TArray<float> Packed; CireLanePath::PackExtras(Compiled, Packed);
    if (Packed.Num() + 2 * 64 * 2 + 150 > 2000) { Error = FString::Printf(TEXT("too many path points for the network (%d values); remove points or paths"), Packed.Num()); return false; }
    if (OutRoutes) *OutRoutes = MoveTemp(Compiled);
    Error.Reset();
    return true;
}
bool CireLayoutEditor::Apply(UWorld* World, FCireLayoutEditorState& E, FString& Out)
{
    FString Error;
    TArray<FString> Parts;
    // layout-wiring: MapLayout.json is the one source of truth. Every match compiles it at startup over the route file
    // (CastleTownRoutes.json in the town: the provisional default, never overwritten by Apply).
    E.Layout.Map = ML::ActiveMap();
    if (!ML::Save(E.Layout, ML::ActivePath(), &Error)) { Out = Error; return false; }
    Parts.Add(TEXT("MapLayout.json saved"));
    if (ML::OfType(E.Layout, ML::Vendor).Num() > 0)
    {
        if (FFileHelper::SaveStringToFile(ML::VendorsJson(E.Layout), *ML::VendorsPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
            Parts.Add(FString::Printf(TEXT("TownVendors.json written, %d merchants re-placed"), CireLayoutRuntime::RespawnVendors(World)));
        else Parts.Add(TEXT("TownVendors.json could not be written"));
    }
    TArray<FString> Notes; FCireBattlefieldRoutes Routes;
    bool bLive = false;
    if (CompileForRuntime(World, E.Layout, &Routes, Notes, Error))
    {
        if (CireLanePath::ApplyLive(World, Routes, &Error))
        {
            bLive = true;
            Parts.Add(FString::Printf(TEXT("live: %d/%d paths, %d/%d spawns; the next match (or Alt+F5 in a match) runs it"), CireLanePath::PathCount(Routes, 0), CireLanePath::PathCount(Routes, 1),
                CireLanePath::SpawnSpots(Routes, 0).Num(), CireLanePath::SpawnSpots(Routes, 1).Num()));
        }
        else Parts.Add(FString::Printf(TEXT("not applied live: %s"), *Error));
    }
    else Parts.Add(FString::Printf(TEXT("the game cannot run it yet (%s); matches keep the route file until it validates"), *Error));
    for (const FString& N : Notes) Parts.Add(N);
    Out = FString::Join(Parts, TEXT(" | "));
    return bLive;
}
void CireLayoutEditor::Load(UWorld* World, FCireLayoutEditorState& E)
{
    FString Error;
    const double Now = World ? World->GetRealTimeSeconds() : 0;
    // layout-wiring: a layout belongs to one map (the town or the procedural town); another map's draft is left alone.
    if (ML::Load(E.Layout, ML::DraftPath(), &Error) && ML::MatchesActiveMap(E.Layout)) Say(E, TEXT("Restored your autosaved draft (Saved/MapLayoutDraft.json)."), Now);
    else if (ML::Load(E.Layout, ML::ActivePath(), &Error) && ML::MatchesActiveMap(E.Layout)) Say(E, TEXT("Loaded the active layout (Content/Data/MapLayout.json)."), Now);
    else { E.Layout = ML::FromRoutes(CireLanePath::Get(World)); E.Layout.Map = ML::ActiveMap(); Say(E, TEXT("Started from the current march route: spawn, path, objective and packs."), Now); }
    E.History.Reset(); E.bLoaded = true; E.bIssuesDirty = true;
}
void CireLayoutEditor::Autosave(FCireLayoutEditorState& E, double Now, bool bForce)
{
    if (!E.bUnsaved || E.bNoFiles || (!bForce && Now - E.EditedAt < .75)) return;
    if (ML::Save(E.Layout, ML::DraftPath())) { E.bUnsaved = false; E.SavedAt = Now; }
}

// ================================================================================================= HUD
bool ACireHUD::IsLayoutWalkView() const { return LayoutEditor.IsValid() && LayoutEditor->bWalk && LayoutEditor->Naming == 0; }

void ACireHUD::OpenLayoutEditor(bool bOpen)
{
#if !UE_BUILD_SHIPPING
    if (bOpen && !CireRouteEditMode::IsActive() && !CireDeveloperTools::CanEdit(GetWorld())) return;
    if (!LayoutEditor.IsValid()) LayoutEditor = MakeShared<FCireLayoutEditorState>();
    FCireLayoutEditorState& E = *LayoutEditor;
    if (bOpen == bLayoutEditor) return;
    bLayoutEditor = bOpen;
    UWorld* World = GetWorld();
    if (bOpen)
    {
        bSettings = false; bEditLayout = false; if (bRouteEditor) OpenRouteEditor(false);
        if (!E.bLoaded) CireLayoutEditor::Load(World, E);
        const ACireHero* Hero = PlayerOwner ? Cast<ACireHero>(PlayerOwner->GetPawn()) : nullptr;
        E.Realm = Hero ? FMath::Clamp(Hero->TeamId, 0, 1) : 0;
        E.Focus = Hero ? Hero->GetActorLocation() : CireLanePath::ToWorld(E.Realm, FVector2D(5000, 0));
    }
    else
    {
        CireLayoutEditor::StopPreview(E);
        CireLayoutEditor::Autosave(E, World ? World->GetRealTimeSeconds() : 0, true);
        if (PlayerOwner) PlayerOwner->SetViewTarget(E.PreviousView.IsValid() ? E.PreviousView.Get() : PlayerOwner->GetPawn());
        if (E.Camera.IsValid()) E.Camera->Destroy();
        E.Camera.Reset();
    }
#endif
}

bool ACireHUD::LayoutEditorEscape()
{
    if (!LayoutEditor.IsValid()) return false;
    FCireLayoutEditorState& E = *LayoutEditor;
    if (E.Naming != 0) { E.Naming = 0; return true; }
    if (E.bLoadList) { E.bLoadList = false; return true; }
    if (E.bReplace || !E.Armed.IsNone() || !E.ChainId.IsEmpty()) { E.bReplace = false; E.Armed = NAME_None; E.ChainId.Reset(); E.ChainFrom.Reset(); return true; }
    if (E.Sel.IsSet()) { E.Sel.Reset(); return true; }
    if (!CireRouteEditMode::IsActive()) { OpenLayoutEditor(false); return true; }
    return true; // edit mode: the editor is the whole mode
}

void ACireHUD::TickLayoutEditor()
{
#if !UE_BUILD_SHIPPING
    if (!bLayoutEditor || !LayoutEditor.IsValid() || !Canvas || !PlayerOwner) return;
    FCireLayoutEditorState& E = *LayoutEditor;
    UWorld* World = GetWorld();
    FCireMapLayout& L = E.Layout;
    const double Now = World->GetRealTimeSeconds();
    const float Dt = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, .1f);
    ACireHero* Hero = Cast<ACireHero>(PlayerOwner->GetPawn());
    const FCireKeybindings& Keys = UISettings.Keybindings;
    const TArray<FCireMarkerType>& Types = ML::Types();
    LayoutUIRects.Reset();
    ResetTransform();
    auto Pressed = [&](FKey K) { return PlayerOwner->WasInputKeyJustPressed(K); };
    const bool bCtrl = PlayerOwner->IsInputKeyDown(EKeys::LeftControl) || PlayerOwner->IsInputKeyDown(EKeys::RightControl);
    const bool bShift = PlayerOwner->IsInputKeyDown(EKeys::LeftShift) || PlayerOwner->IsInputKeyDown(EKeys::RightShift);
    auto Say = [&](const FString& Text) { CireLayoutEditor::Say(E, Text, Now); };
    auto Edit = [&](TFunctionRef<bool(FCireMapLayout&)> Change) { return CireLayoutEditor::Edit(E, Change, Now); };

    // ---- view: walk (the champion) or map (top-down camera) -----------------------------------------------------
    if (Hero && E.bWalk) E.Realm = FMath::Clamp(Hero->TeamId, 0, 1);
    auto EnsureCamera = [&]()
    {
        if (E.Camera.IsValid()) return;
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params))
        { Camera->GetCameraComponent()->SetFieldOfView(55.f); Camera->GetCameraComponent()->bConstrainAspectRatio = false; E.Camera = Camera; }
    };
    if (!E.bWalk)
    {
        EnsureCamera();
        const bool bTyping = E.Naming != 0;
        auto Down = [&](FKey A, FKey B) { return !bTyping && !bSettings && (PlayerOwner->IsInputKeyDown(A) || PlayerOwner->IsInputKeyDown(B)); };
        const FRotator Flat(0, E.Yaw, 0);
        const FVector Forward = Flat.Vector(), Right = FRotationMatrix(Flat).GetUnitAxis(EAxis::Y);
        FVector Move = FVector::ZeroVector;
        if (Down(EKeys::W, EKeys::Up)) Move += Forward;
        if (Down(EKeys::S, EKeys::Down)) Move -= Forward;
        if (Down(EKeys::D, EKeys::Right)) Move += Right;
        if (Down(EKeys::A, EKeys::Left)) Move -= Right;
        E.Focus += Move.GetSafeNormal() * E.Distance * .9f * Dt;
        if (Down(EKeys::Q, EKeys::PageUp)) E.Yaw -= 70.f * Dt;
        if (Down(EKeys::E, EKeys::PageDown)) E.Yaw += 70.f * Dt;
        if (Down(EKeys::Home, EKeys::Home)) E.Pitch = FMath::Clamp(E.Pitch - 40.f * Dt, -89.f, -25.f);
        if (Down(EKeys::End, EKeys::End)) E.Pitch = FMath::Clamp(E.Pitch + 40.f * Dt, -89.f, -25.f);
        if (E.Camera.IsValid())
        {
            const FRotator View(E.Pitch, E.Yaw, 0);
            E.Camera->SetActorLocationAndRotation(E.Focus - View.Vector() * E.Distance, View);
            if (PlayerOwner->GetViewTarget() != E.Camera.Get()) { if (!E.PreviousView.IsValid()) E.PreviousView = PlayerOwner->GetViewTarget(); PlayerOwner->SetViewTarget(E.Camera.Get()); }
        }
    }
    else if (E.Camera.IsValid() && PlayerOwner->GetViewTarget() == E.Camera.Get()) PlayerOwner->SetViewTarget(Hero ? static_cast<AActor*>(Hero) : E.PreviousView.Get());

    // ---- projection and ground picking ----------------------------------------------------------------------------
    auto Project = [&](const FVector& W, FVector2D& Out)
    {
        const FVector S = Canvas->Project(W);
        if (S.Z <= 0.f) return false;
        Out = FVector2D(S.X / Scale, S.Y / Scale);
        return Out.X > -300 && Out.Y > -300 && Out.X < ViewW + 300 && Out.Y < ViewH + 300;
    };
    auto Seg = [&](const FVector& A, const FVector& B, FLinearColor C, float Width)
    { FVector2D PA, PB; if (Project(A, PA) && Project(B, PB)) Line(PA.X, PA.Y, PB.X, PB.Y, C, Width); };
    auto Ring = [&](const FVector& C, float R, FLinearColor Color, float Width, int32 Sides = 40)
    {
        for (int32 K = 0; K < Sides; ++K)
        {
            const double A0 = 2 * PI * K / Sides, A1 = 2 * PI * (K + 1) / Sides;
            Seg(C + FVector(FMath::Cos(A0) * R, FMath::Sin(A0) * R, 0), C + FVector(FMath::Cos(A1) * R, FMath::Sin(A1) * R, 0), Color, Width);
        }
    };
    auto Arrow = [&](const FVector& From, float YawDeg, float Length, FLinearColor C, float Width)
    {
        const FVector2D D = FacingDir(YawDeg);
        const FVector Tip = From + FVector(D.X, D.Y, 0) * Length;
        Seg(From, Tip, C, Width);
        for (const float Side : {150.f, -150.f}) { const FVector2D H = FacingDir(YawDeg + Side); Seg(Tip, Tip + FVector(H.X, H.Y, 0) * Length * .3f, C, Width); }
    };
    auto Ground = [&](FVector& Out)
    {
        if (E.bDebugGround) { Out = E.DebugGround; return true; }
        FVector Origin, Dir;
        if (MX < 0 || !PlayerOwner->DeprojectScreenPositionToWorld(MX * Scale, MY * Scale, Origin, Dir)) return false;
        FHitResult HitResult; FCollisionQueryParams Params(TEXT("CireLayoutPick"), false, Hero);
        for (const FCireLayoutWalker& W : E.Walkers) if (W.Unit.IsValid()) Params.AddIgnoredActor(W.Unit.Get());
        if (World->LineTraceSingleByChannel(HitResult, Origin, Origin + Dir * 60000.f, ECC_Visibility, Params)) { Out = HitResult.ImpactPoint; return true; }
        if (FMath::Abs(Dir.Z) < 1.e-3) return false;
        const double T = -Origin.Z / Dir.Z; if (T <= 0) return false;
        Out = Origin + Dir * T; return true;
    };
    auto RealmAt = [&](const FVector& P)
    {
        for (int32 R = 0; R < 2; ++R) if (CireLanePath::Contains(World, R, P)) return R;
        return E.Realm;
    };
    FVector Cursor; const bool bCursor = Ground(Cursor);
    const int32 CursorRealm = bCursor ? RealmAt(Cursor) : E.Realm;
    const FVector2D CursorLocal = bCursor ? CireLanePath::ToLocal(CursorRealm, Cursor) : FVector2D::ZeroVector;
    const int32 FeetRealm = Hero ? FMath::Clamp(Hero->TeamId, 0, 1) : E.Realm;
    const FVector2D FeetLocal = Hero ? CireLanePath::ToLocal(FeetRealm, Hero->GetActorLocation()) : CursorLocal;
    const float ViewYaw = E.bWalk ? PlayerOwner->GetControlRotation().Yaw : E.Yaw;

    // ---- panels (rects first so world picking ignores the pointer over them) -------------------------------------
    const float BarSlot = 44.f, BarGap = 6.f;
    const int32 SetterCount = FMath::Min(Types.Num(), 22);
    const float BarW = SetterCount * (BarSlot + BarGap) - BarGap, BarX = ViewW * .5f - BarW * .5f, BarY = ViewH - BarSlot - 22.f;
    const float CmdY = BarY - 34.f, CmdH = 24.f;
    const float ListX = 12.f, ListY = 12.f, ListW = 300.f, ListH = CmdY - 24.f;
    const float InspW = 318.f, InspX = ViewW - InspW - 12.f, InspY = 12.f, InspH = CmdY - 24.f;
    LayoutUIRects.Add({BarX - 10, BarY - 8, BarW + 20, BarSlot + 30});
    LayoutUIRects.Add({ListX, ListY, ListW, ListH});
    LayoutUIRects.Add({InspX, InspY, InspW, InspH});
    const float CmdW = 12 * 80.f + 11 * 4.f, CmdX = ViewW * .5f - CmdW * .5f;
    LayoutUIRects.Add({CmdX, CmdY, CmdW, CmdH});
    bool bOverUI = false;
    for (const FCireUIRect& R : LayoutUIRects) bOverUI |= MX >= R.X && MX <= R.X + R.W && MY >= R.Y && MY <= R.Y + R.H;

    // ---- world gizmos ----------------------------------------------------------------------------------------------
    E.Hover.Reset();
    float HoverDistance = 18.f;
    auto Consider = [&](const FVector& W, const FString& Id, int32 Point, ECireVendorPart Part)
    {
        FVector2D S;
        if (bOverUI || !Project(W, S)) return;
        const float D = FVector2D::Distance(S, FVector2D(MX, MY));
        if (D < HoverDistance) { HoverDistance = D; E.Hover.Id = Id; E.Hover.Point = Point; E.Hover.Part = Part; }
    };
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        const int32 Realm = Pass == 0 ? 1 - E.Realm : E.Realm; // the viewed realm last (on top)
        const float Fade = Realm == E.Realm ? 1.f : .45f;
        for (const FCireMapMarker& M : L.Markers)
        {
            if (!ML::ShownInRealm(M, Realm)) continue;
            const FCireMarkerType* T = ML::FindType(M.Type);
            if (!T) continue;
            if (E.TeamFilter >= 0 && static_cast<int32>(M.Owner) != E.TeamFilter) continue;
            const bool bSel = E.Sel.Id == M.Id, bHover = E.Hover.Id == M.Id;
            const FLinearColor TeamC = Faded(TeamColor(M.Owner), Fade), TypeC = Faded(T->Color, Fade);
            const FVector Base = CireLanePath::ToWorld(Realm, M.Position, 6.f);
            const float Width = bSel ? 3.5f : 2.f;
            FString Caption = ML::DisplayLabel(L, M);
            FVector LabelAt = Base + FVector(0, 0, 200);
            switch (T->Gizmo)
            {
            case ECireGizmo::Path:
            case ECireGizmo::Polygon:
            {
                const TArray<FVector2D>& P = M.Points;
                // The link from the monster spawn to the first point (dashed).
                if (M.Type == ML::MonsterPath && !M.From.IsEmpty())
                    if (const FCireMapMarker* Spawn = ML::Find(L, M.From))
                        if (P.Num() > 0 && ML::ShownInRealm(*Spawn, Realm))
                        {
                            const FVector A = CireLanePath::ToWorld(Realm, Spawn->Position, 8.f), B = CireLanePath::ToWorld(Realm, P[0], 8.f);
                            for (int32 K = 0; K < 10; K += 2) Seg(FMath::Lerp(A, B, K / 10.f), FMath::Lerp(A, B, (K + 1) / 10.f), TypeC, 1.5f);
                        }
                const int32 Segs = T->Gizmo == ECireGizmo::Polygon && P.Num() >= 3 ? P.Num() : P.Num() - 1;
                for (int32 I = 0; I < Segs; ++I)
                {
                    const FVector A = CireLanePath::ToWorld(Realm, P[I], 8.f), B = CireLanePath::ToWorld(Realm, P[(I + 1) % P.Num()], 8.f);
                    Seg(A, B, TeamC, Width + 1.f); Seg(A, B, TypeC, Width - .5f);
                    if (M.Type == ML::MonsterPath) // direction chevrons
                    {
                        const FVector Mid = (A + B) * .5f; const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(B.Y - A.Y, B.X - A.X));
                        for (const float Side : {150.f, -150.f}) { const FVector2D H = FacingDir(Yaw + Side); Seg(Mid, Mid + FVector(H.X, H.Y, 0) * 110.f, TypeC, 2.5f); }
                    }
                }
                for (int32 I = 0; I < P.Num(); ++I)
                {
                    const FVector W = CireLanePath::ToWorld(Realm, P[I], 8.f);
                    FVector2D S;
                    if (Project(W, S))
                    {
                        const bool bPointSel = bSel && E.Sel.Point == I;
                        Disc(S.X, S.Y, bPointSel ? 7.f : 4.5f, bPointSel ? CireUIColors::Parchment : TeamC);
                        if (Realm == E.Realm && (bSel || I == 0 || I == P.Num() - 1)) TextFx(FString::FromInt(I + 1), S.X + 6, S.Y - 6, 7.5f, CireUIColors::Parchment, ECireFont::Bold, true);
                    }
                    if (Realm == E.Realm) Consider(W, M.Id, I, ECireVendorPart::Npc);
                }
                if (M.Type == ML::MonsterPath && P.Num() > 0)
                {
                    const double* Walk = E.WalkLength[Realm].Find(M.Id);
                    const double Length = Walk ? *Walk : ML::PolylineLength(ML::WalkPolyline(L, M.Id));
                    Caption += FString::Printf(TEXT("  %.0f m  %s%s"), Length / 100., *CireRouteEditor::FormatWalkTime(Length / FMath::Max(1.f, E.PreviewSpeed)),
                        !M.MergeInto.IsEmpty() ? TEXT("  merges") : ML::PathClosed(L, M.Id) ? TEXT("") : TEXT("  (open)"));
                }
                LabelAt = CireLanePath::ToWorld(Realm, P.Num() ? P[0] : M.Position, 160.f);
                break;
            }
            case ECireGizmo::Ring:
            case ECireGizmo::Zone:
            {
                const float R = FMath::Max(60.f, M.Radius);
                Ring(Base, R, TeamC, Width); Ring(Base, R * .93f, TypeC, 1.5f);
                if (T->Gizmo == ECireGizmo::Zone)
                    for (const float A : {45.f, 135.f}) { const FVector2D D = FacingDir(A); Seg(Base - FVector(D.X, D.Y, 0) * R, Base + FVector(D.X, D.Y, 0) * R, TypeC, 1.5f); }
                Seg(Base, Base + FVector(0, 0, 140), TypeC, 2.f);
                LabelAt = Base + FVector(0, 0, 170);
                if (Realm == E.Realm) Consider(Base, M.Id, INDEX_NONE, ECireVendorPart::Npc);
                break;
            }
            default: // pillar
            {
                const FVector Top = Base + FVector(0, 0, 240);
                Ring(Base, FMath::Max(60.f, M.Radius), TeamC, Width, 28);
                Seg(Base, Top, TeamC, 4.f); Seg(Base, Top, TypeC, 2.f);
                FVector2D S; if (Project(Top, S)) { Disc(S.X, S.Y, 6.f, TypeC); Circle(S.X, S.Y, 7.f, TeamC, 1.5f); }
                LabelAt = Top + FVector(0, 0, 40);
                if (Realm == E.Realm) Consider(Base, M.Id, INDEX_NONE, ECireVendorPart::Npc);
                break;
            }
            }
            if (T->bFacing) Arrow(Base + FVector(0, 0, 4), M.Yaw, 190.f, TypeC, bSel ? 3.f : 2.f);
            if (M.Type == ML::Vendor)
            {
                // NPC silhouette, the sign on its post and the stall footprint: a live preview of the vendor group.
                FVector2D Feet, Head;
                if (Project(Base, Feet) && Project(Base + FVector(0, 0, 175), Head))
                {
                    const float H = FMath::Max(8.f, static_cast<float>(Feet.Y - Head.Y));
                    Line(Feet.X, Feet.Y, Head.X, Head.Y + H * .18f, TypeC, 4.f);
                    Disc(Head.X, Head.Y + H * .1f, FMath::Max(3.f, H * .1f), TypeC);
                    Line(Head.X - H * .18f, Head.Y + H * .35f, Head.X + H * .18f, Head.Y + H * .35f, TypeC, 3.f);
                }
                const FVector SignFoot = CireLanePath::ToWorld(Realm, M.SignPos, 4.f), SignTop = CireLanePath::ToWorld(Realm, M.SignPos, M.SignHeight);
                Seg(SignFoot, SignTop + FVector(0, 0, 40), Faded(CireUIColors::Parchment, Fade * .8f), 2.f);
                const FVector2D SR = FacingDir(M.SignYaw + 90.f);
                const FVector Across(SR.X * 60.f, SR.Y * 60.f, 0);
                const FVector C0 = SignTop - Across + FVector(0, 0, 30), C1 = SignTop + Across + FVector(0, 0, 30), C2 = SignTop + Across - FVector(0, 0, 30), C3 = SignTop - Across - FVector(0, 0, 30);
                const bool bSignSel = bSel && E.Sel.Part == ECireVendorPart::Sign, bStallSel = bSel && E.Sel.Part == ECireVendorPart::Stall;
                const FLinearColor SignC = bSignSel ? CireUIColors::Parchment : TypeC;
                Seg(C0, C1, SignC, 2.5f); Seg(C1, C2, SignC, 2.5f); Seg(C2, C3, SignC, 2.5f); Seg(C3, C0, SignC, 2.5f);
                Arrow(SignTop - FVector(0, 0, 45), M.SignYaw, 80.f, SignC, 1.5f);
                const TArray<FVector2D> Stall = ML::StallCorners(M);
                const FLinearColor StallC = bStallSel ? CireUIColors::Parchment : TypeC;
                for (int32 K = 0; K < 4; ++K) Seg(CireLanePath::ToWorld(Realm, Stall[K], 6.f), CireLanePath::ToWorld(Realm, Stall[(K + 1) % 4], 6.f), StallC, 2.5f);
                Seg(CireLanePath::ToWorld(Realm, Stall[0], 6.f), CireLanePath::ToWorld(Realm, Stall[2], 6.f), Faded(StallC, .5f), 1.f);
                Arrow(CireLanePath::ToWorld(Realm, M.StallPos, 8.f), M.StallYaw, 90.f, StallC, 1.5f);
                if (Realm == E.Realm)
                {
                    Consider(SignTop, M.Id, INDEX_NONE, ECireVendorPart::Sign);
                    Consider(CireLanePath::ToWorld(Realm, M.StallPos, 6.f), M.Id, INDEX_NONE, ECireVendorPart::Stall);
                }
                FVector2D S; if (Project(SignTop + FVector(0, 0, 48), S)) TextFx(TEXT("SIGN"), S.X - 12, S.Y - 12, 7.f, SignC, ECireFont::Bold, true);
                if (Project(CireLanePath::ToWorld(Realm, M.StallPos, 10.f), S)) TextFx(TEXT("STALL"), S.X - 14, S.Y + 4, 7.f, StallC, ECireFont::Bold, true);
            }
            FVector2D S;
            if (Project(LabelAt, S))
            {
                const FString Tag = M.bMirror && M.Owner != ECireMarkerOwner::Shared ? Caption + TEXT("  (mirrored)") : Caption;
                const float W = TextWidth(Tag, 9.f);
                Painter().Rect(S.X - W * .5f - 5, S.Y - 3, W + 10, 16, Faded(Ink, Fade));
                Painter().Rect(S.X - W * .5f - 5, S.Y - 3, 3, 16, TeamC);
                TextFx(Tag, S.X - W * .5f, S.Y - 2, 9.f, bSel ? CireUIColors::BrightGold : Faded(CireUIColors::Parchment, Fade), ECireFont::Bold, true);
            }
            if (bSel || bHover)
            {
                FVector2D B; if (Project(Base, B)) Circle(B.X, B.Y, bSel ? 16.f + 2.f * FMath::Sin(Now * 6.) : 13.f, bSel ? CireUIColors::Parchment : Faded(CireUIColors::Parchment, .6f), 1.8f);
            }
        }
    }
    // Preview monsters: a progress tag over each.
    CireLayoutEditor::TickPreview(World, E, Dt);
    for (const FCireLayoutWalker& W : E.Walkers)
    {
        FVector2D S;
        if (!W.Unit.IsValid() || !Project(W.Unit->GetActorLocation() + FVector(0, 0, 230), S)) continue;
        const double Time = W.Finished >= 0 ? W.Finished - E.PreviewStarted : Now - E.PreviewStarted;
        const FString Tag = FString::Printf(TEXT("%s  %.0f m  %s%s"), *ML::RealmName(W.Realm), W.Walked / 100.f, *CireRouteEditor::FormatWalkTime(Time), W.Finished >= 0 ? TEXT("  ARRIVED") : TEXT(""));
        TextFx(Tag, S.X - TextWidth(Tag, 8.5f) * .5f, S.Y, 8.5f, W.Finished >= 0 ? CireUIColors::Teal : CireUIColors::Orange, ECireFont::Bold, true);
    }

    // ---- the armed setter's reticle at the cursor -----------------------------------------------------------------
    const FCireMarkerType* ArmedType = E.Armed.IsNone() ? nullptr : ML::FindType(E.Armed);
    auto RadiusFor = [&](const FCireMarkerType* T) { const float* R = E.NextRadius.Find(T->Id); return R ? *R : T->DefaultRadius; };
    if (bCursor && !bOverUI && (ArmedType || E.bReplace))
    {
        const FVector At = CireLanePath::ToWorld(CursorRealm, CursorLocal, 8.f);
        const FCireMapMarker* Moving = E.bReplace ? ML::Find(L, E.Sel.Id) : nullptr;
        const FCireMarkerType* T = Moving ? ML::FindType(Moving->Type) : ArmedType;
        const FLinearColor C = T ? T->Color : CireUIColors::Gold;
        const float R = Moving && Moving->Radius > 0 ? Moving->Radius : T && T->bRadius ? RadiusFor(T) : 80.f;
        Ring(At, R, C, 2.5f); Ring(At, R * .5f, Faded(C, .5f), 1.f, 24);
        if (T && T->bFacing) Arrow(At, ViewYaw, 190.f, C, 2.f);
        if (T && IsPointType(T) && !E.ChainId.IsEmpty())
            if (const FCireMapMarker* Chain = ML::Find(L, E.ChainId)) if (Chain->Points.Num() > 0)
                Seg(CireLanePath::ToWorld(CursorRealm, Chain->Points.Last(), 8.f), At, Faded(C, .7f), 1.5f);
        FVector2D S;
        if (Project(At, S))
        {
            const ECireMarkerOwner PlaceOwner = T && T->DefaultOwner == ECireMarkerOwner::Shared ? ECireMarkerOwner::Shared : ML::TeamOfRealm(CursorRealm);
            FString What = Moving ? FString::Printf(TEXT("REPLACE %s"), *ML::DisplayLabel(L, *Moving)) :
                FString::Printf(TEXT("%s %s"), *ML::TeamTag(PlaceOwner), T ? *T->Name.ToUpper() : TEXT(""));
            if (!Moving && T && T->Id == ML::ChallengePack) What += FString::Printf(TEXT("  %d  Tier %d  r %.1f m"), ML::OfType(L, ML::ChallengePack, PlaceOwner).Num() + 1, E.NextTier, R / 100.f);
            TextFx(What + TEXT("   click: place   Esc: cancel"), S.X + 18, S.Y + 10, 9.f, CireUIColors::Parchment, ECireFont::Bold, true);
        }
    }

    // ---- editing operations --------------------------------------------------------------------------------------
    auto Select = [&](const FCireLayoutPick& Pick) { E.Sel = Pick; E.bReplace = false; };
    auto PlaceAt = [&](const FCireMarkerType* T, int32 Realm, const FVector2D& Local, float Yaw)
    {
        const ECireMarkerOwner PlaceOwner = T->DefaultOwner == ECireMarkerOwner::Shared ? ECireMarkerOwner::Shared : ML::TeamOfRealm(Realm);
        if (IsPointType(T))
        {
            FString Id;
            const FString From = !E.ChainFrom.IsEmpty() ? E.ChainFrom : (E.Sel.IsSet() && ML::Find(L, E.Sel.Id) && ML::Find(L, E.Sel.Id)->Type == ML::MonsterSpawn ? E.Sel.Id : FString());
            if (Edit([&](FCireMapLayout& X) { Id = ML::ChainPoint(X, T->Id, E.ChainId, Local, PlaceOwner, From); return !Id.IsEmpty(); }))
            {
                E.ChainId = Id; E.ChainFrom.Reset();
                const FCireMapMarker* P = ML::Find(L, Id);
                Select({Id, P ? P->Points.Num() - 1 : INDEX_NONE, ECireVendorPart::Npc});
                if (T->Id == ML::MonsterPath && ML::PathClosed(L, Id))
                {
                    Say(P && !P->MergeInto.IsEmpty() ? TEXT("Path merged into another path: finished. The next press starts a new path.") : TEXT("Path reached the objective: finished. The next press starts a new path."));
                    E.ChainId.Reset();
                }
                else Say(FString::Printf(TEXT("%s: point %d chained."), P ? *ML::DisplayLabel(L, *P) : TEXT("Path"), P ? P->Points.Num() : 0));
            }
            else Say(TEXT("That path is full (64 points)."));
            return;
        }
        FString Id;
        const float Radius = RadiusFor(T);
        if (Edit([&](FCireMapLayout& X)
            {
                Id = ML::Place(X, T->Id, Local, PlaceOwner, Yaw, true);
                if (Id.IsEmpty()) return false;
                if (T->bRadius) ML::SetRadius(X, Id, Radius);
                if (T->bTier) ML::SetTier(X, Id, E.NextTier);
                return true;
            }))
        {
            Select({Id, INDEX_NONE, ECireVendorPart::Npc});
            const FCireMapMarker* M = ML::Find(L, Id);
            Say(FString::Printf(TEXT("Placed %s%s."), M ? *ML::DisplayLabel(L, *M) : TEXT(""), M && M->bMirror ? TEXT(" and its mirrored twin") : TEXT("")));
        }
        else Say(FString::Printf(TEXT("No more %s markers for this team."), *T->Name));
    };
    auto ReplaceAt = [&](int32 Realm, const FVector2D& Local)
    {
        const FCireLayoutPick Pick = E.Sel;
        if (!Pick.IsSet()) return;
        const bool bOk = Edit([&](FCireMapLayout& X)
        {
            if (Pick.Point != INDEX_NONE) return ML::MovePoint(X, Pick.Id, Pick.Point, Local);
            return ML::MovePart(X, Pick.Id, Pick.Part, Local);
        });
        E.bReplace = false;
        const FCireMapMarker* M = ML::Find(L, Pick.Id);
        Say(bOk && M ? FString::Printf(TEXT("Moved %s%s."), *ML::DisplayLabel(L, *M), Pick.Point != INDEX_NONE ? *FString::Printf(TEXT(" point %d"), Pick.Point + 1) :
            Pick.Part == ECireVendorPart::Sign ? TEXT(" sign") : Pick.Part == ECireVendorPart::Stall ? TEXT(" stall") : TEXT("")) : FString(TEXT("Nothing moved.")));
        (void)Realm;
    };
    auto RemoveSelection = [&]()
    {
        const FCireLayoutPick Pick = E.Sel;
        const FCireMapMarker* M = ML::Find(L, Pick.Id);
        if (!M) return;
        const FString Name = ML::DisplayLabel(L, *M);
        if (Pick.Point != INDEX_NONE)
        {
            if (Edit([&](FCireMapLayout& X) { return ML::RemovePoint(X, Pick.Id, Pick.Point); }))
            {
                const FCireMapMarker* Left = ML::Find(L, Pick.Id);
                E.Sel.Point = Left ? FMath::Clamp(Pick.Point - 1, 0, Left->Points.Num() - 1) : INDEX_NONE;
                if (!Left) E.Sel.Reset();
                Say(FString::Printf(TEXT("Removed point %d of %s: its neighbours re-chain."), Pick.Point + 1, *Name));
            }
            return;
        }
        if (Pick.Part != ECireVendorPart::Npc)
        {
            if (Edit([&](FCireMapLayout& X) { return ML::ResetPart(X, Pick.Id, Pick.Part); }))
                Say(FString::Printf(TEXT("%s: the %s went back to its default spot."), *Name, Pick.Part == ECireVendorPart::Sign ? TEXT("sign") : TEXT("stall")));
            return;
        }
        if (Edit([&](FCireMapLayout& X) { return ML::Remove(X, Pick.Id); }))
        {
            E.Sel.Reset(); if (E.ChainId == Pick.Id) E.ChainId.Reset();
            Say(FString::Printf(TEXT("Removed %s (and its mirrored twin); later markers renumber."), *Name));
        }
    };
    auto FlyTo = [&](const FCireMapMarker& M)
    {
        const int32 Realm = M.Owner == ECireMarkerOwner::Shared ? E.Realm : ML::RealmOf(M.Owner);
        const FVector2D At = M.Points.Num() > 0 ? M.Points[FMath::Clamp(E.Sel.Point, 0, M.Points.Num() - 1)] : M.Position;
        if (E.bWalk && Hero) CireRouteEditMode::TeleportTo(Hero, Realm, At - FacingDir(ViewYaw) * 350., ViewYaw);
        else { E.Realm = Realm; E.Focus = CireLanePath::ToWorld(Realm, At, 0.f); }
    };
    auto SelectedMarker = [&]() { return E.Sel.IsSet() ? ML::Find(L, E.Sel.Id) : nullptr; };
    auto Rotate = [&](float Delta)
    {
        const FCireLayoutPick Pick = E.Sel; const FCireMapMarker* M = SelectedMarker();
        if (!M) return;
        const float Current = Pick.Part == ECireVendorPart::Sign ? M->SignYaw : Pick.Part == ECireVendorPart::Stall ? M->StallYaw : M->Yaw;
        Edit([&](FCireMapLayout& X) { return ML::SetPartYaw(X, Pick.Id, Pick.Part, Current + Delta); });
    };
    auto StepRadius = [&](float Delta)
    {
        const FCireMapMarker* M = SelectedMarker();
        const FCireMarkerType* T = M ? ML::FindType(M->Type) : ArmedType;
        if (!T || !T->bRadius) return;
        if (M) { const FString Id = M->Id; const float R = M->Radius + Delta; Edit([&](FCireMapLayout& X) { return ML::SetRadius(X, Id, R); }); }
        else E.NextRadius.Add(T->Id, FMath::Clamp(RadiusFor(T) + Delta, 50.f, 5000.f));
    };
    auto StepTier = [&](int32 Delta)
    {
        const FCireMapMarker* M = SelectedMarker();
        if (M && M->Type == ML::ChallengePack) { const FString Id = M->Id; const int32 Tier = M->Tier + Delta; Edit([&](FCireMapLayout& X) { return ML::SetTier(X, Id, Tier); }); }
        else E.NextTier = FMath::Clamp(E.NextTier + Delta, 1, FCireChallengeBay::MaxTier);
    };
    auto CycleOwner = [&]()
    {
        const FCireMapMarker* M = SelectedMarker(); if (!M) return;
        const FString Id = M->Id;
        const ECireMarkerOwner Next = M->Owner == ECireMarkerOwner::Team1 ? ECireMarkerOwner::Team2 : M->Owner == ECireMarkerOwner::Team2 ? ECireMarkerOwner::Shared : ECireMarkerOwner::Team1;
        Edit([&](FCireMapLayout& X) { return ML::SetOwner(X, Id, Next); });
    };
    auto ToggleTarget = [&]()
    {
        const FCireMapMarker* M = SelectedMarker(); if (!M || !(M->Type == ML::MonsterSpawn || M->Type == ML::MonsterPath)) return;
        const FString Id = M->Id; const ECireMarkerOwner Next = M->Target == ECireMarkerOwner::Team1 ? ECireMarkerOwner::Team2 : ECireMarkerOwner::Team1;
        Edit([&](FCireMapLayout& X) { return ML::SetTarget(X, Id, Next); });
    };
    auto ToggleMirror = [&]()
    {
        const FCireMapMarker* M = SelectedMarker(); if (!M) return;
        const FString Id = M->Id; const bool bNext = !M->bMirror;
        if (Edit([&](FCireMapLayout& X) { return ML::SetMirror(X, Id, bNext); }))
            Say(bNext ? TEXT("Mirror on: the other team gets a matching twin.") : TEXT("Mirror off: this marker and its twin are now independent."));
    };
    auto CycleKind = [&]()
    {
        const FCireMapMarker* M = SelectedMarker(); if (!M || M->Type != ML::Vendor) return;
        const TArray<FCireVendorType>& VT = ML::VendorTypes();
        int32 Index = VT.IndexOfByPredicate([&](const FCireVendorType& T) { return T.Id == M->Kind; });
        const FString Id = M->Id, Next = VT.Num() ? VT[(Index + 1) % VT.Num()].Id : FString();
        Edit([&](FCireMapLayout& X) { return ML::SetKind(X, Id, Next); });
    };
    auto DoApply = [&]()
    {
        FString Result;
        CireLayoutEditor::RunValidation(World, E, false);
        const bool bLive = CireLayoutEditor::Apply(World, E, Result);
        CireLayoutEditor::Autosave(E, Now, true);
        Say((bLive ? TEXT("APPLIED: ") : TEXT("SAVED (not live): ")) + Result);
    };
    // layout-wiring: TEST THIS LAYOUT: Apply, then a real match on it in a new window.
    auto DoTest = [&]()
    {
        FString Result;
        CireLayoutEditor::RunValidation(World, E, false);
        if (!CireLayoutEditor::Apply(World, E, Result)) { E.bShowIssues = true; Say(TEXT("NOT LAUNCHED: ") + Result); return; }
        CireLayoutEditor::Autosave(E, Now, true);
        FString Launch;
        CireLayoutRuntime::LaunchTestMatch(Launch);
        Say(Launch);
    };
    auto DoNew = [&]()
    {
        if (Edit([&](FCireMapLayout& X) { const FString Name = X.Name; X = FCireMapLayout(); X.Name = Name; return true; }))
        { E.Sel.Reset(); E.ChainId.Reset(); CireLayoutEditor::StopPreview(E); Say(TEXT("New layout: every marker cleared (Undo brings them back).")); }
    };
    auto DoClear = [&]()
    {
        const FName Type = !E.Armed.IsNone() ? E.Armed : E.TypeFilter >= 0 && Types.IsValidIndex(E.TypeFilter) ? Types[E.TypeFilter].Id : NAME_None;
        int32 Count = 0;
        if (Edit([&](FCireMapLayout& X) { Count = ML::Clear(X, Type); return Count > 0; }))
        { E.Sel.Reset(); E.ChainId.Reset(); Say(FString::Printf(TEXT("Cleared %d %s markers."), Count, Type.IsNone() ? TEXT("") : *ML::FindType(Type)->Name)); }
    };
    auto DoValidate = [&]()
    {
        CireLayoutEditor::RunValidation(World, E, true); E.bShowIssues = true;
        int32 Errors = 0; for (const FCireLayoutIssue& I : E.Issues) Errors += I.bError ? 1 : 0;
        const int32 Notes = E.Issues.Num() - Errors;
        Say(Errors == 0 ? FString::Printf(TEXT("VALID: the game can run it (spawns, paths to the objective, navmesh, runtime rules)%s."), Notes ? *FString::Printf(TEXT("; %d notes on the right"), Notes) : TEXT(""))
            : FString::Printf(TEXT("%d problems found (listed on the right)."), Errors));
    };
    auto SwitchView = [&]()
    {
        E.bWalk = !E.bWalk;
        if (!E.bWalk && Hero) { E.Focus = Hero->GetActorLocation(); E.Yaw = PlayerOwner->GetControlRotation().Yaw; }
        Say(E.bWalk ? TEXT("Walk view: walk and run the town; setters place at your feet (key) or the cursor (click).") : TEXT("Map view: WASD pan, Q/E rotate, wheel zoom; setters place at the cursor."));
    };
    auto SwitchRealm = [&]()
    {
        if (E.bWalk && Hero) CireRouteEditMode::SwitchRealm(Hero);
        else { const FVector2D Local = CireLanePath::ToLocal(E.Realm, E.Focus); E.Realm = 1 - E.Realm; E.Focus = CireLanePath::ToWorld(E.Realm, Local, 0.f); }
        Say(FString::Printf(TEXT("Now in %s (%s's realm). The layout is shared: markers show in both."), *ML::RealmName(E.bWalk && Hero ? 1 - E.Realm : E.Realm), *ML::TeamTag(ML::TeamOfRealm(E.bWalk && Hero ? 1 - E.Realm : E.Realm))));
    };

    // ---- keyboard ------------------------------------------------------------------------------------------------
    const bool bKeys = !bSettings && E.Naming == 0;
    if (E.Naming != 0 && !bSettings)
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
            const FString Text = E.NameBuffer.TrimStartAndEnd();
            if (E.Naming == 1 && SelectedMarker()) { const FString Id = E.Sel.Id; Edit([&](FCireMapLayout& X) { return ML::SetName(X, Id, Text); }); }
            else if (E.Naming == 2 && !ML::SanitizeName(Text).IsEmpty())
            {
                L.Name = Text; FString Error;
                Say(ML::Save(L, ML::NamedPath(Text), &Error) ? FString::Printf(TEXT("Saved layout \"%s\" (Content/Data/MapLayouts/%s.json)."), *Text, *ML::SanitizeName(Text)) : Error);
                E.bUnsaved = true; E.EditedAt = Now;
            }
            E.Naming = 0;
        }
    }
    if (bKeys)
    {
        // Setter skills: action bar keys (the player's own bindings).
        for (int32 I = 0; I < SetterCount; ++I)
        {
            const FName Action = SetterAction(I);
            if (Action.IsNone() || !Keys.WasPressed(PlayerOwner, Action)) continue;
            const FCireMarkerType* T = &Types[I];
            if (IsPointType(T))
            {
                if (E.Armed != T->Id) { E.Armed = T->Id; if (!(ML::Find(L, E.ChainId) && ML::Find(L, E.ChainId)->Type == T->Id)) E.ChainId.Reset(); }
                // Each press chains a point: at your feet while walking, at the cursor on the map.
                if (E.bWalk && Hero) PlaceAt(T, FeetRealm, FeetLocal, ViewYaw);
                else if (bCursor && !bOverUI) PlaceAt(T, CursorRealm, CursorLocal, ViewYaw);
                else Say(FString::Printf(TEXT("%s armed: click the ground to chain points (Enter finishes)."), *T->Name));
            }
            else if (E.Armed == T->Id)
            {
                // Pressed again: place it (at your feet facing your view while walking, else at the cursor).
                if (E.bWalk && Hero) PlaceAt(T, FeetRealm, FeetLocal, ViewYaw);
                else if (bCursor) PlaceAt(T, CursorRealm, CursorLocal, ViewYaw);
                E.Armed = NAME_None;
            }
            else { E.Armed = T->Id; E.bReplace = false; Say(FString::Printf(TEXT("%s armed: click to place, or press its key again to place it at your feet."), *T->Name)); }
        }
        if (Pressed(EKeys::R) && E.Sel.IsSet())
        {
            if (!E.bReplace) { E.bReplace = true; E.Armed = NAME_None; Say(TEXT("Replace: click the new spot (or press R again to use your feet).")); }
            else ReplaceAt(E.bWalk && Hero ? FeetRealm : CursorRealm, E.bWalk && Hero ? FeetLocal : CursorLocal);
        }
        if ((Pressed(EKeys::X) || Pressed(EKeys::Delete)) && E.Sel.IsSet()) RemoveSelection();
        if ((bCtrl && Pressed(EKeys::Z)) || Pressed(EKeys::BackSpace)) Say(CireLayoutEditor::Undo(E, Now) ? TEXT("Undone.") : TEXT("Nothing to undo."));
        if (Pressed(EKeys::Enter)) { E.ChainId.Reset(); E.Armed = NAME_None; Say(TEXT("Chain finished.")); }
        if (Pressed(EKeys::Comma)) Rotate(bShift ? -45.f : -15.f);
        if (Pressed(EKeys::Period)) Rotate(bShift ? 45.f : 15.f);
        if (Pressed(EKeys::LeftBracket)) StepRadius(-50.f);
        if (Pressed(EKeys::RightBracket)) StepRadius(50.f);
        if (Pressed(EKeys::Hyphen)) StepTier(-1);
        if (Pressed(EKeys::Equals)) StepTier(1);
        if (Pressed(EKeys::O)) CycleOwner();
        if (Pressed(EKeys::T) && !bCtrl) ToggleTarget();
        if (Pressed(EKeys::Y)) ToggleMirror();
        if (Pressed(EKeys::K)) CycleKind();
        if (Pressed(EKeys::N) && !bCtrl && SelectedMarker()) { E.Naming = 1; E.NameBuffer = SelectedMarker()->Name; }
        if (bCtrl && Pressed(EKeys::N)) DoNew();
        if (Pressed(EKeys::V)) DoValidate();
        if (Pressed(EKeys::P)) { if (E.bPreview) { CireLayoutEditor::StopPreview(E); Say(TEXT("Preview stopped.")); } else CireLayoutEditor::StartPreview(World, E); }
        if (bCtrl && Pressed(EKeys::S)) DoApply();
        if (bCtrl && Pressed(EKeys::T)) DoTest(); // layout-wiring
        if (Pressed(EKeys::M)) SwitchView();
        if (Pressed(EKeys::G)) SwitchRealm();
        if (Pressed(EKeys::F) && bCursor)
        {
            // Aim at a marker and press F: the nearest handle to the cursor within 6 m is selected.
            FCireLayoutPick Best; double BestD = 600. * 600.;
            for (const FCireMapMarker& M : L.Markers)
            {
                if (!ML::ShownInRealm(M, CursorRealm)) continue;
                auto Try = [&](const FVector2D& P, int32 Point, ECireVendorPart Part) { const double D = FVector2D::DistSquared(P, CursorLocal); if (D < BestD) { BestD = D; Best = {M.Id, Point, Part}; } };
                if (M.Points.Num() > 0) for (int32 I = 0; I < M.Points.Num(); ++I) Try(M.Points[I], I, ECireVendorPart::Npc);
                else Try(M.Position, INDEX_NONE, ECireVendorPart::Npc);
                if (M.Type == ML::Vendor) { Try(M.SignPos, INDEX_NONE, ECireVendorPart::Sign); Try(M.StallPos, INDEX_NONE, ECireVendorPart::Stall); }
            }
            if (Best.IsSet()) { Select(Best); Say(FString::Printf(TEXT("Selected %s."), *ML::DisplayLabel(L, *ML::Find(L, Best.Id)))); }
            else Say(TEXT("Nothing within 6 m of the cursor."));
        }
        if (!E.bWalk && !bOverUI)
        {
            if (Pressed(EKeys::MouseScrollUp)) E.Distance = FMath::Max(900.f, E.Distance * .85f);
            if (Pressed(EKeys::MouseScrollDown)) E.Distance = FMath::Min(30000.f, E.Distance * 1.18f);
        }
    }
    // Mouse in the world.
    const bool bRightClick = Pressed(EKeys::RightMouseButton);
    if (bRightClick && (E.bReplace || !E.Armed.IsNone())) { E.bReplace = false; E.Armed = NAME_None; }
    if (Clicked && !bOverUI && !bSettings && E.Naming == 0)
    {
        if (E.bReplace && E.Sel.IsSet() && bCursor) { ReplaceAt(CursorRealm, CursorLocal); Clicked = false; }
        else if (ArmedType && bCursor) { PlaceAt(ArmedType, CursorRealm, CursorLocal, ViewYaw); if (!IsPointType(ArmedType)) E.Armed = NAME_None; Clicked = false; }
        else if (E.Hover.IsSet()) { Select(E.Hover); Clicked = false; }
        else if (!E.bWalk) E.Sel.Reset();
    }

    // ---- validation (light, throttled) and autosave --------------------------------------------------------------
    if (E.bIssuesDirty && Now - E.ValidatedAt > .2) CireLayoutEditor::RunValidation(World, E, false);
    CireLayoutEditor::Autosave(E, Now);

    // ---- panel: setter bar (the WoW-style skill bar) ----------------------------------------------------------------
    auto Button = [&](const FString& Title, float BX, float BY, float W, const FString& Help, bool bEnabled = true, bool bSelected = false, FLinearColor Accent = CireUIColors::Gold, float H = 22.f)
    {
        const bool Over = bEnabled && Hit(BX, BY, W, H);
        CireUIStyle::Button(Painter(), BX, BY, W, H, Title, !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : Over ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 8.5f);
        Tip(Title, Help, BX, BY, W, H);
        if (Over && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    CireUIStyle::Frame(Painter(), BarX - 10, BarY - 8, BarW + 20, BarSlot + 30, CireUIColors::Gold, ECireFrame::Panel);
    for (int32 I = 0; I < SetterCount; ++I)
    {
        const FCireMarkerType& T = Types[I];
        const float SX = BarX + I * (BarSlot + BarGap);
        FCireIconSlot Slot;
        Slot.IconId = T.Icon; Slot.IconTexture = CireUIStyle::FindAbilityIcon(T.Icon); Slot.Tint = T.Color;
        Slot.KeyLabel = Keys.Label(SetterAction(I));
        Slot.bHover = Hit(SX, BarY, BarSlot, BarSlot);
        Slot.bGlow = E.Armed == T.Id;
        Slot.bPressed = E.Armed == T.Id;
        Slot.Charges = ML::OfType(L, T.Id).Num();
        CireUIStyle::IconSlot(Painter(), SX, BarY, BarSlot, Slot, Now);
        const FString Short = T.Label.Left(8);
        TextFx(Short, SX + BarSlot * .5f - TextWidth(Short, 7.f) * .5f, BarY + BarSlot + 3, 7.f, E.Armed == T.Id ? CireUIColors::BrightGold : CireUIColors::Parchment, ECireFont::Bold, true);
        FString Help = FString::Printf(TEXT("%s. %s"), *T.Name,
            T.bPoints ? TEXT("Each press chains a point to the previous one: at your feet while walking, at the cursor on the map. Enter finishes.")
                      : TEXT("Press to arm, then click the ground; press again to place it at your feet facing your view."));
        if (T.Id == ML::ChallengePack) Help += TEXT(" [ ] radius, - = tier.");
        if (T.Id == ML::Vendor) Help += TEXT(" K cycles the vendor type; the sign and stall are separate handles.");
        if (T.bTarget) Help += TEXT(" T switches the team its waves attack.");
        Tip(T.Name, Help + TEXT(" Team markers are mirrored to the other team by default (Y toggles)."), SX, BarY, BarSlot, BarSlot);
        if (Slot.bHover && Clicked)
        {
            Clicked = false; PlayUIFeedback();
            E.Armed = E.Armed == T.Id ? NAME_None : T.Id; E.bReplace = false;
            if (IsPointType(&T) && !(ML::Find(L, E.ChainId) && ML::Find(L, E.ChainId)->Type == T.Id)) E.ChainId.Reset();
        }
    }
    // Command row.
    {
        float X = CmdX; const float W = 80.f;
        const bool bSel = E.Sel.IsSet();
        if (Button(E.bReplace ? TEXT("REPLACING") : TEXT("REPLACE  R"), X, CmdY, W, TEXT("Move the selection to a new spot: click it, or press R again to use your feet. Packs keep their tier and radius; path points keep their order."), bSel, E.bReplace))
        { E.bReplace = !E.bReplace; E.Armed = NAME_None; } X += W + 4;
        if (Button(TEXT("REMOVE  X"), X, CmdY, W, TEXT("Delete the selection. Packs renumber; a removed path point re-chains its neighbours; a vendor's sign or stall goes back to its default spot."), bSel, false, CireUIColors::Red)) RemoveSelection(); X += W + 4;
        if (Button(TEXT("UNDO  ^Z"), X, CmdY, W, TEXT("Undo the last change (Ctrl+Z or Backspace)."), E.History.Num() > 0)) Say(CireLayoutEditor::Undo(E, Now) ? TEXT("Undone.") : TEXT("Nothing to undo.")); X += W + 4;
        if (Button(TEXT("NEW  ^N"), X, CmdY, W, TEXT("Start an empty layout (undoable)."))) DoNew(); X += W + 4;
        const FString ClearWhat = !E.Armed.IsNone() ? FString::Printf(TEXT("every %s marker"), *ML::FindType(E.Armed)->Name) : E.TypeFilter >= 0 && Types.IsValidIndex(E.TypeFilter) ? FString::Printf(TEXT("every %s marker (list filter)"), *Types[E.TypeFilter].Name) : FString(TEXT("every marker"));
        if (Button(TEXT("CLEAR"), X, CmdY, W, FString::Printf(TEXT("Clear %s (undoable)."), *ClearWhat), L.Markers.Num() > 0, false, CireUIColors::Red)) DoClear(); X += W + 4;
        if (Button(TEXT("VALIDATE  V"), X, CmdY, W, TEXT("Check both teams: player spawns, monster spawns that target them, paths reaching their objective, mirrored pairs in sync, markers inside the bounds and on the navmesh, vendor stalls and signs."), true, E.bShowIssues, CireUIColors::Teal)) DoValidate(); X += W + 4;
        if (Button(E.bPreview ? TEXT("STOP  P") : TEXT("PREVIEW  P"), X, CmdY, W, TEXT("Walk a monster down every path in both realms, on the navmesh, at the wave march speed."), true, E.bPreview, CireUIColors::Teal))
        { if (E.bPreview) CireLayoutEditor::StopPreview(E); else CireLayoutEditor::StartPreview(World, E); } X += W + 4;
        if (Button(TEXT("APPLY  ^S"), X, CmdY, W, TEXT("Write MapLayout.json (every match loads it) and TownVendors.json, re-place the merchants and apply every path, spawn, pack and spot live."), true, false, CireUIColors::Teal)) DoApply(); X += W + 4;
        if (Button(TEXT("TEST  ^T"), X, CmdY, W, TEXT("TEST THIS LAYOUT: Apply, then launch a real match on it in a new window (waves, packs, vendors, bots)."), true, false, CireUIColors::Gold)) DoTest(); X += W + 4;
        if (Button(TEXT("SAVE AS"), X, CmdY, W, TEXT("Save this layout under a name (Content/Data/MapLayouts)."))) { E.Naming = 2; E.NameBuffer = L.Name; } X += W + 4;
        if (Button(TEXT("LOAD"), X, CmdY, W, TEXT("Load a named layout into the draft."), true, E.bLoadList)) E.bLoadList = !E.bLoadList; X += W + 4;
        if (Button(E.bWalk ? TEXT("MAP VIEW  M") : TEXT("WALK  M"), X, CmdY, W, TEXT("Walk view (your champion) or map view (top-down camera)."))) SwitchView(); X += W + 4;
        if (Button(TEXT("REALM  G"), X, CmdY, W, TEXT("Go to the other realm (same layout, the other team's copy)."))) SwitchRealm();
    }

    // ---- panel: marker list ---------------------------------------------------------------------------------------
    CireUIStyle::Frame(Painter(), ListX, ListY, ListW, ListH, CireUIColors::Gold, ECireFrame::Panel);
    CireUIStyle::Header(Painter(), ListX + 12, ListY + 10, ListW - 24, FString::Printf(TEXT("MARKERS  (%d)"), L.Markers.Num()), CireUIColors::Gold, 10.f);
    {
        float FX = ListX + 12; const float FY = ListY + 36;
        const TCHAR* TeamNames[] = {TEXT("ALL"), TEXT("SHARED"), TEXT("T1"), TEXT("T2")};
        const int32 TeamValues[] = {-1, 0, 1, 2};
        for (int32 K = 0; K < 4; ++K)
        {
            const float W = K == 1 ? 62.f : 44.f;
            if (Button(TeamNames[K], FX, FY, W, TEXT("Filter the list (and the world gizmos) by team."), true, E.TeamFilter == TeamValues[K], K == 2 ? Team1Color : K == 3 ? Team2Color : CireUIColors::Gold)) { E.TeamFilter = TeamValues[K]; E.ListScroll = 0; }
            FX += W + 4;
        }
        const FString TypeName = E.TypeFilter >= 0 && Types.IsValidIndex(E.TypeFilter) ? Types[E.TypeFilter].Label.ToUpper() : FString(TEXT("ALL TYPES"));
        if (Button(TypeName, FX, FY, ListX + ListW - 12 - FX, TEXT("Filter by setter type (click cycles; the setter bar also shows each count)."), true, E.TypeFilter >= 0))
        { E.TypeFilter = E.TypeFilter + 1 >= Types.Num() ? -1 : E.TypeFilter + 1; E.ListScroll = 0; }
    }
    {
        TArray<const FCireMapMarker*> Rows;
        for (const FCireMapMarker& M : L.Markers)
            if ((E.TeamFilter < 0 || static_cast<int32>(M.Owner) == E.TeamFilter) && (E.TypeFilter < 0 || (Types.IsValidIndex(E.TypeFilter) && M.Type == Types[E.TypeFilter].Id))) Rows.Add(&M);
        const float RowH = 20.f, Top = ListY + 66;
        const int32 Visible = FMath::Max(1, static_cast<int32>((ListH - 76) / RowH));
        if (Hit(ListX, Top, ListW, ListH - 76))
        {
            if (Pressed(EKeys::MouseScrollUp)) E.ListScroll = FMath::Max(0, E.ListScroll - 3);
            if (Pressed(EKeys::MouseScrollDown)) E.ListScroll += 3;
        }
        E.ListScroll = FMath::Clamp(E.ListScroll, 0, FMath::Max(0, Rows.Num() - Visible));
        if (Rows.Num() == 0) Wrapped(TEXT("No markers yet. Pick a setter below (keys 1..0) and place it; Monster Path and Play Bounds chain a point per press."), ListX + 14, Top + 4, ListW - 28, 8.5f, CireUIColors::Muted, 4);
        for (int32 I = 0; I < Visible && E.ListScroll + I < Rows.Num(); ++I)
        {
            const FCireMapMarker& M = *Rows[E.ListScroll + I];
            const FCireMarkerType* T = ML::FindType(M.Type);
            const float RY = Top + I * RowH;
            const bool bRowSel = E.Sel.Id == M.Id, bOver = Hit(ListX + 8, RY, ListW - 16, RowH - 2);
            Painter().Rect(ListX + 8, RY, ListW - 16, RowH - 2, bRowSel ? FLinearColor(.25f, .2f, .08f, .8f) : bOver ? FLinearColor(1, 1, 1, .06f) : FLinearColor(0, 0, 0, .25f));
            Painter().Rect(ListX + 10, RY + 3, 4, RowH - 8, TeamColor(M.Owner));
            Disc(ListX + 22, RY + 9, 4.5f, T ? T->Color : CireUIColors::Gold);
            Label(ML::DisplayLabel(L, M).Left(40), ListX + 32, RY + 3, 8.5f, bRowSel ? CireUIColors::BrightGold : CireUIColors::Parchment);
            if (M.bMirror && M.Owner != ECireMarkerOwner::Shared) Label(TEXT("MIR"), ListX + ListW - 40, RY + 4, 7.f, CireUIColors::Muted);
            if (bOver && Clicked)
            {
                Clicked = false; PlayUIFeedback();
                const bool bDouble = E.LastRowId == M.Id && Now - E.LastRowClick < .4;
                Select({M.Id, M.Points.Num() > 0 ? 0 : INDEX_NONE, ECireVendorPart::Npc});
                if (bDouble || !E.bWalk) FlyTo(M);
                E.LastRowId = M.Id; E.LastRowClick = Now;
            }
        }
        if (Rows.Num() > Visible) Label(FString::Printf(TEXT("%d-%d of %d  (wheel scrolls; double-click flies there)"), E.ListScroll + 1, FMath::Min(Rows.Num(), E.ListScroll + Visible), Rows.Num()), ListX + 12, ListY + ListH - 16, 7.5f, CireUIColors::Muted);
    }

    // ---- panel: inspector, readouts, validation, status -----------------------------------------------------------
    CireUIStyle::Frame(Painter(), InspX, InspY, InspW, InspH, CireUIColors::Teal, ECireFrame::Panel);
    const int32 ViewRealm = E.Realm;
    CireUIStyle::Header(Painter(), InspX + 12, InspY + 10, InspW - 24, FString::Printf(TEXT("MAP LAYOUT  |  %s"), *L.Name.Left(22)), CireUIColors::Gold, 10.f);
    float Y = InspY + 34;
    const float IX = InspX + 14, IW = InspW - 28;
    Label(FString::Printf(TEXT("%s view  |  %s (%s realm)  |  %s"), E.bWalk ? TEXT("Walk") : TEXT("Map"), *ML::RealmName(ViewRealm), *ML::TeamTag(ML::TeamOfRealm(ViewRealm)),
        E.bUnsaved ? TEXT("unsaved") : E.SavedAt >= 0 ? TEXT("autosaved") : TEXT("draft")), IX, Y, 8.5f, CireUIColors::Muted); Y += 16;
    // Selection.
    if (const FCireMapMarker* M = SelectedMarker())
    {
        const FCireMarkerType* T = ML::FindType(M->Type);
        Painter().Rect(IX - 4, Y - 2, IW + 8, 2, TeamColor(M->Owner));
        TextFx(ML::DisplayLabel(L, *M), IX, Y + 4, 10.f, CireUIColors::BrightGold, ECireFont::Bold, true); Y += 20;
        FString Sub = FString::Printf(TEXT("%s  |  %s"), T ? *T->Name : TEXT("?"), *M->Id);
        if (E.Sel.Point != INDEX_NONE) Sub += FString::Printf(TEXT("  |  point %d of %d"), E.Sel.Point + 1, M->Points.Num());
        if (E.Sel.Part != ECireVendorPart::Npc) Sub += E.Sel.Part == ECireVendorPart::Sign ? TEXT("  |  SIGN") : TEXT("  |  STALL");
        Label(Sub, IX, Y, 8.f, CireUIColors::Muted); Y += 16;
        // Owner / target / mirror.
        const FString Id = M->Id;
        float BX = IX;
        for (const ECireMarkerOwner O : {ECireMarkerOwner::Team1, ECireMarkerOwner::Team2, ECireMarkerOwner::Shared})
        {
            if (Button(O == ECireMarkerOwner::Shared ? TEXT("SHARED") : ML::TeamTag(O), BX, Y, 58, TEXT("Owner (O cycles). Team markers live in their team's realm; shared markers show in both."), true, M->Owner == O, TeamColor(O)))
                Edit([&](FCireMapLayout& X) { return ML::SetOwner(X, Id, O); });
            BX += 62;
        }
        if (Button(M->bMirror ? TEXT("MIRROR ON") : TEXT("MIRROR OFF"), BX, Y, IW - (BX - IX), TEXT("Mirror to the other team (Y): a twin at the same realm-local spot in the other realm, kept in sync. Off breaks the pair on purpose."), M->Owner != ECireMarkerOwner::Shared, M->bMirror, CireUIColors::Teal)) ToggleMirror();
        Y += 26;
        if (T && T->bTarget)
        {
            Label(TEXT("WAVES ATTACK"), IX, Y + 5, 8.f, CireUIColors::Muted);
            for (int32 K = 0; K < 2; ++K)
            {
                const ECireMarkerOwner O = K == 0 ? ECireMarkerOwner::Team1 : ECireMarkerOwner::Team2;
                if (Button(ML::TeamTag(O), IX + 96 + K * 62, Y, 58, TEXT("The team this spawn's / path's waves attack (T toggles)."), true, M->Target == O, TeamColor(O)))
                    Edit([&](FCireMapLayout& X) { return ML::SetTarget(X, Id, O); });
            }
            Y += 26;
        }
        auto Stepper = [&](const FString& Caption, const FString& Value, float SY, TFunctionRef<void()> Minus, TFunctionRef<void()> Plus, const FString& Help)
        {
            Label(Caption, IX, SY + 5, 8.f, CireUIColors::Muted);
            if (Button(TEXT("-"), IX + 96, SY, 22, Help)) Minus();
            Painter().Rect(IX + 120, SY, 92, 22, FLinearColor(0, 0, 0, .45f));
            Label(Value, IX + 166 - TextWidth(Value, 9.f) * .5f, SY + 5, 9.f, CireUIColors::Parchment);
            if (Button(TEXT("+"), IX + 214, SY, 22, Help)) Plus();
        };
        const ECireVendorPart Part = E.Sel.Part;
        if (T && (T->bFacing || M->Type == ML::Vendor))
        {
            const float Yaw = Part == ECireVendorPart::Sign ? M->SignYaw : Part == ECireVendorPart::Stall ? M->StallYaw : M->Yaw;
            Stepper(Part == ECireVendorPart::Sign ? TEXT("SIGN FACING") : Part == ECireVendorPart::Stall ? TEXT("STALL FACING") : TEXT("FACING"), FString::Printf(TEXT("%.0f deg"), Yaw), Y,
                [&]() { Rotate(-15.f); }, [&]() { Rotate(15.f); }, TEXT("Rotate the selection (, and . keys; Shift for 45 degrees)."));
            Y += 26;
        }
        if (T && T->bRadius)
        {
            Stepper(TEXT("RADIUS"), FString::Printf(TEXT("%.1f m"), M->Radius / 100.f), Y, [&]() { StepRadius(-50.f); }, [&]() { StepRadius(50.f); }, TEXT("Radius ([ and ] keys)."));
            Y += 26;
        }
        if (T && T->bTier)
        {
            Stepper(TEXT("TIER"), FString::Printf(TEXT("Tier %d"), M->Tier), Y, [&]() { StepTier(-1); }, [&]() { StepTier(1); }, TEXT("Base tier 1..10 (- and = keys); rounds promote it."));
            Y += 26;
        }
        if (M->Type == ML::Vendor)
        {
            const FCireVendorType* VT = ML::FindVendorType(M->Kind);
            if (Button(FString::Printf(TEXT("TYPE: %s"), VT ? *VT->Name.Left(28) : *M->Kind), IX, Y, IW, TEXT("Vendor type from Vendors.json (K cycles). The sign and stall move to that type's default spots."), true, false, CireUIColors::Gold)) CycleKind();
            Y += 26;
            const TCHAR* PartNames[] = {TEXT("NPC"), TEXT("SIGN"), TEXT("STALL")};
            for (int32 K = 0; K < 3; ++K)
                if (Button(PartNames[K], IX + K * 84, Y, 80, TEXT("Select the NPC (whole group), the sign or the stall. Replace, Remove and rotate act on the selected handle."), true, static_cast<int32>(Part) == K))
                    E.Sel.Part = static_cast<ECireVendorPart>(K);
            Y += 26;
        }
        if (T && T->bNamed)
        {
            if (Button(E.Naming == 1 ? FString::Printf(TEXT("NAME: %s_"), *E.NameBuffer) : FString::Printf(TEXT("RENAME (N): %s"), *M->Name.Left(26)), IX, Y, IW, TEXT("Type a name, Enter to keep it, Esc to cancel."), true, E.Naming == 1))
            { E.Naming = 1; E.NameBuffer = M->Name; }
            Y += 26;
        }
        if (M->Type == ML::MonsterSpawn)
        {
            // layout-wiring: how this spawn splits its units across its paths.
            const FString SpawnId = M->Id; const bool bWeighted = M->bSplitWeighted;
            if (Button(bWeighted ? TEXT("SPLIT: BY PATH WEIGHT") : TEXT("SPLIT: EVEN ACROSS PATHS"), IX, Y, IW, TEXT("Even: every path of this spawn gets the same share of its units. By weight: each path's WEIGHT sets its share."), true, bWeighted, CireUIColors::Gold))
                Edit([&](FCireMapLayout& X) { return ML::SetSplit(X, SpawnId, !bWeighted); });
            Y += 26;
            const auto Links = ML::PathsFrom(L, M->Id);
            Wrapped(Links.Num() == 0 ? FString(TEXT("No path yet: NEW PATH starts one here.")) : FString::Printf(TEXT("Paths: %s"), *FString::JoinBy(Links, TEXT(", "), [](const FCireMapMarker* P) { return P->Name; })), IX, Y, IW, 8.5f, Links.Num() ? CireUIColors::Parchment : CireUIColors::Orange, 2);
            Y += 26;
            if (Button(TEXT("NEW PATH FROM HERE"), IX, Y, IW, TEXT("Arm Set Path starting at this spawn: each press chains a point toward the objective."), true, false, CireUIColors::Teal))
            { E.Armed = ML::MonsterPath; E.ChainId.Reset(); E.ChainFrom = M->Id; Say(TEXT("Set Path armed from this spawn: chain points toward the objective (or onto another path to merge).")); }
            Y += 28;
        }
        if (M->Type == ML::MonsterPath)
        {
            const FCireMapMarker* From = M->From.IsEmpty() ? nullptr : ML::Find(L, M->From);
            bool bReaches = false; ML::WalkPolyline(L, M->Id, &bReaches);
            Wrapped(FString::Printf(TEXT("From %s  |  %s  |  %d points"), From ? *ML::DisplayLabel(L, *From) : TEXT("NO SPAWN"),
                !M->MergeInto.IsEmpty() ? *FString::Printf(TEXT("merges into %s"), ML::Find(L, M->MergeInto) ? *ML::Find(L, M->MergeInto)->Name : TEXT("?")) : bReaches ? TEXT("reaches the objective") : TEXT("open end"),
                M->Points.Num()), IX, Y, IW, 8.5f, bReaches || !M->MergeInto.IsEmpty() ? CireUIColors::Parchment : CireUIColors::Orange, 2);
            Y += 26;
            {
                const FString PathId = M->Id; const float Weight = M->Weight;
                const FCireMapMarker* Spawn = M->From.IsEmpty() ? nullptr : ML::Find(L, M->From);
                Stepper(TEXT("WEIGHT"), FString::Printf(TEXT("%.1f%s"), Weight, Spawn && !Spawn->bSplitWeighted ? TEXT(" (even)") : TEXT("")), Y,
                    [&]() { Edit([&](FCireMapLayout& X) { return ML::SetWeight(X, PathId, FMath::Max(0.f, Weight - .5f)); }); },
                    [&]() { Edit([&](FCireMapLayout& X) { return ML::SetWeight(X, PathId, Weight + .5f); }); },
                    TEXT("This path's share of its spawn's units when the spawn splits by weight (0 = no units)."));
                Y += 26;
            }
            if (Button(E.ChainId == M->Id ? TEXT("CHAINING (Enter ends)") : TEXT("CONTINUE THIS PATH"), IX, Y, IW, TEXT("Chain more points onto this path."), !ML::PathClosed(L, M->Id), E.ChainId == M->Id, CireUIColors::Teal))
            { E.ChainId = M->Id; E.Armed = ML::MonsterPath; }
            Y += 28;
        }
        if (Button(TEXT("FLY TO"), IX, Y, IW * .5f - 2, TEXT("Go there (walk view: your champion; map view: the camera)."))) FlyTo(*M);
        if (Button(TEXT("DESELECT"), IX + IW * .5f + 2, Y, IW * .5f - 2, TEXT("Clear the selection (Esc)."))) E.Sel.Reset();
        Y += 30;
    }
    else
    {
        Wrapped(!E.Armed.IsNone() ? FString::Printf(TEXT("%s armed. Click the ground to place it%s."), *ML::FindType(E.Armed)->Name, IsPointType(ML::FindType(E.Armed)) ? TEXT(" (each click chains a point; Enter finishes)") : TEXT(", or press its key again to place it at your feet"))
            : FString(TEXT("Nothing selected. Click a marker (or aim at it and press F). Setter keys 1..0 place markers; R replace, X remove, , . rotate, [ ] radius, - = tier, O owner, T target, Y mirror.")),
            IX, Y, IW, 8.5f, CireUIColors::Parchment, 4);
        Y += 58;
        if (!E.Armed.IsNone())
        {
            const FCireMarkerType* T = ML::FindType(E.Armed);
            if (T && T->bRadius) { Label(FString::Printf(TEXT("Next radius %.1f m ([ ])"), RadiusFor(T) / 100.f), IX, Y, 8.5f, CireUIColors::Muted); Y += 14; }
            if (T && T->bTier) { Label(FString::Printf(TEXT("Next tier %d (- =)"), E.NextTier), IX, Y, 8.5f, CireUIColors::Muted); Y += 14; }
        }
    }
    // Paths: length and walk time per realm.
    {
        Painter().Rect(IX - 4, Y, IW + 8, 1, FLinearColor(1, 1, 1, .12f)); Y += 6;
        TextFx(FString::Printf(TEXT("PATHS  (walk at %.0f cm/s)"), E.PreviewSpeed), IX, Y, 8.5f, CireUIColors::Gold, ECireFont::Bold, true); Y += 16;
        int32 Shown = 0;
        for (const FCireMapMarker& M : L.Markers)
        {
            if (M.Type != ML::MonsterPath || Shown >= 6) continue;
            const int32 Realm = M.Owner == ECireMarkerOwner::Shared ? ViewRealm : ML::RealmOf(M.Owner);
            const double* Walk = E.WalkLength[Realm].Find(M.Id);
            const double Length = Walk ? *Walk : ML::PolylineLength(ML::WalkPolyline(L, M.Id));
            bool bReaches = false; ML::WalkPolyline(L, M.Id, &bReaches);
            Label(FString::Printf(TEXT("%s: %.0f m  %s%s"), *ML::DisplayLabel(L, M).Left(30), Length / 100., *CireRouteEditor::FormatWalkTime(Length / FMath::Max(1.f, E.PreviewSpeed)),
                bReaches ? TEXT("") : TEXT("  (no objective)")), IX, Y, 8.f, bReaches ? CireUIColors::Parchment : CireUIColors::Orange);
            Y += 13; ++Shown;
        }
        if (Shown == 0) { Label(TEXT("No monster paths yet."), IX, Y, 8.f, CireUIColors::Muted); Y += 13; }
        Label(FString::Printf(TEXT("%s"), E.bNavChecked ? TEXT("navmesh lengths (validated)") : TEXT("straight-line lengths until you Validate")), IX, Y, 7.5f, CireUIColors::Muted); Y += 14;
    }
    // Validation.
    {
        Painter().Rect(IX - 4, Y, IW + 8, 1, FLinearColor(1, 1, 1, .12f)); Y += 6;
        const int32 Count = E.Issues.Num();
        TextFx(Count == 0 ? FString(TEXT("VALID")) : FString::Printf(TEXT("%d PROBLEMS%s"), Count, E.bNavChecked ? TEXT("") : TEXT("  (V adds navmesh checks)")), IX, Y, 9.f, Count == 0 ? CireUIColors::Teal : CireUIColors::Orange, ECireFont::Bold, true);
        Y += 18;
        const float Bottom = InspY + InspH - 64;
        for (int32 I = 0; I < Count && Y < Bottom; ++I)
        {
            const FCireLayoutIssue& Issue = E.Issues[I];
            const bool bOver = Hit(IX - 2, Y - 1, IW + 4, 24);
            if (bOver) Painter().Rect(IX - 2, Y - 1, IW + 4, 24, FLinearColor(1, 1, 1, .06f));
            Painter().Rect(IX, Y + 2, 3, 18, Issue.Team == 1 ? Team1Color : Issue.Team == 2 ? Team2Color : CireUIColors::Orange);
            Wrapped(Issue.Message, IX + 8, Y, IW - 8, 8.f, CireUIColors::Parchment, 2);
            if (bOver && Clicked && !Issue.MarkerId.IsEmpty()) { Clicked = false; if (const FCireMapMarker* M = ML::Find(L, Issue.MarkerId)) { Select({M->Id, M->Points.Num() ? 0 : INDEX_NONE, ECireVendorPart::Npc}); FlyTo(*M); } }
            Y += 26;
        }
    }
    // Status line.
    {
        const float SY = InspY + InspH - 58;
        Painter().Rect(IX - 4, SY - 4, IW + 8, 1, FLinearColor(1, 1, 1, .12f));
        if (!E.Message.IsEmpty() && Now - E.MessageAt < 20) Wrapped(E.Message, IX, SY, IW, 8.5f, CireUIColors::Gold, 3);
        Label(E.SavedAt >= 0 ? FString::Printf(TEXT("Autosaved %.0f s ago  |  %d undo steps"), Now - E.SavedAt, E.History.Num()) : FString::Printf(TEXT("%d undo steps"), E.History.Num()), IX, InspY + InspH - 16, 7.5f, CireUIColors::Muted);
    }
    // Save As / Load overlays.
    if (E.Naming == 2)
    {
        const float W = 360, H = 70, X = ViewW * .5f - W * .5f, OY = ViewH * .35f;
        LayoutUIRects.Add({X, OY, W, H});
        CireUIStyle::Frame(Painter(), X, OY, W, H, CireUIColors::Gold, ECireFrame::Panel);
        Label(TEXT("SAVE LAYOUT AS  (Enter saves, Esc cancels)"), X + 14, OY + 10, 9.f, CireUIColors::Gold);
        Painter().Rect(X + 14, OY + 32, W - 28, 24, FLinearColor(0, 0, 0, .5f));
        Label(E.NameBuffer + (FMath::Fmod(Now, 1.) < .5 ? TEXT("_") : TEXT("")), X + 20, OY + 37, 10.f, CireUIColors::Parchment);
    }
    if (E.bLoadList)
    {
        const TArray<FString> Names = ML::ListNamed();
        const float W = 320, RowH = 24, H = 44 + FMath::Max(1, Names.Num()) * RowH, X = ViewW * .5f - W * .5f, OY = ViewH * .25f;
        LayoutUIRects.Add({X, OY, W, H});
        CireUIStyle::Frame(Painter(), X, OY, W, H, CireUIColors::Gold, ECireFrame::Panel);
        Label(TEXT("LOAD LAYOUT  (the draft is replaced; Undo restores it)"), X + 14, OY + 10, 8.5f, CireUIColors::Gold);
        if (Names.Num() == 0) Label(TEXT("No saved layouts yet (SAVE AS)."), X + 14, OY + 34, 9.f, CireUIColors::Muted);
        for (int32 I = 0; I < Names.Num(); ++I)
            if (Button(Names[I], X + 14, OY + 32 + I * RowH, W - 28, TEXT("Load this layout.")))
            {
                FCireMapLayout Loaded; FString Error;
                if (ML::Load(Loaded, ML::NamedPath(Names[I]), &Error)) { Edit([&](FCireMapLayout& X2) { X2 = Loaded; return true; }); E.Sel.Reset(); E.ChainId.Reset(); Say(FString::Printf(TEXT("Loaded layout %s."), *Names[I])); }
                else Say(Error);
                E.bLoadList = false;
            }
    }
    // Help strip under the bar.
    Label(E.bWalk ? TEXT("WALK: WASD / mouse move  |  setter key = at your feet  |  click = at cursor  |  F select aimed  |  M map view  |  G other realm")
                  : TEXT("MAP: WASD pan  |  Q/E rotate  |  Home/End tilt  |  wheel zoom  |  click places / selects  |  M walk view  |  G other realm"),
        BarX - 10, ViewH - 14, 7.5f, CireUIColors::Muted);
#endif
}
