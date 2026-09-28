// wave-director: F8 developer tools > Waves page. A live wave composer drawn with the
// CireUIStyle kit. Edits are a draft until APPLY LIVE (server-authoritative, takes effect
// from the next wave); SAVE writes Content/Data/Waves.json. See Docs/Waves.md.
#include "CireHUD.h"
#include "CireGame.h"
#include "CireWaves.h"
#include "CireNPCArchetypes.h"
#include "CireRaces.h" // monster-races
#include "CireMonsterExpansion.h" // monster-expansion
#include "CireBalanceLab.h"
#include "CireDeveloperTools.h"
#include "Engine/World.h"

namespace
{
// ui-themes: themed colours are references to CireUIColors so they follow the active UI theme.
const FLinearColor &Gold=CireUIColors::Gold, &Parchment=CireUIColors::Parchment, &Muted=CireUIColors::Muted;
const FLinearColor Teal(.2f, .71f, .59f, 1), Row(.045f, .06f, .07f, .96f), RowSelected(.1f, .13f, .12f, 1), Red(.75f, .2f, .23f, 1);
// waves-modes: the "MODES & SCALE" sub-page replaces the wave composer pane (dev UI, one HUD per client).
bool bWaveModesPage = false;

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
    const bool bLab = CireBalanceLab::IsActive(Mode);
    // ---- header: live status ------------------------------------------------------
    const FString Live = State ? FString::Printf(TEXT("LIVE  |  %s  |  NEXT: %s"), State->WaveLabel.IsEmpty() ? TEXT("no wave yet") : *State->WaveLabel,
        State->NextWaveLabel.IsEmpty() ? TEXT("-") : *State->NextWaveLabel) : FString(TEXT("LIVE  |  match state unavailable"));
    Label(Painter().Fit(Live, 9.5f, 470, ECireFont::Heading), L, T - 2, 9.5f, Teal);
    // waves-modes: live scale, damage toggles, packs and game-type presets.
    if (Button(bWaveModesPage ? TEXT("BACK TO WAVE") : TEXT("MODES & SCALE"), L + 480, T - 6, 120, 20,
        TEXT("Live wave scale (health / damage / speed, applied to the monsters already on the road), per-wave damage on/off, fight-back packs, pack sizes and the game-type presets."),
        true, bWaveModesPage, Teal))
        bWaveModesPage = !bWaveModesPage;

    // ---- wave list (left) ---------------------------------------------------------
    const float ListX = L, ListY = T + 18, ListW = 170, RowH = 27;
    const int32 Visible = 6;
    WaveListScroll = FMath::Clamp(WaveListScroll, 0, FMath::Max(0, WaveDraft.Waves.Num() - Visible));
    Painter().Rect(ListX, ListY, ListW, Visible * RowH + 4, FLinearColor(0, 0, 0, .35f));
    for (int32 I = WaveListScroll; I < FMath::Min(WaveDraft.Waves.Num(), WaveListScroll + Visible); ++I)
    {
        const auto& W = WaveDraft.Waves[I];
        const float RY = ListY + 2 + (I - WaveListScroll) * RowH;
        const bool bSel = I == WaveSelected, Over = Hit(ListX + 2, RY, ListW - 4, RowH - 2);
        Painter().Rect(ListX + 2, RY, ListW - 4, RowH - 2, bSel ? RowSelected : Over ? FLinearColor(.07f, .09f, .1f, 1) : Row);
        if (bSel) Painter().Rect(ListX + 2, RY, 3, RowH - 2, Gold);
        // rules-conformance: in campaign order the list is the whole match (row I = cycle I / per-cycle, wave I % per-cycle).
        const int32 PerCycle = FMath::Max(1, WaveDraft.WavesPerCycle);
        const bool bInCycle = WaveDraft.bCampaignOrder ? (WaveDraft.Cycles <= 0 || I < PerCycle * WaveDraft.Cycles) : I < PerCycle;
        Label(Painter().Fit(FString::Printf(TEXT("%d. %s"), I + 1, *W.Label), 9.5f, ListW - 16, ECireFont::Bold), ListX + 10, RY + 2, 9.5f, bInCycle ? Parchment : Muted);
        // monster-races: the wave's race (or the rotation's race for the live cycle).
        const int32 ListCycle = WaveDraft.bCampaignOrder ? I / PerCycle : State ? FMath::Max(0, State->Round - 1) : 0;
        const int32 ListWave = I % PerCycle;
        const FString RaceText = W.Race.IsNone() ? CireWaveDirector::RaceLabel(WaveDraft, W, ListCycle, ListWave) + TEXT("*") : CireWaveDirector::RaceLabel(WaveDraft, W, ListCycle, ListWave);
        // waves-modes: pack waves show their pack layout; damage-off waves are flagged.
        const FString Size = W.Packs > 0 ? FString::Printf(TEXT("%dx%d-%d"), W.Packs, W.PackSizeMin, W.PackSizeMax) : FString::Printf(TEXT("%d/lane"), W.UnitsPerLane());
        Label(Painter().Fit(FString::Printf(TEXT("%s%s  |  %s  |  %s"), W.bDealsDamage ? TEXT("") : TEXT("NO DMG  "), *CireWaveDirector::TypeLabel(W.Type), *Size, *RaceText), 8, ListW - 16, ECireFont::Body), ListX + 10, RY + 15, 8,
            !bInCycle ? Muted : W.bDealsDamage ? Gold : Teal);
        Tip(W.Label, bInCycle ? (WaveDraft.bCampaignOrder ? TEXT("Select to edit. Campaign order: this wave is played in the match (cycle = row / waves per cycle).") : TEXT("Select to edit. This wave is inside the cycle."))
            : TEXT("Beyond the waves the match plays: kept in the list but not played until the cycle (or cycle count) grows."), ListX + 2, RY, ListW - 4, RowH - 2);
        if (Over && Clicked) { Clicked = false; PlayUIFeedback(); WaveSelected = I; bWaveEditBonus = false; } // monster-expansion: back to the cycle
    }
    const float LB = ListY + Visible * RowH + 10;
    if (Button(TEXT("ADD"), ListX, LB, 54, 22, TEXT("Add a Normal wave after the selected one (max 40).")) && WaveDraft.Waves.Num() < 40)
    { WaveDraft.Waves.Insert(CireWaveDirector::Template(ECireWaveType::Normal), WaveSelected + 1); ++WaveSelected; }
    if (Button(TEXT("DUP"), ListX + 58, LB, 54, 22, TEXT("Duplicate the selected wave.")) && WaveDraft.Waves.Num() < 40)
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

