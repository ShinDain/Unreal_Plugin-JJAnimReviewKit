#include "JJAnimReviewNotes.h"

bool JJAnimReviewNotes::AppliesPreviewRootMotion(bool bIgnoreMode, bool bEnableRootMotion, bool bSimulatingPhysics)
{
    return !bIgnoreMode && bEnableRootMotion && !bSimulatingPhysics;
}

bool JJAnimReviewNotes::LocksGhostRoot(bool bIgnoreMode, bool bEnableRootMotion,
    bool bForceRootLock, bool bSimulatingPhysics)
{
    return !bSimulatingPhysics && (bEnableRootMotion || bForceRootLock)
        && !AppliesPreviewRootMotion(bIgnoreMode, bEnableRootMotion, bSimulatingPhysics);
}

FTransform JJAnimReviewNotes::RebaseRoot(const FTransform& Transform,
    const FTransform& RawRoot, const FTransform& LockedRoot)
{
    return Transform.GetRelativeTransform(RawRoot) * LockedRoot;
}

TArray<bool> JJAnimReviewNotes::JointMarkers(const TArray<FName>& Bones)
{
    TArray<bool> Result;
    Result.Reserve(Bones.Num());
    for (FName Bone : Bones)
    {
        TArray<FString> Tokens;
        Bone.ToString().ToLower().ParseIntoArray(Tokens, TEXT("_"), true);
        const bool bDetail = Tokens.Contains(TEXT("twist")) || Tokens.Contains(TEXT("finger"))
            || Tokens.Contains(TEXT("thumb")) || Tokens.Contains(TEXT("index"))
            || Tokens.Contains(TEXT("middle")) || Tokens.Contains(TEXT("ring"))
            || Tokens.Contains(TEXT("pinky")) || Tokens.Contains(TEXT("little"));
        Result.Add(!bDetail);
    }
    return Result;
}

FTransform JJAnimReviewNotes::GhostBasis(const FTransform& ComponentToWorld, const FTransform& AppliedRootMotion)
{
    return AppliedRootMotion.Inverse() * ComponentToWorld;
}

FTransform JJAnimReviewNotes::OffsetGhostBasis(const FTransform& Basis,
    double ForwardYawDegrees, double SpacingCm)
{
    const FVector Right = FRotator(0, ForwardYawDegrees + 90.0, 0).Vector();
    FTransform Result = Basis;
    Result.AddToTranslation(Basis.TransformVector(Right * SpacingCm));
    return Result;
}

TArray<FJJReviewPoseDifference> JJAnimReviewNotes::FilterPoseInstructions(
    const TArray<FJJReviewPoseDifference>& Candidates, const TSet<FName>& ModifiedBones)
{
    TArray<FJJReviewPoseDifference> Result;
    for (const FJJReviewPoseDifference& Candidate : Candidates)
    {
        if (ModifiedBones.Contains(Candidate.Bone))
        {
            Result.Add(Candidate);
        }
    }
    return Result;
}

