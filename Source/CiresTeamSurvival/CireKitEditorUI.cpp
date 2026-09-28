// kit-editor: the HERO CREATOR screen (Champion Select > HERO CREATOR). Eric: "at the character selection screen, I can
// pick a character and select from all the spells that exist, in the same manner as buying spells in the game, and assign
// them to skill buttons as their base loadout, then save those presets."
//
//   header    : title, KIT PROFILE picker (new / copy / rename / delete), back.
//   strip     : every champion (portrait, role ring, dot = has a loadout in this profile).
//   SPELLS & BUTTONS tab : every ability as Skill Shop scroll cards in the Skill Shop's periodic-table sections, with search
//               and filters (role / class list chips, off by default) | the champion's loadout PRESETS (save, save as,
//               new, rename, delete, set default, start-with-loadout).
//   EFFECT PLACEMENT tab : the champion's real body on the draft stage with the selected ability's cast effect looping at
//               its placement, CAST PREVIEW, attach point / bone / offset / size / tint.
//   action bar: the champion's skill buttons with the player's real keybind labels (1-6, R, passive). Click a scroll to
//               fill the next free (or the selected) button, or drag it onto a button; click a button to select it,
//               right-click (or its x) to clear it.
// Every card, name and number is read from the live Ability Database each frame (the Ability Tuner can rename / retune).
// Drawn with the shared style kit and the Skill Shop's own card code (CireShopUI::DrawSkillCard). Docs/KitEditor.md.
#include "CireKitEditor.h"
#include "CireAbilityDB.h"
#include "CireAbilityIcons.h"
#include "CireAbilityTuner.h" // ability-tuner: refresh on live retunes
#include "CireAbilityTunerUI.h"
#include "CireTunerLink.h" // EDIT badge -> Ability Tuner
#include "CireAbilityVFX.h"
#include "CireChampionActions.h"
#include "CireChampionProfiles.h"
#include "CireChampionRoster.h"
#include "CireDraftStage.h"
#include "CireFabVFX.h"
#include "CireGame.h"
#include "CireHUD.h"
#include "CireKeybindings.h"
#include "CireScalingKits.h"
#include "CireShopArt.h"
#include "CireShopUI.h"
#include "CireUIStyle.h"
#include "Components/SkeletalMeshComponent.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "CireAbilityShapes.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Particles/ParticleSystem.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireKitEditorUI, Log, All);

namespace
{
using namespace CireUIColors;
using CireKitEditor::EKind;

struct FKitAnchorChip { const TCHAR* Key; const TCHAR* Label; };
const FKitAnchorChip KitAnchorChips[] = {
    {TEXT(""), TEXT("DEFAULT")}, {TEXT("root"), TEXT("FEET")}, {TEXT("pelvis"), TEXT("PELVIS")}, {TEXT("chest"), TEXT("CHEST")},
    {TEXT("head"), TEXT("HEAD")}, {TEXT("hand_r"), TEXT("R HAND")}, {TEXT("hand_l"), TEXT("L HAND")}, {TEXT("foot_r"), TEXT("R FOOT")},
};
const FLinearColor KitSwatches[] = {
    FLinearColor(1.f, .32f, .12f, 1), FLinearColor(1.f, .72f, .18f, 1), FLinearColor(.98f, .95f, .55f, 1), FLinearColor(.35f, 1.f, .38f, 1),
    FLinearColor(.18f, .92f, .88f, 1), FLinearColor(.25f, .5f, 1.f, 1), FLinearColor(.62f, .32f, 1.f, 1), FLinearColor(1.f, .3f, .78f, 1),
    FLinearColor(.9f, .08f, .1f, 1), FLinearColor(.92f, .94f, 1.f, 1),
};

enum class EKitName : uint8 { None, SaveAs, Rename, NewProfile, CopyProfile, RenameProfile };

struct FKitEditorState
{
    bool bOpen = false;
    int32 Tab = 0;                           // 0 spells & buttons, 1 effect placement
    FString Profile = CireKitEditor::StandardProfile;
    FString Champion;
    FCireKitLoadout Work, Base;              // working copy + what is saved (dirty check); Work.Name "" = not saved yet
    bool bGrant = true, bGrantSaved = true;
    TMap<FString, FCireKitEffectPlacement> Effects, EffectsSaved;
    int32 SelectedSlot = INDEX_NONE;
    FString Selected;                        // ability whose effect is being placed
    CireKitEditor::FPoolFilter Filter;
    bool bOnlyClass = false;
    int32 PoolRow = 0, StripFirst = 0, PresetFirst = 0, BoneCursor = -1;
    TMap<FString, float> Lift;
    FString PressId;                         // drag and drop
    FVector2D PressAt = FVector2D::ZeroVector;
    bool bDragging = false;
    TWeakObjectPtr<ACireDraftStage> Stage;   // preview
    TWeakObjectPtr<UFXSystemComponent> Live;
    FString LiveKey;
    double LiveAt = -100, CastReleaseAt = -1;
    float Yaw = -20.f;
    bool bRotating = false;
    float RotateFromX = 0, RotateFromYaw = 0;
    int32 DragSlider = -1;
    EKitName Naming = EKitName::None;        // naming modal
    FString StashSearch;
    FString Status;                          // feedback
    FLinearColor StatusColor = Parchment;
    double StatusAt = -100;
    FString Confirm;                         // a destructive action waiting for its second click
    FString SavedSearch;                     // champion-select search, restored on close
    bool bGallery = false, bGalleryDone = false;
    double GalleryStart = 0, GalleryShotAt = 0;
    int32 GalleryStage = 0;
    int32 PlaceMode = 0;                     // effect placement tab: 0 cast effect, 1 projectile muzzle
    bool bMuzzleSpell = false;               // muzzle page: edit the selected spell's own override (else the champion's "*")
    TMap<FString, FCireKitMuzzle> Muzzles, MuzzlesSaved; // this champion's muzzles (key "*" or ability id)
    TWeakObjectPtr<UFXSystemComponent> Shot; // looping test projectile
    TWeakObjectPtr<UStaticMeshComponent> Marker, Ball; // muzzle marker + test ball (no projectile art installed)
    FString ShotKey;
    double ShotAt = -100;
    uint32 TunerVersion = 0;                 // CireAbilityTuner::Version() the screen last refreshed for
    bool bTunerDirty = false;                // set by the OnChanged subscription
};
TMap<TWeakObjectPtr<const ACireHUD>, FKitEditorState> GKitEditorStates;
FKitEditorState& KitState(const ACireHUD* HUD)
{
    for (auto It = GKitEditorStates.CreateIterator(); It; ++It) if (!It.Key().IsValid()) It.RemoveCurrent();
    return GKitEditorStates.FindOrAdd(HUD);
}

FLinearColor KitKindColor(EKind K)
{
    return K == EKind::Ultimate ? FLinearColor(.78f, .56f, 1.f, 1) : K == EKind::Passive ? FLinearColor(.84f, .80f, .68f, 1) : Gold;
}
FLinearColor KitRoleColor(const FCireChampionProfile& P)
{
    const auto Role = CireChampionProfiles::PrimaryRole(P);
    return Role == Cires::SkillDraftRole::Tank ? FLinearColor(.36f, .58f, .89f, 1) : Role == Cires::SkillDraftRole::Support ? FLinearColor(.35f, .78f, .49f, 1) : FLinearColor(.85f, .31f, .25f, 1);
}
void KitBorder(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor C, float T = 1.f)
{
    P.Rect(X, Y, W, T, C); P.Rect(X, Y + H - T, W, T, C); P.Rect(X, Y, T, H, C); P.Rect(X + W - T, Y, T, H, C);
}
void KitDrawTarget(const FCireUIPainter& P, UTextureRenderTarget2D* Target, float X, float Y, float W, float H)
{
    if (!P.Canvas || !Target || !Target->GetResource()) return;
    FCanvasTileItem Item(P.ToScreen(X, Y), Target->GetResource(), FVector2D(W * P.Scale, H * P.Scale), FLinearColor::White);
    Item.BlendMode = SE_BLEND_Opaque;
    P.Canvas->DrawItem(Item);
}
void KitStatus(FKitEditorState& S, const FString& Text, FLinearColor Color) { S.Status = Text; S.StatusColor = Color; S.StatusAt = FPlatformTime::Seconds(); }
const FLinearColor KitGood(.45f, 1.f, .55f, 1), KitWarn(1.f, .66f, .28f, 1), KitBad(1.f, .42f, .35f, 1);

bool KitDirty(const FKitEditorState& S)
{
    if (S.Work.Slots != S.Base.Slots || S.bGrant != S.bGrantSaved) return true;
    TMap<FString, FCireKitEffectPlacement> A = S.Effects, B = S.EffectsSaved;
    for (auto It = A.CreateIterator(); It; ++It) if (It.Value().IsDefault()) It.RemoveCurrent();
    for (auto It = B.CreateIterator(); It; ++It) if (It.Value().IsDefault()) It.RemoveCurrent();
    if (A.Num() != B.Num()) return true;
    for (const auto& P : A) { const FCireKitEffectPlacement* O = B.Find(P.Key); if (!O || !(*O == P.Value)) return true; }
    TMap<FString, FCireKitMuzzle> MA = S.Muzzles, MB = S.MuzzlesSaved;
    for (auto It = MA.CreateIterator(); It; ++It) if (It.Value().IsDefault()) It.RemoveCurrent();
    for (auto It = MB.CreateIterator(); It; ++It) if (It.Value().IsDefault()) It.RemoveCurrent();
    if (MA.Num() != MB.Num()) return true;
    for (const auto& P : MA) { const FCireKitMuzzle* O = MB.Find(P.Key); if (!O || *O != P.Value) return true; }
    return false;
}
void KitStopShot(FKitEditorState& S)
{
    if (UFXSystemComponent* Shot = S.Shot.Get()) Shot->DestroyComponent();
    if (UStaticMeshComponent* M = S.Marker.Get()) M->DestroyComponent();
    if (UStaticMeshComponent* B = S.Ball.Get()) B->DestroyComponent();
    S.Shot.Reset(); S.Marker.Reset(); S.Ball.Reset(); S.ShotKey.Reset(); S.ShotAt = -100;
}
// Muzzle preview helpers: components owned by the preview hero (the draft stage renders only its show-only actors).
UStaticMeshComponent* KitPreviewBall(TWeakObjectPtr<UStaticMeshComponent>& Slot, ACireHero* Owner, float Size, FLinearColor Color)
{
    UStaticMeshComponent* C = Slot.Get();
    if (C && C->GetOwner() != Owner) { C->DestroyComponent(); C = nullptr; }
    if (!C)
    {
        static UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
        static UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (!Sphere || !Owner) return nullptr;
        C = NewObject<UStaticMeshComponent>(Owner);
        C->SetStaticMesh(Sphere);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        C->SetCastShadow(false);
        C->SetUsingAbsoluteLocation(true); C->SetUsingAbsoluteRotation(true); C->SetUsingAbsoluteScale(true);
        C->SetupAttachment(Owner->GetRootComponent());
        C->RegisterComponent();
        if (Base) if (UMaterialInstanceDynamic* MID = C->CreateDynamicMaterialInstance(0, Base)) MID->SetVectorParameterValue(TEXT("Color"), Color);
        Slot = C;
    }
    C->SetWorldScale3D(FVector(Size / 100.f));
    return C;
}
void KitMuzzleMarkers(FKitEditorState& S, ACireHero* Owner, const FVector& Muzzle, const FVector& BallAt, bool bBall)
{
    if (UStaticMeshComponent* M = KitPreviewBall(S.Marker, Owner, 7.f, FLinearColor(.2f, 1.f, 1.f, 1))) M->SetWorldLocation(Muzzle);
    if (bBall) { if (UStaticMeshComponent* B = KitPreviewBall(S.Ball, Owner, 16.f, FLinearColor(1.f, .7f, .2f, 1))) { B->SetWorldLocation(BallAt); B->SetVisibility(true); } }
    else if (UStaticMeshComponent* B = S.Ball.Get()) B->SetVisibility(false);
}
// Loads the champion in the current profile: the named or default loadout, else (new in this profile) a copy of Standard's.
void KitLoadChampion(FKitEditorState& S, const FString& Champion, const FString& Loadout = FString())
{
    S.Champion = Champion;
    S.Work = FCireKitLoadout(); S.Base = FCireKitLoadout();
    S.bGrant = S.bGrantSaved = true;
    const FCireKitData& D = CireKitEditor::Data();
    const FCireKitProfile* P = D.FindProfile(S.Profile);
    const FCireKitChampion* C = P ? P->Champions.Find(Champion) : nullptr;
    if (C)
    {
        const FCireKitLoadout* L = Loadout.IsEmpty() ? C->Default() : C->Find(Loadout);
        if (!L) L = C->Default();
        if (L) S.Work = S.Base = *L;
        S.bGrant = S.bGrantSaved = C->bGrantOnDraft;
    }
    else if (const FCireKitLoadout* Std = CireKitEditor::ResolveLoadout(CireKitEditor::StandardProfile, Champion))
    {
        S.Work = *Std; S.Work.Name.Reset();   // unsaved in this profile: SAVE asks for a name
        KitStatus(S, FString::Printf(TEXT("No loadout in %s yet: starting from Standard's \"%s\"."), *S.Profile, *Std->Name), Muted * 1.4f);
    }
    CireKitEditor::Compact(S.Work); CireKitEditor::Compact(S.Base);
    S.Effects = S.EffectsSaved = D.Effects.Contains(Champion) ? D.Effects[Champion] : TMap<FString, FCireKitEffectPlacement>();
    S.Muzzles = S.MuzzlesSaved = D.Muzzles.Contains(Champion) ? D.Muzzles[Champion] : TMap<FString, FCireKitMuzzle>();
    KitStopShot(S);
    S.SelectedSlot = INDEX_NONE;
    const TArray<FString> Skills = S.Work.Skills();
    S.Selected = Skills.Num() ? Skills[0] : FString();
    S.Confirm.Reset(); S.BoneCursor = -1; S.PresetFirst = 0;
    S.LiveKey.Reset();
    if (UFXSystemComponent* Live = S.Live.Get()) Live->DestroyComponent();
    S.Live.Reset();
}
bool KitSaveData(FKitEditorState& S, const FCireKitData& D)
{
    FString Error;
    if (!CireKitEditor::Save(D, &Error)) { KitStatus(S, Error, KitBad); return false; }
    return true;
}
// Writes the working loadout (under Name) + the grant flag + the effects into the data and saves the file.
bool KitCommit(FKitEditorState& S, const FString& Name)
{
    FCireKitData D = CireKitEditor::Data();
    FCireKitProfile* P = D.FindProfile(S.Profile);
    if (!P) { KitStatus(S, TEXT("Profile not found."), KitBad); return false; }
    FCireKitChampion& C = P->Champions.FindOrAdd(S.Champion);
    FCireKitLoadout L = S.Work;
    L.Name = Name;
    CireKitEditor::Compact(L);
    if (FCireKitLoadout* Existing = C.Find(Name)) *Existing = L; else C.Loadouts.Add(L);
    if (!C.Find(C.DefaultLoadout)) C.DefaultLoadout = Name;
    C.bGrantOnDraft = S.bGrant;
    C.Updated = FDateTime::UtcNow().ToIso8601();
    D.Effects.Add(S.Champion, S.Effects);
    // Muzzles: bake the authored socket + offset into champion space on the posed preview body (the server has no pose).
    {
        const ACireDraftStage* Stage = S.Stage.Get();
        const ACireHero* Body = Stage ? Stage->GetPreviewHero() : nullptr;
        if (Body && Body->ChampionProfileId != S.Champion) Body = nullptr;
        TMap<FString, FCireKitMuzzle> Out;
        for (const auto& Pair : S.Muzzles)
        {
            if (Pair.Value.IsDefault()) continue;
            FCireKitMuzzle M = Pair.Value;
            if (Body) CireKitEditor::BakeMuzzle(M, Body);
            else if (const FCireKitMuzzle* Old = S.MuzzlesSaved.Find(Pair.Key); !(Old && *Old == M && Old->bHasPoint)) M.bHasPoint = false;
            Out.Add(Pair.Key, M);
        }
        if (Out.IsEmpty()) D.Muzzles.Remove(S.Champion); else D.Muzzles.Add(S.Champion, Out);
    }
    if (!KitSaveData(S, D)) return false;
    KitLoadChampion(S, S.Champion, Name);
    return true;
}
// ability-tuner: the Ability Tuner renamed / retuned / disabled abilities. Cards and tooltips already read the live rows each
// frame; drop what is cached across frames (the looping preview effect keeps the old VFX scale / tint, hover lift of rows that
// may have moved, a selection that no longer exists) and tell the user.
uint32 GKitTunerStamp = 0;
FDelegateHandle GKitTunerHook;
void KitEnsureTunerHook()
{
    if (GKitTunerHook.IsValid()) return;
    GKitTunerHook = CireAbilityTuner::OnChanged().AddLambda([]()
    {
        ++GKitTunerStamp;
        for (auto& Pair : GKitEditorStates) Pair.Value.bTunerDirty = true;
    });
}
void KitTunerRefresh(FKitEditorState& S)
{
    const bool bFirst = S.TunerVersion == 0 && !S.bTunerDirty;
    S.TunerVersion = CireAbilityTuner::Version();
    S.bTunerDirty = false;
    if (bFirst) return;
    S.LiveKey.Reset();
    if (UFXSystemComponent* Live = S.Live.Get()) Live->DestroyComponent();
    S.Live.Reset();
    S.Lift.Reset();
    if (!S.Selected.IsEmpty() && !CireAbilityDB::Find(S.Selected)) S.Selected.Reset();
    if (!S.PressId.IsEmpty() && !CireAbilityDB::Find(S.PressId)) { S.PressId.Reset(); S.bDragging = false; }
    int32 Disabled = 0;
    for (const FString& Id : S.Work.Skills()) Disabled += CireAbilityTuner::IsDisabled(Id) ? 1 : 0;
    if (Disabled) KitStatus(S, FString::Printf(TEXT("Ability Tuner: %d button%s of this loadout %s disabled in this match."), Disabled, Disabled == 1 ? TEXT("") : TEXT("s"), Disabled == 1 ? TEXT("is") : TEXT("are")), KitWarn);
    else KitStatus(S, TEXT("Ability Tuner changed abilities: cards and effects refreshed."), Muted * 1.4f);
}
FString KitKeyLabel(ACireHUD& HUD, int32 Slot)
{
    if (Slot == FCireKitLoadout::PassiveSlot) return FString();
    const FName Action = CireKeybindings::SlotAction(1, Slot == FCireKitLoadout::UltimateSlot ? 8 : Slot + 1);
    const FString L = HUD.UISettings.Keybindings.Label(Action);
    return L.IsEmpty() ? (Slot == FCireKitLoadout::UltimateSlot ? FString(TEXT("R")) : FString::FromInt(Slot + 1)) : L;
}
FString KitSlotWord(ACireHUD& HUD, int32 Slot)
{
    if (Slot == FCireKitLoadout::PassiveSlot) return TEXT("THE PASSIVE BUTTON");
    return FString::Printf(TEXT("KEY %s"), *KitKeyLabel(HUD, Slot));
}
}

