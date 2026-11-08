#include "JJAnimReviewStore.h"

#include "JJAnimReviewModel.h"
#include "JJAnimReviewNotes.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace JJAnimReviewStorePrivate
{
    FString RecoveryHint(const FString& Directory)
    {
        return TEXT(" Manual recovery: move this take folder outside the asset directory (preserve it, do not delete it), then run the review again: ")
            + Directory;
    }

    const TCHAR* const CoreFiles[] =
    {
        TEXT("pose.json"), TEXT("review.md"), TEXT("review.json")
    };

    FString FullPath(const FString& Input)
    {
        FString Result = FPaths::ConvertRelativePathToFull(Input);
        FPaths::NormalizeDirectoryName(Result);
        FPaths::CollapseRelativeDirectories(Result);
        return Result;
    }

    bool SaveText(const FString& Path, const FString& Text)
    {
        return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }

    bool Copy(const FString& Destination, const FString& Source)
    {
        return IFileManager::Get().Copy(*Destination, *Source, true, false, false) == COPY_OK;
    }

    bool ReadAssetIdentity(const FString& Path, FString& Asset)
    {
        FString Json;
        TSharedPtr<FJsonObject> Object;
        return FFileHelper::LoadFileToString(Json, *Path)
            && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Object)
            && Object && Object->TryGetStringField(TEXT("asset"), Asset);
    }
}

bool FJJAnimReviewStore::ResolveOutputRoot(const FString& Requested, FString& Output, FString& Error)
{
    using namespace JJAnimReviewStorePrivate;
    const FString Allowed = FullPath(FPaths::ProjectSavedDir() / TEXT("JJAnimReview"));
    Output = Allowed;
    if (!Requested.IsEmpty())
    {
        Output = FullPath(FPaths::IsRelative(Requested) ? Allowed / Requested : Requested);
    }
    if (!Output.Equals(Allowed, ESearchCase::IgnoreCase) && !FPaths::IsUnderDirectory(Output, Allowed))
    {
        Error = TEXT("Output must remain inside Project/Saved/JJAnimReview");
        return false;
    }
    return true;
}

bool FJJAnimReviewStore::Open(const FString& OutputRoot, const FString& Asset, FString& Error)
{
    bReadOnly = false;
    FString CheckedRoot;
    if (!ResolveOutputRoot(OutputRoot, CheckedRoot, Error))
    {
        return false;
    }
    CanonicalAsset = Asset;
    AssetDirectory = CheckedRoot / JJAnimReviewModel::AssetDirectoryKey(Asset);
    if (!IFileManager::Get().MakeDirectory(*AssetDirectory, true))
    {
        Error = TEXT("Cannot create output directory: ") + AssetDirectory;
        return false;
    }
    // Win64 file writer opens with no sharing (no FILEWRITE_AllowRead).
    // Existing stale lock files are harmless: the OS handle owns the lock.
    WriteLock.Reset(IFileManager::Get().CreateFileWriter(*(AssetDirectory / TEXT(".writer.lock"))));
    if (!WriteLock)
    {
        Error = TEXT("Another process owns this asset bundle, or output is not writable: ") + AssetDirectory;
        return false;
    }
    return true;
}

bool FJJAnimReviewStore::OpenReadOnly(const FString& OutputRoot, const FString& Asset, FString& Error)
{
    WriteLock.Reset();
    bReadOnly = true;
    FString CheckedRoot;
    if (!ResolveOutputRoot(OutputRoot, CheckedRoot, Error))
    {
        return false;
    }
    CanonicalAsset = Asset;
    AssetDirectory = CheckedRoot / JJAnimReviewModel::AssetDirectoryKey(Asset);
    return true;
}

FString FJJAnimReviewStore::GetTakeDirectory(const FString& Take) const
{
    return AssetDirectory / Take;
}

bool FJJAnimReviewStore::IsOwnedTake(const FString& Directory) const
{
    const FString Full = JJAnimReviewStorePrivate::FullPath(Directory);
    const FString Name = FPaths::GetCleanFilename(Full);
    int32 Number = 0;
    return FPaths::GetPath(Full).Equals(AssetDirectory, ESearchCase::IgnoreCase)
        && JJAnimReviewModel::ParseTakeName(Name, Number);
}

