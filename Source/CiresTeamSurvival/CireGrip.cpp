#include "CireGrip.h"

#include "BonePose.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ReferenceSkeleton.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogCireGrip, Log, All);

namespace
{
using namespace CireGrip;

struct FData
{
    bool bLoaded = false;
    TMap<FString, FWeapon> Weapons;
    TSet<FString> SwapPresets;
};
FData GData;

FVector ReadVector(const TSharedPtr<FJsonObject>& O, const TCHAR* Key, const FVector& Default)
{
    const TArray<TSharedPtr<FJsonValue>>* V = nullptr;
    if (!O->TryGetArrayField(Key, V) || V->Num() != 3) return Default;
    return FVector((*V)[0]->AsNumber(), (*V)[1]->AsNumber(), (*V)[2]->AsNumber());
}

const FData& Data()
{
    if (GData.bLoaded) return GData;
    GData.bLoaded = true;
    FString Text; TSharedPtr<FJsonObject> Root;
    if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Data/WeaponGrips.json"))) ||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root)
    {
        UE_LOG(LogCireGrip, Warning, TEXT("CIRE_GRIP_DATA missing WeaponGrips.json; props keep their legacy placement."));
        return GData;
    }
    const TSharedPtr<FJsonObject>* Weapons = nullptr;
    // fab-integration: WeaponGrips.fab.json (Fab weapon meshes, keyed by full object path) merges in after the base file.
    TArray<TSharedPtr<FJsonObject>> Sources;
    if (Root->TryGetObjectField(TEXT("weapons"), Weapons)) Sources.Add(*Weapons);
    for (const TCHAR* FabFile : {TEXT("Data/WeaponGrips.fab.json"), TEXT("Data/WeaponGrips.fabx.json")}) // monster-expansion: + the bestiary creatures' weapons
    {
        FString FabText; TSharedPtr<FJsonObject> FabRoot; const TSharedPtr<FJsonObject>* FabWeapons = nullptr;
        if (FFileHelper::LoadFileToString(FabText, *FPaths::Combine(FPaths::ProjectContentDir(), FabFile)) &&
            FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(FabText), FabRoot) && FabRoot && FabRoot->TryGetObjectField(TEXT("weapons"), FabWeapons))
            Sources.Add(*FabWeapons);
    }
    for (const TSharedPtr<FJsonObject>& Source : Sources)
        for (const auto& Pair : Source->Values)
        {
            const TSharedPtr<FJsonObject>* O = nullptr;
            if (!Pair.Value->TryGetObject(O)) continue;
            FWeapon W; W.Mesh = FString(Pair.Key.ToView());
            W.Handle = ReadVector(*O, TEXT("handle"), FVector::ZeroVector);
            W.Axis = ReadVector(*O, TEXT("axis"), FVector::UpVector).GetSafeNormal();
            W.Edge = ReadVector(*O, TEXT("edge"), FVector::ForwardVector).GetSafeNormal();
            double Number = 0;
            if ((*O)->TryGetNumberField(TEXT("radiusCm"), Number)) W.RadiusCm = FMath::Clamp(Number, .3, 8.);
            if ((*O)->TryGetNumberField(TEXT("tilt"), Number)) W.TiltDeg = FMath::Clamp(Number, -80., 80.);
            FString Grip;
            (*O)->TryGetStringField(TEXT("grip"), Grip);
            W.bShield = Grip == TEXT("shield"); W.bAmmo = Grip == TEXT("ammo");
            (*O)->TryGetBoolField(TEXT("carry"), W.bCarry);
            W.CarryAt = ReadVector(*O, TEXT("carryAt"), (*O)->HasField(TEXT("offHand")) ? FVector(.34, .2, .62) : FVector(.34, .02, .62));
            W.Hand = W.bAmmo ? EHand::None : W.bShield ? EHand::Shield : EHand::Power;
            if ((*O)->HasField(TEXT("offHand")))
            {
                W.bTwoHand = true;
                W.OffHand = ReadVector(*O, TEXT("offHand"), FVector::ZeroVector);
                W.OffAxis = ReadVector(*O, TEXT("offAxis"), W.Axis).GetSafeNormal();
            }
            if (W.Axis.IsNearlyZero() || W.Edge.IsNearlyZero() || FMath::Abs(FVector::DotProduct(W.Axis, W.Edge)) > .95f) continue;
            GData.Weapons.Add(W.Mesh, W);
        }
    const TArray<TSharedPtr<FJsonValue>>* Swap = nullptr;
    if (Root->TryGetArrayField(TEXT("swapHandPresets"), Swap))
        for (const auto& V : *Swap) GData.SwapPresets.Add(V->AsString());
    UE_LOG(LogCireGrip, Log, TEXT("CIRE_GRIP_DATA weapons=%d swap=%d"), GData.Weapons.Num(), GData.SwapPresets.Num());
    return GData;
}

