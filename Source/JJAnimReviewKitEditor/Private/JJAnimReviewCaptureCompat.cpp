#include "JJAnimReviewEngineCompat.h"

#include "JJAnimReviewSheet.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/LineBatchComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/DirectionalLightComponent.h"
#include "CanvasTypes.h"
#include "CanvasItem.h"
#include "ImageUtils.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "PreviewScene.h"
#include "RenderingThread.h"
#include "UObject/StrongObjectPtr.h"

bool JJAnimReviewEngineCompat::CaptureSheet(UAnimSequence* Animation, const FJJReviewPoseData& Pose,
    const FJJReviewCaptureSettings& Settings, const FString& Filename, FString& Reason)
{
    return CaptureSheetWithNotes(Animation, Pose, Settings, {}, Filename, Reason);
}

bool JJAnimReviewEngineCompat::CaptureSheetWithNotes(UAnimSequence* Animation, const FJJReviewPoseData& Pose,
    const FJJReviewCaptureSettings& Settings, const TArray<FJJReviewAnnotation>& Notes,
    const FString& Filename, FString& Reason)
{
    check(IsInGameThread());
    // A11: GUI-only public scene/capture/readback path. No recurring capture or asset writes.
    if (IsRunningCommandlet() || !FApp::CanEverRender())
    {
        Reason = TEXT("Sheet skipped: GUI editor and rendering required (commandlet/nullrhi unsupported).");
        return false;
    }
    if (!Animation)
    {
        Reason = TEXT("Sheet skipped: no animation.");
        return false;
    }
    USkeletalMesh* Preview = Animation->GetPreviewMesh();
    if (!Preview && Animation->GetSkeleton())
    {
        Preview = Animation->GetSkeleton()->GetPreviewMesh();
    }
    if (!Preview)
    {
        Reason = TEXT("Sheet skipped: animation and skeleton have no preview mesh; assign one manually.");
        return false;
    }
    if (Pose.Samples.IsEmpty() || Pose.BoneNames.IsEmpty()
        || Pose.Samples.ContainsByPredicate([&](const auto& Sample)
        {
            return Sample.ComponentTransforms.Num() != Pose.BoneNames.Num();
        }))
    {
        Reason = TEXT("Sheet skipped: incomplete pose data.");
        return false;
    }
    TArray<FJJReviewBoneGroup> UserGroups;
    TArray<FJJReviewBoneGroup> Groups;
    bool bMatched = false;
    FString GroupError;
    // Bad optional group data does not block the sheet; suffix colors remain available.
    if (!LoadBoneGroups(Animation, UserGroups, Groups, bMatched, GroupError))
    {
        Reason = TEXT("Sheet uses suffix colors; groups unavailable: ") + GroupError;
    }
    const TArray<int32> Frames = JJAnimReviewSheet::SelectFrames(Pose, Settings.FrameCount, Settings.Frames);
    const FJJReviewSheetLayout Layout = JJAnimReviewSheet::Layout(Frames.Num(), Settings.YawDegrees.Num(), Settings.CellSize);
    if (Layout.Cell == 0 || !FMath::IsFinite(Settings.ForwardYawOffset)
        || Settings.YawDegrees.ContainsByPredicate([](double Value)
        {
            return !FMath::IsFinite(Value);
        }))
    {
        Reason = TEXT("Sheet skipped: invalid angles/layout (1-8 finite angles, up to 24 frames).");
        return false;
    }

    // A16/A17: independent immutable poses per column, shadow-free camera-side fill.
    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetLightBrightness(1.6f).SetSkyBrightness(0.8f));
    TArray<TStrongObjectPtr<UPoseableMeshComponent>> Meshes;
    TStrongObjectPtr<USceneCaptureComponent2D> Capture(NewObject<USceneCaptureComponent2D>());
    TStrongObjectPtr<ULineBatchComponent> Grid(NewObject<ULineBatchComponent>());
    TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
    Scene.DirectionalLight->SetCastShadows(false);
    Grid->SetComponentTickEnabled(false);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->SetComponentTickEnabled(false);
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    // A17: SceneCapture background is black regardless of target ClearColor.
    Capture->PostProcessSettings.bOverride_ColorOffset = true;
    Capture->PostProcessSettings.ColorOffset = FVector4(0.08, 0.08, 0.08, 0.0);
    Capture->PostProcessSettings.bOverride_AutoExposureMethod = true;
    Capture->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
    Capture->PrimitiveRenderMode = ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
    Capture->FOVAngle = 35;
    Target->ClearColor = FLinearColor(0.22f, 0.22f, 0.22f);
    Target->InitCustomFormat(Layout.Cell, Layout.Cell, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target.Get();
    Scene.AddComponent(Grid.Get(), FTransform::Identity);
    Scene.AddComponent(Capture.Get(), FTransform::Identity);
    for (int32 I = -10; I <= 10; ++I)
    {
        const float V = I * 50.0f;
        const FLinearColor Color = I == 0 ? FLinearColor::Gray : FLinearColor(0.12f, 0.12f, 0.12f);
        Grid->DrawLine(FVector(-500, V, 0), FVector(500, V, 0), Color, 0, 0.5f, -1);
        Grid->DrawLine(FVector(V, -500, 0), FVector(V, 500, 0), Color, 0, 0.5f, -1);
    }
    TArray<FColor> Pixels;
    Pixels.Init(FColor::Black, Layout.Size.X * Layout.Size.Y);
    FScopedSlowTask Progress(Frames.Num() * Settings.YawDegrees.Num(), FText::FromString(TEXT("JJAnimReview: contact sheet")));
    Progress.MakeDialog(true);
    bool bSuccess = true;
    FBox AllBounds(ForceInit);
    for (int32 Frame : Frames)
    {
        const FJJReviewPoseSample* Nearest = &Pose.Samples[0];
        for (const auto& Candidate : Pose.Samples)
        {
            if (FMath::Abs(Candidate.SourceFrame - Frame) < FMath::Abs(Nearest->SourceFrame - Frame))
            {
                Nearest = &Candidate;
            }
        }
        const FVector Root = Nearest->ComponentTransforms[0].ToTransform().GetLocation();
        for (const auto& Bone : Nearest->ComponentTransforms)
        {
            AllBounds += Bone.ToTransform().GetLocation() - FVector(Root.X, Root.Y, 0);
        }
    }
    const FVector Center = AllBounds.GetCenter();
    const double Distance = FMath::Max(120.0, AllBounds.GetExtent().Size() * 4.2);
    TSet<FName> SkinBones;
    const bool bSkinBonesAvailable = WeightedBones(Preview, SkinBones);
    const auto Segments = JJAnimReviewNotes::WeightedSegments(Pose.BoneNames, Pose.ParentIndices,
        bSkinBonesAvailable ? &SkinBones : nullptr);
    for (int32 Column = 0; Column < Frames.Num() && bSuccess; ++Column)
    {
        Meshes.Emplace(NewObject<UPoseableMeshComponent>());
        UPoseableMeshComponent* Mesh = Meshes.Last().Get();
        Mesh->SetSkinnedAssetAndUpdate(Preview);
        Mesh->SetComponentTickEnabled(false);
        Mesh->SetCastShadow(false);
        Scene.AddComponent(Mesh, FTransform::Identity);
        Capture->ClearShowOnlyComponents();
        Capture->ShowOnlyComponent(Mesh);
        Capture->ShowOnlyComponent(Grid.Get());
        const FJJReviewPoseSample* Sample = &Pose.Samples[0];
        for (const auto& Candidate : Pose.Samples)
        {
            if (FMath::Abs(Candidate.SourceFrame - Frames[Column]) < FMath::Abs(Sample->SourceFrame - Frames[Column]))
            {
                Sample = &Candidate;
            }
        }
        // A11: Poseable mesh uses component-space transforms; never plays notifies.
        const FVector Root = Sample->ComponentTransforms[0].ToTransform().GetTranslation();
        for (int32 B = 0; B < Pose.BoneNames.Num(); ++B)
        {
            FTransform Transform = Sample->ComponentTransforms[B].ToTransform();
            Transform.AddToTranslation(FVector(-Root.X, -Root.Y, 0));
            Mesh->SetBoneTransformByName(Pose.BoneNames[B], Transform, EBoneSpaces::ComponentSpace);
        }
        Mesh->RefreshBoneTransforms();
        Scene.GetWorld()->SendAllEndOfFrameUpdates();
        FlushRenderingCommands();
        for (int32 Row = 0; Row < Settings.YawDegrees.Num(); ++Row)
        {
            Progress.EnterProgressFrame(1);
            if (Progress.ShouldCancel())
            {
                Reason = TEXT("Sheet cancelled; numerical review retained.");
                bSuccess = false;
                break;
            }
            const FRotator Orbit(Row == Settings.YawDegrees.Num() - 1 ? 40 : 0,
                Settings.YawDegrees[Row] + Settings.ForwardYawOffset, 0);
            const FVector Camera = Center + Orbit.Vector() * Distance;
            Capture->SetWorldLocationAndRotation(Camera, (Center - Camera).Rotation());
            Scene.SetLightDirection((Center - Camera).Rotation());
            Scene.GetWorld()->SendAllEndOfFrameUpdates();
            Capture->CaptureScene();
            FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
            FCanvas Canvas(Resource, nullptr, Scene.GetWorld(), Scene.GetWorld()->GetFeatureLevel());
            const FString Label = FString::Printf(TEXT("%df  front-relative yaw %.0f%s"), Sample->SourceFrame, Settings.YawDegrees[Row],
                Row == Settings.YawDegrees.Num() - 1 ? TEXT(" elevated") : TEXT(""));
            FCanvasTextItem Text(FVector2D(5, 5), FText::FromString(Label), GEngine->GetSmallFont(), FLinearColor::White);
            Text.EnableShadow(FLinearColor::Black);
            Canvas.DrawItem(Text);
            // A21: project sampled bones after capture, so the mesh cannot occlude lines.
            const FRotationMatrix CameraAxes((Center - Camera).Rotation());
            const FVector Forward = CameraAxes.GetUnitAxis(EAxis::X);
            const FVector Right = CameraAxes.GetUnitAxis(EAxis::Y);
            const FVector Up = CameraAxes.GetUnitAxis(EAxis::Z);
            const double Focal = Layout.Cell * 0.5 / FMath::Tan(FMath::DegreesToRadians(Capture->FOVAngle * 0.5));
            auto Project = [&](int32 Bone, FVector2D& Screen)
            {
                if (!Sample->ComponentTransforms.IsValidIndex(Bone))
                {
                    return false;
                }
                const FVector Position = Sample->ComponentTransforms[Bone].ToTransform().GetLocation() - FVector(Root.X, Root.Y, 0);
                const FVector Relative = Position - Camera;
                const double Depth = FVector::DotProduct(Relative, Forward);
                if (Depth <= 1.0)
                {
                    return false;
                }
                Screen = FVector2D(Layout.Cell * 0.5 + FVector::DotProduct(Relative, Right) * Focal / Depth,
                    Layout.Cell * 0.5 - FVector::DotProduct(Relative, Up) * Focal / Depth);
                return Screen.X >= 0 && Screen.X < Layout.Cell && Screen.Y >= 0 && Screen.Y < Layout.Cell;
            };
            TSet<FName> Highlighted;
            for (const auto& Note : Notes)
            {
                if (Note.Status == TEXT("open") && Sample->SourceFrame >= Note.Frame
                    && Sample->SourceFrame <= JJAnimReviewNotes::EndFrame(Note))
                {
                    for (FName Bone : JJAnimReviewNotes::Targets(Note))
                    {
                        Highlighted.Add(Bone);
                    }
                }
            }
            for (const auto& Segment : Segments)
            {
                const int32 Bone = Segment.Bone;
                const int32 Parent = Segment.Parent;
                FVector2D ChildScreen;
                FVector2D ParentScreen;
                if (!Project(Bone, ChildScreen) || !Project(Parent, ParentScreen))
                {
                    continue;
                }
                FString Side = JJAnimReviewNotes::InferSide({Pose.BoneNames[Bone]});
                for (const auto& Group : Groups)
                {
                    if (Group.Bones.Contains(Pose.BoneNames[Bone]))
                    {
                        Side = Group.Side;
                        break;
                    }
                }
                const bool bHighlight = Highlighted.Contains(Pose.BoneNames[Bone]);
                FCanvasLineItem Line(ParentScreen, ChildScreen);
                Line.SetColor(bHighlight ? FLinearColor::Yellow : JJAnimReviewNotes::SideColor(Side));
                Line.LineThickness = bHighlight ? 3.0f : 1.5f;
                Canvas.DrawItem(Line);
            }
            if (Column == 0 && Row == 0)
            {
                FCanvasTextItem Legend(FVector2D(4, Layout.Cell - 14),
                    FText::FromString(TEXT("Blue=L Red=R Green=C Yellow=note")), GEngine->GetTinyFont(), FLinearColor::White);
                Legend.EnableShadow(FLinearColor::Black);
                Canvas.DrawItem(Legend);
            }
            Canvas.Flush_GameThread();
            TArray<FColor> Cell;
            if (!Resource->ReadPixels(Cell) || Cell.Num() != Layout.Cell * Layout.Cell)
            {
                Reason = TEXT("Sheet readback failed; numerical review retained.");
                bSuccess = false;
                break;
            }
            for (int32 Y = 0; Y < Layout.Cell; ++Y)
            {
                for (int32 X = 0; X < Layout.Cell; ++X)
                {
                    FColor Color = Cell[Y * Layout.Cell + X];
                    Color.A = 255;
                    Pixels[(Row * Layout.Cell + Y) * Layout.Size.X + Column * Layout.Cell + X] = Color;
                }
            }
        }
    }
    Capture->TextureTarget = nullptr;
    Scene.RemoveComponent(Capture.Get());
    Scene.RemoveComponent(Grid.Get());
    Capture->ClearShowOnlyComponents();
    for (auto& Mesh : Meshes)
    {
        Scene.RemoveComponent(Mesh.Get());
        Mesh->DestroyComponent();
    }
    FlushRenderingCommands();
    if (bSuccess)
    {
        TArray64<uint8> Png;
        FImageUtils::PNGCompressImageArray(Layout.Size.X, Layout.Size.Y, Pixels, Png);
        bSuccess = !Png.IsEmpty() && FFileHelper::SaveArrayToFile(Png, *Filename);
        if (!bSuccess)
        {
            Reason = TEXT("Sheet PNG write failed.");
        }
    }
    return bSuccess;
}
