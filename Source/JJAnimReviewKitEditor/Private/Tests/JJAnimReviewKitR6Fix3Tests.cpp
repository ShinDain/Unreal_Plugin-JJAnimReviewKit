#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "JJAnimReviewNotes.h"

namespace JJAnimReviewKitR6Fix3TestsPrivate
{
    constexpr EAutomationTestFlags R6Fix3TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    FJJReviewBoneGroup Fix3Group(const FString& Name, const TArray<FName>& Bones)
    {
        FJJReviewBoneGroup Group;
        Group.Name = Name;
        Group.Bones = Bones;
        return Group;
    }

    FJJReviewAnnotation Fix3Note(const FString& Id, int32 Start, int32 End, const FString& Status)
    {
        FJJReviewAnnotation Note;
        Note.AnnotationId = Id;
        Note.Frame = Start;
        Note.EndFrame = End;
        Note.Status = Status;
        return Note;
    }

    const FJJReviewBoneGroup* Fix3Find(const TArray<FJJReviewBoneGroup>& Groups, const FString& Name)
    {
        return Groups.FindByPredicate([&Name](const FJJReviewBoneGroup& Group)
        {
            return Group.Name == Name;
        });
    }

    bool Fix3SameNames(const TArray<FJJReviewBoneGroup>& A, const TArray<FJJReviewBoneGroup>& B)
    {
        if (A.Num() != B.Num())
        {
            return false;
        }
        for (int32 Index = 0; Index < A.Num(); ++Index)
        {
            if (A[Index].Name != B[Index].Name || A[Index].Bones != B[Index].Bones)
            {
                return false;
            }
        }
        return true;
    }
}

using namespace JJAnimReviewKitR6Fix3TestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6Fix3GroupSaveTest,
    "JJAnimReviewKit.R6Fix3.ChangeGroupSave", R6Fix3TestFlags)

bool FJJAnimReviewR6Fix3GroupSaveTest::RunTest(const FString& Parameters)
{
    const FName Clavicle(TEXT("clavicle_l"));
    const FName Hand(TEXT("hand_l"));
    const TArray<FJJReviewBoneGroup> Template = { Fix3Group(TEXT("왼팔"), { Clavicle, Hand }) };
    const TArray<FJJReviewBoneGroup> User;
    const TArray<FJJReviewBoneGroup> Resolved = Template;

    FJJReviewBoneGroup Requested = Fix3Group(TEXT("  검  "), { Hand });
    FJJReviewGroupChange Free = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT(""), Requested, EJJReviewGroupOperation::Save);
    TestTrue(TEXT("Save succeeds"), Free.Error.IsEmpty());
    TestEqual(TEXT("Free name is trimmed"), Free.FinalName, FString(TEXT("검")));
    TestTrue(TEXT("Saved group is added"), Fix3Find(Free.UserGroups, TEXT("검")) != nullptr);
    TestFalse(TEXT("Save is not a no-op"), Free.bNoOp);

    Requested.Name = TEXT("왼팔");
    FJJReviewGroupChange Duplicate = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT(""), Requested, EJJReviewGroupOperation::Save);
    TestEqual(TEXT("Built-in name gets a number"), Duplicate.FinalName, FString(TEXT("왼팔 2")));
    const FJJReviewBoneGroup* Saved = Fix3Find(Duplicate.UserGroups, TEXT("왼팔 2"));
    TestTrue(TEXT("Numbered group keeps requested bones"), Saved && Saved->Bones == TArray<FName>({ Hand }));
    TestTrue(TEXT("Built-in is not hidden by a save"), Fix3Find(Duplicate.UserGroups, TEXT("왼팔")) == nullptr);

    // A hidden built-in (tombstone in User) still reserves its name.
    const TArray<FJJReviewBoneGroup> Hidden = { Fix3Group(TEXT("왼팔"), {}) };
    FJJReviewGroupChange AfterHide = JJAnimReviewNotes::ChangeGroup(Hidden, Template, {}, TEXT(""), Requested, EJJReviewGroupOperation::Save);
    TestEqual(TEXT("Hidden built-in name stays reserved"), AfterHide.FinalName, FString(TEXT("왼팔 2")));

    FJJReviewBoneGroup Empty = Fix3Group(TEXT("빈 그룹"), {});
    FJJReviewGroupChange NoBones = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT(""), Empty, EJJReviewGroupOperation::Save);
    TestFalse(TEXT("Save without bones fails"), NoBones.Error.IsEmpty());
    TestTrue(TEXT("Failed save leaves groups unchanged"), Fix3SameNames(NoBones.UserGroups, User));

    FJJReviewBoneGroup Blank = Fix3Group(TEXT("   "), { Hand });
    FJJReviewGroupChange NoName = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT(""), Blank, EJJReviewGroupOperation::Save);
    TestFalse(TEXT("Save with a blank name fails"), NoName.Error.IsEmpty());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6Fix3GroupRenameTest,
    "JJAnimReviewKit.R6Fix3.ChangeGroupRename", R6Fix3TestFlags)