bool FJJAnimReviewStore::Recover(const FString& Directory, FString& Error)
{
    using namespace JJAnimReviewStorePrivate;
    const FString Marker = Directory / TEXT(".transaction");
    if (!IFileManager::Get().FileExists(*Marker))
    {
        return true;
    }
    FString Kind;
    if (!IsOwnedTake(Directory) || !FFileHelper::LoadFileToString(Kind, *Marker)
        || (Kind != TEXT("existing") && Kind != TEXT("new")))
    {
        Error = TEXT("Invalid transaction marker; preserve files for manual recovery: ") + Directory;
        return false;
    }
    for (const TCHAR* File : CoreFiles)
    {
        const FString FinalPath = Directory / File;
        if (Kind == TEXT("existing"))
        {
            if (!Copy(FinalPath, FinalPath + TEXT(".previous")))
            {
                Error = TEXT("Cannot restore interrupted bundle transaction: ") + FinalPath;
                return false;
            }
        }
        else if (!IFileManager::Get().Delete(*FinalPath, false, false, true))
        {
            Error = TEXT("Cannot remove incomplete new-take file: ") + FinalPath;
            return false;
        }
    }
    if (!IFileManager::Get().Delete(*Marker, true, false, true))
    {
        Error = TEXT("Cannot finish transaction recovery: ") + Marker;
        return false;
    }
    return true;
}

bool FJJAnimReviewStore::ReadTakes(TArray<FJJReviewBundle>& Takes, FString& Error)
{
    Takes.Reset();
    TArray<FString> Directories;
    IFileManager::Get().FindFiles(Directories, *(AssetDirectory / TEXT("take_*")), false, true);
    for (const FString& Name : Directories)
    {
        const FString Directory = GetTakeDirectory(Name);
        if (!IsOwnedTake(Directory))
        {
            continue;
        }
        const FString ReviewPath = Directory / TEXT("review.json");
        const bool bInterrupted = IFileManager::Get().FileExists(*(Directory / TEXT(".transaction")));
        FString TransactionKind;
        if (bInterrupted)
        {
            FFileHelper::LoadFileToString(TransactionKind, *(Directory / TEXT(".transaction")));
        }
        if (IFileManager::Get().FileExists(*ReviewPath))
        {
            FString ExistingAsset;
            const bool bReadable = JJAnimReviewStorePrivate::ReadAssetIdentity(ReviewPath, ExistingAsset);
            if ((bReadable && !ExistingAsset.Equals(CanonicalAsset, ESearchCase::IgnoreCase))
                || (!bReadable && (!bInterrupted || TransactionKind != TEXT("existing"))))
            {
                Error = TEXT("Asset folder collision or unreadable identity; no recovery/write allowed: ") + ReviewPath;
                return false;
            }
        }
        if (bInterrupted && TransactionKind == TEXT("existing"))
        {
            FString PreviousAsset;
            if (!JJAnimReviewStorePrivate::ReadAssetIdentity(ReviewPath + TEXT(".previous"), PreviousAsset)
                || !PreviousAsset.Equals(CanonicalAsset, ESearchCase::IgnoreCase))
            {
                Error = TEXT("Transaction backup asset identity mismatch; preserve files: ") + Directory;
                return false;
            }
        }
        if (bReadOnly && bInterrupted)
        {
            Error = TEXT("Bundle transaction in progress; retry after the writer finishes: ") + Directory
                + TEXT(". 크래시로 남은 마커라면 같은 에셋의 리뷰를 한 번 다시 실행하면 복구된다.");
            return false;
        }
        if (!bReadOnly && !Recover(Directory, Error))
        {
            Error += JJAnimReviewStorePrivate::RecoveryHint(Directory);
            return false;
        }
        if (!IFileManager::Get().FileExists(*ReviewPath))
        {
            // Staging-only directories have never committed a review.
            if (bInterrupted || IFileManager::Get().FileExists(*(Directory / TEXT(".uncommitted"))))
            {
                continue;
            }
            Error = TEXT("Take is missing review.json: ") + Directory + JJAnimReviewStorePrivate::RecoveryHint(Directory);
            return false;
        }
        FString Json;
        FJJReviewBundle Bundle;
        if (!FFileHelper::LoadFileToString(Json, *ReviewPath)
            || !JJAnimReviewModel::FromJson(Json, Bundle, Error)
            || !JJAnimReviewModel::ValidateBundle(Bundle, Error))
        {
            Error = TEXT("Cannot read existing take ") + Name + TEXT(": ") + Error
                + JJAnimReviewStorePrivate::RecoveryHint(Directory);
            return false;
        }
        if (!Bundle.Asset.Equals(CanonicalAsset, ESearchCase::IgnoreCase) || Bundle.Take != Name)
        {
            Error = TEXT("Take asset/path identity mismatch: ") + ReviewPath + JJAnimReviewStorePrivate::RecoveryHint(Directory);
            return false;
        }
        Takes.Add(MoveTemp(Bundle));
    }
    Takes.Sort([](const FJJReviewBundle& A, const FJJReviewBundle& B)
    {
        return A.TakeNumber < B.TakeNumber;
    });
    return true;
}

