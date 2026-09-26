// nav-paths: the in-game path editor (F8 > Developer > Paths). A top-down editor camera over the
// realm, draggable route handles drawn over the world, live validation colours (navmesh reachability
// and route clearance), the engine navmesh debug draw, and a minimap navmesh overlay. Edits are a
// draft until APPLY LIVE (server-authoritative CireLanePath::ApplyLive). Dev-only, like the rest of F8.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireNav.h"
#include "CireRouteEditor.h"
#include "CireDeveloperTools.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "Misc/App.h"

struct FCireRouteEditorState
{
    FCireBattlefieldRoutes Draft;
    bool bLoaded = false, bLinked = true, bShowNav = true, bDragging = false, bDirty = true;
    int32 Team = 0;
    // Selection / hover: kind 1 waypoint, 2 challenge bay, 3 goal zone.
    int32 SelKind = 0, SelIndex = INDEX_NONE, HoverKind = 0, HoverIndex = INDEX_NONE;
    FCireRouteValidation Validation;
    double ValidatedAt = -10;
    uint32 ValidatedNavRevision = MAX_uint32;
    TWeakObjectPtr<ACameraActor> Camera;
    TWeakObjectPtr<AActor> PreviousView;
    FVector Focus = FVector(5000, -2100, 0);
    float Yaw = 180.f, Pitch = -62.f, Distance = 6200.f;
    bool bNavFlagBefore = false;
#if !UE_BUILD_SHIPPING
    bool bDebugDrag = false; FVector DebugDragWorld = FVector::ZeroVector;
#endif
};

namespace
{
// ui-themes: themed colours are references to CireUIColors so they follow the active UI theme.
const FLinearColor &Gold=CireUIColors::Gold, &Parchment=CireUIColors::Parchment, &Muted=CireUIColors::Muted;
const FLinearColor Teal(.2f, .71f, .59f, 1), Red(.75f, .2f, .23f, 1), Purple(.66f, .46f, .83f, 1), Orange(1.f, .55f, .1f, 1);
constexpr float ToolbarW = 344.f, ToolbarH = 336.f;
FVector2D LocalOf(int32 Team, const FVector& World) { return CireLanePath::ToLocal(Team, World); } // medieval-kingdom
FVector WorldOf(int32 Team, const FVector2D& Local, float Z = 5.f) { return CireLanePath::ToWorld(Team, Local, Z); }
void SetNavigationShowFlag(UWorld* World, bool bShow)
{
    if (UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr) Viewport->EngineShowFlags.SetNavigation(bShow);
}
bool NavigationShowFlag(UWorld* World)
{
    const UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
    return Viewport && Viewport->EngineShowFlags.Navigation;
}
}

void ACireHUD::OpenRouteEditor(bool bOpen)
{
#if !UE_BUILD_SHIPPING
    if (bOpen && !CireDeveloperTools::CanEdit(GetWorld())) return;
    if (!RouteEditor.IsValid()) RouteEditor = MakeShared<FCireRouteEditorState>();
    FCireRouteEditorState& E = *RouteEditor;
    if (bOpen == bRouteEditor) return;
    bRouteEditor = bOpen;
    UWorld* World = GetWorld();
    if (bOpen)
    {
        bSettings = false; bEditLayout = false;
        if (!E.bLoaded) { E.Draft = CireLanePath::Get(World); E.bLoaded = true; E.bDirty = true; }
        const ACireHero* Hero = PlayerOwner ? Cast<ACireHero>(PlayerOwner->GetPawn()) : nullptr;
        E.Team = Hero ? FMath::Clamp(Hero->TeamId, 0, 1) : 0;
        {   // medieval-kingdom: the pack town frame opens on its (provisional) castle goal
            const auto& R = CireLanePath::Get(World);
            E.Focus = FVector(CireLanePath::RealmOrigin(E.Team) + (R.bTownFrame ? R.GoalCenter : FVector2D(5200, 0)), 0);
        }
        FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
        if (ACameraActor* Camera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform::Identity, Params))
        {
            Camera->GetCameraComponent()->SetFieldOfView(55.f);
            Camera->GetCameraComponent()->bConstrainAspectRatio = false;
            E.Camera = Camera;
        }
        E.PreviousView = PlayerOwner ? PlayerOwner->GetViewTarget() : nullptr;
        if (PlayerOwner && E.Camera.IsValid()) PlayerOwner->SetViewTarget(E.Camera.Get());
        E.bNavFlagBefore = NavigationShowFlag(World);
        SetNavigationShowFlag(World, E.bShowNav);
        DeveloperMessage = TEXT("Path editor: drag handles on the ground, WASD pan, Q/E rotate, wheel zoom, Esc to leave.");
    }
    else
    {
        E.bDragging = false;
        if (PlayerOwner) PlayerOwner->SetViewTarget(E.PreviousView.IsValid() ? E.PreviousView.Get() : PlayerOwner->GetPawn());
        if (E.Camera.IsValid()) E.Camera->Destroy();
        E.Camera.Reset();
        SetNavigationShowFlag(World, E.bNavFlagBefore);
    }
#endif
}

