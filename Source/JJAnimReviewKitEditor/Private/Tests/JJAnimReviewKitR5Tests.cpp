#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "JJAnimReviewModel.h"
#include "JJAnimReviewNotes.h"
#include "JJAnimReviewStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

namespace JJAnimReviewKitR5TestsPrivate
{
    constexpr EAutomationTestFlags R5TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    const FString R5ContentA = TEXT("pose_md5_v2:00000000000000000000000000000001");

    FJJReviewAnnotation R5MakeNote(const FString& Id, int32 Start, int32 End = INDEX_NONE)
    {
        FJJReviewAnnotation Note;
        Note.AnnotationId = Id;
        Note.AuthoredTake = TEXT("take_1");
        Note.AuthoredContentId = R5ContentA;
        Note.Frame = Start;
        Note.EndFrame = End;
        Note.Text = TEXT("raise then strike");
        return Note;
    }

    FJJReviewBundle R5MakeBundle(int32 TakeNumber)
    {
        FJJReviewBundle Bundle;
        Bundle.Asset = TEXT("/Game/ClaudeTest/Fake.Fake");
        Bundle.TakeNumber = TakeNumber;
        Bundle.Take = FString::Printf(TEXT("take_%d"), TakeNumber);
        Bundle.ContentId = R5ContentA;
        Bundle.CreatedUtc = TEXT("2026-11-07T00:00:00Z");
        Bundle.UpdatedUtc = Bundle.CreatedUtc;
        return Bundle;
    }

    // Ter-like UE5 mannequin hierarchy (names confirmed in a Ter pose.json).
    void R5MakeMannequin(TArray<FName>& Bones, TArray<int32>& Parents)
    {
        auto Add = [&](const TCHAR* Name, const TCHAR* Parent)
        {
            Bones.Add(FName(Name));
            Parents.Add(Parent ? Bones.IndexOfByKey(FName(Parent)) : INDEX_NONE);
        };
        Add(TEXT("root"), nullptr);
        Add(TEXT("attach"), TEXT("root"));
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
            Add(*N(TEXT("upperarm_twist_01")), *N(TEXT("upperarm")));
            Add(*N(TEXT("lowerarm")), *N(TEXT("upperarm")));
            Add(*N(TEXT("hand")), *N(TEXT("lowerarm")));
            Add(*N(TEXT("index_metacarpal")), *N(TEXT("hand")));
            Add(*N(TEXT("index_01")), *N(TEXT("index_metacarpal")));
            Add(*N(TEXT("weapon")), *N(TEXT("hand")));
            Add(*N(TEXT("thigh")), TEXT("pelvis"));
            Add(*N(TEXT("thigh_twist_01")), *N(TEXT("thigh")));
            Add(*N(TEXT("calf")), *N(TEXT("thigh")));
            Add(*N(TEXT("foot")), *N(TEXT("calf")));
            Add(*N(TEXT("ball")), *N(TEXT("foot")));
            Add(*N(TEXT("ik_hand")), TEXT("root"));
        }
    }

    const FJJReviewBoneGroup* R5FindGroup(const TArray<FJJReviewBoneGroup>& Groups, const FString& Name)
    {
        return Groups.FindByPredicate([&Name](const FJJReviewBoneGroup& Group)
        {
            return Group.Name == Name;
        });
    }
}

using namespace JJAnimReviewKitR5TestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewTakeNameTest,
    "JJAnimReviewKit.R5.TakeNameParseAndSort", R5TestFlags)