FVector Pos(const FReferenceSkeleton& Skeleton, const TCHAR* Name) { return ReferenceComponent(Skeleton, Name).GetLocation(); }
bool Has(const FReferenceSkeleton& Skeleton, const FString& Name) { return Skeleton.FindBoneIndex(FName(*Name)) != INDEX_NONE; }

FVector Circumcenter(const FVector& A, const FVector& B, const FVector& C)
{
    const FVector AB = B - A, AC = C - A, N = FVector::CrossProduct(AB, AC);
    const double D = 2.0 * N.SizeSquared();
    if (D < UE_SMALL_NUMBER) return (A + B + C) / 3.0;
    return A + (FVector::CrossProduct(N, AB) * AC.SizeSquared() + FVector::CrossProduct(AC, N) * AB.SizeSquared()) / D;
}

double DistanceToLine(const FVector& P, const FVector& Origin, const FVector& Dir)
{
    const FVector D = P - Origin;
    return (D - Dir * FVector::DotProduct(D, Dir)).Size();
}

struct FPalm { FVector H, A, T, N; bool bValid = false; };
FPalm Palm(const FReferenceSkeleton& S, const FString& Side, bool bRight)
{
    FPalm P;
    const FString Hand = TEXT("hand") + Side;
    if (!Has(S, Hand) || !Has(S, TEXT("middle_01") + Side) || !Has(S, TEXT("index_01") + Side) || !Has(S, TEXT("pinky_01") + Side)) return P;
    P.H = Pos(S, *Hand);
    P.A = (Pos(S, *(TEXT("middle_01") + Side)) - P.H).GetSafeNormal();
    FVector T = Pos(S, *(TEXT("index_01") + Side)) - Pos(S, *(TEXT("pinky_01") + Side));
    P.T = (T - P.A * FVector::DotProduct(T, P.A)).GetSafeNormal();
    // Palm normal from hand chirality: right palm = A x T, left palm = T x A (T runs pinky -> index).
    P.N = (bRight ? FVector::CrossProduct(P.A, P.T) : FVector::CrossProduct(P.T, P.A)).GetSafeNormal();
    P.bValid = !P.A.IsNearlyZero() && !P.T.IsNearlyZero() && !P.N.IsNearlyZero();
    return P;
}

/** Curls one chain: per-joint component-space rotations (cumulative), returns joint positions and the tip. */
struct FChain
{
    TArray<int32> Bones; TArray<FVector> P; TArray<FQuat> WorldRot; TArray<FQuat> ParentRot;
    TArray<FQuat> Cum; TArray<FVector> Curled; FVector Tip = FVector::ZeroVector;
};
bool LoadChain(const FReferenceSkeleton& S, const FString& Finger, const FString& Side, FChain& C)
{
    for (int32 J = 1; J <= 3; ++J)
    {
        const int32 I = S.FindBoneIndex(FName(*FString::Printf(TEXT("%s_%02d%s"), *Finger, J, *Side)));
        if (I == INDEX_NONE) return false;
        C.Bones.Add(I);
        const FTransform W = ReferenceComponent(S, S.GetBoneName(I));
        C.P.Add(W.GetLocation()); C.WorldRot.Add(W.GetRotation());
        C.ParentRot.Add(ReferenceComponent(S, S.GetBoneName(S.GetParentIndex(I))).GetRotation());
    }
    return true;
}
/** Deltas[j] is the joint's own component-space rotation; applied cumulatively down the chain. */
void Curl(FChain& C, const TArray<FQuat>& Deltas)
{
    C.Cum.SetNum(3); C.Curled.SetNum(3);
    FQuat Cum = FQuat::Identity;
    C.Curled[0] = C.P[0];
    for (int32 J = 0; J < 3; ++J)
    {
        Cum = Deltas[J] * Cum;
        C.Cum[J] = Cum;
        if (J < 2) C.Curled[J + 1] = C.Curled[J] + Cum.RotateVector(C.P[J + 1] - C.P[J]);
    }
    C.Tip = C.Curled[2] + Cum.RotateVector(C.P[2] - C.P[1]) * .9;
}
void Emit(const FChain& C, FHandPose& Out)
{
    // New world = Cum * bind world; local = inverse(new parent world) * new world.
    for (int32 J = 0; J < 3; ++J)
    {
        const FQuat NewWorld = C.Cum[J] * C.WorldRot[J];
        const FQuat NewParent = J == 0 ? C.ParentRot[0] : C.Cum[J - 1] * C.WorldRot[J - 1];
        Out.Bones.Add(C.Bones[J]);
        Out.Local.Add((NewParent.Inverse() * NewWorld).GetNormalized());
    }
}

