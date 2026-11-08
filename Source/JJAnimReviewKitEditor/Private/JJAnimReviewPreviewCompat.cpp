#include "JJAnimReviewEngineCompat.h"
#include "JJAnimReviewComparison.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSequenceHelpers.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "AnimPreviewInstance.h"
#include "AnimPose.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"

FTransform JJAnimReviewEngineCompat::PreviewGhostBasis(UDebugSkelMeshComponent* Mesh)
{
    check(IsInGameThread());
    FTransform AppliedRootMotion = FTransform::Identity;
    UAnimPreviewInstance* Preview = Mesh->PreviewInstance;
    // A31: Ignore / disabled motion applies no root delta; physics uses a conservative public guard.
    if (Preview && JJAnimReviewNotes::AppliesPreviewRootMotion(
        Mesh->GetProcessRootMotionMode() == EProcessRootMotionMode::Ignore,
        Mesh->DoesCurrentAssetHaveRootMotion(), Mesh->IsAnySimulatingPhysics()))
    {
        if (const UAnimSequence* Animation = Cast<UAnimSequence>(Preview->GetCurrentAsset()))
        {
            // Match ResetRootTransform / LoopAndReset's public extraction route, including mirroring.
            // Loop may retain earlier cycles / floor wrapping in the resulting basis (A31).
            const FTransform Initial = UE::Anim::ExtractRootTransformFromAnimationAsset(Animation, 0.0f);
            AppliedRootMotion = UE::Anim::ExtractRootMotionFromAnimationAsset(
                Animation, Preview->GetMirrorDataTable(), 0.0f, Preview->GetCurrentTime()) * Initial;
        }
    }
    return JJAnimReviewNotes::GhostBasis(Mesh->GetComponentTransform(), AppliedRootMotion);
}

bool JJAnimReviewEngineCompat::GhostRootLock(UDebugSkelMeshComponent* Mesh,
    const FJJReviewPoseData& Pose, double Time, FTransform& RawRoot, FTransform& LockedRoot)
{
    check(IsInGameThread());
    const UAnimSequence* Animation = Mesh->PreviewInstance
        ? Cast<UAnimSequence>(Mesh->PreviewInstance->GetCurrentAsset()) : nullptr;
    if (!Animation || !JJAnimReviewNotes::LocksGhostRoot(
        Mesh->GetProcessRootMotionMode() == EProcessRootMotionMode::Ignore,
        Animation->bEnableRootMotion, Animation->bForceRootLock, Mesh->IsAnySimulatingPhysics()))
    {
        return false;
    }
    const int32 Root = Pose.ParentIndices.IndexOfByKey(INDEX_NONE);
    const USkeletalMesh* PreviewMesh = Mesh->GetSkeletalMeshAsset();
    if (Root == INDEX_NONE || Pose.Samples.IsEmpty() || !PreviewMesh)
    {
        return false;
    }
    // A31/A32: settings belong to the current asset; first-frame data belongs to this ghost take.
    RawRoot = JJAnimReviewComparison::AtTime(Pose, Root, Time);
    switch (Animation->RootMotionRootLock)
    {
    case ERootMotionRootLock::AnimFirstFrame:
        LockedRoot = JJAnimReviewComparison::AtTime(Pose, Root, 0.0);
        break;
    case ERootMotionRootLock::Zero:
        LockedRoot = FTransform::Identity;
        break;
    default:
        LockedRoot = PreviewMesh->GetRefSkeleton().GetRefBonePose()[0];
        break;
    }
    return true;
}

TSet<FName> JJAnimReviewEngineCompat::ModifiedPreviewBones(UDebugSkelMeshComponent* Mesh)
{
    check(IsInGameThread());
    TSet<FName> Result;
    if (Mesh && Mesh->PreviewInstance)
    {
        // A30: these are Persona's manual controllers, not curve controllers or tree selection.
        for (const FAnimNode_ModifyBone& Controller : Mesh->PreviewInstance->GetBoneControllers())
        {
            Result.Add(Controller.BoneToModify.BoneName);
        }
    }
    return Result;
}

bool JJAnimReviewEngineCompat::ResetPreviewBones(UDebugSkelMeshComponent* Mesh, FString& Message)
{
    check(IsInGameThread());
    UAnimPreviewInstance* Preview = Mesh ? Mesh->PreviewInstance.Get() : nullptr;
    if (!Preview || Preview->GetBoneControllers().IsEmpty())
    {
        Message = TEXT("초기화할 본 수정이 없습니다.");
        return false;
    }
    if (!GEditor)
    {
        Message = TEXT("본 수정 초기화: 에디터 트랜잭션을 사용할 수 없습니다.");
        return false;
    }

    // A30 / D27: same transaction order as Skeleton Tree's Reset All Bone Transforms.
    GEditor->BeginTransaction(FText::FromString(TEXT("본 수정 초기화")));
    Preview->SetFlags(RF_Transactional);
    Preview->Modify();
    Preview->ResetModifiedBone();
    GEditor->EndTransaction();
    Message = TEXT("본 수정을 초기화했습니다. Ctrl+Z로 되돌릴 수 있습니다.");
    return true;
}

bool JJAnimReviewEngineCompat::NormalizeInstructionRoot(UAnimSequence* Animation,
    UDebugSkelMeshComponent* Mesh, FJJReviewPoseData& Original, FString& Error)
{
    check(IsInGameThread());
    if (!Animation->HasRootMotion() && !Animation->bForceRootLock)
    {
        return true;
    }
    // A32: preview extraction locks the root even when component processing mode is Ignore.
    // Evaluate the same lock policy without manual controllers; never compare locked root to raw motion.
    FAnimPoseEvaluationOptions Options;
    Options.EvaluationType = EAnimDataEvalType::Raw;
    Options.bExtractRootMotion = Animation->HasRootMotion();
    Options.bIncorporateRootMotionIntoPose = false;
    Options.bEvaluateCurves = false;
    Options.OptionalSkeletalMesh = Mesh->GetSkeletalMeshAsset();
    FAnimPose Pose;
    UAnimPoseExtensions::GetAnimPoseAtTime(Animation,
        Mesh->PreviewInstance->GetCurrentTime(), Options, Pose);
    if (!UAnimPoseExtensions::IsValid(Pose))
    {
        Error = TEXT("Cannot evaluate the preview root-lock baseline.");
        return false;
    }

    const int32 RootIndex = Original.ParentIndices.IndexOfByKey(INDEX_NONE);
    if (RootIndex == INDEX_NONE || Original.Samples.Num() != 1)
    {
        Error = TEXT("Pose instruction source is missing its root.");
        return false;
    }
    const FTransform LockedRoot = UAnimPoseExtensions::GetBonePose(
        Pose, Original.BoneNames[RootIndex], EAnimPoseSpaces::World);
    const FTransform RawRoot = Original.Samples[0].ComponentTransforms[RootIndex].ToTransform();
    if (LockedRoot.ContainsNaN())
    {
        Error = TEXT("Preview root-lock baseline contains a non-finite transform.");
        return false;
    }
    // Rebase all bones together so every non-root parent-space difference stays unchanged.
    for (FJJReviewTransform& Transform : Original.Samples[0].ComponentTransforms)
    {
        Transform = FJJReviewTransform::FromTransform(
            JJAnimReviewNotes::RebaseRoot(Transform.ToTransform(), RawRoot, LockedRoot));
    }
    return true;
}