bool FJJAnimReviewStore::ReadPose(const FJJReviewBundle& Bundle, FJJReviewPoseData& Pose, FString& Error) const
{
    FString Json;
    FString ActualId;
    const FString Directory = GetTakeDirectory(Bundle.Take);
    if (Bundle.ContentId.StartsWith(TEXT("pose_md5_v1:")))
    {
        Error = TEXT("Legacy v1 pose verification is unavailable in the v2 reader. Run the review again to create a v2 take; preserve the v1 folder: ")
            + Directory;
        return false;
    }
    if (!IsOwnedTake(Directory) || !FFileHelper::LoadFileToString(Json, *(Directory / TEXT("pose.json")))
        || !JJAnimReviewModel::FromJson(Json, Pose, Error)
        || !JJAnimReviewModel::ComputeContentId(Pose, ActualId, Error)
        || Pose.ContentId != Bundle.ContentId || ActualId != Bundle.ContentId)
    {
        Error = TEXT("Pose/review identity mismatch or unreadable pose: ") + Directory + TEXT(" ") + Error
            + JJAnimReviewStorePrivate::RecoveryHint(Directory);
        return false;
    }
    return true;
}

bool FJJAnimReviewStore::Save(const FJJReviewBundle& Bundle, const FJJReviewPoseData& Pose, FString& Error)
{
    using namespace JJAnimReviewStorePrivate;
    const FString Directory = GetTakeDirectory(Bundle.Take);
    FString Computed;
    if (!WriteLock || !Bundle.Asset.Equals(CanonicalAsset, ESearchCase::IgnoreCase)
        || Pose.SchemaVersion != 2 || !IsOwnedTake(Directory) || !JJAnimReviewModel::ValidateBundle(Bundle, Error)
        || !JJAnimReviewModel::ComputeContentId(Pose, Computed, Error)
        || Computed != Bundle.ContentId || Pose.ContentId != Bundle.ContentId)
    {
        Error = TEXT("Refusing to save an invalid bundle: ") + Error;
        return false;
    }
    // Refuse collisions before recovery or staging can rewrite committed data.
    TArray<FJJReviewBundle> ExistingTakes;
    if (!ReadTakes(ExistingTakes, Error))
    {
        return false;
    }
    if (!IFileManager::Get().MakeDirectory(*Directory, true) || !Recover(Directory, Error))
    {
        Error = TEXT("Cannot prepare take directory: ") + Directory + TEXT(" ") + Error;
        return false;
    }
    FString PoseJson;
    FString ReviewJson;
    if (!JJAnimReviewModel::ToJson(Pose, PoseJson) || !JJAnimReviewModel::ToJson(Bundle, ReviewJson))
    {
        Error = TEXT("Bundle serialization failed");
        return false;
    }
    const FString Texts[] =
    {
        PoseJson, JJAnimReviewModel::MakeMarkdown(Bundle), ReviewJson
    };
    const bool bExisting = IFileManager::Get().FileExists(*(Directory / TEXT("review.json")));
    if (!bExisting && !SaveText(Directory / TEXT(".uncommitted"), TEXT("new")))
    {
        Error = TEXT("Cannot mark new staging directory");
        return false;
    }
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(CoreFiles); ++Index)
    {
        const FString Path = Directory / CoreFiles[Index];
        if (!SaveText(Path + TEXT(".next"), Texts[Index])
            || (bExisting && !Copy(Path + TEXT(".previous"), Path)))
        {
            Error = TEXT("Cannot stage bundle; committed files preserved: ") + Path;
            return false;
        }
    }
    const FString Marker = Directory / TEXT(".transaction");
    if (!SaveText(Marker, bExisting ? TEXT("existing") : TEXT("new")))
    {
        Error = TEXT("Cannot create transaction marker");
        return false;
    }
    for (const TCHAR* File : CoreFiles)
    {
        const FString Path = Directory / File;
        if (!Copy(Path, Path + TEXT(".next")))
        {
            FString RecoveryError;
            const bool bRecovered = Recover(Directory, RecoveryError);
            Error = TEXT("Bundle write failed: ") + Path;
            if (!bRecovered)
            {
                Error += TEXT("; recovery required: ") + RecoveryError;
            }
            return false;
        }
    }
    // review.json is written last; marker removal commits all three files.
    if (!IFileManager::Get().Delete(*Marker, true, false, true))
    {
        Error = TEXT("Bundle transaction not committed; will restore previous state next time");
        return false;
    }
    for (const TCHAR* File : CoreFiles)
    {
        const FString Path = Directory / File;
        IFileManager::Get().Delete(*(Path + TEXT(".next")), false, false, true);
        IFileManager::Get().Delete(*(Path + TEXT(".previous")), false, false, true);
    }
    IFileManager::Get().Delete(*(Directory / TEXT(".uncommitted")), false, false, true);
    return true;
}