FJJReviewGroupChange JJAnimReviewNotes::ChangeGroup(const TArray<FJJReviewBoneGroup>& User,
    const TArray<FJJReviewBoneGroup>& Template, const TArray<FJJReviewBoneGroup>& Resolved,
    const FString& TargetName, const FJJReviewBoneGroup& Requested, EJJReviewGroupOperation Operation)
{
    FJJReviewGroupChange Result;
    Result.UserGroups = User;
    FJJReviewBoneGroup Group = Requested;
    Group.Name = Group.Name.TrimStartAndEnd();
    Result.FinalName = Group.Name;
    const auto* Existing = Resolved.FindByPredicate([&](const auto& Item)
    {
        return Item.Name == TargetName;
    });
    if (Operation != EJJReviewGroupOperation::Save && !Existing)
    {
        Result.Error = TEXT("그룹을 먼저 선택하세요.");
        return Result;
    }
    if (Operation == EJJReviewGroupOperation::Rename && Group.Name == Existing->Name)
    {
        Result.bNoOp = true;
        return Result;
    }
    if (Operation != EJJReviewGroupOperation::Save)
    {
        Group.Bones = Existing->Bones;
    }
    if (Operation != EJJReviewGroupOperation::Delete && (Group.Name.IsEmpty() || Group.Bones.IsEmpty()))
    {
        Result.Error = TEXT("그룹 이름과 본 선택이 필요합니다.");
        return Result;
    }

    if (Operation != EJJReviewGroupOperation::Delete)
    {
        auto Reserved = Template;
        Reserved.Append(Resolved);
        Reserved.Append(User);
        if (Operation == EJJReviewGroupOperation::Rename)
        {
            Reserved.RemoveAll([&](const auto& Item)
            {
                return Item.Name == Existing->Name;
            });
        }
        Group.Name = UniqueGroupName(Group.Name, Reserved);
    }
    if (Operation != EJJReviewGroupOperation::Save)
    {
        Result.UserGroups.RemoveAll([&](const auto& Item)
        {
            return Item.Name == Existing->Name;
        });
        if (Template.ContainsByPredicate([&](const auto& Item)
        {
            return Item.Name == Existing->Name;
        }))
        {
            FJJReviewBoneGroup Hidden = *Existing;
            Hidden.Bones.Reset();
            Result.UserGroups.Add(Hidden);
        }
    }
    if (Operation != EJJReviewGroupOperation::Delete)
    {
        Result.UserGroups.RemoveAll([&](const auto& Item)
        {
            return Item.Name == Group.Name;
        });
        Result.UserGroups.Add(Group);
    }
    Result.FinalName = Group.Name;
    return Result;
}

FJJReviewVisibilityBounds JJAnimReviewNotes::VisibilityBounds(const TArray<FJJReviewAnnotation>& Notes,
    int32 Frame, const FString& SelectedId)
{
    FJJReviewVisibilityBounds Result;
    for (const auto& Note : Notes)
    {
        if (Note.Status != TEXT("open") && Note.AnnotationId != SelectedId)
        {
            continue;
        }
        const int64 Boundaries[] =
        {
            static_cast<int64>(Note.Frame) - 2,
            static_cast<int64>(EndFrame(Note)) + 3
        };
        for (int64 Boundary : Boundaries)
        {
            if (Boundary <= Frame)
            {
                Result.FromFrame = FMath::Max(Result.FromFrame, Boundary);
            }
            else
            {
                Result.UntilFrame = FMath::Min(Result.UntilFrame, Boundary);
            }
        }
    }
    return Result;
}

TArray<int32> JJAnimReviewNotes::MapGhostBones(const TArray<FName>& MeshBones, const TArray<FName>& SampleBones)
{
    TArray<int32> Result;
    Result.Reserve(MeshBones.Num());
    for (const FName Bone : MeshBones)
    {
        const int32 Sample = SampleBones.IndexOfByKey(Bone);
        if (Sample == INDEX_NONE)
        {
            return {};
        }
        Result.Add(Sample);
    }
    return Result;
}

bool JJAnimReviewNotes::ComponentToLocalPose(const TArray<int32>& Parents,
    const TArray<FTransform>& ComponentPose, TArray<FTransform>& LocalPose)
{
    if (Parents.Num() != ComponentPose.Num())
    {
        return false;
    }
    for (int32 Bone = 0; Bone < Parents.Num(); ++Bone)
    {
        const int32 Parent = Parents[Bone];
        if (Parent != INDEX_NONE && (!ComponentPose.IsValidIndex(Parent) || Parent == Bone))
        {
            return false;
        }
    }
    LocalPose.SetNum(ComponentPose.Num());
    for (int32 Bone = 0; Bone < Parents.Num(); ++Bone)
    {
        const int32 Parent = Parents[Bone];
        LocalPose[Bone] = Parent == INDEX_NONE
            ? ComponentPose[Bone] : ComponentPose[Bone].GetRelativeTransform(ComponentPose[Parent]);
    }
    return true;
}

