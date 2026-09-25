#include "CireUITheme.h"
#include "CireUIStyle.h"
#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireUITheme, Log, All);

namespace
{
const TCHAR* GPieceNames[] = {TEXT("Panel"), TEXT("Card"), TEXT("Tooltip"), TEXT("Slot"), TEXT("SlotPassive"), TEXT("SlotUltimate"),
    TEXT("Ring"), TEXT("BarFrame"), TEXT("CastFrame"), TEXT("Minimap"), TEXT("Banner"), TEXT("Divider"), TEXT("Ornament"), TEXT("BarFill")};
static_assert(UE_ARRAY_COUNT(GPieceNames) == static_cast<int32>(ECireThemePiece::Count), "piece names");

TArray<FCireUITheme> GThemes;
FName GDefault;
int32 GActive = INDEX_NONE;
bool GLoaded = false;

bool ReadColor(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Key, FLinearColor& Out, TArray<FString>& Errors, const FString& Where)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!Obj.IsValid() || !Obj->TryGetArrayField(Key, Values) || !Values || (Values->Num() != 3 && Values->Num() != 4))
    {
        Errors.Add(FString::Printf(TEXT("%s: palette.%s must be [r,g,b] or [r,g,b,a]"), *Where, Key));
        return false;
    }
    float C[4] = {0, 0, 0, 1};
    for (int32 I = 0; I < Values->Num(); ++I)
    {
        double V = 0;
        if (!(*Values)[I]->TryGetNumber(V) || !FMath::IsFinite(V) || V < 0 || V > 4)
        {
            Errors.Add(FString::Printf(TEXT("%s: palette.%s component %d out of range"), *Where, Key, I));
            return false;
        }
        C[I] = static_cast<float>(V);
    }
    Out = FLinearColor(C[0], C[1], C[2], C[3]);
    return true;
}

bool IsStrip(ECireThemePiece P)
{
    return P == ECireThemePiece::BarFrame || P == ECireThemePiece::CastFrame || P == ECireThemePiece::Banner || P == ECireThemePiece::Divider;
}

void LoadThemes()
{
    if (GLoaded) return;
    GLoaded = true;
    FString Json;
    const FString Path = FPaths::ProjectContentDir() / TEXT("Data/UIThemes.json");
    if (!FFileHelper::LoadFileToString(Json, *Path))
    {
        UE_LOG(LogCireUITheme, Warning, TEXT("CIRE_UITHEME_MISSING %s"), *Path);
        return;
    }
    TArray<FString> Errors;
    if (!CireUITheme::ParseJson(Json, GThemes, GDefault, Errors))
        for (const FString& E : Errors) UE_LOG(LogCireUITheme, Warning, TEXT("CIRE_UITHEME_ERROR %s"), *E);
    UE_LOG(LogCireUITheme, Log, TEXT("CIRE_UITHEME_LOADED themes=%d default=%s"), GThemes.Num(), *GDefault.ToString());
}

UTexture2D* LoadTex(const FString& Path)
{
    if (Path.IsEmpty()) return nullptr;
    FString Object = Path;
    if (!Object.Contains(TEXT("."))) Object += TEXT(".") + FPackageName::GetShortName(Path);
    UTexture2D* T = LoadObject<UTexture2D>(nullptr, *Object, nullptr, LOAD_NoWarn | LOAD_Quiet);
    if (T) T->AddToRoot();
    return T;
}

void ApplyPalette(const FCireUITheme& T)
{
    using namespace CireUIColors;
    Gold = T.Trim; BrightGold = T.BrightTrim; Parchment = T.Text; Muted = T.Muted; Ink = T.Ink; Card = T.Card; Hover = T.Hover;
    ThemeAccent = T.Accent; ThemeGlow = T.Glow; TooltipBg = T.TooltipBg; TooltipBorder = T.TooltipBorder; BarBack = T.BarBack;
    TitleText = T.Title; ThemeFiligree = T.Filigree; PanelTint = T.PanelTint;
}
} // namespace

