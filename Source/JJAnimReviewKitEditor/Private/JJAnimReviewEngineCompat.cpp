#include "JJAnimReviewEngineCompat.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimTypes.h"
#include "Animation/Skeleton.h"
#include "AnimPose.h"
#include "AnimationBlueprintLibrary.h"
#include "Misc/PackageName.h"
#include "UObject/SoftObjectPath.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"

namespace JJAnimReviewEngineCompat
{
    FJJReviewCheckContext CheckContext(UAnimSequence* Animation, const FJJReviewProfileSettings& Profile)
    {
        check(IsInGameThread());
        // A10: Read-only public preview mesh, socket and constraint metadata.
        FJJReviewCheckContext Result;
        Result.bLooping = Animation->bLoop;
        USkeletalMesh* Mesh = Animation->GetPreviewMesh();
        if (!Mesh)
        {
            Mesh = Animation->GetSkeleton()->GetPreviewMesh();
        }
        for (const FJJReviewSocketBonePair& Pair : Profile.SocketBonePairs)
        {
            const USkeletalMeshSocket* Socket = Mesh ? Mesh->FindSocket(Pair.Socket) : Animation->GetSkeleton()->FindSocket(Pair.Socket);
            if (Socket)
            {
                FJJReviewSocket Item;
                Item.Socket = Pair.Socket;
                Item.Parent = Socket->BoneName;
                Item.Target = Pair.Bone;
                Item.Local = Socket->GetSocketLocalTransform();
                Result.Sockets.Add(Item);
            }
        }
        const UPhysicsAsset* Physics = Mesh ? Mesh->GetPhysicsAsset() : nullptr;
        if (Physics)
        {
            for (const UPhysicsConstraintTemplate* Template : Physics->ConstraintSetup)
            {
                if (!Template)
                {
                    continue;
                }
                const FConstraintInstance& C = Template->DefaultInstance;
                FJJReviewJoint Joint;
                Joint.Child = C.ConstraintBone1;
                Joint.Parent = C.ConstraintBone2;
                Joint.ChildFrame = C.GetRefFrame(EConstraintFrame::Frame1);
                Joint.ParentFrame = C.GetRefFrame(EConstraintFrame::Frame2);
                Joint.LimitsDegrees = FVector(C.GetAngularTwistLimit(), C.GetAngularSwing1Limit(), C.GetAngularSwing2Limit());
                auto Motion = [](EAngularConstraintMotion Value)
                {
                    if (Value == ACM_Free)
                    {
                        return 0;
                    }
                    return Value == ACM_Locked ? 2 : 1;
                };
                Joint.Motion = FIntVector(Motion(C.GetAngularTwistMotion()), Motion(C.GetAngularSwing1Motion()), Motion(C.GetAngularSwing2Motion()));
                Result.Joints.Add(Joint);
            }
            Result.Warnings.Add(TEXT("Joint limits use quaternion swing/twist projection, an approximation of the physics cone; verify borderline results."));
        }
        return Result;
    }

    bool NormalizeAssetPath(const FString& Input, FString& ObjectPath, FString& Error)
    {
        // A1: Accept package, object and export-text paths, never filesystem paths.
        FString Path = FPackageName::ExportTextPathToObjectPath(Input.TrimStartAndEnd());
        Path.ReplaceInline(TEXT("\\"), TEXT("/"));
        if (!Path.Contains(TEXT(".")))
        {
            Path += TEXT(".") + FPackageName::GetLongPackageAssetName(Path);
        }

        const FSoftObjectPath SoftPath(Path);
        if (!SoftPath.IsValid() || !SoftPath.GetSubPathString().IsEmpty()
            || !FPackageName::IsValidLongPackageName(SoftPath.GetLongPackageName()))
        {
            Error = TEXT("Expected a long AnimSequence asset path, e.g. /Game/Anims/Walk.Walk");
            return false;
        }
        ObjectPath = SoftPath.ToString();
        return true;
    }

    UAnimSequence* LoadAnimation(const FString& ObjectPath, FString& Error)
    {
        check(IsInGameThread());
        UAnimSequence* Animation = LoadObject<UAnimSequence>(nullptr, *ObjectPath);
        if (!Animation || !Animation->GetSkeleton())
        {
            Error = TEXT("AnimSequence could not be loaded or has no skeleton: ") + ObjectPath;
            return nullptr;
        }
        return Animation;
    }

    bool ResolveProfile(UAnimSequence* Animation, const FString& ProfilePath,
        FJJReviewProfileSettings& Settings, bool& bEstimated, FString& Error)
    {
        check(IsInGameThread());
        bEstimated = ProfilePath.IsEmpty();
        const FReferenceSkeleton& Reference = Animation->GetSkeleton()->GetReferenceSkeleton();
        if (bEstimated)
        {
            TArray<FName> Names;
            for (int32 Index = 0; Index < Reference.GetNum(); ++Index)
            {
                Names.Add(Reference.GetBoneName(Index));
            }
            Settings = FJJReviewProfileSettings::Estimate(Names);
            return true;
        }

        UJJAnimReviewProfile* Profile = LoadObject<UJJAnimReviewProfile>(nullptr, *ProfilePath);
        if (!Profile || Profile->Skeleton.LoadSynchronous() != Animation->GetSkeleton())
        {
            Error = TEXT("Explicit profile is missing or its skeleton does not match the animation");
            return false;
        }
        Settings = Profile->Settings;
        return true;
    }