void FJJAnimReviewStore::Prune(const TArray<FJJReviewBundle>& Takes, TArray<FString>& Warnings)
{
    if (bReadOnly || !WriteLock)
    {
        Warnings.Add(TEXT("Retention cleanup skipped: writer lock required."));
        return;
    }
    for (int32 Index : JJAnimReviewModel::SelectPrunableTakes(Takes))
    {
        const FString Directory = GetTakeDirectory(Takes[Index].Take);
        if (!IsOwnedTake(Directory) || !IFileManager::Get().DeleteDirectory(*Directory, false, true))
        {
            Warnings.Add(TEXT("Retention cleanup skipped/failed: ") + Directory);
        }
    }
    // Re-read after deletion: a failed take deletion must keep its image references.
    TArray<FJJReviewBundle> Remaining;
    FString Error;
    if (!ReadTakes(Remaining, Error))
    {
        Warnings.Add(TEXT("Image cleanup skipped: ") + Error);
        return;
    }
    TArray<FString> Folders;
    IFileManager::Get().FindFiles(Folders, *(AssetDirectory / TEXT("notes/*")), false, true);
    for (const FString& Folder : JJAnimReviewNotes::UnreferencedImageFolders(Folders, Remaining))
    {
        const FString Directory = AssetDirectory / TEXT("notes") / Folder;
        if (!IFileManager::Get().DeleteDirectory(*Directory, false, true))
        {
            Warnings.Add(TEXT("Image cleanup failed: ") + Directory);
        }
    }
}

FString FJJAnimReviewStore::GetImagePath(const FString& Relative) const
{
    return JJAnimReviewNotes::IsSafeImagePath(Relative) ? AssetDirectory / Relative : FString();
}

bool FJJAnimReviewStore::ImportImage(FJJReviewAnnotation& Annotation, const FString& Source, FString& Error)
{
    FString Extension = FPaths::GetExtension(Source).ToLower();
    if (Extension == TEXT("jpeg"))
    {
        Extension = TEXT("jpg");
    }
    const FString Relative = TEXT("notes/") + Annotation.AnnotationId + TEXT("/")
        + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".") + Extension;
    const FString Destination = GetImagePath(Relative);
    if (bReadOnly || !WriteLock || Destination.IsEmpty() || (Extension != TEXT("png") && Extension != TEXT("jpg"))
        || !IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true)
        || IFileManager::Get().Copy(*Destination, *Source, false, false, false) != COPY_OK)
    {
        Error = TEXT("Cannot copy PNG/JPG annotation attachment.");
        return false;
    }
    Annotation.Images.Add(Relative);
    return true;
}