    // ---- waves-modes: MODES & SCALE sub-page (right pane) ------------------------------
    if (bWaveModesPage)
    {
        const float EX = L + 190, EW = 410;
        FCireWaveDef& W = WaveDraft.Waves[WaveSelected];
        CireUIStyle::Header(Painter(), EX, T + 16, EW, TEXT("MODES & LIVE SCALE"), Teal, 10.f);
        // Live scale: applies at once to the living wave monsters (and every later spawn).
        auto& S = WaveDraft.Live;
        StepF(TEXT("LIVE HEALTH x"), S.Health, .05f, .1f, 10, EX, T + 56, 94, 2, TEXT(""), TEXT("Every wave monster's health (living ones keep their health fraction)."));
        StepF(TEXT("LIVE DAMAGE x"), S.Damage, .05f, 0, 10, EX + 98, T + 56, 94, 2, TEXT(""), TEXT("Every wave monster's damage."));
        StepF(TEXT("LIVE SPEED x"), S.Speed, .05f, .2f, 3, EX + 196, T + 56, 94, 2, TEXT(""), TEXT("Every wave monster's movement speed (on top of the -20% wave pace)."));
        if (Button(TEXT("APPLY SCALE NOW"), EX + 294, T + 56, 116, 20, TEXT("Apply the live scale to the running match at once (monsters already on the road included). Also: console cire.WaveScale <health> <damage> <speed>."), !bLab, false, Teal))
        {
            if (CireWaveDirector::SetLiveScale(Mode, S, &Error)) DeveloperMessage = FString::Printf(TEXT("Live wave scale: health x%.2f, damage x%.2f, speed x%.2f."), S.Health, S.Damage, S.Speed);
            else DeveloperMessage = Error;
        }
        // Monster rules (Waves.json "monsters") and the pack-size modifier.
        auto& Mr = WaveDraft.Monsters;
        StepI(TEXT("PACK SIZE +/-"), WaveDraft.PackSizeBonus, -3, 3, EX, T + 96, 76, TEXT("Difficulty modifier: added to every pack of every pack wave (pack size 1-10)."));
        StepF(TEXT("WAVE SPEED x"), Mr.Speed, .05f, .2f, 2, EX + 80, T + 96, 76, 2, TEXT(""), TEXT("Every wave monster's speed (Eric: 0.80 = -20%)."));
        StepF(TEXT("ARMORED SPEED x"), Mr.ArmoredSpeed, .05f, .1f, 2, EX + 160, T + 96, 84, 2, TEXT(""), TEXT("Armored marchers move this much slower on top of the wave speed (0.50 = -50%)."));
        StepF(TEXT("ARMORED STUN x"), Mr.ArmoredStunMultiplier, .25f, 0, 5, EX + 248, T + 96, 80, 2, TEXT(""), TEXT("Stuns last this much longer on armored marchers."));
        if (Button(Mr.bArmoredSlowImmune ? TEXT("NO SLOWS") : TEXT("SLOWABLE"), EX + 332, T + 96, 78, 20, TEXT("Armored marchers cannot be slowed (they can still be stunned, rooted and path-blocked)."), true, Mr.bArmoredSlowImmune, Teal))
            Mr.bArmoredSlowImmune = !Mr.bArmoredSlowImmune;
        // The selected wave: damage toggle, packs, fight-back packs.
        const FString DamageTitle = FString::Printf(TEXT("WAVE %d: %s"), WaveSelected + 1, W.bDealsDamage ? TEXT("DAMAGE ON") : TEXT("DAMAGE OFF"));
        if (Button(DamageTitle, EX, T + 136, 130, 20, TEXT("Damage ON: this wave fights the heroes (armored units never do). OFF: it only paths to the castle like an armored round, except the fight-back packs below."),
            true, !W.bDealsDamage, W.bDealsDamage ? Gold : Teal))
            W.bDealsDamage = !W.bDealsDamage;
        StepI(TEXT("PACKS"), W.Packs, 0, 8, EX + 136, T + 136, 70, TEXT("0 = the rows spawn as authored. N = N packs; the rows are each pack's recipe (counts are weights)."));
        if (StepI(TEXT("SIZE MIN"), W.PackSizeMin, 1, 8, EX + 210, T + 136, 64, TEXT("Smallest pack.")) && W.PackSizeMax < W.PackSizeMin) W.PackSizeMax = W.PackSizeMin;
        if (StepI(TEXT("SIZE MAX"), W.PackSizeMax, 1, 8, EX + 278, T + 136, 64, TEXT("Largest pack.")) && W.PackSizeMin > W.PackSizeMax) W.PackSizeMin = W.PackSizeMax;
        {
            const int32 Lo = W.Packs * FMath::Clamp(W.PackSizeMin + WaveDraft.PackSizeBonus, 1, 10), Hi = W.Packs * FMath::Clamp(W.PackSizeMax + WaveDraft.PackSizeBonus, 1, 10);
            Label(W.Packs > 0 ? FString::Printf(TEXT("%d-%d / lane"), Lo, Hi) : FString::Printf(TEXT("%d / lane"), W.UnitsPerLane()), EX + 348, T + 140, 9.5f, Gold);
        }
        Label(TEXT("FIGHT BACK"), EX, T + 168, 8.5f, Muted);
        for (int32 P = 1; P <= 8; ++P)
        {
            const bool bOn = W.FightBackPacks.Contains(P);
            if (Button(FString::FromInt(P), EX + 60 + (P - 1) * 24, T + 164, 22, 20,
                FString::Printf(TEXT("Pack %d fights back when this wave's damage is off (the Hybrid mode). Legacy waves without packs count rows."), P), true, bOn, Red))
            { if (bOn) W.FightBackPacks.Remove(P); else { W.FightBackPacks.Add(P); W.FightBackPacks.Sort(); } }
        }
        if (Button(TEXT("ALL WAVES OFF"), EX + 256, T + 164, 76, 20, TEXT("Damage off for every wave (Hero TD). Keeps each wave's fight-back packs.")))
            for (auto& X : WaveDraft.Waves) X.bDealsDamage = false;
        if (Button(TEXT("ALL ON"), EX + 336, T + 164, 74, 20, TEXT("Damage on for every wave (Standard).")))
            for (auto& X : WaveDraft.Waves) X.bDealsDamage = true;
        // Presets: load into the draft, save, and play (host, before the first wave).
        Label(TEXT("PRESETS"), EX, T + 196, 8.5f, Muted);
        const TArray<FCireWavePreset>& Presets = CireWaveDirector::Presets();
        float PX = EX + 50;
        for (const FCireWavePreset& P : Presets)
        {
            const float PW = FMath::Min(110.f, TextWidth(P.Label, 8.5f) + 18);
            if (PX + PW > EX + EW) break;
            if (Button(P.Label, PX, T + 192, PW, 20, P.Description + TEXT("\nClick to load it into the draft (APPLY LIVE or PLAY THIS TYPE to use it)."), true, WaveDraft.Preset == P.Id, P.bBuiltIn ? Gold : Teal))
            { CireWaveDirector::ApplyPreset(WaveDraft, P); DeveloperMessage = FString::Printf(TEXT("Preset %s loaded into the draft."), *P.Label); }
            PX += PW + 4;
        }
        const FCireWavePreset* Current = CireWaveDirector::FindPreset(WaveDraft.Preset);
        if (Button(Current ? FString::Printf(TEXT("SAVE OVER %s"), *Current->Label.ToUpper()) : FString(TEXT("SAVE OVER")), EX, T + 222, 136, 22,
            TEXT("Write the draft's damage toggles, fight-back packs, live scale, pack modifier and PvP rounds over the selected preset (Content/Data/WavePresets.json)."), Current != nullptr))
        {
            const FCireWavePreset P = CireWaveDirector::CapturePreset(WaveDraft, Current->Id, Current->Label, Current->Description);
            DeveloperMessage = CireWaveDirector::SavePreset(P, &Error) ? FString::Printf(TEXT("Preset %s saved."), *P.Label) : Error;
        }
        if (Button(TEXT("SAVE AS NEW"), EX + 140, T + 222, 110, 22, TEXT("Save the draft's settings as a new Custom preset. It shows up under GAME TYPE when hosting.")))
        {
            int32 N = 1; while (CireWaveDirector::FindPreset(FName(*FString::Printf(TEXT("custom_%d"), N)))) ++N;
            const FCireWavePreset P = CireWaveDirector::CapturePreset(WaveDraft, FName(*FString::Printf(TEXT("custom_%d"), N)), FString::Printf(TEXT("Custom %d"), N),
                TEXT("Saved from F8 > Waves > Modes & Scale."));
            if (CireWaveDirector::SavePreset(P, &Error)) { WaveDraft.Preset = P.Id; DeveloperMessage = FString::Printf(TEXT("Saved as %s (Content/Data/WavePresets.json)."), *P.Label); }
            else DeveloperMessage = Error;
        }
        const bool bCanPlay = State && State->Wave == 0 && !bLab;
        if (Button(TEXT("PLAY THIS TYPE"), EX + 254, T + 222, 156, 22, TEXT("Host: make the selected preset this match's game type (before the first wave). Afterwards use APPLY LIVE."), bCanPlay && Current != nullptr, false, Teal))
            DeveloperMessage = CireWaveDirector::SelectPreset(Mode, WaveDraft.Preset, &Error) ? TEXT("Game type set for this match.") : Error;
    }
    // ---- selected wave (right) -----------------------------------------------------
    // monster-expansion: EDIT BONUS WAVE switches the composer to Waves.json bonusWave.wave (its own type, never in the cycle).
    const bool bBonusEdit = bWaveEditBonus;
    FCireWaveDef& W = bBonusEdit ? WaveDraft.Bonus.Wave : WaveDraft.Waves[WaveSelected];
    if (!bWaveModesPage) // waves-modes: the MODES & SCALE page owns the right pane
    {
    const float EX = L + 190, EW = 410;
    CireUIStyle::Header(Painter(), EX, T + 16, EW, bBonusEdit ? FString::Printf(TEXT("BONUS LOOT WAVE  |  %s"), *W.Label.ToUpper()) :
        FString::Printf(TEXT("WAVE %d  |  %s"), WaveSelected + 1, *W.Label.ToUpper()), bBonusEdit ? CireMonsterExpansion::SpecialColor(2) : Gold, 10.f);
    const float R1 = T + 42;
    if (Button(FString(TEXT("TYPE: ")) + CireWaveDirector::TypeLabel(W.Type), EX, R1, 128, 22,
        bBonusEdit ? TEXT("The bonus loot wave always has the Bonus Loot type: fleeing treasure creatures that never attack and never cost lives.") :
        TEXT("Cycle the wave type label. Use APPLY TEMPLATE to replace the composition with that type's template. Bonus Loot = fleeing treasure creatures (no lives lost)."), !bBonusEdit))
        W.Type = static_cast<ECireWaveType>((static_cast<int32>(W.Type) + 1) % static_cast<int32>(ECireWaveType::Count));
    if (Button(TEXT("TEMPLATE"), EX + 132, R1, 74, 22, TEXT("Replace this wave's rows, pacing and label with the template for its type (Armored Escort = 1 non-attacking tank + 4 defenders). Template rows follow the wave's race.")))
    { const ECireWaveType Type = W.Type; const FName Race = W.Race; W = CireWaveDirector::Template(Type); W.Race = Race; }
    // monster-races: the wave's race. Rotation = Waves.json campaign.raceRotation for the cycle being played.
    const int32 EditCycle = WaveDraft.bCampaignOrder ? WaveSelected / FMath::Max(1, WaveDraft.WavesPerCycle) : State ? FMath::Max(0, State->Round - 1) : 0;
    const int32 EditWave = WaveDraft.bCampaignOrder ? WaveSelected % FMath::Max(1, WaveDraft.WavesPerCycle) : 0; // rules-conformance
    if (!bBonusEdit)
    {
        const FCireRace* Race = CireRaces::FindRace(W.Race);
        const FString Title = Race ? FString(TEXT("RACE: ")) + Race->Short.ToUpper() : FString(TEXT("RACE: ROTATION"));
        FString Help = Race ? Race->Name + TEXT(". ") + Race->Lore : FString::Printf(TEXT("Follows the campaign rotation: this cycle fields %s. Click to pick a race for this wave."),
            *CireWaveDirector::RaceLabel(WaveDraft, W, EditCycle, EditWave));
        Help += TEXT(" Rows marked with a slot (Line, Caster, ...) take that race's unit; explicit units stay as chosen.");
        if (Button(Title, EX + 210, R1, 104, 22, Help, true, Race != nullptr, Teal)) CireWaveDirector::CycleWaveRace(W);
    }
    if (!bBonusEdit && Button(W.bMustClear ? TEXT("MUST CLEAR") : TEXT("OVERLAPS NEXT"), EX + 318, R1, 92, 22,
        TEXT("Must clear: the next wave waits for this one to die or leak. Overlaps: the next wave's timer starts once this one has fully spawned."), true, W.bMustClear, W.bMustClear ? Teal : Gold))
        W.bMustClear = !W.bMustClear;
    const float R2 = R1 + 42;
    StepF(TEXT("SPAWN INTERVAL"), W.SpawnInterval, .1f, 0, 5, EX, R2, 96, 1, TEXT("s"), TEXT("Seconds between individual spawns inside this wave."));
    StepF(TEXT("DELAY BEFORE"), W.DelayBefore, 1, 0, 120, EX + 104, R2, 96, 0, TEXT("s"), TEXT("Extra wait before this wave, on top of the breather."));
    StepF(TEXT("REWARD x"), W.RewardMultiplier, .05f, 0, 10, EX + 208, R2, 96, 2, TEXT(""), TEXT("Kill XP multiplier for this wave's units (gold always follows the bounty ruling)."));
    Label(FString::Printf(TEXT("%d units / lane"), W.UnitsPerLane()), EX + 316, R2 + 3, 10, W.UnitsPerLane() > 30 ? Red : Gold);

    // Composition rows.
    const float CY = R2 + 30;
    // monster-races: UNIT follows the race by slot; R = rank colour (click cycles), SK = skill tier override.
    Label(TEXT("UNIT (SLOT: RACE UNIT)"), EX, CY, 8, Muted); Label(TEXT("R M T B $"), EX + 100, CY, 8, Muted); Label(TEXT("COUNT"), EX + 156, CY, 8, Muted); Label(TEXT("HEALTH x"), EX + 200, CY, 8, Muted);
    Label(TEXT("DAMAGE x"), EX + 260, CY, 8, Muted); Label(TEXT("SIZE"), EX + 320, CY, 8, Muted); Label(TEXT("SK"), EX + 369, CY, 8, Muted);
    const TArray<FName> Ids = ArchetypeIds();
    int32 RemoveRow = INDEX_NONE;
    for (int32 I = 0; I < W.Units.Num() && I < 8; ++I)
    {
        auto& U = W.Units[I];
        const float RY = CY + 13 + I * 25;
        Painter().Rect(EX, RY - 1, EW, 23, I % 2 ? FLinearColor(0, 0, 0, .18f) : FLinearColor(0, 0, 0, .3f));
        (void)Ids;
        const FName RowRace = CireWaveDirector::RaceFor(WaveDraft, W, EditCycle, I, EditWave);
        if (bBonusEdit)
        {
            // monster-expansion: bonus rows cycle through the bestiary creatures.
            if (Button(Painter().Fit(CireWaveDirector::RowUnitLabel(U, NAME_None, EditCycle), 8.5f, 92, ECireFont::Bold), EX + 1, RY, 96, 21,
                TEXT("Click to cycle the bestiary creatures (treasure goblin, gilded stag, ...). Bonus creatures flee, never attack and escape after the escape timer.")))
            {
                const auto& List = CireMonsterExpansion::Creatures();
                const int32 At = List.IndexOfByPredicate([&](const CireMonsterExpansion::FCreature& C) { return C.Id == U.Archetype; });
                if (!List.IsEmpty()) U.Archetype = List[(At + 1) % List.Num()].Id;
            }
        }
        else if (Button(Painter().Fit(CireWaveDirector::RowUnitLabel(U, RowRace, EditCycle), 8.5f, 92, ECireFont::Bold), EX + 1, RY, 96, 21,
            TEXT("Click to cycle: the race slots (Line, Bruiser, Tank, Caster, Ranged, Special, Warlord, Colossus, Boss = colossus/warlord by cycle), then this race's units by name. Slot rows follow the wave's race.")))
            CireWaveDirector::CycleRowUnit(U, RowRace);
        StepI(FString(), U.Count, 1, 20, EX + 156, RY, 42, TEXT("Units of this row per lane (1-20)."));
        StepF(FString(), U.HealthScale, .05f, .1f, 20, EX + 200, RY, 58, 2, TEXT(""), TEXT("Health multiplier on top of wave/round scaling."));
        StepF(FString(), U.DamageScale, .05f, .05f, 10, EX + 260, RY, 58, 2, TEXT(""), TEXT("Damage multiplier."));
        StepF(FString(), U.SizeScale, .1f, .5f, 3, EX + 320, RY, 46, 1, TEXT(""), TEXT("Body size multiplier."));
        {
            // Skill tier override: A = the wave schedule, 1..3 = force tier I..III, 0 skills with "-".
            const FString Tier = U.SkillCount == 0 ? FString(TEXT("-")) : U.SkillTier > 0 ? FString::FromInt(U.SkillTier) : FString(TEXT("A"));
            if (Button(Tier, EX + 368, RY, 22, 21, TEXT("Skills: A = the wave schedule (none before Waves.json firstSkillWave, then more and stronger). 1-3 forces tier I-III. - = no skills."),
                true, U.SkillTier > 0 || U.SkillCount == 0, Teal))
            {
                if (U.SkillCount == 0) { U.SkillCount = -1; U.SkillTier = 0; }
                else if (U.SkillTier >= 3) { U.SkillTier = 0; U.SkillCount = 0; }
                else ++U.SkillTier;
            }
        }
        if (Button(TEXT("x"), EX + 392, RY, 18, 21, TEXT("Remove this row."), W.Units.Num() > 1, false, Red)) RemoveRow = I;
        // Rank (colour + strength): click cycles normal, veteran, elite, champion, warlord, mythic.
        {
            const ECireNPCRank Rank = U.EffectiveRank();
            const FCireRankStyle& Style = CireRaces::Rank(Rank);
            const float BX = EX + 99;
            const bool Over = Hit(BX, RY, 10, 21);
            Painter().Rect(BX, RY, 10, 21, Rank == ECireNPCRank::Normal ? FLinearColor(.12f, .12f, .12f, .9f) : Style.Color);
            Label(Style.Label.Left(1), BX + 1.5f, RY + 5, 8, Rank == ECireNPCRank::Normal ? Muted : FLinearColor::Black);
            Tip(Style.Label + TEXT(" rank"), FString::Printf(TEXT("x%.2g health, x%.2g damage, x%.2g size, +%d skills. Click to cycle normal / veteran (green) / elite (blue) / champion (purple) / warlord (orange) / mythic (red)."),
                Style.Health, Style.Damage, Style.Size, Style.SkillBonus), BX, RY, 10, 21);
            if (Over && Clicked) { Clicked = false; PlayUIFeedback(); if (U.bElite && U.Rank < ECireNPCRank::Elite) U.Rank = ECireNPCRank::Elite; CireWaveDirector::CycleRowRank(U); }
        }
        const TCHAR* Flags[] = {TEXT("M"), TEXT("T"), TEXT("B"), TEXT("$")};
        bool* Values[] = {&U.bNonAttacking, &U.bEscortee, &U.bBoss, &U.bRare};
        const TCHAR* Help[] = {TEXT("Marcher: never attacks, walks through heroes, must be stopped."),
            TEXT("Escortee: the protected tank of an escort wave (implies marcher); attackers in the wave defend it."), TEXT("Lane boss: leaks for 10 lives (archetype leak cost)."),
            TEXT("Rare: this row always spawns as a Rare Spawn (glow, Rare plate, tougher, rich personal chest; Waves.json rareSpawn stats). Random rares come from the RARES row below.")}; // monster-expansion
        // Flag toggles: Marcher (non-attacking), escorTee, Boss, Rare ($).
        for (int32 F = 0; F < (bBonusEdit ? 0 : 4); ++F)
        {
            const float BX = EX + 110 + F * 11;
            const bool bOn = *Values[F];
            const bool Over = Hit(BX, RY, 10, 21);
            Painter().Rect(BX, RY, 10, 21, bOn ? (F == 3 ? CireMonsterExpansion::SpecialColor(1) : F == 2 ? Red : Teal) : FLinearColor(.12f, .12f, .12f, .9f));
            Label(Flags[F], BX + 1.5f, RY + 5, 8, bOn ? FLinearColor::Black : Muted);
            Tip(FString(Flags[F]), Help[F], BX, RY, 10, 21);
            if (Over && Clicked) { Clicked = false; PlayUIFeedback(); *Values[F] = !bOn; if (F == 1 && *Values[F]) U.bNonAttacking = true; if (F == 2 && *Values[F]) { U.bNonAttacking = false; U.bEscortee = false; }
                if (F == 3 && *Values[F]) { U.bBoss = false; U.bEscortee = false; U.bNonAttacking = false; } }
        }
    }
    if (RemoveRow != INDEX_NONE && W.Units.Num() > 1) W.Units.RemoveAt(RemoveRow);
    const float AddY = CY + 13 + FMath::Min(W.Units.Num(), 8) * 25 + 2;
    if (W.Units.Num() < 8 && Button(TEXT("+ ROW"), EX, AddY, 70, 20, TEXT("Add a composition row (max 8).")))
    {
        FCireWaveUnit U; U.Archetype = TEXT("hollow_infantry"); U.Slot = TEXT("line"); U.Count = 1; // monster-races: follows the race
        if (bBonusEdit) { U.Archetype = TEXT("treasure_goblin"); U.Slot = NAME_None; } // monster-expansion
        W.Units.Add(U);
    }
    }