FLinearColor JJAnimReviewNotes::GroupColor(const FString& Name)
{
    const TArray<FString> Defaults =
    {
        TEXT("머리"), TEXT("몸통"), TEXT("왼팔"), TEXT("왼손가락"),
        TEXT("오른팔"), TEXT("오른손가락"), TEXT("왼다리"), TEXT("오른다리")
    };
    const TArray<FLinearColor> Palette =
    {
        FLinearColor(1, 0.7f, 0.05f), FLinearColor(0.1f, 0.8f, 0.4f),
        FLinearColor(0.1f, 0.35f, 1), FLinearColor(0, 0.9f, 0.95f),
        FLinearColor(1, 0.12f, 0.15f), FLinearColor(1, 0.2f, 0.8f),
        FLinearColor(0.5f, 0.15f, 1), FLinearColor(1, 0.4f, 0.05f)
    };
    const int32 Index = Defaults.IndexOfByKey(Name);
    if (Index != INDEX_NONE)
    {
        return Palette[Index];
    }
    if (Name.IsEmpty())
    {
        return FLinearColor(0.85f, 0.9f, 0.15f);
    }
    uint32 Hash = 2166136261u;
    for (TCHAR Character : Name)
    {
        Hash = (Hash ^ static_cast<uint32>(Character)) * 16777619u;
    }
    return FLinearColor::MakeFromHSV8(static_cast<uint8>(Hash % 256), 190, 240);
}

FString JJAnimReviewNotes::UniqueGroupName(const FString& Requested, const TArray<FJJReviewBoneGroup>& Groups)
{
    const FString Base = Requested.TrimStartAndEnd();
    FString Candidate = Base;
    int64 Number = 2;
    while (Groups.ContainsByPredicate([&](const auto& Group)
    {
        return Group.Name == Candidate;
    }))
    {
        Candidate = Base + FString::Printf(TEXT(" %lld"), Number++);
    }
    return Candidate;
}

TArray<FJJReviewBoneGroup> JJAnimReviewNotes::RestoreDefaultGroups(
    const TArray<FJJReviewBoneGroup>& User, const TArray<FJJReviewBoneGroup>& Template)
{
    TArray<FJJReviewBoneGroup> Result = User;
    Result.RemoveAll([&](const auto& Group)
    {
        return Group.Bones.IsEmpty() && Template.ContainsByPredicate([&](const auto& Default)
        {
            return Default.Name == Group.Name;
        });
    });
    return Result;
}

TArray<FJJReviewBoneSegment> JJAnimReviewNotes::WeightedSegments(const TArray<FName>& Bones,
    const TArray<int32>& Parents, const TSet<FName>* WeightedBones)
{
    TArray<FJJReviewBoneSegment> Result;
    for (int32 Bone = 0; Bone < Bones.Num(); ++Bone)
    {
        if (WeightedBones && !WeightedBones->Contains(Bones[Bone]))
        {
            continue;
        }
        int32 Parent = Parents.IsValidIndex(Bone) ? Parents[Bone] : INDEX_NONE;
        int32 Steps = 0;
        while (Bones.IsValidIndex(Parent) && Parent != Bone && ++Steps <= Bones.Num())
        {
            if (!WeightedBones || WeightedBones->Contains(Bones[Parent]))
            {
                Result.Add({Bone, Parent});
                break;
            }
            Parent = Parents.IsValidIndex(Parent) ? Parents[Parent] : INDEX_NONE;
        }
    }
    return Result;
}

TArray<FJJReviewVisibleNote> JJAnimReviewNotes::VisibleNotes(const TArray<FJJReviewAnnotation>& Notes,
    int32 Frame, const FString& SelectedId)
{
    TArray<FJJReviewVisibleNote> Result;
    for (int32 Index = 0; Index < Notes.Num(); ++Index)
    {
        const auto& Note = Notes[Index];
        const bool bSelected = !SelectedId.IsEmpty() && Note.AnnotationId == SelectedId;
        if ((Note.Status == TEXT("open") || bSelected)
            && static_cast<int64>(Frame) >= static_cast<int64>(Note.Frame) - 2
            && static_cast<int64>(Frame) <= static_cast<int64>(EndFrame(Note)) + 2)
        {
            Result.Add({Index, bSelected, Targets(Note)});
        }
    }
    return Result;
}

