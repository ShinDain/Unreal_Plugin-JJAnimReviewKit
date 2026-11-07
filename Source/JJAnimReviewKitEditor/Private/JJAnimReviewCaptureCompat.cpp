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

    FPreviewScene Scene(FPreviewScene::ConstructionValues().SetLightBrightness(4.0f).SetSkyBrightness(1.0f));
    TStrongObjectPtr<UPoseableMeshComponent> Mesh(NewObject<UPoseableMeshComponent>());
    TStrongObjectPtr<USceneCaptureComponent2D> Capture(NewObject<USceneCaptureComponent2D>());
    TStrongObjectPtr<ULineBatchComponent> Grid(NewObject<ULineBatchComponent>());
    TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>());
    Mesh->SetSkinnedAssetAndUpdate(Preview);
    Mesh->SetComponentTickEnabled(false);
    Grid->SetComponentTickEnabled(false);
    Capture->bCaptureEveryFrame = false;
    Capture->bCaptureOnMovement = false;
    Capture->SetComponentTickEnabled(false);
    Capture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
    Capture->FOVAngle = 35;
    Target->ClearColor = FLinearColor(0.035f, 0.04f, 0.05f);
    Target->InitCustomFormat(Layout.Cell, Layout.Cell, PF_B8G8R8A8, false);
    Target->UpdateResourceImmediate(true);
    Capture->TextureTarget = Target.Get();
    Scene.AddComponent(Mesh.Get(), FTransform::Identity);
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
    for (int32 Column = 0; Column < Frames.Num() && bSuccess; ++Column)
    {
        const FJJReviewPoseSample* Sample = &Pose.Samples[0];
        for (const auto& Candidate : Pose.Samples)
        {
            if (FMath::Abs(Candidate.SourceFrame - Frames[Column]) < FMath::Abs(Sample->SourceFrame - Frames[Column]))
            {
                Sample = &Candidate;
            }
        }
        // A11: Poseable mesh uses component-space transforms; never plays notifies.
        FBox Bounds(ForceInit);
        const FVector Root = Sample->ComponentTransforms[0].ToTransform().GetTranslation();
        for (int32 B = 0; B < Pose.BoneNames.Num(); ++B)
        {
            FTransform Transform = Sample->ComponentTransforms[B].ToTransform();
            Transform.AddToTranslation(FVector(-Root.X, -Root.Y, 0));
            Mesh->SetBoneTransformByName(Pose.BoneNames[B], Transform, EBoneSpaces::ComponentSpace);
            Bounds += Transform.GetTranslation();
        }
        Mesh->RefreshBoneTransforms();
        const FVector Center = Bounds.GetCenter();
        const double Distance = FMath::Max(120.0, Bounds.GetExtent().Size() * 4.2);
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
            Capture->CaptureScene();
            FTextureRenderTargetResource* Resource = Target->GameThread_GetRenderTargetResource();
            FCanvas Canvas(Resource, nullptr, Scene.GetWorld(), Scene.GetWorld()->GetFeatureLevel());
            const FString Label = FString::Printf(TEXT("%df  yaw %.0f%s"), Sample->SourceFrame, Settings.YawDegrees[Row],
                Row == Settings.YawDegrees.Num() - 1 ? TEXT(" elevated") : TEXT(""));
            FCanvasTextItem Text(FVector2D(5, 5), FText::FromString(Label), GEngine->GetSmallFont(), FLinearColor::White);
            Text.EnableShadow(FLinearColor::Black);
            Canvas.DrawItem(Text);
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
    Scene.RemoveComponent(Mesh.Get());
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
