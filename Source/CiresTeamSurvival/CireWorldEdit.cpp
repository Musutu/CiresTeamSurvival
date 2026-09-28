// world-editor: CireWorldEdit.h (Docs/WorldEditor.md).
#include "CireWorldEdit.h"
#include "CireGame.h"
#include "CireLanePath.h"
#include "CireMapLayout.h"
#include "CireNav.h"
#include "CireRouteEditMode.h"
#include "CireTownMap.h"
#include "Components/AudioComponent.h"
#include "Components/LightComponentBase.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/Info.h"
#include "GameFramework/Volume.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "LevelInstance/LevelInstanceInterface.h"
#include "LevelInstance/LevelInstanceSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "NavigationSystem.h"
#include "NiagaraComponent.h"
#include "Particles/ParticleSystemComponent.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireWorldEdit, Log, All);

namespace ML = CireMapLayout;

// ================================================================================================= entries and sets
FBox2D FCireWorldEditEntry::Footprint(float Pad) const
{
    const FVector2D Half = FVector2D(FMath::Max(50.0, Size.X * .5), FMath::Max(50.0, Size.Y * .5)) + FVector2D(Pad);
    return FBox2D(FVector2D(Local) - Half, FVector2D(Local) + Half);
}
int32 FCireWorldEditSet::IndexOf(const FString& Key) const
{
    return Removed.IndexOfByPredicate([&](const FCireWorldEditEntry& E) { return E.Key() == Key; });
}

// ================================================================================================= files
FString CireWorldEdit::Dir() { return FPaths::ProjectContentDir() / TEXT("Data/WorldEdits"); }
FString CireWorldEdit::SanitizeName(const FString& Name)
{
    FString Out;
    for (const TCHAR C : Name.TrimStartAndEnd()) if (FChar::IsAlnum(C) || C == TEXT('_') || C == TEXT('-') || C == TEXT(' ')) Out.AppendChar(C == TEXT(' ') ? TEXT('_') : C);
    Out = Out.Left(48);
    if (Out.Equals(TEXT("off"), ESearchCase::IgnoreCase) || Out.Equals(TEXT("none"), ESearchCase::IgnoreCase)) return FString(); // reserved: no set
    return Out;
}
FString CireWorldEdit::NamedPath(const FString& Name) { return Dir() / (SanitizeName(Name) + TEXT(".json")); }
FString CireWorldEdit::SettingsPath()
{
    FString Path = FPaths::ProjectContentDir() / TEXT("Data/WorldEdit.json");
    FParse::Value(FCommandLine::Get(), TEXT("CireWorldEditJson="), Path);
    return Path;
}
FString CireWorldEdit::DraftPath() { return FPaths::ProjectSavedDir() / TEXT("WorldEditDraft.json"); }
TArray<FString> CireWorldEdit::ListNamed()
{
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Dir() / TEXT("*.json")), true, false);
    for (FString& F : Files) F = FPaths::GetBaseFilename(F);
    Files.Sort();
    return Files;
}

namespace
{
TArray<TSharedPtr<FJsonValue>> WENumbers(std::initializer_list<double> Values)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    for (const double V : Values) Out.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(V)));
    return Out;
}
FVector WEVector(const TSharedPtr<FJsonObject>& O, const TCHAR* Key)
{
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    FVector Out = FVector::ZeroVector;
    if (O->TryGetArrayField(Key, A) && A)
        for (int32 I = 0; I < FMath::Min(3, A->Num()); ++I) { double V = 0; if ((*A)[I]->TryGetNumber(V) && FMath::IsFinite(V)) Out[I] = V; }
    return Out;
}
}