#if !UE_BUILD_SHIPPING
void ACireHUD::DebugRouteView(const FVector& Focus, float Distance, float Pitch, float Yaw)
{
    if (!RouteEditor.IsValid()) return;
    RouteEditor->Focus = Focus; RouteEditor->Distance = Distance; RouteEditor->Pitch = Pitch; RouteEditor->Yaw = Yaw;
}
void ACireHUD::DebugRouteDrag(int32 Team, int32 Index, const FVector& World, bool bRelease)
{
    if (!RouteEditor.IsValid()) return;
    FCireRouteEditorState& E = *RouteEditor;
    E.Team = FMath::Clamp(Team, 0, 1); E.SelKind = 1; E.SelIndex = Index;
    E.bDebugDrag = !bRelease; E.DebugDragWorld = World; E.bDragging = !bRelease;
    CireRouteEditor::MovePoint(E.Draft, E.Team, Index, LocalOf(E.Team, World), E.bLinked);
    E.bDirty = true;
}
#endif

void ACireHUD::TickRouteEditor()
{
#if !UE_BUILD_SHIPPING
    if (!bRouteEditor || !RouteEditor.IsValid() || !Canvas || !PlayerOwner) return;
    FCireRouteEditorState& E = *RouteEditor;
    UWorld* World = GetWorld();
    auto& Draft = E.Draft;
    const auto& Live = CireLanePath::Get(World);
    const float Dt = FMath::Clamp(static_cast<float>(FApp::GetDeltaTime()), 0.f, .1f);
    ResetTransform();

    // ---- camera: WASD / arrows pan, Q/E rotate, R/F tilt, wheel zoom --------------------------------
    {
        const bool bPanBlocked = bSettings;
        auto Down = [&](FKey A, FKey B) { return !bPanBlocked && (PlayerOwner->IsInputKeyDown(A) || PlayerOwner->IsInputKeyDown(B)); };
        const FRotator Flat(0, E.Yaw, 0);
        const FVector Forward = Flat.Vector(), Right = FRotationMatrix(Flat).GetUnitAxis(EAxis::Y);
        const float Speed = E.Distance * 0.9f * Dt;
        FVector Move = FVector::ZeroVector;
        if (Down(EKeys::W, EKeys::Up)) Move += Forward;
        if (Down(EKeys::S, EKeys::Down)) Move -= Forward;
        if (Down(EKeys::D, EKeys::Right)) Move += Right;
        if (Down(EKeys::A, EKeys::Left)) Move -= Right;
        E.Focus += Move.GetSafeNormal() * Speed;
        if (Down(EKeys::Q, EKeys::PageUp)) E.Yaw -= 70.f * Dt;
        if (Down(EKeys::E, EKeys::PageDown)) E.Yaw += 70.f * Dt;
        if (Down(EKeys::R, EKeys::Home)) E.Pitch = FMath::Clamp(E.Pitch - 40.f * Dt, -89.f, -25.f);
        if (Down(EKeys::F, EKeys::End)) E.Pitch = FMath::Clamp(E.Pitch + 40.f * Dt, -89.f, -25.f);
        if (!bSettings && PlayerOwner->WasInputKeyJustPressed(EKeys::MouseScrollUp)) E.Distance = FMath::Max(900.f, E.Distance * .85f);
        if (!bSettings && PlayerOwner->WasInputKeyJustPressed(EKeys::MouseScrollDown)) E.Distance = FMath::Min(22000.f, E.Distance * 1.18f);
        E.Focus.X = FMath::Clamp(E.Focus.X, Live.MinX - 3000.f, Live.MaxX + 1500.f);
        E.Focus.Y = FMath::Clamp(E.Focus.Y, -6000., 6000.);
        if (E.Camera.IsValid())
        {
            const FRotator View(E.Pitch, E.Yaw, 0);
            E.Camera->SetActorLocationAndRotation(E.Focus - View.Vector() * E.Distance, View);
            if (PlayerOwner->GetViewTarget() != E.Camera.Get()) PlayerOwner->SetViewTarget(E.Camera.Get());
        }
        if (!bSettings && PlayerOwner->WasInputKeyJustPressed(EKeys::Tab)) { E.Team = 1 - E.Team; E.Focus += FVector(CireLanePath::RealmOrigin(E.Team) - CireLanePath::RealmOrigin(1 - E.Team), 0); }
    }

    // ---- projection helpers ----------------------------------------------------------------------
    auto Project = [&](const FVector& W, FVector2D& Out)
    {
        const FVector S = Canvas->Project(W);
        if (S.Z <= 0.f) return false;
        Out = FVector2D(S.X / Scale, S.Y / Scale);
        return Out.X > -200 && Out.Y > -200 && Out.X < ViewW + 200 && Out.Y < ViewH + 200;
    };
    auto Seg = [&](const FVector& A, const FVector& B, FLinearColor C, float Width)
    {
        FVector2D PA, PB;
        if (Project(A, PA) && Project(B, PB)) Line(PA.X, PA.Y, PB.X, PB.Y, C, Width);
    };
    auto Ground = [&](float SX, float SY, FVector& Out)
    {
        FVector Origin, Dir;
        if (!PlayerOwner->DeprojectScreenPositionToWorld(SX * Scale, SY * Scale, Origin, Dir) || FMath::Abs(Dir.Z) < 1.e-3) return false;
        const double T = -Origin.Z / Dir.Z;
        if (T <= 0) return false;
        Out = Origin + Dir * T; Out.Z = 0; return true;
    };

    // ---- validation (throttled; also after navmesh rebuilds) ---------------------------------------
    const double Now = World->GetRealTimeSeconds();
    const uint32 NavRevision = CireNav::Stats(World).NavRevision;
    if ((E.bDirty || NavRevision != E.ValidatedNavRevision) && Now - E.ValidatedAt > (E.bDragging ? .35 : .15))
    {
        E.Validation = CireRouteEditor::Validate(World, Draft);
        E.ValidatedAt = Now; E.ValidatedNavRevision = NavRevision; E.bDirty = false;
    }
    const auto& V = E.Validation;

    // ---- world overlay ---------------------------------------------------------------------------
    const FCireUIRect Bar = {12.f, 12.f, ToolbarW, ToolbarH};
    const bool bOverBar = MX >= Bar.X && MX <= Bar.X + Bar.W && MY >= Bar.Y && MY <= Bar.Y + Bar.H;
    E.HoverKind = 0; E.HoverIndex = INDEX_NONE;
    float HoverDistance = 16.f;
    auto Consider = [&](int32 Kind, int32 Index, const FVector2D& Screen)
    {
        const float D = FVector2D::Distance(Screen, FVector2D(MX, MY));
        if (!bOverBar && D < HoverDistance) { HoverDistance = D; E.HoverKind = Kind; E.HoverIndex = Index; }
    };
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        const int32 Team = Pass == 0 ? 1 - E.Team : E.Team; // draw the edited realm last (on top)
        const bool bActive = Team == E.Team;
        const float Fade = bActive ? 1.f : .35f;
        const auto& Points = Draft.LocalPoints[Team];
        const float HalfLane = Draft.LaneWidth * .5f;
        // Lane corridor and segments coloured by navmesh reachability.
        for (int32 I = 0; I + 1 < Points.Num(); ++I)
        {
            const FVector A = WorldOf(Team, Points[I]), B = WorldOf(Team, Points[I + 1]);
            const FVector Dir = (B - A).GetSafeNormal2D(), Side(-Dir.Y, Dir.X, 0);
            Seg(A + Side * HalfLane, B + Side * HalfLane, Gold * FLinearColor(1, 1, 1, .45f * Fade), 1.2f);
            Seg(A - Side * HalfLane, B - Side * HalfLane, Gold * FLinearColor(1, 1, 1, .45f * Fade), 1.2f);
            const FCireRouteSegmentCheck* Check = V.Segments[Team].IsValidIndex(I) ? &V.Segments[Team][I] : nullptr;
            const FLinearColor Color = CireRouteEditor::ReachColor(Check ? Check->Reach : ECireRouteReach::Unknown) * FLinearColor(1, 1, 1, Fade);
            Seg(A, B, Color, bActive ? 4.f : 2.f);
            // The navmesh path a unit actually walks along this segment.
            if (Check && bActive)
                for (int32 K = 0; K + 1 < Check->Path.Num(); ++K) Seg(Check->Path[K] + FVector(0, 0, 8), Check->Path[K + 1] + FVector(0, 0, 8), FLinearColor(1, 1, 1, .7f), 1.4f);
            FVector2D Mid;
            if (Check && bActive && Project((A + B) * .5f, Mid))
            {
                const FString Tag = Check->PropConflicts > 0 ? FString::Printf(TEXT("%s | %d pieces in lane"), CireRouteEditor::ReachLabel(Check->Reach), Check->PropConflicts)
                    : FString(CireRouteEditor::ReachLabel(Check->Reach));
                TextFx(Tag, Mid.X + 6, Mid.Y + 6, 8.5f, Check->PropConflicts > 0 ? Orange : Color, ECireFont::Bold, true);
            }
        }
        // Waypoint handles: breach (spawn) red, goal teal, the rest gold with their index.
        for (int32 I = 0; I < Points.Num(); ++I)
        {
            FVector2D P;
            if (!Project(WorldOf(Team, Points[I]), P)) continue;
            if (bActive) Consider(1, I, P);
            const bool bSel = bActive && E.SelKind == 1 && E.SelIndex == I, bHover = bActive && E.HoverKind == 1 && E.HoverIndex == I;
            const FLinearColor C = (I == 0 ? Red : I == Points.Num() - 1 ? Teal : Gold) * FLinearColor(1, 1, 1, Fade);
            const float R = bSel ? 10.f : bHover ? 9.f : 7.f;
            Disc(P.X, P.Y, R + 2, FLinearColor(0, 0, 0, .7f * Fade));
            Disc(P.X, P.Y, R, C);
            if (bSel) Circle(P.X, P.Y, R + 6, Parchment, 2.f);
            if (bActive)
            {
                const FString Name = I == 0 ? FString(TEXT("BREACH")) : I == Points.Num() - 1 ? FString(TEXT("GOAL")) : FString::FromInt(I);
                TextFx(Name, P.X + R + 3, P.Y - 7, 9.f, Parchment, ECireFont::Bold, true);
            }
        }
        // Challenge bays (purple diamonds) with reachability rings, and the castle goal zone.
        for (int32 Tier = 1, Bays = CireLanePath::BayCount(Draft, Team); Tier <= Bays; ++Tier) // dev-route-tools: 1..16 packs
        {
            FVector2D P;
            if (!Project(WorldOf(Team, CireLanePath::BayPoint(Draft, Team, Tier)), P)) continue;
            if (bActive) Consider(2, Tier - 1, P);
            const bool bSel = bActive && E.SelKind == 2 && E.SelIndex == Tier - 1;
            const FLinearColor C = Purple * FLinearColor(1, 1, 1, Fade);
            const float R = bSel ? 11.f : 8.f;
            Tri(FVector2D(P.X, P.Y - R), FVector2D(P.X + R, P.Y), FVector2D(P.X, P.Y + R), C);
            Tri(FVector2D(P.X, P.Y - R), FVector2D(P.X - R, P.Y), FVector2D(P.X, P.Y + R), C);
            if (bActive)
            {
                Circle(P.X, P.Y, R + 4, CireRouteEditor::ReachColor(V.BayReach[Team].IsValidIndex(Tier - 1) ? V.BayReach[Team][Tier - 1] : ECireRouteReach::Unknown), 2.f);
                const int32 Conflicts = V.BayConflicts[Team].IsValidIndex(Tier - 1) ? V.BayConflicts[Team][Tier - 1] : 0;
                TextFx(Conflicts > 0 ? FString::Printf(TEXT("BAY %d | %d pieces"), Tier, Conflicts) : FString::Printf(TEXT("BAY %d"), Tier), P.X + R + 4, P.Y - 7, 9.f,
                    Conflicts > 0 ? Orange : Parchment, ECireFont::Bold, true);
            }
        }
        {
            const FVector2D C = Draft.GoalCenter, H = Draft.GoalSize * .5;
            const FVector Corners[4] = {WorldOf(Team, C + FVector2D(-H.X, -H.Y)), WorldOf(Team, C + FVector2D(H.X, -H.Y)), WorldOf(Team, C + FVector2D(H.X, H.Y)), WorldOf(Team, C + FVector2D(-H.X, H.Y))};
            for (int32 K = 0; K < 4; ++K) Seg(Corners[K], Corners[(K + 1) % 4], Teal * FLinearColor(1, 1, 1, Fade), bActive ? 2.5f : 1.f);
            FVector2D P;
            if (Project(WorldOf(Team, C), P))
            {
                if (bActive) Consider(3, 0, P);
                const bool bSel = bActive && E.SelKind == 3;
                Panel(P.X - 6, P.Y - 6, 12, 12, (bSel ? Parchment : Teal) * FLinearColor(1, 1, 1, Fade));
                if (bActive) TextFx(TEXT("CASTLE GOAL ZONE"), P.X + 10, P.Y - 7, 9.f, Teal, ECireFont::Bold, true);
            }
        }
    }

    // ---- mouse: select, drag, release -------------------------------------------------------------
    const bool bLeftDown = PlayerOwner->IsInputKeyDown(EKeys::LeftMouseButton);
    if (Clicked && !bSettings && !bOverBar)
    {
        if (E.HoverKind != 0) { E.SelKind = E.HoverKind; E.SelIndex = E.HoverIndex; E.bDragging = true; Clicked = false; }
        else { E.SelKind = 0; E.SelIndex = INDEX_NONE; }
    }
    bool bDragActive = E.bDragging && bLeftDown;
    FVector DragWorld = FVector::ZeroVector;
    bool bHaveGround = bDragActive && Ground(MX, MY, DragWorld);