TMap<FName, FJJReviewBoneColor> JJAnimReviewNotes::BoneColors(const TArray<FJJReviewBoneGroup>& Groups,
    const TArray<FJJReviewAnnotation>& Notes, const TArray<FJJReviewVisibleNote>& Visible,
    bool bShowGroups, bool bShowSelection, const TArray<FName>& Selection, const FString& SelectionGroup)
{
    TMap<FName, FJJReviewBoneColor> Result;
    const FLinearColor Neutral(0.35f, 0.35f, 0.35f);
    auto Apply = [&](const TArray<FName>& Bones, const FLinearColor& Color, int32 Priority)
    {
        for (FName Bone : Bones)
        {
            const auto* Existing = Result.Find(Bone);
            if (!Existing || Priority < Existing->Priority)
            {
                Result.Add(Bone, {Color, Priority});
            }
        }
    };
    if (bShowGroups)
    {
        for (int32 Index = 0; Index < Groups.Num(); ++Index)
        {
            Apply(Groups[Index].Bones, FMath::Lerp(Neutral, GroupColor(Groups[Index].Name), 0.35f), 200000 + Index);
        }
    }
    if (bShowSelection)
    {
        Apply(Selection, GroupColor(SelectionGroup), 100000);
    }
    for (const auto& Entry : Visible)
    {
        if (!Notes.IsValidIndex(Entry.Index))
        {
            continue;
        }
        const auto& Note = Notes[Entry.Index];
        const int32 GroupIndex = Groups.IndexOfByPredicate([&](const auto& Group)
        {
            return Group.Name == Note.GroupName;
        });
        const int32 Order = GroupIndex == INDEX_NONE ? Groups.Num() : GroupIndex;
        const FLinearColor Color = GroupColor(Note.GroupName);
        Apply(Entry.Bones, Entry.bSelected ? Color : FMath::Lerp(Neutral, Color, 0.55f), Entry.bSelected ? -1 : Order);
    }
    return Result;
}

FLinearColor JJAnimReviewNotes::VertexColor(const TArray<FJJReviewVertexInfluence>& Influences,
    const TMap<FName, FJJReviewBoneColor>& Colors)
{
    const FLinearColor Neutral(0.35f, 0.35f, 0.35f);
    FLinearColor Result(0, 0, 0, 0);
    float Total = 0;
    for (const auto& Influence : Influences)
    {
        if (!FMath::IsFinite(Influence.Weight) || Influence.Weight <= 0)
        {
            continue;
        }
        const auto* Color = Colors.Find(Influence.Bone);
        Result += (Color ? Color->Color : Neutral) * Influence.Weight;
        Total += Influence.Weight;
    }
    if (Total <= SMALL_NUMBER)
    {
        return Neutral;
    }
    Result /= Total;
    Result.A = 1;
    return Result;
}

#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace JJAnimReviewNotesPrivate
{
    FString OneLine(FString Text)
    {
        return Text.Replace(TEXT("\r"), TEXT(" ")).Replace(TEXT("\n"), TEXT(" "));
    }

    FString BoneList(const TArray<FName>& Bones, int32 Limit)
    {
        FString Result;
        int32 Included = 0;
        for (FName Bone : Bones)
        {
            const FString Candidate = Result + (Result.IsEmpty() ? TEXT("") : TEXT(", ")) + OneLine(Bone.ToString());
            const int32 Remaining = Bones.Num() - Included - 1;
            const FString Suffix = Remaining > 0 ? FString::Printf(TEXT(", … +%d bones"), Remaining) : FString();
            if (Candidate.Len() + Suffix.Len() > Limit)
            {
                break;
            }
            Result = Candidate;
            ++Included;
        }
        if (Included < Bones.Num())
        {
            Result += (Result.IsEmpty() ? TEXT("") : TEXT(", "))
                + FString::Printf(TEXT("… +%d bones"), Bones.Num() - Included);
        }
        return Result;
    }

    bool ValidGroup(const FJJReviewBoneGroup& Group)
    {
        return !Group.Name.TrimStartAndEnd().IsEmpty() && Group.Name.Len() <= 80
            && (Group.Side == TEXT("left") || Group.Side == TEXT("right") || Group.Side == TEXT("center"))
            && !Group.Bones.Contains(NAME_None);
    }
}

int32 JJAnimReviewNotes::EndFrame(const FJJReviewAnnotation& Note)
{
    return Note.EndFrame == INDEX_NONE ? Note.Frame : Note.EndFrame;
}

