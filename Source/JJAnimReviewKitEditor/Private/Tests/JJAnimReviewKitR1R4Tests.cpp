#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "JJAnimReviewChecks.h"
#include "JJAnimReviewComparison.h"
#include "JJAnimReviewEditing.h"
#include "JJAnimReviewModel.h"
#include "JJAnimReviewSheet.h"

namespace JJAnimReviewKitR1R4TestsPrivate
{
    constexpr EAutomationTestFlags AnimR1R4TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    // Bone layout used by every synthetic pose: 0 root, 1 foot (child of root), 2 hand (child of root).
    constexpr int32 RootBone = 0;
    constexpr int32 FootBone = 1;
    constexpr int32 HandBone = 2;

    struct FFrameState
    {
        FVector Root = FVector::ZeroVector;
        FVector Foot = FVector(0.0, 10.0, 0.0);
        FRotator FootRotation = FRotator::ZeroRotator;
        FVector Hand = FVector(0.0, -30.0, 100.0);
        FRotator HandRotation = FRotator::ZeroRotator;
        FVector RootMotion = FVector::ZeroVector;
    };

    FJJReviewPoseData MakePose(int32 Frames, TFunctionRef<void(int32, FFrameState&)> Edit)
    {
        FJJReviewPoseData Pose;
        Pose.RateNumerator = 30;
        Pose.RateDenominator = 1;
        Pose.DurationSeconds = (Frames - 1) / 30.0;
        Pose.BoneNames = { FName(TEXT("root")), FName(TEXT("foot_l")), FName(TEXT("hand_r")) };
        Pose.ParentIndices = { INDEX_NONE, RootBone, RootBone };
        for (int32 Frame = 0; Frame < Frames; ++Frame)
        {
            FFrameState State;
            Edit(Frame, State);

            FJJReviewPoseSample Sample;
            Sample.SourceFrame = Frame;
            Sample.TimeSeconds = Frame / 30.0;
            Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(FTransform(FQuat::Identity, State.Root)));
            Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(FTransform(State.FootRotation, State.Foot)));
            Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(FTransform(State.HandRotation, State.Hand)));
            Sample.RootMotion = FJJReviewTransform::FromTransform(FTransform(FQuat::Identity, State.RootMotion));
            Pose.Samples.Add(Sample);
        }

        return Pose;
    }

    // Profile with exactly one check enabled, so each test isolates one rule.
    FJJReviewProfileSettings OnlyCheck(const FName Check, double Threshold)
    {
        FJJReviewProfileSettings Profile;
        Profile.Checks.Reset();
        FJJReviewCheckSettings Settings;
        Settings.bEnabled = true;
        Settings.Threshold = Threshold;
        Profile.Checks.Add(Check, Settings);
        return Profile;
    }

    int32 CountStatus(const TArray<FJJReviewCheckResult>& Results, const FString& Check, const FString& Status)
    {
        int32 Count = 0;
        for (const FJJReviewCheckResult& Result : Results)
        {
            if (Result.Check == Check && Result.Status == Status)
            {
                ++Count;
            }
        }

        return Count;
    }

    const FJJReviewCheckResult* FindStatus(const TArray<FJJReviewCheckResult>& Results, const FString& Check, const FString& Status)
    {
        return Results.FindByPredicate([&Check, &Status](const FJJReviewCheckResult& Result)
        {
            return Result.Check == Check && Result.Status == Status;
        });
    }
}

using namespace JJAnimReviewKitR1R4TestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewFootSlidingTest,
    "JJAnimReviewKit.Checks.FootSliding", AnimR1R4TestFlags)