// ------------------------------------------------------------------ open / close
uint32 CireKitEditor::TunerStamp()
{
    KitEnsureTunerHook();
    return GKitTunerStamp;
}

bool CireKitEditor::IsOpen(const ACireHUD* HUD)
{
    if (!HUD || !IsAvailable()) return false;
    const FKitEditorState* S = GKitEditorStates.Find(HUD);
#if !UE_BUILD_SHIPPING
    FString GalleryChampion;
    if ((!S || !S->bOpen) && (FParse::Param(FCommandLine::Get(), TEXT("CireKitEditorGallery")) || FParse::Value(FCommandLine::Get(), TEXT("CireKitEditorGallery="), GalleryChampion)))
    {
        FKitEditorState& G = KitState(HUD);
        if (!G.bGalleryDone) { G.bGallery = true; G.bOpen = true; return true; }
    }
#endif
    return S && S->bOpen;
}

void CireKitEditor::Open(ACireHUD* HUD, bool bOpen, const FString& ChampionId)
{
    if (!HUD || !IsAvailable()) return;
    FKitEditorState& S = KitState(HUD);
    ACireController* Controller = ::Cast<ACireController>(HUD->GetOwningPlayerController());
    if (bOpen && !S.bOpen)
    {
        S.bOpen = true;
        KitEnsureTunerHook(); // ability-tuner
        S.TunerVersion = CireAbilityTuner::Version(); S.bTunerDirty = false;
        if (Controller) { S.SavedSearch = Controller->DraftSearch; Controller->DraftSearch.Reset(); Controller->bDraftSearch = false; }
        if (!Data().FindProfile(S.Profile)) S.Profile = StandardProfile;
        const FString Want = !ChampionId.IsEmpty() && CireChampionRoster::Find(ChampionId) ? ChampionId
            : !S.Champion.IsEmpty() ? S.Champion : CireChampionRoster::Count() ? CireChampionRoster::All()[0].Id : FString();
        KitLoadChampion(S, Want);
        HUD->PlayInterfaceSound(4, .5f);
    }
    else if (!bOpen && S.bOpen)
    {
        S.bOpen = false;
        S.Naming = EKitName::None;
        if (Controller) { Controller->DraftSearch = S.SavedSearch; Controller->bDraftSearch = false; }
        if (UFXSystemComponent* C = S.Live.Get()) C->DestroyComponent();
        S.Live.Reset(); S.LiveKey.Reset();
        KitStopShot(S);
        if (ACireDraftStage* Stage = S.Stage.Get()) Stage->Destroy();
        S.Stage.Reset();
    }
}

