#include "JJAnimReviewChecks.h"
#include "JJAnimReviewModel.h"

namespace JJAnimReviewChecksPrivate
{
    FTransform Bone(const FJJReviewPoseSample& Sample, int32 Index)
    {
        return Sample.ComponentTransforms[Index].ToTransform();
    }

    double Angle(const FQuat& A, const FQuat& B)
    {
        return FMath::RadiansToDegrees(A.AngularDistance(B));
    }

    int32 Add(TArray<FJJReviewCheckResult>& Results, const FString& Check, const FString& Status,
        FName Name, int32 Start, int32 End, double Value, double Limit,
        const FString& Unit, const FString& Message, int32 PreviousJointIndex = INDEX_NONE)
    {
        const int32 PreviousIndex = Check == TEXT("joint_limits") ? PreviousJointIndex : Results.Num() - 1;
        if (Results.IsValidIndex(PreviousIndex))
        {
            FJJReviewCheckResult& Last = Results[PreviousIndex];
            const int32 AxisEnd = Message.Find(TEXT(" angle="));
            const bool bSameJointAxis = Check == TEXT("joint_limits") && AxisEnd != INDEX_NONE
                && Last.Message.Left(AxisEnd) == Message.Left(AxisEnd);
            if (Last.Check == Check && Last.Status == Status && Last.Bone == Name
                && Last.Unit == Unit && Last.EndFrame + 1 >= Start
                && (Last.Message == Message || bSameJointAxis))
            {
                Last.EndFrame = FMath::Max(End, Last.EndFrame);
                if (bSameJointAxis && Value > Last.Measured)
                {
                    Last.Message = Message;
                }
                Last.Measured = FMath::Max(Value, Last.Measured);
                return PreviousIndex;
            }
        }

        FJJReviewCheckResult Result;
        Result.Check = Check;
        Result.Status = Status;
        Result.Bone = Name;
        Result.StartFrame = Start;
        Result.EndFrame = End;
        Result.Measured = Value;
        Result.Threshold = Limit;
        Result.Unit = Unit;
        Result.Message = Message;
        return Results.Add(MoveTemp(Result));
    }
}