bool FJJAnimReviewFootSlidingTest::RunTest(const FString& Parameters)
{
    FJJReviewProfileSettings Profile = OnlyCheck(TEXT("foot_sliding"), 2.0);
    Profile.FootBones = { FName(TEXT("foot_l")) };
    const FJJReviewCheckContext Context;

    // Planted foot that slides 0.5cm per frame along X while staying on the ground.
    const FJJReviewPoseData Sliding = MakePose(12, [](int32 Frame, FFrameState& State)
    {
        State.Foot.X = Frame * 0.5;
    });
    const TArray<FJJReviewCheckResult> SlidingResults = JJAnimReviewChecks::Evaluate(Sliding, Profile, Context);
    const FJJReviewCheckResult* Failure = FindStatus(SlidingResults, TEXT("foot_sliding"), TEXT("failed"));
    TestNotNull(TEXT("Sliding planted foot is detected"), Failure);
    if (Failure)
    {
        TestEqual(TEXT("Failure names the foot bone"), Failure->Bone, FName(TEXT("foot_l")));
        TestTrue(TEXT("Measured slide exceeds 2cm"), Failure->Measured > 2.0);
        TestEqual(TEXT("Failure ends at the last frame"), Failure->EndFrame, 11);
    }
    TestEqual(TEXT("Consecutive frames are merged into one range"), CountStatus(SlidingResults, TEXT("foot_sliding"), TEXT("failed")), 1);

    const FJJReviewPoseData Planted = MakePose(12, [](int32, FFrameState&)
    {
    });
    TestEqual(TEXT("Still planted foot passes"),
        CountStatus(JJAnimReviewChecks::Evaluate(Planted, Profile, Context), TEXT("foot_sliding"), TEXT("passed")), 1);

    // Swing phase: the foot is lifted, so horizontal travel is not sliding.
    const FJJReviewPoseData Swing = MakePose(12, [](int32 Frame, FFrameState& State)
    {
        State.Foot.X = Frame * 5.0;
        State.Foot.Z = 20.0;
    });
    TestEqual(TEXT("Lifted foot travel is not sliding"),
        CountStatus(JJAnimReviewChecks::Evaluate(Swing, Profile, Context), TEXT("foot_sliding"), TEXT("failed")), 0);

    // Prior review L3: no configured foot bones must warn, not silently pass.
    FJJReviewProfileSettings NoFeet = Profile;
    NoFeet.FootBones.Reset();
    const TArray<FJJReviewCheckResult> NoFeetResults = JJAnimReviewChecks::Evaluate(Planted, NoFeet, Context);
    TestEqual(TEXT("Missing foot bones warns"), CountStatus(NoFeetResults, TEXT("foot_sliding"), TEXT("warning")), 1);
    TestEqual(TEXT("Missing foot bones does not pass"), CountStatus(NoFeetResults, TEXT("foot_sliding"), TEXT("passed")), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewPenetrationTest,
    "JJAnimReviewKit.Checks.Penetration", AnimR1R4TestFlags)

bool FJJAnimReviewPenetrationTest::RunTest(const FString& Parameters)
{
    FJJReviewProfileSettings Profile = OnlyCheck(TEXT("penetration"), 1.0);
    Profile.PenetrationBones = { FName(TEXT("hand_r")) };
    const FJJReviewCheckContext Context;

    const FJJReviewPoseData Below = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        if (Frame == 3 || Frame == 4)
        {
            State.Hand.Z = -5.0;
        }
    });
    const TArray<FJJReviewCheckResult> Results = JJAnimReviewChecks::Evaluate(Below, Profile, Context);
    const FJJReviewCheckResult* Failure = FindStatus(Results, TEXT("penetration"), TEXT("failed"));
    TestNotNull(TEXT("Bone under the floor is detected"), Failure);
    if (Failure)
    {
        TestEqual(TEXT("Range starts at frame 3"), Failure->StartFrame, 3);
        TestEqual(TEXT("Range ends at frame 4"), Failure->EndFrame, 4);
        TestTrue(TEXT("Depth is about 5cm"), FMath::IsNearlyEqual(Failure->Measured, 5.0, 0.01));
    }

    const FJJReviewPoseData Shallow = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.Hand.Z = -0.5;
    });
    TestEqual(TEXT("Within tolerance passes"),
        CountStatus(JJAnimReviewChecks::Evaluate(Shallow, Profile, Context), TEXT("penetration"), TEXT("passed")), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewPopTest,
    "JJAnimReviewKit.Checks.Pop", AnimR1R4TestFlags)