const TCHAR* CireUITheme::PieceName(ECireThemePiece Piece)
{
    const int32 I = static_cast<int32>(Piece);
    return I >= 0 && I < static_cast<int32>(ECireThemePiece::Count) ? GPieceNames[I] : TEXT("?");
}

bool CireUITheme::ParseJson(const FString& Json, TArray<FCireUITheme>& Out, FName& OutDefault, TArray<FString>& Errors)
{
    Out.Reset();
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root.IsValid())
    {
        Errors.Add(TEXT("UIThemes.json is not valid JSON"));
        return false;
    }
    FString Default;
    Root->TryGetStringField(TEXT("default"), Default);
    OutDefault = FName(*Default);
    const TArray<TSharedPtr<FJsonValue>>* Themes = nullptr;
    if (!Root->TryGetArrayField(TEXT("themes"), Themes) || !Themes || Themes->IsEmpty())
    {
        Errors.Add(TEXT("UIThemes.json needs a non-empty \"themes\" array"));
        return false;
    }
    for (const TSharedPtr<FJsonValue>& Value : *Themes)
    {
        const TSharedPtr<FJsonObject> Obj = Value->AsObject();
        if (!Obj.IsValid()) { Errors.Add(TEXT("theme entry is not an object")); continue; }
        FCireUITheme T;
        FString Id;
        if (!Obj->TryGetStringField(TEXT("id"), Id) || Id.IsEmpty()) { Errors.Add(TEXT("theme without an id")); continue; }
        T.Id = FName(*Id);
        const FString Where = TEXT("theme ") + Id;
        if (Out.ContainsByPredicate([&](const FCireUITheme& O) { return O.Id == T.Id; })) Errors.Add(Where + TEXT(": duplicate id"));
        if (!Obj->TryGetStringField(TEXT("name"), T.Name) || T.Name.IsEmpty()) Errors.Add(Where + TEXT(": missing name"));
        Obj->TryGetStringField(TEXT("tagline"), T.Tagline);
        if (!Obj->TryGetStringField(TEXT("atlas"), T.AtlasPath) || !T.AtlasPath.StartsWith(TEXT("/Game/"))) Errors.Add(Where + TEXT(": atlas must be a /Game/ path"));
        if (!Obj->TryGetStringField(TEXT("fill"), T.FillPath) || !T.FillPath.StartsWith(TEXT("/Game/"))) Errors.Add(Where + TEXT(": fill must be a /Game/ path"));
        const TSharedPtr<FJsonObject>* Palette = nullptr;
        if (!Obj->TryGetObjectField(TEXT("palette"), Palette) || !Palette) Errors.Add(Where + TEXT(": missing palette"));
        else
        {
            const TSharedPtr<FJsonObject>& Pal = *Palette;
            ReadColor(Pal, TEXT("trim"), T.Trim, Errors, Where); ReadColor(Pal, TEXT("brightTrim"), T.BrightTrim, Errors, Where);
            ReadColor(Pal, TEXT("text"), T.Text, Errors, Where); ReadColor(Pal, TEXT("muted"), T.Muted, Errors, Where);
            ReadColor(Pal, TEXT("ink"), T.Ink, Errors, Where); ReadColor(Pal, TEXT("card"), T.Card, Errors, Where);
            ReadColor(Pal, TEXT("hover"), T.Hover, Errors, Where); ReadColor(Pal, TEXT("accent"), T.Accent, Errors, Where);
            ReadColor(Pal, TEXT("glow"), T.Glow, Errors, Where); ReadColor(Pal, TEXT("tooltipBg"), T.TooltipBg, Errors, Where);
            ReadColor(Pal, TEXT("tooltipBorder"), T.TooltipBorder, Errors, Where); ReadColor(Pal, TEXT("barBack"), T.BarBack, Errors, Where);
            ReadColor(Pal, TEXT("title"), T.Title, Errors, Where); ReadColor(Pal, TEXT("filigree"), T.Filigree, Errors, Where);
            ReadColor(Pal, TEXT("panelTint"), T.PanelTint, Errors, Where);
        }
        double Number = 0;
        if (Obj->TryGetNumberField(TEXT("glowStrength"), Number)) T.GlowStrength = FMath::Clamp(static_cast<float>(Number), 0.f, 3.f);
        if (Obj->TryGetNumberField(TEXT("fillScale"), Number)) T.FillScale = FMath::Clamp(static_cast<float>(Number), 16.f, 2048.f);
        if (Obj->TryGetNumberField(TEXT("panelOpacity"), Number)) T.PanelOpacity = FMath::Clamp(static_cast<float>(Number), .3f, 1.f);
        Obj->TryGetBoolField(TEXT("gem"), T.bGem);
        Obj->TryGetBoolField(TEXT("cornerOrnaments"), T.bCornerOrnaments);
        const TArray<TSharedPtr<FJsonValue>>* AtlasSize = nullptr;
        FVector2f Size(0, 0);
        if (Obj->TryGetArrayField(TEXT("atlasSize"), AtlasSize) && AtlasSize && AtlasSize->Num() == 2)
            Size = FVector2f((*AtlasSize)[0]->AsNumber(), (*AtlasSize)[1]->AsNumber());
        if (Size.X < 16 || Size.Y < 16) Errors.Add(Where + TEXT(": atlasSize must be [w,h]"));
        const TSharedPtr<FJsonObject>* Pieces = nullptr;
        if (!Obj->TryGetObjectField(TEXT("pieces"), Pieces) || !Pieces) Errors.Add(Where + TEXT(": missing pieces"));
        for (int32 I = 0; I < static_cast<int32>(ECireThemePiece::Count); ++I)
        {
            const TSharedPtr<FJsonObject>* PieceObj = nullptr;
            if (!Pieces || !(*Pieces)->TryGetObjectField(GPieceNames[I], PieceObj) || !PieceObj)
            {
                Errors.Add(FString::Printf(TEXT("%s: missing piece %s"), *Where, GPieceNames[I]));
                continue;
            }
            FCireThemePiece& P = T.Pieces[I];
            const TArray<TSharedPtr<FJsonValue>>* Rect = nullptr;
            if (!(*PieceObj)->TryGetArrayField(TEXT("rect"), Rect) || !Rect || Rect->Num() != 4)
            {
                Errors.Add(FString::Printf(TEXT("%s: piece %s needs rect [x,y,w,h]"), *Where, GPieceNames[I]));
                continue;
            }
            const float X = (*Rect)[0]->AsNumber(), Y = (*Rect)[1]->AsNumber(), W = (*Rect)[2]->AsNumber(), H = (*Rect)[3]->AsNumber();
            if (W < 2 || H < 2 || X < 0 || Y < 0 || (Size.X > 0 && (X + W > Size.X + .5f || Y + H > Size.Y + .5f)))
            {
                Errors.Add(FString::Printf(TEXT("%s: piece %s rect outside the atlas"), *Where, GPieceNames[I]));
                continue;
            }
            if (Size.X > 0) { P.UV0 = FVector2f(X / Size.X, Y / Size.Y); P.UV1 = FVector2f((X + W) / Size.X, (Y + H) / Size.Y); }
            P.SizePx = FVector2f(W, H);
            if ((*PieceObj)->TryGetNumberField(TEXT("slice"), Number)) P.Slice = FMath::Clamp(static_cast<float>(Number), 0.f, .49f);
            if ((*PieceObj)->TryGetNumberField(TEXT("corner"), Number)) P.Corner = FMath::Clamp(static_cast<float>(Number), 0.f, 128.f);
            P.bValid = true;
        }
        Out.Add(MoveTemp(T));
    }
    if (!Out.IsEmpty() && !Out.ContainsByPredicate([&](const FCireUITheme& T) { return T.Id == OutDefault; }))
    {
        Errors.Add(TEXT("default theme is not one of the themes"));
        OutDefault = Out[0].Id;
    }
    return Errors.IsEmpty();
}

