#pragma once

#include "CoreMinimal.h"
#include "JJAnimReviewTypes.h"
#include "JJAnimReviewChecks.h"
#include "JJAnimReviewCaptureSettings.h"
#include "JJAnimReviewNotes.h"
#include "UObject/StrongObjectPtr.h"

class UAnimSequence;
class UJJAnimReviewProfile;
class IAnimationEditor;
class USkeletalMesh;
class UMaterial;
class USkinnedMeshComponent;
class UDebugSkelMeshComponent;

// A27: owned by the editor module; no material exists until the first ghost request.
struct FJJReviewGhostMaterialState
{
    TStrongObjectPtr<UMaterial> Material;
    bool bAttempted = false;
    bool bResubmitted = false;
};

// Asset/animation API boundary. Assumptions A1-A4 in PORTING.md.
namespace JJAnimReviewEngineCompat
{
    FJJReviewGhostMaterialState& GhostMaterialState();
    UMaterial* GhostMaterial(bool& bPending, FString& Reason);
    FTransform PreviewGhostBasis(UDebugSkelMeshComponent* Mesh);
    bool GhostRootLock(UDebugSkelMeshComponent* Mesh, const FJJReviewPoseData& Pose,
        double Time, FTransform& RawRoot, FTransform& LockedRoot);
    TSet<FName> ModifiedPreviewBones(UDebugSkelMeshComponent* Mesh);
    bool ResetPreviewBones(UDebugSkelMeshComponent* Mesh, FString& Message);
    bool NormalizeInstructionRoot(UAnimSequence* Animation, UDebugSkelMeshComponent* Mesh,
        FJJReviewPoseData& Original, FString& Error);
    bool WeightedBones(USkeletalMesh* Mesh, TSet<FName>& Bones);
    UMaterial* VertexColorMaterial(USkinnedMeshComponent* Mesh, FString& Reason);
    bool VisualizationMaterialReady(UMaterial* Material, USkinnedMeshComponent* Mesh, FString& Reason);
    bool BoneWeightMaterialReady(USkinnedMeshComponent* Mesh, FString& Reason);
    bool BuildVertexColors(USkeletalMesh* Mesh, const TMap<FName, FJJReviewBoneColor>& Colors,
        TMap<int32, TArray<FLinearColor>>& LODColors, FString& Reason, bool* bNoTargets = nullptr);
    bool NormalizeAssetPath(const FString& Input, FString& ObjectPath, FString& Error);
    UAnimSequence* LoadAnimation(const FString& ObjectPath, FString& Error);
    bool ResolveProfile(UAnimSequence* Animation, const FString& ProfilePath,
        FJJReviewProfileSettings& Settings, bool& bEstimated, FString& Error);
    bool GetSourceTimes(UAnimSequence* Animation, TArray<double>& Times, FString& Error);
    bool Sample(UAnimSequence* Animation, const TArray<double>& Times,
        FJJReviewPoseData& Output, FString& Error);
    FJJReviewCheckContext CheckContext(UAnimSequence* Animation, const FJJReviewProfileSettings& Profile);
    bool CaptureSheet(UAnimSequence* Animation, const FJJReviewPoseData& Pose,
        const FJJReviewCaptureSettings& Settings, const FString& Filename, FString& Reason);
    bool CaptureSheetWithNotes(UAnimSequence* Animation, const FJJReviewPoseData& Pose,
        const FJJReviewCaptureSettings& Settings, const TArray<FJJReviewAnnotation>& Notes,
        const FString& Filename, FString& Reason);
    bool LoadBoneGroups(UAnimSequence* Animation, TArray<FJJReviewBoneGroup>& User,
        TArray<FJJReviewBoneGroup>& Resolved, bool& bTemplateMatched, FString& Error);
    bool SaveBoneGroups(UAnimSequence* Animation, const TArray<FJJReviewBoneGroup>& User, FString& Error);
    void SkeletonBones(UAnimSequence* Animation, TArray<FName>& Names, TArray<int32>& Parents);
    void RegisterPersona();
    void UnregisterPersona();
    void OpenAnnotations(TWeakPtr<IAnimationEditor> Editor);
    void CloseAnnotationTabs();
}
