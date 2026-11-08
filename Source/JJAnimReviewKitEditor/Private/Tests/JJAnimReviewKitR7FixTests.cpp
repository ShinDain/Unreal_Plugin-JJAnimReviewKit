#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "JJAnimReviewNotes.h"

namespace JJAnimReviewKitR7FixTestsPrivate
{
    constexpr EAutomationTestFlags R7FixTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

using namespace JJAnimReviewKitR7FixTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR7FixRootGatesTest,
    "JJAnimReviewKit.R7Fix.RootMotionGates", R7FixTestFlags)

bool FJJAnimReviewR7FixRootGatesTest::RunTest(const FString& Parameters)
{
    // Component moves only when the mode processes root motion, the asset enables it, and no physics blends.
    TestTrue(TEXT("LoopAndReset + enabled applies root motion"), JJAnimReviewNotes::AppliesPreviewRootMotion(false, true, false));
    TestFalse(TEXT("Ignore applies nothing"), JJAnimReviewNotes::AppliesPreviewRootMotion(true, true, false));
    TestFalse(TEXT("Disabled asset applies nothing"), JJAnimReviewNotes::AppliesPreviewRootMotion(false, false, false));
    TestFalse(TEXT("Physics keeps the conservative identity"), JJAnimReviewNotes::AppliesPreviewRootMotion(false, true, true));

    // Ghost root is locked when the preview locks its root but the component does not move.
    TestTrue(TEXT("Ignore + enabled locks the ghost root"), JJAnimReviewNotes::LocksGhostRoot(true, true, false, false));
    TestTrue(TEXT("Force root lock without root motion locks the ghost root"), JJAnimReviewNotes::LocksGhostRoot(false, false, true, false));
    TestTrue(TEXT("Ignore + force root lock locks the ghost root"), JJAnimReviewNotes::LocksGhostRoot(true, false, true, false));
    TestFalse(TEXT("Applied root motion uses the basis, not a lock"), JJAnimReviewNotes::LocksGhostRoot(false, true, false, false));
    TestFalse(TEXT("Applied root motion wins over force lock"), JJAnimReviewNotes::LocksGhostRoot(false, true, true, false));
    TestFalse(TEXT("No root motion and no lock leaves the ghost as sampled"), JJAnimReviewNotes::LocksGhostRoot(true, false, false, false));
    TestFalse(TEXT("Physics skips the lock correction"), JJAnimReviewNotes::LocksGhostRoot(true, true, false, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR7FixRebaseRootTest,
    "JJAnimReviewKit.R7Fix.RebaseRoot", R7FixTestFlags)

bool FJJAnimReviewR7FixRebaseRootTest::RunTest(const FString& Parameters)
{
    // Unlocked sample: the root travelled 50cm and turned 10 degrees; the hand hangs off the root.
    const FTransform RawRoot(FQuat(FRotator(0.0, 10.0, 0.0)), FVector(50.0, 0.0, 0.0));
    const FTransform HandLocal(FQuat(FRotator(0.0, 0.0, 30.0)), FVector(0.0, -30.0, 120.0));
    const FTransform RawHand = HandLocal * RawRoot;
    const FTransform LockedRoot(FVector(0.0, 0.0, 2.0));

    TestTrue(TEXT("Root lands on the locked root"),
        JJAnimReviewNotes::RebaseRoot(RawRoot, RawRoot, LockedRoot).Equals(LockedRoot, 1.e-4));
    const FTransform Hand = JJAnimReviewNotes::RebaseRoot(RawHand, RawRoot, LockedRoot);
    TestTrue(TEXT("Hand keeps its pose relative to the root"), Hand.Equals(HandLocal * LockedRoot, 1.e-4));
    TestFalse(TEXT("Root travel is removed from the hand"), Hand.GetLocation().Equals(RawHand.GetLocation(), 1.0));
    TestTrue(TEXT("Identity raw and locked roots change nothing"),
        JJAnimReviewNotes::RebaseRoot(RawHand, FTransform::Identity, FTransform::Identity).Equals(RawHand, 1.e-4));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR7FixJointMarkersTest,
    "JJAnimReviewKit.R7Fix.JointMarkers", R7FixTestFlags)

bool FJJAnimReviewR7FixJointMarkersTest::RunTest(const FString& Parameters)
{
    const TArray<FName> Bones = {
        TEXT("pelvis"), TEXT("spine_01"), TEXT("hand_l"), TEXT("spring_01"), TEXT("string_01"),
        TEXT("index_01_l"), TEXT("ring_01_r"), TEXT("lowerarm_twist_01_l"), TEXT("thumb_02_r"),
        TEXT("Index_03_L"), TEXT("pinky_metacarpal_l")
    };
    const TArray<bool> Expected = { true, true, true, true, true, false, false, false, false, false, false };
    const TArray<bool> Markers = JJAnimReviewNotes::JointMarkers(Bones);
    TestEqual(TEXT("One marker flag per bone"), Markers.Num(), Bones.Num());
    if (Markers.Num() == Bones.Num())
    {
        for (int32 Index = 0; Index < Bones.Num(); ++Index)
        {
            TestEqual(*FString::Printf(TEXT("Marker for %s"), *Bones[Index].ToString()), Markers[Index], Expected[Index]);
        }
    }
    TestEqual(TEXT("Empty input gives empty output"), JJAnimReviewNotes::JointMarkers({}).Num(), 0);
    return true;
}

#endif