bool JJAnimReviewNotes::ValidateRange(const FJJReviewAnnotation& Note, int32 LastFrame, FString& Error)
{
    if (Note.Frame < 0 || EndFrame(Note) < Note.Frame || EndFrame(Note) > LastFrame)
    {
        Error = TEXT("Invalid annotation range: require 0 <= start <= end <= last sample frame.");
        return false;
    }
    return true;
}

bool JJAnimReviewNotes::IsSafeImagePath(const FString& Path)
{
    TArray<FString> Parts;
    Path.ParseIntoArray(Parts, TEXT("/"), false);
    if (Parts.Num() != 3 || Parts[0] != TEXT("notes") || Parts[1].IsEmpty() || Parts[2].IsEmpty()
        || Path.Contains(TEXT("..")) || Path.Contains(TEXT("\\")) || Path.Contains(TEXT(":")))
    {
        return false;
    }
    for (TCHAR C : Parts[1])
    {
        if (C >= 128 || (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-')))
        {
            return false;
        }
    }
    for (TCHAR C : Parts[2])
    {
        if (C >= 128 || (!FChar::IsAlnum(C) && C != TEXT('_') && C != TEXT('-') && C != TEXT('.')))
        {
            return false;
        }
    }
    const FString Extension = Parts[2].Right(4).ToLower();
    return Extension == TEXT(".png") || Extension == TEXT(".jpg");
}

TArray<FName> JJAnimReviewNotes::Targets(const FJJReviewAnnotation& Note)
{
    if (!Note.Bones.IsEmpty())
    {
        return Note.Bones;
    }
    TArray<FName> Result;
    if (!Note.Bone.IsNone())
    {
        Result.Add(Note.Bone);
    }
    return Result;
}

FString JJAnimReviewNotes::RangeText(const FJJReviewAnnotation& Note)
{
    if (EndFrame(Note) == Note.Frame)
    {
        return FString::Printf(TEXT("%df"), Note.Frame);
    }
    return FString::Printf(TEXT("%d–%df"), Note.Frame, EndFrame(Note));
}

FString JJAnimReviewNotes::AnnotationLine(const FJJReviewAnnotation& Note)
{
    const FString Target = Note.GroupName.IsEmpty()
        ? JJAnimReviewNotesPrivate::BoneList(Targets(Note), 160)
        : JJAnimReviewNotesPrivate::OneLine(Note.GroupName).Left(160);
    return FString::Printf(TEXT("%s [%s] %s %s %s"), *RangeText(Note),
        *JJAnimReviewNotesPrivate::OneLine(Target), *Note.Direction, *Note.Amount,
        *JJAnimReviewNotesPrivate::OneLine(Note.Text.Left(160)));
}

TArray<FString> JJAnimReviewNotes::GroupLegend(const TArray<FJJReviewAnnotation>& Notes)
{
    TArray<FString> Result;
    TArray<FString> Order;
    TMap<FString, TArray<FName>> Used;
    for (const auto& Note : Notes)
    {
        if (Note.Status != TEXT("open") || Note.GroupName.IsEmpty())
        {
            continue;
        }
        Order.AddUnique(Note.GroupName);
        for (FName Bone : Note.Bones)
        {
            Used.FindOrAdd(Note.GroupName).AddUnique(Bone);
        }
    }
    for (const FString& Name : Order)
    {
        const FString Prefix = JJAnimReviewNotesPrivate::OneLine(Name).Left(80) + TEXT(" = ");
        Result.Add(Prefix + JJAnimReviewNotesPrivate::BoneList(Used.FindOrAdd(Name), 160 - Prefix.Len()));
    }
    return Result;
}

TArray<FString> JJAnimReviewNotes::UnreferencedImageFolders(const TArray<FString>& Folders, const TArray<FJJReviewBundle>& Remaining)
{
    TSet<FString> Referenced;
    for (const auto& Take : Remaining)
    {
        for (const auto& Note : Take.Annotations)
        {
            for (const FString& Image : Note.Images)
            {
                if (IsSafeImagePath(Image))
                {
                    TArray<FString> Parts;
                    Image.ParseIntoArray(Parts, TEXT("/"));
                    Referenced.Add(Parts[1]);
                }
            }
        }
    }
    TArray<FString> Result;
    for (const FString& Folder : Folders)
    {
        if (IsSafeImagePath(TEXT("notes/") + Folder + TEXT("/check.png")) && !Referenced.Contains(Folder))
        {
            Result.Add(Folder);
        }
    }
    return Result;
}

FString JJAnimReviewNotes::InferSide(const TArray<FName>& Bones)
{
    int32 Left = 0;
    int32 Right = 0;
    for (FName Bone : Bones)
    {
        Left += Bone.ToString().EndsWith(TEXT("_l")) ? 1 : 0;
        Right += Bone.ToString().EndsWith(TEXT("_r")) ? 1 : 0;
    }
    if (Left > Right)
    {
        return TEXT("left");
    }
    return Right > Left ? TEXT("right") : TEXT("center");
}

FLinearColor JJAnimReviewNotes::SideColor(const FString& Side)
{
    if (Side == TEXT("left"))
    {
        return FLinearColor(0.1f, 0.4f, 1.0f);
    }
    return Side == TEXT("right") ? FLinearColor(1.0f, 0.15f, 0.1f) : FLinearColor(0.1f, 1.0f, 0.3f);
}

TArray<FJJReviewBoneGroup> JJAnimReviewNotes::MannequinTemplate(const TArray<FName>& Bones, const TArray<int32>& Parents, bool& bMatched)
{
    const TArray<FName> Anchors =
    {
        TEXT("pelvis"), TEXT("spine_01"), TEXT("neck_01"), TEXT("head"),
        TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
        TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
        TEXT("thigh_l"), TEXT("calf_l"), TEXT("thigh_r"), TEXT("calf_r")
    };
    int32 Matches = 0;
    for (FName Anchor : Anchors)
    {
        Matches += Bones.Contains(Anchor) ? 1 : 0;
    }
    bMatched = Matches >= 12;
    if (!bMatched)
    {
        return {};
    }
    TArray<FJJReviewBoneGroup> Groups;
    const TCHAR* Names[] =
    {
        TEXT("머리"), TEXT("몸통"), TEXT("왼팔"), TEXT("왼손가락"), TEXT("오른팔"), TEXT("오른손가락"), TEXT("왼다리"), TEXT("오른다리")
    };
    for (int32 Index = 0; Index < 8; ++Index)
    {
        FJJReviewBoneGroup Group;
        Group.Name = Names[Index];
        Group.Side = Index == 2 || Index == 3 || Index == 6 ? TEXT("left") : TEXT("center");
        if (Index == 4 || Index == 5 || Index == 7)
        {
            Group.Side = TEXT("right");
        }
        Groups.Add(Group);
    }
    for (int32 Index = 0; Index < Bones.Num(); ++Index)
    {
        const FString Name = Bones[Index].ToString().ToLower();
        if (Name.StartsWith(TEXT("ik_")) || Name.Contains(TEXT("attach")) || Name.StartsWith(TEXT("weapon_")))
        {
            continue;
        }
        int32 Group = INDEX_NONE;
        if (Name == TEXT("head") || Name == TEXT("neck_01") || Name == TEXT("neck_02"))
        {
            Group = 0;
        }
        else if (Name == TEXT("pelvis") || (Name >= TEXT("spine_01") && Name <= TEXT("spine_05")))
        {
            Group = 1;
        }
        else
        {
            const bool bLeft = Name.EndsWith(TEXT("_l"));
            const bool bRight = Name.EndsWith(TEXT("_r"));
            if (!bLeft && !bRight)
            {
                continue;
            }
            int32 Parent = Parents.IsValidIndex(Index) ? Parents[Index] : INDEX_NONE;
            int32 Steps = 0;
            while (Bones.IsValidIndex(Parent) && ++Steps <= Bones.Num())
            {
                if (Bones[Parent] == FName(bLeft ? TEXT("hand_l") : TEXT("hand_r")))
                {
                    Group = bLeft ? 3 : 5;
                    break;
                }
                Parent = Parents.IsValidIndex(Parent) ? Parents[Parent] : INDEX_NONE;
            }
            if (Group == INDEX_NONE && (Name.StartsWith(TEXT("clavicle_")) || Name.StartsWith(TEXT("upperarm_")) || Name.StartsWith(TEXT("lowerarm_")) || Name.StartsWith(TEXT("hand_"))))
            {
                Group = bLeft ? 2 : 4;
            }
            if (Group == INDEX_NONE && (Name.StartsWith(TEXT("thigh_")) || Name.StartsWith(TEXT("calf_")) || Name.StartsWith(TEXT("foot_")) || Name.StartsWith(TEXT("ball_"))))
            {
                Group = bLeft ? 6 : 7;
            }
        }
        if (Groups.IsValidIndex(Group))
        {
            Groups[Group].Bones.Add(Bones[Index]);
        }
    }
    return Groups;
}

TArray<FJJReviewBoneGroup> JJAnimReviewNotes::ResolveGroups(const TArray<FName>& SkeletonBones,
    const TArray<FJJReviewBoneGroup>& Template, const TArray<FJJReviewBoneGroup>& User)
{
    TArray<FJJReviewBoneGroup> Result = Template;
    for (const auto& Override : User)
    {
        Result.RemoveAll([&](const auto& Group)
        {
            return Group.Name == Override.Name;
        });
        Result.Add(Override);
    }
    for (auto& Group : Result)
    {
        TArray<FName> Filtered;
        for (FName Bone : Group.Bones)
        {
            if (SkeletonBones.Contains(Bone))
            {
                Filtered.AddUnique(Bone);
            }
        }
        Group.Bones = MoveTemp(Filtered);
    }
    return Result;
}

bool JJAnimReviewNotes::GroupsToJson(const FString& Skeleton, const TArray<FJJReviewBoneGroup>& Groups, FString& Json, FString& Error)
{
    auto Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schemaVersion"), 1);
    Root->SetStringField(TEXT("skeleton"), Skeleton);
    TArray<TSharedPtr<FJsonValue>> Values;
    TSet<FString> Names;
    for (const auto& Group : Groups)
    {
        if (!JJAnimReviewNotesPrivate::ValidGroup(Group) || Names.Contains(Group.Name))
        {
            Error = TEXT("Invalid or duplicate bone group.");
            return false;
        }
        Names.Add(Group.Name);
        auto Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("name"), Group.Name);
        Object->SetStringField(TEXT("side"), Group.Side);
        TArray<TSharedPtr<FJsonValue>> Bones;
        for (FName Bone : Group.Bones)
        {
            Bones.Add(MakeShared<FJsonValueString>(Bone.ToString()));
        }
        Object->SetArrayField(TEXT("bones"), Bones);
        Values.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("groups"), Values);
    return FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
}

bool JJAnimReviewNotes::GroupsFromJson(const FString& Json, const FString& Skeleton, TArray<FJJReviewBoneGroup>& Groups, FString& Error)
{
    TSharedPtr<FJsonObject> Root;
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    FString Path;
    int32 Version = 0;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root
        || Root->Values.Num() != 3 || !Root->TryGetNumberField(TEXT("schemaVersion"), Version) || Version != 1
        || !Root->TryGetStringField(TEXT("skeleton"), Path) || !Path.Equals(Skeleton, ESearchCase::IgnoreCase)
        || !Root->TryGetArrayField(TEXT("groups"), Values))
    {
        Error = TEXT("Invalid/unsupported bone group schema or skeleton path collision.");
        return false;
    }
    TArray<FJJReviewBoneGroup> Parsed;
    for (const auto& Value : *Values)
    {
        const TSharedPtr<FJsonObject>* Object = nullptr;
        const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
        FJJReviewBoneGroup Group;
        if (!Value->TryGetObject(Object) || !Object->IsValid() || (*Object)->Values.Num() != 3
            || !(*Object)->TryGetStringField(TEXT("name"), Group.Name) || !(*Object)->TryGetStringField(TEXT("side"), Group.Side)
            || !(*Object)->TryGetArrayField(TEXT("bones"), Bones))
        {
            Error = TEXT("Invalid bone group fields.");
            return false;
        }
        for (const auto& Bone : *Bones)
        {
            FString Name;
            if (!Bone->TryGetString(Name) || Name.IsEmpty())
            {
                Error = TEXT("Invalid group bone name.");
                return false;
            }
            Group.Bones.AddUnique(FName(Name));
        }
        Parsed.Add(Group);
    }
    FString Checked;
    if (!GroupsToJson(Skeleton, Parsed, Checked, Error))
    {
        return false;
    }
    Groups = MoveTemp(Parsed);
    return true;
}
