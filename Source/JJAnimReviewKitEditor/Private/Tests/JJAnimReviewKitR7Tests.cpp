#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "JJAnimReviewEditing.h"
#include "JJAnimReviewNotes.h"

namespace JJAnimReviewKitR7TestsPrivate
{
    constexpr EAutomationTestFlags R7TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    FJJReviewPoseDifference R7Difference(const TCHAR* Bone, double Degrees)
    {
        FJJReviewPoseDifference Difference;
        Difference.Bone = FName(Bone);
        Difference.RotationDegrees = Degrees;
        return Difference;
    }

    // root -> spine -> hand_r, plus a virtual bone under spine.
    FJJReviewPoseData R7Pose()
    {
        FJJReviewPoseData Pose;
        Pose.BoneNames = { FName(TEXT("root")), FName(TEXT("spine_01")), FName(TEXT("hand_r")), FName(TEXT("VB hand_r_prop")) };
        Pose.ParentIndices = { INDEX_NONE, 0, 1, 1 };
        FJJReviewPoseSample Sample;
        Sample.SourceFrame = 4;
        Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(FTransform::Identity));
        Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(FTransform(FVector(0.0, 0.0, 100.0))));
        Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(FTransform(FVector(0.0, -30.0, 130.0))));
        Sample.ComponentTransforms.Add(FJJReviewTransform::FromTransform(FTransform(FVector(0.0, -40.0, 130.0))));
        Pose.Samples.Add(Sample);
        return Pose;
    }
}

using namespace JJAnimReviewKitR7TestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR7GhostBasisTest,
    "JJAnimReviewKit.R7.GhostBasis", R7TestFlags)

bool FJJAnimReviewR7GhostBasisTest::RunTest(const FString& Parameters)
{
    const FTransform Base(FQuat(FRotator(0.0, 30.0, 0.0)), FVector(100.0, 50.0, 0.0));
    TestTrue(TEXT("No applied root motion keeps the component transform"),
        JJAnimReviewNotes::GhostBasis(Base, FTransform::Identity).Equals(Base, 1.e-4));

    // Persona moved the component by the applied root motion (expressed in the base frame).
    const FTransform Applied(FQuat(FRotator(0.0, 45.0, 0.0)), FVector(80.0, 0.0, 0.0));
    const FTransform Component = Applied * Base;
    const FTransform Basis = JJAnimReviewNotes::GhostBasis(Component, Applied);
    TestTrue(TEXT("Basis removes the applied root motion"), Basis.Equals(Base, 1.e-4));

    // Ghost take with the same root motion as the preview: its root lands where the preview component is.
    const FTransform SameGhostRoot = Applied;
    TestTrue(TEXT("Same root motion overlaps the preview"),
        (SameGhostRoot * Basis).GetLocation().Equals(Component.GetLocation(), 1.e-3));

    // Ghost take that moved 20cm further forward: the difference stays visible, rotated into the base frame.
    const FTransform FurtherGhostRoot(Applied.GetRotation(), Applied.GetTranslation() + FVector(20.0, 0.0, 0.0));
    const FVector Offset = (FurtherGhostRoot * Basis).GetLocation() - Component.GetLocation();
    TestTrue(TEXT("Different root motion keeps its 20cm difference"), FMath::IsNearlyEqual(Offset.Size(), 20.0, 1.e-3));
    TestTrue(TEXT("Difference is expressed in the base frame"),
        Offset.Equals(Base.TransformVectorNoScale(FVector(20.0, 0.0, 0.0)), 1.e-3));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR7FilterTest,
    "JJAnimReviewKit.R7.FilterPoseInstructions", R7TestFlags)

bool FJJAnimReviewR7FilterTest::RunTest(const FString& Parameters)
{
    const TArray<FJJReviewPoseDifference> Candidates = {
        R7Difference(TEXT("upperarm_l"), 18.0),
        R7Difference(TEXT("VB hand_l_prop"), 0.0),
        R7Difference(TEXT("hand_l"), 42.0),
        R7Difference(TEXT("root"), 3.0)
    };
    TestEqual(TEXT("No modified bones gives no instructions"), JJAnimReviewNotes::FilterPoseInstructions(Candidates, {}).Num(), 0);

    const TSet<FName> Modified = { FName(TEXT("hand_l")), FName(TEXT("upperarm_l")), FName(TEXT("not_a_candidate")) };
    const TArray<FJJReviewPoseDifference> Kept = JJAnimReviewNotes::FilterPoseInstructions(Candidates, Modified);
    TestEqual(TEXT("Only modified candidates are kept"), Kept.Num(), 2);
    if (Kept.Num() == 2)
    {
        TestEqual(TEXT("Candidate order is preserved (first)"), Kept[0].Bone, FName(TEXT("upperarm_l")));
        TestEqual(TEXT("Candidate order is preserved (second)"), Kept[1].Bone, FName(TEXT("hand_l")));
        TestEqual(TEXT("Values are copied unchanged"), Kept[1].RotationDegrees, 42.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR7PoseInstructionsModifiedTest,
    "JJAnimReviewKit.R7.PoseInstructionsModifiedOnly", R7TestFlags)

bool FJJAnimReviewR7PoseInstructionsModifiedTest::RunTest(const FString& Parameters)
{
    const FJJReviewPoseData Original = R7Pose();
    FJJReviewPoseSample Preview = Original.Samples[0];
    // User rotated the hand 20 degrees; the virtual bone drifted 0.12cm (post-process/compression noise).
    Preview.ComponentTransforms[2] = FJJReviewTransform::FromTransform(
        FTransform(FQuat(FRotator(0.0, 20.0, 0.0)), FVector(0.0, -30.0, 130.0)));
    Preview.ComponentTransforms[3] = FJJReviewTransform::FromTransform(FTransform(FVector(0.12, -40.0, 130.0)));

    FString AllText;
    const TArray<FJJReviewPoseDifference> All = JJAnimReviewEditing::PoseInstructions(Original, Preview, AllText);
    TestEqual(TEXT("Without a modified set, the noise is reported too (old behavior)"), All.Num(), 2);

    const TSet<FName> Modified = { FName(TEXT("hand_r")) };
    FString Text;
    const TArray<FJJReviewPoseDifference> Filtered = JJAnimReviewEditing::PoseInstructions(Original, Preview, Text, 0.1, 0.5, &Modified);
    TestEqual(TEXT("Only the modified hand is reported"), Filtered.Num(), 1);
    if (Filtered.Num() == 1)
    {
        TestEqual(TEXT("Reported bone is the hand"), Filtered[0].Bone, FName(TEXT("hand_r")));
        TestEqual(TEXT("Frame comes from the preview sample"), Filtered[0].Frame, 4);
    }
    TestTrue(TEXT("Text names the hand"), Text.Contains(TEXT("hand_r")));
    TestFalse(TEXT("Text omits the unmodified virtual bone"), Text.Contains(TEXT("VB hand_r_prop")));

    // Modified bone without an actual difference: nothing to report, text stays empty.
    const TSet<FName> OnlyRoot = { FName(TEXT("root")) };
    FString RootText;
    TestEqual(TEXT("Modified bone with no change reports nothing"),
        JJAnimReviewEditing::PoseInstructions(Original, Preview, RootText, 0.1, 0.5, &OnlyRoot).Num(), 0);
    TestTrue(TEXT("Empty result gives empty text"), RootText.IsEmpty());
    return true;
}

#endif