    // ---- globals ---------------------------------------------------------------------
    const float GY = Y + 318;
    Painter().Rect(L, GY - 18, 600, 1, Gold * FLinearColor(1, 1, 1, .5f));
    StepF(TEXT("BREATHER"), WaveDraft.BreatherSeconds, 1, 0, 120, L, GY, 92, 0, TEXT("s"), TEXT("Seconds between a cleared wave and the next spawn: the Skill Shop window (15-20 s recommended)."));
    StepI(TEXT("WAVES / CYCLE"), WaveDraft.WavesPerCycle, 1, 10, L + 100, GY, 92, TEXT("Cleared waves before prep, arena and recovery. The list wraps if it is shorter."));
    StepI(TEXT("CYCLES (0 = LOOP)"), WaveDraft.Cycles, 0, 50, L + 200, GY, 92, TEXT("0 loops forever with per-cycle scaling. N ends the match after cycle N (more lives wins)."));
    StepF(TEXT("CYCLE HEALTH +"), WaveDraft.CycleHealthGrowth, .05f, 0, 2, L + 300, GY, 92, 2, TEXT(""), TEXT("Health multiplier added per completed cycle."));
    StepF(TEXT("STALL LIMIT"), WaveDraft.MaxWaveSeconds, 10, 30, 900, L + 400, GY, 92, 0, TEXT("s"), TEXT("Failsafe: after this many seconds a wave's leftovers stop fighting and march, then despawn after the grace period."));
    if (Button(WaveDraft.bStallFailsafe ? TEXT("FAILSAFE ON") : TEXT("FAILSAFE OFF"), L + 500, GY, 100, 20, TEXT("Stall failsafe. Keep it on for play; waves can never hold a cycle forever while it is enabled."), true, WaveDraft.bStallFailsafe, WaveDraft.bStallFailsafe ? Teal : Red))
        WaveDraft.bStallFailsafe = !WaveDraft.bStallFailsafe;