const TArray<FCireUITheme>& CireUITheme::All() { LoadThemes(); return GThemes; }
FName CireUITheme::DefaultId() { LoadThemes(); return GDefault; }

const FCireUITheme* CireUITheme::Find(FName Id)
{
    for (const FCireUITheme& T : All()) if (T.Id == Id) return &T;
    return nullptr;
}

const FCireUITheme* CireUITheme::Active()
{
    if (GActive == INDEX_NONE) SetActive(DefaultId());
    return GThemes.IsValidIndex(GActive) ? &GThemes[GActive] : nullptr;
}

FName CireUITheme::SetActive(FName Id)
{
    const TArray<FCireUITheme>& Themes = All();
    int32 Index = Themes.IndexOfByPredicate([&](const FCireUITheme& T) { return T.Id == Id; });
    if (Index == INDEX_NONE) Index = Themes.IndexOfByPredicate([&](const FCireUITheme& T) { return T.Id == GDefault; });
    if (Index == INDEX_NONE && !Themes.IsEmpty()) Index = 0;
    if (Index == INDEX_NONE) return NAME_None;
    if (GActive != Index) { GActive = Index; ApplyPalette(Themes[Index]); }
    return Themes[Index].Id;
}

bool CireUITheme::LoadArt(const FCireUITheme& Theme)
{
    if (!Theme.bLoaded)
    {
        Theme.bLoaded = true;
        Theme.Atlas = LoadTex(Theme.AtlasPath);
        Theme.Fill = LoadTex(Theme.FillPath);
    }
    return Theme.Atlas && Theme.Fill;
}