TArray<FJJReviewCheckResult> JJAnimReviewChecks::Evaluate(const FJJReviewPoseData& Pose,
    const FJJReviewProfileSettings& Profile, const FJJReviewCheckContext& Context)
{
    using namespace JJAnimReviewChecksPrivate;
    TArray<FJJReviewCheckResult> Results;
    FString Unused;
    FString Error;
    // Reuse the hash input validation, including finite transforms and topology.
    if (!JJAnimReviewModel::ComputeContentId(Pose, Unused, Error))
    {
        Add(Results, TEXT("input"), TEXT("failed"), NAME_None, 0, 0, 0, 0, TEXT(""), Error);
        return Results;
    }
    const int32 Count = FMath::Min(Pose.Samples.Num(), 10000);
    if (Count < Pose.Samples.Num())
    {
        Add(Results, TEXT("coverage"), TEXT("warning"), NAME_None, 0, 0, Count, Pose.Samples.Num(), TEXT("samples"),
            TEXT("Checks limited to first 10000 samples; identity and loop endpoints use full pose."));
    }
    for (const TPair<FName, FJJReviewCheckSettings>& Entry : Profile.Checks)
    {
        const FString Key = Entry.Key.ToString();
        const double Limit = Entry.Value.Threshold;
        const int32 Before = Results.Num();
        auto Report = [&](FName Name, int32 First, int32 Last, double Value, const FString& Unit)
        {
            if (Value > Limit)
            {
                Add(Results, Key, TEXT("failed"), Name, First, Last, Value, Limit, Unit, TEXT(""));
            }
        };
        auto Warn = [&](const FString& Message)
        {
            Add(Results, Key, TEXT("warning"), NAME_None, 0, 0, 0, Limit, TEXT(""), Message);
        };
        if (!Entry.Value.bEnabled)
        {
            Add(Results, Key, TEXT("skipped"), NAME_None, 0, 0, 0, Limit, TEXT(""), TEXT("Disabled by profile"));
            continue;
        }
        if (!FMath::IsFinite(Limit) || Limit < 0 || Count < 2)
        {
            Warn(TEXT("Invalid threshold or fewer than two samples"));
            continue;
        }
        if (Key == TEXT("foot_sliding") || Key == TEXT("penetration"))
        {
            const TArray<FName>& Names = Key == TEXT("foot_sliding") ? Profile.FootBones : Profile.PenetrationBones;
            if (Names.IsEmpty())
            {
                Warn(TEXT("No configured bones; ground check was not performed"));
            }
            for (FName Name : Names)
            {
                const int32 B = Pose.BoneNames.IndexOfByKey(Name);
                if (B == INDEX_NONE)
                {
                    Warn(TEXT("Missing configured bone: ") + Name.ToString());
                    continue;
                }
                int32 ContactStart = INDEX_NONE;
                FVector Anchor = FVector::ZeroVector;
                double GroundHeight = Profile.GroundHeightCm;
                if (Context.bEstimatedProfile && Key == TEXT("foot_sliding"))
                {
                    GroundHeight = Bone(Pose.Samples[0], B).GetTranslation().Z;
                    for (const FJJReviewPoseSample& Sample : Pose.Samples)
                    {
                        GroundHeight = FMath::Min(GroundHeight, Bone(Sample, B).GetTranslation().Z);
                    }
                }
                for (int32 I = 0; I < Count; ++I)
                {
                    const auto& S = Pose.Samples[I];
                    const FVector P = Bone(S, B).GetTranslation();
                    if (Key == TEXT("penetration"))
                    {
                        Report(Name, S.SourceFrame, S.SourceFrame, Profile.GroundHeightCm - P.Z, TEXT("cm"));
                        continue;
                    }
                    const double Dt = I > 0 ? S.TimeSeconds - Pose.Samples[I - 1].TimeSeconds : 0;
                    const double VerticalSpeed = Dt > 0
                        ? FMath::Abs(P.Z - Bone(Pose.Samples[I - 1], B).GetTranslation().Z) / Dt : 0;
                    const double HorizontalSpeed = Dt > 0
                        ? FVector::Dist2D(P, Bone(Pose.Samples[I - 1], B).GetTranslation()) / Dt : 0;
                    if (FMath::Abs(P.Z - GroundHeight) <= Profile.ContactHeightCm
                        && VerticalSpeed <= Profile.ContactSpeedCmPerSecond
                        && (!Context.bEstimatedProfile
                            || HorizontalSpeed <= Profile.EstimatedContactMaxHorizontalSpeedCmPerSecond))
                    {
                        if (ContactStart == INDEX_NONE)
                        {
                            ContactStart = S.SourceFrame;
                            Anchor = P;
                        }
                        Report(Name, ContactStart, S.SourceFrame, FVector::Dist2D(P, Anchor), TEXT("cm"));
                    }
                    else
                    {
                        ContactStart = INDEX_NONE;
                    }
                }
            }
        }
        else if (Key == TEXT("pop") || Key == TEXT("root_discontinuity"))
        {
            const int32 Bones = Key == TEXT("pop") ? Pose.BoneNames.Num() : 1;
            for (int32 B = 0; B < Bones; ++B)
            {
                for (int32 I = 2; I < Count; ++I)
                {
                    const auto& A = Pose.Samples[I - 2];
                    const auto& M = Pose.Samples[I - 1];
                    const auto& Z = Pose.Samples[I];
                    const double Dt0 = M.TimeSeconds - A.TimeSeconds;
                    const double Dt1 = Z.TimeSeconds - M.TimeSeconds;
                    if (Dt0 <= 0 || Dt1 <= 0)
                    {
                        continue;
                    }
                    const FTransform T0 = Key == TEXT("pop") ? Bone(A, B) : A.RootMotion.ToTransform();
                    const FTransform T1 = Key == TEXT("pop") ? Bone(M, B) : M.RootMotion.ToTransform();
                    const FTransform T2 = Key == TEXT("pop") ? Bone(Z, B) : Z.RootMotion.ToTransform();
                    const FVector Expected = T1.GetTranslation() + (T1.GetTranslation() - T0.GetTranslation()) * (Dt1 / Dt0);
                    const double PositionJump = FVector::Distance(Expected, T2.GetTranslation());
                    if (Key == TEXT("pop"))
                    {
                        const FQuat Delta = T1.GetRotation() * T0.GetRotation().Inverse();
                        const FQuat Predicted = FQuat::Slerp(FQuat::Identity, Delta, Dt1 / Dt0) * T1.GetRotation();
                        Report(Pose.BoneNames[B], Z.SourceFrame, Z.SourceFrame,
                            Angle(Predicted, T2.GetRotation()), TEXT("deg"));
                        // Profile pop threshold is angular; translational impulses use root-discontinuity cm threshold.
                        const FJJReviewCheckSettings* Linear = Profile.Checks.Find(TEXT("root_discontinuity"));
                        if (Linear && PositionJump > Linear->Threshold)
                        {
                            Add(Results, Key, TEXT("failed"), Pose.BoneNames[B], Z.SourceFrame, Z.SourceFrame,
                                PositionJump, Linear->Threshold, TEXT("cm"), TEXT("Linear velocity impulse"));
                        }
                    }
                    else
                    {
                        Report(Pose.BoneNames[0], Z.SourceFrame, Z.SourceFrame, PositionJump, TEXT("cm"));
                    }
                }
            }
        }
        else if (Key == TEXT("loop_mismatch"))
        {
            if (!Context.bLooping)
            {
                Add(Results, Key, TEXT("skipped"), NAME_None, 0, 0, 0, Limit, TEXT(""), TEXT("Asset is not marked looping"));
            }
            else
            {
                for (int32 B = 0; B < Pose.BoneNames.Num(); ++B)
                {
                    // Local pose isolates intended root travel from articulation at the seam.
                    FTransform A = Bone(Pose.Samples[0], B);
                    FTransform Z = Bone(Pose.Samples.Last(), B);
                    const int32 Parent = Pose.ParentIndices[B];
                    if (Parent != INDEX_NONE)
                    {
                        A = A.GetRelativeTransform(Bone(Pose.Samples[0], Parent));
                        Z = Z.GetRelativeTransform(Bone(Pose.Samples.Last(), Parent));
                        Report(Pose.BoneNames[B], 0, Pose.Samples.Last().SourceFrame,
                            FVector::Distance(A.GetTranslation(), Z.GetTranslation()), TEXT("cm"));
                    }
                    Report(Pose.BoneNames[B], 0, Pose.Samples.Last().SourceFrame,
                        Angle(A.GetRotation(), Z.GetRotation()), TEXT("deg"));
                }
            }
        }
        else if (Key == TEXT("socket_drift"))
        {
            if (Context.Sockets.Num() != Profile.SocketBonePairs.Num() || Context.Sockets.IsEmpty())
            {
                Warn(TEXT("No socket pairs or some socket/bone bindings could not be resolved"));
            }
            for (const FJJReviewSocket& Socket : Context.Sockets)
            {
                const int32 P = Pose.BoneNames.IndexOfByKey(Socket.Parent);
                const int32 B = Pose.BoneNames.IndexOfByKey(Socket.Target);
                if (P == INDEX_NONE || B == INDEX_NONE)
                {
                    Warn(TEXT("Socket binding references missing bone"));
                    continue;
                }
                double Baseline = 0;
                for (int32 I = 0; I < Count; ++I)
                {
                    const auto& S = Pose.Samples[I];
                    const double Distance = FVector::Distance((Socket.Local * Bone(S, P)).GetTranslation(), Bone(S, B).GetTranslation());
                    if (I == 0)
                    {
                        Baseline = Distance;
                    }
                    Report(Socket.Socket, S.SourceFrame, S.SourceFrame, FMath::Abs(Distance - Baseline), TEXT("cm"));
                }
            }
        }
        else if (Key == TEXT("joint_limits"))
        {
            TMap<FName, TMap<int32, int32>> LastResultByBoneAxis;
            if (Context.Joints.IsEmpty())
            {
                Add(Results, Key, TEXT("skipped"), NAME_None, 0, 0, 0, Limit, TEXT("deg"), TEXT("No Physics Asset angular constraints"));
            }
            for (const FJJReviewJoint& Joint : Context.Joints)
            {
                const int32 C = Pose.BoneNames.IndexOfByKey(Joint.Child);
                const int32 P = Pose.BoneNames.IndexOfByKey(Joint.Parent);
                if (C == INDEX_NONE || P == INDEX_NONE)
                {
                    Warn(TEXT("Constraint references missing bone"));
                    continue;
                }
                for (int32 I = 0; I < Count; ++I)
                {
                    const auto& S = Pose.Samples[I];
                    const FQuat Q = (Joint.ChildFrame * Bone(S, C)).GetRelativeTransform(Joint.ParentFrame * Bone(S, P)).GetRotation();
                    FQuat Swing;
                    FQuat Twist;
                    Q.ToSwingTwist(FVector::XAxisVector, Swing, Twist);
                    const FVector Axis = Swing.GetRotationAxis();
                    const FVector Angles(FMath::RadiansToDegrees(Twist.GetAngle()),
                        FMath::Abs(FMath::RadiansToDegrees(Swing.GetAngle()) * Axis.Z),
                        FMath::Abs(FMath::RadiansToDegrees(Swing.GetAngle()) * Axis.Y));
                    const TCHAR* Axes[] =
                    {
                        TEXT("twist"), TEXT("swing1"), TEXT("swing2")
                    };
                    for (int32 K = 0; K < 3; ++K)
                    {
                        if (Joint.Motion[K] == 1)
                        {
                            const double Measured = FMath::Abs(FMath::UnwindDegrees(Angles[K]));
                            const double Bound = Joint.LimitsDegrees[K];
                            const double Excess = Measured - Bound;
                            if (Excess > Limit)
                            {
                                TMap<int32, int32>& LastResultByAxis = LastResultByBoneAxis.FindOrAdd(Joint.Child);
                                const int32* PreviousIndex = LastResultByAxis.Find(K);
                                const int32 ResultIndex = Add(Results, Key, Context.bEstimatedProfile ? TEXT("warning") : TEXT("failed"),
                                    Joint.Child, S.SourceFrame, S.SourceFrame, Excess, Limit, TEXT("deg"),
                                    FString::Printf(TEXT("axis=%s angle=%.3fdeg limit=%.3fdeg motion=%d (limited)"),
                                        Axes[K], Measured, Bound, Joint.Motion[K]),
                                    PreviousIndex ? *PreviousIndex : INDEX_NONE);
                                LastResultByAxis.Add(K, ResultIndex);
                            }
                        }
                    }
                }
            }
        }
        else
        {
            Warn(TEXT("Unknown check key"));
        }
        if (Results.Num() == Before)
        {
            Add(Results, Key, TEXT("passed"), NAME_None, 0, 0, 0, Limit, TEXT(""), TEXT(""));
        }
    }
    return Results;
}