bool FJJAnimReviewR6Fix3GroupRenameTest::RunTest(const FString& Parameters)
{
    const FName Clavicle(TEXT("clavicle_l"));
    const FName Hand(TEXT("hand_l"));
    const FName Sword(TEXT("weapon_r"));
    const TArray<FJJReviewBoneGroup> Template = { Fix3Group(TEXT("왼팔"), { Clavicle, Hand }) };
    const TArray<FJJReviewBoneGroup> User = { Fix3Group(TEXT("검"), { Sword }) };
    TArray<FJJReviewBoneGroup> Resolved = Template;
    Resolved.Append(User);

    // Same name (after trimming) does nothing and is not renumbered to "검 2".
    FJJReviewBoneGroup Same = Fix3Group(TEXT(" 검 "), {});
    FJJReviewGroupChange NoOp = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT("검"), Same, EJJReviewGroupOperation::Rename);
    TestTrue(TEXT("Same-name rename is a no-op"), NoOp.bNoOp);
    TestTrue(TEXT("No-op keeps groups"), Fix3SameNames(NoOp.UserGroups, User));

    // Renaming a user group excludes its own name from reservation and keeps its bones.
    FJJReviewBoneGroup NewName = Fix3Group(TEXT("무기"), {});
    FJJReviewGroupChange Renamed = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT("검"), NewName, EJJReviewGroupOperation::Rename);
    TestTrue(TEXT("User rename succeeds"), Renamed.Error.IsEmpty());
    TestEqual(TEXT("User rename final name"), Renamed.FinalName, FString(TEXT("무기")));
    const FJJReviewBoneGroup* Weapon = Fix3Find(Renamed.UserGroups, TEXT("무기"));
    TestTrue(TEXT("Renamed group keeps the old bones"), Weapon && Weapon->Bones == TArray<FName>({ Sword }));
    TestTrue(TEXT("Old user name is gone with no tombstone"), Fix3Find(Renamed.UserGroups, TEXT("검")) == nullptr);

    // Renaming to a built-in name is numbered.
    FJJReviewBoneGroup ToBuiltIn = Fix3Group(TEXT("왼팔"), {});
    FJJReviewGroupChange Collide = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT("검"), ToBuiltIn, EJJReviewGroupOperation::Rename);
    TestEqual(TEXT("Rename onto a built-in name is numbered"), Collide.FinalName, FString(TEXT("왼팔 2")));

    // Renaming a built-in hides it with a tombstone and adds the new group with its bones.
    FJJReviewBoneGroup Arm = Fix3Group(TEXT("팔"), {});
    FJJReviewGroupChange BuiltIn = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT("왼팔"), Arm, EJJReviewGroupOperation::Rename);
    const FJJReviewBoneGroup* Tombstone = Fix3Find(BuiltIn.UserGroups, TEXT("왼팔"));
    TestTrue(TEXT("Built-in rename leaves a tombstone"), Tombstone && Tombstone->Bones.IsEmpty());
    const FJJReviewBoneGroup* NewArm = Fix3Find(BuiltIn.UserGroups, TEXT("팔"));
    TestTrue(TEXT("Built-in rename keeps the built-in bones"), NewArm && NewArm->Bones == TArray<FName>({ Clavicle, Hand }));

    FJJReviewGroupChange Missing = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT("없음"), Arm, EJJReviewGroupOperation::Rename);
    TestFalse(TEXT("Rename without a target fails"), Missing.Error.IsEmpty());
    TestTrue(TEXT("Failed rename leaves groups unchanged"), Fix3SameNames(Missing.UserGroups, User));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6Fix3GroupDeleteTest,
    "JJAnimReviewKit.R6Fix3.ChangeGroupDelete", R6Fix3TestFlags)