struct FPoseKey
{
    const USkeletalMesh* Mesh; bool bRight; EHand Type; int32 Radius;
    bool operator==(const FPoseKey& O) const { return Mesh == O.Mesh && bRight == O.bRight && Type == O.Type && Radius == O.Radius; }
    friend uint32 GetTypeHash(const FPoseKey& K) { return HashCombine(HashCombine(GetTypeHash(K.Mesh), K.Radius), (uint32)K.Type * 2 + K.bRight); }
};
}

FTransform CireGrip::ReferenceComponent(const FReferenceSkeleton& Skeleton, FName Bone)
{
    int32 Index = Skeleton.FindBoneIndex(Bone);
    if (Index == INDEX_NONE) return FTransform::Identity;
    FTransform Result = Skeleton.GetRefBonePose()[Index];
    while ((Index = Skeleton.GetParentIndex(Index)) != INDEX_NONE) Result = Result * Skeleton.GetRefBonePose()[Index];
    return Result;
}

const CireGrip::FWeapon* CireGrip::FindWeapon(const FString& MeshName) { return Data().Weapons.Find(MeshName); }
const CireGrip::FWeapon* CireGrip::FindWeapon(const UStaticMesh* Mesh)
{
    if (!Mesh) return nullptr;
    // fab-integration: full object path first (Fab packs reuse armory names such as SM_WarHammer), then the name.
    if (const FWeapon* ByPath = Data().Weapons.Find(Mesh->GetPathName())) return ByPath;
    return FindWeapon(Mesh->GetName());
}
bool CireGrip::SwapsHands(const FString& Preset) { return Data().SwapPresets.Contains(Preset); }

