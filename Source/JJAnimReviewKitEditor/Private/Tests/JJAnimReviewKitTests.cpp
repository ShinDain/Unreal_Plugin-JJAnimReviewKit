#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/FileManager.h"
#include "JJAnimReviewEngineCompat.h"
#include "JJAnimReviewModel.h"
#include "JJAnimReviewStore.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

#include <limits>

namespace JJAnimReviewKitTestsPrivate
{
    constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

    FJJReviewTransform MakeTransform(double X, double Roll = 0.0, double Pitch = 0.0, double Yaw = 0.0)
    {
        FJJReviewTransform Result;
        Result.TranslationCm.X = X;
        Result.RotationDegrees.X = Roll;
        Result.RotationDegrees.Y = Pitch;
        Result.RotationDegrees.Z = Yaw;
        Result.Scale.X = 1.0;
        Result.Scale.Y = 1.0;
        Result.Scale.Z = 1.0;
        return Result;
    }

    FJJReviewPoseData MakePose(double ChildOffset)
    {
        FJJReviewPoseData Pose;
        Pose.DurationSeconds = 2.0 / 30.0;
        Pose.BoneNames = { FName(TEXT("root")), FName(TEXT("child")) };
        Pose.ParentIndices = { -1, 0 };
        Pose.NotifyTimesSeconds = { 1.0 / 30.0 };
        for (int32 Frame = 0; Frame < 3; ++Frame)
        {
            FJJReviewPoseSample Sample;
            Sample.SourceFrame = Frame;
            Sample.TimeSeconds = Frame / 30.0;
            Sample.ComponentTransforms.Add(MakeTransform(0.0));
            Sample.ComponentTransforms.Add(MakeTransform(10.0 + ChildOffset * Frame));
            Sample.RootMotion = MakeTransform(0.0);
            Pose.Samples.Add(Sample);
        }

        return Pose;
    }

    FString ContentIdOf(const FJJReviewPoseData& Pose)
    {
        FString Id;
        FString Error;
        JJAnimReviewModel::ComputeContentId(Pose, Id, Error);
        return Id;
    }

    FJJReviewAnnotation MakeAnnotation(const FString& Id, const FString& Take, const FString& ContentId,
        const FString& Status = TEXT("open"))
    {
        FJJReviewAnnotation Annotation;
        Annotation.AnnotationId = Id;
        Annotation.AuthoredTake = Take;
        Annotation.AuthoredContentId = ContentId;
        Annotation.Status = Status;
        Annotation.Frame = 1;
        Annotation.Bone = FName(TEXT("child"));
        Annotation.Text = TEXT("raise the arm");
        return Annotation;
    }

    FJJReviewBundle MakeBundle(int32 TakeNumber, const FString& ContentId)
    {
        FJJReviewBundle Bundle;
        Bundle.Asset = TEXT("/Game/ClaudeTest/Fake.Fake");
        Bundle.TakeNumber = TakeNumber;
        Bundle.Take = FString::Printf(TEXT("take_%06d"), TakeNumber);
        Bundle.ContentId = ContentId;
        Bundle.CreatedUtc = TEXT("2026-11-07T00:00:00Z");
        Bundle.UpdatedUtc = Bundle.CreatedUtc;
        return Bundle;
    }

    bool HasOnlySafeCharacters(const FString& Text)
    {
        for (TCHAR Character : Text)
        {
            const bool bSafe = (Character >= TEXT('a') && Character <= TEXT('z'))
                || (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_');
            if (!bSafe)
            {
                return false;
            }
        }

        return true;
    }
}

using namespace JJAnimReviewKitTestsPrivate;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewA1AssetPathTest,
    "JJAnimReviewKit.Porting.A1_NormalizeAssetPath", TestFlags)