bool FJJAnimReviewPopTest::RunTest(const FString& Parameters)
{
    const FJJReviewProfileSettings Profile = OnlyCheck(TEXT("pop"), 45.0);
    const FJJReviewCheckContext Context;

    // Constant angular velocity (3 deg/frame) is smooth.
    const FJJReviewPoseData Smooth = MakePose(20, [](int32 Frame, FFrameState& State)
    {
        State.HandRotation = FRotator(0.0, Frame * 3.0, 0.0);
    });
    TestEqual(TEXT("Constant angular velocity has no pop"),
        CountStatus(JJAnimReviewChecks::Evaluate(Smooth, Profile, Context), TEXT("pop"), TEXT("failed")), 0);

    // One frame snaps 90 degrees and comes back.
    const FJJReviewPoseData Popping = MakePose(20, [](int32 Frame, FFrameState& State)
    {
        State.HandRotation = FRotator(0.0, Frame == 10 ? 90.0 : 0.0, 0.0);
    });
    const TArray<FJJReviewCheckResult> Results = JJAnimReviewChecks::Evaluate(Popping, Profile, Context);
    const FJJReviewCheckResult* Failure = FindStatus(Results, TEXT("pop"), TEXT("failed"));
    TestNotNull(TEXT("One-frame snap is detected"), Failure);
    if (Failure)
    {
        TestEqual(TEXT("Pop names the hand"), Failure->Bone, FName(TEXT("hand_r")));
        TestTrue(TEXT("Pop range contains frame 10"), Failure->StartFrame <= 10 && Failure->EndFrame >= 10);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewRootDiscontinuityTest,
    "JJAnimReviewKit.Checks.RootDiscontinuity", AnimR1R4TestFlags)

bool FJJAnimReviewRootDiscontinuityTest::RunTest(const FString& Parameters)
{
    const FJJReviewProfileSettings Profile = OnlyCheck(TEXT("root_discontinuity"), 10.0);
    const FJJReviewCheckContext Context;

    const FJJReviewPoseData Steady = MakePose(20, [](int32 Frame, FFrameState& State)
    {
        State.RootMotion.X = Frame * 3.0;
    });
    TestEqual(TEXT("Steady root motion passes"),
        CountStatus(JJAnimReviewChecks::Evaluate(Steady, Profile, Context), TEXT("root_discontinuity"), TEXT("passed")), 1);

    const FJJReviewPoseData Jump = MakePose(20, [](int32 Frame, FFrameState& State)
    {
        State.RootMotion.X = Frame * 3.0 + (Frame >= 8 ? 30.0 : 0.0);
    });
    const FJJReviewCheckResult* Failure = FindStatus(
        JJAnimReviewChecks::Evaluate(Jump, Profile, Context), TEXT("root_discontinuity"), TEXT("failed"));
    TestNotNull(TEXT("30cm root jump is detected"), Failure);
    if (Failure)
    {
        TestEqual(TEXT("Jump is reported at frame 8"), Failure->StartFrame, 8);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewLoopMismatchTest,
    "JJAnimReviewKit.Checks.LoopMismatch", AnimR1R4TestFlags)

bool FJJAnimReviewLoopMismatchTest::RunTest(const FString& Parameters)
{
    const FJJReviewProfileSettings Profile = OnlyCheck(TEXT("loop_mismatch"), 2.0);
    FJJReviewCheckContext Looping;
    Looping.bLooping = true;

    const FJJReviewPoseData Matching = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.Hand.X = FMath::Sin(Frame / 9.0 * 2.0 * PI) * 10.0;
    });
    TestEqual(TEXT("Matching ends pass"),
        CountStatus(JJAnimReviewChecks::Evaluate(Matching, Profile, Looping), TEXT("loop_mismatch"), TEXT("failed")), 0);

    const FJJReviewPoseData Mismatch = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.Hand.X = Frame * 1.0;
    });
    const FJJReviewCheckResult* Failure = FindStatus(
        JJAnimReviewChecks::Evaluate(Mismatch, Profile, Looping), TEXT("loop_mismatch"), TEXT("failed"));
    TestNotNull(TEXT("9cm seam is detected"), Failure);
    if (Failure)
    {
        TestEqual(TEXT("Seam names the hand"), Failure->Bone, FName(TEXT("hand_r")));
    }

    const FJJReviewCheckContext NotLooping;
    TestEqual(TEXT("Non-looping asset skips the check"),
        CountStatus(JJAnimReviewChecks::Evaluate(Mismatch, Profile, NotLooping), TEXT("loop_mismatch"), TEXT("skipped")), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewSocketDriftTest,
    "JJAnimReviewKit.Checks.SocketDrift", AnimR1R4TestFlags)