// ------------------------------------------------------------------ the screen
void CireKitEditor::Draw(ACireHUD& HUD, ACireHero* Hero, ACireController* Controller)
{
    FKitEditorState& S = KitState(&HUD);
    UWorld* World = HUD.GetWorld();
    APlayerController* PC = HUD.GetOwningPlayerController();
    if (!World || !Hero) return;
    if (!Data().FindProfile(S.Profile)) S.Profile = StandardProfile;
    if (S.Champion.IsEmpty() && CireChampionRoster::Count()) KitLoadChampion(S, CireChampionRoster::All()[0].Id);
    KitEnsureTunerHook();
    if (S.bTunerDirty || S.TunerVersion != CireAbilityTuner::Version()) KitTunerRefresh(S); // ability-tuner: live retune
    const FCireChampionProfile* Profile = CireChampionRoster::Find(S.Champion);
    const double Now = FPlatformTime::Seconds();
    const float Time = static_cast<float>(FMath::Fmod(Now, 10000.0));
    FCireUIPainter P = HUD.ScreenPainter();
    const FVector2D View = HUD.LogicalViewport();
    const float VW = View.X, VH = View.Y;
    const bool bModal = S.Naming != EKitName::None;
    const bool bLive = HUD.IsInteractive() && !S.bGallery;
    const bool bInteractive = bLive && !bModal;
    const FVector2D Pointer = bLive ? CireShopUI::Pointer(HUD) : FVector2D(-1000, -1000);
    const FVector2D M = bInteractive ? Pointer : FVector2D(-1000, -1000);
    const auto In = [&](float X, float Y, float W, float H) { return M.X >= X && M.X < X + W && M.Y >= Y && M.Y < Y + H; };
    const auto Click = [&](float X, float Y, float W, float H) { if (!bInteractive || !HUD.HasClick() || !In(X, Y, W, H)) return false; HUD.TakeClick(); return true; };
    const bool bRight = bInteractive && PC && PC->WasInputKeyJustPressed(EKeys::RightMouseButton);
    const bool bMouseDown = bLive && PC && PC->IsInputKeyDown(EKeys::LeftMouseButton);
    const bool bClickThisFrame = bInteractive && HUD.HasClick();
    const int32 Wheel = bInteractive && PC ? (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown) ? 1 : PC->WasInputKeyJustPressed(EKeys::MouseScrollUp) ? -1 : 0) : 0;
    if (!bMouseDown) { S.DragSlider = -1; S.bRotating = false; }
    const auto Button = [&](float X, float Y, float W, float H, const FString& Label, bool bEnabled, FLinearColor Accent = Gold, bool bSelected = false, float Size = 9.5f)
    {
        const bool bOver = bEnabled && In(X, Y, W, H);
        CireUIStyle::Button(P, X, Y, W, H, Label, !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : bOver ? (bMouseDown ? ECireButtonState::Pressed : ECireButtonState::Hover) : ECireButtonState::Normal, Accent, Size);
        return bEnabled && Click(X, Y, W, H);
    };
    const auto Chip = [&](float& X, float Y, const FString& Label, FLinearColor Color, bool bOn, float H = 20.f) -> bool
    {
        const float W = P.TextWidth(Label, 9.f, ECireFont::Heading) + 24;
        const bool bOver = In(X, Y, W, H);
        P.Rect(X, Y, W, H, bOn ? Color * FLinearColor(.34f, .34f, .34f, .95f) : FLinearColor(.03f, .035f, .045f, .92f));
        KitBorder(P, X, Y, W, H, bOn ? Color : Color * FLinearColor(.55f, .55f, .55f, .7f), bOver ? 1.6f : 1.f);
        CireShopArt::Diamond(P, X + 9, Y + H * .5f, 3.5f, bOn ? Color : Muted, bOn);
        P.Text(Label, X + 17, Y + (H - CireUIStyle::ReadableSize(9.f)) * .5f - 1, 9.f, bOn ? Parchment : Muted * 1.25f, ECireFont::Heading, false, false);
        const bool bHit = Click(X, Y, W, H);
        X += W + 5;
        return bHit;
    };
    const auto StartNaming = [&](EKitName Mode, const FString& Suggest)
    {
        if (!Controller) return;
        S.Naming = Mode;
        S.StashSearch = Controller->DraftSearch;
        Controller->DraftSearch = Suggest.Left(24);
        Controller->bDraftSearch = true;
    };
    // Keep the pick timer from locking a champion while the host edits (standalone / listen server only).
    if (Hero->HasAuthority() && Hero->DraftDeadline > 0) Hero->DraftDeadline = FMath::Max(Hero->DraftDeadline, World->GetTimeSeconds() + 30.f);
    const bool bDirty = KitDirty(S);

    // ================= backdrop + header =================
    P.Rect(0, 0, VW, VH, FLinearColor(.010f, .013f, .020f, .985f));
    for (int32 I = 0; I < 12; ++I) P.Rect(0, VH - (I + 1) * 18, VW, 18, FLinearColor(.05f, .035f, .015f, .012f * (12 - I)));
    const float Pad = FMath::Clamp(VW * .012f, 10.f, 20.f);
    P.Text(TEXT("HERO CREATOR"), Pad, Pad - 2, 18.f, TitleText, ECireFont::Display, true, true);
    P.Text(TEXT("Pick spells like in the Skill Shop, put them on the skill buttons, save the loadout."), Pad, Pad + 25, 8.5f, Muted * 1.3f, ECireFont::Body, false, false);
    {
        const float BW = 200, BH = 30, BX = VW - Pad - BW, BY = Pad;
        if (Button(BX, BY, BW, BH, TEXT("BACK TO CHAMPION SELECT"), true))
        {
            if (bDirty && S.Confirm != TEXT("close")) { S.Confirm = TEXT("close"); KitStatus(S, TEXT("Unsaved changes: click BACK again to discard them."), KitWarn); }
            else { Open(&HUD, false); return; }
        }
        // KIT PROFILE picker.
        const FCireKitData& D = Data();
        int32 ProfileIndex = D.Profiles.IndexOfByPredicate([&](const FCireKitProfile& X) { return X.Name == S.Profile; });
        const float X0 = FMath::Max(Pad + 360.f, VW * .30f);
        float X = X0;
        const float Y = Pad + 2, H = 26;
        P.Text(TEXT("KIT PROFILE"), X, Y + 5, 9.f, Gold, ECireFont::Heading, false, false);
        X += 88;
        const float BoxW = 200;
        const bool bPrev = Button(X, Y, 26, H, TEXT("<"), D.Profiles.Num() > 1), bNext = Button(X + BoxW - 26, Y, 26, H, TEXT(">"), D.Profiles.Num() > 1);
        if (bPrev || bNext)
        {
            if (bDirty && S.Confirm != TEXT("profile")) { S.Confirm = TEXT("profile"); KitStatus(S, TEXT("Unsaved changes: click again to switch profile and discard them."), KitWarn); }
            else
            {
                ProfileIndex = (FMath::Max(0, ProfileIndex) + (bNext ? 1 : -1) + D.Profiles.Num()) % D.Profiles.Num();
                S.Profile = D.Profiles[ProfileIndex].Name;
                KitLoadChampion(S, S.Champion);
                HUD.PlayInterfaceSound(4, .45f);
            }
        }
        P.Rect(X + 29, Y, BoxW - 58, H, FLinearColor(.02f, .025f, .035f, .95f));
        KitBorder(P, X + 29, Y, BoxW - 58, H, BrightGold * FLinearColor(1, 1, 1, .7f));
        const FString PName = P.Fit(S.Profile, 10.5f, BoxW - 66, ECireFont::Bold);
        P.Text(PName, X + BoxW * .5f - P.TextWidth(PName, 10.5f, ECireFont::Bold) * .5f, Y + 4, 10.5f, BrightGold, ECireFont::Bold, false, false);
        X += BoxW + 8;
        const bool bStd = S.Profile.Equals(StandardProfile, ESearchCase::IgnoreCase);
        if (Button(X, Y, 56, H, TEXT("NEW"), true, Gold, false, 8.5f)) StartNaming(EKitName::NewProfile, TEXT("New Profile"));
        if (Button(X + 60, Y, 60, H, TEXT("COPY"), true, Gold, false, 8.5f)) StartNaming(EKitName::CopyProfile, S.Profile + TEXT(" Copy"));
        if (Button(X + 124, Y, 72, H, TEXT("RENAME"), !bStd, Gold, false, 8.5f)) StartNaming(EKitName::RenameProfile, S.Profile);
        if (Button(X + 200, Y, 70, H, S.Confirm == TEXT("delprofile") ? TEXT("SURE?") : TEXT("DELETE"), !bStd, KitBad, false, 8.5f))
        {
            if (S.Confirm != TEXT("delprofile")) { S.Confirm = TEXT("delprofile"); KitStatus(S, FString::Printf(TEXT("Delete the %s profile and every loadout in it? Click again."), *S.Profile), KitWarn); }
            else
            {
                FCireKitData Next = D;
                const FString Gone = S.Profile;
                Next.Profiles.RemoveAll([&](const FCireKitProfile& X2) { return X2.Name == Gone; });
                if (KitSaveData(S, Next)) { S.Profile = StandardProfile; KitLoadChampion(S, S.Champion); KitStatus(S, FString::Printf(TEXT("Deleted profile %s."), *Gone), Muted * 1.4f); }
            }
        }
        const FString Active = ActiveProfile(World);
        const FString Uses = FString::Printf(TEXT("This match uses: %s%s"), *Active, Active == S.Profile ? TEXT("  (the profile you are editing)") : TEXT(""));
        P.Text(Uses, X0, Y + H + 4, 8.5f, Active == S.Profile ? KitGood : Muted * 1.3f, ECireFont::Body, false, false);
    }

    // ================= champion strip =================
    const float StripY = Pad + 50, StripH = 66.f;
    {
        const TArray<FCireChampionProfile>& All = CireChampionRoster::All();
        const FCireKitProfile* Prof = Data().FindProfile(S.Profile);
        const float Cell = 62.f, SX = Pad + 24, SW = VW - 2 * Pad - 48;
        const int32 Visible = FMath::Max(1, FMath::FloorToInt(SW / Cell));
        const int32 MaxFirst = FMath::Max(0, All.Num() - Visible);
        if (In(Pad, StripY, VW - 2 * Pad, StripH) && Wheel) S.StripFirst += Wheel * 3;
        S.StripFirst = FMath::Clamp(S.StripFirst, 0, MaxFirst);
        CireUIStyle::Frame(P, Pad, StripY, VW - 2 * Pad, StripH, Gold, ECireFrame::Inset);
        for (int32 Dir = 0; Dir < 2; ++Dir)
        {
            const float AX = Dir == 0 ? Pad + 4 : VW - Pad - 20, CY = StripY + StripH * .5f;
            const bool bOn = Dir == 0 ? S.StripFirst > 0 : S.StripFirst < MaxFirst;
            const FLinearColor C = !bOn ? Muted * .5f : In(AX, StripY, 16, StripH) ? BrightGold : Gold;
            if (Dir == 0) P.Tri(FVector2D(AX + 2, CY), FVector2D(AX + 14, CY - 10), FVector2D(AX + 14, CY + 10), C);
            else P.Tri(FVector2D(AX + 14, CY), FVector2D(AX + 2, CY - 10), FVector2D(AX + 2, CY + 10), C);
            if (bOn && Click(AX, StripY, 16, StripH)) S.StripFirst = FMath::Clamp(S.StripFirst + (Dir == 0 ? -Visible : Visible), 0, MaxFirst);
        }
        for (int32 I = S.StripFirst; I < All.Num() && I < S.StripFirst + Visible; ++I)
        {
            const FCireChampionProfile& C = All[I];
            const float CX = SX + (I - S.StripFirst) * Cell + Cell * .5f, CY = StripY + 25;
            const bool bSel = C.Id == S.Champion, bOver = In(CX - Cell * .5f, StripY, Cell, StripH);
            if (bSel) CireUIStyle::Glow(P, CX - 27, CY - 27, 54, 54, FLinearColor(1.f, .8f, .3f, .55f));
            if (!CireUIStyle::PortraitFace(P, C.Id, CX, CY, 19.f)) { P.Disc(CX, CY, 19.f, Card); P.Text(C.DisplayName.Left(1), CX - 5, CY - 9, 13.f, Parchment, ECireFont::Heading); }
            P.Circle(CX, CY, 20.f, bSel ? BrightGold : bOver ? ThemeGlow : KitRoleColor(C) * FLinearColor(1, 1, 1, .8f), bSel ? 2.4f : 1.4f, 40);
            if (Prof && Prof->Champions.Contains(C.Id)) { P.Disc(CX + 14, CY - 14, 5.5f, Ink, 16); P.Disc(CX + 14, CY - 14, 4.f, KitGood, 16); }
            const FString Name = P.Fit(C.DisplayName, 8.f, Cell - 4, ECireFont::Heading);
            P.Text(Name, CX - P.TextWidth(Name, 8.f, ECireFont::Heading) * .5f, StripY + 46, 8.f, bSel ? BrightGold : bOver ? Parchment : Muted * 1.3f, ECireFont::Heading, false, false);
            if (bOver)
            {
                FCireTooltipSpec Tip; Tip.PortraitId = C.Id; Tip.Title = C.DisplayName; Tip.Tag = C.ClassType.ToUpper(); Tip.Accent = KitRoleColor(C);
                FString From;
                const FCireKitLoadout* L = ResolveLoadout(S.Profile, C.Id, &From);
                Tip.Text(L ? FString::Printf(TEXT("%s: \"%s\" (%d skills)%s"), *From, *L->Name, L->Count(), From != S.Profile ? TEXT(", fallback") : TEXT(""))
                           : FString(TEXT("No loadout: built-in opening pick + Skill Shop list.")), L ? KitGood : Muted);
                Tip.Footer = TEXT("Click to edit this champion");
                HUD.SetRichTooltip(Tip);
            }
            if (Click(CX - Cell * .5f, StripY, Cell, StripH) && C.Id != S.Champion)
            {
                if (bDirty && S.Confirm != C.Id) { S.Confirm = C.Id; KitStatus(S, FString::Printf(TEXT("Unsaved changes: click %s again to discard them."), *C.DisplayName), KitWarn); }
                else { KitLoadChampion(S, C.Id); Profile = CireChampionRoster::Find(S.Champion); HUD.PlayInterfaceSound(4, .45f); }
            }
        }
    }
    if (!Profile) return;

    // ================= tabs + layout =================
    const float TabY = StripY + StripH + 8, TabH = 26;
    {
        static const TCHAR* Tabs[] = {TEXT("SPELLS & BUTTONS"), TEXT("EFFECT PLACEMENT")};
        float X = Pad;
        for (int32 T = 0; T < 2; ++T)
        {
            const float W = P.TextWidth(Tabs[T], 10.f, ECireFont::Heading) + 40;
            if (Button(X, TabY, W, TabH, Tabs[T], true, Gold, S.Tab == T, 10.f)) { S.Tab = T; HUD.PlayInterfaceSound(4, .4f); }
            X += W + 6;
        }
        const FString Hint = S.Tab == 0 ? FString(TEXT("Click a scroll: next free button (or the selected one)  ·  drag a scroll onto a button  ·  right-click a button: clear"))
                                        : S.PlaceMode == 1 ? FString(TEXT("Set where this champion's projectiles leave from: attach point + offset, with a looping test shot"))
                                        : FString(TEXT("Click a skill button below to place that ability's cast effect on the body"));
        P.Text(P.Fit(Hint, 8.5f, VW - X - Pad - 8, ECireFont::Body), X + 8, TabY + 6, 8.5f, Muted * 1.3f, ECireFont::Body, false, false);
    }
    const float BarH = 110.f;
    const float BodyY = TabY + TabH + 8, BarY = VH - Pad - BarH, BodyB = BarY - 8;
    const float Gap = 10.f;
    const float RightW = FMath::Clamp(VW * .27f, 320.f, 440.f);
    const float LeftW = VW - 2 * Pad - RightW - Gap;
    const float LX = Pad, RX = LX + LeftW + Gap;

    // Action-bar layout first (the drop targets).
    const float SlotS = 54.f, SlotGap = 8.f, SlotY = BarY + 30;
    float SlotX[FCireKitLoadout::SlotCount];
    {
        float X = Pad + 18;
        for (int32 I = 0; I < FCireKitLoadout::ActiveSlots; ++I) { SlotX[I] = X; X += SlotS + SlotGap; }
        X += 22; SlotX[FCireKitLoadout::UltimateSlot] = X; X += SlotS + 30; SlotX[FCireKitLoadout::PassiveSlot] = X;
    }
    const auto SlotAt = [&](FVector2D Pt) { for (int32 I = 0; I < FCireKitLoadout::SlotCount; ++I) if (Pt.X >= SlotX[I] && Pt.X < SlotX[I] + SlotS && Pt.Y >= SlotY && Pt.Y < SlotY + SlotS) return I; return int32(INDEX_NONE); };
    const auto DoAssign = [&](int32 Slot, const FString& Id)
    {
        const FString Name = ACireHero::SkillName(Id);
        if (Slot == INDEX_NONE) { KitStatus(S, TEXT("All six key buttons are filled: click a button to select it, then click a scroll to replace it."), KitWarn); return; }
        FString Why;
        if (!Assign(S.Work, Slot, Id, &Why)) { KitStatus(S, Why, KitBad); return; }
        const int32 At = S.Work.Slots.IndexOfByKey(Id);
        KitStatus(S, Why.IsEmpty() ? FString::Printf(TEXT("%s on %s."), *Name, *KitSlotWord(HUD, At)) : FString::Printf(TEXT("%s on %s. %s"), *Name, *KitSlotWord(HUD, At), *Why), Why.IsEmpty() ? KitGood : KitWarn);
        S.Selected = Id; S.SelectedSlot = INDEX_NONE; S.BoneCursor = -1;
        HUD.PlayInterfaceSound(4, .5f);
    };

    ACireDraftStage* Stage = S.Stage.Get();
    ACireHero* PreviewHero = nullptr;
    if (S.Tab == 1)
    {
        if (!Stage) { Stage = ACireDraftStage::SpawnStage(World); S.Stage = Stage; }
        if (Stage)
        {
            Stage->Touch(); Stage->SetCutout(false); Stage->SetTurntable(false, S.Yaw); Stage->ShowProfile(S.Champion);
            PreviewHero = Stage->GetPreviewHero();
        }
    }
    if ((S.Tab != 1 || S.PlaceMode != 1) && S.Shot.IsValid()) KitStopShot(S);

    if (S.Tab == 0)
    {
        // ================= LEFT: every spell, Skill Shop style =================
        CireShopArt::Panel(P, LX, BodyY, LeftW, BodyB - BodyY);
        const float IX = LX + 16, IW = LeftW - 32;
        float Y = BodyY + 12;
        {
            const float SW = FMath::Min(250.f, IW * .38f), SH = 24;
            const bool bFocus = Controller && Controller->bDraftSearch && !bModal;
            P.Rect(IX, Y, SW, SH, FLinearColor(.02f, .025f, .035f, .95f));
            KitBorder(P, IX, Y, SW, SH, bFocus ? BrightGold : In(IX, Y, SW, SH) ? Gold : Gold * FLinearColor(1, 1, 1, .45f), bFocus ? 1.6f : 1.f);
            P.Circle(IX + 12, Y + 11, 5.f, Muted * 1.4f, 1.3f, 16); P.Line(IX + 15.5f, Y + 14.5f, IX + 19, Y + 18, Muted * 1.4f, 1.5f);
            const FString Typed = bModal ? S.StashSearch : Controller ? Controller->DraftSearch : FString();
            S.Filter.Search = Typed;
            const FString Shown = Typed.IsEmpty() && !bFocus ? FString(TEXT("Search every spell: name, school, tag...")) : Typed + (bFocus && FMath::Fmod(Time, 1.f) < .55f ? TEXT("|") : TEXT(""));
            P.Text(P.Fit(Shown, 9.f, SW - 44, ECireFont::Body), IX + 26, Y + 4, 9.f, Typed.IsEmpty() && !bFocus ? Muted : Parchment, ECireFont::Body, false, false);
            if (!Typed.IsEmpty() && !bModal)
            {
                const float XX = IX + SW - 14, XY = Y + 12;
                P.Line(XX - 4, XY - 4, XX + 4, XY + 4, Muted * 1.5f, 1.4f); P.Line(XX - 4, XY + 4, XX + 4, XY - 4, Muted * 1.5f, 1.4f);
                if (Click(XX - 8, Y, 16, SH) && Controller) { Controller->DraftSearch.Reset(); S.PoolRow = 0; }
            }
            if (bInteractive && HUD.HasClick() && Controller)
            {
                if (In(IX, Y, SW, SH)) { Controller->bDraftSearch = true; HUD.TakeClick(); }
                else Controller->bDraftSearch = false;
            }
            float CX = IX + SW + 10;
            static const TCHAR* Kinds[] = {TEXT("ALL"), TEXT("ACTIVES"), TEXT("PASSIVES"), TEXT("ULTIMATES")};
            for (int32 K = -1; K < 3; ++K)
                if (Chip(CX, Y + 2, Kinds[K + 1], K < 0 ? Gold : KitKindColor(static_cast<EKind>(K)), S.Filter.Kind == K)) { S.Filter.Kind = K; S.PoolRow = 0; }
            Y += SH + 7;
        }
        // Section chips (the Skill Shop periodic table) + optional role / class-list chips (off by default).
        const int32 NSec = CireShopUI::SkillSectionCount();
        {
            FPoolFilter Unhidden = S.Filter; Unhidden.HiddenSections = 0;
            const auto Counted = Pool(Unhidden);
            float CX = IX, CY = Y;
            if (Chip(CX, CY, TEXT("ALL GROUPS"), Gold, S.Filter.HiddenSections == 0)) { S.Filter.HiddenSections = 0; S.PoolRow = 0; }
            for (const auto& G : Counted)
            {
                FString Id, Label, ChipLabel; FLinearColor Color;
                CireShopUI::SkillSectionInfo(G.Key, Id, Label, ChipLabel, Color);
                const FString Text = FString::Printf(TEXT("%s  %d"), *ChipLabel, G.Value.Num());
                if (CX + P.TextWidth(Text, 9.f, ECireFont::Heading) + 29 > IX + IW) { CX = IX; CY += 24; }
                const bool bOn = (S.Filter.HiddenSections & (1u << G.Key)) == 0;
                if (Chip(CX, CY, Text, Color, bOn))
                {
                    if (S.Filter.HiddenSections == 0) S.Filter.HiddenSections = ~(1u << G.Key);
                    else S.Filter.HiddenSections ^= 1u << G.Key;
                    uint32 AllBits = 0; for (int32 I = 0; I < NSec; ++I) AllBits |= 1u << I;
                    if ((S.Filter.HiddenSections & AllBits) == AllBits) S.Filter.HiddenSections = 0;
                    S.PoolRow = 0;
                }
            }
            CY += 24; CX = IX;
            P.Text(TEXT("OPTIONAL"), CX, CY + 3, 8.f, Muted * 1.2f, ECireFont::Heading, false, false);
            CX += 66;
            static const TCHAR* Roles[] = {TEXT("DPS"), TEXT("TANK"), TEXT("HEAL")};
            static const FLinearColor RoleColors[] = {FLinearColor(.85f, .31f, .25f, 1), FLinearColor(.36f, .58f, .89f, 1), FLinearColor(.35f, .78f, .49f, 1)};
            for (int32 R = 0; R < 3; ++R) if (Chip(CX, CY, Roles[R], RoleColors[R], S.Filter.Role == Roles[R])) { S.Filter.Role = S.Filter.Role == Roles[R] ? FString() : FString(Roles[R]); S.PoolRow = 0; }
            CX += 6;
            if (Chip(CX, CY, FString::Printf(TEXT("%s'S CLASS LIST"), *Profile->DisplayName.ToUpper()), FLinearColor(.4f, .85f, .95f, 1), S.bOnlyClass)) { S.bOnlyClass = !S.bOnlyClass; S.PoolRow = 0; }
            S.Filter.OnlyChampion = S.bOnlyClass ? S.Champion : FString();
            Y = CY + 28;
        }
        // Shelves: section blocks of scroll cards, paged by rows (the Skill Shop's layout).
        const auto Groups = Pool(S.Filter);
        constexpr float CardW = 128.f, CGap = 9.f, BPad = 7.f, Head = 20.f;
        // The scroll art keeps its own aspect: size the shelf to the tallest tier so no row has dead space.
        const float CardH = FMath::Max3(CireShopArt::NaturalHeight(CireShopArt::EScroll::Golden, CardW), CireShopArt::NaturalHeight(CireShopArt::EScroll::Plain, CardW),
            CireShopArt::NaturalHeight(CireShopArt::EScroll::Prismatic, CardW)) + 6.f;
        const float AreaY = Y, AreaH = BodyB - 22 - AreaY, AreaW = IW - 14;
        const float RowH = CardH + Head + BPad * 2 + 6;
        struct FBlock { int32 Group, Start, Count; bool bCont; float X, W; };
        TArray<TArray<FBlock>> Rows;
        {
            TArray<FBlock> Row; float RXp = 0;
            for (int32 G = 0; G < Groups.Num(); ++G)
                for (int32 Start = 0; Start < Groups[G].Value.Num();)
                {
                    const int32 Left = Groups[G].Value.Num() - Start;
                    const int32 Fit = FMath::FloorToInt((AreaW - RXp - BPad * 2 + CGap) / (CardW + CGap));
                    if (Fit < 1 || (Fit < Left && Fit < 2 && RXp > 0)) { Rows.Add(Row); Row.Reset(); RXp = 0; continue; }
                    const int32 Take = FMath::Min(Fit, Left);
                    const float W = BPad * 2 + Take * CardW + (Take - 1) * CGap;
                    Row.Add({G, Start, Take, Start > 0, RXp, W});
                    RXp += W + 10; Start += Take;
                }
            if (Row.Num()) Rows.Add(Row);
        }
        const int32 PerPage = FMath::Max(1, FMath::FloorToInt(AreaH / RowH));
        const int32 MaxRow = FMath::Max(0, Rows.Num() - PerPage);
        if (In(IX, AreaY, IW, AreaH) && Wheel) S.PoolRow += Wheel;
        S.PoolRow = FMath::Clamp(S.PoolRow, 0, MaxRow);
        int32 Total = 0; for (const auto& G : Groups) Total += G.Value.Num();
        if (MaxRow > 0)
        {
            const float SBX = IX + IW - 8, SBH = AreaH;
            P.Rect(SBX, AreaY, 8, SBH, FLinearColor(.05f, .045f, .04f, .9f));
            const float Thumb = FMath::Max(30.f, SBH * PerPage / float(Rows.Num()));
            P.Rect(SBX, AreaY + (SBH - Thumb) * S.PoolRow / float(MaxRow), 8, Thumb, CireShopArt::Filigree * FLinearColor(1, 1, 1, .8f));
        }
        const FString More = FString::Printf(TEXT("%d SPELLS  ·  ROWS %d-%d OF %d  ·  SCROLL FOR MORE"), Total, Rows.Num() ? S.PoolRow + 1 : 0, FMath::Min(Rows.Num(), S.PoolRow + PerPage), Rows.Num());
        CireShopArt::Spaced(P, More, IX + IW * .5f, BodyB - 18, 7.f, .3f, CireShopArt::Filigree * .85f, ECireFont::Display, true, false);
        if (Rows.IsEmpty()) CireShopArt::Spaced(P, TEXT("NO SPELL MATCHES THE SEARCH AND FILTERS"), IX + IW * .5f, AreaY + 60, 10.f, .3f, Muted, ECireFont::Display, true, false);
        struct FCard { FString Id; float X, Y; };
        TArray<FCard> Cards;
        for (int32 R = S.PoolRow; R < Rows.Num() && R < S.PoolRow + PerPage; ++R)
        {
            const float RY = AreaY + (R - S.PoolRow) * RowH;
            for (const FBlock& B : Rows[R])
            {
                FString Id, Label, ChipLabel; FLinearColor Color;
                CireShopUI::SkillSectionInfo(Groups[B.Group].Key, Id, Label, ChipLabel, Color);
                const float BX = IX + B.X, BH = RowH - 6;
                P.Rect(BX, RY, B.W, BH, Color * FLinearColor(.06f, .06f, .06f, .55f));
                KitBorder(P, BX, RY, B.W, BH, Color * FLinearColor(1, 1, 1, .85f), 1.6f);
                P.Rect(BX, RY, B.W, Head, Color * FLinearColor(.28f, .28f, .28f, .95f));
                const FString Caption = Label + (B.bCont ? TEXT("  (CONT.)") : TEXT(""));
                CireShopArt::Spaced(P, P.Fit(Caption, 9.5f, B.W - 16, ECireFont::Display), BX + 8, RY + 4, 9.5f, .18f, Parchment, ECireFont::Display, false, false);
                for (int32 I = 0; I < B.Count; ++I) Cards.Add({Groups[B.Group].Value[B.Start + I]->Id, BX + BPad + I * (CardW + CGap), RY + Head + BPad});
            }
        }
        FString HoverId;
        for (const FCard& C : Cards) if (In(C.X, C.Y, CardW, CardH)) HoverId = C.Id;
        const float Dt = World->GetDeltaSeconds();
        for (const FCard& C : Cards) { float& L = S.Lift.FindOrAdd(C.Id); L = FMath::FInterpTo(L, C.Id == HoverId || C.Id == S.PressId ? 1.f : 0.f, Dt > 0 ? Dt : .016f, 14.f); }
        Cards.StableSort([&](const FCard& A, const FCard& B) { return S.Lift.FindRef(A.Id) < S.Lift.FindRef(B.Id); });
        for (const FCard& C : Cards)
        {
            const float Lift = S.Lift.FindRef(C.Id), Grow = 1.f + .06f * Lift;
            const float CW = CardW * Grow, CH = CardH * Grow, X = C.X - (CW - CardW) * .5f, Y2 = C.Y - (CH - CardH) * .5f - 5.f * Lift;
            const int32 On = S.Work.Slots.IndexOfByKey(C.Id);
            const EKind K = KindOf(C.Id);
            const bool bTunerOff = CireAbilityTuner::IsDisabled(C.Id); // ability-tuner: still assignable (templates outlive a match), flagged
            const FString Caption = bTunerOff ? FString(TEXT("DISABLED BY TUNER"))
                : On != INDEX_NONE ? (On == FCireKitLoadout::PassiveSlot ? FString(TEXT("ON PASSIVE")) : FString::Printf(TEXT("ON KEY %s"), *KitKeyLabel(HUD, On)))
                : K == EKind::Ultimate ? FString(TEXT("ULTIMATE")) : K == EKind::Passive ? FString(TEXT("PASSIVE")) : FString(TEXT("ACTIVE"));
            if (On != INDEX_NONE) CireShopArt::Glow(P, X - CW * .06f, Y2 - CH * .04f, CW * 1.12f, CH * 1.08f, FLinearColor(1.f, .8f, .35f, .45f));
            CireShopUI::DrawSkillCard(P, C.Id, X, Y2, CW, CH, Time, Lift, bTunerOff, Caption, On != INDEX_NONE ? TEXT("ASSIGNED") : TEXT("ASSIGN"), bTunerOff ? KitBad : On != INDEX_NONE ? KitGood : BrightGold);
            if (C.Id == HoverId && S.PressId.IsEmpty())
            {
                const int32 Target = TargetSlot(S.Work, C.Id, S.SelectedSlot);
                const FString Foot = On != INDEX_NONE ? FString::Printf(TEXT("On %s. Drag it to another button to move it."), *KitSlotWord(HUD, On))
                    : Target == INDEX_NONE ? FString(TEXT("All six key buttons are filled: select one to replace it."))
                    : FString::Printf(TEXT("Click: put it on %s  ·  or drag it onto a button"), *KitSlotWord(HUD, Target));
                CireShopUI::TipSkill(HUD, Hero, C.Id, Foot);
                if (CireTunerLink::Badge(HUD, P, M, X + CW, Y2, C.Id, bInteractive)) S.PressId.Reset(); // EDIT -> Ability Tuner (takes the click)
            }
        }
        // A press on a card starts a click or a drag (resolved on release, below).
        if (!HoverId.IsEmpty() && bClickThisFrame && HUD.HasClick()) { HUD.TakeClick(); S.PressId = HoverId; S.PressAt = Pointer; S.bDragging = false; }

        // ================= RIGHT: loadout presets =================
        CireShopArt::Panel(P, RX, BodyY, RightW, BodyB - BodyY);
        const float PX = RX + 16, PW = RightW - 32;
        float PY = BodyY + 12;
        CireShopArt::Spaced(P, TEXT("LOADOUT PRESETS"), PX + 10, PY + 2, 11.f, .25f, TitleText, ECireFont::Display, false, true);
        PY += 22;
        const FCireKitProfile* Prof = Data().FindProfile(S.Profile);
        const FCireKitChampion* Entry = Prof ? Prof->Champions.Find(S.Champion) : nullptr;
        FString From;
        const FCireKitLoadout* Used = ResolveLoadout(S.Profile, S.Champion, &From);
        const FString UsedText = !Used ? FString::Printf(TEXT("%s has no loadout here: matches use the built-in kit."), *Profile->DisplayName)
            : From == S.Profile ? FString::Printf(TEXT("%s starts %s matches with \"%s\"."), *Profile->DisplayName, *S.Profile, *Used->Name)
            : FString::Printf(TEXT("None in %s: matches fall back to Standard's \"%s\"."), *S.Profile, *Used->Name);
        P.Wrapped(UsedText, PX, PY, PW, 8.5f, Used ? (From == S.Profile ? KitGood : KitWarn) : Muted * 1.3f, 2, ECireFont::Body, 2.f);
        PY += 32;
        const int32 NPresets = Entry ? Entry->Loadouts.Num() : 0;
        const float RowHt = 42.f;
        const int32 Fit = FMath::Max(2, FMath::FloorToInt((BodyB - PY - 190) / (RowHt + 4)));
        if (In(PX, PY, PW, Fit * (RowHt + 4)) && Wheel) S.PresetFirst += Wheel;
        S.PresetFirst = FMath::Clamp(S.PresetFirst, 0, FMath::Max(0, NPresets - Fit));
        if (NPresets == 0) P.Wrapped(TEXT("No saved presets for this champion in this profile yet. Fill the skill buttons below and press SAVE."), PX, PY + 4, PW, 9.f, Muted * 1.3f, 3);
        for (int32 I = S.PresetFirst; I < NPresets && I < S.PresetFirst + Fit; ++I)
        {
            const FCireKitLoadout& L = Entry->Loadouts[I];
            const float RY = PY + (I - S.PresetFirst) * (RowHt + 4);
            const bool bLoaded = L.Name == S.Work.Name, bDefault = Entry->Default() == &L, bOver = In(PX, RY, PW, RowHt);
            P.Rect(PX, RY, PW, RowHt, bLoaded ? FLinearColor(.12f, .09f, .03f, .95f) : bOver ? Hover : FLinearColor(.025f, .03f, .04f, .92f));
            KitBorder(P, PX, RY, PW, RowHt, bLoaded ? BrightGold : bOver ? ThemeGlow : Gold * FLinearColor(1, 1, 1, .35f), bLoaded ? 1.6f : 1.f);
            // Default star: click to make this preset the default the game uses.
            const float SX2 = PX + 14, SY2 = RY + RowHt * .5f, R = 7.f;
            const FLinearColor SC = bDefault ? BrightGold : In(SX2 - 10, RY, 20, RowHt) ? Gold : Muted * .8f;
            for (int32 K2 = 0; K2 < 5; ++K2)
            {
                const float A0 = -PI * .5f + K2 * 2 * PI / 5, A1 = A0 + PI / 5, A2 = A0 - PI / 5;
                P.Tri(FVector2D(SX2 + FMath::Cos(A0) * R, SY2 + FMath::Sin(A0) * R), FVector2D(SX2 + FMath::Cos(A1) * R * .42f, SY2 + FMath::Sin(A1) * R * .42f), FVector2D(SX2 + FMath::Cos(A2) * R * .42f, SY2 + FMath::Sin(A2) * R * .42f), SC);
            }
            P.Disc(SX2, SY2, R * .45f, SC, 12);
            P.Text(P.Fit(L.Name, 10.f, PW - 44 - 8 * 17, ECireFont::Bold), PX + 28, RY + 5, 10.f, bLoaded ? BrightGold : Parchment, ECireFont::Bold, false, false);
            P.Text(bDefault ? FString(TEXT("DEFAULT")) : FString::Printf(TEXT("%d skills"), L.Count()), PX + 28, RY + 23, 8.f, bDefault ? KitGood : Muted * 1.2f, ECireFont::Heading, false, false);
            for (int32 Sl = 0; Sl < FCireKitLoadout::SlotCount; ++Sl)
            {
                const float IXs = PX + PW - 8 - (FCireKitLoadout::SlotCount - Sl) * 17, IYs = RY + 13;
                if (L.Slots.IsValidIndex(Sl) && !L.Slots[Sl].IsEmpty()) CireAbilityIcons::Draw(P, L.Slots[Sl], IXs, IYs, 16.f);
                else P.Rect(IXs, IYs, 16, 16, FLinearColor(.06f, .06f, .07f, 1));
            }
            if (In(SX2 - 10, RY, 20, RowHt)) HUD.SetRichTooltip(FCireTooltipSpec().Text(bDefault ? TEXT("The default: the game starts this champion with it.") : TEXT("Click: make this the default loadout the game uses."), Parchment));
            else if (bOver) HUD.SetRichTooltip(FCireTooltipSpec().Text(bLoaded ? TEXT("This preset is loaded on the buttons.") : TEXT("Click to load this preset onto the buttons."), Parchment));
            if (!bDefault && Click(SX2 - 10, RY, 20, RowHt))
            {
                FCireKitData D = Data();
                if (FCireKitProfile* DP = D.FindProfile(S.Profile)) if (FCireKitChampion* DC = DP->Champions.Find(S.Champion)) DC->DefaultLoadout = L.Name;
                const FString Name = L.Name;
                if (KitSaveData(S, D)) KitStatus(S, FString::Printf(TEXT("\"%s\" is now %s's default in %s."), *Name, *Profile->DisplayName, *S.Profile), KitGood);
                break;
            }
            if (!bLoaded && Click(PX + 28, RY, PW - 28, RowHt))
            {
                const FString Name = L.Name;
                if (bDirty && S.Confirm != Name) { S.Confirm = Name; KitStatus(S, TEXT("Unsaved changes: click the preset again to discard them."), KitWarn); }
                else { KitLoadChampion(S, S.Champion, Name); HUD.PlayInterfaceSound(4, .45f); }
                break;
            }
        }
        PY += Fit * (RowHt + 4) + 6;
        Entry = Prof ? Data().FindProfile(S.Profile)->Champions.Find(S.Champion) : nullptr; // may have changed above
        {
            const float BW = (PW - 12) / 3.f, BH = 28;
            const bool bSaved = !S.Work.Name.IsEmpty() && Entry && Entry->Find(S.Work.Name);
            if (Button(PX, PY, BW, BH, bSaved && !bDirty ? TEXT("SAVED") : TEXT("SAVE"), bDirty || !bSaved, KitGood))
            {
                if (!bSaved) StartNaming(EKitName::SaveAs, FString::Printf(TEXT("%s Loadout"), *Profile->DisplayName));
                else { const FString Name = S.Work.Name; if (KitCommit(S, Name)) KitStatus(S, FString::Printf(TEXT("Saved \"%s\" for %s in %s."), *Name, *Profile->DisplayName, *S.Profile), KitGood); }
            }
            if (Button(PX + BW + 6, PY, BW, BH, TEXT("SAVE AS"), true)) StartNaming(EKitName::SaveAs, S.Work.Name.IsEmpty() ? FString::Printf(TEXT("%s Loadout"), *Profile->DisplayName) : S.Work.Name + TEXT(" 2"));
            if (Button(PX + 2 * (BW + 6), PY, BW, BH, TEXT("NEW"), true))
            {
                if (bDirty && S.Confirm != TEXT("new")) { S.Confirm = TEXT("new"); KitStatus(S, TEXT("Unsaved changes: click NEW again to discard them."), KitWarn); }
                else { S.Work = FCireKitLoadout(); S.Base = FCireKitLoadout(); S.SelectedSlot = INDEX_NONE; KitStatus(S, TEXT("Empty loadout: fill the buttons, then SAVE."), Muted * 1.4f); }
            }
            PY += BH + 6;
            if (Button(PX, PY, BW, BH, TEXT("RENAME"), bSaved)) StartNaming(EKitName::Rename, S.Work.Name);
            if (Button(PX + BW + 6, PY, BW, BH, S.Confirm == TEXT("delete") ? TEXT("SURE?") : TEXT("DELETE"), bSaved, KitBad))
            {
                if (S.Confirm != TEXT("delete")) { S.Confirm = TEXT("delete"); KitStatus(S, FString::Printf(TEXT("Delete \"%s\"? Click again."), *S.Work.Name), KitWarn); }
                else
                {
                    FCireKitData D = Data();
                    const FString Gone = S.Work.Name;
                    if (FCireKitProfile* DP = D.FindProfile(S.Profile))
                        if (FCireKitChampion* DC = DP->Champions.Find(S.Champion))
                        {
                            DC->Loadouts.RemoveAll([&](const FCireKitLoadout& X2) { return X2.Name == Gone; });
                            if (DC->Loadouts.IsEmpty()) DP->Champions.Remove(S.Champion);
                        }
                    if (KitSaveData(S, D)) { KitLoadChampion(S, S.Champion); KitStatus(S, FString::Printf(TEXT("Deleted \"%s\"."), *Gone), Muted * 1.4f); }
                }
            }
            const bool bIsDefault = bSaved && Entry->Default() && Entry->Default()->Name == S.Work.Name;
            if (Button(PX + 2 * (BW + 6), PY, BW, BH, bIsDefault ? TEXT("DEFAULT") : TEXT("SET DEFAULT"), bSaved && !bIsDefault, BrightGold, bIsDefault, 8.5f))
            {
                FCireKitData D = Data();
                if (FCireKitProfile* DP = D.FindProfile(S.Profile)) if (FCireKitChampion* DC = DP->Champions.Find(S.Champion)) DC->DefaultLoadout = S.Work.Name;
                if (KitSaveData(S, D)) KitStatus(S, FString::Printf(TEXT("\"%s\" is now the default in %s."), *S.Work.Name, *S.Profile), KitGood);
            }
            PY += BH + 10;
        }
        {
            const bool bOn = S.bGrant, bOver = In(PX, PY, PW, 34);
            P.Rect(PX, PY + 2, 16, 16, FLinearColor(.02f, .025f, .035f, 1)); KitBorder(P, PX, PY + 2, 16, 16, bOver ? BrightGold : Gold, 1.2f);
            if (bOn) { P.Line(PX + 3, PY + 10, PX + 7, PY + 14, BrightGold, 2.f); P.Line(PX + 7, PY + 14, PX + 14, PY + 5, BrightGold, 2.f); }
            P.Text(TEXT("START MATCHES WITH THE DEFAULT"), PX + 22, PY + 1, 9.f, bOn ? Parchment : Muted * 1.3f, ECireFont::Heading, false, false);
            P.Text(P.Fit(bOn ? FString(TEXT("Learned at level 1 on these buttons, levelled in the Skill Shop")) : FString(TEXT("Off: the loadout is only sold in the Skill Shop")), 8.f, PW - 22, ECireFont::Body),
                PX + 22, PY + 18, 8.f, Muted * 1.2f, ECireFont::Body, false, false);
            if (Click(PX, PY, PW, 34)) S.bGrant = !S.bGrant;
            PY += 42;
        }
        const bool bFresh = !S.Status.IsEmpty() && Now - S.StatusAt < 9.0;
        const FString Line = bFresh ? S.Status : bDirty ? FString(TEXT("Unsaved changes.")) : FString();
        if (!Line.IsEmpty()) P.Wrapped(Line, PX, PY, PW, 9.f, bFresh ? S.StatusColor : KitWarn, 3, ECireFont::Body, 2.f);
    }
    else
    {
        // ================= EFFECT PLACEMENT: preview (left) + controls (right) =================
        CireShopArt::Panel(P, LX, BodyY, LeftW, BodyB - BodyY);
        const float ImgTop = BodyY + 14, ImgBottom = BodyB - 50;
        float ImgH = ImgBottom - ImgTop, ImgW = ImgH * .75f;
        if (ImgW > LeftW - 40) { ImgW = LeftW - 40; ImgH = ImgW / .75f; }
        const float ImgX = LX + (LeftW - ImgW) * .5f;
        P.Rect(ImgX - 1, ImgTop - 1, ImgW + 2, ImgH + 2, FLinearColor(0, 0, 0, 1));
        if (Stage && Stage->GetRenderTarget() && PreviewHero) KitDrawTarget(P, Stage->GetRenderTarget(), ImgX, ImgTop, ImgW, ImgH);
        else P.Text(TEXT("Loading champion..."), ImgX + 12, ImgTop + ImgH * .5f, 10.f, Muted, ECireFont::Body);
        KitBorder(P, ImgX, ImgTop, ImgW, ImgH, Gold * FLinearColor(1, 1, 1, .6f), 1.f);
        if (bInteractive && HUD.HasClick() && In(ImgX, ImgTop, ImgW, ImgH)) { HUD.TakeClick(); S.bRotating = true; S.RotateFromX = M.X; S.RotateFromYaw = S.Yaw; }
        if (S.bRotating && bMouseDown) S.Yaw = S.RotateFromYaw - (M.X - S.RotateFromX) * .6f;
        P.Text(TEXT("drag to turn"), ImgX + 8, ImgTop + ImgH - 18, 8.f, Muted * 1.2f, ECireFont::Body, false, true);
        const FCireAbilityDef* SelDef = CireAbilityDB::Find(S.Selected);
        {
            const float BY = BodyB - 40, BH = 30, BX = ImgX;
            if (Button(BX, BY, 34, BH, TEXT("<"), true)) S.Yaw -= 45.f;
            if (Button(BX + 38, BY, 34, BH, TEXT(">"), true)) S.Yaw += 45.f;
            if (Button(BX + 78, BY, FMath::Max(120.f, ImgW - 78), BH, TEXT("CAST PREVIEW"), SelDef && PreviewHero, FLinearColor(.3f, .9f, 1.f, 1)) && PreviewHero && SelDef)
            {
                // The cast clip plays when a skill cooldown starts (CireChampionActions): one learned slot, bumped each click.
                PreviewHero->Skills = {SelDef->Id};
                if (PreviewHero->Cooldowns.Num() != 1) PreviewHero->Cooldowns = {0.f};
                PreviewHero->Cooldowns[0] += 1.f;
                S.CastReleaseAt = Now + CireChampionActions::SkillWindup(World, SelDef->Id);
                if (UFXSystemComponent* C = S.Live.Get()) C->DestroyComponent();
                S.Live.Reset();
                HUD.PlayInterfaceSound(4, .4f);
            }
        }
        CireShopArt::Panel(P, RX, BodyY, RightW, BodyB - BodyY);
        const float IX = RX + 16, IW = RightW - 32;
        float Y = BodyY + 12;
        CireShopArt::Spaced(P, S.PlaceMode == 1 ? TEXT("PROJECTILE MUZZLE") : TEXT("EFFECT PLACEMENT"), IX + 10, Y + 2, 11.f, .25f, TitleText, ECireFont::Display, false, true);
        Y += 22;
        {
            // Page: the cast effect of the selected ability, or where the champion's projectiles leave from.
            float CX = IX, CY = Y;
            const int32 Was = S.PlaceMode;
            if (Chip(CX, CY, TEXT("CAST EFFECT"), Gold, S.PlaceMode == 0, 22.f)) S.PlaceMode = 0;
            if (Chip(CX, CY, TEXT("PROJECTILE MUZZLE"), FLinearColor(.3f, .9f, 1.f, 1), S.PlaceMode == 1, 22.f)) S.PlaceMode = 1;
            if (Was != S.PlaceMode)
            {
                S.BoneCursor = -1; KitStopShot(S);
                if (UFXSystemComponent* C = S.Live.Get()) C->DestroyComponent();
                S.Live.Reset(); S.LiveKey.Reset();
                HUD.PlayInterfaceSound(4, .4f);
            }
            Y = CY + 30;
        }
        USkeletalMeshComponent* Mesh = PreviewHero ? PreviewHero->GetMesh() : nullptr;
        // Attach point chips + the bone / socket picker (shared by the cast effect and the muzzle).
        const auto AttachRows = [&](FString& Attach, const TCHAR* DefaultText)
        {
            P.Text(TEXT("ATTACH TO"), IX, Y + 3, 8.5f, Gold, ECireFont::Heading, false, false);
            {
                float CX = IX + 76, CY = Y;
                for (const FKitAnchorChip& A : KitAnchorChips)
                {
                    const bool bResolved = !*A.Key || !Mesh || ResolveAttach(Mesh, A.Key) != NAME_None;
                    const float W = P.TextWidth(A.Label, 9.f, ECireFont::Heading) + 29;
                    if (CX + W > IX + IW) { CX = IX + 76; CY += 24; }
                    if (Chip(CX, CY, A.Label, bResolved ? FLinearColor(.3f, .9f, 1.f, 1) : Muted * .6f, Attach == A.Key) && bResolved) { Attach = A.Key; S.BoneCursor = -1; }
                }
                Y = CY + 28;
            }
            TArray<FName> Names = Mesh ? Mesh->GetAllSocketNames() : TArray<FName>();
            Names.RemoveAll([](const FName& N) { const FString X2 = N.ToString().ToLower(); return X2.Contains(TEXT("twist")) || X2.StartsWith(TEXT("ik_")) || X2.StartsWith(TEXT("vb ")); });
            const FName Resolved = ResolveAttach(Mesh, Attach);
            const FString Shown = Attach.IsEmpty() ? FString(DefaultText) : Resolved.IsNone() ? FString::Printf(TEXT("%s (not on this body)"), *Attach) : Resolved.ToString();
            P.Text(TEXT("BONE"), IX, Y + 5, 8.5f, Gold, ECireFont::Heading, false, false);
            const float BX = IX + 76, BW = IW - 76;
            const bool bHas = Names.Num() > 0;
            const bool bPrev = Button(BX, Y, 26, 24, TEXT("<"), bHas, Gold, false, 9.f), bNext = Button(BX + BW - 26, Y, 26, 24, TEXT(">"), bHas, Gold, false, 9.f);
            if (bPrev || bNext)
            {
                int32 Index = S.BoneCursor >= 0 ? S.BoneCursor : Names.IndexOfByKey(Resolved);
                Index = Index < 0 ? 0 : (Index + (bNext ? 1 : -1) + Names.Num()) % Names.Num();
                S.BoneCursor = Index; Attach = Names[Index].ToString();
            }
            P.Rect(BX + 30, Y, BW - 60, 24, FLinearColor(.02f, .025f, .035f, .95f));
            const FString FitName = P.Fit(Shown, 9.f, BW - 68, ECireFont::Body);
            P.Text(FitName, BX + 30 + (BW - 60 - P.TextWidth(FitName, 9.f, ECireFont::Body)) * .5f, Y + 4, 9.f, Attach.IsEmpty() ? Muted * 1.3f : Resolved.IsNone() ? KitBad : Parchment, ECireFont::Body, false, false);
            Y += 32;
        };
        const auto SliderRow = [&](int32 Id, const TCHAR* Label, float& Value, float Min, float Max, float Default, const FString& Text)
        {
            P.Text(Label, IX, Y + 1, 8.5f, Gold, ECireFont::Heading, false, false);
            const float TX = IX + 76, TW = IW - 76 - 58, TY = Y + 5;
            const bool bOver = In(TX - 6, Y - 2, TW + 12, 20);
            CireUIStyle::Slider(P, TX, TY, TW, (Value - Min) / (Max - Min), true, bOver || S.DragSlider == Id);
            P.Text(Text, TX + TW + 10, Y + 1, 9.f, Parchment, ECireFont::Numbers, false, false);
            if (bInteractive && HUD.HasClick() && bOver) { HUD.TakeClick(); S.DragSlider = Id; }
            if (S.DragSlider == Id && bMouseDown) Value = FMath::Clamp(Min + (Pointer.X - TX) / TW * (Max - Min), Min, Max);
            if (bOver && bRight) Value = Default;
            if (bOver && Wheel) Value = FMath::Clamp(Value - Wheel * (Max - Min) / 60.f, Min, Max);
            Y += 24;
        };
        const auto OffsetRows = [&](int32 IdBase, FVector& Offset)
        {
            float OX = Offset.X, OY = Offset.Y, OZ = Offset.Z;
            SliderRow(IdBase + 0, TEXT("FORWARD"), OX, -150, 150, 0, FString::Printf(TEXT("%+.0f"), OX));
            SliderRow(IdBase + 1, TEXT("RIGHT"), OY, -150, 150, 0, FString::Printf(TEXT("%+.0f"), OY));
            SliderRow(IdBase + 2, TEXT("UP"), OZ, -150, 150, 0, FString::Printf(TEXT("%+.0f"), OZ));
            Offset = FVector(FMath::RoundToFloat(OX), FMath::RoundToFloat(OY), FMath::RoundToFloat(OZ));
        };
        if (S.PlaceMode == 1)
        {
            // ================= projectile muzzle (per champion, optional per-spell override) =================
            const FLinearColor Cyan(.3f, .9f, 1.f, 1);
            const bool bSpell = S.bMuzzleSpell && SelDef;
            {
                float CX = IX, CY = Y;
                if (Chip(CX, CY, TEXT("ALL PROJECTILES"), Cyan, !bSpell)) { S.bMuzzleSpell = false; S.BoneCursor = -1; }
                if (SelDef)
                {
                    const FString Label = P.Fit(FString::Printf(TEXT("ONLY %s"), *SelDef->Name.ToUpper()), 9.f, FMath::Max(40.f, IX + IW - CX - 30), ECireFont::Heading);
                    if (Chip(CX, CY, Label, FLinearColor(.78f, .56f, 1.f, 1), bSpell)) { S.bMuzzleSpell = true; S.BoneCursor = -1; }
                }
                Y = CY + 26;
            }
            const FString Key = bSpell ? SelDef->Id : FString(CireKitEditor::MuzzleAll);
            FCireKitMuzzle& Mz = S.Muzzles.FindOrAdd(Key);
            const FCireKitMuzzle* AllMz = S.Muzzles.Find(CireKitEditor::MuzzleAll);
            const bool bOwn = SelDef && S.Muzzles.Contains(SelDef->Id) && !S.Muzzles[SelDef->Id].IsDefault();
            const FString Help = bSpell ? FString::Printf(TEXT("%s's projectiles only (overrides ALL PROJECTILES)."), *SelDef->Name)
                : bOwn ? FString::Printf(TEXT("Every skillshot and the ranged basic attack. %s has its own muzzle."), *SelDef->Name)
                : FString(TEXT("Every skillshot and the ranged basic attack of this champion (every profile)."));
            P.Wrapped(Help, IX, Y, IW, 8.5f, Muted * 1.35f, 2, ECireFont::Body);
            Y += 30;
            AttachRows(Mz.Attach, bSpell && AllMz && !AllMz->IsDefault() ? TEXT("default (the ALL PROJECTILES muzzle)") : TEXT("default (chest, in front)"));
            OffsetRows(11, Mz.Offset);
            const bool bResetMuzzle = Button(IX, Y + 2, 140, 26, TEXT("RESET MUZZLE"), !Mz.IsDefault(), Gold, false, 9.f);
            if (Button(IX + 146, Y + 2, IW - 146, 26, bDirty ? TEXT("SAVE") : TEXT("SAVED"), bDirty, KitGood, false, 9.f))
            {
                if (S.Work.Name.IsEmpty()) StartNaming(EKitName::SaveAs, FString::Printf(TEXT("%s Loadout"), *Profile->DisplayName));
                else { const FString Name = S.Work.Name; if (KitCommit(S, Name)) KitStatus(S, TEXT("Saved the loadout, its effect placements and the projectile muzzle."), KitGood); }
            }
            if (bResetMuzzle) { Mz = FCireKitMuzzle(); S.BoneCursor = -1; }
            Y += 34;
            P.Text(TEXT("Right-click a slider to reset it  ·  the wheel nudges it"), IX, Y, 8.f, Muted * 1.1f, ECireFont::Body, false, false);
            Y += 16;
            if (!S.Status.IsEmpty() && Now - S.StatusAt < 9.0) P.Wrapped(S.Status, IX, Y, IW, 9.f, S.StatusColor, 3, ECireFont::Body, 2.f);

            // Live preview: a marker on the muzzle + a looping test projectile flying forward from it.
            const FCireKitMuzzle Shown = !Mz.IsDefault() ? Mz : (bSpell && AllMz) ? *AllMz : Mz;
            if (PreviewHero && Mesh)
            {
                const FVector Muzzle = MuzzleWorldPosed(PreviewHero, Shown);
                const FVector Forward = PreviewHero->GetActorForwardVector();
                const FString ShotId = SelDef ? SelDef->Id : FString(TEXT("muzzle_test"));
                const FName SkillKey(*ShotId);
                const CireFabVFX::FEntry* ShotEntry = CireFabVFX::FindFor(SkillKey, CireAbilityShapes::SchoolFor(SkillKey), CireFabVFX::ERole::Projectile);
                UFXSystemAsset* ShotSystem = CireFabVFX::Resolve(ShotEntry);
                const FString ShotKey = FString::Printf(TEXT("%s|%s|%u"), *S.Champion, *ShotId, S.TunerVersion);
                constexpr float Travel = 650.f, Flight = .85f, Pause = .35f;
                UFXSystemComponent* ShotC = S.Shot.Get();
                if (ShotKey != S.ShotKey || (!ShotC && ShotSystem) || Now - S.ShotAt > Flight + Pause)
                {
                    if (ShotC) ShotC->DestroyComponent();
                    ShotC = nullptr;
                    if (UNiagaraSystem* Niagara = ::Cast<UNiagaraSystem>(ShotSystem))
                        ShotC = UNiagaraFunctionLibrary::SpawnSystemAttached(Niagara, PreviewHero->GetRootComponent(), NAME_None, Muzzle, Forward.Rotation(), EAttachLocation::KeepWorldPosition, false);
                    else if (UParticleSystem* Cascade = ::Cast<UParticleSystem>(ShotSystem))
                        ShotC = UGameplayStatics::SpawnEmitterAttached(Cascade, PreviewHero->GetRootComponent(), NAME_None, Muzzle, Forward.Rotation(), EAttachLocation::KeepWorldPosition, false);
                    if (ShotC && ShotEntry)
                    {
                        CireFabVFX::ApplyEntryTint(ShotC, *ShotEntry);
                        ShotC->SetUsingAbsoluteScale(true);
                        ShotC->SetWorldScale3D(FVector(FMath::Max(.05f, ShotEntry->Scale * CireAbilityVFX::SpellEffectScale(World))));
                    }
                    S.Shot = ShotC; S.ShotKey = ShotKey; S.ShotAt = Now;
                }
                const float T01 = FMath::Clamp(static_cast<float>(Now - S.ShotAt) / Flight, 0.f, 1.f);
                const FVector ShotAt = Muzzle + Forward * Travel * T01;
                if (ShotC) { ShotC->SetWorldLocationAndRotation(ShotAt, Forward.Rotation()); ShotC->SetVisibility(Now - S.ShotAt <= Flight); }
                KitMuzzleMarkers(S, PreviewHero, Muzzle, ShotC ? FVector::ZeroVector : ShotAt, !ShotC && Now - S.ShotAt <= Flight);
                const FVector Local = PreviewHero->GetActorTransform().InverseTransformPosition(Muzzle);
                P.Text(FString::Printf(TEXT("muzzle %+.0f fwd  %+.0f right  %+.0f up%s"), Local.X, Local.Y, Local.Z, ShotSystem ? TEXT("") : TEXT("  ·  no projectile art installed: test ball")),
                    ImgX + 8, ImgTop + 8, 8.f, Muted * 1.3f, ECireFont::Body, false, true);
            }
        }
        else if (!SelDef)
        {
            P.Wrapped(TEXT("Select a skill button below. Its cast effect loops on the champion at the placement you set here: attach point, offset, size and colour. Placements belong to the champion (every profile)."),
                IX, Y, IW, 9.f, Muted * 1.3f, 6, ECireFont::Body);
        }
        else
        {
            CireShopUI::DrawSkillMedallion(P, SelDef->Id, IX + 18, Y + 18, 17.f, Time);
            P.Text(P.Fit(SelDef->Name, 11.f, IW - 44, ECireFont::Bold), IX + 42, Y + 2, 11.f, BrightGold, ECireFont::Bold, false, true);
            float DataScale = 1.f;
            const CireFabVFX::FEntry* Entry = nullptr;
            UFXSystemAsset* System = CastSystem(SelDef->Id, &DataScale, &Entry);
            P.Text(P.Fit(System ? System->GetName() : FString(TEXT("no cast effect: procedural presentation only")), 8.f, IW - 44, ECireFont::Body), IX + 42, Y + 20, 8.f, Muted * 1.3f, ECireFont::Body, false, false);
            Y += 44;
            FCireKitEffectPlacement& Pl = S.Effects.FindOrAdd(S.Selected);
            AttachRows(Pl.Attach, TEXT("default (effect origin)"));
            OffsetRows(1, Pl.Offset);
            SliderRow(4, TEXT("SIZE"), Pl.Scale, .2f, 3.f, 1.f, FString::Printf(TEXT("%.2fx"), Pl.Scale));
            Pl.Scale = FMath::RoundToFloat(Pl.Scale * 100.f) / 100.f;
            {
                P.Text(TEXT("TINT"), IX, Y + 3, 8.5f, Gold, ECireFont::Heading, false, false);
                const float SW = FMath::Min(20.f, (IW - 76 - 10 * 3) / 11.f);
                float SX = IX + 76;
                const bool bNone = Pl.Tint.A <= 0.f;
                P.Rect(SX, Y, SW, SW, FLinearColor(.03f, .03f, .04f, 1)); P.Line(SX + 2, Y + SW - 2, SX + SW - 2, Y + 2, FLinearColor(.8f, .2f, .2f, 1), 1.5f);
                KitBorder(P, SX, Y, SW, SW, bNone ? BrightGold : In(SX, Y, SW, SW) ? ThemeGlow : Muted, bNone ? 2.f : 1.f);
                if (Click(SX, Y, SW, SW)) Pl.Tint = FLinearColor(0, 0, 0, 0);
                SX += SW + 3;
                for (const FLinearColor& C : KitSwatches)
                {
                    const bool bOn = Pl.Tint.A > 0 && Pl.Tint.Equals(C, .01f);
                    P.Rect(SX, Y, SW, SW, C);
                    KitBorder(P, SX, Y, SW, SW, bOn ? BrightGold : In(SX, Y, SW, SW) ? ThemeGlow : FLinearColor(0, 0, 0, .8f), bOn ? 2.f : 1.f);
                    if (Click(SX, Y, SW, SW)) Pl.Tint = C;
                    SX += SW + 3;
                }
                Y += SW + 8;
            }
            if (Pl.Tint.A > 0) SliderRow(5, TEXT("STRENGTH"), Pl.TintStrength, 0.f, 1.f, 1.f, FString::Printf(TEXT("%.0f%%"), Pl.TintStrength * 100.f));
            Pl.TintStrength = FMath::RoundToFloat(Pl.TintStrength * 100.f) / 100.f;
            if (Button(IX, Y + 2, 140, 26, TEXT("RESET EFFECT"), !Pl.IsDefault(), Gold, false, 9.f)) Pl = FCireKitEffectPlacement();
            const bool bSaved = !S.Work.Name.IsEmpty();
            if (Button(IX + 146, Y + 2, IW - 146, 26, bDirty ? TEXT("SAVE") : TEXT("SAVED"), bDirty, KitGood, false, 9.f))
            {
                if (!bSaved) StartNaming(EKitName::SaveAs, FString::Printf(TEXT("%s Loadout"), *Profile->DisplayName));
                else { const FString Name = S.Work.Name; if (KitCommit(S, Name)) KitStatus(S, TEXT("Saved the loadout and its effect placements."), KitGood); }
            }
            Y += 34;
            P.Text(TEXT("Right-click a slider to reset it  ·  the wheel nudges it"), IX, Y, 8.f, Muted * 1.1f, ECireFont::Body, false, false);
            Y += 16;
            if (!S.Status.IsEmpty() && Now - S.StatusAt < 9.0) P.Wrapped(S.Status, IX, Y, IW, 9.f, S.StatusColor, 3, ECireFont::Body, 2.f);

            // Live effect on the preview body (re-spawned when it ends, or the attach point / colour changes).
            if (PreviewHero && Mesh && S.Effects.Contains(S.Selected))
            {
                const FCireKitEffectPlacement Now1 = S.Effects[S.Selected];
                const float Base = DataScale * CireAbilityVFX::SpellEffectScale(World);
                const FString Key = FString::Printf(TEXT("%s|%s|%s|%s|%.3f|%u"), *S.Champion, *S.Selected, *ResolveAttach(Mesh, Now1.Attach).ToString(), *Now1.Tint.ToString(), Now1.TintStrength, S.TunerVersion);
                UFXSystemComponent* Live = S.Live.Get();
                const bool bWaitRelease = S.CastReleaseAt > 0 && Now < S.CastReleaseAt;
                UNiagaraComponent* Nia = ::Cast<UNiagaraComponent>(Live);
                const bool bFinished = Live && ((Nia && Nia->IsComplete()) || !Live->IsActive());
                if (!bWaitRelease && System && (Key != S.LiveKey || !Live || (bFinished && Now - S.LiveAt > 1.2) || Now - S.LiveAt > 4.0 || S.CastReleaseAt > 0))
                {
                    if (Live) Live->DestroyComponent();
                    Live = SpawnPlaced(PreviewHero, System, Now1, Base, false, Entry);
                    S.Live = Live; S.LiveKey = Key; S.LiveAt = Now; S.CastReleaseAt = -1;
                }
                if (Live) ApplyPlacement(Live, PreviewHero, Now1, Base, false);
            }
        }
    }
    for (auto It = S.Effects.CreateIterator(); It; ++It) if (It.Value().IsDefault() && It.Key() != S.Selected) It.RemoveCurrent();

    // ================= the champion's skill buttons =================
    {
        const float BW = VW - 2 * Pad;
        CireShopArt::Panel(P, Pad, BarY, BW, BarH);
        const FString Title = FString::Printf(TEXT("BASE LOADOUT  ·  %s  ·  %s PROFILE"), *(S.Work.Name.IsEmpty() ? FString(TEXT("NOT SAVED YET")) : S.Work.Name.ToUpper()), *S.Profile.ToUpper());
        CireShopArt::Spaced(P, P.Fit(Title, 8.5f, SlotX[FCireKitLoadout::PassiveSlot] + SlotS - Pad - 18, ECireFont::Display), Pad + 30, BarY + 10, 8.5f, .25f, CireShopArt::Filigree, ECireFont::Display, false, false);
        const int32 HoverSlot = SlotAt(M);
        const int32 DropSlot = S.bDragging ? SlotAt(Pointer) : INDEX_NONE;
        for (int32 I = 0; I < FCireKitLoadout::SlotCount; ++I)
        {
            const FString Id = S.Work.Slots.IsValidIndex(I) ? S.Work.Slots[I] : FString();
            const float X = SlotX[I];
            if (I == FCireKitLoadout::UltimateSlot) CireShopArt::Spaced(P, TEXT("ULTIMATE"), X + SlotS * .5f, SlotY + SlotS + 2, 7.f, .2f, FLinearColor(.78f, .56f, 1.f, 1), ECireFont::Display, true, false);
            if (I == FCireKitLoadout::PassiveSlot) CireShopArt::Spaced(P, TEXT("PASSIVE"), X + SlotS * .5f, SlotY + SlotS + 2, 7.f, .2f, FLinearColor(.84f, .80f, .68f, 1), ECireFont::Display, true, false);
            FCireIconSlot Icon;
            Icon.bEmpty = Id.IsEmpty();
            Icon.IconId = Id;
            Icon.IconTexture = Id.IsEmpty() ? nullptr : CireUIStyle::FindAbilityIcon(Id);
            Icon.Tint = I == FCireKitLoadout::UltimateSlot ? FLinearColor(.78f, .56f, 1.f, 1) : I == FCireKitLoadout::PassiveSlot ? FLinearColor(.75f, .75f, .8f, 1) : Gold;
            Icon.Kind = I == FCireKitLoadout::UltimateSlot ? ECireSlotKind::Ultimate : I == FCireKitLoadout::PassiveSlot ? ECireSlotKind::Passive : ECireSlotKind::Normal;
            Icon.KeyLabel = KitKeyLabel(HUD, I);
            Icon.bHover = HoverSlot == I || DropSlot == I;
            Icon.bGlow = S.SelectedSlot == I || (S.Tab == 1 && !Id.IsEmpty() && Id == S.Selected);
            CireUIStyle::IconSlot(P, X, SlotY, SlotS, Icon, Now);
            if (DropSlot == I)
            {
                const bool bOk = SlotAccepts(I, S.PressId);
                KitBorder(P, X - 3, SlotY - 3, SlotS + 6, SlotS + 6, bOk ? (SlotWarning(I, S.PressId).IsEmpty() ? KitGood : KitWarn) : KitBad, 2.f);
            }
            const FString Warn = SlotWarning(I, Id);
            const bool bEditBadge = HoverSlot == I && !Id.IsEmpty() && S.PressId.IsEmpty() && CireTunerLink::Available(PC); // the badge takes the corner
            if (!Warn.IsEmpty() && !bEditBadge) { P.Disc(X + SlotS - 5, SlotY + 5, 7.f, FLinearColor(.55f, .35f, .02f, 1), 16); P.Text(TEXT("!"), X + SlotS - 7, SlotY - 3, 10.f, Parchment, ECireFont::Bold, false, false); }
            if (!Id.IsEmpty() && S.Tab == 0 && (S.SelectedSlot == I || HoverSlot == I))
            {
                const float CX = X + 6, CY = SlotY + 6;
                const bool bOverX = In(CX - 7, CY - 7, 14, 14);
                P.Disc(CX, CY, 7.f, bOverX ? KitBad : FLinearColor(.25f, .05f, .04f, 1), 16);
                P.Line(CX - 3, CY - 3, CX + 3, CY + 3, Parchment, 1.4f); P.Line(CX - 3, CY + 3, CX + 3, CY - 3, Parchment, 1.4f);
                if (Click(CX - 7, CY - 7, 14, 14)) { S.Work.Slots[I].Reset(); Compact(S.Work); S.SelectedSlot = INDEX_NONE; HUD.PlayInterfaceSound(4, .35f); continue; }
            }
            if (HoverSlot == I && S.PressId.IsEmpty())
            {
                const FString Foot = S.Tab == 1 ? FString(TEXT("Click: place this ability's effect")) : Id.IsEmpty() ? FString(TEXT("Empty: click to select it, then click a scroll (or drag one here)."))
                    : FString(TEXT("Click: select (the next scroll replaces it)  ·  right-click: clear"));
                if (!Id.IsEmpty()) CireShopUI::TipSkill(HUD, Hero, Id, Warn.IsEmpty() ? Foot : Warn + TEXT("\n") + Foot);
                else HUD.SetRichTooltip(FCireTooltipSpec().Text(FString::Printf(TEXT("%s. %s"), *KitSlotWord(HUD, I), *Foot), Parchment));
            }
            const bool bOverEdit = bEditBadge && CireTunerLink::Badge(HUD, P, M, X + SlotS, SlotY, Id, bInteractive); // EDIT -> Ability Tuner
            if (!bOverEdit && Click(X, SlotY, SlotS, SlotS))
            {
                if (S.Tab == 1) { if (!Id.IsEmpty()) { S.Selected = Id; S.BoneCursor = -1; } }
                else S.SelectedSlot = S.SelectedSlot == I ? INDEX_NONE : I;
                HUD.PlayInterfaceSound(4, .35f);
            }
            if (HoverSlot == I && bRight && !Id.IsEmpty() && !bOverEdit) { S.Work.Slots[I].Reset(); Compact(S.Work); if (S.SelectedSlot == I) S.SelectedSlot = INDEX_NONE; }
        }
        const float TX = SlotX[FCireKitLoadout::PassiveSlot] + SlotS + 24, TW = VW - Pad - 16 - TX;
        if (TW > 120)
        {
            FString Info = FString::Printf(TEXT("%d / 8 buttons filled"), S.Work.Count());
            if (S.SelectedSlot != INDEX_NONE) Info += FString::Printf(TEXT("  ·  %s selected: click a scroll to put it there"), *KitSlotWord(HUD, S.SelectedSlot));
            P.Wrapped(Info, TX, SlotY + 2, TW, 9.f, S.SelectedSlot != INDEX_NONE ? BrightGold : Muted * 1.3f, 2, ECireFont::Body, 2.f);
            P.Wrapped(TEXT("Keys show your own keybinds. In a match the actives bind to the keys in this order."), TX, SlotY + 32, TW, 8.f, Muted * 1.1f, 2, ECireFont::Body, 2.f);
        }
    }

    // ================= drag and drop: finish =================
    if (!S.PressId.IsEmpty())
    {
        if (bMouseDown && FVector2D::Distance(Pointer, S.PressAt) > 8.f) S.bDragging = true;
        if (S.bDragging && bMouseDown)
        {
            CireShopUI::DrawSkillMedallion(P, S.PressId, Pointer.X, Pointer.Y, 22.f, Time);
            P.Text(ACireHero::SkillName(S.PressId), Pointer.X + 26, Pointer.Y - 8, 9.f, Parchment, ECireFont::Bold, true, true);
        }
        if (!bMouseDown)
        {
            const FString Id = S.PressId;
            if (S.bDragging)
            {
                const int32 Slot = SlotAt(Pointer);
                // An ultimate / passive dropped on a key button goes to its own button.
                if (Slot != INDEX_NONE) DoAssign(SlotAccepts(Slot, Id) ? Slot : TargetSlot(S.Work, Id, INDEX_NONE), Id);
            }
            else DoAssign(TargetSlot(S.Work, Id, S.SelectedSlot), Id);
            S.PressId.Reset(); S.bDragging = false;
        }
    }
    if (bClickThisFrame && HUD.HasClick()) S.Confirm.Reset(); // a click on nothing drops a pending confirmation

    // ================= naming modal =================
    if (bModal)
    {
        P.Rect(0, 0, VW, VH, FLinearColor(0, 0, 0, .55f));
        const float W = 440, H = 150, X = (VW - W) * .5f, Y = (VH - H) * .5f;
        CireShopArt::Panel(P, X, Y, W, H);
        static const TCHAR* Titles[] = {TEXT(""), TEXT("SAVE LOADOUT AS"), TEXT("RENAME LOADOUT"), TEXT("NEW KIT PROFILE"), TEXT("COPY PROFILE AS"), TEXT("RENAME PROFILE")};
        CireShopArt::Spaced(P, Titles[static_cast<int32>(S.Naming)], X + W * .5f, Y + 16, 11.f, .25f, TitleText, ECireFont::Display, true, true);
        const FString Text = Controller ? Controller->DraftSearch : FString();
        P.Rect(X + 24, Y + 46, W - 48, 30, FLinearColor(.02f, .025f, .035f, 1));
        KitBorder(P, X + 24, Y + 46, W - 48, 30, BrightGold, 1.4f);
        P.Text(Text + (FMath::Fmod(Time, 1.f) < .55f ? TEXT("|") : TEXT("")), X + 34, Y + 51, 11.f, Parchment, ECireFont::Bold, false, false);
        P.Text(TEXT("Type a name  ·  Enter to confirm  ·  Esc to cancel"), X + 24, Y + 82, 8.f, Muted * 1.3f, ECireFont::Body, false, false);
        const auto MB = [&](float BX, const TCHAR* Label, FLinearColor Accent)
        {
            const bool bOver = Pointer.X >= BX && Pointer.X < BX + 120 && Pointer.Y >= Y + 106 && Pointer.Y < Y + 134;
            CireUIStyle::Button(P, BX, Y + 106, 120, 28, Label, bOver ? ECireButtonState::Hover : ECireButtonState::Normal, Accent, 10.f);
            if (bOver && bLive && HUD.HasClick()) { HUD.TakeClick(); return true; }
            return false;
        };
        bool bOk = MB(X + W * .5f - 126, TEXT("OK"), KitGood);
        const bool bCancel = MB(X + W * .5f + 6, TEXT("CANCEL"), Gold);
        // Enter ends typing with the text; Esc empties it first (= cancel). A stray click elsewhere keeps typing.
        if (Controller && !Controller->bDraftSearch && !bCancel && !bOk) bOk = !Text.TrimStartAndEnd().IsEmpty();
        if (Controller && bLive && HUD.HasClick()) { HUD.TakeClick(); Controller->bDraftSearch = true; }
        const bool bEnd = bOk || bCancel || (Controller && !Controller->bDraftSearch);
        if (bEnd)
        {
            const FString Name = Text.TrimStartAndEnd().Left(40);
            const EKitName Mode = S.Naming;
            S.Naming = EKitName::None;
            if (Controller) { Controller->DraftSearch = S.StashSearch; Controller->bDraftSearch = false; }
            if (bOk && !bCancel && !Name.IsEmpty())
            {
                FCireKitData D = Data();
                FCireKitProfile* DP = D.FindProfile(S.Profile);
                if (Mode == EKitName::SaveAs)
                {
                    const FCireKitChampion* DC = DP ? DP->Champions.Find(S.Champion) : nullptr;
                    if (DC && DC->Find(Name) && Name != S.Work.Name) KitStatus(S, FString::Printf(TEXT("A preset named \"%s\" exists: pick another name."), *Name), KitBad);
                    else if (KitCommit(S, Name)) KitStatus(S, FString::Printf(TEXT("Saved \"%s\" for %s in %s."), *Name, *Profile->DisplayName, *S.Profile), KitGood);
                }
                else if (Mode == EKitName::Rename && DP)
                {
                    FCireKitChampion* C = DP->Champions.Find(S.Champion);
                    FCireKitLoadout* L = C ? C->Find(S.Work.Name) : nullptr;
                    if (!L) KitStatus(S, TEXT("Save the loadout first."), KitBad);
                    else if (C->Find(Name)) KitStatus(S, FString::Printf(TEXT("A preset named \"%s\" exists."), *Name), KitBad);
                    else
                    {
                        if (C->DefaultLoadout == L->Name) C->DefaultLoadout = Name;
                        L->Name = Name;
                        if (KitSaveData(S, D)) { S.Work.Name = S.Base.Name = Name; KitStatus(S, FString::Printf(TEXT("Renamed to \"%s\"."), *Name), KitGood); }
                    }
                }
                else
                {
                    if (D.FindProfile(Name) && !(Mode == EKitName::RenameProfile && Name.Equals(S.Profile, ESearchCase::IgnoreCase))) KitStatus(S, FString::Printf(TEXT("A profile named \"%s\" exists."), *Name), KitBad);
                    else
                    {
                        if (Mode == EKitName::RenameProfile && DP) DP->Name = Name;
                        else { FCireKitProfile New; New.Name = Name; if (Mode == EKitName::CopyProfile && DP) New.Champions = DP->Champions; D.Profiles.Add(New); }
                        if (KitSaveData(S, D))
                        {
                            S.Profile = Name;
                            KitLoadChampion(S, S.Champion);
                            KitStatus(S, Mode == EKitName::RenameProfile ? FString::Printf(TEXT("Profile renamed to %s."), *Name) : FString::Printf(TEXT("Profile %s created."), *Name), KitGood);
                        }
                    }
                }
            }
        }
    }