bool FJJAnimReviewA1AssetPathTest::RunTest(const FString& Parameters)
{
    FString Path;
    FString Error;
    TestTrue(TEXT("Package path is accepted"), JJAnimReviewEngineCompat::NormalizeAssetPath(TEXT("/Game/Anims/Walk"), Path, Error));
    TestEqual(TEXT("Package path gains the asset name"), Path, FString(TEXT("/Game/Anims/Walk.Walk")));

    TestTrue(TEXT("Object path is accepted"), JJAnimReviewEngineCompat::NormalizeAssetPath(TEXT(" /Game/Anims/Walk.Walk "), Path, Error));
    TestEqual(TEXT("Object path is trimmed and kept"), Path, FString(TEXT("/Game/Anims/Walk.Walk")));

    TestTrue(TEXT("Export text path is accepted"),
        JJAnimReviewEngineCompat::NormalizeAssetPath(TEXT("/Script/Engine.AnimSequence'/Game/Anims/Walk.Walk'"), Path, Error));
    TestEqual(TEXT("Export text path is unwrapped"), Path, FString(TEXT("/Game/Anims/Walk.Walk")));

    TestFalse(TEXT("Empty path is rejected"), JJAnimReviewEngineCompat::NormalizeAssetPath(TEXT(""), Path, Error));
    TestFalse(TEXT("Filesystem path is rejected"),
        JJAnimReviewEngineCompat::NormalizeAssetPath(TEXT("C:/Temp/Walk.uasset"), Path, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewContentIdTest,
    "JJAnimReviewKit.Model.ContentIdHash", TestFlags)

bool FJJAnimReviewContentIdTest::RunTest(const FString& Parameters)
{
    const FJJReviewPoseData Base = MakePose(1.0);
    const FString BaseId = ContentIdOf(Base);
    // R0-fix (M2/L1): hash policy is v2 (Raw root motion + canonical quaternion sign).
    TestTrue(TEXT("Content id has the v2 prefix"), BaseId.StartsWith(TEXT("pose_md5_v2:")));
    TestEqual(TEXT("Same pose gives the same id"), ContentIdOf(MakePose(1.0)), BaseId);

    FJJReviewPoseData Tiny = Base;
    Tiny.Samples[1].ComponentTransforms[1].TranslationCm.X += 0.0004;
    TestEqual(TEXT("Change below 0.001cm keeps the id"), ContentIdOf(Tiny), BaseId);

    FJJReviewPoseData Moved = Base;
    Moved.Samples[1].ComponentTransforms[1].TranslationCm.X += 0.01;
    TestNotEqual(TEXT("Change of 0.01cm changes the id"), ContentIdOf(Moved), BaseId);

    FJJReviewPoseData YawPlus = Base;
    YawPlus.Samples[0].ComponentTransforms[1] = MakeTransform(10.0, 0.0, 0.0, 180.0);
    FJJReviewPoseData YawMinus = Base;
    YawMinus.Samples[0].ComponentTransforms[1] = MakeTransform(10.0, 0.0, 0.0, -180.0);
    TestEqual(TEXT("Yaw 180 and -180 (q and -q) hash the same"), ContentIdOf(YawPlus), ContentIdOf(YawMinus));

    // R0-fix L1: a tiny W of either sign rounds to zero before the sign is chosen.
    FJJReviewPoseData NearPlus = Base;
    NearPlus.Samples[0].ComponentTransforms[1] = MakeTransform(10.0, 0.0, 0.0, 179.99999999);
    FJJReviewPoseData NearMinus = Base;
    NearMinus.Samples[0].ComponentTransforms[1] = MakeTransform(10.0, 0.0, 0.0, -179.99999999);
    TestEqual(TEXT("Yaw just below +180 and just above -180 hash the same"), ContentIdOf(NearPlus), ContentIdOf(NearMinus));

    FJJReviewPoseData NegativeZero = Base;
    NegativeZero.Samples[0].ComponentTransforms[0].TranslationCm.Y = -0.0;
    TestEqual(TEXT("Negative zero hashes like zero"), ContentIdOf(NegativeZero), BaseId);

    FJJReviewPoseData NotifyMoved = Base;
    NotifyMoved.NotifyTimesSeconds[0] += 0.01;
    TestNotEqual(TEXT("Moving a notify changes the id"), ContentIdOf(NotifyMoved), BaseId);

    FJJReviewPoseData NotFinite = Base;
    NotFinite.Samples[2].ComponentTransforms[1].TranslationCm.Z = std::numeric_limits<double>::quiet_NaN();
    FString Id;
    FString Error;
    TestFalse(TEXT("Non-finite value is rejected"), JJAnimReviewModel::ComputeContentId(NotFinite, Id, Error));

    FJJReviewPoseData BadShape = Base;
    BadShape.Samples[0].ComponentTransforms.Pop();
    TestFalse(TEXT("Sample with a missing bone is rejected"), JJAnimReviewModel::ComputeContentId(BadShape, Id, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewAssetKeyTest,
    "JJAnimReviewKit.Model.AssetDirectoryKey", TestFlags)

bool FJJAnimReviewAssetKeyTest::RunTest(const FString& Parameters)
{
    const FString KeyA = JJAnimReviewModel::AssetDirectoryKey(TEXT("/Game/Anims/Walk.Walk"));
    const FString KeyB = JJAnimReviewModel::AssetDirectoryKey(TEXT("/Game/Anims/Run.Run"));
    TestEqual(TEXT("Key is deterministic"), JJAnimReviewModel::AssetDirectoryKey(TEXT("/Game/Anims/Walk.Walk")), KeyA);
    TestNotEqual(TEXT("Different assets get different keys"), KeyA, KeyB);
    TestTrue(TEXT("Key contains only safe characters"), HasOnlySafeCharacters(KeyA));

    const FString LongPath = TEXT("/Game/") + FString::ChrN(200, TEXT('a')) + TEXT(".Asset");
    const FString LongKey = JJAnimReviewModel::AssetDirectoryKey(LongPath);
    TestTrue(TEXT("Long path key is bounded (80 + 1 + 32)"), LongKey.Len() <= 113);

    const FString SimilarA = JJAnimReviewModel::AssetDirectoryKey(TEXT("/Game/") + FString::ChrN(100, TEXT('a')) + TEXT("1.X"));
    const FString SimilarB = JJAnimReviewModel::AssetDirectoryKey(TEXT("/Game/") + FString::ChrN(100, TEXT('a')) + TEXT("2.X"));
    TestNotEqual(TEXT("Paths sharing the 80-char readable prefix still differ"), SimilarA, SimilarB);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewTakeLifecycleTest,
    "JJAnimReviewKit.Model.TakeLifecycle", TestFlags)

bool FJJAnimReviewTakeLifecycleTest::RunTest(const FString& Parameters)
{
    const FString IdA = ContentIdOf(MakePose(1.0));
    const FString IdB = ContentIdOf(MakePose(2.0));

    FJJReviewBundle First = MakeBundle(0, IdA);
    JJAnimReviewModel::SelectTake(First, nullptr, TEXT("T1"));
    TestEqual(TEXT("First review is take 1"), First.TakeNumber, 1);
    TestEqual(TEXT("Take name is zero padded"), First.Take, FString(TEXT("take_000001")));
    First.Annotations.Add(MakeAnnotation(TEXT("a-open"), First.Take, IdA));
    First.Annotations.Add(MakeAnnotation(TEXT("a-resolved"), First.Take, IdA, TEXT("resolved")));
    First.Images.Add(TEXT("sheet.png"));

    FJJReviewBundle Same = MakeBundle(0, IdA);
    JJAnimReviewModel::SelectTake(Same, &First, TEXT("T2"));
    TestEqual(TEXT("Same content keeps the take number"), Same.TakeNumber, 1);
    TestEqual(TEXT("Same content keeps CreatedUtc"), Same.CreatedUtc, FString(TEXT("T1")));
    TestEqual(TEXT("Same content updates UpdatedUtc"), Same.UpdatedUtc, FString(TEXT("T2")));
    TestEqual(TEXT("Same content keeps all annotations"), Same.Annotations.Num(), 2);
    TestEqual(TEXT("Same content keeps images"), Same.Images.Num(), 1);

    FJJReviewBundle Changed = MakeBundle(0, IdB);
    JJAnimReviewModel::SelectTake(Changed, &First, TEXT("T3"));
    TestEqual(TEXT("New content creates the next take"), Changed.TakeNumber, 2);
    TestEqual(TEXT("Only open annotations are carried"), Changed.Annotations.Num(), 1);
    if (Changed.Annotations.Num() == 1)
    {
        const FJJReviewAnnotation& Carried = Changed.Annotations[0];
        TestEqual(TEXT("Carried annotation keeps its id"), Carried.AnnotationId, FString(TEXT("a-open")));
        TestEqual(TEXT("Carried annotation keeps its authored take"), Carried.AuthoredTake, FString(TEXT("take_000001")));
        TestEqual(TEXT("Carried annotation keeps its authored content"), Carried.AuthoredContentId, IdA);
        TestTrue(TEXT("Carried annotation is marked as from a previous version"), Carried.bFromPreviousVersion);
    }

    TestEqual(TEXT("New content does not carry images"), Changed.Images.Num(), 0);

    FString Error;
    TestTrue(TEXT("Carried bundle validates"), JJAnimReviewModel::ValidateBundle(Changed, Error));

    TArray<FJJReviewBundle> Takes = { First, Changed };
    TestTrue(TEXT("Resolving updates every take"), JJAnimReviewModel::SetAnnotationState(Takes, TEXT("a-open"), TEXT("resolved")));
    TestEqual(TEXT("Take 1 copy resolved"), Takes[0].Annotations[0].Status, FString(TEXT("resolved")));
    TestEqual(TEXT("Take 2 copy resolved"), Takes[1].Annotations[0].Status, FString(TEXT("resolved")));
    TestFalse(TEXT("Unknown status is rejected"), JJAnimReviewModel::SetAnnotationState(Takes, TEXT("a-open"), TEXT("done")));
    TestFalse(TEXT("Unknown id is reported"), JJAnimReviewModel::SetAnnotationState(Takes, TEXT("missing"), TEXT("open")));

    FJJReviewBundle Duplicate = First;
    Duplicate.Annotations.Add(MakeAnnotation(TEXT("a-open"), First.Take, IdA));
    TestFalse(TEXT("Duplicate annotation id is invalid"), JJAnimReviewModel::ValidateBundle(Duplicate, Error));

    FJJReviewBundle WrongName = First;
    WrongName.Take = TEXT("take_1");
    TestFalse(TEXT("Take name must match the number"), JJAnimReviewModel::ValidateBundle(WrongName, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewRetentionTest,
    "JJAnimReviewKit.Model.TakeRetention", TestFlags)

bool FJJAnimReviewRetentionTest::RunTest(const FString& Parameters)
{
    const FString Id = ContentIdOf(MakePose(1.0));
    auto MakeTakes = [&Id](int32 Count)
    {
        TArray<FJJReviewBundle> Takes;
        for (int32 Number = Count; Number >= 1; --Number)
        {
            Takes.Add(MakeBundle(Number, Id));
        }

        return Takes;
    };
    auto PrunedNumbers = [](const TArray<FJJReviewBundle>& Takes, const TArray<int32>& Indices)
    {
        TArray<int32> Numbers;
        for (int32 Index : Indices)
        {
            Numbers.Add(Takes[Index].TakeNumber);
        }

        Numbers.Sort();
        return Numbers;
    };

    TArray<FJJReviewBundle> Twelve = MakeTakes(12);
    TestTrue(TEXT("12 takes without open notes prune the two oldest"),
        PrunedNumbers(Twelve, JJAnimReviewModel::SelectPrunableTakes(Twelve)) == TArray<int32>({ 1, 2 }));

    // D15 after U-R0-1=A: only the latest take and the authored takes of notes still open in the latest take are protected.
    TArray<FJJReviewBundle> WithOpen = MakeTakes(12);
    for (FJJReviewBundle& Take : WithOpen)
    {
        Take.Annotations.Add(MakeAnnotation(TEXT("keep"), TEXT("take_000001"), Id));
        Take.Annotations.Last().bFromPreviousVersion = Take.TakeNumber != 1;
    }
    TestTrue(TEXT("Authored take of a note open in the latest survives; carried-only intermediates are pruned"),
        PrunedNumbers(WithOpen, JJAnimReviewModel::SelectPrunableTakes(WithOpen)) == TArray<int32>({ 2, 3 }));

    TArray<FJJReviewBundle> OpenOnlyInOld = MakeTakes(12);
    for (FJJReviewBundle& Take : OpenOnlyInOld)
    {
        if (Take.TakeNumber == 1)
        {
            Take.Annotations.Add(MakeAnnotation(TEXT("old"), Take.Take, Id));
        }
    }
    TestTrue(TEXT("An old open copy not present in the latest does not protect its take"),
        PrunedNumbers(OpenOnlyInOld, JJAnimReviewModel::SelectPrunableTakes(OpenOnlyInOld)) == TArray<int32>({ 1, 2 }));

    TArray<FJJReviewBundle> ResolvedInLatest = MakeTakes(12);
    for (FJJReviewBundle& Take : ResolvedInLatest)
    {
        const bool bLatest = Take.TakeNumber == 12;
        Take.Annotations.Add(MakeAnnotation(TEXT("keep"), TEXT("take_000001"), Id, bLatest ? TEXT("resolved") : TEXT("open")));
    }
    TestTrue(TEXT("Resolving in the latest releases the authored take"),
        PrunedNumbers(ResolvedInLatest, JJAnimReviewModel::SelectPrunableTakes(ResolvedInLatest)) == TArray<int32>({ 1, 2 }));

    TArray<FJJReviewBundle> AllOpen = MakeTakes(12);
    for (FJJReviewBundle& Take : AllOpen)
    {
        Take.Annotations.Add(MakeAnnotation(TEXT("keep"), Take.Take, Id));
    }
    TestTrue(TEXT("Each take's own open note does not protect it unless the latest still has it"),
        PrunedNumbers(AllOpen, JJAnimReviewModel::SelectPrunableTakes(AllOpen)) == TArray<int32>({ 1, 2 }));

    TArray<FJJReviewBundle> Ten = MakeTakes(10);
    TestEqual(TEXT("At the limit nothing is pruned"), JJAnimReviewModel::SelectPrunableTakes(Ten).Num(), 0);

    TArray<FJJReviewBundle> KeepOne = MakeTakes(3);
    const TArray<int32> Numbers = PrunedNumbers(KeepOne, JJAnimReviewModel::SelectPrunableTakes(KeepOne, 0));
    TestFalse(TEXT("The latest take is never pruned"), Numbers.Contains(3));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewMarkdownBudgetTest,
    "JJAnimReviewKit.Model.MarkdownBudgetAndOrder", TestFlags)

bool FJJAnimReviewMarkdownBudgetTest::RunTest(const FString& Parameters)
{
    const FString IdA = ContentIdOf(MakePose(1.0));
    const FString IdB = ContentIdOf(MakePose(2.0));
    FJJReviewBundle Bundle = MakeBundle(2, IdB);
    Bundle.bEstimatedProfile = true;
    Bundle.CheckStatus = TEXT("failed");

    FJJReviewCheckResult Warning;
    Warning.Check = TEXT("pop");
    Warning.Status = TEXT("warning");
    FJJReviewCheckResult Failure;
    Failure.Check = TEXT("foot_slide");
    Failure.Status = TEXT("failed");
    FJJReviewCheckResult Passed;
    Passed.Check = TEXT("loop");
    Passed.Status = TEXT("passed");
    Bundle.Checks = { Warning, Failure, Passed };

    Bundle.Annotations.Add(MakeAnnotation(TEXT("old"), TEXT("take_000001"), IdA));
    Bundle.Annotations.Last().bFromPreviousVersion = true;
    Bundle.Annotations.Add(MakeAnnotation(TEXT("new"), Bundle.Take, IdB));

    const FString Small = JJAnimReviewModel::MakeMarkdown(Bundle);
    TestTrue(TEXT("Estimated profile warning is present"), Small.Contains(TEXT("estimated profile")));
    const int32 FailedAt = Small.Find(TEXT("foot_slide"));
    const int32 WarningAt = Small.Find(TEXT("- warning pop"));
    TestTrue(TEXT("Failures are listed before warnings"), FailedAt != INDEX_NONE && WarningAt != INDEX_NONE && FailedAt < WarningAt);
    TestTrue(TEXT("Passed checks are summarized"), Small.Contains(TEXT("passed: 1")));
    const int32 CurrentSection = Small.Find(TEXT("## 이번 지적"));
    const int32 PreviousSection = Small.Find(TEXT("## 이전 지적(미해결)"));
    const int32 PreviousNote = Small.Find(TEXT("[이전 버전에서 작성]"));
    TestTrue(TEXT("Carried note is listed in the previous section"),
        CurrentSection != INDEX_NONE && PreviousSection > CurrentSection && PreviousNote > PreviousSection);
    TestTrue(TEXT("Nothing omitted for a small bundle"), Small.Contains(TEXT("Omitted: 0.")));

    for (int32 Index = 0; Index < 300; ++Index)
    {
        Bundle.Annotations.Add(MakeAnnotation(FString::Printf(TEXT("n%d"), Index), Bundle.Take, IdB));
    }

    const FString Large = JJAnimReviewModel::MakeMarkdown(Bundle);
    const int32 Bytes = FTCHARToUTF8(*Large).Length();
    // R0-fix M3 (U-R0-3=A): 6000-byte budget including footer, per-section omitted counts.
    TestTrue(FString::Printf(TEXT("Large bundle stays within 6000 UTF-8 bytes (%d)"), Bytes), Bytes <= 6000);
    TestFalse(TEXT("Large bundle reports omitted lines"), Large.Contains(TEXT("Omitted: 0.")));
    TestFalse(TEXT("Omitted current notes are counted per section"), Large.Contains(TEXT("current=0")));
    TestTrue(TEXT("No failures were omitted"), Large.Contains(TEXT("failures=0")));
    TestTrue(TEXT("Failure line survives truncation"), Large.Contains(TEXT("foot_slide")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewJsonTest,
    "JJAnimReviewKit.Model.JsonRoundTripAndValidation", TestFlags)

bool FJJAnimReviewJsonTest::RunTest(const FString& Parameters)
{
    const FString Id = ContentIdOf(MakePose(1.0));
    FJJReviewBundle Bundle = MakeBundle(3, Id);
    Bundle.Annotations.Add(MakeAnnotation(TEXT("a1"), Bundle.Take, Id));

    FString Json;
    TestTrue(TEXT("Bundle serializes"), JJAnimReviewModel::ToJson(Bundle, Json));
    FJJReviewBundle Loaded;
    FString Error;
    TestTrue(FString::Printf(TEXT("Bundle round trips (%s)"), *Error), JJAnimReviewModel::FromJson(Json, Loaded, Error));
    TestEqual(TEXT("Take survives"), Loaded.Take, Bundle.Take);
    TestEqual(TEXT("Content id survives"), Loaded.ContentId, Id);
    TestEqual(TEXT("Annotation survives"), Loaded.Annotations.Num(), 1);

    FJJReviewPoseData Pose = MakePose(1.0);
    Pose.ContentId = Id;
    FString PoseJson;
    FJJReviewPoseData LoadedPose;
    TestTrue(TEXT("Pose serializes"), JJAnimReviewModel::ToJson(Pose, PoseJson));
    TestTrue(TEXT("Pose round trips"), JJAnimReviewModel::FromJson(PoseJson, LoadedPose, Error));
    TestEqual(TEXT("Round-tripped pose hashes the same"), ContentIdOf(LoadedPose), Id);

    TSharedPtr<FJsonObject> Object;
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object);
    if (!TestTrue(TEXT("Serialized bundle parses as an object"), Object.IsValid()))
    {
        return false;
    }

    auto Write = [](const TSharedPtr<FJsonObject>& Source)
    {
        FString Out;
        FJsonSerializer::Serialize(Source.ToSharedRef(), TJsonWriterFactory<>::Create(&Out));
        return Out;
    };

    TSharedPtr<FJsonObject> Extra = MakeShared<FJsonObject>(*Object);
    Extra->SetStringField(TEXT("futureField"), TEXT("x"));
    TestFalse(TEXT("Unknown field is rejected"), JJAnimReviewModel::FromJson(Write(Extra), Loaded, Error));

    TSharedPtr<FJsonObject> Missing = MakeShared<FJsonObject>(*Object);
    Missing->RemoveField(TEXT("take"));
    TestFalse(TEXT("Missing field is rejected"), JJAnimReviewModel::FromJson(Write(Missing), Loaded, Error));

    TSharedPtr<FJsonObject> Future = MakeShared<FJsonObject>(*Object);
    Future->SetNumberField(TEXT("schemaVersion"), 2);
    TestFalse(TEXT("Unsupported schema version is rejected"), JJAnimReviewModel::FromJson(Write(Future), Loaded, Error));

    TestFalse(TEXT("Garbage is rejected"), JJAnimReviewModel::FromJson(TEXT("{not json"), Loaded, Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJJAnimReviewStoreTest,
    "JJAnimReviewKit.Porting.A9_StoreLockAndRecovery", TestFlags)

bool FJJAnimReviewStoreTest::RunTest(const FString& Parameters)
{
    FString Root;
    FString Error;
    TestTrue(TEXT("Relative output inside the review root is allowed"),
        FJJAnimReviewStore::ResolveOutputRoot(TEXT("Sub"), Root, Error));
    TestFalse(TEXT("Relative path escaping the root is rejected"),
        FJJAnimReviewStore::ResolveOutputRoot(TEXT("../Outside"), Root, Error));
    TestFalse(TEXT("Absolute path outside the root is rejected"),
        FJJAnimReviewStore::ResolveOutputRoot(TEXT("C:/Windows/Temp"), Root, Error));

    const FString Output = TEXT("ClaudeTests_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Asset = TEXT("/Game/ClaudeTest/Fake.Fake");
    FString OutputRoot;
    FJJAnimReviewStore::ResolveOutputRoot(Output, OutputRoot, Error);

    {
        FJJAnimReviewStore Store;
        if (!TestTrue(FString::Printf(TEXT("Store opens (%s)"), *Error), Store.Open(Output, Asset, Error)))
        {
            return false;
        }

        FJJAnimReviewStore Second;
        TestFalse(TEXT("A second writer on the same asset is refused"), Second.Open(Output, Asset, Error));

        FJJReviewPoseData Pose = MakePose(1.0);
        Pose.ContentId = ContentIdOf(Pose);
        FJJReviewBundle Bundle = MakeBundle(1, Pose.ContentId);
        Bundle.Asset = Asset;
        Bundle.Annotations.Add(MakeAnnotation(TEXT("a1"), Bundle.Take, Pose.ContentId));
        TestTrue(FString::Printf(TEXT("Take 1 saves (%s)"), *Error), Store.Save(Bundle, Pose, Error));

        FJJReviewBundle Mismatch = Bundle;
        Mismatch.ContentId = ContentIdOf(MakePose(5.0));
        TestFalse(TEXT("Save refuses a bundle whose content id does not match the pose"), Store.Save(Mismatch, Pose, Error));

        TArray<FJJReviewBundle> Takes;
        TestTrue(TEXT("Takes read back"), Store.ReadTakes(Takes, Error));
        TestEqual(TEXT("One take on disk"), Takes.Num(), 1);
        FJJReviewPoseData ReadBack;
        TestTrue(TEXT("Pose reads back with a matching id"), Takes.Num() == 1 && Store.ReadPose(Takes[0], ReadBack, Error));

        const FString TakeDir = Store.GetTakeDirectory(Bundle.Take);
        const TCHAR* Files[] = { TEXT("pose.json"), TEXT("review.md"), TEXT("review.json") };
        for (const TCHAR* File : Files)
        {
            IFileManager::Get().Copy(*(TakeDir / File + TEXT(".previous")), *(TakeDir / File));
        }

        FFileHelper::SaveStringToFile(TEXT("{ torn write"), *(TakeDir / TEXT("review.json")));
        FFileHelper::SaveStringToFile(TEXT("existing"), *(TakeDir / TEXT(".transaction")));
        TestTrue(FString::Printf(TEXT("Interrupted existing-take transaction recovers (%s)"), *Error), Store.ReadTakes(Takes, Error));
        TestEqual(TEXT("Recovered take is readable"), Takes.Num(), 1);
        TestFalse(TEXT("Transaction marker is cleared"), IFileManager::Get().FileExists(*(TakeDir / TEXT(".transaction"))));

        const FString NewDir = Store.GetTakeDirectory(TEXT("take_000002"));
        IFileManager::Get().MakeDirectory(*NewDir, true);
        FFileHelper::SaveStringToFile(TEXT("{ partial"), *(NewDir / TEXT("pose.json")));
        FFileHelper::SaveStringToFile(TEXT("new"), *(NewDir / TEXT(".transaction")));
        TestTrue(TEXT("Interrupted new-take transaction is skipped"), Store.ReadTakes(Takes, Error));
        TestEqual(TEXT("Only the committed take is listed"), Takes.Num(), 1);
        TestFalse(TEXT("Partial file of the new take is removed"), IFileManager::Get().FileExists(*(NewDir / TEXT("pose.json"))));
    }

    {
        FJJAnimReviewStore Reopened;
        TestTrue(TEXT("Lock is released when the store is destroyed"), Reopened.Open(Output, Asset, Error));
    }

    IFileManager::Get().DeleteDirectory(*OutputRoot, false, true);
    return true;
}

#endif
