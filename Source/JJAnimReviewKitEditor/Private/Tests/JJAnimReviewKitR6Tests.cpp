#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "JJAnimReviewNotes.h"
#include <limits>

namespace JJAnimReviewKitR6TestsPrivate
{
    constexpr EAutomationTestFlags R6TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    FJJReviewBoneGroup R6MakeGroup(const FString& Name, const TArray<FName>& Bones)
    {
        FJJReviewBoneGroup Group;
        Group.Name = Name;
        Group.Bones = Bones;
        return Group;
    }

    FJJReviewAnnotation R6MakeNote(const FString& Id, int32 Start, int32 End, const FString& Status)
    {
        FJJReviewAnnotation Note;
        Note.AnnotationId = Id;
        Note.Frame = Start;
        Note.EndFrame = End;
        Note.Status = Status;
        return Note;
    }

    bool R6SameColor(const FLinearColor& A, const FLinearColor& B)
    {
        return A.Equals(B, 0.0005f);
    }

    bool R6HasSegment(const TArray<FJJReviewBoneSegment>& Segments, int32 Bone, int32 Parent)
    {
        return Segments.ContainsByPredicate([Bone, Parent](const FJJReviewBoneSegment& Segment)
        {
            return Segment.Bone == Bone && Segment.Parent == Parent;
        });
    }

    void R6MakeMannequin(TArray<FName>& Bones, TArray<int32>& Parents)
    {
        auto Add = [&](const TCHAR* Name, const TCHAR* Parent)
        {
            Bones.Add(FName(Name));
            Parents.Add(Parent ? Bones.IndexOfByKey(FName(Parent)) : INDEX_NONE);
        };
        Add(TEXT("root"), nullptr);
        Add(TEXT("pelvis"), TEXT("root"));
        Add(TEXT("spine_01"), TEXT("pelvis"));
        Add(TEXT("spine_02"), TEXT("spine_01"));
        Add(TEXT("spine_03"), TEXT("spine_02"));
        Add(TEXT("spine_04"), TEXT("spine_03"));
        Add(TEXT("spine_05"), TEXT("spine_04"));
        Add(TEXT("neck_01"), TEXT("spine_05"));
        Add(TEXT("neck_02"), TEXT("neck_01"));
        Add(TEXT("head"), TEXT("neck_02"));
        const TCHAR* Sides[] = { TEXT("l"), TEXT("r") };
        for (const TCHAR* Side : Sides)
        {
            auto N = [Side](const TCHAR* Base)
            {
                return FString::Printf(TEXT("%s_%s"), Base, Side);
            };
            Add(*N(TEXT("clavicle")), TEXT("spine_05"));
            Add(*N(TEXT("upperarm")), *N(TEXT("clavicle")));
            Add(*N(TEXT("lowerarm")), *N(TEXT("upperarm")));
            Add(*N(TEXT("hand")), *N(TEXT("lowerarm")));
            Add(*N(TEXT("index_metacarpal")), *N(TEXT("hand")));
            Add(*N(TEXT("index_01")), *N(TEXT("index_metacarpal")));
            Add(*N(TEXT("thigh")), TEXT("pelvis"));
            Add(*N(TEXT("calf")), *N(TEXT("thigh")));
            Add(*N(TEXT("foot")), *N(TEXT("calf")));
            Add(*N(TEXT("ball")), *N(TEXT("foot")));
        }
    }
}

using namespace JJAnimReviewKitR6TestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6GroupColorTest,
    "JJAnimReviewKit.R6.GroupColors", R6TestFlags)

