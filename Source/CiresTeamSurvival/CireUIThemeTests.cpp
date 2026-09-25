// ui-themes: native checks for the UI theme layer (run by RunExpansionChecks --only native).
#include "CireUITheme.h"
#include "CireHUD.h"
#include "CireUIStyle.h"
#include "CireUISettings.h"
#if !UE_BUILD_SHIPPING
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

bool CireUITheme::RunSmoke()
{
    bool Pass = true;
    int32 Count = 0;
    auto Check = [&](bool bValue, const FString& Name) { ++Count; Pass &= bValue; if (!bValue) UE_LOG(LogTemp, Error, TEXT("CIRE_UITHEME_ASSERT %s"), *Name); };

    // Data validation: every theme parses, has every piece, and its atlas + fill load.
    TArray<FString> Errors;
    Check(Validate(Errors, true), TEXT("themes valid: ") + FString::Join(Errors, TEXT("; ")));
    const TArray<FCireUITheme>& Themes = All();
    Check(Themes.Num() == 4, FString::Printf(TEXT("four themes (found %d)"), Themes.Num()));
    Check(DefaultId() == FName(TEXT("GildedCitadel")), TEXT("Gilded Citadel is the default"));
    for (const TCHAR* Id : {TEXT("GildedCitadel"), TEXT("Ironbound"), TEXT("ArcaneVeil"), TEXT("VerdantBloom")})
        Check(Find(FName(Id)) != nullptr, FString(TEXT("theme present: ")) + Id);

    // Every kit element resolves art in every theme, and activating a theme applies its palette.
    const FName Before = Active() ? Active()->Id : NAME_None;
    FCireUIPainter P; // no canvas: Draw resolves the art without drawing
    for (const FCireUITheme& T : Themes)
    {
        SetActive(T.Id);
        Check(Active() && Active()->Id == T.Id, TEXT("activate ") + T.Id.ToString());
        Check(CireUIStyle::HasThemeArt(), TEXT("art loaded for ") + T.Id.ToString());
        Check(CireUIColors::Gold.Equals(T.Trim) && CireUIColors::Parchment.Equals(T.Text) && CireUIColors::Ink.Equals(T.Ink),
            TEXT("palette applied for ") + T.Id.ToString());
        for (int32 I = 0; I < static_cast<int32>(ECireThemePiece::RequiredCount); ++I)
        {
            const ECireThemePiece Piece = static_cast<ECireThemePiece>(I);
            const bool bResolved = Piece == ECireThemePiece::BarFill ? DrawBarFill(P, 0, 0, 10, 10, FLinearColor::White) : Draw(P, Piece, 0, 0, 64, 32);
            Check(bResolved, FString::Printf(TEXT("%s resolves %s"), *T.Id.ToString(), PieceName(Piece)));
            const FCireThemePiece& Pc = T.Piece(Piece);
            Check(Pc.UV1.X > Pc.UV0.X && Pc.UV1.Y > Pc.UV0.Y && Pc.UV1.X <= 1.001f && Pc.UV1.Y <= 1.001f,
                FString::Printf(TEXT("%s %s uv inside the atlas"), *T.Id.ToString(), PieceName(Piece)));
        }
        Check(DrawFill(P, 0, 0, 10, 10, FLinearColor::White), TEXT("fill resolves for ") + T.Id.ToString());
        // hud-art: every shipped theme also has the painted state pieces (slot/button states, buff border).
        for (int32 I = static_cast<int32>(ECireThemePiece::RequiredCount); I < static_cast<int32>(ECireThemePiece::Count); ++I)
            Check(Draw(P, static_cast<ECireThemePiece>(I), 0, 0, 64, 32), FString::Printf(TEXT("%s resolves state piece %s"), *T.Id.ToString(), PieceName(static_cast<ECireThemePiece>(I))));
    }
    Check(SetActive(FName(TEXT("NoSuchTheme"))) == DefaultId(), TEXT("unknown theme falls back to the default"));
    SetActive(Before);

    // Unit-frame captions: a normal (unranked) monster never reads as a construct ("BRUISERCONSTRUCT").
    Check(CireUnitFrameHeader(true, false, false, 0, 0, 0, false, TEXT("Bruiser"), FString(), false) == TEXT("BRUISER"), TEXT("normal monster header"));
    Check(!CireUnitFrameHeader(true, false, false, 0, 2, 0, false, TEXT("Caster"), FString(), true).Contains(TEXT("CONSTRUCT")), TEXT("elite monster header has no CONSTRUCT"));
    Check(CireUnitFrameHeader(true, false, false, 0, 0, 2, false, TEXT("Bruiser"), TEXT("Veteran"), false) == TEXT("VETERAN  /  BRUISER"), TEXT("ranked monster header"));
    Check(CireUnitFrameHeader(false, true, false, 2, 0, 0, false, TEXT("Tank"), FString(), false) == TEXT("ALLY  /  TANK"), TEXT("ally hero header"));
    Check(CireUnitFrameHeader(false, false, false, 0, 0, 0, false, FString(), FString(), false) == TEXT("CONSTRUCT"), TEXT("construct header"));

    // Parser rejects broken data with readable errors.
    {
        TArray<FCireUITheme> Parsed; FName Default; TArray<FString> E;
        Check(!ParseJson(TEXT("{ nope"), Parsed, Default, E) && E.Num() > 0, TEXT("invalid JSON rejected"));
        E.Reset();
        Check(!ParseJson(TEXT("{\"default\":\"A\",\"themes\":[{\"id\":\"A\",\"name\":\"A\",\"atlas\":\"/Game/X\",\"fill\":\"/Game/Y\",\"atlasSize\":[64,64],\"palette\":{},\"pieces\":{}}]}"), Parsed, Default, E)
            && E.ContainsByPredicate([](const FString& S) { return S.Contains(TEXT("missing piece Panel")); }), TEXT("missing pieces reported"));
    }

    // Persistence: default, round trip, unknown id sanitised, schema-5 profile migrates to the default.
    const FString Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OptionsTests"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString File = FPaths::Combine(Directory, TEXT("theme-") + FGuid::NewGuid().ToString() + TEXT(".ini"));
    {
        FCireUISettings S; S.Load(File);
        Check(S.UITheme == TEXT("GildedCitadel"), TEXT("new profile uses Gilded Citadel"));
        S.UITheme = TEXT("ArcaneVeil"); S.ChatFontSize = 17;
        Check(S.Save(), TEXT("profile saved"));
        FCireUISettings R; R.Load(File);
        Check(R.UITheme == TEXT("ArcaneVeil") && FMath::IsNearlyEqual(R.ChatFontSize, 17.f), TEXT("theme persists with other preferences"));
        R.UITheme = TEXT("Bogus"); R.Save();
        FCireUISettings Q; Q.Load(File);
        Check(Q.UITheme == TEXT("GildedCitadel"), TEXT("unknown saved theme falls back to the default"));
    }
    {
        // A schema-5 profile (before themes) keeps its preferences and gets the default theme.
        FString Text;
        FFileHelper::LoadFileToString(Text, *File);
        Text.ReplaceInline(TEXT("Version=6"), TEXT("Version=5"));
        Text.ReplaceInline(TEXT("UITheme=GildedCitadel"), TEXT("UITheme=Ironbound"));
        FFileHelper::SaveStringToFile(Text, *File);
        FCireUISettings V; V.Load(File);
        Check(V.UITheme == TEXT("GildedCitadel") && FMath::IsNearlyEqual(V.ChatFontSize, 17.f), TEXT("schema 5 migrates: default theme, preferences kept"));
        V.Save();
        FString Saved; FFileHelper::LoadFileToString(Saved, *File);
        Check(Saved.Contains(TEXT("Version=6")) && Saved.Contains(TEXT("UITheme=GildedCitadel")), TEXT("saving writes schema 6 with the theme"));
    }
    IFileManager::Get().Delete(*File);
    UE_LOG(LogTemp, Display, TEXT("CIRE_UITHEME_SMOKE_%s checks=%d"), Pass ? TEXT("PASS") : TEXT("FAIL"), Count);
    return Pass;
}
#else
bool CireUITheme::RunSmoke() { return true; }
#endif