bool FJJAnimReviewR6Fix3GroupDeleteTest::RunTest(const FString& Parameters)
{
    const FName Hand(TEXT("hand_l"));
    const FName Sword(TEXT("weapon_r"));
    const TArray<FJJReviewBoneGroup> Template = { Fix3Group(TEXT("왼팔"), { Hand }) };
    const TArray<FJJReviewBoneGroup> User = { Fix3Group(TEXT("검"), { Sword }) };
    TArray<FJJReviewBoneGroup> Resolved = Template;
    Resolved.Append(User);
    const FJJReviewBoneGroup Unused;

    FJJReviewGroupChange UserDelete = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT("검"), Unused, EJJReviewGroupOperation::Delete);
    TestTrue(TEXT("User delete succeeds"), UserDelete.Error.IsEmpty());
    TestEqual(TEXT("User delete removes the entry entirely"), UserDelete.UserGroups.Num(), 0);

    // After deleting, the same name can be saved again without a number.
    FJJReviewBoneGroup Again = Fix3Group(TEXT("검"), { Sword });
    FJJReviewGroupChange Reuse = JJAnimReviewNotes::ChangeGroup(UserDelete.UserGroups, Template, Template, TEXT(""), Again, EJJReviewGroupOperation::Save);
    TestEqual(TEXT("Deleted user name is reusable"), Reuse.FinalName, FString(TEXT("검")));

    FJJReviewGroupChange BuiltInDelete = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT("왼팔"), Unused, EJJReviewGroupOperation::Delete);
    const FJJReviewBoneGroup* Tombstone = Fix3Find(BuiltInDelete.UserGroups, TEXT("왼팔"));
    TestTrue(TEXT("Built-in delete leaves an empty tombstone"), Tombstone && Tombstone->Bones.IsEmpty());
    TestTrue(TEXT("Built-in delete keeps other user groups"), Fix3Find(BuiltInDelete.UserGroups, TEXT("검")) != nullptr);

    FJJReviewGroupChange Missing = JJAnimReviewNotes::ChangeGroup(User, Template, Resolved, TEXT(""), Unused, EJJReviewGroupOperation::Delete);
    TestFalse(TEXT("Delete without a target fails"), Missing.Error.IsEmpty());
    TestTrue(TEXT("Failed delete leaves groups unchanged"), Fix3SameNames(Missing.UserGroups, User));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6Fix3VisibilityBoundsTest,
    "JJAnimReviewKit.R6Fix3.VisibilityBounds", R6Fix3TestFlags)