bool FJJAnimReviewSocketDriftTest::RunTest(const FString& Parameters)
{
    FJJReviewProfileSettings Profile = OnlyCheck(TEXT("socket_drift"), 2.0);
    FJJReviewSocketBonePair Pair;
    Pair.Socket = TEXT("weapon_r");
    Pair.Bone = TEXT("hand_r");
    Profile.SocketBonePairs = { Pair };

    FJJReviewCheckContext Context;
    FJJReviewSocket Socket;
    Socket.Socket = TEXT("weapon_r");
    Socket.Parent = TEXT("root");
    Socket.Target = TEXT("hand_r");
    Context.Sockets = { Socket };

    const FJJReviewPoseData Fixed = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.Root.X = Frame * 2.0;
        State.Hand.X = Frame * 2.0;
    });
    TestEqual(TEXT("Hand moving with its socket parent passes"),
        CountStatus(JJAnimReviewChecks::Evaluate(Fixed, Profile, Context), TEXT("socket_drift"), TEXT("passed")), 1);

    // Hand moves away from the socket parent (root) mostly along the root→hand line: ~0.96cm per frame.
    const FJJReviewPoseData Drifting = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.Hand.Z = 100.0 + Frame * 1.0;
    });
    const FJJReviewCheckResult* Failure = FindStatus(
        JJAnimReviewChecks::Evaluate(Drifting, Profile, Context), TEXT("socket_drift"), TEXT("failed"));
    TestNotNull(TEXT("Hand drifting from the socket is detected"), Failure);
    if (Failure)
    {
        TestEqual(TEXT("Drift names the socket"), Failure->Bone, FName(TEXT("weapon_r")));
    }

    const FJJReviewCheckContext Unresolved;
    TestEqual(TEXT("Unresolved socket binding warns"),
        CountStatus(JJAnimReviewChecks::Evaluate(Fixed, Profile, Unresolved), TEXT("socket_drift"), TEXT("warning")), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewJointLimitsTest,
    "JJAnimReviewKit.Checks.JointLimits", AnimR1R4TestFlags)

bool FJJAnimReviewJointLimitsTest::RunTest(const FString& Parameters)
{
    const FJJReviewProfileSettings Profile = OnlyCheck(TEXT("joint_limits"), 5.0);

    // Hand twist (roll around X) limited to 10 degrees, swings free.
    FJJReviewCheckContext Context;
    FJJReviewJoint Joint;
    Joint.Child = TEXT("hand_r");
    Joint.Parent = TEXT("root");
    Joint.LimitsDegrees = FVector(10.0, 0.0, 0.0);
    Joint.Motion = FIntVector(1, 0, 0);
    Context.Joints = { Joint };

    const FJJReviewPoseData Within = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.HandRotation = FRotator(0.0, 0.0, Frame * 1.0);
    });
    TestEqual(TEXT("Twist within limit + tolerance passes"),
        CountStatus(JJAnimReviewChecks::Evaluate(Within, Profile, Context), TEXT("joint_limits"), TEXT("failed")), 0);

    const FJJReviewPoseData Over = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.HandRotation = FRotator(0.0, 0.0, Frame * 4.0);
    });
    const FJJReviewCheckResult* Failure = FindStatus(
        JJAnimReviewChecks::Evaluate(Over, Profile, Context), TEXT("joint_limits"), TEXT("failed"));
    TestNotNull(TEXT("Twist of 36 degrees over a 10 degree limit is detected"), Failure);
    if (Failure)
    {
        TestTrue(TEXT("Excess is about 26 degrees"), FMath::IsNearlyEqual(Failure->Measured, 26.0, 0.5));
    }

    // A free swing axis must not be judged even when it moves a lot.
    const FJJReviewPoseData FreeSwing = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.HandRotation = FRotator(0.0, Frame * 8.0, 0.0);
    });
    TestEqual(TEXT("Large motion on a free axis passes"),
        CountStatus(JJAnimReviewChecks::Evaluate(FreeSwing, Profile, Context), TEXT("joint_limits"), TEXT("failed")), 0);

    // Review finding M (R1~R4 review): a Locked axis is a ragdoll simplification, not an animation limit.
    // Expected after the fix: small natural motion on a Locked axis is not a failure. Fails on the current code (Bound = 0).
    FJJReviewCheckContext LockedTwist;
    FJJReviewJoint Locked = Joint;
    Locked.LimitsDegrees = FVector::ZeroVector;
    Locked.Motion = FIntVector(2, 0, 0);
    LockedTwist.Joints = { Locked };
    const FJJReviewPoseData NaturalTwist = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.HandRotation = FRotator(0.0, 0.0, Frame * 1.5);
    });
    TestEqual(TEXT("Natural 13.5 degree twist on a Locked axis is not a failure"),
        CountStatus(JJAnimReviewChecks::Evaluate(NaturalTwist, Profile, LockedTwist), TEXT("joint_limits"), TEXT("failed")), 0);

    const FJJReviewCheckContext NoPhysics;
    TestEqual(TEXT("No physics constraints skips the check"),
        CountStatus(JJAnimReviewChecks::Evaluate(Over, Profile, NoPhysics), TEXT("joint_limits"), TEXT("skipped")), 1);
    return true;
}