CireGrip::FHandPose CireGrip::BuildHandPose(const USkeletalMesh& Body, bool bRight, EHand Type, float RadiusMesh)
{
    static TMap<FPoseKey, FHandPose> Cache;
    const FPoseKey Key{&Body, bRight, Type, FMath::RoundToInt(RadiusMesh * 200.f)};
    if (const FHandPose* Found = Cache.Find(Key)) return *Found;
    FHandPose Out; Out.Type = Type; Out.RadiusMesh = RadiusMesh;
    const FReferenceSkeleton& S = Body.GetRefSkeleton();
    const FString Side = bRight ? TEXT("_r") : TEXT("_l");
    const FPalm Palm = ::Palm(S, Side, bRight);
    Out.Hand = S.FindBoneIndex(FName(*(TEXT("hand") + Side)));
    if (!Palm.bValid || Out.Hand == INDEX_NONE || Type == EHand::None) return Cache.Add(Key, Out);
    const FTransform HandCS = ReferenceComponent(S, S.GetBoneName(Out.Hand));
    FVector K = FVector::CrossProduct(Palm.A, Palm.N).GetSafeNormal();
    if (FVector::DotProduct(FQuat(K, .5).RotateVector(Palm.A), Palm.N) < 0) K = -K; // curl toward the palm
    const TCHAR* Fingers[] = {TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")};
    FChain Chains[4]; bool bHas[4] = {false, false, false, false};
    FVector Knuckles = FVector::ZeroVector; int32 Count = 0;
    for (int32 F = 0; F < 4; ++F)
        if ((bHas[F] = LoadChain(S, Fingers[F], Side, Chains[F]))) { Knuckles += Chains[F].P[0]; ++Count; }
    if (Count < 2) return Cache.Add(Key, Out);
    Knuckles /= Count;
    // Handle axis from the palm: across the fingers (pinky -> index), leaning toward the wrist on the pinky
    // side like a real power grip, tangent to the finger roots on the palm side at the wrapped radius.
    const double Rt = FMath::Max(.05, (double)RadiusMesh);
    FVector Axis = (Palm.T + Palm.A * .27).GetSafeNormal();
    FVector Origin = Knuckles + Palm.N * Rt + Palm.A * (Rt * .15);
    if (Type == EHand::Pinch) Origin = Knuckles + Palm.N * Rt;
    // Cupped hands: no finger root may sit inside the handle; lift the axis off the lowest knuckle.
    for (int32 Pass = 0; Pass < 4 && Type != EHand::Pinch; ++Pass)
    {
        double Deficit = 0;
        for (int32 F = 0; F < 4; ++F)
            if (bHas[F])
            {
                Deficit = FMath::Max(Deficit, Rt * 1.05 - DistanceToLine(Chains[F].P[0], Origin, Axis));
                Deficit = FMath::Max(Deficit, Rt * 1.05 - DistanceToLine(Chains[F].P[1], Origin, Axis));
            }
        if (Deficit <= 0) break;
        Origin += Palm.N * Deficit;
    }
    FChain Index;
    for (int32 F = 0; F < 4; ++F)
    {
        if (!bHas[F]) continue;
        FChain& C = Chains[F];
        TArray<FQuat> D;
        if (Type == EHand::Pinch)
        {
            const double Pinch[] = {32, 42, 75, 80};
            D.Init(FQuat(K, FMath::DegreesToRadians(Pinch[F])), 3);
        }
        else
        {
            // Proximal and distal curls that keep every finger joint and the tip on the wrapped cylinder.
            double Best = TNumericLimits<double>::Max();
            D.Init(FQuat::Identity, 3);
            for (int32 P1 = 0; P1 <= 22; ++P1)
                for (int32 P2 = 4; P2 <= 26; ++P2)
                {
                    const double T1 = FMath::DegreesToRadians(P1 * 5.0), T2 = FMath::DegreesToRadians(P2 * 5.0);
                    TArray<FQuat> Try = {FQuat(K, T1), FQuat(K, T2), FQuat(K, T2 * .8)};
                    Curl(C, Try);
                    const double D1 = DistanceToLine(C.Curled[1], Origin, Axis), D2 = DistanceToLine(C.Curled[2], Origin, Axis), DT = DistanceToLine(C.Tip, Origin, Axis);
                    double J = FMath::Square(D1 - Rt) + FMath::Square(D2 - Rt) + .6 * FMath::Square(DT - Rt * .95);
                    for (const double Dist : {D1, D2, DT}) if (Dist < Rt * .9) J += 50.0 * FMath::Square(Rt * .9 - Dist);
                    if (J < Best) { Best = J; D = Try; }
                }
        }
        Curl(C, D);
        Emit(C, Out);
        if (F == 0) Index = C;
    }
    FVector X = (Palm.A - Axis * FVector::DotProduct(Palm.A, Axis)).GetSafeNormal();
    Out.GripComponent = FTransform(FRotationMatrix::MakeFromZX(Axis, X).ToQuat(), Origin, FVector::OneVector);
    Out.GripInHand = Out.GripComponent.GetRelativeTransform(HandCS);
    // Thumb: search a roll about the knuckle axis and a curl of the two outer joints that closes it over the fingers.
    FChain Thumb;
    if (LoadChain(S, TEXT("thumb"), Side, Thumb))
    {
        const FVector Dir = (Thumb.P[1] - Thumb.P[0]).GetSafeNormal();
        FVector Kt = FVector::CrossProduct(Dir, Palm.N).GetSafeNormal();
        if (Kt.IsNearlyZero()) Kt = K;
        double Best = TNumericLimits<double>::Max(); TArray<FQuat> BestD; BestD.Init(FQuat::Identity, 3);
        for (int32 A = -6; A <= 6; ++A)
            for (int32 B = -8; B <= 8; ++B)
            {
                const FQuat Roll(Palm.A, FMath::DegreesToRadians(A * 10.0));
                const FQuat Bend(Roll.RotateVector(Kt), FMath::DegreesToRadians(B * 10.0));
                TArray<FQuat> D = {Roll, Bend, Bend};
                Curl(Thumb, D);
                double J;
                if (Type == EHand::Pinch) J = (Thumb.Tip - Index.Tip).SizeSquared() * 4.0;
                else
                {
                    const double R = Rt;
                    const double D3 = DistanceToLine(Thumb.Curled[2], Origin, Axis), DT = DistanceToLine(Thumb.Tip, Origin, Axis);
                    J = FMath::Square(DT - R) + FMath::Square(D3 - R) + .3 * (Thumb.Tip - Index.Curled[1]).SizeSquared();
                    if (DT < R * .85) J += 100.0 * FMath::Square(R * .85 - DT);
                    if (D3 < R * .85) J += 100.0 * FMath::Square(R * .85 - D3);
                }
                if (J < Best) { Best = J; BestD = D; }
            }
        Curl(Thumb, BestD);
        Emit(Thumb, Out);
        if (Type == EHand::Pinch) Out.PinchInHand = HandCS.InverseTransformPosition((Thumb.Tip + Index.Tip) * .5);
    }
    Out.bValid = true;
    return Cache.Add(Key, Out);
}

CireGrip::FPlacement CireGrip::Place(const USkeletalMesh& Body, FName Bone, const FWeapon& Weapon, float PropScale, float MeshScale)
{
    FPlacement Out;
    const FReferenceSkeleton& S = Body.GetRefSkeleton();
    const bool bRight = Bone == TEXT("hand_r");
    if (!(bRight || Bone == TEXT("hand_l")) || Weapon.bAmmo || MeshScale <= UE_SMALL_NUMBER) return Out;
    const FString Side = bRight ? TEXT("_r") : TEXT("_l");
    const FPalm Palm = ::Palm(S, Side, bRight);
    if (!Palm.bValid) return Out;
    const float Scale = PropScale / MeshScale; // mesh units per prop centimetre
    const float Radius = Weapon.RadiusCm * Scale;
    const FChain* None = nullptr; (void)None;
    // Finger thickness: about a third of a proximal phalanx.
    FChain Middle; const bool bMiddle = LoadChain(S, TEXT("middle"), Side, Middle);
    const float Finger = bMiddle ? .3f * static_cast<float>((Middle.P[1] - Middle.P[0]).Size()) : .4f;
    Out.Pose = BuildHandPose(Body, bRight, Weapon.bShield ? EHand::Power : Weapon.Hand, (Weapon.bShield ? 1.3f * Scale : Radius) + Finger);
    if (!Out.Pose.bValid) return Out;
    const FQuat MeshBasis = FRotationMatrix::MakeFromZX(Weapon.Axis, Weapon.Edge).ToQuat();
    FTransform Component;
    if (Weapon.bShield)
    {
        // Strapped on the outside of the forearm, face out on the back-of-hand side, long axis across the arm.
        const FName Lower(*(TEXT("lowerarm") + Side));
        if (S.FindBoneIndex(Lower) == INDEX_NONE) return Out;
        const FVector Elbow = ReferenceComponent(S, Lower).GetLocation();
        const float Forearm = static_cast<float>((Palm.H - Elbow).Size());
        const FVector Center = FMath::Lerp(Elbow, Palm.H, .55f) - Palm.N * (Forearm * .11f + 3.2f * Scale);
        const FQuat Target = FRotationMatrix::MakeFromZX(Palm.T, Palm.N).ToQuat();
        const FQuat Rotation = Target * MeshBasis.Inverse();
        Component = FTransform(Rotation, Center - Rotation.RotateVector(Weapon.Handle * Scale), FVector(Scale));
        Out.Bone = Lower;
    }
    else
    {
        FQuat Grip = Out.Pose.GripComponent.GetRotation();
        const FVector Z = Grip.GetAxisZ(), X = Grip.GetAxisX();
        const float Tilt = FMath::DegreesToRadians(Weapon.TiltDeg);
        const FVector TZ = Z * FMath::Cos(Tilt) + X * FMath::Sin(Tilt), TX = X * FMath::Cos(Tilt) - Z * FMath::Sin(Tilt);
        const FQuat Target = FRotationMatrix::MakeFromZX(TZ, TX).ToQuat();
        const FQuat Rotation = Target * MeshBasis.Inverse();
        Component = FTransform(Rotation, Out.Pose.GripComponent.GetLocation() - Rotation.RotateVector(Weapon.Handle * Scale), FVector(Scale));
        Out.Bone = Bone;
    }
    Out.Component = Component;
    Out.Relative = Component.GetRelativeTransform(ReferenceComponent(S, Out.Bone));
    if (Weapon.bCarry && !Weapon.bShield && S.FindBoneIndex(TEXT("spine_03")) != INDEX_NONE)
    {
        // Upright carry: grip in front of the hip with the elbow bent, handle vertical, knuckles forward.
        const FName Upper(*(TEXT("upperarm") + Side));
        const FVector Shoulder = ReferenceComponent(S, Upper).GetLocation();
        const FVector Chest = ReferenceComponent(S, TEXT("spine_03")).GetLocation();
        const float Arm = static_cast<float>((Palm.H - Shoulder).Size());
        FVector Forward = ((ReferenceComponent(S, TEXT("ball_l")).GetLocation() - ReferenceComponent(S, TEXT("foot_l")).GetLocation()) +
            (ReferenceComponent(S, TEXT("ball_r")).GetLocation() - ReferenceComponent(S, TEXT("foot_r")).GetLocation())).GetSafeNormal2D();
        if (Forward.IsNearlyZero()) Forward = FVector(0, 1, 0);
        FVector Lateral = (Shoulder - Chest).GetSafeNormal2D();
        Lateral = (Lateral - Forward * FVector::DotProduct(Lateral, Forward)).GetSafeNormal();
        // Two-handed weapons are carried nearer the midline so the second hand reaches its grip (WeaponGrips.json carryAt).
        const FVector Grip = Shoulder + Forward * (Weapon.CarryAt.X * Arm) - FVector::UpVector * (Weapon.CarryAt.Z * Arm) - Lateral * (Weapon.CarryAt.Y * Arm);
        const FVector Up = (FVector::UpVector + Forward * .12f).GetSafeNormal();
        const FTransform Target(FRotationMatrix::MakeFromZX(Up, Forward).ToQuat(), Grip, FVector::OneVector);
        const FTransform HandTarget = Lateral.IsNearlyZero() ? FTransform::Identity : Out.Pose.GripInHand.Inverse() * Target;
        Out.CarryInChest = HandTarget.GetRelativeTransform(ReferenceComponent(S, TEXT("spine_03")));
        Out.bCarry = !Lateral.IsNearlyZero() && !Out.CarryInChest.ContainsNaN();
    }
    if (Weapon.bTwoHand && !Weapon.bShield)
    {
        const bool bOffRight = !bRight;
        const FString OffSide = bOffRight ? TEXT("_r") : TEXT("_l");
        Out.OffPose = BuildHandPose(Body, bOffRight, EHand::Power, Radius + Finger);
        if (Out.OffPose.bValid)
        {
            const FVector Point = Component.TransformPosition(Weapon.OffHand);
            FVector Dir = Component.TransformVectorNoScale(Weapon.OffAxis).GetSafeNormal();
            // The support hand's knuckles continue its arm (shoulder -> grip), so the wrist stays reachable.
            const FVector OffShoulder = ReferenceComponent(S, FName(*(TEXT("upperarm") + OffSide))).GetLocation();
            FVector Reach = Point - OffShoulder;
            FVector OffX = (Reach - Dir * FVector::DotProduct(Reach, Dir)).GetSafeNormal();
            if (OffX.IsNearlyZero()) OffX = (Palm.N - Dir * FVector::DotProduct(Palm.N, Dir)).GetSafeNormal();
            const FTransform OffGrip(FRotationMatrix::MakeFromZX(Dir, OffX).ToQuat(), Point, FVector::OneVector);
            const FTransform OffHand = Out.OffPose.GripInHand.Inverse() * OffGrip;
            Out.OffHandInMain = OffHand.GetRelativeTransform(ReferenceComponent(S, Bone));
            Out.OffHandBone = FName(*(TEXT("hand") + OffSide));
            Out.bTwoHand = true;
        }
    }
    Out.bValid = !Out.Relative.ContainsNaN();
    return Out;
}

void CireGrip::AddToHands(const USkeletalMesh& Body, const FPlacement& P, FHands& Hands)
{
    if (!P.bValid || !P.Pose.bValid) return;
    const FReferenceSkeleton& S = Body.GetRefSkeleton();
    const int32 Side = S.GetBoneName(P.Pose.Hand) == TEXT("hand_r") ? 1 : 0;
    // A weapon held in the off hand of a two-hander (summoner's dagger) releases that second grip.
    if (Hands.bTwoHand && Side != Hands.MainSide && !P.bTwoHand)
    {
        Hands.bTwoHand = false;
        Hands.Pose[Side] = P.Pose; Hands.Weight[Side] = 1.f;
    }
    // A held weapon outranks a shield strap hand; the first held prop of a hand wins.
    else if (Hands.Weight[Side] <= 0.f || Hands.Pose[Side].Type == EHand::Shield) { Hands.Pose[Side] = P.Pose; Hands.Weight[Side] = 1.f; }
    for (int32 Arm = 0; Arm < 2; ++Arm)
    {
        const TCHAR* Suffix = Arm ? TEXT("_r") : TEXT("_l");
        Hands.Arm[Arm][0] = S.FindBoneIndex(FName(FString(TEXT("upperarm")) + Suffix));
        Hands.Arm[Arm][1] = S.FindBoneIndex(FName(FString(TEXT("lowerarm")) + Suffix));
        Hands.Arm[Arm][2] = S.FindBoneIndex(FName(FString(TEXT("hand")) + Suffix));
    }
    if (P.bTwoHand && P.OffPose.bValid && Hands.Weight[1 - Side] <= 0.f)
    {
        Hands.bTwoHand = true; Hands.MainSide = Side; Hands.OffHandInMain = P.OffHandInMain; Hands.TwoHandWeight = 1.f;
        Hands.Pose[1 - Side] = P.OffPose; Hands.Weight[1 - Side] = 1.f;
    }
    if (P.bCarry)
    {
        Hands.bCarry = true; Hands.MainSide = Side; Hands.CarryInChest = P.CarryInChest; Hands.TwoHandWeight = 1.f;
        Hands.Chest = S.FindBoneIndex(TEXT("spine_03"));
    }
}

FTransform CireGrip::ComponentBone(const FCompactPose& Pose, int32 MeshBone)
{
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    FCompactPoseBoneIndex Index = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(MeshBone));
    FTransform Result = FTransform::Identity;
    if (!Index.IsValid()) return Result;
    Result = Pose[Index];
    for (Index = Bones.GetParentBoneIndex(Index); Index.IsValid(); Index = Bones.GetParentBoneIndex(Index)) Result = Result * Pose[Index];
    return Result;
}