bool FJJAnimReviewR6GroupColorTest::RunTest(const FString& Parameters)
{
    // The fixed palette is keyed by the built-in template names, so both lists must agree.
    TArray<FName> Bones;
    TArray<int32> Parents;
    R6MakeMannequin(Bones, Parents);
    bool bMatched = false;
    const TArray<FJJReviewBoneGroup> Template = JJAnimReviewNotes::MannequinTemplate(Bones, Parents, bMatched);
    TestTrue(TEXT("Template matched"), bMatched);
    TestEqual(TEXT("Eight built-in groups"), Template.Num(), 8);

    const TArray<FString> Fixed =
    {
        TEXT("머리"), TEXT("몸통"), TEXT("왼팔"), TEXT("왼손가락"),
        TEXT("오른팔"), TEXT("오른손가락"), TEXT("왼다리"), TEXT("오른다리")
    };
    for (const FJJReviewBoneGroup& Group : Template)
    {
        TestTrue(FString::Printf(TEXT("Template name %s has a fixed palette entry"), *Group.Name), Fixed.Contains(Group.Name));
    }

    for (int32 A = 0; A < Fixed.Num(); ++A)
    {
        const FLinearColor ColorA = JJAnimReviewNotes::GroupColor(Fixed[A]);
        TestTrue(FString::Printf(TEXT("%s is stable"), *Fixed[A]), R6SameColor(ColorA, JJAnimReviewNotes::GroupColor(Fixed[A])));
        for (int32 B = A + 1; B < Fixed.Num(); ++B)
        {
            const FLinearColor ColorB = JJAnimReviewNotes::GroupColor(Fixed[B]);
            const float Distance = FMath::Abs(ColorA.R - ColorB.R) + FMath::Abs(ColorA.G - ColorB.G) + FMath::Abs(ColorA.B - ColorB.B);
            TestTrue(FString::Printf(TEXT("%s and %s are clearly different"), *Fixed[A], *Fixed[B]), Distance > 0.25f);
        }
    }

    const FLinearColor User = JJAnimReviewNotes::GroupColor(TEXT("검 궤적"));
    TestTrue(TEXT("User group color is stable across calls"), R6SameColor(User, JJAnimReviewNotes::GroupColor(TEXT("검 궤적"))));
    TestTrue(TEXT("User group color is opaque"), FMath::IsNearlyEqual(User.A, 1.0f));
    TestFalse(TEXT("User group color is not the bones-only color"), R6SameColor(User, JJAnimReviewNotes::GroupColor(TEXT(""))));
    TestTrue(TEXT("Bones-only color is stable"), R6SameColor(JJAnimReviewNotes::GroupColor(TEXT("")), JJAnimReviewNotes::GroupColor(TEXT(""))));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6UniqueNameTest,
    "JJAnimReviewKit.R6.UniqueGroupName", R6TestFlags)