const TArray<FString>& CireUITheme::AssetPaths()
{
    static TArray<FString> Paths;
    if (Paths.IsEmpty())
        for (const FCireUITheme& T : All())
            for (const FString& Path : {T.AtlasPath, T.FillPath})
                Paths.Add(Path.Contains(TEXT(".")) ? Path : Path + TEXT(".") + FPackageName::GetShortName(Path));
    return Paths;
}

bool CireUITheme::Draw(const FCireUIPainter& P, ECireThemePiece Which, float X, float Y, float W, float H, FLinearColor Tint, float CornerScale)
{
    const FCireUITheme* T = Active();
    if (!T || W <= 0 || H <= 0 || !LoadArt(*T)) return false;
    const FCireThemePiece& Pc = T->Piece(Which);
    if (!Pc.bValid) return false;
    UTexture2D* Tex = T->Atlas;
    const FVector2f D = Pc.UV1 - Pc.UV0;
    if (Pc.Slice <= 0.f)
    {
        P.Tex(Tex, X, Y, W, H, Tint, Pc.UV0.X, Pc.UV0.Y, Pc.UV1.X, Pc.UV1.Y);
        return true;
    }
    if (IsStrip(Which))
    {
        // Horizontal 3-slice: caps keep their aspect at the drawn height.
        const float CapPx = Pc.Slice * Pc.SizePx.X;
        const float Cap = FMath::Min(H * CapPx / FMath::Max(1.f, Pc.SizePx.Y) * CornerScale, W * .5f);
        const float Us[] = {Pc.UV0.X, Pc.UV0.X + D.X * Pc.Slice, Pc.UV1.X - D.X * Pc.Slice, Pc.UV1.X};
        const float Xs[] = {X, X + Cap, X + W - Cap, X + W};
        for (int32 I = 0; I < 3; ++I)
            if (Xs[I + 1] > Xs[I]) P.Tex(Tex, Xs[I], Y, Xs[I + 1] - Xs[I], H, Tint, Us[I], Pc.UV0.Y, Us[I + 1], Pc.UV1.Y);
        return true;
    }
    // 9-slice: the source corner is Slice of the shorter side; drawn corner is Corner logical units.
    const float CornerPx = Pc.Slice * FMath::Min(Pc.SizePx.X, Pc.SizePx.Y);
    const float SU = CornerPx / FMath::Max(1.f, Pc.SizePx.X), SV = CornerPx / FMath::Max(1.f, Pc.SizePx.Y);
    const float C = FMath::Min(Pc.Corner * CornerScale, FMath::Min(W, H) * .5f);
    const float Xs[] = {X, X + C, X + W - C, X + W}, Ys[] = {Y, Y + C, Y + H - C, Y + H};
    const float Us[] = {Pc.UV0.X, Pc.UV0.X + D.X * SU, Pc.UV1.X - D.X * SU, Pc.UV1.X};
    const float Vs[] = {Pc.UV0.Y, Pc.UV0.Y + D.Y * SV, Pc.UV1.Y - D.Y * SV, Pc.UV1.Y};
    for (int32 R = 0; R < 3; ++R)
        for (int32 Q = 0; Q < 3; ++Q)
            if (Xs[Q + 1] > Xs[Q] && Ys[R + 1] > Ys[R])
                P.Tex(Tex, Xs[Q], Ys[R], Xs[Q + 1] - Xs[Q], Ys[R + 1] - Ys[R], Tint, Us[Q], Vs[R], Us[Q + 1], Vs[R + 1]);
    return true;
}

