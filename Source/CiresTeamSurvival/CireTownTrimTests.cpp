// town-trim: native checks for the Play Bounds trim (CireTownTrim.h). Part of -CireCombatExpansionProbe; logs
// CIRE_TOWN_TRIM_TESTS_PASS / _FAIL. Pure geometry plus a small world fixture (the probe runs the procedural town, whose
// realm frames are (0, -/+2100): the same code maps a realm-local polygon into either realm).
#include "CireTownTrim.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMapLayout.h"
#include "CireNavCache.h"
#include "CireTownMap.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireTownTrimTests, Log, All);

namespace
{
struct FChecker
{
    int32 Count = 0; bool bPass = true;
    void operator()(bool bValue, const FString& Why) { ++Count; if (!bValue) { bPass = false; UE_LOG(LogCireTownTrimTests, Error, TEXT("CIRE_TOWN_TRIM_CHECK_FAIL %s"), *Why); } }
};
FBox2D Box(double X0, double Y0, double X1, double Y1) { return FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)); }
const TCHAR* Name(ECireTrimClass C) { return C == ECireTrimClass::Inside ? TEXT("inside") : C == ECireTrimClass::Outside ? TEXT("outside") : TEXT("straddling"); }
}

bool CireTownTrim::RunTests(ACireGameMode* Mode)
{
    FChecker Check;
    ON_SCOPE_EXIT { SetPolygonOverride(nullptr); };
    using C = ECireTrimClass;
    // ---- point in polygon, with margin ---------------------------------------------------------------------------------
    const TArray<FVector2D> Square = {{-1000, -1000}, {1000, -1000}, {1000, 1000}, {-1000, 1000}};
    // An L: the square minus its north-east quarter (a concave notch).
    const TArray<FVector2D> Ell = {{-1000, -1000}, {1000, -1000}, {1000, 0}, {0, 0}, {0, 1000}, {-1000, 1000}};
    Check(InsidePolygon(Square, {0, 0}) && InsidePolygon(Square, {990, -990}), TEXT("points inside the square"));
    Check(!InsidePolygon(Square, {1010, 0}) && !InsidePolygon(Square, {0, -1200}), TEXT("points outside the square"));
    Check(InsidePolygon(Ell, {-500, 500}) && !InsidePolygon(Ell, {500, 500}), TEXT("the L's notch is outside"));
    Check(FMath::IsNearlyEqual(EdgeDistance(Square, {1300, 0}), 300.0, 1.0) && FMath::IsNearlyEqual(EdgeDistance(Square, {0, 0}), 1000.0, 1.0), TEXT("edge distance"));
    Check(WithinMargin(Square, {1100, 0}, 150.f) && !WithinMargin(Square, {1200, 0}, 150.f), TEXT("margin: 1 m past the edge loads with a 1.5 m margin, 2 m does not"));
    Check(WithinMargin(Ell, {100, 100}, 150.f) && !WithinMargin(Ell, {500, 500}, 150.f), TEXT("margin inside the L's notch"));
    Check(WithinMargin(TArray<FVector2D>(), {1e6, 1e6}, 0.f), TEXT("no polygon: everything is within"));

    // ---- actor / cell classification: inside, straddling, outside ------------------------------------------------------
    Check(Classify(Square, Box(-200, -200, 200, 200), 150.f) == C::Inside, TEXT("a box in the middle is inside"));
    Check(Classify(Square, Box(900, -100, 1100, 100), 150.f) == C::Straddling, TEXT("a box across the edge straddles"));
    Check(Classify(Square, Box(1100, -100, 1300, 100), 150.f) == C::Straddling, TEXT("a box 1 m past the edge is within the margin (loads)"));
    Check(Classify(Square, Box(1200, -100, 1400, 100), 150.f) == C::Outside, TEXT("a box 2 m past the edge is outside a 1.5 m margin"));
    Check(Classify(Square, Box(1200, -100, 1400, 100), 0.f) == C::Outside && Classify(Square, Box(1001, -100, 1400, 100), 0.f) == C::Outside, TEXT("no margin: just past the edge is outside"));
    Check(Classify(Square, Box(-5000, -5000, 5000, 5000), 0.f) == C::Straddling, TEXT("a box around the whole polygon straddles (the landscape)"));
    Check(Classify(Ell, Box(300, 300, 700, 700), 150.f) == C::Outside, TEXT("a box in the L's notch is outside"));
    Check(Classify(Ell, Box(-600, -600, 600, 600), 0.f) == C::Straddling, TEXT("a box over the notch corner straddles"));
    Check(Classify(Ell, Box(-900, 100, -100, 900), 0.f) == C::Inside, TEXT("a box in the L's arm is inside"));
    // A thin sliver crossing the polygon with no corner inside and no vertex in the box.
    Check(Classify(Square, Box(-2000, -10, 2000, 10), 0.f) == C::Straddling, TEXT("a wall crossing the polygon straddles"));
    Check(Classify(TArray<FVector2D>(), Box(1e6, 1e6, 2e6, 2e6), 0.f) == C::Inside, TEXT("no bounds: nothing is outside"));

    // ---- realm mirroring: one realm-local polygon cuts both realms the same ---------------------------------------------
    {
        const FVector2D O0 = CireLanePath::RealmOrigin(0), O1 = CireLanePath::RealmOrigin(1);
        Check(FVector2D::Distance(O0, O1) > 100.0, TEXT("two distinct realm origins"));
        const TArray<FVector2D> Local = {{-600, -600}, {600, -600}, {600, 600}, {-600, 600}};
        struct FCase { FBox2D B; C Want; };
        const FCase Cases[] = {{Box(-100, -100, 100, 100), C::Inside}, {Box(550, -50, 650, 50), C::Straddling}, {Box(-1000, -1000, -900, -900), C::Outside}, {Box(900, 0, 1000, 100), C::Outside}};
        for (const FCase& K : Cases)
        {
            ECireTrimClass Got[2];
            for (int32 Realm = 0; Realm < 2; ++Realm)
            {
                const FVector2D O = Realm == 0 ? O0 : O1;
                Got[Realm] = ClassifyWorld(Local, FBox(FVector(K.B.Min + O, 0), FVector(K.B.Max + O, 300)), 150.f);
            }
            Check(Got[0] == K.Want && Got[1] == K.Want, FString::Printf(TEXT("mirrored box %s: realm0=%s realm1=%s want %s"), *K.B.ToString(), Name(Got[0]), Name(Got[1]), Name(K.Want)));
        }
    }

    // ---- nav bounds raster ------------------------------------------------------------------------------------------------
    {
        const TArray<FBox2D> Boxes = RasterBoxes(Ell, 150.f, 400.f, FBox2D(ForceInit));
        bool bCovers = true;
        for (const FVector2D& P : {FVector2D(-900, -900), FVector2D(900, -500), FVector2D(-500, 900), FVector2D(1100, -500)})
        { bool bIn = false; for (const FBox2D& B : Boxes) bIn |= B.IsInside(P); bCovers &= bIn; }
        bool bNotch = false; for (const FBox2D& B : Boxes) bNotch |= B.IsInside(FVector2D(700, 700));
        double Area = 0; for (const FBox2D& B : Boxes) Area += B.GetArea();
        Check(bCovers && !bNotch && Boxes.Num() > 0 && Boxes.Num() <= 48, FString::Printf(TEXT("nav boxes cover the L plus margin and skip its notch (%d boxes)"), Boxes.Num()));
        Check(Area < 2300.0 * 2300.0, FString::Printf(TEXT("nav boxes are tighter than the polygon's bounding box (%.0f m2)"), Area / 1e4));
    }

    // ---- settings ---------------------------------------------------------------------------------------------------------
    {
        const FCireTrimSettings S = ParseSettings(TEXT("{\"trim\": {\"margin\": 2200, \"backdrop\": \"off\", \"landmarkHeight\": 900, \"backdropLevels\": [\"SL_X\"]}}"));
        Check(FMath::IsNearlyEqual(S.Margin, 2200.f) && S.Backdrop == ECireTrimBackdrop::Off && FMath::IsNearlyEqual(S.LandmarkHeight, 900.f) && S.BackdropLevels == TArray<FString>{TEXT("SL_X")}, TEXT("trim settings parse"));
        const FCireTrimSettings D = ParseSettings(TEXT("{}"));
        Check(D.bEnabled && D.Backdrop == ECireTrimBackdrop::Low && FMath::IsNearlyEqual(D.Margin, 1500.f), TEXT("trim defaults: on, low backdrop, 15 m margin"));
    }

    // ---- no bounds means no trim; the nav cache key carries the bounds ---------------------------------------------------
    {
        const TArray<FVector2D> None;
        SetPolygonOverride(&None);
        const FString KeyNone = CireNavCache::Key();
        TArray<FBox> Nav;
        Check(!Active() && Signature().IsEmpty() && !NavBoxes(0, FBox(FVector(-1), FVector(1)), Nav), TEXT("no bounds: no trim, no signature, the realm's own nav bounds"));
        SetPolygonOverride(&Square);
        const FString KeyA = CireNavCache::Key();
        const FVector2D R1 = CireLanePath::RealmOrigin(1);
        const FBox Realm1(FVector(R1 - FVector2D(5000), -500), FVector(R1 + FVector2D(5000), 900));
        Check(Active() && !Signature().IsEmpty() && NavBoxes(1, Realm1, Nav) && Nav.Num() > 0 && FMath::IsNearlyEqual(Nav[0].Min.Z, -500.0), TEXT("bounds: trim on, nav boxes in the realm's height range"));
        {
            TArray<FBox> Small;
            const FBox Tight(FVector(R1 - FVector2D(500), -500), FVector(R1 + FVector2D(500), 900));
            bool bClipped = NavBoxes(1, Tight, Small);
            for (const FBox& B : Small) bClipped &= Tight.ExpandBy(1.0).IsInsideOrOn(B.Min) && Tight.ExpandBy(1.0).IsInsideOrOn(B.Max);
            Check(bClipped, TEXT("the trim never adds navmesh: nav boxes stay inside the realm's own nav box"));
        }
        {
            FBox2D All(ForceInit); for (const FBox& B : Nav) All += FBox2D(FVector2D(B.Min), FVector2D(B.Max));
            const FVector2D O = CireLanePath::RealmOrigin(1);
            Check(All.IsInside(O) && All.IsInside(O + FVector2D(1100, 0)) && !All.IsInside(O + FVector2D(2700, 0)), TEXT("realm 1 nav boxes sit on realm 1's frame"));
        }
        TArray<FVector2D> Moved = Square; Moved[2] = FVector2D(1200, 1000); // one corner dragged
        SetPolygonOverride(&Moved);
        const FString KeyB = CireNavCache::Key();
        Check(KeyA != KeyNone && KeyB != KeyA, TEXT("the nav cache key changes when the bounds are drawn or edited"));
        SetPolygonOverride(&None);
        Check(CireNavCache::Key() == KeyNone, TEXT("removing the bounds restores the untrimmed key"));
    }

    // ---- validate: markers near the edge -----------------------------------------------------------------------------------
    {
        namespace ML = CireMapLayout;
        FCireMapLayout L; L.Map = ML::ActiveMap();
        FString Bounds;
        for (const FVector2D& P : {FVector2D(-10000, -10000), FVector2D(10000, -10000), FVector2D(10000, 10000), FVector2D(-10000, 10000)})
            Bounds = ML::ChainPoint(L, ML::PlayBounds, Bounds, P, ECireMarkerOwner::Shared);
        const FString Near = ML::Place(L, ML::ChallengePack, FVector2D(9500, 0), ECireMarkerOwner::Team1);
        const FString Far = ML::Place(L, ML::ChallengePack, FVector2D(0, 0), ECireMarkerOwner::Team1);
        const TArray<FCireLayoutIssue> Issues = EdgeIssues(L);
        bool bNear = false, bFar = false, bError = false;
        for (const FCireLayoutIssue& I : Issues) { bNear |= I.MarkerId == Near; bFar |= I.MarkerId == Far; bError |= I.bError; }
        Check(bNear && !bFar && !bError, TEXT("validate notes a pack within the trim margin of the edge (a note, not an error)"));
        FCireMapLayout NoBounds; ML::Place(NoBounds, ML::ChallengePack, FVector2D(9500, 0), ECireMarkerOwner::Team1);
        Check(EdgeIssues(NoBounds).IsEmpty() && LayoutPolygon(NoBounds).IsEmpty(), TEXT("no bounds: no edge notes"));
    }

    // ---- world fixture: real actors classified in both realms -------------------------------------------------------------
    if (UWorld* World = Mode ? Mode->GetWorld() : nullptr)
    {
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        const TArray<FVector2D> Local = {{-800, -800}, {800, -800}, {800, 800}, {-800, 800}};
        TArray<AActor*> Spawned;
        ON_SCOPE_EXIT { for (AActor* A : Spawned) if (IsValid(A)) A->Destroy(); };
        struct FCase { FVector2D At; float Scale; C Want; };
        const FCase Cases[] = {{{0, 0}, 1.f, C::Inside}, {{800, 0}, 2.f, C::Straddling}, {{1500, 1500}, 1.f, C::Outside}};
        for (int32 Realm = 0; Realm < 2 && Cube; ++Realm)
            for (const FCase& K : Cases)
            {
                FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn; Params.ObjectFlags |= RF_Transient;
                auto* A = World->SpawnActor<AStaticMeshActor>(CireLanePath::ToWorld(Realm, K.At, 5000.f), FRotator::ZeroRotator, Params);
                if (!A) continue;
                Spawned.Add(A);
                A->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
                A->GetStaticMeshComponent()->SetStaticMesh(Cube); A->SetActorScale3D(FVector(K.Scale));
                const ECireTrimClass Got = ClassifyWorld(Local, A->GetComponentsBoundingBox(true), 150.f);
                Check(Got == K.Want, FString::Printf(TEXT("realm %d actor at %s: %s, want %s"), Realm, *K.At.ToString(), Name(Got), Name(K.Want)));
            }
        Check(Cube != nullptr && Spawned.Num() == 6, TEXT("world fixture spawned"));
    }

    UE_LOG(LogCireTownTrimTests, Display, TEXT("%s checks=%d"), Check.bPass ? TEXT("CIRE_TOWN_TRIM_TESTS_PASS") : TEXT("CIRE_TOWN_TRIM_TESTS_FAIL"), Check.Count);
    return Check.bPass;
}