bool FJJAnimReviewR6Fix3VisibilityBoundsTest::RunTest(const FString& Parameters)
{
    // Open note on frames 10..12 is visible on 8..14: enter at 8, leave at 15 (half-open).
    const TArray<FJJReviewAnnotation> Notes = {
        Fix3Note(TEXT("open"), 10, 12, TEXT("open")),
        Fix3Note(TEXT("done"), 30, INDEX_NONE, TEXT("resolved"))
    };

    const FJJReviewVisibilityBounds Before = JJAnimReviewNotes::VisibilityBounds(Notes, 5, TEXT(""));
    TestEqual(TEXT("Before: no lower boundary"), Before.FromFrame, MIN_int64);
    TestEqual(TEXT("Before: next boundary is the enter frame"), Before.UntilFrame, static_cast<int64>(8));

    const FJJReviewVisibilityBounds AtEnter = JJAnimReviewNotes::VisibilityBounds(Notes, 8, TEXT(""));
    TestEqual(TEXT("Enter frame belongs to the visible span"), AtEnter.FromFrame, static_cast<int64>(8));
    TestEqual(TEXT("Visible span ends at end+3"), AtEnter.UntilFrame, static_cast<int64>(15));

    const FJJReviewVisibilityBounds Last = JJAnimReviewNotes::VisibilityBounds(Notes, 14, TEXT(""));
    TestEqual(TEXT("Last visible frame is still inside"), Last.UntilFrame, static_cast<int64>(15));

    const FJJReviewVisibilityBounds After = JJAnimReviewNotes::VisibilityBounds(Notes, 15, TEXT(""));
    TestEqual(TEXT("Leave frame starts the next span"), After.FromFrame, static_cast<int64>(15));
    TestEqual(TEXT("Resolved unselected note adds no boundary"), After.UntilFrame, MAX_int64);

    // Same frame, but the resolved note is selected: single frame 30 (EndFrame = INDEX_NONE) is visible on 28..32,
    // so it adds boundaries 28 and 33.
    const FJJReviewVisibilityBounds Selected = JJAnimReviewNotes::VisibilityBounds(Notes, 15, TEXT("done"));
    TestEqual(TEXT("Selected resolved note adds its enter boundary"), Selected.UntilFrame, static_cast<int64>(28));
    const FJJReviewVisibilityBounds InSelected = JJAnimReviewNotes::VisibilityBounds(Notes, 29, TEXT("done"));
    TestEqual(TEXT("Inside selected span: lower boundary"), InSelected.FromFrame, static_cast<int64>(28));
    TestEqual(TEXT("Inside selected span: single frame end uses Frame"), InSelected.UntilFrame, static_cast<int64>(33));

    // Cache correctness: every frame inside the same bounds gives the same visible-note index set.
    // "swap" enters on frame 15 exactly when "open" leaves, so the count stays 1 but the set changes.
    TArray<FJJReviewAnnotation> CacheNotes = Notes;
    CacheNotes.Add(Fix3Note(TEXT("swap"), 17, 20, TEXT("open")));
    auto VisibleIndices = [&CacheNotes](int32 Frame)
    {
        TArray<int32> Indices;
        for (const FJJReviewVisibleNote& Visible : JJAnimReviewNotes::VisibleNotes(CacheNotes, Frame, TEXT("done")))
        {
            Indices.Add(Visible.Index);
        }
        return Indices;
    };
    const FJJReviewVisibilityBounds Swap = JJAnimReviewNotes::VisibilityBounds(CacheNotes, 15, TEXT("done"));
    TestEqual(TEXT("Swap frame starts a new span"), Swap.FromFrame, static_cast<int64>(15));
    TestTrue(TEXT("Precondition: same count, different set across the swap"),
        VisibleIndices(14).Num() == VisibleIndices(15).Num() && VisibleIndices(14) != VisibleIndices(15));
    for (int32 Frame = 0; Frame < 40; ++Frame)
    {
        const FJJReviewVisibilityBounds Bounds = JJAnimReviewNotes::VisibilityBounds(CacheNotes, Frame, TEXT("done"));
        const TArray<int32> Expected = VisibleIndices(Frame);
        const int64 Low = FMath::Max<int64>(Bounds.FromFrame, 0);
        const int64 High = FMath::Min<int64>(Bounds.UntilFrame, 40);
        bool bStable = Low <= Frame && Frame < High;
        for (int64 Other = Low; Other < High; ++Other)
        {
            bStable &= VisibleIndices(static_cast<int32>(Other)) == Expected;
        }
        TestTrue(*FString::Printf(TEXT("Visible note set is constant within bounds at frame %d"), Frame), bStable);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6Fix3MapGhostBonesTest,
    "JJAnimReviewKit.R6Fix3.MapGhostBones", R6Fix3TestFlags)

bool FJJAnimReviewR6Fix3MapGhostBonesTest::RunTest(const FString& Parameters)
{
    const TArray<FName> Mesh = { TEXT("root"), TEXT("pelvis"), TEXT("hand_l") };
    const TArray<FName> Samples = { TEXT("hand_l"), TEXT("extra"), TEXT("root"), TEXT("pelvis") };
    TestTrue(TEXT("Mesh order maps to sample indices"), JJAnimReviewNotes::MapGhostBones(Mesh, Samples) == TArray<int32>({ 2, 3, 0 }));

    const TArray<FName> MissingHand = { TEXT("root"), TEXT("pelvis") };
    TestEqual(TEXT("Missing mesh bone gives an empty map"), JJAnimReviewNotes::MapGhostBones(Mesh, MissingHand).Num(), 0);
    TestEqual(TEXT("Empty mesh gives an empty map"), JJAnimReviewNotes::MapGhostBones({}, Samples).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6Fix3ComponentToLocalTest,
    "JJAnimReviewKit.R6Fix3.ComponentToLocalPose", R6Fix3TestFlags)

bool FJJAnimReviewR6Fix3ComponentToLocalTest::RunTest(const FString& Parameters)
{
    // Child (index 0) is stored before its parent (index 1) to prove order independence.
    const FTransform RootLocal(FQuat(FRotator(0.0, 90.0, 0.0)), FVector(10.0, 0.0, 0.0), FVector(1.0));
    const FTransform ParentLocal(FQuat(FRotator(30.0, 0.0, 0.0)), FVector(0.0, 5.0, 0.0), FVector(2.0));
    const FTransform ChildLocal(FQuat(FRotator(0.0, 0.0, 45.0)), FVector(3.0, 0.0, 1.0), FVector(1.0));
    const FTransform RootComponent = RootLocal;
    const FTransform ParentComponent = ParentLocal * RootComponent;
    const FTransform ChildComponent = ChildLocal * ParentComponent;

    const TArray<int32> Parents = { 1, 2, INDEX_NONE };
    const TArray<FTransform> Component = { ChildComponent, ParentComponent, RootComponent };
    TArray<FTransform> Local;
    TestTrue(TEXT("Valid hierarchy converts"), JJAnimReviewNotes::ComponentToLocalPose(Parents, Component, Local));
    TestEqual(TEXT("Output size"), Local.Num(), 3);
    if (Local.Num() == 3)
    {
        TestTrue(TEXT("Root stays in component space"), Local[2].Equals(RootLocal, 1.e-4));
        TestTrue(TEXT("Scaled parent local recovered"), Local[1].Equals(ParentLocal, 1.e-4));
        TestTrue(TEXT("Child local recovered under scaled, rotated parent"), Local[0].Equals(ChildLocal, 1.e-4));
    }

    const TArray<FTransform> Sentinel = { FTransform(FVector(7.0, 7.0, 7.0)) };
    TArray<FTransform> Untouched = Sentinel;
    TestFalse(TEXT("Size mismatch fails"), JJAnimReviewNotes::ComponentToLocalPose({ INDEX_NONE }, Component, Untouched));
    TestTrue(TEXT("Failure leaves output unchanged (size)"), Untouched.Num() == 1 && Untouched[0].Equals(Sentinel[0]));
    TestFalse(TEXT("Self parent fails"), JJAnimReviewNotes::ComponentToLocalPose({ 0 }, { RootComponent }, Untouched));
    TestFalse(TEXT("Out-of-range parent fails"), JJAnimReviewNotes::ComponentToLocalPose({ 5 }, { RootComponent }, Untouched));
    TestTrue(TEXT("Failure leaves output unchanged (parent)"), Untouched.Num() == 1 && Untouched[0].Equals(Sentinel[0]));
    return true;
}

#endif