// R1~R4-fix re-review: two limited swing axes exceeding on the same frames must merge into one range per axis,
// not one row per frame (the Ter walk loop produced 469 omitted per-frame warnings).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewJointLimitsMergeTest,
    "JJAnimReviewKit.Checks.JointLimitsMergePerAxis", AnimR1R4TestFlags)

bool FJJAnimReviewJointLimitsMergeTest::RunTest(const FString& Parameters)
{
    const FJJReviewProfileSettings Profile = OnlyCheck(TEXT("joint_limits"), 5.0);
    FJJReviewCheckContext Context;
    FJJReviewJoint Joint;
    Joint.Child = TEXT("hand_r");
    Joint.Parent = TEXT("root");
    Joint.LimitsDegrees = FVector(0.0, 5.0, 5.0);
    Joint.Motion = FIntVector(0, 1, 1);
    Context.Joints = { Joint };

    // Both swings exceed by a varying amount on every frame.
    const FJJReviewPoseData Both = MakePose(10, [](int32 Frame, FFrameState& State)
    {
        State.HandRotation = FRotator(20.0 + Frame, 20.0 + Frame, 0.0);
    });
    const TArray<FJJReviewCheckResult> Results = JJAnimReviewChecks::Evaluate(Both, Profile, Context);
    const int32 Failed = CountStatus(Results, TEXT("joint_limits"), TEXT("failed"));
    // Both swing axes exceed on every frame: exactly one range per axis (axes must not be merged together).
    TestEqual(TEXT("Exceeding swing axes produce exactly one range per axis"), Failed, 2);
    for (const FJJReviewCheckResult& Result : Results)
    {
        if (Result.Check == TEXT("joint_limits") && Result.Status == TEXT("failed"))
        {
            TestEqual(TEXT("Merged range starts at the first frame"), Result.StartFrame, 0);
            TestEqual(TEXT("Merged range ends at the last frame"), Result.EndFrame, 9);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewCleanAndInputTest,
    "JJAnimReviewKit.Checks.CleanDataAndInvalidInput", AnimR1R4TestFlags)

bool FJJAnimReviewCleanAndInputTest::RunTest(const FString& Parameters)
{
    // Default profile (all seven checks) on clean data: no failures.
    FJJReviewProfileSettings Profile;
    Profile.FootBones = { FName(TEXT("foot_l")) };
    Profile.PenetrationBones = { FName(TEXT("hand_r")) };
    FJJReviewCheckContext Context;
    Context.bLooping = true;

    const FJJReviewPoseData Clean = MakePose(30, [](int32 Frame, FFrameState& State)
    {
        State.Hand.X = FMath::Sin(Frame / 29.0 * 2.0 * PI) * 5.0;
        State.RootMotion.X = Frame * 2.0;
    });
    const TArray<FJJReviewCheckResult> Results = JJAnimReviewChecks::Evaluate(Clean, Profile, Context);
    int32 Failed = 0;
    for (const FJJReviewCheckResult& Result : Results)
    {
        if (Result.Status == TEXT("failed"))
        {
            ++Failed;
            AddInfo(FString::Printf(TEXT("Unexpected failure %s %s"), *Result.Check, *Result.Bone.ToString()));
        }
    }
    TestEqual(TEXT("Clean data has no failures (no false positives)"), Failed, 0);
    TestTrue(TEXT("Default profile runs all seven checks"), Profile.Checks.Num() >= 7);

    FJJReviewPoseData Broken = Clean;
    Broken.Samples[3].ComponentTransforms.Pop();
    const TArray<FJJReviewCheckResult> BrokenResults = JJAnimReviewChecks::Evaluate(Broken, Profile, Context);
    TestEqual(TEXT("Invalid pose yields one input failure"), BrokenResults.Num(), 1);
    if (BrokenResults.Num() == 1)
    {
        TestEqual(TEXT("Input failure is labelled"), BrokenResults[0].Check, FString(TEXT("input")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewSheetTest,
    "JJAnimReviewKit.Sheet.FrameSelectionAndLayout", AnimR1R4TestFlags)

bool FJJAnimReviewSheetTest::RunTest(const FString& Parameters)
{
    FJJReviewPoseData Pose = MakePose(61, [](int32, FFrameState&)
    {
    });
    Pose.NotifyTimesSeconds = { 0.5 };

    const TArray<int32> Frames = JJAnimReviewSheet::SelectFrames(Pose, 6, {});
    TestEqual(TEXT("Six frames are selected"), Frames.Num(), 6);
    TestTrue(TEXT("Start and end are included"), Frames.Contains(0) && Frames.Contains(60));
    TestTrue(TEXT("Notify frame (0.5s at 30fps = 15) is included"), Frames.Contains(15));
    for (int32 Index = 1; Index < Frames.Num(); ++Index)
    {
        TestTrue(TEXT("Frames strictly increase"), Frames[Index] > Frames[Index - 1]);
    }

    const TArray<int32> Explicit = JJAnimReviewSheet::SelectFrames(Pose, 6, { 50, 5, 500, 5 });
    TestTrue(TEXT("Explicit frames are clamped, de-duplicated and sorted"), Explicit == TArray<int32>({ 5, 50, 60 }));

    const FJJReviewPoseData Short = MakePose(3, [](int32, FFrameState&)
    {
    });
    TestEqual(TEXT("Short clip never repeats frames"), JJAnimReviewSheet::SelectFrames(Short, 6, {}).Num(), 3);

    const FJJReviewSheetLayout Default = JJAnimReviewSheet::Layout(6, 4, 256);
    TestEqual(TEXT("Columns are frames"), Default.Columns, 6);
    TestEqual(TEXT("Rows are angles"), Default.Rows, 4);
    TestTrue(TEXT("Long side stays within 1568px"), FMath::Max(Default.Size.X, Default.Size.Y) <= 1568);

    const FJJReviewSheetLayout Wide = JJAnimReviewSheet::Layout(24, 8, 512);
    TestTrue(TEXT("Maximum grid stays within 1568px"), FMath::Max(Wide.Size.X, Wide.Size.Y) <= 1568);
    TestEqual(TEXT("Too many frames is rejected"), JJAnimReviewSheet::Layout(25, 4, 256).Cell, 0);
    TestEqual(TEXT("No angles is rejected"), JJAnimReviewSheet::Layout(6, 0, 256).Cell, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewEditingTest,
    "JJAnimReviewKit.Editing.AnnotationOperations", AnimR1R4TestFlags)

bool FJJAnimReviewEditingTest::RunTest(const FString& Parameters)
{
    const FJJReviewPoseData Pose = MakePose(3, [](int32, FFrameState&)
    {
    });
    FString ContentId;
    FString Error;
    JJAnimReviewModel::ComputeContentId(Pose, ContentId, Error);

    FJJReviewBundle Bundle;
    Bundle.Asset = TEXT("/Game/ClaudeTest/Fake.Fake");
    Bundle.TakeNumber = 2;
    Bundle.Take = TEXT("take_000002");
    Bundle.ContentId = ContentId;
    Bundle.CreatedUtc = TEXT("2026-11-07T00:00:00Z");
    Bundle.UpdatedUtc = Bundle.CreatedUtc;

    FJJReviewAnnotation Note;
    Note.Frame = 1;
    Note.Bone = TEXT("hand_r");
    Note.Text = TEXT("raise the hand");
    Note.Direction = TEXT("+Z");
    Note.Amount = TEXT("little");
    TestTrue(TEXT("Add succeeds"), JJAnimReviewEditing::Apply(Bundle, Note, TEXT("add"), Error));
    if (!TestEqual(TEXT("One annotation added"), Bundle.Annotations.Num(), 1))
    {
        return false;
    }

    const FJJReviewAnnotation Added = Bundle.Annotations[0];
    TestFalse(TEXT("Added annotation gets an id"), Added.AnnotationId.IsEmpty());
    TestEqual(TEXT("Added annotation is authored by the current take"), Added.AuthoredTake, Bundle.Take);
    TestEqual(TEXT("Added annotation is open"), Added.Status, FString(TEXT("open")));

    FJJReviewAnnotation Updated = Added;
    Updated.Text = TEXT("raise the hand more");
    Updated.Amount = TEXT("much");
    TestTrue(TEXT("Update succeeds"), JJAnimReviewEditing::Apply(Bundle, Updated, TEXT("update"), Error));
    TestEqual(TEXT("Update keeps the id for a note of this take"), Bundle.Annotations[0].AnnotationId, Added.AnnotationId);
    TestEqual(TEXT("Update changes the text"), Bundle.Annotations[0].Text, Updated.Text);

    TestTrue(TEXT("Resolve succeeds"), JJAnimReviewEditing::Apply(Bundle, Bundle.Annotations[0], TEXT("resolve"), Error));
    TestEqual(TEXT("Resolve marks resolved"), Bundle.Annotations[0].Status, FString(TEXT("resolved")));
    TestTrue(TEXT("Reopen succeeds"), JJAnimReviewEditing::Apply(Bundle, Bundle.Annotations[0], TEXT("reopen"), Error));
    TestEqual(TEXT("Reopen marks open"), Bundle.Annotations[0].Status, FString(TEXT("open")));

    FJJReviewAnnotation BadDirection = Note;
    BadDirection.Direction = TEXT("up");
    TestFalse(TEXT("Invalid direction is rejected"), JJAnimReviewEditing::Apply(Bundle, BadDirection, TEXT("add"), Error));

    // Editing a carried note re-authors it in the current take (D15: origin follows content).
    FJJReviewAnnotation Carried;
    Carried.AnnotationId = TEXT("carried-1");
    Carried.AuthoredTake = TEXT("take_000001");
    Carried.AuthoredContentId = TEXT("pose_md5_v2:old");
    Carried.bFromPreviousVersion = true;
    Carried.Frame = 2;
    Carried.Text = TEXT("old note");
    Bundle.Annotations.Add(Carried);
    FJJReviewAnnotation CarriedEdit = Carried;
    CarriedEdit.Text = TEXT("old note, edited");
    TestTrue(TEXT("Editing a carried note succeeds"), JJAnimReviewEditing::Apply(Bundle, CarriedEdit, TEXT("update"), Error));
    const FJJReviewAnnotation* Reauthored = Bundle.Annotations.FindByPredicate([](const FJJReviewAnnotation& Item)
    {
        return Item.Text == TEXT("old note, edited");
    });
    TestNotNull(TEXT("Edited carried note exists"), Reauthored);
    if (Reauthored)
    {
        TestEqual(TEXT("Edited carried note is authored by the current take"), Reauthored->AuthoredTake, Bundle.Take);
        TestFalse(TEXT("Edited carried note is no longer from a previous version"), Reauthored->bFromPreviousVersion);
    }

    const int32 Before = Bundle.Annotations.Num();
    TestTrue(TEXT("Delete succeeds"), JJAnimReviewEditing::Apply(Bundle, Bundle.Annotations[0], TEXT("delete"), Error));
    TestEqual(TEXT("Delete removes one"), Bundle.Annotations.Num(), Before - 1);
    TestFalse(TEXT("Deleting again reports a stale tab"), JJAnimReviewEditing::Apply(Bundle, Added, TEXT("delete"), Error));
    TestFalse(TEXT("Unknown operation is rejected"), JJAnimReviewEditing::Apply(Bundle, Bundle.Annotations[0], TEXT("rename"), Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewPoseInstructionTest,
    "JJAnimReviewKit.Editing.PoseInstructions", AnimR1R4TestFlags)

bool FJJAnimReviewPoseInstructionTest::RunTest(const FString& Parameters)
{
    const FJJReviewPoseData Original = MakePose(1, [](int32, FFrameState&)
    {
    });

    // Preview: hand rotated 20 degrees around Z, foot moved by 0.05cm (below the 0.1cm threshold).
    FJJReviewPoseSample Preview = Original.Samples[0];
    Preview.SourceFrame = 7;
    Preview.ComponentTransforms[HandBone] = FJJReviewTransform::FromTransform(
        FTransform(FRotator(0.0, 20.0, 0.0), FVector(0.0, -30.0, 100.0)));
    Preview.ComponentTransforms[FootBone] = FJJReviewTransform::FromTransform(
        FTransform(FQuat::Identity, FVector(0.05, 10.0, 0.0)));

    FString Signed;
    const TArray<FJJReviewPoseDifference> Differences = JJAnimReviewEditing::PoseInstructions(Original, Preview, Signed);
    TestEqual(TEXT("Only the hand change is kept"), Differences.Num(), 1);
    if (Differences.Num() == 1)
    {
        TestEqual(TEXT("Difference names the hand"), Differences[0].Bone, FName(TEXT("hand_r")));
        TestEqual(TEXT("Difference is at the preview frame"), Differences[0].Frame, 7);
        TestTrue(TEXT("Rotation is about 20 degrees"), FMath::IsNearlyEqual(Differences[0].RotationDegrees, 20.0, 0.1));
    }
    TestTrue(TEXT("Signed instruction text mentions the hand"), Signed.Contains(TEXT("hand_r")));

    // A parent rotation moves children in component space but not in local space.
    FJJReviewPoseData Chain = Original;
    Chain.ParentIndices = { INDEX_NONE, RootBone, FootBone };
    FJJReviewPoseSample ChainPreview = Chain.Samples[0];
    const FTransform RootDelta(FRotator(0.0, 30.0, 0.0));
    for (int32 Bone = 0; Bone < 3; ++Bone)
    {
        ChainPreview.ComponentTransforms[Bone] = FJJReviewTransform::FromTransform(
            Chain.Samples[0].ComponentTransforms[Bone].ToTransform() * RootDelta);
    }
    const TArray<FJJReviewPoseDifference> ChainDifferences = JJAnimReviewEditing::PoseInstructions(Chain, ChainPreview, Signed);
    TestEqual(TEXT("Rotating the root reports only the root"), ChainDifferences.Num(), 1);

    FJJReviewPoseSample WrongShape = Preview;
    WrongShape.ComponentTransforms.Pop();
    TestEqual(TEXT("Mismatched bone count yields nothing"), JJAnimReviewEditing::PoseInstructions(Original, WrongShape, Signed).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewComparisonTest,
    "JJAnimReviewKit.Comparison.TakeDifference", AnimR1R4TestFlags)

bool FJJAnimReviewComparisonTest::RunTest(const FString& Parameters)
{
    const FJJReviewPoseData Previous = MakePose(20, [](int32 Frame, FFrameState& State)
    {
        State.Hand.X = Frame * 1.0;
    });

    const FJJReviewComparison Same = JJAnimReviewComparison::Compare(Previous, Previous);
    TestEqual(TEXT("Identical takes have no maxima"), Same.Maxima.Num(), 0);
    TestEqual(TEXT("Identical takes have no ranges"), Same.Ranges.Num(), 0);

    // Hand differs by 5cm from frame 10 to 14; foot rotates 30 degrees at frame 3 only.
    const FJJReviewPoseData Current = MakePose(20, [](int32 Frame, FFrameState& State)
    {
        State.Hand.X = Frame * 1.0 + ((Frame >= 10 && Frame <= 14) ? 5.0 : 0.0);
        if (Frame == 3)
        {
            State.FootRotation = FRotator(0.0, 30.0, 0.0);
        }
    });
    const FJJReviewComparison Changed = JJAnimReviewComparison::Compare(Current, Previous);
    TestEqual(TEXT("Two bones differ"), Changed.Maxima.Num(), 2);
    if (Changed.Maxima.Num() == 2)
    {
        TestEqual(TEXT("Largest normalized difference first (30deg/0.5 > 5cm/0.1)"), Changed.Maxima[0].Bone, FName(TEXT("foot_l")));
        TestTrue(TEXT("Hand difference is about 5cm"), FMath::IsNearlyEqual(Changed.Maxima[1].TranslationCm, 5.0, 0.01));
    }
    const FJJReviewCheckResult* HandRange = Changed.Ranges.FindByPredicate([](const FJJReviewCheckResult& Range)
    {
        return Range.Bone == FName(TEXT("hand_r"));
    });
    TestNotNull(TEXT("Hand difference range exists"), HandRange);
    if (HandRange)
    {
        TestEqual(TEXT("Hand range starts at 10"), HandRange->StartFrame, 10);
        TestEqual(TEXT("Hand range ends at 14"), HandRange->EndFrame, 14);
    }

    TestEqual(TEXT("TopCount limits maxima"), JJAnimReviewComparison::Compare(Current, Previous, 1).Maxima.Num(), 1);

    FJJReviewPoseData Renamed = Current;
    Renamed.BoneNames[2] = TEXT("hand_l");
    const FJJReviewComparison Skipped = JJAnimReviewComparison::Compare(Renamed, Previous);
    TestEqual(TEXT("Hierarchy change skips comparison"), Skipped.Maxima.Num(), 0);
    TestEqual(TEXT("Hierarchy change warns"), Skipped.Warnings.Num(), 1);

    const FTransform Mid = JJAnimReviewComparison::AtTime(Previous, HandBone, 2.5 / 30.0);
    TestTrue(TEXT("AtTime interpolates between frames"), FMath::IsNearlyEqual(Mid.GetLocation().X, 2.5, 0.001));
    return true;
}

#endif