bool FJJAnimReviewTakeNameTest::RunTest(const FString& Parameters)
{
    int32 Number = 0;
    TestTrue(TEXT("take_1 parses"), JJAnimReviewModel::ParseTakeName(TEXT("take_1"), Number) && Number == 1);
    TestTrue(TEXT("take_12 parses"), JJAnimReviewModel::ParseTakeName(TEXT("take_12"), Number) && Number == 12);
    TestTrue(TEXT("take_2147483647 parses"), JJAnimReviewModel::ParseTakeName(TEXT("take_2147483647"), Number) && Number == MAX_int32);

    const TCHAR* Bad[] =
    {
        TEXT("take_0"), TEXT("take_01"), TEXT("take_000001"), TEXT("take_"), TEXT("take_-1"), TEXT("take_1a"),
        TEXT("Take_1"), TEXT("take_2147483648"), TEXT("take_99999999999999999999"), TEXT("take_ 1")
    };
    for (const TCHAR* Name : Bad)
    {
        TestFalse(FString::Printf(TEXT("%s is rejected"), Name), JJAnimReviewModel::ParseTakeName(Name, Number));
    }

    const TArray<FString> Sorted = JJAnimReviewModel::SortTakeNames(
        { TEXT("take_10"), TEXT("take_2"), TEXT("junk"), TEXT("take_1"), TEXT("take_000003"), TEXT("take_9") });
    const TArray<FString> Expected = { TEXT("take_1"), TEXT("take_2"), TEXT("take_9"), TEXT("take_10") };
    TestTrue(TEXT("Invalid names are dropped and order is numeric"), Sorted == Expected);

    FJJReviewBundle Latest = R5MakeBundle(MAX_int32);
    FJJReviewBundle Next = R5MakeBundle(1);
    Next.ContentId = TEXT("pose_md5_v2:00000000000000000000000000000002");
    JJAnimReviewModel::SelectTake(Next, &Latest, TEXT("T"));
    TestEqual(TEXT("Overflowing take number yields no take"), Next.TakeNumber, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewRangeTest,
    "JJAnimReviewKit.R5.AnnotationRange", R5TestFlags)

bool FJJAnimReviewRangeTest::RunTest(const FString& Parameters)
{
    FString Error;
    const FJJReviewAnnotation Single = R5MakeNote(TEXT("s"), 8);
    TestEqual(TEXT("INDEX_NONE end means single frame"), JJAnimReviewNotes::EndFrame(Single), 8);
    TestTrue(TEXT("Single frame is valid"), JJAnimReviewNotes::ValidateRange(Single, 20, Error));
    TestEqual(TEXT("Single frame text"), JJAnimReviewNotes::RangeText(Single), FString(TEXT("8f")));

    const FJJReviewAnnotation Range = R5MakeNote(TEXT("r"), 4, 12);
    TestTrue(TEXT("4-12 is valid"), JJAnimReviewNotes::ValidateRange(Range, 20, Error));
    TestEqual(TEXT("Range text uses an en dash"), JJAnimReviewNotes::RangeText(Range), FString(TEXT("4–12f")));
    TestTrue(TEXT("End on the last frame is valid"), JJAnimReviewNotes::ValidateRange(R5MakeNote(TEXT("e"), 4, 20), 20, Error));
    TestEqual(TEXT("Explicit equal end prints as single"), JJAnimReviewNotes::RangeText(R5MakeNote(TEXT("q"), 5, 5)), FString(TEXT("5f")));

    TestFalse(TEXT("Start after end is rejected"), JJAnimReviewNotes::ValidateRange(R5MakeNote(TEXT("b"), 12, 4), 20, Error));
    TestFalse(TEXT("End past the last frame is rejected"), JJAnimReviewNotes::ValidateRange(R5MakeNote(TEXT("b"), 4, 21), 20, Error));
    TestFalse(TEXT("Negative start is rejected"), JJAnimReviewNotes::ValidateRange(R5MakeNote(TEXT("b"), -1, 3), 20, Error));
    TestFalse(TEXT("Single frame past the end is rejected"), JJAnimReviewNotes::ValidateRange(R5MakeNote(TEXT("b"), 21), 20, Error));

    FJJReviewBundle Bundle = R5MakeBundle(1);
    Bundle.Annotations.Add(R5MakeNote(TEXT("bad"), 9, 3));
    TestFalse(TEXT("Bundle with an inverted range does not validate"), JJAnimReviewModel::ValidateBundle(Bundle, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewTargetsTest,
    "JJAnimReviewKit.R5.TargetsLineAndLegend", R5TestFlags)

bool FJJAnimReviewTargetsTest::RunTest(const FString& Parameters)
{
    FJJReviewAnnotation OneBone = R5MakeNote(TEXT("b"), 8);
    OneBone.Bone = FName(TEXT("hand_l"));
    TestTrue(TEXT("Single bone target"), JJAnimReviewNotes::Targets(OneBone) == TArray<FName>{ FName(TEXT("hand_l")) });

    FJJReviewAnnotation Group = R5MakeNote(TEXT("g"), 4, 12);
    Group.GroupName = TEXT("왼팔");
    Group.Bones = { FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")) };
    Group.Direction = TEXT("+Z");
    Group.Amount = TEXT("much");
    Group.Text = TEXT("raise\nthen strike");
    TestEqual(TEXT("Group snapshot is the target"), JJAnimReviewNotes::Targets(Group).Num(), 2);
    const FString Line = JJAnimReviewNotes::AnnotationLine(Group);
    TestTrue(FString::Printf(TEXT("Line has range and group (%s)"), *Line), Line.StartsWith(TEXT("4–12f [왼팔] +Z much")));
    TestFalse(TEXT("Line is a single line"), Line.Contains(TEXT("\n")));

    FJJReviewAnnotation Multi = R5MakeNote(TEXT("m"), 2);
    Multi.Bones = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
    TestTrue(TEXT("Unnamed multi-bone target lists bones"), JJAnimReviewNotes::AnnotationLine(Multi).Contains(TEXT("[hand_l, hand_r]")));

    // Same group name with different snapshots: one legend line, union of bones.
    FJJReviewAnnotation Later = Group;
    Later.AnnotationId = TEXT("g2");
    Later.Bones = { FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")) };
    FJJReviewAnnotation Resolved = Group;
    Resolved.AnnotationId = TEXT("g3");
    Resolved.GroupName = TEXT("오른팔");
    Resolved.Status = TEXT("resolved");
    const TArray<FString> Legend = JJAnimReviewNotes::GroupLegend({ Group, Multi, Later, Resolved });
    TestEqual(TEXT("Only open named groups appear once"), Legend.Num(), 1);
    if (Legend.Num() == 1)
    {
        TestEqual(TEXT("Legend holds the bone union in order"), Legend[0], FString(TEXT("왼팔 = upperarm_l, lowerarm_l, hand_l")));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewMannequinTest,
    "JJAnimReviewKit.R5.MannequinTemplate", R5TestFlags)

bool FJJAnimReviewMannequinTest::RunTest(const FString& Parameters)
{
    TArray<FName> Bones;
    TArray<int32> Parents;
    R5MakeMannequin(Bones, Parents);
    bool bMatched = false;
    const TArray<FJJReviewBoneGroup> Groups = JJAnimReviewNotes::MannequinTemplate(Bones, Parents, bMatched);
    TestTrue(TEXT("Mannequin skeleton matches"), bMatched);
    TestEqual(TEXT("Eight template groups"), Groups.Num(), 8);

    const FJJReviewBoneGroup* LeftArm = R5FindGroup(Groups, TEXT("왼팔"));
    const FJJReviewBoneGroup* LeftFingers = R5FindGroup(Groups, TEXT("왼손가락"));
    const FJJReviewBoneGroup* RightLeg = R5FindGroup(Groups, TEXT("오른다리"));
    const FJJReviewBoneGroup* Torso = R5FindGroup(Groups, TEXT("몸통"));
    const FJJReviewBoneGroup* Head = R5FindGroup(Groups, TEXT("머리"));
    if (!TestTrue(TEXT("Named groups exist"), LeftArm && LeftFingers && RightLeg && Torso && Head))
    {
        return false;
    }
    TestEqual(TEXT("Left arm side"), LeftArm->Side, FString(TEXT("left")));
    TestEqual(TEXT("Right leg side"), RightLeg->Side, FString(TEXT("right")));
    TestEqual(TEXT("Torso side"), Torso->Side, FString(TEXT("center")));
    const TArray<FName> ExpectedArm =
    {
        FName(TEXT("clavicle_l")), FName(TEXT("upperarm_l")), FName(TEXT("upperarm_twist_01_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l"))
    };
    TestTrue(TEXT("Left arm = clavicle..hand with twist"), LeftArm->Bones == ExpectedArm);
    const TArray<FName> ExpectedFingers = { FName(TEXT("index_metacarpal_l")), FName(TEXT("index_01_l")) };
    TestTrue(TEXT("Fingers are hand descendants only (weapon excluded)"), LeftFingers->Bones == ExpectedFingers);
    TestTrue(TEXT("Leg includes twist and ball"), RightLeg->Bones.Contains(FName(TEXT("thigh_twist_01_r"))) && RightLeg->Bones.Contains(FName(TEXT("ball_r"))));
    TestEqual(TEXT("Torso = pelvis + spine_01..05"), Torso->Bones.Num(), 6);
    TestEqual(TEXT("Head = neck_01, neck_02, head"), Head->Bones.Num(), 3);

    for (const FJJReviewBoneGroup& Group : Groups)
    {
        for (const TCHAR* Excluded : { TEXT("root"), TEXT("attach"), TEXT("ik_hand_l"), TEXT("weapon_l"), TEXT("weapon_r") })
        {
            TestFalse(FString::Printf(TEXT("%s is not in %s"), Excluded, *Group.Name), Group.Bones.Contains(FName(Excluded)));
        }
    }

    TArray<FName> Few = { FName(TEXT("root")), FName(TEXT("Bip01")), FName(TEXT("pelvis")), FName(TEXT("head")) };
    TArray<int32> FewParents = { INDEX_NONE, 0, 1, 2 };
    const TArray<FJJReviewBoneGroup> None = JJAnimReviewNotes::MannequinTemplate(Few, FewParents, bMatched);
    TestFalse(TEXT("Non-mannequin skeleton does not match"), bMatched);
    TestEqual(TEXT("No template groups for it"), None.Num(), 0);

    TestEqual(TEXT("Side inference left"), JJAnimReviewNotes::InferSide({ FName(TEXT("hand_l")), FName(TEXT("lowerarm_l")), FName(TEXT("spine_01")) }), FString(TEXT("left")));
    TestEqual(TEXT("Side inference tie"), JJAnimReviewNotes::InferSide({ FName(TEXT("hand_l")), FName(TEXT("hand_r")) }), FString(TEXT("center")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewGroupsTest,
    "JJAnimReviewKit.R5.ResolveAndJsonGroups", R5TestFlags)

bool FJJAnimReviewGroupsTest::RunTest(const FString& Parameters)
{
    TArray<FName> Bones;
    TArray<int32> Parents;
    R5MakeMannequin(Bones, Parents);
    bool bMatched = false;
    const TArray<FJJReviewBoneGroup> Template = JJAnimReviewNotes::MannequinTemplate(Bones, Parents, bMatched);

    FJJReviewBoneGroup Override;
    Override.Name = TEXT("왼팔");
    Override.Side = TEXT("left");
    Override.Bones = { FName(TEXT("upperarm_l")), FName(TEXT("upperarm_l")), FName(TEXT("missing_bone")), FName(TEXT("lowerarm_l")) };
    FJJReviewBoneGroup Wrist;
    Wrist.Name = TEXT("손목");
    Wrist.Side = TEXT("center");
    Wrist.Bones = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
    FJJReviewBoneGroup Suppress;
    Suppress.Name = TEXT("머리");
    Suppress.Side = TEXT("center");

    const TArray<FJJReviewBoneGroup> Resolved = JJAnimReviewNotes::ResolveGroups(Bones, Template, { Override, Wrist, Suppress });
    const FJJReviewBoneGroup* Arm = R5FindGroup(Resolved, TEXT("왼팔"));
    TestTrue(TEXT("User group replaces the template group, filtered and deduplicated"),
        Arm && Arm->Bones == TArray<FName>{ FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")) });
    TestEqual(TEXT("Template name appears once"), Resolved.FilterByPredicate([](const FJJReviewBoneGroup& G)
    {
        return G.Name == TEXT("왼팔");
    }).Num(), 1);
    TestNotNull(TEXT("New user group is added"), R5FindGroup(Resolved, TEXT("손목")));
    const FJJReviewBoneGroup* Head = R5FindGroup(Resolved, TEXT("머리"));
    TestTrue(TEXT("Empty override suppresses the template bones"), Head && Head->Bones.IsEmpty());

    const FString Skeleton = TEXT("/Game/Characters/SK_Mannequin.SK_Mannequin");
    FString Json;
    FString Error;
    TestTrue(TEXT("Groups serialize"), JJAnimReviewNotes::GroupsToJson(Skeleton, { Override, Wrist, Suppress }, Json, Error));
    TArray<FJJReviewBoneGroup> Loaded;
    TestTrue(FString::Printf(TEXT("Groups round trip (%s)"), *Error), JJAnimReviewNotes::GroupsFromJson(Json, Skeleton, Loaded, Error));
    TestEqual(TEXT("Group count survives"), Loaded.Num(), 3);
    if (Loaded.Num() == 3)
    {
        TestEqual(TEXT("Korean name survives"), Loaded[0].Name, FString(TEXT("왼팔")));
        TestEqual(TEXT("Side survives"), Loaded[0].Side, FString(TEXT("left")));
        TestTrue(TEXT("Empty override survives"), Loaded[2].Bones.IsEmpty());
    }

    TestFalse(TEXT("Other skeleton's file is rejected"),
        JJAnimReviewNotes::GroupsFromJson(Json, TEXT("/Game/Other/SK_Other.SK_Other"), Loaded, Error));
    TestFalse(TEXT("Garbage is rejected"), JJAnimReviewNotes::GroupsFromJson(TEXT("{"), Skeleton, Loaded, Error));
    TestFalse(TEXT("Wrong schema is rejected"),
        JJAnimReviewNotes::GroupsFromJson(Json.Replace(TEXT("\"schemaVersion\": 1"), TEXT("\"schemaVersion\": 2")), Skeleton, Loaded, Error));

    FJJReviewBoneGroup BadSide = Wrist;
    BadSide.Side = TEXT("up");
    TestFalse(TEXT("Unknown side is rejected"), JJAnimReviewNotes::GroupsToJson(Skeleton, { BadSide }, Json, Error));
    TestFalse(TEXT("Duplicate group names are rejected"), JJAnimReviewNotes::GroupsToJson(Skeleton, { Wrist, Wrist }, Json, Error));
    FJJReviewBoneGroup Blank = Wrist;
    Blank.Name = TEXT("  ");
    TestFalse(TEXT("Blank name is rejected"), JJAnimReviewNotes::GroupsToJson(Skeleton, { Blank }, Json, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewImageRulesTest,
    "JJAnimReviewKit.R5.ImagePathAndCleanup", R5TestFlags)

bool FJJAnimReviewImageRulesTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("PNG under notes/<id>/ is safe"), JJAnimReviewNotes::IsSafeImagePath(TEXT("notes/a-1_B/0123abcd.png")));
    TestTrue(TEXT("JPG is safe"), JJAnimReviewNotes::IsSafeImagePath(TEXT("notes/a1/x.JPG")));
    const TCHAR* Unsafe[] =
    {
        TEXT("../x.png"), TEXT("notes/../x.png"), TEXT("notes/a/../../x.png"), TEXT("notes/a/b/x.png"), TEXT("C:/x.png"),
        TEXT("notes\\a\\x.png"), TEXT("notes/a/x.gif"), TEXT("sheet.png"), TEXT("notes//x.png"), TEXT("notes/a b/x.png")
    };
    for (const TCHAR* Path : Unsafe)
    {
        TestFalse(FString::Printf(TEXT("%s is unsafe"), Path), JJAnimReviewNotes::IsSafeImagePath(Path));
    }

    FJJReviewBundle Old = R5MakeBundle(1);
    FJJReviewAnnotation Kept = R5MakeNote(TEXT("kept"), 1);
    Kept.Images = { TEXT("notes/kept/a.png") };
    Old.Annotations.Add(Kept);
    FJJReviewBundle Latest = R5MakeBundle(2);
    FJJReviewAnnotation Carried = R5MakeNote(TEXT("carried"), 2);
    Carried.Images = { TEXT("notes/carried/b.jpg") };
    Latest.Annotations.Add(Carried);
    const TArray<FString> Orphans = JJAnimReviewNotes::UnreferencedImageFolders(
        { TEXT("kept"), TEXT("carried"), TEXT("deleted"), TEXT("..") }, { Old, Latest });
    TestTrue(TEXT("Only the unreferenced safe folder is removed"), Orphans == TArray<FString>{ TEXT("deleted") });
    TestEqual(TEXT("With no remaining takes every safe folder is an orphan"),
        JJAnimReviewNotes::UnreferencedImageFolders({ TEXT("kept") }, {}).Num(), 1);

    FString Error;
    FJJReviewBundle BadImage = R5MakeBundle(1);
    FJJReviewAnnotation Escape = R5MakeNote(TEXT("x"), 1);
    Escape.Images = { TEXT("notes/../../secret.png") };
    BadImage.Annotations.Add(Escape);
    TestFalse(TEXT("Bundle with an escaping image path does not validate"), JJAnimReviewModel::ValidateBundle(BadImage, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewSchema2Test,
    "JJAnimReviewKit.R5.Schema2AndMarkdown", R5TestFlags)

bool FJJAnimReviewSchema2Test::RunTest(const FString& Parameters)
{
    FJJReviewBundle Bundle = R5MakeBundle(3);
    FJJReviewAnnotation Group = R5MakeNote(TEXT("g"), 4, 12);
    Group.GroupName = TEXT("왼팔");
    Group.Bones = { FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")) };
    Group.Images = { TEXT("notes/g/ref.png") };
    Bundle.Annotations.Add(Group);

    FString Json;
    FString Error;
    TestTrue(TEXT("Schema 2 bundle serializes"), JJAnimReviewModel::ToJson(Bundle, Json));
    FJJReviewBundle Loaded;
    TestTrue(FString::Printf(TEXT("Schema 2 round trips (%s)"), *Error), JJAnimReviewModel::FromJson(Json, Loaded, Error));
    if (TestEqual(TEXT("Annotation survives"), Loaded.Annotations.Num(), 1))
    {
        const FJJReviewAnnotation& Note = Loaded.Annotations[0];
        TestEqual(TEXT("EndFrame survives"), Note.EndFrame, 12);
        TestEqual(TEXT("Group name survives"), Note.GroupName, FString(TEXT("왼팔")));
        TestEqual(TEXT("Bone snapshot survives"), Note.Bones.Num(), 3);
        TestTrue(TEXT("Image path survives"), Note.Images == TArray<FString>{ TEXT("notes/g/ref.png") });
    }
    TestFalse(TEXT("Schema 1 is rejected"),
        JJAnimReviewModel::FromJson(Json.Replace(TEXT("\"schemaVersion\": 2"), TEXT("\"schemaVersion\": 1")), Loaded, Error));

    const FString Markdown = JJAnimReviewModel::MakeMarkdown(Bundle);
    TestTrue(TEXT("Markdown has the group legend"), Markdown.Contains(TEXT("왼팔 = upperarm_l, lowerarm_l, hand_l")));
    TestTrue(TEXT("Markdown has the range and group line"), Markdown.Contains(TEXT("4–12f [왼팔]")));
    TestTrue(TEXT("Markdown lists the image path"), Markdown.Contains(TEXT("notes/g/ref.png")));

    FJJReviewBundle Big = R5MakeBundle(4);
    for (int32 Index = 0; Index < 120; ++Index)
    {
        FJJReviewAnnotation Note = R5MakeNote(FString::Printf(TEXT("n%d"), Index), Index % 30, Index % 30 + 3);
        Note.GroupName = FString::Printf(TEXT("그룹%d"), Index);
        for (int32 Bone = 0; Bone < 12; ++Bone)
        {
            Note.Bones.Add(FName(*FString::Printf(TEXT("long_bone_name_%02d_l"), Bone)));
        }
        Note.Text = FString::ChrN(150, TEXT('x'));
        Big.Annotations.Add(Note);
    }
    const FString BigMarkdown = JJAnimReviewModel::MakeMarkdown(Big);
    const int32 Bytes = FTCHARToUTF8(*BigMarkdown).Length();
    TestTrue(FString::Printf(TEXT("Markdown with many groups stays within 6000 UTF-8 bytes (%d)"), Bytes), Bytes <= 6000);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewLegendBudgetTest,
    "JJAnimReviewKit.R5.LegendDoesNotHideFailures", R5TestFlags)

bool FJJAnimReviewLegendBudgetTest::RunTest(const FString& Parameters)
{
    // Review S2 repro: long group legends/targets are emitted before failures and can exhaust the md budget.
    FJJReviewBundle Bundle = R5MakeBundle(5);
    FJJReviewCheckResult Failure;
    Failure.Check = TEXT("foot_sliding");
    Failure.Status = TEXT("failed");
    Failure.Bone = FName(TEXT("ball_l"));
    Failure.StartFrame = 0;
    Failure.EndFrame = 58;
    Failure.Measured = 2.4;
    Failure.Threshold = 2.0;
    Failure.Unit = TEXT("cm");
    Bundle.Checks.Add(Failure);
    for (int32 Index = 0; Index < 5; ++Index)
    {
        FJJReviewAnnotation Note = R5MakeNote(FString::Printf(TEXT("n%d"), Index), Index);
        Note.GroupName = FString::Printf(TEXT("손가락%d"), Index);
        for (int32 Bone = 0; Bone < 40; ++Bone)
        {
            Note.Bones.Add(FName(*FString::Printf(TEXT("finger_bone_long_name_%02d_l"), Bone)));
        }
        Bundle.Annotations.Add(Note);
    }
    const FString Markdown = JJAnimReviewModel::MakeMarkdown(Bundle);
    TestTrue(TEXT("The failed check line survives long group legends"), Markdown.Contains(TEXT("- failed foot_sliding")));
    TestTrue(TEXT("Failures come before the group legend"),
        Markdown.Find(TEXT("- failed foot_sliding")) < Markdown.Find(TEXT("- group: ")) || !Markdown.Contains(TEXT("- group: ")));

    // R5-fix M2: legend entries and anonymous target lists are capped with "… +N bones".
    const TArray<FString> Legend = JJAnimReviewNotes::GroupLegend(Bundle.Annotations);
    if (TestEqual(TEXT("One legend line per group"), Legend.Num(), 5))
    {
        TestTrue(FString::Printf(TEXT("Legend line is capped (%d chars)"), Legend[0].Len()), Legend[0].Len() <= 160);
        TestTrue(TEXT("Legend line reports omitted bones"), Legend[0].Contains(TEXT("bones")) && Legend[0].Contains(TEXT("… +")));
        TestTrue(TEXT("Legend line keeps the first bone"), Legend[0].Contains(TEXT("finger_bone_long_name_00_l")));
    }
    FJJReviewAnnotation Anonymous = Bundle.Annotations[0];
    Anonymous.GroupName.Reset();
    const FString Line = JJAnimReviewNotes::AnnotationLine(Anonymous);
    TestTrue(TEXT("Anonymous multi-bone target is capped"), Line.Contains(TEXT("… +")) && Line.Len() < 400);
    FJJReviewAnnotation Few = Anonymous;
    Few.Bones = { FName(TEXT("hand_l")), FName(TEXT("hand_r")) };
    TestTrue(TEXT("Short target list is unchanged"), JJAnimReviewNotes::AnnotationLine(Few).Contains(TEXT("[hand_l, hand_r]")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewStoreR5Test,
    "JJAnimReviewKit.R5.StoreCollisionAndImage", R5TestFlags)

bool FJJAnimReviewStoreR5Test::RunTest(const FString& Parameters)
{
    const FString Output = TEXT("ClaudeTests_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FString OutputRoot;
    FString Error;
    FJJAnimReviewStore::ResolveOutputRoot(Output, OutputRoot, Error);
    const FString AssetA = TEXT("/Game/ClaudeTest/A/Same.Same");
    const FString AssetB = TEXT("/Game/ClaudeTest/B/Same.Same");

    FJJReviewPoseData Pose;
    Pose.DurationSeconds = 1.0 / 30.0;
    Pose.BoneNames = { FName(TEXT("root")) };
    Pose.ParentIndices = { INDEX_NONE };
    for (int32 Frame = 0; Frame < 2; ++Frame)
    {
        FJJReviewPoseSample Sample;
        Sample.SourceFrame = Frame;
        Sample.TimeSeconds = Frame / 30.0;
        FJJReviewTransform Transform;
        Transform.Scale.X = 1.0;
        Transform.Scale.Y = 1.0;
        Transform.Scale.Z = 1.0;
        Sample.ComponentTransforms.Add(Transform);
        Sample.RootMotion = Transform;
        Pose.Samples.Add(Sample);
    }
    JJAnimReviewModel::ComputeContentId(Pose, Pose.ContentId, Error);

    FString TakeDirA;
    {
        FJJAnimReviewStore Store;
        if (!TestTrue(FString::Printf(TEXT("Store A opens (%s)"), *Error), Store.Open(Output, AssetA, Error)))
        {
            return false;
        }
        FJJReviewBundle Bundle = R5MakeBundle(1);
        Bundle.Asset = AssetA;
        Bundle.ContentId = Pose.ContentId;
        FJJReviewAnnotation Note = R5MakeNote(TEXT("note1"), 0);
        Note.AuthoredContentId = Pose.ContentId;

        const FString Source = OutputRoot / TEXT("source.png");
        FFileHelper::SaveStringToFile(TEXT("not really a png"), *Source);
        TestTrue(FString::Printf(TEXT("PNG attachment imports (%s)"), *Error), Store.ImportImage(Note, Source, Error));
        if (TestEqual(TEXT("One image path recorded"), Note.Images.Num(), 1))
        {
            TestTrue(TEXT("Recorded path is safe"), JJAnimReviewNotes::IsSafeImagePath(Note.Images[0]));
            TestTrue(TEXT("Copied file exists"), IFileManager::Get().FileExists(*Store.GetImagePath(Note.Images[0])));
            TestTrue(TEXT("Image lives outside take folders"), Note.Images[0].StartsWith(TEXT("notes/note1/")));
        }
        const FString Gif = OutputRoot / TEXT("source.gif");
        FFileHelper::SaveStringToFile(TEXT("gif"), *Gif);
        TestFalse(TEXT("GIF attachment is refused"), Store.ImportImage(Note, Gif, Error));

        // R5-fix L4: .jpeg is accepted and stored as .jpg.
        const FString Jpeg = OutputRoot / TEXT("photo.JPEG");
        FFileHelper::SaveStringToFile(TEXT("jpeg"), *Jpeg);
        FJJReviewAnnotation JpegNote = R5MakeNote(TEXT("note2"), 0);
        TestTrue(FString::Printf(TEXT("JPEG attachment imports (%s)"), *Error), Store.ImportImage(JpegNote, Jpeg, Error));
        if (TestEqual(TEXT("One JPEG path recorded"), JpegNote.Images.Num(), 1))
        {
            TestTrue(TEXT("JPEG copy uses .jpg"), JpegNote.Images[0].EndsWith(TEXT(".jpg")));
            TestTrue(TEXT("JPEG path is safe"), JJAnimReviewNotes::IsSafeImagePath(JpegNote.Images[0]));
            TestTrue(TEXT("JPEG copy exists"), IFileManager::Get().FileExists(*Store.GetImagePath(JpegNote.Images[0])));
        }

        Bundle.Annotations.Add(Note);
        TestTrue(FString::Printf(TEXT("Take 1 saves (%s)"), *Error), Store.Save(Bundle, Pose, Error));
        TakeDirA = Store.GetTakeDirectory(TEXT("take_1"));
        TestTrue(TEXT("Take folder uses take_1"), FPaths::GetCleanFilename(TakeDirA) == TEXT("take_1"));
        TestTrue(TEXT("Asset folder uses the D18 key"),
            FPaths::GetCleanFilename(FPaths::GetPath(TakeDirA)) == JJAnimReviewModel::AssetDirectoryKey(AssetA));
    }

    {
        // Simulate a 6-hex collision: asset B's folder already holds asset A's take.
        FJJAnimReviewStore Store;
        if (!TestTrue(TEXT("Store B opens"), Store.Open(Output, AssetB, Error)))
        {
            return false;
        }
        const FString TakeDirB = Store.GetTakeDirectory(TEXT("take_1"));
        IFileManager::Get().MakeDirectory(*TakeDirB, true);
        for (const TCHAR* File : { TEXT("review.json"), TEXT("pose.json"), TEXT("review.md") })
        {
            IFileManager::Get().Copy(*(TakeDirB / File), *(TakeDirA / File));
        }
        TArray<FJJReviewBundle> Takes;
        TestFalse(TEXT("Reading another asset's take is refused"), Store.ReadTakes(Takes, Error));
        FJJReviewBundle Bundle = R5MakeBundle(1);
        Bundle.Asset = AssetB;
        Bundle.ContentId = Pose.ContentId;
        TestFalse(TEXT("Writing over another asset's take is refused"), Store.Save(Bundle, Pose, Error));
        FString Json;
        FFileHelper::LoadFileToString(Json, *(TakeDirB / TEXT("review.json")));
        TestTrue(TEXT("Colliding review.json is left untouched"), Json.Contains(AssetA));
    }

    IFileManager::Get().DeleteDirectory(*OutputRoot, false, true);
    return true;
}

#endif
