#pragma once

#include "JJAnimReviewTypes.h"

namespace JJAnimReviewEditing
{
    // Pure schema-preserving operations. Origin changes only when editing a carried note.
    bool Apply(FJJReviewBundle& Bundle, FJJReviewAnnotation Annotation, const FString& Operation, FString& Error);
    TArray<FJJReviewPoseDifference> PoseInstructions(const FJJReviewPoseData& Original,
        const FJJReviewPoseSample& Preview, FString& SignedInstructions,
        double TranslationThreshold = 0.1, double RotationThreshold = 0.5);
}
