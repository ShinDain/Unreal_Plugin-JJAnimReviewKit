#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "JJAnimReviewNotes.h"

namespace JJAnimReviewKitR7Fix2TestsPrivate
{
    constexpr EAutomationTestFlags R7Fix2TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

using namespace JJAnimReviewKitR7Fix2TestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR7Fix2OffsetGhostBasisTest,
    "JJAnimReviewKit.R7Fix2.OffsetGhostBasis", R7Fix2TestFlags)

bool FJJAnimReviewR7Fix2OffsetGhostBasisTest::RunTest(const FString& Parameters)
{
    const FTransform Identity = FTransform::Identity;
    TestTrue(TEXT("Zero spacing keeps the basis"),
        JJAnimReviewNotes::OffsetGhostBasis(Identity, 90.0, 0.0).Equals(Identity, 1.e-4));

    // Forward +X (yaw 0): character right is +Y.
    TestTrue(TEXT("Yaw 0, +100 moves to +Y"),
        JJAnimReviewNotes::OffsetGhostBasis(Identity, 0.0, 100.0).GetLocation().Equals(FVector(0.0, 100.0, 0.0), 1.e-3));
    // Mannequin forward +Y (yaw 90): character right is -X.
    TestTrue(TEXT("Yaw 90, +100 moves to -X"),
        JJAnimReviewNotes::OffsetGhostBasis(Identity, 90.0, 100.0).GetLocation().Equals(FVector(-100.0, 0.0, 0.0), 1.e-3));
    TestTrue(TEXT("Negative spacing moves to the left"),
        JJAnimReviewNotes::OffsetGhostBasis(Identity, 90.0, -50.0).GetLocation().Equals(FVector(50.0, 0.0, 0.0), 1.e-3));

    // The offset is expressed in the basis (component) frame and keeps the basis rotation.
    const FTransform Basis(FQuat(FRotator(0.0, 90.0, 0.0)), FVector(10.0, 20.0, 30.0));
    const FTransform Moved = JJAnimReviewNotes::OffsetGhostBasis(Basis, 90.0, 100.0);
    TestTrue(TEXT("Rotation is unchanged"), Moved.GetRotation().Equals(Basis.GetRotation(), 1.e-4));
    TestTrue(TEXT("Offset follows the basis rotation"),
        Moved.GetLocation().Equals(Basis.GetLocation() + Basis.TransformVector(FVector(-100.0, 0.0, 0.0)), 1.e-3));
    TestTrue(TEXT("Offset distance equals the spacing"),
        FMath::IsNearlyEqual((Moved.GetLocation() - Basis.GetLocation()).Size(), 100.0, 1.e-3));
    return true;
}

#endif