    bool GetSourceTimes(UAnimSequence* Animation, TArray<double>& Times, FString& Error)
    {
        check(IsInGameThread());
        // A2: Full source-frame grid, independent of profile/capture/check limits.
        const FFrameRate Rate = Animation->GetSamplingFrameRate();
        const double Duration = Animation->GetPlayLength();
        const int32 Count = Animation->GetNumberOfSampledKeys();
        if (Rate.Numerator <= 0 || Rate.Denominator <= 0 || !FMath::IsFinite(Duration)
            || Duration < 0.0 || Count <= 0)
        {
            Error = TEXT("Invalid animation timing/source keys");
            return false;
        }
        Times.Reset(Count + 1);
        for (int32 Frame = 0; Frame < Count; ++Frame)
        {
            Times.Add(FMath::Min(Frame * Rate.AsInterval(), Duration));
        }
        if (!FMath::IsNearlyEqual(Times.Last(), Duration, 1.e-7))
        {
            Times.Add(Duration);
        }
        return true;
    }

    bool Sample(UAnimSequence* Animation, const TArray<double>& Times,
        FJJReviewPoseData& Output, FString& Error)
    {
        check(IsInGameThread());
        Output = FJJReviewPoseData();
        if (!Animation || !Animation->GetSkeleton() || Times.IsEmpty())
        {
            Error = TEXT("Sampling requires animation, skeleton and sample times");
            return false;
        }

        const FFrameRate Rate = Animation->GetSamplingFrameRate();
        Output.DurationSeconds = Animation->GetPlayLength();
        Output.RateNumerator = Rate.Numerator;
        Output.RateDenominator = Rate.Denominator;
        if (Rate.Numerator <= 0 || Rate.Denominator <= 0)
        {
            Error = TEXT("Invalid sampling rate");
            return false;
        }

        FAnimPoseEvaluationOptions Options;
        Options.EvaluationType = EAnimDataEvalType::Raw;
        Options.bIncorporateRootMotionIntoPose = true;
        Options.bExtractRootMotion = false;
        Options.bEvaluateCurves = false;
        const FReferenceSkeleton& Reference = Animation->GetSkeleton()->GetReferenceSkeleton();
        if (Reference.GetNum() == 0)
        {
            Error = TEXT("Sampling requires a skeleton root bone");
            return false;
        }
        const FName RootBone = Reference.GetBoneName(0);
        FAnimPose ZeroPose;
        UAnimPoseExtensions::GetAnimPoseAtTime(Animation, 0.0, Options, ZeroPose);
        if (!UAnimPoseExtensions::IsValid(ZeroPose))
        {
            Error = TEXT("Cannot evaluate the Raw root pose at time zero");
            return false;
        }
        // A3: Both root motion and component poses come from the same Raw evaluation.
        const FTransform InitialRoot = UAnimPoseExtensions::GetBonePose(ZeroPose, RootBone, EAnimPoseSpaces::World);
        if (InitialRoot.ContainsNaN())
        {
            Error = TEXT("Initial Raw root contains a non-finite transform");
            return false;
        }
        double PreviousTime = -1.0;

        for (double Time : Times)
        {
            if (!FMath::IsFinite(Time) || Time < 0.0 || Time > Output.DurationSeconds || Time < PreviousTime)
            {
                Error = TEXT("Sample times must be finite, ordered and within the animation range");
                return false;
            }
            PreviousTime = Time;
            FAnimPose Pose;
            // A2: This public path uses CPU bone-container/compact-pose evaluation.
            UAnimPoseExtensions::GetAnimPoseAtTime(Animation, Time, Options, Pose);
            if (!UAnimPoseExtensions::IsValid(Pose))
            {
                Error = FString::Printf(TEXT("Pose evaluation failed at %.6f seconds"), Time);
                return false;
            }

            TArray<FName> Names;
            UAnimPoseExtensions::GetBoneNames(Pose, Names);
            if (Output.BoneNames.IsEmpty())
            {
                Output.BoneNames = Names;
                for (const FName Bone : Names)
                {
                    const int32 ReferenceIndex = Reference.FindBoneIndex(Bone);
                    const int32 ParentIndex = ReferenceIndex == INDEX_NONE ? INDEX_NONE : Reference.GetParentIndex(ReferenceIndex);
                    Output.ParentIndices.Add(ParentIndex == INDEX_NONE ? INDEX_NONE : Names.IndexOfByKey(Reference.GetBoneName(ParentIndex)));
                }
            }
            if (Names != Output.BoneNames)
            {
                Error = TEXT("Bone topology changed during sampling");
                return false;
            }

            FJJReviewPoseSample& Sample = Output.Samples.AddDefaulted_GetRef();
            Sample.TimeSeconds = Time;
            Sample.SourceFrame = FMath::RoundToInt(Time * Rate.AsDecimal());
            for (const FName Bone : Names)
            {
                // A2: EAnimPoseSpaces::World means component space in AnimPose.h.
                const FTransform& Transform = UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
                if (Transform.ContainsNaN())
                {
                    Error = TEXT("Pose contains a non-finite transform");
                    return false;
                }
                Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(Transform));
            }

            // A3: Cumulative change in the initial Raw root's coordinate frame.
            const FTransform& CurrentRoot = UAnimPoseExtensions::GetBonePose(Pose, RootBone, EAnimPoseSpaces::World);
            const FTransform Motion = CurrentRoot.GetRelativeTransform(InitialRoot);
            if (Motion.ContainsNaN())
            {
                Error = TEXT("Root motion contains a non-finite transform");
                return false;
            }
            Sample.RootMotion = FJJReviewTransform::FromTransform(Motion);
        }

        // A4: Read notify times without instantiating or firing notifies.
        TArray<FAnimNotifyEvent> Events;
        UAnimationBlueprintLibrary::GetAnimationNotifyEvents(Animation, Events);
        for (const FAnimNotifyEvent& Event : Events)
        {
            Output.NotifyTimesSeconds.Add(Event.GetTime());
        }
        Output.NotifyTimesSeconds.Sort();
        return true;
    }
}
