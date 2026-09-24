// wave-director: F8 developer tools > Waves page. A live wave composer drawn with the
// CireUIStyle kit. Edits are a draft until APPLY LIVE (server-authoritative, takes effect
// from the next wave); SAVE writes Content/Data/Waves.json. See Docs/Waves.md.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireWaves.h"
#include "CireNPCArchetypes.h"
#include "CireBalanceLab.h"
#include "CireDeveloperTools.h"
#include "Engine/World.h"

namespace
{
const FLinearColor Gold(.77f, .61f, .34f, 1), Parchment(.91f, .9f, .83f, 1), Muted(.5f, .57f, .59f, 1), Teal(.2f, .71f, .59f, 1),
    Row(.045f, .06f, .07f, .96f), RowSelected(.1f, .13f, .12f, 1), Red(.75f, .2f, .23f, 1);

TArray<FName> ArchetypeIds()
{
    TArray<FName> Ids;
    CireNPCArchetypes::Get().Archetypes.GetKeys(Ids);
    Ids.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
    return Ids;
}
FString ShortArchetype(FName Id)
{
    const auto* A = CireNPCArchetypes::Find(Id);
    FString Name = A ? A->DisplayName : Id.ToString();
    Name.ReplaceInline(TEXT("Hollow "), TEXT(""));
    Name.ReplaceInline(TEXT(", Pack Leader"), TEXT(""));
    return Name;
}
}