    // ---- actions -----------------------------------------------------------------------
    // ---- pacing (Waves.json "pacing") -----------------------------------------------------
    const float PY = GY + 34;
    StepF(TEXT("SPAWN AT ROUTE"), WaveDraft.SpawnAlongRoute, .05f, 0, .7f, L, PY, 80, 2, TEXT(""), TEXT("Where waves appear along the road: 0 = the breach gate, 0.30 = 30% of the way to the castle. Shorter walk, faster waves."));
    StepF(TEXT("MARCH SPEED x"), WaveDraft.MarchSpeedMultiplier, .05f, .5f, 2, L + 86, PY, 80, 2, TEXT(""), TEXT("Wave units walk this much faster while not fighting (combat speed unchanged; bosses excluded)."));
    StepF(TEXT("FIRST WAVE"), WaveDraft.FirstWaveDelay, 1, 0, 120, L + 172, PY, 80, 0, TEXT("s"), TEXT("Delay before the first wave of the match."));
    StepF(TEXT("PREP"), WaveDraft.PrepSeconds, 5, 5, 600, L + 258, PY, 80, 0, TEXT("s"), TEXT("Town preparation after a cycle's last wave."));
    StepF(TEXT("ARENA"), WaveDraft.ArenaSeconds, 5, 15, 900, L + 344, PY, 80, 0, TEXT("s"), TEXT("Arena time limit (ends early when a team is wiped)."));
    StepF(TEXT("RECOVERY"), WaveDraft.RecoverySeconds, 1, 1, 180, L + 430, PY, 80, 0, TEXT("s"), TEXT("Regroup time after the arena before the next cycle."));
    if (Button(WaveDraft.bEarlyContinue ? TEXT("READY-UP ON") : TEXT("READY-UP OFF"), L + 516, PY, 84, 20, TEXT("When on, the breather (Skill Shop window) ends 1 s after every human player presses Ready."), true, WaveDraft.bEarlyContinue, WaveDraft.bEarlyContinue ? Teal : Gold))
        WaveDraft.bEarlyContinue = !WaveDraft.bEarlyContinue;
    // ---- monster-expansion: Rare Spawns and the Bonus Loot Wave (Waves.json rareSpawn / bonusWave) ------------------
    {
        const float SY = PY + 34;
        const FLinearColor RareC = CireMonsterExpansion::SpecialColor(1), BonusC = CireMonsterExpansion::SpecialColor(2);
        auto& Rr = WaveDraft.Rare; auto& Bn = WaveDraft.Bonus;
        if (Button(Rr.bEnabled ? TEXT("RARES") : TEXT("NO RARE"), L, SY, 44, 20, TEXT("Rare Spawns: occasionally a rare creature (lich, griffon, drake, frostfang, horned brute) joins a normal or pack wave in both lanes. It glows, wears a Rare plate, is tougher and drops a rich personal chest."), true, Rr.bEnabled, RareC))
            Rr.bEnabled = !Rr.bEnabled;
        float RarePct = Rr.Chance * 100.f;
        if (StepF(TEXT("RARE %"), RarePct, 5, 0, 100, L + 46, SY, 60, 0, TEXT(""), TEXT("Chance that an eligible wave (normal, packs, custom) brings a rare."))) Rr.Chance = RarePct / 100.f;
        StepI(TEXT("RARE FROM"), Rr.FromWave, 1, 200, L + 108, SY, 60, TEXT("First global wave that can roll a rare. Early waves stay readable."));
        StepF(TEXT("RARE $ (MOB)"), Rr.Bounty, 1, 0, 100, L + 170, SY, 60, 0, TEXT(""), TEXT("Kill bounty of a rare in mob values (a normal mob is 1). Its personal chest comes on top (LootTables.json rareSpawn)."));
        if (Button(Bn.bEnabled ? TEXT("BONUS") : TEXT("NO BONUS"), L + 232, SY, 44, 20, TEXT("Bonus Loot Wave: after a cleared wave (never the cycle's last) a short wave of treasure creatures may flee down the lane during the breather. They never attack, never cost lives and escape after the escape timer."), true, Bn.bEnabled, BonusC))
            Bn.bEnabled = !Bn.bEnabled;
        float BonusPct = Bn.Chance * 100.f;
        if (StepF(TEXT("BONUS %"), BonusPct, 5, 0, 100, L + 278, SY, 60, 0, TEXT(""), TEXT("Chance per cleared wave (at most MaxPerCycle per cycle, from bonusWave.fromWave)."))) Bn.Chance = BonusPct / 100.f;
        StepF(TEXT("ESCAPE"), Bn.EscapeSeconds, 1, 5, 120, L + 340, SY, 60, 0, TEXT("s"), TEXT("Seconds before an uncaught bonus creature escapes with its loot. The breather only grows by bonusWave.extraBreatherSeconds."));
        StepF(TEXT("BONUS $ (MOB)"), Bn.Bounty, 1, 0, 100, L + 402, SY, 60, 0, TEXT(""), TEXT("Kill bounty of each bonus creature in mob values. Its personal purse comes on top (LootTables.json bonusWave)."));
        if (Button(bWaveEditBonus ? TEXT("EDIT WAVES") : TEXT("EDIT BONUS"), L + 464, SY, 66, 20, TEXT("Switch the composer between the cycle's waves and the bonus loot wave's composition."), true, bWaveEditBonus, BonusC))
            bWaveEditBonus = !bWaveEditBonus;
        if (Button(TEXT("SPAWN BONUS"), L + 532, SY, 68, 20, TEXT("Start the applied bonus loot wave now (survival phase only)."), !bLab, false, BonusC))
        {
            FString BonusError;
            DeveloperMessage = CireWaveDirector::StartBonusWave(Mode, &BonusError) ? TEXT("Bonus loot wave started.") : BonusError;
        }
    }
    const float AY = Y + 414; // monster-expansion: one row lower (rares / bonus row above)

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