bool CireUITheme::DrawFill(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Tint)
{
    const FCireUITheme* T = Active();
    if (!T || W <= 0 || H <= 0 || !LoadArt(*T)) return false;
    // Tiles in logical units so the grain stays put when a panel is resized; offset by position.
    const float S = T->FillScale;
    P.Tex(T->Fill, X, Y, W, H, Tint, X / S, Y / S, (X + W) / S, (Y + H) / S);
    return true;
}

bool CireUITheme::DrawBarFill(const FCireUIPainter& P, float X, float Y, float W, float H, FLinearColor Tint)
{
    const FCireUITheme* T = Active();
    if (!T || W <= 0 || H <= 0 || !LoadArt(*T)) return false;
    const FCireThemePiece& Pc = T->Piece(ECireThemePiece::BarFill);
    if (!Pc.bValid) return false;
    P.Tex(T->Atlas, X, Y, W, H, Tint, Pc.UV0.X, Pc.UV0.Y, Pc.UV1.X, Pc.UV1.Y);
    return true;
}

bool CireUITheme::Validate(TArray<FString>& Errors, bool bRequireArt)
{
    FString Json;
    const FString Path = FPaths::ProjectContentDir() / TEXT("Data/UIThemes.json");
    if (!FFileHelper::LoadFileToString(Json, *Path)) { Errors.Add(TEXT("missing ") + Path); return false; }
    TArray<FCireUITheme> Parsed;
    FName Default;
    CireUITheme::ParseJson(Json, Parsed, Default, Errors);
    if (Parsed.Num() < 4) Errors.Add(FString::Printf(TEXT("expected 4 themes, found %d"), Parsed.Num()));
    if (bRequireArt)
        for (const FCireUITheme& T : Parsed)
        {
            if (!LoadArt(T)) Errors.Add(FString::Printf(TEXT("theme %s: atlas or fill texture failed to load"), *T.Id.ToString()));
            for (int32 I = 0; I < static_cast<int32>(ECireThemePiece::Count); ++I)
                if (!T.Pieces[I].bValid) Errors.Add(FString::Printf(TEXT("theme %s: piece %s unresolved"), *T.Id.ToString(), GPieceNames[I]));
        }
    return Errors.IsEmpty();
}
