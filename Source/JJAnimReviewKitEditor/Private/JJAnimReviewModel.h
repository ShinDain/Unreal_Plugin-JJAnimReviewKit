#pragma once

#include "JJAnimReviewTypes.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// Engine-object/render independent seams for Claude's synthetic-data tests.
namespace JJAnimReviewModel
{
    bool ValidateJsonShape(const TSharedPtr<FJsonObject>& Object, const UStruct* Type, FString& Error);

    template<typename T>
    bool ToJson(const T& Value, FString& Json)
    {
        return FJsonObjectConverter::UStructToJsonObjectString(Value, Json);
    }

    template<typename T>
    bool FromJson(const FString& Json, T& Value, FString& Error)
    {
        FText Reason;
        TSharedPtr<FJsonObject> Object;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Object) || !ValidateJsonShape(Object, T::StaticStruct(), Error))
        {
            if (Error.IsEmpty())
            {
                Error = TEXT("Invalid JSON object");
            }
            return false;
        }
        if (!FJsonObjectConverter::JsonObjectToUStruct(Object.ToSharedRef(), T::StaticStruct(), &Value, 0, 0, true, &Reason))
        {
            Error = Reason.ToString();
            return false;
        }
        if (Value.SchemaVersion != 1)
        {
            Error = TEXT("Unsupported review schema version");
            return false;
        }
        return true;
    }

    FString AssetDirectoryKey(const FString& CanonicalObjectPath);
    bool ComputeContentId(const FJJReviewPoseData& Pose, FString& ContentId, FString& Error);
    bool ValidateBundle(const FJJReviewBundle& Bundle, FString& Error);
    void SelectTake(FJJReviewBundle& NewBundle, const FJJReviewBundle* Latest, const FString& UtcNow);
    TArray<int32> SelectPrunableTakes(const TArray<FJJReviewBundle>& Takes, int32 KeepCount = 10);
    bool SetAnnotationState(TArray<FJJReviewBundle>& Takes, const FString& AnnotationId, const FString& Status);
    FString MakeMarkdown(const FJJReviewBundle& Bundle);
}
