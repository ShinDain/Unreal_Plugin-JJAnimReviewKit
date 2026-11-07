#pragma once

#include "CoreMinimal.h"
#include "JJAnimReviewTypes.h"
#include "JJAnimReviewChecks.h"
#include "JJAnimReviewCaptureSettings.h"

class UAnimSequence;
class UJJAnimReviewProfile;
class IAnimationEditor;

// Asset/animation API boundary. Assumptions A1-A4 in PORTING.md.
namespace JJAnimReviewEngineCompat
{
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
    void RegisterPersona();
    void UnregisterPersona();
    void OpenAnnotations(TWeakPtr<IAnimationEditor> Editor);
    void CloseAnnotationTabs();
}