#if !UE_BUILD_SHIPPING
    // Gallery: -CireKitEditorGallery[=<champion>] captures both tabs (Saved/KitEditorGallery) and quits. Nothing is saved.
    if (S.bGallery && !S.bGalleryDone)
    {
        if (S.GalleryStart == 0)
        {
            S.GalleryStart = Now;
            FString Want;
            if (FParse::Value(FCommandLine::Get(), TEXT("CireKitEditorGallery="), Want) && CireChampionRoster::Find(Want)) KitLoadChampion(S, Want);
            // A representative loadout from the whole pool: the first actives, an ultimate and a passive.
            S.Work = FCireKitLoadout(); S.Work.Name = TEXT("Gallery Sample");
            for (const FCireAbilityDef& D : CireAbilityDB::All()) { const int32 T = TargetSlot(S.Work, D.Id); if (T != INDEX_NONE && S.Work.Slots[T].IsEmpty()) Assign(S.Work, T, D.Id); }
            S.Selected = S.Work.Slots[0];
            FCireKitEffectPlacement& Pl = S.Effects.FindOrAdd(S.Selected);
            Pl.Attach = TEXT("hand_r"); Pl.Offset = FVector(20, 0, 10); Pl.Scale = 1.3f; Pl.Tint = KitSwatches[4]; Pl.TintStrength = .8f;
            S.SelectedSlot = 2;
            S.Tab = 0;
            if (Controller) Controller->DraftSearch.Reset();
        }
        const bool bReady = S.Tab == 0 ? Now - S.GalleryStart > 14.0 : ((Stage && Stage->IsPreviewReady() && Stage->SecondsShown() > 3.0) || Now - S.GalleryStart > 40.0);
        const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("KitEditorGallery"));
        if (S.GalleryShotAt == 0 && bReady)
        {
            const FString File = FPaths::Combine(Dir, FString::Printf(TEXT("hero_creator_%d.png"), S.GalleryStage));
            FScreenshotRequest::RequestScreenshot(File, false, false, false, FIntRect(), true);
            S.GalleryShotAt = Now;
            UE_LOG(LogCireKitEditorUI, Display, TEXT("CIRE_KIT_EDITOR_GALLERY_SHOT %s champion=%s tab=%d buttons=%d live=%d"), *File, *S.Champion, S.Tab, S.Work.Count(), S.Live.IsValid() ? 1 : 0);
        }
        if (S.GalleryShotAt > 0 && Now - S.GalleryShotAt > 1.5)
        {
            ++S.GalleryStage;
            S.GalleryShotAt = 0;
            S.GalleryStart = Now;
            S.Tab = 1;
            if (S.GalleryStage >= 2)
            {
                S.bGalleryDone = true;
                UE_LOG(LogCireKitEditorUI, Display, TEXT("CIRE_KIT_EDITOR_GALLERY_PASS directory=%s"), *Dir);
                FPlatformMisc::RequestExitWithStatus(false, 0);
            }
        }
    }
#endif
}