void ACireHUD::DrawWaveEditor(float X, float Y)
{
#if !UE_BUILD_SHIPPING
    auto* Mode = GetWorld()->GetAuthGameMode<ACireGameMode>();
    if (!Mode) return;
    const auto* State = GetWorld()->GetGameState<ACireGameState>();
    if (!bWaveDraftLoaded) { WaveDraft = CireWaveDirector::Config(GetWorld()); bWaveDraftLoaded = true; WaveSelected = 0; }
    if (WaveDraft.Waves.IsEmpty()) WaveDraft.Waves.Add(CireWaveDirector::Template(ECireWaveType::Normal));
    WaveSelected = FMath::Clamp(WaveSelected, 0, WaveDraft.Waves.Num() - 1);
    FString Error;
    auto Button = [&](const FString& Title, float BX, float BY, float W, float H, const FString& Help, bool bEnabled = true, bool bSelected = false, FLinearColor Accent = Gold)
    {
        const bool Over = bEnabled && Hit(BX, BY, W, H);
        CireUIStyle::Button(Painter(), BX, BY, W, H, Title, !bEnabled ? ECireButtonState::Disabled : bSelected ? ECireButtonState::Selected : Over ? ECireButtonState::Hover : ECireButtonState::Normal,
            Accent, H < 22 ? 8.5f : 9.5f);
        if (!Help.IsEmpty()) Tip(Title, Help, BX, BY, W, H);
        if (Over && Clicked) { Clicked = false; PlayUIFeedback(); return true; }
        return false;
    };
    // Stepper: [-] value [+] with clamping. Returns true when the value changed.
    auto StepF = [&](const FString& Caption, float& Value, float Step, float Min, float Max, float BX, float BY, float W, int32 Decimals, const TCHAR* Suffix, const FString& Help)
    {
        if (!Caption.IsEmpty()) Label(Caption, BX, BY - 13, 8, Muted);
        const float Before = Value;
        if (Button(TEXT("-"), BX, BY, 18, 20, FString())) Value = FMath::Clamp(FMath::RoundToFloat((Value - Step) / Step) * Step, Min, Max);
        const FString Text = FString::Printf(TEXT("%.*f%s"), Decimals, Value, Suffix);
        Painter().Rect(BX + 19, BY, W - 38, 20, FLinearColor(0, 0, 0, .45f));
        Label(Text, BX + W * .5f - TextWidth(Text, 9.5f) * .5f, BY + 4, 9.5f, Parchment);
        if (Button(TEXT("+"), BX + W - 18, BY, 18, 20, FString())) Value = FMath::Clamp(FMath::RoundToFloat((Value + Step) / Step) * Step, Min, Max);
        if (!Help.IsEmpty()) Tip(Caption.IsEmpty() ? FString(TEXT("Value")) : Caption, Help, BX + 19, BY, W - 38, 20);
        return !FMath::IsNearlyEqual(Before, Value);
    };
    auto StepI = [&](const FString& Caption, int32& Value, int32 Min, int32 Max, float BX, float BY, float W, const FString& Help)
    {
        float V = static_cast<float>(Value);
        const bool bChanged = StepF(Caption, V, 1.f, static_cast<float>(Min), static_cast<float>(Max), BX, BY, W, 0, TEXT(""), Help);
        Value = FMath::RoundToInt(V);
        return bChanged;
    };

    const float L = X, T = Y + 44;
    // ---- header: live status ------------------------------------------------------
    const FString Live = State ? FString::Printf(TEXT("LIVE  |  %s  |  NEXT: %s"), State->WaveLabel.IsEmpty() ? TEXT("no wave yet") : *State->WaveLabel,
        State->NextWaveLabel.IsEmpty() ? TEXT("-") : *State->NextWaveLabel) : FString(TEXT("LIVE  |  match state unavailable"));
    Label(Painter().Fit(Live, 9.5f, 600, ECireFont::Heading), L, T - 2, 9.5f, Teal);

    // ---- wave list (left) ---------------------------------------------------------
    const float ListX = L, ListY = T + 18, ListW = 170, RowH = 27;
    const int32 Visible = 8;
    WaveListScroll = FMath::Clamp(WaveListScroll, 0, FMath::Max(0, WaveDraft.Waves.Num() - Visible));
    Painter().Rect(ListX, ListY, ListW, Visible * RowH + 4, FLinearColor(0, 0, 0, .35f));
    for (int32 I = WaveListScroll; I < FMath::Min(WaveDraft.Waves.Num(), WaveListScroll + Visible); ++I)
    {
        const auto& W = WaveDraft.Waves[I];
        const float RY = ListY + 2 + (I - WaveListScroll) * RowH;
        const bool bSel = I == WaveSelected, Over = Hit(ListX + 2, RY, ListW - 4, RowH - 2);
        Painter().Rect(ListX + 2, RY, ListW - 4, RowH - 2, bSel ? RowSelected : Over ? FLinearColor(.07f, .09f, .1f, 1) : Row);
        if (bSel) Painter().Rect(ListX + 2, RY, 3, RowH - 2, Gold);
        const bool bInCycle = I < WaveDraft.WavesPerCycle;
        Label(Painter().Fit(FString::Printf(TEXT("%d. %s"), I + 1, *W.Label), 9.5f, ListW - 16, ECireFont::Bold), ListX + 10, RY + 2, 9.5f, bInCycle ? Parchment : Muted);
        Label(Painter().Fit(FString::Printf(TEXT("%s  |  %d/lane"), *CireWaveDirector::TypeLabel(W.Type), W.UnitsPerLane()), 8, ListW - 16, ECireFont::Body), ListX + 10, RY + 15, 8, bInCycle ? Gold : Muted);
        Tip(W.Label, bInCycle ? TEXT("Select to edit. This wave is inside the cycle.") : TEXT("Beyond waves-per-cycle: kept in the list but not played until the cycle grows."), ListX + 2, RY, ListW - 4, RowH - 2);
        if (Over && Clicked) { Clicked = false; PlayUIFeedback(); WaveSelected = I; }
    }
    const float LB = ListY + Visible * RowH + 10;
    if (Button(TEXT("ADD"), ListX, LB, 54, 22, TEXT("Add a Normal wave after the selected one (max 20).")) && WaveDraft.Waves.Num() < 20)
    { WaveDraft.Waves.Insert(CireWaveDirector::Template(ECireWaveType::Normal), WaveSelected + 1); ++WaveSelected; }
    if (Button(TEXT("DUP"), ListX + 58, LB, 54, 22, TEXT("Duplicate the selected wave.")) && WaveDraft.Waves.Num() < 20)
    { const FCireWaveDef Copy = WaveDraft.Waves[WaveSelected]; WaveDraft.Waves.Insert(Copy, WaveSelected + 1); ++WaveSelected; }
    if (Button(TEXT("DEL"), ListX + 116, LB, 54, 22, TEXT("Remove the selected wave (at least one remains)."), WaveDraft.Waves.Num() > 1, false, Red) && WaveDraft.Waves.Num() > 1)
    { WaveDraft.Waves.RemoveAt(WaveSelected); WaveSelected = FMath::Min(WaveSelected, WaveDraft.Waves.Num() - 1); }
    if (Button(TEXT("MOVE UP"), ListX, LB + 26, 83, 22, TEXT("Move the selected wave earlier in the cycle."), WaveSelected > 0) && WaveSelected > 0)
    { WaveDraft.Waves.Swap(WaveSelected, WaveSelected - 1); --WaveSelected; }
    if (Button(TEXT("MOVE DOWN"), ListX + 87, LB + 26, 83, 22, TEXT("Move the selected wave later in the cycle."), WaveSelected + 1 < WaveDraft.Waves.Num()) && WaveSelected + 1 < WaveDraft.Waves.Num())
    { WaveDraft.Waves.Swap(WaveSelected, WaveSelected + 1); ++WaveSelected; }
    if (WaveDraft.Waves.Num() > Visible)
    {
        if (Button(TEXT("^"), ListX + ListW + 2, ListY, 16, 20, FString())) WaveListScroll = FMath::Max(0, WaveListScroll - 1);
        if (Button(TEXT("v"), ListX + ListW + 2, ListY + Visible * RowH - 16, 16, 20, FString())) ++WaveListScroll;
    }
    WaveSelected = FMath::Clamp(WaveSelected, 0, WaveDraft.Waves.Num() - 1);

    // ---- selected wave (right) -----------------------------------------------------
    FCireWaveDef& W = WaveDraft.Waves[WaveSelected];
    const float EX = L + 190, EW = 410;
    CireUIStyle::Header(Painter(), EX, T + 16, EW, FString::Printf(TEXT("WAVE %d  |  %s"), WaveSelected + 1, *W.Label.ToUpper()), Gold, 10.f);
    const float R1 = T + 42;
    if (Button(FString(TEXT("TYPE: ")) + CireWaveDirector::TypeLabel(W.Type), EX, R1, 170, 22,
        TEXT("Cycle the wave type label. Use APPLY TEMPLATE to replace the composition with that type's template.")))
        W.Type = static_cast<ECireWaveType>((static_cast<int32>(W.Type) + 1) % static_cast<int32>(ECireWaveType::Count));
    if (Button(TEXT("APPLY TEMPLATE"), EX + 176, R1, 120, 22, TEXT("Replace this wave's rows, pacing and label with the template for its type (Armored Escort = 1 non-attacking tank + 4 defenders).")))
    { const ECireWaveType Type = W.Type; W = CireWaveDirector::Template(Type); }
    if (Button(W.bMustClear ? TEXT("MUST CLEAR") : TEXT("OVERLAPS NEXT"), EX + 302, R1, 108, 22,
        TEXT("Must clear: the next wave waits for this one to die or leak. Overlaps: the next wave's timer starts once this one has fully spawned."), true, W.bMustClear, W.bMustClear ? Teal : Gold))
        W.bMustClear = !W.bMustClear;
    const float R2 = R1 + 42;
    StepF(TEXT("SPAWN INTERVAL"), W.SpawnInterval, .1f, 0, 5, EX, R2, 96, 1, TEXT("s"), TEXT("Seconds between individual spawns inside this wave."));
    StepF(TEXT("DELAY BEFORE"), W.DelayBefore, 1, 0, 120, EX + 104, R2, 96, 0, TEXT("s"), TEXT("Extra wait before this wave, on top of the breather."));
    StepF(TEXT("REWARD x"), W.RewardMultiplier, .05f, 0, 10, EX + 208, R2, 96, 2, TEXT(""), TEXT("Kill XP/gold multiplier for this wave's units."));
    Label(FString::Printf(TEXT("%d units / lane"), W.UnitsPerLane()), EX + 316, R2 + 3, 10, W.UnitsPerLane() > 30 ? Red : Gold);

    // Composition rows.
    const float CY = R2 + 30;
    Label(TEXT("ARCHETYPE"), EX, CY, 8, Muted); Label(TEXT("E M T B"), EX + 93, CY, 8, Muted); Label(TEXT("COUNT"), EX + 136, CY, 8, Muted); Label(TEXT("HEALTH x"), EX + 196, CY, 8, Muted);
    Label(TEXT("DAMAGE x"), EX + 262, CY, 8, Muted); Label(TEXT("SIZE"), EX + 328, CY, 8, Muted);
    const TArray<FName> Ids = ArchetypeIds();
    int32 RemoveRow = INDEX_NONE;
    for (int32 I = 0; I < W.Units.Num() && I < 8; ++I)
    {
        auto& U = W.Units[I];
        const float RY = CY + 13 + I * 25;
        Painter().Rect(EX, RY - 1, EW, 23, I % 2 ? FLinearColor(0, 0, 0, .18f) : FLinearColor(0, 0, 0, .3f));
        const int32 Index = FMath::Max(0, Ids.IndexOfByKey(U.Archetype));
        if (Button(Painter().Fit(ShortArchetype(U.Archetype), 8.5f, 84, ECireFont::Bold), EX + 1, RY, 88, 21, TEXT("Click to cycle the NPC archetype (NPCArchetypes.json).")) && Ids.Num() > 0)
            U.Archetype = Ids[(Index + 1) % Ids.Num()];
        StepI(FString(), U.Count, 1, 20, EX + 136, RY, 58, TEXT("Units of this row per lane (1-20)."));
        StepF(FString(), U.HealthScale, .05f, .1f, 20, EX + 196, RY, 64, 2, TEXT(""), TEXT("Health multiplier on top of wave/round scaling."));
        StepF(FString(), U.DamageScale, .05f, .05f, 10, EX + 262, RY, 64, 2, TEXT(""), TEXT("Damage multiplier."));
        StepF(FString(), U.SizeScale, .05f, .5f, 3, EX + 328, RY, 50, 2, TEXT(""), TEXT("Body size multiplier."));
        // Flags: Elite, Non-attacking marcher, Escortee, Boss.
        const float FX = EX + 330 + 50;
        if (Button(TEXT("x"), FX + 12, RY, 18, 21, TEXT("Remove this row."), W.Units.Num() > 1, false, Red)) RemoveRow = I;
        const TCHAR* Flags[] = {TEXT("E"), TEXT("M"), TEXT("T"), TEXT("B")};
        bool* Values[] = {&U.bElite, &U.bNonAttacking, &U.bEscortee, &U.bBoss};
        const TCHAR* Help[] = {TEXT("Elite: x1.6 health, x1.25 damage, gold elite marker."), TEXT("Marcher: never attacks, walks through heroes, must be stopped."),
            TEXT("Escortee: the protected tank of an escort wave (implies marcher); attackers in the wave defend it."), TEXT("Lane boss: leaks for 10 lives (archetype leak cost).")};
        // Flag toggles: Elite, Marcher (non-attacking), escorTee, Boss.
        for (int32 F = 0; F < 4; ++F)
        {
            const float BX = EX + 92 + F * 11;
            const bool bOn = *Values[F];
            const bool Over = Hit(BX, RY, 10, 21);
            Painter().Rect(BX, RY, 10, 21, bOn ? (F == 3 ? Red : Teal) : FLinearColor(.12f, .12f, .12f, .9f));
            Label(Flags[F], BX + 1.5f, RY + 5, 8, bOn ? FLinearColor::Black : Muted);
            Tip(FString(Flags[F]), Help[F], BX, RY, 10, 21);
            if (Over && Clicked) { Clicked = false; PlayUIFeedback(); *Values[F] = !bOn; if (F == 2 && *Values[F]) U.bNonAttacking = true; if (F == 3 && *Values[F]) { U.bNonAttacking = false; U.bEscortee = false; } }
        }
    }
    if (RemoveRow != INDEX_NONE && W.Units.Num() > 1) W.Units.RemoveAt(RemoveRow);
    const float AddY = CY + 13 + FMath::Min(W.Units.Num(), 8) * 25 + 2;
    if (W.Units.Num() < 8 && Button(TEXT("+ ROW"), EX, AddY, 70, 20, TEXT("Add a composition row (max 8).")))
    { FCireWaveUnit U; U.Archetype = TEXT("hollow_infantry"); U.Count = 1; W.Units.Add(U); }

    // ---- globals ---------------------------------------------------------------------
    const float GY = Y + 318;
    Painter().Rect(L, GY - 18, 600, 1, Gold * FLinearColor(1, 1, 1, .5f));
    StepF(TEXT("BREATHER"), WaveDraft.BreatherSeconds, 1, 0, 120, L, GY, 92, 0, TEXT("s"), TEXT("Seconds between a cleared wave and the next spawn."));
    StepI(TEXT("WAVES / CYCLE"), WaveDraft.WavesPerCycle, 1, 10, L + 100, GY, 92, TEXT("Cleared waves before prep, arena and recovery. The list wraps if it is shorter."));
    StepI(TEXT("CYCLES (0 = LOOP)"), WaveDraft.Cycles, 0, 50, L + 200, GY, 92, TEXT("0 loops forever with per-cycle scaling. N ends the match after cycle N (more lives wins)."));
    StepF(TEXT("CYCLE HEALTH +"), WaveDraft.CycleHealthGrowth, .05f, 0, 2, L + 300, GY, 92, 2, TEXT(""), TEXT("Health multiplier added per completed cycle."));
    StepF(TEXT("STALL LIMIT"), WaveDraft.MaxWaveSeconds, 10, 30, 900, L + 400, GY, 92, 0, TEXT("s"), TEXT("Failsafe: after this many seconds a wave's leftovers stop fighting and march, then despawn after the grace period."));
    if (Button(WaveDraft.bStallFailsafe ? TEXT("FAILSAFE ON") : TEXT("FAILSAFE OFF"), L + 500, GY, 100, 20, TEXT("Stall failsafe. Keep it on for play; waves can never hold a cycle forever while it is enabled."), true, WaveDraft.bStallFailsafe, WaveDraft.bStallFailsafe ? Teal : Red))
        WaveDraft.bStallFailsafe = !WaveDraft.bStallFailsafe;

    // ---- actions -----------------------------------------------------------------------
    const float AY = Y + 350;
    const bool bLab = CireBalanceLab::IsActive(Mode);
    if (Button(TEXT("APPLY LIVE"), L, AY, 96, 24, TEXT("Validate and apply on the server. Takes effect from the next wave."), !bLab, false, Teal))
    {
        if (CireWaveDirector::ApplyLive(Mode, WaveDraft, &Error)) { WaveDraft = CireWaveDirector::Config(GetWorld()); DeveloperMessage = TEXT("Waves applied live: changes take effect from the next wave."); }
        else DeveloperMessage = Error;
    }
    if (Button(TEXT("SAVE JSON"), L + 100, AY, 96, 24, TEXT("Validate and write Content/Data/Waves.json.")))
        DeveloperMessage = CireWaveDirector::SaveFile(WaveDraft, &Error) ? TEXT("Saved Content/Data/Waves.json.") : Error;
    if (Button(TEXT("LOAD JSON"), L + 200, AY, 96, 24, TEXT("Load Waves.json into the draft for review. Apply to activate.")))
    { FCireWaveConfig Loaded; if (CireWaveDirector::LoadFile(Loaded, &Error)) { WaveDraft = Loaded; WaveSelected = 0; DeveloperMessage = TEXT("Waves.json loaded into the draft. Apply to activate."); } else DeveloperMessage = Error; }
    if (Button(TEXT("DEFAULTS"), L + 300, AY, 96, 24, TEXT("Reset the draft to the built-in progression: normal, normal, armored, armored escort, boss.")))
    { WaveDraft = CireWaveDirector::Defaults(); WaveSelected = 0; DeveloperMessage = TEXT("Draft reset to defaults. Apply to activate, Save to keep."); }
    if (Button(TEXT("SKIP TO THIS"), L + 400, AY, 98, 24, TEXT("Make the selected wave the next one in the live cycle (uses the applied config)."), !bLab && WaveSelected < (State ? State->WavesPerCycle : 1)))
        DeveloperMessage = CireWaveDirector::SkipTo(Mode, WaveSelected + 1, false, &Error) ? FString::Printf(TEXT("Wave %d is next."), WaveSelected + 1) : Error;
    if (Button(TEXT("SPAWN NOW"), L + 502, AY, 98, 24, TEXT("Spawn the selected draft wave immediately as an extra test wave (survival phase only)."), !bLab, false, Red))
        DeveloperMessage = CireWaveDirector::SpawnNow(Mode, W, &Error) ? FString::Printf(TEXT("Spawning %s now."), *W.Label) : Error;
#endif
}