namespace
{
FQuat Between(const FVector& From, const FVector& To) { return FQuat::FindBetweenNormals(From.GetSafeNormal(), To.GetSafeNormal()); }

/** Places Hand at Target (component space) by bending Upper/Lower, then sets the hand's rotation. */
void TwoBoneIK(FCompactPose& Pose, const int32 Chain[3], const FTransform& Target, float Weight)
{
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    FCompactPoseBoneIndex I[3];
    for (int32 J = 0; J < 3; ++J)
    {
        if (Chain[J] == INDEX_NONE) return;
        I[J] = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Chain[J]));
        if (!I[J].IsValid()) return;
    }
    const FCompactPoseBoneIndex UpperParent = Bones.GetParentBoneIndex(I[0]);
    const FCompactPoseBoneIndex LowerParent = Bones.GetParentBoneIndex(I[1]);
    const FCompactPoseBoneIndex HandParent = Bones.GetParentBoneIndex(I[2]);
    if (!UpperParent.IsValid() || LowerParent != I[0] && Bones.GetParentBoneIndex(LowerParent) != I[0]) return;
    const FTransform Up = CireGrip::ComponentBone(Pose, Chain[0]), Lo = CireGrip::ComponentBone(Pose, Chain[1]), Ha = CireGrip::ComponentBone(Pose, Chain[2]);
    const FVector A = Up.GetLocation(), B = Lo.GetLocation(), C = Ha.GetLocation();
    const FVector Goal = FMath::Lerp(C, Target.GetLocation(), Weight);
    const double L1 = (B - A).Size(), L2 = (C - B).Size();
    if (L1 < UE_SMALL_NUMBER || L2 < UE_SMALL_NUMBER) return;
    FVector ToGoal = Goal - A; double D = ToGoal.Size();
    if (D < UE_SMALL_NUMBER) return;
    const FVector Dir = ToGoal / D;
    D = FMath::Clamp(D, FMath::Abs(L1 - L2) + .001, L1 + L2 - .001);
    FVector Bend = (B - A) - Dir * FVector::DotProduct(B - A, Dir);
    if (Bend.IsNearlyZero()) Bend = FVector::UpVector - Dir * FVector::DotProduct(FVector::UpVector, Dir);
    Bend.Normalize();
    const double Along = (L1 * L1 - L2 * L2 + D * D) / (2.0 * D);
    const FVector Elbow = A + Dir * Along + Bend * FMath::Sqrt(FMath::Max(0.0, L1 * L1 - Along * Along));
    const FVector Wrist = A + Dir * D;
    // Upper arm: swing its direction onto the new elbow.
    const FQuat UpperWorld = Between(B - A, Elbow - A) * Up.GetRotation();
    const FTransform UpperParentCS = CireGrip::ComponentBone(Pose, Bones.MakeMeshPoseIndex(UpperParent).GetInt());
    Pose[I[0]].SetRotation((UpperParentCS.GetRotation().Inverse() * UpperWorld).GetNormalized());
    // Lower arm (after the upper arm moved).
    const FTransform LowerNow = CireGrip::ComponentBone(Pose, Chain[1]);
    const FTransform HandNow = CireGrip::ComponentBone(Pose, Chain[2]);
    const FQuat LowerWorld = Between(HandNow.GetLocation() - LowerNow.GetLocation(), Wrist - Elbow) * LowerNow.GetRotation();
    const FTransform LowerParentCS = CireGrip::ComponentBone(Pose, Bones.MakeMeshPoseIndex(LowerParent).GetInt());
    Pose[I[1]].SetRotation((LowerParentCS.GetRotation().Inverse() * LowerWorld).GetNormalized());
    // Hand orientation.
    const FTransform HandParentCS = CireGrip::ComponentBone(Pose, Bones.MakeMeshPoseIndex(HandParent).GetInt());
    const FQuat HandWorld = FQuat::Slerp(Ha.GetRotation(), Target.GetRotation(), Weight);
    Pose[I[2]].SetRotation((HandParentCS.GetRotation().Inverse() * HandWorld).GetNormalized());
}
}