bool FJJAnimReviewR6UniqueNameTest::RunTest(const FString& Parameters)
{
    const FName Hand(TEXT("hand_l"));
    TArray<FJJReviewBoneGroup> Groups = { R6MakeGroup(TEXT("왼팔"), { Hand }) };
    TestEqual(TEXT("Free name is kept"), JJAnimReviewNotes::UniqueGroupName(TEXT("검"), Groups), FString(TEXT("검")));
    TestEqual(TEXT("Name is trimmed"), JJAnimReviewNotes::UniqueGroupName(TEXT("  검 "), Groups), FString(TEXT("검")));
    TestEqual(TEXT("Duplicate gets 2"), JJAnimReviewNotes::UniqueGroupName(TEXT("왼팔"), Groups), FString(TEXT("왼팔 2")));
    TestEqual(TEXT("Trimmed duplicate gets 2"), JJAnimReviewNotes::UniqueGroupName(TEXT(" 왼팔 "), Groups), FString(TEXT("왼팔 2")));

    Groups.Add(R6MakeGroup(TEXT("왼팔 2"), { Hand }));
    TestEqual(TEXT("Next free number"), JJAnimReviewNotes::UniqueGroupName(TEXT("왼팔"), Groups), FString(TEXT("왼팔 3")));

    TArray<FJJReviewBoneGroup> Gap = { R6MakeGroup(TEXT("왼팔"), { Hand }), R6MakeGroup(TEXT("왼팔 3"), { Hand }) };
    TestEqual(TEXT("First empty number is reused"), JJAnimReviewNotes::UniqueGroupName(TEXT("왼팔"), Gap), FString(TEXT("왼팔 2")));

    // A hidden built-in (empty bone list) still reserves its name, so restore cannot collide later.
    TArray<FJJReviewBoneGroup> Hidden = { R6MakeGroup(TEXT("머리"), {}) };
    TestEqual(TEXT("Hidden built-in name is reserved"), JJAnimReviewNotes::UniqueGroupName(TEXT("머리"), Hidden), FString(TEXT("머리 2")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6RestoreDefaultsTest,
    "JJAnimReviewKit.R6.RestoreDefaultGroups", R6TestFlags)

bool FJJAnimReviewR6RestoreDefaultsTest::RunTest(const FString& Parameters)
{
    TArray<FName> Bones;
    TArray<int32> Parents;
    R6MakeMannequin(Bones, Parents);
    bool bMatched = false;
    const TArray<FJJReviewBoneGroup> Template = JJAnimReviewNotes::MannequinTemplate(Bones, Parents, bMatched);

    const TArray<FJJReviewBoneGroup> User =
    {
        R6MakeGroup(TEXT("왼팔"), {}),
        R6MakeGroup(TEXT("내 왼팔"), { FName(TEXT("upperarm_l")) }),
        R6MakeGroup(TEXT("머리"), { FName(TEXT("head")) }),
        R6MakeGroup(TEXT("오른다리"), {}),
        R6MakeGroup(TEXT("사용자 빈 그룹"), {})
    };
    const TArray<FJJReviewBoneGroup> Restored = JJAnimReviewNotes::RestoreDefaultGroups(User, Template);
    auto Has = [&Restored](const FString& Name)
    {
        return Restored.ContainsByPredicate([&Name](const FJJReviewBoneGroup& Group)
        {
            return Group.Name == Name;
        });
    };
    TestFalse(TEXT("Deleted built-in tombstone removed"), Has(TEXT("왼팔")));
    TestFalse(TEXT("Second tombstone removed"), Has(TEXT("오른다리")));
    TestTrue(TEXT("Renamed copy kept"), Has(TEXT("내 왼팔")));
    TestTrue(TEXT("Non-empty override of a built-in kept"), Has(TEXT("머리")));
    TestTrue(TEXT("Empty non-template entry kept"), Has(TEXT("사용자 빈 그룹")));
    TestEqual(TEXT("Only tombstones removed"), Restored.Num(), 3);

    const TArray<FJJReviewBoneGroup> Resolved = JJAnimReviewNotes::ResolveGroups(Bones, Template, Restored);
    const FJJReviewBoneGroup* Arm = Resolved.FindByPredicate([](const FJJReviewBoneGroup& Group)
    {
        return Group.Name == TEXT("왼팔");
    });
    TestTrue(TEXT("Built-in group visible again after restore"), Arm && !Arm->Bones.IsEmpty());
    const FJJReviewBoneGroup* Head = Resolved.FindByPredicate([](const FJJReviewBoneGroup& Group)
    {
        return Group.Name == TEXT("머리");
    });
    TestTrue(TEXT("User override still wins after restore"), Head && Head->Bones.Num() == 1);

    const TArray<FJJReviewBoneGroup> Nothing = JJAnimReviewNotes::RestoreDefaultGroups({}, Template);
    TestEqual(TEXT("Empty user list stays empty"), Nothing.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6WeightedSegmentsTest,
    "JJAnimReviewKit.R6.WeightedSegments", R6TestFlags)

bool FJJAnimReviewR6WeightedSegmentsTest::RunTest(const FString& Parameters)
{
    // 0 root, 1 pelvis, 2 spine, 3 helper (unweighted), 4 hand, 5 ik_foot_root, 6 ik_foot_l, 7 prop_01
    const TArray<FName> Bones =
    {
        TEXT("root"), TEXT("pelvis"), TEXT("spine"), TEXT("helper"), TEXT("hand"),
        TEXT("ik_foot_root"), TEXT("ik_foot_l"), TEXT("prop_01")
    };
    const TArray<int32> Parents = { INDEX_NONE, 0, 1, 2, 3, 0, 5, 0 };
    const TSet<FName> Weighted = { TEXT("pelvis"), TEXT("spine"), TEXT("hand") };

    const TArray<FJJReviewBoneSegment> Segments = JJAnimReviewNotes::WeightedSegments(Bones, Parents, &Weighted);
    TestTrue(TEXT("spine -> pelvis"), R6HasSegment(Segments, 2, 1));
    TestTrue(TEXT("hand skips unweighted helper -> spine"), R6HasSegment(Segments, 4, 2));
    TestEqual(TEXT("Only weighted-to-weighted segments"), Segments.Num(), 2);
    for (const FJJReviewBoneSegment& Segment : Segments)
    {
        TestTrue(TEXT("No IK, prop or root endpoint"), Segment.Bone != 0 && Segment.Bone < 5 && Segment.Parent != 0 && Segment.Parent < 5);
    }

    const TArray<FJJReviewBoneSegment> All = JJAnimReviewNotes::WeightedSegments(Bones, Parents, nullptr);
    TestEqual(TEXT("Unavailable mesh data keeps every parent link"), All.Num(), Bones.Num() - 1);
    TestTrue(TEXT("ik_foot_l -> ik_foot_root kept without data"), R6HasSegment(All, 6, 5));

    const TSet<FName> Empty;
    TestEqual(TEXT("Available empty set draws nothing"), JJAnimReviewNotes::WeightedSegments(Bones, Parents, &Empty).Num(), 0);

    // Malformed hierarchies must terminate and never produce invalid indices.
    const TArray<FName> Loop = { TEXT("a"), TEXT("b"), TEXT("c") };
    const TArray<int32> LoopParents = { 1, 2, 0 };
    const TSet<FName> OnlyA = { TEXT("a") };
    TestEqual(TEXT("Cycle without weighted ancestor terminates"), JJAnimReviewNotes::WeightedSegments(Loop, LoopParents, &OnlyA).Num(), 0);
    const TArray<int32> SelfParent = { 0, 7, INDEX_NONE };
    TestEqual(TEXT("Self or out-of-range parent is skipped"), JJAnimReviewNotes::WeightedSegments(Loop, SelfParent, nullptr).Num(), 0);
    const TArray<int32> Short = { INDEX_NONE };
    TestEqual(TEXT("Missing parent entries are skipped"), JJAnimReviewNotes::WeightedSegments(Loop, Short, nullptr).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6VisibleNotesTest,
    "JJAnimReviewKit.R6.VisibleNotes", R6TestFlags)

bool FJJAnimReviewR6VisibleNotesTest::RunTest(const FString& Parameters)
{
    FJJReviewAnnotation Range = R6MakeNote(TEXT("range"), 10, 12, TEXT("open"));
    Range.GroupName = TEXT("왼팔");
    Range.Bones = { TEXT("upperarm_l"), TEXT("hand_l") };
    FJJReviewAnnotation Single = R6MakeNote(TEXT("single"), 20, INDEX_NONE, TEXT("open"));
    Single.Bone = TEXT("head");
    const FJJReviewAnnotation Resolved = R6MakeNote(TEXT("resolved"), 10, 10, TEXT("resolved"));
    const TArray<FJJReviewAnnotation> Notes = { Range, Single, Resolved };

    auto Ids = [&Notes](const TArray<FJJReviewVisibleNote>& Visible)
    {
        TArray<FString> Result;
        for (const FJJReviewVisibleNote& Entry : Visible)
        {
            Result.Add(Notes.IsValidIndex(Entry.Index) ? Notes[Entry.Index].AnnotationId : TEXT("?"));
        }
        return Result;
    };

    TestEqual(TEXT("7f is outside 10-12 +-2"), JJAnimReviewNotes::VisibleNotes(Notes, 7, TEXT("")).Num(), 0);
    TestTrue(TEXT("8f shows the open range only"), Ids(JJAnimReviewNotes::VisibleNotes(Notes, 8, TEXT(""))) == TArray<FString>{ TEXT("range") });
    TestTrue(TEXT("14f still shows the range"), Ids(JJAnimReviewNotes::VisibleNotes(Notes, 14, TEXT(""))) == TArray<FString>{ TEXT("range") });
    TestEqual(TEXT("15f shows nothing"), JJAnimReviewNotes::VisibleNotes(Notes, 15, TEXT("")).Num(), 0);
    TestTrue(TEXT("Single note uses its own frame +-2"), Ids(JJAnimReviewNotes::VisibleNotes(Notes, 22, TEXT(""))) == TArray<FString>{ TEXT("single") });

    const TArray<FJJReviewVisibleNote> WithSelection = JJAnimReviewNotes::VisibleNotes(Notes, 10, TEXT("resolved"));
    TestTrue(TEXT("Selected resolved note is shown"), Ids(WithSelection).Contains(TEXT("resolved")));
    for (const FJJReviewVisibleNote& Entry : WithSelection)
    {
        TestEqual(TEXT("Only the selected entry is flagged"), Entry.bSelected, Notes[Entry.Index].AnnotationId == TEXT("resolved"));
    }
    TestEqual(TEXT("Selected resolved note still obeys the frame window"), JJAnimReviewNotes::VisibleNotes(Notes, 30, TEXT("resolved")).Num(), 0);

    const TArray<FJJReviewVisibleNote> Group = JJAnimReviewNotes::VisibleNotes(Notes, 11, TEXT(""));
    TestTrue(TEXT("Group note carries its bone copy"), Group.Num() == 1 && Group[0].Bones.Num() == 2);

    const TArray<FJJReviewAnnotation> Edge = { R6MakeNote(TEXT("edge"), MAX_int32, INDEX_NONE, TEXT("open")) };
    TestEqual(TEXT("int32 max frame does not overflow"), JJAnimReviewNotes::VisibleNotes(Edge, MAX_int32 - 2, TEXT("")).Num(), 1);
    TestEqual(TEXT("Far negative frame is not visible"), JJAnimReviewNotes::VisibleNotes(Edge, MIN_int32, TEXT("")).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6BoneColorsTest,
    "JJAnimReviewKit.R6.BoneColorLayers", R6TestFlags)

bool FJJAnimReviewR6BoneColorsTest::RunTest(const FString& Parameters)
{
    const FName Shoulder(TEXT("upperarm_l"));
    const FName Hand(TEXT("hand_l"));
    const FName Head(TEXT("head"));
    const FName Spine(TEXT("spine_01"));
    const TArray<FJJReviewBoneGroup> Groups =
    {
        R6MakeGroup(TEXT("왼팔"), { Shoulder, Hand }),
        R6MakeGroup(TEXT("왼손 강조"), { Hand }),
        R6MakeGroup(TEXT("머리"), { Head })
    };

    TMap<FName, FJJReviewBoneColor> None = JJAnimReviewNotes::BoneColors(Groups, {}, {}, false, false, {}, TEXT(""));
    TestEqual(TEXT("Nothing on paints nothing"), None.Num(), 0);

    // Group layer: overlap resolved by group list order.
    TMap<FName, FJJReviewBoneColor> AllGroups = JJAnimReviewNotes::BoneColors(Groups, {}, {}, true, false, {}, TEXT(""));
    TestEqual(TEXT("All group bones painted"), AllGroups.Num(), 3);
    TestTrue(TEXT("Overlap keeps the earlier group color"), AllGroups.Contains(Hand) && R6SameColor(AllGroups[Hand].Color, AllGroups[Shoulder].Color));
    TMap<FName, FJJReviewBoneColor> LaterOnly = JJAnimReviewNotes::BoneColors({ Groups[1] }, {}, {}, true, false, {}, TEXT(""));
    TestFalse(TEXT("Precondition: the two overlapping groups have different colors"),
        R6SameColor(JJAnimReviewNotes::GroupColor(TEXT("왼팔")), JJAnimReviewNotes::GroupColor(TEXT("왼손 강조"))));
    TestFalse(TEXT("Later overlapping group does not repaint the bone"), R6SameColor(AllGroups[Hand].Color, LaterOnly[Hand].Color));
    TestFalse(TEXT("Group layer is dimmed, not the raw color"), R6SameColor(AllGroups[Head].Color, JJAnimReviewNotes::GroupColor(TEXT("머리"))));

    // Selection layer beats the group layer and uses the raw group color.
    TMap<FName, FJJReviewBoneColor> Selected = JJAnimReviewNotes::BoneColors(Groups, {}, {}, true, true, { Hand }, TEXT("왼손 강조"));
    TestTrue(TEXT("Selected group raw color"), R6SameColor(Selected[Hand].Color, JJAnimReviewNotes::GroupColor(TEXT("왼손 강조"))));
    TestTrue(TEXT("Selection outranks groups"), Selected[Hand].Priority < Selected[Shoulder].Priority);

    // Note layer beats selection; the selected note beats other notes.
    FJJReviewAnnotation ArmNote = R6MakeNote(TEXT("arm"), 0, INDEX_NONE, TEXT("open"));
    ArmNote.GroupName = TEXT("왼팔");
    ArmNote.Bones = { Shoulder, Hand };
    FJJReviewAnnotation LooseNote = R6MakeNote(TEXT("loose"), 0, INDEX_NONE, TEXT("open"));
    LooseNote.Bones = { Hand, Spine };
    const TArray<FJJReviewAnnotation> Notes = { ArmNote, LooseNote };

    const TArray<FJJReviewVisibleNote> NoneSelected = JJAnimReviewNotes::VisibleNotes(Notes, 0, TEXT(""));
    TMap<FName, FJJReviewBoneColor> NoteLayer = JJAnimReviewNotes::BoneColors(Groups, Notes, NoneSelected, true, true, { Hand }, TEXT("왼손 강조"));
    TestTrue(TEXT("Note outranks selection"), NoteLayer[Hand].Priority < Selected[Hand].Priority);
    TestFalse(TEXT("Note color replaces the selection color"), R6SameColor(NoteLayer[Hand].Color, Selected[Hand].Color));
    TestTrue(TEXT("Grouped note wins the shared bone by group order"),
        R6SameColor(NoteLayer[Hand].Color, NoteLayer[Shoulder].Color));
    TestTrue(TEXT("Loose note paints its other bone"), NoteLayer.Contains(Spine));
    TestFalse(TEXT("Unselected notes are dimmed"), R6SameColor(NoteLayer[Shoulder].Color, JJAnimReviewNotes::GroupColor(TEXT("왼팔"))));

    const TArray<FJJReviewVisibleNote> LooseSelected = JJAnimReviewNotes::VisibleNotes(Notes, 0, TEXT("loose"));
    TMap<FName, FJJReviewBoneColor> Focus = JJAnimReviewNotes::BoneColors(Groups, Notes, LooseSelected, false, false, {}, TEXT(""));
    TestTrue(TEXT("Selected note wins the shared bone"), R6SameColor(Focus[Hand].Color, JJAnimReviewNotes::GroupColor(TEXT(""))));
    TestTrue(TEXT("Selected note uses the raw bones-only color"), R6SameColor(Focus[Spine].Color, JJAnimReviewNotes::GroupColor(TEXT(""))));
    TestTrue(TEXT("Other note keeps its own bone"), Focus.Contains(Shoulder) && !R6SameColor(Focus[Shoulder].Color, Focus[Hand].Color));

    TArray<FJJReviewVisibleNote> Broken = NoneSelected;
    Broken.Add({ 99, true, { Head } });
    TMap<FName, FJJReviewBoneColor> Safe = JJAnimReviewNotes::BoneColors(Groups, Notes, Broken, false, false, {}, TEXT(""));
    TestFalse(TEXT("Out-of-range visible entry is ignored"), Safe.Contains(Head));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewR6VertexColorTest,
    "JJAnimReviewKit.R6.VertexColorBlend", R6TestFlags)

bool FJJAnimReviewR6VertexColorTest::RunTest(const FString& Parameters)
{
    const FName Painted(TEXT("hand_l"));
    const FName Plain(TEXT("lowerarm_l"));
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    TMap<FName, FJJReviewBoneColor> Colors;
    Colors.Add(Painted, { FLinearColor(1, 0, 0), 0 });
    const FLinearColor Neutral = JJAnimReviewNotes::VertexColor({}, Colors);
    TestTrue(TEXT("No influence gives an opaque neutral"), FMath::IsNearlyEqual(Neutral.A, 1.0f) && FMath::IsNearlyEqual(Neutral.R, Neutral.G));

    const FLinearColor Full = JJAnimReviewNotes::VertexColor({ { Painted, 1.0f } }, Colors);
    TestTrue(TEXT("Fully weighted vertex takes the bone color"), R6SameColor(Full, FLinearColor(1, 0, 0, 1)));

    const FLinearColor Half = JJAnimReviewNotes::VertexColor({ { Painted, 0.5f }, { Plain, 0.5f } }, Colors);
    TestTrue(TEXT("Half weight blends with neutral"),
        FMath::IsNearlyEqual(Half.R, (1.0f + Neutral.R) * 0.5f, 0.001f) && FMath::IsNearlyEqual(Half.G, Neutral.G * 0.5f, 0.001f));
    TestTrue(TEXT("Blend is opaque"), FMath::IsNearlyEqual(Half.A, 1.0f));

    const FLinearColor Unnormalized = JJAnimReviewNotes::VertexColor({ { Painted, 2.0f }, { Plain, 2.0f } }, Colors);
    TestTrue(TEXT("Weights are normalized"), R6SameColor(Unnormalized, Half));

    const FLinearColor Bad = JJAnimReviewNotes::VertexColor({ { Painted, 0.0f }, { Painted, -1.0f }, { Painted, NaN } }, Colors);
    TestTrue(TEXT("Zero, negative and NaN weights are ignored"), R6SameColor(Bad, Neutral));

    const FLinearColor Mixed = JJAnimReviewNotes::VertexColor({ { Painted, 1.0f }, { Plain, NaN } }, Colors);
    TestTrue(TEXT("A NaN influence does not poison valid ones"), R6SameColor(Mixed, FLinearColor(1, 0, 0, 1)));
    return true;
}

#endif