#if !UE_BUILD_SHIPPING
    if (E.bDebugDrag) { bDragActive = true; bHaveGround = true; DragWorld = E.DebugDragWorld; }
#endif
    if (bDragActive && bHaveGround)
    {
        FVector2D Local = LocalOf(E.Team, DragWorld);
        Local.X = FMath::Clamp(Local.X, static_cast<double>(Live.MinX + 100), static_cast<double>(Live.MaxX - 100));
        Local.Y = FMath::Clamp(Local.Y, static_cast<double>(-Live.HalfWidth + 150), static_cast<double>(Live.HalfWidth - 150));
        if (E.SelKind == 1) CireRouteEditor::MovePoint(Draft, E.Team, E.SelIndex, Local, E.bLinked);
        else if (E.SelKind == 2) CireRouteEditor::MoveBay(World, Draft, E.Team, E.SelIndex + 1, Local, E.bLinked);
        else if (E.SelKind == 3) Draft.GoalCenter = Local;
        E.bDirty = true;
        FVector2D P;
        if (Project(WorldOf(E.Team, Local), P))
        {
            Circle(P.X, P.Y, 18, Parchment, 1.5f);
            TextFx(FString::Printf(TEXT("DRAGGING  x %.0f  y %.0f"), Local.X, Local.Y), P.X + 20, P.Y + 10, 10.f, Parchment, ECireFont::Bold, true);
        }
    }
    else if (E.bDragging && !bLeftDown) { E.bDragging = false; E.bDirty = true; }

    // ---- toolbar (CireUIStyle kit) -----------------------------------------------------------------
    FString Error;
    CireUIStyle::Frame(Painter(), Bar.X, Bar.Y, Bar.W, Bar.H, Teal, ECireFrame::Panel);
    CireUIStyle::Header(Painter(), Bar.X + 14, Bar.Y + 12, Bar.W - 28, FString::Printf(TEXT("PATH EDITOR  |  %s REALM"), E.Team == 0 ? TEXT("EMBER") : TEXT("DUSK")), Gold, 11.f);
    auto Button = [&](const FString& Title, float BX, float BY, float W, const FString& Help, bool bEnabled = true, bool bSelected = false, FLinearColor Accent = Gold)
    {
        const bool Over = bEnabled && Hit(BX, BY, W, 22);
        CireUIStyle::Button(Painter(), BX, BY, W, 22, Title, !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : Over ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 8.5f);
        Tip(Title, Help, BX, BY, W, 22);
        if (Over && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    const float L = Bar.X + 14, W = Bar.W - 28;
    float Y = Bar.Y + 40;
    const FLinearColor SummaryColor = !V.bRulesOk ? Red : V.Unreachable > 0 ? Orange : V.Conflicts > 0 ? CireUIColors::Neutral : Teal;
    Wrapped(V.Summary(), L, Y, W, 9.f, SummaryColor, 2); Y += 26;
    // Selection details.
    FString Selected = TEXT("Nothing selected. Click a handle: waypoint (gold), breach (red), goal (teal), bay (purple).");
    const auto& Points = Draft.LocalPoints[E.Team];
    if (E.SelKind == 1 && Points.IsValidIndex(E.SelIndex))
    {
        const auto& Segs = V.Segments[E.Team];
        const FString In = E.SelIndex > 0 && Segs.IsValidIndex(E.SelIndex - 1) ? FString::Printf(TEXT("in %.0f m %s"), Segs[E.SelIndex - 1].Direct / 100.f, CireRouteEditor::ReachLabel(Segs[E.SelIndex - 1].Reach)) : FString(TEXT("spawn"));
        const FString Out = Segs.IsValidIndex(E.SelIndex) ? FString::Printf(TEXT("out %.0f m %s"), Segs[E.SelIndex].Direct / 100.f, CireRouteEditor::ReachLabel(Segs[E.SelIndex].Reach)) : FString(TEXT("goal"));
        Selected = FString::Printf(TEXT("WAYPOINT %d  (%.0f, %.0f)  |  %s  |  %s"), E.SelIndex, Points[E.SelIndex].X, Points[E.SelIndex].Y, *In, *Out);
    }
    else if (E.SelKind == 2) { const FVector2D B = CireLanePath::BayPoint(Draft, E.Team, E.SelIndex + 1); Selected = FString::Printf(TEXT("CHALLENGE BAY %d  (%.0f, %.0f)  |  %s"), E.SelIndex + 1, B.X, B.Y, CireRouteEditor::ReachLabel(V.BayReach[E.Team].IsValidIndex(E.SelIndex) ? V.BayReach[E.Team][E.SelIndex] : ECireRouteReach::Unknown)); }
    else if (E.SelKind == 3) Selected = FString::Printf(TEXT("CASTLE GOAL ZONE  (%.0f, %.0f)  %.0f x %.0f cm"), Draft.GoalCenter.X, Draft.GoalCenter.Y, Draft.GoalSize.X, Draft.GoalSize.Y);
    Painter().Rect(L, Y - 2, W, 30, FLinearColor(0, 0, 0, .35f));
    Wrapped(Selected, L + 4, Y, W - 8, 8.5f, Parchment, 2); Y += 34;
    // Edit actions.
    const bool bPoint = E.SelKind == 1 && Points.IsValidIndex(E.SelIndex);
    if (Button(TEXT("INSERT AFTER"), L, Y, 102, TEXT("Insert a waypoint halfway to the next one (Ins)."), bPoint && E.SelIndex + 1 < Points.Num()) ||
        (bPoint && PlayerOwner->WasInputKeyJustPressed(EKeys::Insert)))
    { const int32 New = CireRouteEditor::InsertAfter(Draft, E.Team, E.SelIndex, E.bLinked); if (New != INDEX_NONE) { E.SelIndex = New; E.bDirty = true; } }
    if (Button(TEXT("DELETE"), L + 106, Y, 76, TEXT("Delete the selected waypoint (Del). The breach and goal points stay."), bPoint && E.SelIndex > 0 && E.SelIndex + 1 < Points.Num() && Points.Num() > 3, false, Red) ||
        (bPoint && PlayerOwner->WasInputKeyJustPressed(EKeys::Delete)))
    { if (CireRouteEditor::Delete(Draft, E.Team, E.SelIndex, E.bLinked)) { E.SelIndex = FMath::Max(1, E.SelIndex - 1); E.bDirty = true; } }
    if (Button(E.bLinked ? TEXT("REALMS LINKED") : TEXT("PER REALM"), L + 186, Y, 130, TEXT("Linked: every edit is mirrored to the other realm (both towns keep the same route). Per realm: edit Ember and Dusk separately (Tab switches realm)."), true, E.bLinked, E.bLinked ? Teal : Gold))
    {
        E.bLinked = !E.bLinked;
        if (E.bLinked) { Draft.LocalPoints[1 - E.Team] = Draft.LocalPoints[E.Team]; Draft.Bays[1 - E.Team] = Draft.Bays[E.Team]; E.bDirty = true; }
    }
    Y += 28;
    if (Button(TEXT("EMBER"), L, Y, 76, TEXT("Edit the Ember realm (Tab)."), true, E.Team == 0)) { if (E.Team != 0) E.Focus += FVector(CireLanePath::RealmOrigin(0) - CireLanePath::RealmOrigin(1), 0); E.Team = 0; }
    if (Button(TEXT("DUSK"), L + 80, Y, 76, TEXT("Edit the Dusk realm (Tab)."), true, E.Team == 1)) { if (E.Team != 1) E.Focus += FVector(CireLanePath::RealmOrigin(1) - CireLanePath::RealmOrigin(0), 0); E.Team = 1; }
    if (Button(E.bShowNav ? TEXT("NAVMESH ON") : TEXT("NAVMESH OFF"), L + 160, Y, 100, TEXT("Engine navmesh debug draw in the world (green = walkable). The minimap overlay follows it."), true, E.bShowNav, Teal))
    { E.bShowNav = !E.bShowNav; SetNavigationShowFlag(World, E.bShowNav); }
    if (Button(TEXT("FOCUS"), L + 264, Y, 52, TEXT("Centre the camera on the selection."), E.SelKind != 0))
    {
        if (E.SelKind == 1 && Points.IsValidIndex(E.SelIndex)) E.Focus = WorldOf(E.Team, Points[E.SelIndex], 0);
        else if (E.SelKind == 2) E.Focus = WorldOf(E.Team, CireLanePath::BayPoint(Draft, E.Team, E.SelIndex + 1), 0);
        else if (E.SelKind == 3) E.Focus = WorldOf(E.Team, Draft.GoalCenter, 0);
    }
    Y += 36;
    // Stepper: [-] value [+].
    auto Step = [&](const FString& Caption, float& Value, float Delta, float Min, float Max, float BX, float BY, float BW, const FString& Help)
    {
        Label(Caption, BX, BY - 12, 8, Muted);
        bool bChanged = false;
        if (Button(TEXT("-"), BX, BY, 20, Help)) { Value = FMath::Clamp(Value - Delta, Min, Max); bChanged = true; }
        const FString Text = FString::Printf(TEXT("%.0f cm"), Value);
        Painter().Rect(BX + 22, BY, BW - 44, 22, FLinearColor(0, 0, 0, .45f));
        Label(Text, BX + BW * .5f - TextWidth(Text, 9.5f) * .5f, BY + 5, 9.5f, Parchment);
        if (Button(TEXT("+"), BX + BW - 20, BY, 20, Help)) { Value = FMath::Clamp(Value + Delta, Min, Max); bChanged = true; }
        return bChanged;
    };
    float LaneWidth = Draft.LaneWidth, GoalDepth = Draft.GoalSize.X, GoalWidth = Draft.GoalSize.Y;
    if (Step(TEXT("LANE WIDTH"), LaneWidth, 20, 360, 1000, L, Y, 100, TEXT("Road width. Town pieces within half of it + 70 cm of the route are removed on apply (route clearance)."))) { Draft.LaneWidth = LaneWidth; E.bDirty = true; }
    if (Step(TEXT("GOAL DEPTH"), GoalDepth, 50, 300, 1600, L + 108, Y, 100, TEXT("Leak zone depth along the road (x)."))) { Draft.GoalSize.X = GoalDepth; E.bDirty = true; }
    if (Step(TEXT("GOAL WIDTH"), GoalWidth, 100, 400, 2 * Live.HalfWidth, L + 216, Y, 100, TEXT("Leak zone width across the road (y)."))) { Draft.GoalSize.Y = GoalWidth; E.bDirty = true; }
    Y += 32;
    // Commit actions.
    if (Button(TEXT("APPLY LIVE"), L, Y, 100, TEXT("Validate and apply on the server: the route, road and goal update, town pieces re-check clearance, the navmesh rebuilds and every unit re-routes."), V.bRulesOk, false, Teal))
    {
        if (CireLanePath::ApplyLive(World, Draft, &Error)) { DeveloperMessage = TEXT("Route applied live. Units re-route; the navmesh rebuilds around re-placed pieces."); E.bDirty = true; }
        else DeveloperMessage = Error;
    }
    if (Button(TEXT("SAVE JSON"), L + 104, Y, 100, TEXT("Validate and write Content/Data/BattlefieldRoutes.json.")))
        DeveloperMessage = CireLanePath::SaveFile(Draft, &Error) ? TEXT("Saved Content/Data/BattlefieldRoutes.json.") : Error;
    if (Button(TEXT("LOAD JSON"), L + 208, Y, 108, TEXT("Load BattlefieldRoutes.json into the draft. Apply to activate.")))
    { FCireBattlefieldRoutes Loaded; if (CireLanePath::LoadFile(Loaded, &Error)) { Draft = Loaded; E.bDirty = true; DeveloperMessage = TEXT("BattlefieldRoutes.json loaded into the draft."); } else DeveloperMessage = Error; }
    Y += 26;
    if (Button(TEXT("DEFAULTS"), L, Y, 100, TEXT("Reset the draft to the authored town route (breach, gate, market, lanes, square, castle).")))
    { const auto Bounds = Draft; Draft = CireLanePath::TownDefaults(); Draft.MinX = Bounds.MinX; Draft.MaxX = Bounds.MaxX; Draft.HalfWidth = Bounds.HalfWidth; E.bDirty = true; DeveloperMessage = TEXT("Draft reset to the town defaults. Apply to activate."); }
    if (Button(TEXT("REVERT"), L + 104, Y, 100, TEXT("Discard the draft and reload the live route."))) { Draft = Live; E.bDirty = true; }
    if (Button(TEXT("EXIT"), L + 208, Y, 108, TEXT("Leave the path editor (Esc). The draft is kept for next time."), true, false, Red)) { OpenRouteEditor(false); return; }
    Y += 34;
    // Legend + navigation stats.
    const ECireRouteReach Legend[] = {ECireRouteReach::Direct, ECireRouteReach::Detour, ECireRouteReach::Partial, ECireRouteReach::None};
    for (int32 K = 0; K < 4; ++K)
    {
        const float LX = L + K * 80;
        Painter().Rect(LX, Y + 4, 14, 4, CireRouteEditor::ReachColor(Legend[K]));
        Label(CireRouteEditor::ReachLabel(Legend[K]), LX + 18, Y, 8.5f, Muted);
    }
    Y += 18;
    const FCireNavStats& S = CireNav::Stats(World);
    Wrapped(FString::Printf(TEXT("Navmesh: %d / %d tiles (hero / large), built in %.0f ms, %d rebuilds (last %.0f ms). Paths: %d queries, %.3f ms avg. Validation %.1f ms."),
        S.Tiles[0], S.Tiles[1], S.InitialBuildMs, S.Rebuilds, S.LastRebuildMs, S.Queries, S.Queries ? S.QueryMs / S.Queries : 0.0, V.Ms), L, Y, W, 8.f, Muted, 3);
    Y += 36;
    Wrapped(TEXT("WASD pan  |  Q/E rotate  |  R/F tilt  |  wheel zoom  |  Tab realm  |  Ins/Del  |  Esc exit"), L, Y, W, 8.f, Gold, 2);
    if (!DeveloperMessage.IsEmpty()) Wrapped(DeveloperMessage, L, Bar.Y + Bar.H + 8, W + 60, 9.f, Gold, 3);
#endif
}

void ACireHUD::DrawRouteMinimap(int32 Team, TFunctionRef<FVector2D(FVector, int32)> Map)
{
    UWorld* World = GetWorld();
    const bool bEditor = bRouteEditor && RouteEditor.IsValid();
    if ((bEditor && RouteEditor->bShowNav) || bMinimapNav)
    {
        // Navmesh coverage (sampled every metre), drawn as merged runs per row.
        const CireNav::FCoverage& C = CireNav::RealmCoverage(World, Team);
        for (int32 Row = 0; Row < C.H; ++Row)
        {
            const float WY = C.Bounds.Min.Y + (Row + .5f) * C.Cell;
            for (int32 X = 0; X < C.W;)
            {
                if (!C.Cells[Row * C.W + X]) { ++X; continue; }
                int32 End = X;
                while (End + 1 < C.W && C.Cells[Row * C.W + End + 1]) ++End;
                const FVector2D A = Map(FVector(C.Bounds.Min.X + X * C.Cell, WY, 0), Team), B = Map(FVector(C.Bounds.Min.X + (End + 1) * C.Cell, WY, 0), Team);
                Line(A.X, A.Y, B.X, B.Y, FLinearColor(.2f, .75f, .35f, .45f), 3.4f);
                X = End + 1;
            }
        }
    }
    if (!bEditor) return;
    const FCireRouteEditorState& E = *RouteEditor;
    const auto& Points = E.Draft.LocalPoints[Team];
    for (int32 I = 0; I + 1 < Points.Num(); ++I)
    {
        const FVector2D A = Map(WorldOf(Team, Points[I]), Team), B = Map(WorldOf(Team, Points[I + 1]), Team);
        const auto* Check = E.Validation.Segments[Team].IsValidIndex(I) ? &E.Validation.Segments[Team][I] : nullptr;
        Line(A.X, A.Y, B.X, B.Y, CireRouteEditor::ReachColor(Check ? Check->Reach : ECireRouteReach::Unknown), 2.f);
    }
    for (int32 I = 0; I < Points.Num(); ++I)
    {
        const FVector2D P = Map(WorldOf(Team, Points[I]), Team);
        const bool bSel = Team == E.Team && E.SelKind == 1 && E.SelIndex == I;
        Panel(P.X - (bSel ? 2.5f : 1.5f), P.Y - (bSel ? 2.5f : 1.5f), bSel ? 5 : 3, bSel ? 5 : 3, bSel ? Parchment : Gold);
    }
    // Camera footprint on the minimap.
    if (Team == E.Team) { const FVector2D F = Map(E.Focus, Team); Circle(F.X, F.Y, 4, Parchment, 1.f, 12); }
}

void ACireHUD::DrawRoutePage(float X, float Y)
{
#if !UE_BUILD_SHIPPING
    UWorld* World = GetWorld();
    if (!RouteEditor.IsValid()) RouteEditor = MakeShared<FCireRouteEditorState>();
    FCireRouteEditorState& E = *RouteEditor;
    if (!E.bLoaded) { E.Draft = CireLanePath::Get(World); E.bLoaded = true; E.bDirty = true; }
    if (E.bDirty || E.ValidatedNavRevision != CireNav::Stats(World).NavRevision)
    { E.Validation = CireRouteEditor::Validate(World, E.Draft); E.bDirty = false; E.ValidatedNavRevision = CireNav::Stats(World).NavRevision; E.ValidatedAt = World->GetRealTimeSeconds(); }
    auto Button = [&](const FString& Title, float BX, float BY, float W, const FString& Help, bool bSelected = false, FLinearColor Accent = Gold)
    {
        const bool Over = Hit(BX, BY, W, 25);
        CireUIStyle::Button(Painter(), BX, BY, W, 25, Title, bSelected ? ECireButtonState::Selected : Over ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 9.5f);
        Tip(Title, Help, BX, BY, W, 25);
        if (Over && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    const float L = X, T = Y + 48;
    const auto& Live = CireLanePath::Get(World);
    const FCireNavStats& S = CireNav::Stats(World);
    CireUIStyle::Header(Painter(), L, T - 4, 600, TEXT("BATTLEFIELD PATHS AND NAVIGATION"), Gold, 10.f);
    Label(FString::Printf(TEXT("Live route revision %u  |  %d / %d waypoints  |  lane %.0f cm  |  goal %.0f x %.0f cm at (%.0f, %.0f)"), CireLanePath::Revision(World),
        Live.LocalPoints[0].Num(), Live.LocalPoints[1].Num(), Live.LaneWidth, Live.GoalSize.X, Live.GoalSize.Y, Live.GoalCenter.X, Live.GoalCenter.Y), L, T + 22, 9.5f, Parchment);
    Label(FString::Printf(TEXT("Navmesh: %s  |  hero %d tiles, large %d tiles  |  initial build %.0f ms  |  %d rebuilds (last %.0f ms)"),
        CireNav::HasNavigation(World) ? (CireNav::IsReady(World) ? TEXT("ready") : TEXT("rebuilding")) : TEXT("none on this peer"), S.Tiles[0], S.Tiles[1], S.InitialBuildMs, S.Rebuilds, S.LastRebuildMs), L, T + 40, 9.5f, Teal);
    Label(FString::Printf(TEXT("Paths: %d queries (%.3f ms avg, %.3f ms peak), %d partial, %d failed, %d straight-line fallbacks, %d unsticks"),
        S.Queries, S.Queries ? S.QueryMs / S.Queries : 0.0, S.PeakQueryMs, S.Partial, S.Failed, S.Fallbacks, S.Unsticks), L, T + 58, 9.5f, Muted);
    const FLinearColor Summary = !E.Validation.bRulesOk ? Red : E.Validation.Unreachable > 0 ? Orange : Teal;
    Label(TEXT("Draft: ") + E.Validation.Summary(), L, T + 80, 10.f, Summary);
    // Per-segment table for the draft (Ember realm; per-realm edits show in the editor).
    for (int32 Team = 0; Team < 2; ++Team)
    {
        const float CX = L + Team * 300;
        Label(Team == 0 ? TEXT("EMBER SEGMENTS") : TEXT("DUSK SEGMENTS"), CX, T + 102, 8.5f, Gold);
        const auto& Segs = E.Validation.Segments[Team];
        for (int32 I = 0; I < Segs.Num() && I < 12; ++I)
        {
            const float RY = T + 116 + I * 15;
            Painter().Rect(CX + 2, RY + 4, 10, 4, CireRouteEditor::ReachColor(Segs[I].Reach));
            Label(FString::Printf(TEXT("%d-%d  %.0f m  %s%s"), I, I + 1, Segs[I].Direct / 100.f, CireRouteEditor::ReachLabel(Segs[I].Reach),
                Segs[I].PropConflicts ? *FString::Printf(TEXT("  |  %d pieces in lane"), Segs[I].PropConflicts) : TEXT("")), CX + 16, RY, 8.5f, Parchment);
        }
    }
    const float B = Y + 350;
    if (Button(TEXT("OPEN PATH EDITOR"), L, B, 180, TEXT("Top-down editor over the realm: drag waypoints, the breach, bays and the goal zone on the ground; live navmesh validation."), false, Teal)) { OpenRouteEditor(true); return; }
    const bool bNav = NavigationShowFlag(World);
    if (Button(bNav ? TEXT("WORLD NAVMESH: ON") : TEXT("WORLD NAVMESH: OFF"), L + 186, B, 170, TEXT("Engine navmesh debug draw in the game view."), bNav)) SetNavigationShowFlag(World, !bNav);
    if (Button(bMinimapNav ? TEXT("MINIMAP NAV: ON") : TEXT("MINIMAP NAV: OFF"), L + 362, B, 150, TEXT("Draw navmesh coverage on the minimap."), bMinimapNav)) bMinimapNav = !bMinimapNav;
    // dev-route-tools: the map layout editor (setters, team-owned mirrored markers). RouteEditor.cmd opens it as a clean edit mode.
    if (Button(TEXT("MAP LAYOUT EDITOR"), L + 518, B, 170, TEXT("Author spawns, monster paths, challenge packs, vendors, the objective and more; RouteEditor.cmd opens it with every game system paused."), false, Teal)) { OpenLayoutEditor(true); return; }
    FString Error;
    if (Button(TEXT("APPLY DRAFT"), L, B + 30, 120, TEXT("Apply the draft live (server-authoritative)."), false, Teal))
        DeveloperMessage = CireLanePath::ApplyLive(World, E.Draft, &Error) ? TEXT("Route applied live.") : Error;
    if (Button(TEXT("SAVE JSON"), L + 126, B + 30, 110, TEXT("Write the draft to Content/Data/BattlefieldRoutes.json.")))
        DeveloperMessage = CireLanePath::SaveFile(E.Draft, &Error) ? TEXT("Saved Content/Data/BattlefieldRoutes.json.") : Error;
    if (Button(TEXT("LOAD JSON"), L + 242, B + 30, 110, TEXT("Load BattlefieldRoutes.json into the draft.")))
    { FCireBattlefieldRoutes Loaded; if (CireLanePath::LoadFile(Loaded, &Error)) { E.Draft = Loaded; E.bDirty = true; DeveloperMessage = TEXT("Loaded into the draft."); } else DeveloperMessage = Error; }
    if (Button(TEXT("DEFAULTS"), L + 358, B + 30, 110, TEXT("Reset the draft to the authored town route.")))
    { const auto Bounds = E.Draft; E.Draft = CireLanePath::TownDefaults(); E.Draft.MinX = Bounds.MinX; E.Draft.MaxX = Bounds.MaxX; E.Draft.HalfWidth = Bounds.HalfWidth; E.bDirty = true; }
    if (Button(TEXT("REVERT"), L + 474, B + 30, 110, TEXT("Discard the draft (reload the live route)."))) { E.Draft = Live; E.bDirty = true; }
#endif
}