void CireGrip::TwistSpine(FCompactPose& Pose, float Degrees)
{
    if (FMath::Abs(Degrees) < .05f || !FMath::IsFinite(Degrees)) return;
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    for (const TCHAR* Name : {TEXT("spine_01"), TEXT("spine_02"), TEXT("spine_03")})
    {
        const int32 Mesh = Bones.GetReferenceSkeleton().FindBoneIndex(Name);
        if (Mesh == INDEX_NONE) continue;
        const FCompactPoseBoneIndex Bone = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
        const FCompactPoseBoneIndex Parent = Bone.IsValid() ? Bones.GetParentBoneIndex(Bone) : FCompactPoseBoneIndex(INDEX_NONE);
        if (!Parent.IsValid()) continue;
        const FQuat ParentWorld = ComponentBone(Pose, Bones.MakeMeshPoseIndex(Parent).GetInt()).GetRotation();
        const FVector Axis = ParentWorld.UnrotateVector(FVector::UpVector).GetSafeNormal();
        Pose[Bone].SetRotation((FQuat(Axis, FMath::DegreesToRadians(Degrees / 3.f)) * Pose[Bone].GetRotation()).GetNormalized());
    }
}

void CireGrip::Apply(FCompactPose& Pose, const FHands& Hands)
{
    const FBoneContainer& Bones = Pose.GetBoneContainer();
    if (Hands.bCarry && Hands.Chest != INDEX_NONE && Hands.TwoHandWeight > KINDA_SMALL_NUMBER)
    {
        const FTransform Target = Hands.CarryInChest * ComponentBone(Pose, Hands.Chest);
        if (!Target.ContainsNaN()) TwoBoneIK(Pose, Hands.Arm[Hands.MainSide], Target, FMath::Clamp(Hands.TwoHandWeight, 0.f, 1.f));
    }
    if (Hands.bTwoHand && Hands.TwoHandWeight > KINDA_SMALL_NUMBER)
    {
        const int32 Main = Hands.MainSide, Off = 1 - Main;
        if (Hands.Arm[Main][2] != INDEX_NONE)
        {
            const FTransform MainHand = ComponentBone(Pose, Hands.Arm[Main][2]);
            const FTransform Target = Hands.OffHandInMain * MainHand;
            if (!Target.ContainsNaN()) TwoBoneIK(Pose, Hands.Arm[Off], Target, FMath::Clamp(Hands.TwoHandWeight, 0.f, 1.f));
        }
    }
    for (int32 Side = 0; Side < 2; ++Side)
    {
        const float W = FMath::Clamp(Hands.Weight[Side], 0.f, 1.f);
        const FHandPose& P = Hands.Pose[Side];
        if (W <= 0.f || !P.bValid) continue;
        for (int32 I = 0; I < P.Bones.Num(); ++I)
        {
            const FCompactPoseBoneIndex C = Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(P.Bones[I]));
            if (!C.IsValid()) continue;
            Pose[C].SetRotation(FQuat::Slerp(Pose[C].GetRotation(), P.Local[I], W).GetNormalized());
        }
    }
}