FString CireWorldEdit::ToJson(const FCireWorldEditSet& Set)
{
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("name"), Set.Name);
    Root->SetStringField(TEXT("map"), Set.Map);
    Root->SetStringField(TEXT("notes"), TEXT("world-editor (Docs/WorldEditor.md): town pieces removed in matches. id = <realm sublevel>/<actor name>, realm 0 DAYLIGHT / 1 DARKNIGHT. local / size are realm-local centimetres (informative: the id is the key). actors / drawCalls: what removing it saves."));
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (const FCireWorldEditEntry& E : Set.Removed)
    {
        TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
        O->SetStringField(TEXT("id"), E.Id);
        O->SetNumberField(TEXT("realm"), E.Realm);
        O->SetStringField(TEXT("kind"), E.Kind);
        O->SetStringField(TEXT("label"), E.Label);
        O->SetArrayField(TEXT("local"), WENumbers({E.Local.X, E.Local.Y, E.Local.Z}));
        O->SetArrayField(TEXT("size"), WENumbers({E.Size.X, E.Size.Y, E.Size.Z}));
        O->SetNumberField(TEXT("actors"), E.Actors);
        O->SetNumberField(TEXT("drawCalls"), E.DrawCalls);
        Rows.Add(MakeShared<FJsonValueObject>(O));
    }
    Root->SetArrayField(TEXT("removed"), Rows);
    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Root, Writer);
    return Out;
}
bool CireWorldEdit::FromJson(const FString& Json, FCireWorldEditSet& Out, FString* Error)
{
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { if (Error) *Error = TEXT("not JSON"); return false; }
    FCireWorldEditSet S;
    Root->TryGetStringField(TEXT("name"), S.Name);
    Root->TryGetStringField(TEXT("map"), S.Map);
    const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
    if (Root->TryGetArrayField(TEXT("removed"), Rows) && Rows)
        for (const TSharedPtr<FJsonValue>& V : *Rows)
        {
            const TSharedPtr<FJsonObject>* O = nullptr;
            if (!V || !V->TryGetObject(O) || !O) continue;
            FCireWorldEditEntry E;
            if (!(*O)->TryGetStringField(TEXT("id"), E.Id) || E.Id.IsEmpty() || !E.Id.Contains(TEXT("/"))) continue;
            double Realm = 0; (*O)->TryGetNumberField(TEXT("realm"), Realm); E.Realm = FMath::Clamp(FMath::RoundToInt(Realm), 0, 1);
            (*O)->TryGetStringField(TEXT("kind"), E.Kind);
            (*O)->TryGetStringField(TEXT("label"), E.Label);
            E.Local = WEVector(*O, TEXT("local")); E.Size = WEVector(*O, TEXT("size"));
            double N = 0;
            if ((*O)->TryGetNumberField(TEXT("actors"), N)) E.Actors = FMath::Max(0, FMath::RoundToInt(N));
            if ((*O)->TryGetNumberField(TEXT("drawCalls"), N)) E.DrawCalls = FMath::Max(0, FMath::RoundToInt(N));
            if (!S.Contains(E.Key())) S.Removed.Add(MoveTemp(E)); // duplicates collapse
        }
    Out = MoveTemp(S);
    return true;
}
bool CireWorldEdit::Save(const FCireWorldEditSet& Set, const FString& Path, FString* Error)
{
    if (!FFileHelper::SaveStringToFile(ToJson(Set), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { if (Error) *Error = FString::Printf(TEXT("could not write %s"), *Path); return false; }
    return true;
}
bool CireWorldEdit::Load(FCireWorldEditSet& Out, const FString& Path, FString* Error)
{
    FString Json;
    if (!FFileHelper::LoadFileToString(Json, *Path)) { if (Error) *Error = FString::Printf(TEXT("no file %s"), *Path); return false; }
    return FromJson(Json, Out, Error);
}
FString CireWorldEdit::Hash(const FCireWorldEditSet& Set)
{
    if (Set.Removed.IsEmpty()) return FString();
    TArray<FString> Keys;
    for (const FCireWorldEditEntry& E : Set.Removed) Keys.Add(E.Key());
    Keys.Sort();
    return FMD5::HashAnsiString(*FString::Join(Keys, TEXT(";"))).Left(12);
}
FCireWorldEditSavings CireWorldEdit::Savings(const FCireWorldEditSet& Set)
{
    FCireWorldEditSavings S;
    for (const FCireWorldEditEntry& E : Set.Removed) { ++S.Units; S.Actors += E.Actors; S.DrawCalls += E.DrawCalls; }
    return S;
}

// ================================================================================================= settings
namespace
{
FCireWorldEditSettings GWESettings;
bool bWESettingsLoaded = false;
}
FCireWorldEditSettings CireWorldEdit::ParseSettings(const FString& Json)
{
    FCireWorldEditSettings S;
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) return S;
    Root->TryGetStringField(TEXT("active"), S.Active);
    S.Active = SanitizeName(S.Active);
    Root->TryGetBoolField(TEXT("mirror"), S.bMirror);
    Root->TryGetBoolField(TEXT("ghosts"), S.bGhosts);
    double N = 0;
    if (Root->TryGetNumberField(TEXT("protectPad"), N) && FMath::IsFinite(N)) S.ProtectPad = FMath::Clamp(float(N), 0.f, 5000.f);
    if (Root->TryGetNumberField(TEXT("boxSelectMaxSize"), N) && FMath::IsFinite(N)) S.BoxSelectMaxSize = FMath::Max(100.f, float(N));
    const TArray<TSharedPtr<FJsonValue>>* Words = nullptr;
    if (Root->TryGetArrayField(TEXT("protectWords"), Words) && Words)
    {
        S.ProtectWords.Reset();
        for (const auto& V : *Words) { FString W; if (V && V->TryGetString(W) && !W.IsEmpty()) S.ProtectWords.Add(W); }
    }
    return S;
}
const FCireWorldEditSettings& CireWorldEdit::Settings()
{
    if (!bWESettingsLoaded)
    {
        bWESettingsLoaded = true;
        FString Json;
        if (FFileHelper::LoadFileToString(Json, *SettingsPath())) GWESettings = ParseSettings(Json);
    }
    return GWESettings;
}
bool CireWorldEdit::SaveActiveName(const FString& Name, FString* Error)
{
    // Keep every other key of the file (Eric's tunables) and only replace "active".
    FString Json; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Json, *SettingsPath()) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("active"), SanitizeName(Name));
    FString Out;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    if (!FFileHelper::SaveStringToFile(Out, *SettingsPath(), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { if (Error) *Error = TEXT("could not write WorldEdit.json"); return false; }
    GWESettings.Active = SanitizeName(Name); bWESettingsLoaded = true;
    return true;
}

// ================================================================================================= stable ids
bool CireWorldEdit::ParseRealmLevel(const FString& ShortName, FString& OutSublevel, int32& OutRealm)
{
    // "SL_Houses_CireRealm0" (CireTownMap::LoadRealms names every realm copy deterministically; PIE may prefix it).
    const int32 At = ShortName.Find(TEXT("_CireRealm"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
    if (At <= 0) return false;
    const FString Digits = ShortName.Mid(At + 10);
    if (Digits.Len() != 1 || !FChar::IsDigit(Digits[0])) return false;
    OutRealm = FMath::Clamp(Digits[0] - TEXT('0'), 0, 1);
    OutSublevel = ShortName.Left(At);
    if (OutSublevel.StartsWith(TEXT("UEDPIE_"))) { const int32 U = OutSublevel.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, 7); if (U > 0) OutSublevel = OutSublevel.Mid(U + 1); }
    return !OutSublevel.IsEmpty();
}
bool CireWorldEdit::RealmLevel(const ULevel* Level, FString& OutSublevel, int32& OutRealm)
{
    if (!Level) return false;
    return ParseRealmLevel(FPackageName::GetShortName(Level->GetOutermost()->GetName()), OutSublevel, OutRealm);
}
FString CireWorldEdit::ActorId(const AActor* Actor, int32* OutRealm)
{
    FString Sublevel; int32 Realm = 0;
    if (!Actor || !RealmLevel(Actor->GetLevel(), Sublevel, Realm)) return FString();
    if (OutRealm) *OutRealm = Realm;
    return Sublevel + TEXT("/") + Actor->GetFName().ToString();
}
FString CireWorldEdit::KindOf(const FString& Label, bool bLevelInstance)
{
    auto Has = [&](std::initializer_list<const TCHAR*> Words) { for (const TCHAR* W : Words) if (Label.Contains(W)) return true; return false; };
    if (Has({TEXT("Castle"), TEXT("Tower"), TEXT("Gate"), TEXT("Keep"), TEXT("Wall"), TEXT("Rampart"), TEXT("Bridge")})) return TEXT("Fortification");
    if (Has({TEXT("House"), TEXT("Home"), TEXT("Shop"), TEXT("Tavern"), TEXT("Inn"), TEXT("Church"), TEXT("Chapel"), TEXT("Barn"), TEXT("Mill"), TEXT("Building"), TEXT("Smith"), TEXT("Stable")})) return TEXT("Building");
    if (Has({TEXT("Tree"), TEXT("Bush"), TEXT("Plant"), TEXT("Grass"), TEXT("Rock"), TEXT("Cliff"), TEXT("Stone"), TEXT("Flower"), TEXT("Ivy"), TEXT("Foliage")})) return TEXT("Nature");
    return bLevelInstance ? TEXT("Building") : TEXT("Prop");
}

// ================================================================================================= editing
int32 CireWorldEdit::Remove(FCireWorldEditDoc& Doc, const TArray<FCireWorldEditEntry>& Entries, const FString& What)
{
    FCireWorldEditStep Step; Step.What = What;
    for (const FCireWorldEditEntry& E : Entries)
        if (!Doc.Set.Contains(E.Key()) && !Step.Removed.ContainsByPredicate([&](const FCireWorldEditEntry& X) { return X.Key() == E.Key(); })) Step.Removed.Add(E);
    if (Step.Removed.IsEmpty()) return 0;
    Doc.Set.Removed.Append(Step.Removed);
    const int32 N = Step.Removed.Num();
    Doc.Undo.Add(MoveTemp(Step)); if (Doc.Undo.Num() > 200) Doc.Undo.RemoveAt(0);
    Doc.Redo.Reset(); ++Doc.Revision;
    return N;
}
int32 CireWorldEdit::Restore(FCireWorldEditDoc& Doc, const TArray<FString>& Keys, const FString& What)
{
    FCireWorldEditStep Step; Step.What = What;
    for (const FString& K : Keys)
    {
        const int32 I = Doc.Set.IndexOf(K);
        if (I == INDEX_NONE) continue;
        Step.Restored.Add(Doc.Set.Removed[I]);
        Doc.Set.Removed.RemoveAt(I);
    }
    if (Step.Restored.IsEmpty()) return 0;
    const int32 N = Step.Restored.Num();
    Doc.Undo.Add(MoveTemp(Step)); if (Doc.Undo.Num() > 200) Doc.Undo.RemoveAt(0);
    Doc.Redo.Reset(); ++Doc.Revision;
    return N;
}
namespace
{
void WEApply(FCireWorldEditSet& Set, const FCireWorldEditStep& Step, bool bForward)
{
    const TArray<FCireWorldEditEntry>& Add = bForward ? Step.Removed : Step.Restored;
    const TArray<FCireWorldEditEntry>& Drop = bForward ? Step.Restored : Step.Removed;
    for (const FCireWorldEditEntry& E : Drop) { const int32 I = Set.IndexOf(E.Key()); if (I != INDEX_NONE) Set.Removed.RemoveAt(I); }
    for (const FCireWorldEditEntry& E : Add) if (!Set.Contains(E.Key())) Set.Removed.Add(E);
}
}
bool CireWorldEdit::Undo(FCireWorldEditDoc& Doc, FString* OutWhat)
{
    if (Doc.Undo.IsEmpty()) return false;
    FCireWorldEditStep Step = Doc.Undo.Pop();
    WEApply(Doc.Set, Step, false);
    if (OutWhat) *OutWhat = Step.What;
    Doc.Redo.Add(MoveTemp(Step)); ++Doc.Revision;
    return true;
}
bool CireWorldEdit::Redo(FCireWorldEditDoc& Doc, FString* OutWhat)
{
    if (Doc.Redo.IsEmpty()) return false;
    FCireWorldEditStep Step = Doc.Redo.Pop();
    WEApply(Doc.Set, Step, true);
    if (OutWhat) *OutWhat = Step.What;
    Doc.Undo.Add(MoveTemp(Step)); ++Doc.Revision;
    return true;
}
void CireWorldEdit::ReplaceSet(FCireWorldEditDoc& Doc, const FCireWorldEditSet& Set, const FString& What)
{
    FCireWorldEditStep Step; Step.What = What;
    Step.Restored = Doc.Set.Removed;
    Step.Removed = Set.Removed;
    Doc.Set.Removed = Set.Removed; Doc.Set.Name = Set.Name; Doc.Set.Map = Set.Map;
    Doc.Undo.Add(MoveTemp(Step)); if (Doc.Undo.Num() > 200) Doc.Undo.RemoveAt(0);
    Doc.Redo.Reset(); ++Doc.Revision;
}
FCireWorldEditEntry CireWorldEdit::TwinOf(const FCireWorldEditEntry& Entry)
{
    FCireWorldEditEntry T = Entry; T.Realm = 1 - FMath::Clamp(Entry.Realm, 0, 1);
    return T;
}
TArray<FCireWorldEditEntry> CireWorldEdit::WithTwins(const TArray<FCireWorldEditEntry>& Entries, TFunctionRef<bool(const FString& Key)> TwinExists, int32* OutTwins)
{
    TArray<FCireWorldEditEntry> Out = Entries;
    int32 Twins = 0;
    for (const FCireWorldEditEntry& E : Entries)
    {
        const FCireWorldEditEntry T = TwinOf(E);
        if (!TwinExists(T.Key()) || Out.ContainsByPredicate([&](const FCireWorldEditEntry& X) { return X.Key() == T.Key(); })) continue;
        Out.Add(T); ++Twins;
    }
    if (OutTwins) *OutTwins = Twins;
    return Out;
}

// ================================================================================================= protection
namespace
{
bool WESegmentHitsBox(const FVector2D& A, const FVector2D& B, const FBox2D& Box)
{
    // Liang-Barsky clip of the segment against the box.
    double T0 = 0, T1 = 1;
    const FVector2D D = B - A;
    const double P[4] = {-D.X, D.X, -D.Y, D.Y};
    const double Q[4] = {A.X - Box.Min.X, Box.Max.X - A.X, A.Y - Box.Min.Y, Box.Max.Y - A.Y};
    for (int32 I = 0; I < 4; ++I)
    {
        if (FMath::Abs(P[I]) < 1e-9) { if (Q[I] < 0) return false; continue; }
        const double R = Q[I] / P[I];
        if (P[I] < 0) T0 = FMath::Max(T0, R); else T1 = FMath::Min(T1, R);
        if (T0 > T1) return false;
    }
    return true;
}
bool WECircleHitsBox(const FVector2D& C, double R, const FBox2D& Box)
{
    const FVector2D Near(FMath::Clamp(C.X, Box.Min.X, Box.Max.X), FMath::Clamp(C.Y, Box.Min.Y, Box.Max.Y));
    return FVector2D::DistSquared(Near, C) <= R * R;
}
bool WEHardMarker(FName Type)
{
    return Type == ML::PlayerSpawn || Type == ML::MonsterSpawn || Type == ML::ChallengePack || Type == ML::Vendor || Type == ML::Objective
        || Type == ML::BossSpawn || Type == ML::Rift || Type == ML::Respawn || Type == ML::RecallPoint;
}
bool WEWordListed(const FString& Text, const TArray<FString>& Words)
{
    for (const FString& W : Words) if (!W.IsEmpty() && Text.Contains(W)) return true;
    return false;
}
}
FCireWorldEditGuard CireWorldEdit::Guard(const FCireMapLayout& Layout, const FCireWorldEditEntry& Entry, const FCireWorldEditSettings& S)
{
    FCireWorldEditGuard G;
    const FBox2D Box = Entry.Footprint(S.ProtectPad);
    const bool bFort = WEWordListed(Entry.Label, S.ProtectWords) || WEWordListed(Entry.Id, S.ProtectWords);
    auto Raise = [&](ECireWorldEditGuard Level, const FString& Why) { if (Level > G.Level) { G.Level = Level; G.Why = Why; } };
    for (const FCireMapMarker& M : Layout.Markers)
    {
        if (!ML::ShownInRealm(M, Entry.Realm)) continue;
        const FString Label = ML::DisplayLabel(Layout, M);
        if (WEHardMarker(M.Type))
        {
            bool bIn = M.Points.Num() == 0 && (Box.IsInside(M.Position) || (M.Type == ML::Objective && M.Radius > 0 && WECircleHitsBox(M.Position, M.Radius, Box)));
            for (const FVector2D& P : M.Points) bIn |= Box.IsInside(P);
            if (M.Type == ML::Vendor) bIn |= Box.IsInside(M.SignPos) || Box.IsInside(M.StallPos);
            if (bIn) Raise(ECireWorldEditGuard::Protected, FString::Printf(TEXT("PROTECTED: %s stands in it"), *Label));
        }
        else if (M.Type == ML::MonsterPath)
        {
            bool bCross = false;
            for (int32 I = 0; I + 1 < M.Points.Num() && !bCross; ++I) bCross = WESegmentHitsBox(M.Points[I], M.Points[I + 1], Box);
            if (!bCross) continue;
            if (bFort) Raise(ECireWorldEditGuard::Protected, FString::Printf(TEXT("PROTECTED: castle / gate piece on %s"), *Label));
            else Raise(ECireWorldEditGuard::Warn, FString::Printf(TEXT("%s runs through it: waves walk where it stood (VALIDATE checks the paths)"), *Label));
        }
    }
    if (bFort) Raise(ECireWorldEditGuard::Warn, TEXT("castle / gate piece: check the objective area and the routes after removing it"));
    return G;
}
TArray<FCireLayoutIssue> CireWorldEdit::Issues(const FCireMapLayout& Layout, const FCireWorldEditSet& Set, const FCireWorldEditSettings& S)
{
    TArray<FCireLayoutIssue> Out;
    for (const FCireWorldEditEntry& E : Set.Removed)
    {
        const FBox2D Box = E.Footprint();
        const FString What = FString::Printf(TEXT("%s (%s)"), *E.Label.Left(32), *ML::RealmName(E.Realm));
        const FCireWorldEditGuard G = Guard(Layout, E, S);
        if (G.Level == ECireWorldEditGuard::Protected) Out.Add({true, E.Realm + 1, FString(), FString::Printf(TEXT("Removed %s but %s: restore it or move the marker"), *What, *G.Why.RightChop(11))});
        for (const FCireMapMarker& M : Layout.Markers)
        {
            if (!ML::ShownInRealm(M, E.Realm) || M.Type == ML::PlayBounds || M.Type == ML::Zone || M.Type == ML::Blocker) continue;
            const int32 Team = M.Owner == ECireMarkerOwner::Team1 ? 1 : M.Owner == ECireMarkerOwner::Team2 ? 2 : 0;
            if (M.Type == ML::MonsterPath)
            {
                bool bCross = false;
                for (int32 I = 0; I + 1 < M.Points.Num() && !bCross; ++I) bCross = WESegmentHitsBox(M.Points[I], M.Points[I + 1], Box);
                if (bCross) Out.Add({false, Team, M.Id, FString::Printf(TEXT("%s now runs where %s stood (VALIDATE: blocked / unblocked)"), *ML::DisplayLabel(Layout, M), *What)});
                continue;
            }
            bool bIn = Box.IsInside(M.Position);
            for (const FVector2D& P : M.Points) bIn |= Box.IsInside(P);
            if (bIn && G.Level != ECireWorldEditGuard::Protected)
                Out.Add({false, Team, M.Id, FString::Printf(TEXT("%s stood in / on %s, which is removed: it may float or lose its ground"), *ML::DisplayLabel(Layout, M), *What)});
        }
    }
    return Out;
}

// ================================================================================================= the loaded town
namespace
{
bool WEIsLandscape(const AActor* A) { return A->GetClass()->GetName().Contains(TEXT("Landscape")); }
bool WERenders(const AActor* A)
{
    if (A->PrimaryActorTick.bCanEverTick && !A->IsA<AInfo>()) return true;
    TInlineComponentArray<UActorComponent*> Components(A);
    for (const UActorComponent* C : Components) if (C->IsA<UPrimitiveComponent>() || C->IsA<ULightComponentBase>() || C->IsA<UFXSystemComponent>() || C->IsA<UAudioComponent>()) return true;
    return false;
}
bool WECandidate(const AActor* A)
{
    if (!A || A->IsActorBeingDestroyed() || A->IsA<AWorldSettings>() || A->IsA<AVolume>() || WEIsLandscape(A)) return false;
    if (Cast<ILevelInstanceInterface>(A)) return true;
    if (A->IsA<AInfo>() || A->GetAttachParentActor()) return false;
    return WERenders(A);
}
FBox WEActorBox(const AActor* A)
{
    FBox Box = A->GetComponentsBoundingBox(true);
    if (!Box.IsValid || Box.GetExtent().GetMax() > 1e6) return FBox(ForceInit);
    return Box;
}
int32 WEDrawCalls(const AActor* A)
{
    int32 N = 0;
    TInlineComponentArray<UPrimitiveComponent*> Prims(A);
    for (const UPrimitiveComponent* P : Prims)
    {
        if (!P->IsRegistered() || !P->IsVisible()) continue;
        if (const auto* SM = Cast<UStaticMeshComponent>(P)) { if (SM->GetStaticMesh()) N += FMath::Max(1, SM->GetNumMaterials()); }
        else if (const auto* SK = Cast<USkinnedMeshComponent>(P)) N += FMath::Max(1, SK->GetNumMaterials());
        else ++N;
    }
    return N;
}
FString WELabel(AActor* A)
{
    if (auto* LI = Cast<ILevelInstanceInterface>(A)) return FPackageName::GetShortName(LI->GetWorldAssetPackage());
    if (auto* SMA = Cast<AStaticMeshActor>(A))
        if (SMA->GetStaticMeshComponent() && SMA->GetStaticMeshComponent()->GetStaticMesh()) return SMA->GetStaticMeshComponent()->GetStaticMesh()->GetName();
#if WITH_EDITOR
    const FString L = A->GetActorLabel(false);
    if (!L.IsEmpty()) return L;
#endif
    return A->GetClass()->GetName().Replace(TEXT("_C"), TEXT(""));
}
/** The actor of a unit in this world by id and realm (null when it is not loaded or was destroyed). */
AActor* WEFindUnit(UWorld* World, const FString& Id, int32 Realm)
{
    if (!World) return nullptr;
    FString Level, Name;
    if (!Id.Split(TEXT("/"), &Level, &Name)) return nullptr;
    for (ULevel* L : World->GetLevels())
    {
        FString Sub; int32 R = 0;
        if (!L || !CireWorldEdit::RealmLevel(L, Sub, R) || R != Realm || Sub != Level) continue;
        for (AActor* A : L->Actors) if (A && !A->IsActorBeingDestroyed() && A->GetFName().ToString() == Name) return A;
    }
    return nullptr;
}
}

void CireWorldEdit::ForEachUnitActor(UWorld* World, AActor* Unit, TFunctionRef<void(AActor*)> Visit)
{
    if (!IsValid(Unit)) return;
    Visit(Unit);
    if (auto* LI = Cast<ILevelInstanceInterface>(Unit))
    {
        auto* Sub = World ? World->GetSubsystem<ULevelInstanceSubsystem>() : nullptr;
        if (!Sub || !Sub->IsLoaded(LI)) return;
        TArray<AActor*> Inside;
        Sub->ForEachActorInLevelInstance(LI, [&](AActor* A) { if (IsValid(A)) Inside.Add(A); return true; });
        for (AActor* A : Inside)
        {
            if (Cast<ILevelInstanceInterface>(A)) ForEachUnitActor(World, A, Visit); // nested pieces
            else Visit(A);
        }
        return;
    }
    TArray<AActor*> Attached;
    Unit->GetAttachedActors(Attached, true, true);
    for (AActor* A : Attached) if (IsValid(A)) Visit(A);
}
bool CireWorldEdit::DescribeUnit(UWorld* World, AActor* Unit, FCireWorldUnit& Out)
{
    int32 Realm = 0;
    const FString Id = ActorId(Unit, &Realm);
    if (Id.IsEmpty()) return false;
    Out = FCireWorldUnit();
    Out.Id = Id; Out.Realm = Realm; Out.Actor = Unit;
    Out.bLevelInstance = Cast<ILevelInstanceInterface>(Unit) != nullptr;
    Out.Label = WELabel(Unit);
    Out.Kind = KindOf(Out.Label, Out.bLevelInstance);
    ForEachUnitActor(World, Unit, [&](AActor* A)
    {
        if (Cast<ILevelInstanceInterface>(A) || !WERenders(A)) return;
        ++Out.Actors; Out.DrawCalls += WEDrawCalls(A);
        const FBox B = WEActorBox(A);
        if (B.IsValid) Out.Bounds += B;
    });
#if WITH_EDITOR
    if (!Out.Bounds.IsValid && Out.bLevelInstance)
        if (auto* Sub = World ? World->GetSubsystem<ULevelInstanceSubsystem>() : nullptr) { FBox B(ForceInit); if (Sub->GetLevelInstanceBounds(Cast<ILevelInstanceInterface>(Unit), B) && B.IsValid) Out.Bounds = B; }
#endif
    if (!Out.Bounds.IsValid) Out.Bounds = FBox(Unit->GetActorLocation() - FVector(50), Unit->GetActorLocation() + FVector(50));
    return true;
}
FCireWorldEditEntry CireWorldEdit::EntryOf(const FCireWorldUnit& U)
{
    FCireWorldEditEntry E;
    E.Id = U.Id; E.Realm = U.Realm; E.Kind = U.Kind; E.Label = U.Label;
    const FVector C = U.Bounds.GetCenter();
    const FVector2D L = CireLanePath::ToLocal(U.Realm, C);
    E.Local = FVector(L.X, L.Y, C.Z - CireTownMap::RealmOrigin(U.Realm).Z);
    E.Size = U.Bounds.GetSize();
    E.Actors = U.Actors; E.DrawCalls = U.DrawCalls;
    return E;
}
void CireWorldEdit::BuildIndex(UWorld* World, TArray<FCireWorldUnit>& Out)
{
    Out.Reset();
    if (!World) return;
    const double Started = FPlatformTime::Seconds();
    for (ULevel* Level : World->GetLevels())
    {
        FString Sub; int32 Realm = 0;
        if (!Level || !Level->bIsVisible || !RealmLevel(Level, Sub, Realm)) continue;
        for (AActor* A : Level->Actors)
        {
            if (!WECandidate(A)) continue;
            FCireWorldUnit U;
            if (DescribeUnit(World, A, U) && (U.bLevelInstance || U.Actors > 0)) Out.Add(MoveTemp(U));
        }
    }
    UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_INDEX units=%d ms=%.0f"), Out.Num(), (FPlatformTime::Seconds() - Started) * 1000.0);
}
AActor* CireWorldEdit::UnitActorOf(UWorld* World, AActor* Hit)
{
    if (!IsValid(Hit) || !World) return nullptr;
    AActor* Outer = nullptr;
    if (auto* Sub = World->GetSubsystem<ULevelInstanceSubsystem>())
        Sub->ForEachLevelInstanceAncestors(Hit, [&](ILevelInstanceInterface* LI) { Outer = Cast<AActor>(LI); return true; }); // the last one is the outermost
    if (Outer && !ActorId(Outer).IsEmpty()) return Outer;
    AActor* Root = Hit;
    while (Root->GetAttachParentActor()) Root = Root->GetAttachParentActor();
    return !ActorId(Root).IsEmpty() && WECandidate(Root) ? Root : nullptr;
}

// ================================================================================================= reversible removal
namespace
{
struct FWEOriginal { bool bHidden = false, bCollision = true; };
TMap<TWeakObjectPtr<AActor>, FWEOriginal> GWEOriginal;
void WENavUpdate(AActor* A)
{
    if (A && A->GetWorld() && FNavigationSystem::GetCurrent<UNavigationSystemV1>(A->GetWorld())) UNavigationSystemV1::UpdateActorAndComponentsInNavOctree(*A, false);
}
}
void CireWorldEdit::SetUnitRemoved(UWorld* World, AActor* Unit, bool bRemoved, bool bGhost)
{
    ForEachUnitActor(World, Unit, [&](AActor* A)
    {
        const TWeakObjectPtr<AActor> Key(A);
        if (bRemoved)
        {
            if (!GWEOriginal.Contains(Key)) GWEOriginal.Add(Key, {A->IsHidden(), A->GetActorEnableCollision()});
            A->SetActorHiddenInGame(bGhost ? GWEOriginal[Key].bHidden : true); // a ghost draws (as it did), never collides
            if (A->GetActorEnableCollision()) { A->SetActorEnableCollision(false); WENavUpdate(A); }
        }
        else if (const FWEOriginal* O = GWEOriginal.Find(Key))
        {
            A->SetActorHiddenInGame(O->bHidden);
            if (O->bCollision && !A->GetActorEnableCollision()) { A->SetActorEnableCollision(true); WENavUpdate(A); }
            GWEOriginal.Remove(Key);
        }
    });
    for (auto It = GWEOriginal.CreateIterator(); It; ++It) if (!It->Key.IsValid()) It.RemoveCurrent();
}

// ================================================================================================= runtime: the active set
namespace
{
FCireWorldEditSet GWECurrent;             // the set this load applies / the world runs
bool bWEFrozen = false;
bool bWEChosen = false; FString GWEChosen; // ApplySet / the server's choice
FCireWorldEditSet GWEOverride; bool bWEOverride = false;
TSet<FString> GWEKeys;
struct FWEStats { int32 LiSkipped = 0, LiUnloaded = 0, Destroyed = 0, Hidden = 0; double Ms = 0; };
FWEStats GWEStats[2];
TSet<FString> GWELoadRemoved;              // keys removed at load (destroyed actors, unloaded Level Instances)
TMap<FString, TWeakObjectPtr<AActor>> GWEUnloaded; // key -> Level Instance actor (can load again)
TSet<FString> GWELiveRemoved;              // keys removed live (hidden, reversible)
TSet<TWeakObjectPtr<AActor>> GWEDecided;
FString GWEServerHash;

bool WEHasParam(const TCHAR* Name) { return FParse::Param(FCommandLine::Get(), Name); }
FString WEResolveName()
{
    if (bWEChosen) return GWEChosen;
    FString V;
    if (FParse::Value(FCommandLine::Get(), TEXT("CireWorldEdit="), V)) return CireWorldEdit::SanitizeName(V);
    return CireWorldEdit::Settings().Active;
}
bool WELoadNamed(const FString& Name, FCireWorldEditSet& Out)
{
    Out = FCireWorldEditSet();
    if (Name.IsEmpty()) return true;
    FString Error;
    if (!CireWorldEdit::Load(Out, CireWorldEdit::NamedPath(Name), &Error)) { Out = FCireWorldEditSet(); UE_LOG(LogCireWorldEdit, Warning, TEXT("CIRE_WORLD_EDIT_MISSING_SET %s: %s"), *Name, *Error); return false; }
    if (Out.Name.IsEmpty()) Out.Name = Name;
    return true;
}
void WERebuildKeys() { GWEKeys.Reset(); for (const FCireWorldEditEntry& E : CireWorldEdit::Current().Removed) GWEKeys.Add(E.Key()); }
bool WEAppliesHere() { return CireTownMap::IsActive() && !CireRouteEditMode::IsActive() && !WEHasParam(TEXT("CireNoWorldEdit")); }
ULevelInstanceSubsystem* WESub(UWorld* World) { return World ? World->GetSubsystem<ULevelInstanceSubsystem>() : nullptr; }
void WEUnloadInstance(UWorld* World, AActor* A, const FString& Key, FWEStats& S)
{
    auto* Sub = WESub(World); auto* LI = Cast<ILevelInstanceInterface>(A);
    if (!Sub || !LI) return;
    if (Sub->IsLoaded(LI)) ++S.LiUnloaded; else ++S.LiSkipped;
    Sub->RequestUnloadLevelInstance(LI); // drops a queued load, or unloads it (every nested piece goes with it)
    GWELoadRemoved.Add(Key); GWEUnloaded.Add(Key, A);
}
void WEDestroyUnit(UWorld* World, AActor* A, const FString& Key, FWEStats& S)
{
    TArray<AActor*> All;
    CireWorldEdit::ForEachUnitActor(World, A, [&](AActor* X) { All.Add(X); });
    for (int32 I = All.Num() - 1; I >= 0; --I) // leaves first
    {
        AActor* X = All[I];
        if (!IsValid(X)) continue;
        if (X->Destroy()) ++S.Destroyed;
        else { X->SetActorHiddenInGame(true); X->SetActorEnableCollision(false); X->SetActorTickEnabled(false); ++S.Hidden; }
    }
    GWELoadRemoved.Add(Key);
}
/** Is this unit's outermost Level Instance in the set? (a nested level streamed late) */
void WECatchLateInstance(UWorld* World, ULevel* Level)
{
    auto* Sub = WESub(World);
    if (!Sub) return;
    AActor* Outer = nullptr;
    for (ILevelInstanceInterface* LI = Sub->GetOwningLevelInstance(Level); LI; )
    {
        Outer = Cast<AActor>(LI);
        LI = Outer ? Sub->GetOwningLevelInstance(Outer->GetLevel()) : nullptr;
    }
    if (!Outer) return;
    int32 Realm = 0; const FString Id = CireWorldEdit::ActorId(Outer, &Realm);
    const FString Key = FString::Printf(TEXT("%d|%s"), Realm, *Id);
    if (Id.IsEmpty() || !GWEKeys.Contains(Key)) return;
    if (auto* LI = Cast<ILevelInstanceInterface>(Outer)) { Sub->RequestUnloadLevelInstance(LI); GWELoadRemoved.Add(Key); GWEUnloaded.Add(Key, Outer); }
}
void WESyncReplicated(UWorld* World)
{
    if (!World || World->GetNetMode() == NM_Client) return;
    for (TActorIterator<ACireWorld> It(World); It; ++It)
    {
        It->WorldEditSet = GWECurrent.Name;
        It->WorldEditHash = CireWorldEdit::Hash(GWECurrent);
    }
}
}

FString CireWorldEdit::ActiveSet() { return bWEFrozen ? GWECurrent.Name : WEResolveName(); }
const FCireWorldEditSet& CireWorldEdit::Current()
{
    if (bWEOverride) return GWEOverride;
    if (!bWEFrozen)
    {
        // Before the town loads: the set the load will apply (cached per name).
        static FString CachedName = TEXT("\x01");
        const FString Name = WEResolveName();
        if (Name != CachedName) { CachedName = Name; WELoadNamed(Name, GWECurrent); }
    }
    return GWECurrent;
}
bool CireWorldEdit::Active()
{
    if (bWEOverride) return GWEOverride.Removed.Num() > 0;
    return WEAppliesHere() && Current().Removed.Num() > 0;
}
FString CireWorldEdit::Signature()
{
    if (!Active()) return FString();
    return FString::Printf(TEXT("worldedit1 %s;"), *Hash(Current()));
}
void CireWorldEdit::SetOverride(const FCireWorldEditSet* Set)
{
    bWEOverride = Set != nullptr;
    GWEOverride = Set ? *Set : FCireWorldEditSet();
}
void CireWorldEdit::BeginLoad(UWorld* World)
{
    WELoadNamed(WEResolveName(), GWECurrent);
    bWEFrozen = true;
    for (FWEStats& S : GWEStats) S = FWEStats();
    GWELoadRemoved.Reset(); GWEUnloaded.Reset(); GWELiveRemoved.Reset(); GWEDecided.Reset();
    WERebuildKeys();
    if (Active())
    {
        const FCireWorldEditSavings Sv = Savings(GWECurrent);
        UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_BEGIN set=%s units=%d hash=%s saves_actors=%d saves_draw_calls=%d"), *GWECurrent.Name, Sv.Units, *Hash(GWECurrent), Sv.Actors, Sv.DrawCalls);
    }
    else UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_OFF set=%s (no set, an empty set, the layout editor or -CireNoWorldEdit): nothing is removed"), GWECurrent.Name.IsEmpty() ? TEXT("none") : *GWECurrent.Name);
}
int32 CireWorldEdit::FilterLevelInstances(UWorld* World)
{
    if (!World || !Active()) return 0;
    int32 Count = 0;
    for (ULevel* Level : World->GetLevels())
    {
        FString Sub; int32 Realm = 0;
        if (!Level || !Level->bIsVisible || !RealmLevel(Level, Sub, Realm)) continue;
        for (AActor* A : Level->Actors)
        {
            if (!A || !Cast<ILevelInstanceInterface>(A) || GWEDecided.Contains(A)) continue;
            const FString Key = FString::Printf(TEXT("%d|%s/%s"), Realm, *Sub, *A->GetFName().ToString());
            if (!GWEKeys.Contains(Key)) continue;
            GWEDecided.Add(A);
            WEUnloadInstance(World, A, Key, GWEStats[Realm]); ++Count;
        }
    }
    return Count;
}
void CireWorldEdit::ApplyLevel(UWorld* World, ULevel* Level)
{
    if (!World || !Level || Level == World->PersistentLevel || !Active()) return;
    const double Started = FPlatformTime::Seconds();
    FString Sub; int32 Realm = 0;
    if (!RealmLevel(Level, Sub, Realm)) { WECatchLateInstance(World, Level); return; }
    TArray<TPair<AActor*, FString>> Doomed;
    for (AActor* A : Level->Actors)
    {
        if (!A || A->IsActorBeingDestroyed()) continue;
        const FString Key = FString::Printf(TEXT("%d|%s/%s"), Realm, *Sub, *A->GetFName().ToString());
        if (!GWEKeys.Contains(Key) || GWELoadRemoved.Contains(Key)) continue;
        if (Cast<ILevelInstanceInterface>(A)) { GWEDecided.Add(A); WEUnloadInstance(World, A, Key, GWEStats[Realm]); }
        else Doomed.Emplace(A, Key);
    }
    for (const auto& D : Doomed) if (IsValid(D.Key)) WEDestroyUnit(World, D.Key, D.Value, GWEStats[Realm]);
    GWEStats[Realm].Ms += (FPlatformTime::Seconds() - Started) * 1000.0;
}
void CireWorldEdit::LogSummary(UWorld* World)
{
    if (!Active()) return;
    int32 Missing = 0;
    for (const FCireWorldEditEntry& E : Current().Removed)
        if (!GWELoadRemoved.Contains(E.Key())) { ++Missing; if (Missing <= 8) UE_LOG(LogCireWorldEdit, Warning, TEXT("CIRE_WORLD_EDIT_NOT_FOUND %s (not in the loaded town: trimmed, or the pack changed)"), *E.Key()); }
    for (int32 T = 0; T < 2; ++T)
        UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT realm=%d li_skipped=%d li_unloaded=%d actors_destroyed=%d hidden=%d ms=%.0f"), T, GWEStats[T].LiSkipped, GWEStats[T].LiUnloaded, GWEStats[T].Destroyed, GWEStats[T].Hidden, GWEStats[T].Ms);
    const FCireWorldEditSavings Sv = Savings(Current());
    UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_SAVINGS set=%s units=%d removed=%d not_found=%d actors=%d draw_calls=%d netmode=%d"), *Current().Name, Sv.Units, GWELoadRemoved.Num(), Missing, Sv.Actors, Sv.DrawCalls, World ? int32(World->GetNetMode()) : -1);
}
bool CireWorldEdit::IsGone(UWorld* World, const FCireWorldEditEntry& Entry)
{
    AActor* A = WEFindUnit(World, Entry.Id, Entry.Realm);
    if (!A) return true; // destroyed
    if (auto* LI = Cast<ILevelInstanceInterface>(A))
    {
        auto* Sub = WESub(World);
        if (Sub && !Sub->IsLoaded(LI)) return true;
        bool bAllOff = true; // loaded: gone only when removed live (every piece hidden, no collision)
        ForEachUnitActor(World, A, [&](AActor* X) { if (!Cast<ILevelInstanceInterface>(X) && WERenders(X) && (!X->IsHidden() || X->GetActorEnableCollision())) bAllOff = false; });
        return bAllOff;
    }
    return A->IsHidden() && !A->GetActorEnableCollision();
}
bool CireWorldEdit::ApplySet(UWorld* World, const FString& InName)
{
    const FString Name = SanitizeName(InName);
    FCireWorldEditSet Next;
    if (!Name.IsEmpty() && !WELoadNamed(Name, Next)) return false;
    bWEChosen = true; GWEChosen = Name;
    if (!bWEFrozen) { UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_SELECTED set=%s units=%d (applies when the town loads)"), Name.IsEmpty() ? TEXT("none") : *Name, Next.Removed.Num()); return true; }
    // After load: switch live. Restore what the new set keeps, remove what it adds.
    TSet<FString> Want; for (const FCireWorldEditEntry& E : Next.Removed) Want.Add(E.Key());
    int32 Restored = 0, Removed = 0, Restart = 0, NotFound = 0;
    const bool bLive = WEAppliesHere() && World;
    if (bLive)
    {
        for (const FString& Key : TSet<FString>(GWELiveRemoved))
        {
            if (Want.Contains(Key)) continue;
            FString R, Id; Key.Split(TEXT("|"), &R, &Id);
            if (AActor* A = WEFindUnit(World, Id, FCString::Atoi(*R))) SetUnitRemoved(World, A, false);
            GWELiveRemoved.Remove(Key); ++Restored;
        }
        for (const FString& Key : TSet<FString>(GWELoadRemoved))
        {
            if (Want.Contains(Key)) continue;
            const TWeakObjectPtr<AActor>* LIActor = GWEUnloaded.Find(Key);
            auto* Sub = WESub(World);
            if (LIActor && LIActor->IsValid() && Sub) { Sub->RequestLoadLevelInstance(Cast<ILevelInstanceInterface>(LIActor->Get()), true); GWEUnloaded.Remove(Key); GWELoadRemoved.Remove(Key); ++Restored; }
            else ++Restart; // destroyed at load: back on the next load
        }
        for (const FCireWorldEditEntry& E : Next.Removed)
        {
            if (GWELoadRemoved.Contains(E.Key()) || GWELiveRemoved.Contains(E.Key())) continue;
            if (AActor* A = WEFindUnit(World, E.Id, E.Realm)) { SetUnitRemoved(World, A, true); GWELiveRemoved.Add(E.Key()); ++Removed; }
            else ++NotFound;
        }
    }
    GWECurrent = Next; WERebuildKeys();
    WESyncReplicated(World);
    UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_LIVE set=%s units=%d removed=%d restored=%d restart_to_restore=%d not_found=%d live=%d"), Name.IsEmpty() ? TEXT("none") : *Name, Next.Removed.Num(), Removed, Restored, Restart, NotFound, bLive ? 1 : 0);
    return true;
}
void CireWorldEdit::SetFromServer(UWorld* World, const FString& Name, const FString& ServerHash)
{
    GWEServerHash = ServerHash;
    const FString Want = SanitizeName(Name);
    if (!bWEChosen || GWEChosen != Want)
        if (!ApplySet(World, Want)) { bWEChosen = true; GWEChosen = Want; } // missing here: run no set rather than another one
    const FString Mine = Hash(Current());
    if (Mine != ServerHash)
        UE_LOG(LogCireWorldEdit, Warning, TEXT("CIRE_WORLD_EDIT_MISMATCH set=%s server_hash=%s client_hash=%s: this client's Content/Data/WorldEdits differs from the host's"), *Name, *ServerHash, *Mine);
}

// ================================================================================================= console
namespace
{
FAutoConsoleCommandWithWorldAndArgs GWorldEditCommand(TEXT("cire.WorldEdit"),
    TEXT("cire.WorldEdit: status. cire.WorldEdit <name>|off: select a world edit set (live on a server: every client follows). cire.WorldEdit list: log the removable units."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
    {
        if (Args.Num() > 0 && Args[0] == TEXT("list"))
        {
            TArray<FCireWorldUnit> Units; CireWorldEdit::BuildIndex(World, Units);
            for (const FCireWorldUnit& U : Units)
                UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_UNIT %s kind=%s label=%s size=%.0fx%.0fx%.0f actors=%d draw_calls=%d"), *U.Key(), *U.Kind, *U.Label,
                    U.Bounds.GetSize().X, U.Bounds.GetSize().Y, U.Bounds.GetSize().Z, U.Actors, U.DrawCalls);
            return;
        }
        if (Args.Num() > 0)
        {
            if (!CireWorldEdit::ApplySet(World, Args[0])) UE_LOG(LogCireWorldEdit, Warning, TEXT("CIRE_WORLD_EDIT no set named %s (Content/Data/WorldEdits)"), *Args[0]);
            return;
        }
        const FCireWorldEditSavings S = CireWorldEdit::Savings(CireWorldEdit::Current());
        UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_STATUS set=%s active=%d units=%d saves_actors=%d saves_draw_calls=%d signature=%s sets=%s"), *CireWorldEdit::ActiveSet(), CireWorldEdit::Active() ? 1 : 0,
            S.Units, S.Actors, S.DrawCalls, *CireWorldEdit::Signature(), *FString::Join(CireWorldEdit::ListNamed(), TEXT(",")));
        CireWorldEdit::LogSummary(World);
    }));
}

// ================================================================================================= town-load probe
// -CireWorldEditDump: write every removable unit of the loaded town to Saved/WorldEditUnits.json and quit (authoring aid).
// -CireWorldEditProbe (Tools/RunWorldEditProbe.py): a dedicated server loads the town with a set (-CireWorldEdit=<name>), a
// client joins and follows the replicated choice. Both check every removed unit is gone and its twin (when not removed)
// is there. The server then waits for the client to finish, switches the set off live (the removed Level Instances load
// again), rebuilds the navmesh and checks it changed over the removed buildings.
namespace
{
struct FWEProbe
{
    int32 Stage = 0;
    double StartedAt = -1, StageAt = 0;
    bool bSawClient = false, bPass = true;
    TArray<int32> NavBefore;
    FCireWorldEditSet Probed;
    FString Notes;
};
FWEProbe GWEProbe;
TArray<int32> WENavSamples(UWorld* World, const FCireWorldEditSet& Set)
{
    TArray<int32> Out;
    for (const FCireWorldEditEntry& E : Set.Removed)
    {
        if (E.Kind != TEXT("Building") && E.Kind != TEXT("Fortification")) continue;
        const FBox2D F = E.Footprint();
        const double Z = CireTownMap::RealmOrigin(E.Realm).Z + E.Local.Z;
        for (int32 Y = 0; Y < 6; ++Y)
            for (int32 X = 0; X < 6; ++X)
            {
                const FVector2D L(FMath::Lerp(F.Min.X, F.Max.X, (X + .5) / 6.0), FMath::Lerp(F.Min.Y, F.Max.Y, (Y + .5) / 6.0));
                FVector W = CireLanePath::ToWorld(E.Realm, L, 0.f); W.Z = Z;
                FVector P;
                Out.Add(CireNav::Project(World, W, P, FVector(30, 30, FMath::Max(400.0, E.Size.Z * .6 + 300.0)), 40.f) ? FMath::RoundToInt(P.Z / 20.0) : MIN_int32);
            }
    }
    return Out;
}
void WEProbeCheck(const TCHAR* Tag, bool bOk, const FString& Why)
{
    if (!bOk) GWEProbe.bPass = false;
    UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_PROBE_%s %s %s"), bOk ? TEXT("OK") : TEXT("CHECK_FAIL"), Tag, *Why);
}
void WEProbeFinish(bool bServer)
{
    UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_PROBE_%s_%s %s"), bServer ? TEXT("SERVER") : TEXT("CLIENT"), GWEProbe.bPass ? TEXT("PASS") : TEXT("FAIL"), *GWEProbe.Notes);
    GWEProbe.Stage = 99;
    FPlatformMisc::RequestExit(false, TEXT("CireWorldEditProbe"));
}
}

void CireWorldEdit::TickProbe(UWorld* World)
{
    TickGallery(World);
    static const bool bProbe = WEHasParam(TEXT("CireWorldEditProbe")), bDump = WEHasParam(TEXT("CireWorldEditDump"));
    if ((!bProbe && !bDump) || !World || GWEProbe.Stage == 99) return;
    const double Now = FPlatformTime::Seconds();
    if (GWEProbe.StartedAt < 0) GWEProbe.StartedAt = Now;
    const bool bServer = World->GetNetMode() != NM_Client;
    if (Now - GWEProbe.StartedAt > 1500) { GWEProbe.bPass = false; GWEProbe.Notes += TEXT(" timeout"); WEProbeFinish(bServer); return; }
    if (GWEProbe.Stage == 0)
    {
        if (!CireTownMap::IsActive() || CireTownMap::LoadedLevels(World) == 0) return;
        if (bServer && CireNav::HasNavigation(World) && !CireNav::IsReady(World)) return;
        if (GWEProbe.StageAt <= 0) { GWEProbe.StageAt = Now; return; }
        if (Now - GWEProbe.StageAt < 5) return; // late Level Instances settle
        GWEProbe.Stage = 1; GWEProbe.StageAt = Now;
    }
    if (bDump)
    {
        TArray<FCireWorldUnit> Units; BuildIndex(World, Units);
        FCireWorldEditSet All; All.Name = TEXT("units");
        for (const FCireWorldUnit& U : Units) All.Removed.Add(EntryOf(U));
        const FString File = FPaths::ProjectSavedDir() / TEXT("WorldEditUnits.json");
        Save(All, File);
        UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_DUMP units=%d file=%s"), Units.Num(), *File);
        GWEProbe.Stage = 99; FPlatformMisc::RequestExit(false, TEXT("CireWorldEditDump"));
        return;
    }
    if (GWEProbe.Stage == 1)
    {
        const FCireWorldEditSet& Set = Current();
        GWEProbe.Probed = Set;
        // Every removed unit is gone on this peer; its twin (when the set keeps it) is not.
        WEProbeCheck(TEXT("set"), Set.Removed.Num() > 0 && Active(), FString::Printf(TEXT("set=%s units=%d active=%d hash=%s signature=%s"), *Set.Name, Set.Removed.Num(), Active() ? 1 : 0, *Hash(Set), *Signature()));
        int32 Gone = 0, AtLoad = 0, Twins = 0, TwinsPresent = 0;
        for (const FCireWorldEditEntry& E : Set.Removed)
        {
            const bool bGone = IsGone(World, E);
            Gone += bGone ? 1 : 0; AtLoad += GWELoadRemoved.Contains(E.Key()) ? 1 : 0;
            if (!bGone) UE_LOG(LogCireWorldEdit, Warning, TEXT("CIRE_WORLD_EDIT_PROBE_STILL_THERE %s"), *E.Key());
            const FCireWorldEditEntry T = TwinOf(E);
            if (Set.Contains(T.Key())) continue;
            ++Twins;
            if (WEFindUnit(World, T.Id, T.Realm) && !IsGone(World, T)) ++TwinsPresent;
        }
        WEProbeCheck(TEXT("gone"), Gone == Set.Removed.Num(), FString::Printf(TEXT("%d/%d removed units gone (netmode %d)"), Gone, Set.Removed.Num(), int32(World->GetNetMode())));
        WEProbeCheck(TEXT("at_load"), AtLoad == Set.Removed.Num(), FString::Printf(TEXT("%d/%d removed while the town loaded (found by their stable ids)"), AtLoad, Set.Removed.Num()));
        WEProbeCheck(TEXT("twins"), TwinsPresent == Twins, FString::Printf(TEXT("%d/%d twins the set keeps are still there"), TwinsPresent, Twins));
        GWEProbe.Notes += FString::Printf(TEXT("gone=%d/%d twins=%d/%d"), Gone, Set.Removed.Num(), TwinsPresent, Twins);
        if (!bServer) { WEProbeFinish(false); return; }
        GWEProbe.NavBefore = WENavSamples(World, Set);
        int32 OnNav = 0; for (const int32 V : GWEProbe.NavBefore) OnNav += V != MIN_int32 ? 1 : 0;
        UE_LOG(LogCireWorldEdit, Display, TEXT("CIRE_WORLD_EDIT_PROBE_NAV_BEFORE samples=%d on_navmesh=%d"), GWEProbe.NavBefore.Num(), OnNav);
        GWEProbe.Stage = 2; GWEProbe.StageAt = Now;
        return;
    }
    if (GWEProbe.Stage == 2)
    {
        // Wait for the client to join and finish (it exits after its checks); a standalone run goes straight on.
        const UNetDriver* Net = World->GetNetDriver();
        const int32 Clients = Net ? Net->ClientConnections.Num() : 0;
        GWEProbe.bSawClient |= Clients > 0;
        const bool bHost = World->GetNetMode() == NM_DedicatedServer || World->GetNetMode() == NM_ListenServer;
        if (bHost && !(GWEProbe.bSawClient && Clients == 0))
        {
            if (Now - GWEProbe.StageAt > 900) { WEProbeCheck(TEXT("client"), false, TEXT("no client joined and left within 900 s")); WEProbeFinish(true); }
            return;
        }
        if (bHost) WEProbeCheck(TEXT("client"), true, TEXT("a client joined, checked and left"));
        // Switch the set off live: the removed Level Instances load again, then the navmesh rebuilds over them.
        ApplySet(World, TEXT("off"));
        GWEProbe.Stage = 3; GWEProbe.StageAt = Now;
        return;
    }
    if (GWEProbe.Stage == 3)
    {
        auto* Sub = WESub(World);
        const FCireWorldEditSet& Probed = GWEProbe.Probed;
        bool bLoaded = true;
        for (const FCireWorldEditEntry& E : Probed.Removed)
            if (AActor* A = WEFindUnit(World, E.Id, E.Realm)) if (auto* LI = Cast<ILevelInstanceInterface>(A)) bLoaded &= Sub && Sub->IsLoaded(LI);
        if (!bLoaded && Now - GWEProbe.StageAt < 120) return;
        CireTownMap::PrepareRealmLevels(World);
        const double Ms = CireNav::FlushBuild(World);
        const TArray<int32> After = WENavSamples(World, Probed);
        int32 Changed = 0;
        for (int32 I = 0; I < FMath::Min(After.Num(), GWEProbe.NavBefore.Num()); ++I) Changed += After[I] != GWEProbe.NavBefore[I] ? 1 : 0;
        int32 Back = 0, Instances = 0;
        for (const FCireWorldEditEntry& E : Probed.Removed)
            if (AActor* A = WEFindUnit(World, E.Id, E.Realm)) if (Cast<ILevelInstanceInterface>(A)) { ++Instances; Back += IsGone(World, E) ? 0 : 1; }
        WEProbeCheck(TEXT("restore"), bLoaded && Back == Instances && Instances > 0, FString::Printf(TEXT("%d/%d removed buildings loaded again when the set was switched off live"), Back, Instances));
        WEProbeCheck(TEXT("navmesh"), Changed > 0, FString::Printf(TEXT("%d of %d navmesh samples over the removed buildings changed (rebuild %.0f ms)"), Changed, After.Num(), Ms));
        GWEProbe.Notes += FString::Printf(TEXT(" restored=%d/%d nav_changed=%d/%d"), Back, Instances, Changed, After.Num());
        WEProbeFinish(true);
    }
}
