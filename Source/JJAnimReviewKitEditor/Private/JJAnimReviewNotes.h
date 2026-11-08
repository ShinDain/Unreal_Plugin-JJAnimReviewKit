#pragma once

#include "JJAnimReviewTypes.h"

struct FJJReviewBoneGroup
{
    FString Name;
    FString Side = TEXT("center");
    TArray<FName> Bones;
};

struct FJJReviewBoneSegment
{
    int32 Bone = INDEX_NONE;
    int32 Parent = INDEX_NONE;
};

struct FJJReviewVisibleNote
{
    int32 Index = INDEX_NONE;
    bool bSelected = false;
    TArray<FName> Bones;
};

struct FJJReviewBoneColor
{
    FLinearColor Color = FLinearColor::Gray;
    int32 Priority = MAX_int32;
};

struct FJJReviewVertexInfluence
{
    FName Bone;
    float Weight = 0;
};

enum class EJJReviewGroupOperation : uint8
{
    Save,
    Rename,
    Delete
};

struct FJJReviewGroupChange
{
    TArray<FJJReviewBoneGroup> UserGroups;
    FString FinalName;
    FString Error;
    bool bNoOp = false;
};

struct FJJReviewVisibilityBounds
{
    int64 FromFrame = MIN_int64;
    int64 UntilFrame = MAX_int64;
};

// Pure value/JSON operations, with no asset, editor, renderer or filesystem access.
namespace JJAnimReviewNotes
{
    bool AppliesPreviewRootMotion(bool bIgnoreMode, bool bEnableRootMotion, bool bSimulatingPhysics);
    bool LocksGhostRoot(bool bIgnoreMode, bool bEnableRootMotion, bool bForceRootLock, bool bSimulatingPhysics);
    FTransform RebaseRoot(const FTransform& Transform, const FTransform& RawRoot, const FTransform& LockedRoot);
    TArray<bool> JointMarkers(const TArray<FName>& Bones);
    // UE composes local * parent: remove only the preview's applied motion, not the saved take's root.
    FTransform GhostBasis(const FTransform& ComponentToWorld, const FTransform& AppliedRootMotion);
    FTransform OffsetGhostBasis(const FTransform& Basis, double ForwardYawDegrees, double SpacingCm);
    TArray<FJJReviewPoseDifference> FilterPoseInstructions(const TArray<FJJReviewPoseDifference>& Candidates,
        const TSet<FName>& ModifiedBones);
    FJJReviewGroupChange ChangeGroup(const TArray<FJJReviewBoneGroup>& User,
        const TArray<FJJReviewBoneGroup>& Template, const TArray<FJJReviewBoneGroup>& Resolved,
        const FString& TargetName, const FJJReviewBoneGroup& Requested, EJJReviewGroupOperation Operation);
    FJJReviewVisibilityBounds VisibilityBounds(const TArray<FJJReviewAnnotation>& Notes,
        int32 Frame, const FString& SelectedId);
    // Empty result means that at least one mesh bone has no saved sample.
    TArray<int32> MapGhostBones(const TArray<FName>& MeshBones, const TArray<FName>& SampleBones);
    bool ComponentToLocalPose(const TArray<int32>& Parents, const TArray<FTransform>& ComponentPose,
        TArray<FTransform>& LocalPose);
    FLinearColor GroupColor(const FString& Name);
    FString UniqueGroupName(const FString& Requested, const TArray<FJJReviewBoneGroup>& Groups);
    TArray<FJJReviewBoneGroup> RestoreDefaultGroups(const TArray<FJJReviewBoneGroup>& User,
        const TArray<FJJReviewBoneGroup>& Template);
    // nullptr means unavailable mesh data; an available empty set draws no lines.
    TArray<FJJReviewBoneSegment> WeightedSegments(const TArray<FName>& Bones,
        const TArray<int32>& Parents, const TSet<FName>* WeightedBones);
    TArray<FJJReviewVisibleNote> VisibleNotes(const TArray<FJJReviewAnnotation>& Notes,
        int32 Frame, const FString& SelectedId);
    TMap<FName, FJJReviewBoneColor> BoneColors(const TArray<FJJReviewBoneGroup>& Groups,
        const TArray<FJJReviewAnnotation>& Notes, const TArray<FJJReviewVisibleNote>& Visible,
        bool bShowGroups, bool bShowSelection, const TArray<FName>& Selection,
        const FString& SelectionGroup);
    FLinearColor VertexColor(const TArray<FJJReviewVertexInfluence>& Influences,
        const TMap<FName, FJJReviewBoneColor>& Colors);
    int32 EndFrame(const FJJReviewAnnotation& Note);
    bool ValidateRange(const FJJReviewAnnotation& Note, int32 LastFrame, FString& Error);
    bool IsSafeImagePath(const FString& Path);
    TArray<FName> Targets(const FJJReviewAnnotation& Note);
    FString RangeText(const FJJReviewAnnotation& Note);
    FString AnnotationLine(const FJJReviewAnnotation& Note);
    TArray<FString> GroupLegend(const TArray<FJJReviewAnnotation>& Notes);
    TArray<FString> UnreferencedImageFolders(const TArray<FString>& Folders, const TArray<FJJReviewBundle>& Remaining);
    FString InferSide(const TArray<FName>& Bones);
    FLinearColor SideColor(const FString& Side);
    TArray<FJJReviewBoneGroup> MannequinTemplate(const TArray<FName>& Bones, const TArray<int32>& Parents, bool& bMatched);
    TArray<FJJReviewBoneGroup> ResolveGroups(const TArray<FName>& SkeletonBones,
        const TArray<FJJReviewBoneGroup>& Template, const TArray<FJJReviewBoneGroup>& User);
    bool GroupsToJson(const FString& Skeleton, const TArray<FJJReviewBoneGroup>& Groups, FString& Json, FString& Error);
    bool GroupsFromJson(const FString& Json, const FString& Skeleton, TArray<FJJReviewBoneGroup>& Groups, FString& Error);
}
