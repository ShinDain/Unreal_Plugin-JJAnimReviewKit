#include "JJAnimReviewModel.h"

#include "Misc/SecureHash.h"
#include "UObject/UnrealType.h"

namespace JJAnimReviewModelPrivate
{
    bool ValidateJsonValue(const TSharedPtr<FJsonValue>& Value, const FProperty* Property, FString& Error)
    {
        if (!Value.IsValid() || Value->Type == EJson::Null)
        {
            Error = TEXT("Null JSON property: ") + Property->GetName();
            return false;
        }
        if (const FStructProperty* Struct = CastField<FStructProperty>(Property))
        {
            if (Value->Type != EJson::Object)
            {
                Error = TEXT("Expected structured JSON property: ") + Property->GetName();
                return false;
            }
            return JJAnimReviewModel::ValidateJsonShape(Value->AsObject(), Struct->Struct, Error);
        }
        if (const FArrayProperty* Array = CastField<FArrayProperty>(Property))
        {
            if (Value->Type != EJson::Array)
            {
                Error = TEXT("Expected JSON array: ") + Property->GetName();
                return false;
            }
            for (const TSharedPtr<FJsonValue>& Entry : Value->AsArray())
            {
                if (!ValidateJsonValue(Entry, Array->Inner, Error))
                {
                    return false;
                }
            }
        }
        if (const FMapProperty* Map = CastField<FMapProperty>(Property))
        {
            if (Value->Type != EJson::Object)
            {
                Error = TEXT("Expected JSON map: ") + Property->GetName();
                return false;
            }
            for (const auto& Entry : Value->AsObject()->Values)
            {
                if (!ValidateJsonValue(Entry.Value, Map->ValueProp, Error))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void HashText(FMD5& Hash, const FString& Text)
    {
        const FTCHARToUTF8 Utf8(*Text);
        const FString Prefix = FString::Printf(TEXT("%d:"), Utf8.Length());
        const FTCHARToUTF8 PrefixUtf8(*Prefix);
        Hash.Update(reinterpret_cast<const uint8*>(PrefixUtf8.Get()), PrefixUtf8.Length());
        Hash.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
    }

    bool HashNumber(FMD5& Hash, double Value, double Precision)
    {
        if (!FMath::IsFinite(Value) || FMath::Abs(Value / Precision) > 9.e18)
        {
            return false;
        }
        const int64 Rounded = FMath::RoundToInt64(Value / Precision);
        HashText(Hash, FString::Printf(TEXT("%lld"), static_cast<long long>(Rounded)));
        return true;
    }

    bool HashTransform(FMD5& Hash, const FJJReviewTransform& Value)
    {
        if (!FMath::IsFinite(Value.RotationDegrees.X) || !FMath::IsFinite(Value.RotationDegrees.Y)
            || !FMath::IsFinite(Value.RotationDegrees.Z))
        {
            return false;
        }
        const FTransform Transform = Value.ToTransform();
        const FQuat Rotation = Transform.GetRotation().GetNormalized();
        const double Components[] =
        {
            Rotation.W, Rotation.X, Rotation.Y, Rotation.Z
        };
        int64 Rounded[4];
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Components); ++Index)
        {
            Rounded[Index] = FMath::RoundToInt64(FMath::Abs(Components[Index]) / 0.000001);
            if (Components[Index] < 0.0)
            {
                Rounded[Index] = -Rounded[Index];
            }
        }
        // Choose sign after rounding, so near-zero W cannot split +/-180 degrees.
        bool bNegative = false;
        for (const int64 Component : Rounded)
        {
            if (Component != 0)
            {
                bNegative = Component < 0;
                break;
            }
        }
        for (const int64 Component : Rounded)
        {
            const int64 Canonical = bNegative ? -Component : Component;
            HashText(Hash, FString::Printf(TEXT("%lld"), static_cast<long long>(Canonical)));
        }
        return HashNumber(Hash, Value.TranslationCm.X, 0.001)
            && HashNumber(Hash, Value.TranslationCm.Y, 0.001)
            && HashNumber(Hash, Value.TranslationCm.Z, 0.001)
            && HashNumber(Hash, Value.Scale.X, 0.000001)
            && HashNumber(Hash, Value.Scale.Y, 0.000001)
            && HashNumber(Hash, Value.Scale.Z, 0.000001);
    }

    FString OneLine(FString Text, int32 Limit = 240)
    {
        Text.ReplaceInline(TEXT("\r"), TEXT(" "));
        Text.ReplaceInline(TEXT("\n"), TEXT(" "));
        return Text.Left(Limit);
    }
}

bool JJAnimReviewModel::ValidateJsonShape(const TSharedPtr<FJsonObject>& Object, const UStruct* Type, FString& Error)
{
    if (!Object.IsValid())
    {
        Error = TEXT("Expected JSON object");
        return false;
    }
    int32 ExpectedFields = 0;
    for (TFieldIterator<FProperty> Property(Type); Property; ++Property)
    {
        ++ExpectedFields;
        const FString Name = FJsonObjectConverter::StandardizeCase(Property->GetName());
        const TSharedPtr<FJsonValue> Value = Object->TryGetField(Name);
        if (!Value.IsValid())
        {
            Error = TEXT("Missing required JSON field: ") + Name;
            return false;
        }
        if (!JJAnimReviewModelPrivate::ValidateJsonValue(Value, *Property, Error))
        {
            return false;
        }
    }
    if (Object->Values.Num() != ExpectedFields)
    {
        Error = TEXT("Unknown JSON fields; refusing a lossy rewrite");
        return false;
    }
    return true;
}

FString JJAnimReviewModel::AssetDirectoryKey(const FString& CanonicalObjectPath)
{
    const FString Lower = CanonicalObjectPath.ToLower();
    FString Readable;
    for (TCHAR Character : Lower)
    {
        if (FChar::IsAlnum(Character) && Character < 128)
        {
            Readable.AppendChar(Character);
        }
        else
        {
            Readable.AppendChar(TEXT('_'));
        }
    }
    FMD5 Hash;
    JJAnimReviewModelPrivate::HashText(Hash, Lower);
    uint8 Digest[16];
    Hash.Final(Digest);
    return Readable.Left(80) + TEXT("_") + BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
}

bool JJAnimReviewModel::ComputeContentId(const FJJReviewPoseData& Pose, FString& ContentId, FString& Error)
{
    using namespace JJAnimReviewModelPrivate;
    if (Pose.Samples.IsEmpty() || Pose.BoneNames.IsEmpty()
        || Pose.ParentIndices.Num() != Pose.BoneNames.Num()
        || Pose.RateNumerator <= 0 || Pose.RateDenominator <= 0)
    {
        Error = TEXT("Incomplete pose data");
        return false;
    }
    FMD5 Hash;
    HashText(Hash, TEXT("pose_md5_v2"));
    HashText(Hash, Pose.SamplingPolicy);
    HashText(Hash, FString::FromInt(Pose.RateNumerator));
    HashText(Hash, FString::FromInt(Pose.RateDenominator));
    if (!HashNumber(Hash, Pose.DurationSeconds, 0.000001))
    {
        Error = TEXT("Invalid duration");
        return false;
    }
    for (int32 Index = 0; Index < Pose.BoneNames.Num(); ++Index)
    {
        HashText(Hash, Pose.BoneNames[Index].ToString().ToLower());
        HashText(Hash, FString::FromInt(Pose.ParentIndices[Index]));
    }
    for (const FJJReviewPoseSample& Sample : Pose.Samples)
    {
        HashText(Hash, FString::FromInt(Sample.SourceFrame));
        if (Sample.ComponentTransforms.Num() != Pose.BoneNames.Num()
            || !HashNumber(Hash, Sample.TimeSeconds, 0.000001)
            || !HashTransform(Hash, Sample.RootMotion))
        {
            Error = TEXT("Invalid pose sample shape or numeric data");
            return false;
        }
        for (const FJJReviewTransform& Transform : Sample.ComponentTransforms)
        {
            if (!HashTransform(Hash, Transform))
            {
                Error = TEXT("Invalid transform in pose hash");
                return false;
            }
        }
    }
    HashText(Hash, TEXT("notifies"));
    for (double Time : Pose.NotifyTimesSeconds)
    {
        if (!HashNumber(Hash, Time, 0.000001))
        {
            Error = TEXT("Invalid notify time");
            return false;
        }
    }
    uint8 Digest[16];
    Hash.Final(Digest);
    ContentId = TEXT("pose_md5_v2:") + BytesToHex(Digest, UE_ARRAY_COUNT(Digest)).ToLower();
    return true;
}

bool JJAnimReviewModel::ValidateBundle(const FJJReviewBundle& Bundle, FString& Error)
{
    if (Bundle.SchemaVersion != 1 || Bundle.Asset.IsEmpty() || Bundle.TakeNumber <= 0
        || Bundle.Take != FString::Printf(TEXT("take_%06d"), Bundle.TakeNumber)
        || (!Bundle.ContentId.StartsWith(TEXT("pose_md5_v1:"))
            && !Bundle.ContentId.StartsWith(TEXT("pose_md5_v2:"))))
    {
        Error = TEXT("Invalid bundle identity/schema; refusing to overwrite existing data");
        return false;
    }
    TSet<FString> Ids;
    for (const FJJReviewAnnotation& Annotation : Bundle.Annotations)
    {
        if (Annotation.AnnotationId.IsEmpty() || Ids.Contains(Annotation.AnnotationId)
            || Annotation.AuthoredTake.IsEmpty() || Annotation.AuthoredContentId.IsEmpty()
            || (Annotation.Status != TEXT("open") && Annotation.Status != TEXT("resolved")))
        {
            Error = TEXT("Invalid annotation identity/status");
            return false;
        }
        Ids.Add(Annotation.AnnotationId);
    }
    return true;
}

void JJAnimReviewModel::SelectTake(FJJReviewBundle& NewBundle, const FJJReviewBundle* Latest, const FString& UtcNow)
{
    const bool bSameContent = Latest && Latest->ContentId == NewBundle.ContentId;
    NewBundle.TakeNumber = Latest ? Latest->TakeNumber + (bSameContent ? 0 : 1) : 1;
    NewBundle.Take = FString::Printf(TEXT("take_%06d"), NewBundle.TakeNumber);
    NewBundle.CreatedUtc = bSameContent ? Latest->CreatedUtc : UtcNow;
    NewBundle.UpdatedUtc = UtcNow;
    if (!Latest)
    {
        return;
    }
    if (bSameContent)
    {
        NewBundle.Annotations = Latest->Annotations;
        NewBundle.Images = Latest->Images;
        NewBundle.PoseDifferences = Latest->PoseDifferences;
        return;
    }
    for (const FJJReviewAnnotation& Annotation : Latest->Annotations)
    {
        if (Annotation.Status == TEXT("open"))
        {
            FJJReviewAnnotation Carried = Annotation;
            Carried.bFromPreviousVersion = true;
            NewBundle.Annotations.Add(MoveTemp(Carried));
        }
    }
}

TArray<int32> JJAnimReviewModel::SelectPrunableTakes(const TArray<FJJReviewBundle>& Takes, int32 KeepCount)
{
    TArray<int32> Order;
    for (int32 Index = 0; Index < Takes.Num(); ++Index)
    {
        Order.Add(Index);
    }
    Order.Sort([&Takes](int32 A, int32 B)
    {
        return Takes[A].TakeNumber < Takes[B].TakeNumber;
    });
    TArray<int32> Result;
    if (Order.IsEmpty())
    {
        return Result;
    }
    TSet<FString> ProtectedAuthoredTakes;
    for (const FJJReviewAnnotation& Annotation : Takes[Order.Last()].Annotations)
    {
        if (Annotation.Status == TEXT("open"))
        {
            ProtectedAuthoredTakes.Add(Annotation.AuthoredTake);
        }
    }
    int32 Remaining = Takes.Num();
    for (const int32 Index : Order)
    {
        if (Remaining <= FMath::Max(1, KeepCount) || Index == Order.Last())
        {
            break;
        }
        if (!ProtectedAuthoredTakes.Contains(Takes[Index].Take))
        {
            Result.Add(Index);
            --Remaining;
        }
    }
    return Result;
}

bool JJAnimReviewModel::SetAnnotationState(TArray<FJJReviewBundle>& Takes, const FString& AnnotationId, const FString& Status)
{
    if (Status != TEXT("open") && Status != TEXT("resolved"))
    {
        return false;
    }
    bool bFound = false;
    for (FJJReviewBundle& Take : Takes)
    {
        for (FJJReviewAnnotation& Annotation : Take.Annotations)
        {
            if (Annotation.AnnotationId == AnnotationId)
            {
                Annotation.Status = Status;
                bFound = true;
            }
        }
    }
    return bFound;
}

FString JJAnimReviewModel::MakeMarkdown(const FJJReviewBundle& Bundle)
{
    using namespace JJAnimReviewModelPrivate;
    // UTF-8 byte policy, not a tokenizer measurement. Reserve covers all omission counts.
    constexpr int32 ByteBudget = 6000;
    constexpr int32 FooterReserve = 240;
    enum ESection
    {
        Failures, Warnings, Current, Previous, Other, SectionCount
    };
    FString Output;
    FString PendingHeader;
    int32 Omitted[SectionCount] =
    {
        0
    };
    ESection Section = Other;
    bool bBudgetExhausted = false;
    auto Add = [&](auto MakeLine)
    {
        if (bBudgetExhausted)
        {
            ++Omitted[Section];
            return;
        }
        const FString Line = MakeLine();
        const FString Candidate = Output + PendingHeader + Line + TEXT("\n");
        if (FTCHARToUTF8(*Candidate).Length() <= ByteBudget - FooterReserve)
        {
            Output = Candidate;
            PendingHeader.Reset();
        }
        else
        {
            bBudgetExhausted = true;
            ++Omitted[Section];
        }
    };

    Add([&]()
    {
        return TEXT("# Animation review ") + OneLine(Bundle.Take, 40);
    });
    if (Bundle.bEstimatedProfile)
    {
        Add([]()
        {
            return FString(TEXT("WARNING: 추정 프로필 / estimated profile; verify bones and thresholds."));
        });
    }
    Add([&]()
    {
        return OneLine(Bundle.Asset, 160);
    });
    if (Bundle.CheckStatus == TEXT("not_run"))
    {
        Add([]()
        {
            return FString(TEXT("검사 없음 (not_run): R0 pose sampling only."));
        });
    }
    int32 Passed = 0;
    int32 Skipped = 0;
    TArray<FString> SkippedNames;
    for (const FJJReviewCheckResult& Check : Bundle.Checks)
    {
        Passed += Check.Status == TEXT("passed") ? 1 : 0;
        if (Check.Status == TEXT("skipped"))
        {
            ++Skipped;
            if (SkippedNames.Num() < 7)
            {
                SkippedNames.AddUnique(OneLine(Check.Check, 40));
            }
        }
    }
    Add([&]()
    {
        FString Summary = FString::Printf(TEXT("통과 합계 / passed: %d"), Passed);
        if (Skipped > 0)
        {
            Summary += FString::Printf(TEXT("; skipped: %d (%s)"),
                Skipped, *FString::Join(SkippedNames, TEXT(", ")));
        }
        return Summary;
    });
    constexpr int32 LinesPerCheck = 5;
    TMap<FString, int32> CheckLines;
    const TCHAR* Severities[] =
    {
        TEXT("failed"), TEXT("warning")
    };
    for (const TCHAR* Severity : Severities)
    {
        Section = FCString::Strcmp(Severity, TEXT("failed")) == 0 ? Failures : Warnings;
        for (const FJJReviewCheckResult& Check : Bundle.Checks)
        {
            if (Check.Status == Severity)
            {
                if (bBudgetExhausted || CheckLines.FindOrAdd(Check.Check) >= LinesPerCheck)
                {
                    ++Omitted[Section];
                    continue;
                }
                ++CheckLines.FindOrAdd(Check.Check);
                Add([&]()
                {
                    const FString Message = OneLine(Check.Message, 100);
                    FString Line = FString::Printf(TEXT("- %s %s %s %d-%df %.3f%s / %.3f"),
                        Severity, *OneLine(Check.Check, 40), *OneLine(Check.Bone.ToString(), 40),
                        Check.StartFrame, Check.EndFrame, Check.Measured, *OneLine(Check.Unit, 12),
                        Check.Threshold);
                    if (!Message.IsEmpty())
                    {
                        Line += TEXT(": ") + Message;
                    }
                    return Line;
                });
            }
        }
    }
    Section = Warnings;
    for (const FString& Warning : Bundle.Warnings)
    {
        Add([&]()
        {
            return TEXT("- WARNING: ") + OneLine(Warning);
        });
    }
    const bool PreviousStates[] =
    {
        false, true
    };
    for (bool bPrevious : PreviousStates)
    {
        Section = bPrevious ? Previous : Current;
        PendingHeader = bPrevious ? TEXT("## 이전 지적(미해결)\n") : TEXT("## 이번 지적\n");
        for (const FJJReviewAnnotation& Annotation : Bundle.Annotations)
        {
            const bool bOld = Annotation.AuthoredContentId != Bundle.ContentId || Annotation.AuthoredTake != Bundle.Take;
            if (Annotation.Status == TEXT("open") && bOld == bPrevious)
            {
                Add([&]()
                {
                    return FString::Printf(TEXT("- %df %s %s %s: %s%s"), Annotation.Frame,
                        *OneLine(Annotation.Bone.ToString(), 40), *OneLine(Annotation.Direction, 10),
                        *OneLine(Annotation.Amount, 10), *OneLine(Annotation.Text, 160),
                        bOld ? TEXT(" [이전 버전에서 작성]") : TEXT(""));
                });
                for (const FJJReviewPoseDifference& Instruction : Annotation.PoseInstructions)
                {
                    Add([&]()
                    {
                        return FString::Printf(TEXT("  pose instruction %s %df %.3fcm %.3fdeg (signed parent-space delta in review.json annotation text)"),
                            *OneLine(Instruction.Bone.ToString(), 40), Instruction.Frame,
                            Instruction.TranslationCm, Instruction.RotationDegrees);
                    });
                }
            }
        }
        PendingHeader.Reset();
    }
    Section = Other;
    for (const FJJReviewPoseDifference& Difference : Bundle.PoseDifferences)
    {
        Add([&]()
        {
            return FString::Printf(TEXT("- pose delta %s %df %.3fcm %.3fdeg"),
                *OneLine(Difference.Bone.ToString(), 40), Difference.Frame,
                Difference.TranslationCm, Difference.RotationDegrees);
        });
    }
    for (const FString& Image : Bundle.Images)
    {
        Add([&]()
        {
            return TEXT("- image: ") + OneLine(Image, 160);
        });
    }
    int64 TotalOmitted = 0;
    for (const int32 Count : Omitted)
    {
        TotalOmitted += Count;
    }
    Output += FString::Printf(
        TEXT("\nOmitted: %lld. failures=%d warnings=%d current=%d previous=%d other=%d. Full data: review.json; samples: pose.json.\n"),
        static_cast<long long>(TotalOmitted),
        Omitted[Failures], Omitted[Warnings], Omitted[Current], Omitted[Previous], Omitted[Other]);
    return Output;
}
