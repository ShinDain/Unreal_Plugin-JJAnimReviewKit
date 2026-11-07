#include "JJAnimReviewEngineCompat.h"

#include "JJAnimReviewComparison.h"
#include "JJAnimReviewEditing.h"
#include "JJAnimReviewService.h"
#include "JJAnimReviewStore.h"
#include "IAnimationEditor.h"
#include "IPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "Animation/AnimSequence.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/LineBatchComponent.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UObject/StrongObjectPtr.h"

namespace JJAnimReviewAnnotationWidgetCompatPrivate
{
    const FName TabId(TEXT("JJAnimReview.Annotations"));
    TArray<TWeakPtr<SDockTab>> Tabs;
    TArray<TWeakPtr<FTabManager>> Managers;

    class SAnnotations : public SCompoundWidget
    {
    public:
        SLATE_BEGIN_ARGS(SAnnotations)
        {
        }
            SLATE_ARGUMENT(TWeakPtr<IAnimationEditor>, Editor)
        SLATE_END_ARGS()

        void Construct(const FArguments& Args)
        {
            Editor = Args._Editor;
            const TCHAR* Axes[] =
            {
                TEXT(""), TEXT("+X"), TEXT("-X"), TEXT("+Y"), TEXT("-Y"), TEXT("+Z"), TEXT("-Z")
            };
            for (const TCHAR* Axis : Axes)
            {
                Directions.Add(MakeShared<FString>(Axis));
            }
            Amounts.Add(MakeShared<FString>(TEXT("little")));
            Amounts.Add(MakeShared<FString>(TEXT("much")));
            ChildSlot
            [
                SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(STextBlock).Text(FText::FromString(TEXT("기본 출력 폴더의 최신 리뷰를 편집합니다. 추가는 현재 프레임·선택 본을 사용합니다."))).AutoWrapText(true)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("새로고침"))).OnClicked_Lambda([this]()
                        {
                            Refresh();
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SCheckBox).IsChecked(ECheckBoxState::Checked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState Value)
                        {
                            bNotes = Value == ECheckBoxState::Checked;
                            SyncDrawing();
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("주석 표시 ±2f")))
                        ]
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SCheckBox).IsChecked(ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda([this](ECheckBoxState Value)
                        {
                            bGhost = Value == ECheckBoxState::Checked;
                            LoadGhost();
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("이전 테이크 고스트")))
                        ]
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SAssignNew(GhostTake, STextComboBox).OptionsSource(&GhostTakes)
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Choice, ESelectInfo::Type)
                        {
                            if (bRefreshing)
                            {
                                return;
                            }
                            if (Choice.IsValid() && GhostTakes.Contains(Choice))
                            {
                                SelectedGhostTake = Choice;
                                LoadGhost();
                            }
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(Text, SEditableTextBox).HintText(FText::FromString(TEXT("주석 텍스트")))
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SAssignNew(Direction, STextComboBox).OptionsSource(&Directions).InitiallySelectedItem(Directions[0])
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SAssignNew(Amount, STextComboBox).OptionsSource(&Amounts).InitiallySelectedItem(Amounts[0])
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("현재 위치에 추가"))).OnClicked_Lambda([this]()
                        {
                            Write(TEXT("add"), false);
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("선택 주석 수정"))).OnClicked_Lambda([this]()
                        {
                            Write(TEXT("update"), false);
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("포즈 지시 저장"))).OnClicked_Lambda([this]()
                        {
                            Write(TEXT("add"), true);
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(Status, STextBlock).AutoWrapText(true)
                ]
                + SVerticalBox::Slot().FillHeight(1)
                [
                    SAssignNew(Rows, SScrollBox)
                ]
            ];
            Refresh();
        }

        virtual ~SAnnotations() override
        {
            StopDrawing();
        }

        TSharedPtr<IPersonaPreviewScene> GetScene() const
        {
            const auto Pinned = Editor.Pin();
            if (!Pinned)
            {
                return nullptr;
            }
            return Pinned->GetPersonaToolkit()->GetPreviewScene();
        }

        UAnimSequence* Animation() const
        {
            const auto Scene = GetScene();
            return Scene ? Cast<UAnimSequence>(Scene->GetPreviewAnimationAsset()) : nullptr;
        }

        void Refresh()
        {
            bRefreshing = true;
            StopDrawing();
            Rows->ClearChildren();
            UAnimSequence* Anim = Animation();
            FString Error;
            if (!Anim || !JJAnimReviewService::ReadLatest(Anim->GetPathName(), Bundle, Takes, Error))
            {
                bLoaded = false;
                ResetGhostTakes();
                Status->SetText(FText::FromString(Error.IsEmpty() ? TEXT("Open an AnimSequence.") : Error));
                bRefreshing = false;
                LoadGhost();
                return;
            }
            Asset = Anim->GetPathName();
            bLoaded = true;
            Selected = FJJReviewAnnotation();
            Status->SetText(FText::FromString(Bundle.Take));

            // Ghost candidates: every retained take except the latest, newest first.
            ResetGhostTakes();
            for (int32 Index = Takes.Num() - 2; Index >= 0; --Index)
            {
                if (Takes[Index].ContentId.StartsWith(TEXT("pose_md5_v2:")))
                {
                    GhostTakes.Add(MakeShared<FString>(Takes[Index].Take));
                }
            }
            GhostTake->RefreshOptions();
            if (!GhostTakes.IsEmpty())
            {
                SelectedGhostTake = GhostTakes[0];
                GhostTake->SetSelectedItem(SelectedGhostTake);
            }

            for (const FJJReviewAnnotation& Item : Bundle.Annotations)
            {
                const FString Label = FString::Printf(TEXT("%df %s %s %s [%s]%s: %s"), Item.Frame,
                    *Item.Bone.ToString(), *Item.Direction, *Item.Amount, *Item.Status,
                    Item.bFromPreviousVersion ? TEXT(" 이전 버전에서 작성") : TEXT(""), *Item.Text.Left(120));
                Rows->AddSlot()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([this, Item]()
                        {
                            Select(Item);
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(Item.Status == TEXT("open") ? TEXT("해결") : TEXT("다시 열기")))
                        .OnClicked_Lambda([this, Item]()
                        {
                            Selected = Item;
                            Write(Item.Status == TEXT("open") ? TEXT("resolve") : TEXT("reopen"), false);
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("삭제"))).OnClicked_Lambda([this, Item]()
                        {
                            Selected = Item;
                            Write(TEXT("delete"), false);
                            return FReply::Handled();
                        })
                    ]
                ];
            }
            bRefreshing = false;
            LoadGhost();
        }

        void Select(const FJJReviewAnnotation& Item)
        {
            Selected = Item;
            Text->SetText(FText::FromString(Item.Text));
            for (const auto& Option : Directions)
            {
                if (*Option == Item.Direction)
                {
                    Direction->SetSelectedItem(Option);
                }
            }
            for (const auto& Option : Amounts)
            {
                if (*Option == Item.Amount)
                {
                    Amount->SetSelectedItem(Option);
                }
            }
            const auto Scene = GetScene();
            UAnimSequence* Anim = Animation();
            UDebugSkelMeshComponent* Mesh = Scene ? Scene->GetPreviewMeshComponent() : nullptr;
            if (Anim && Mesh && Mesh->GetSingleNodeInstance())
            {
                // A13: Scrub without notifies, keys or asset writes.
                Mesh->GetSingleNodeInstance()->SetPlaying(false);
                Mesh->GetSingleNodeInstance()->SetPosition(Item.Frame * Anim->GetSamplingFrameRate().AsInterval(), false);
                Scene->InvalidateViews();
            }
        }

        void Write(const FString& Operation, bool bPose)
        {
            UAnimSequence* Anim = Animation();
            const auto Scene = GetScene();
            UDebugSkelMeshComponent* Mesh = Scene ? Scene->GetPreviewMeshComponent() : nullptr;
            if (!bLoaded || !Anim || Anim->GetPathName() != Asset || !Mesh || !Mesh->GetSingleNodeInstance())
            {
                Status->SetText(FText::FromString(TEXT("Create a review bundle and refresh this tab.")));
                return;
            }
            FJJReviewAnnotation Item = Selected;
            FString Error;
            if (Operation == TEXT("add") || Operation == TEXT("update"))
            {
                Item.Text = Text->GetText().ToString();
                Item.Direction = *Direction->GetSelectedItem();
                Item.Amount = *Amount->GetSelectedItem();
                if (Operation == TEXT("add"))
                {
                    Item = FJJReviewAnnotation();
                    Item.Text = Text->GetText().ToString();
                    Item.Direction = *Direction->GetSelectedItem();
                    Item.Amount = *Amount->GetSelectedItem();
                    Item.Frame = FMath::RoundToInt(Mesh->GetSingleNodeInstance()->GetCurrentTime() * Anim->GetSamplingFrameRate().AsDecimal());
                    const int32 Index = Scene->GetSelectedBoneIndex();
                    if (Mesh->GetSkinnedAsset() && Index >= 0 && Index < Mesh->GetNumBones())
                    {
                        Item.Bone = Mesh->GetBoneName(Index);
                    }
                }
                if (bPose)
                {
                    // A14: Read evaluated component poses only. Preview controllers remain untouched.
                    const double Time = Mesh->GetSingleNodeInstance()->GetCurrentTime();
                    FJJReviewPoseData Original;
                    if (!JJAnimReviewEngineCompat::Sample(Anim, TArray<double>{Time}, Original, Error))
                    {
                        Status->SetText(FText::FromString(Error));
                        return;
                    }
                    FJJReviewPoseSample Preview;
                    Preview.SourceFrame = Item.Frame;
                    for (FName Bone : Original.BoneNames)
                    {
                        const int32 Index = Mesh->GetBoneIndex(Bone);
                        if (!Mesh->GetComponentSpaceTransforms().IsValidIndex(Index))
                        {
                            Status->SetText(FText::FromString(TEXT("Preview mesh is missing a sampled bone.")));
                            return;
                        }
                        Preview.ComponentTransforms.Add(FJJReviewTransform::FromTransform(Mesh->GetComponentSpaceTransforms()[Index]));
                    }
                    FString Signed;
                    Item.PoseInstructions = JJAnimReviewEditing::PoseInstructions(Original, Preview, Signed);
                    if (Item.PoseInstructions.IsEmpty())
                    {
                        Status->SetText(FText::FromString(TEXT("No preview changes above 0.1cm / 0.5deg.")));
                        return;
                    }
                    Item.Text += TEXT(" [Preview pose instruction; parent-space]") + Signed;
                }
            }
            if (!JJAnimReviewService::EditAnnotation(Asset, Bundle.ContentId, Item, Operation, Error))
            {
                Status->SetText(FText::FromString(Error));
                return;
            }
            Refresh();
            if (bPose)
            {
                Status->SetText(FText::FromString(TEXT("포즈 지시 저장됨. 미리보기 수정은 유지됩니다. Persona의 본 수정 초기화로 직접 지우세요.")));
            }
        }

        void StopDrawing()
        {
            if (const auto Scene = DrawingScene.Pin())
            {
                Scene->UnregisterOnPostTick(this);
                if (Lines.IsValid())
                {
                    Scene->RemoveComponent(Lines.Get());
                }
                Scene->InvalidateViews();
            }
            Lines.Reset();
            DrawingScene.Reset();
        }

        void ResetGhostTakes()
        {
            SelectedGhostTake.Reset();
            GhostTakes.Reset();
            GhostPose = FJJReviewPoseData();
            bGhostLoaded = false;
            if (GhostTake.IsValid())
            {
                GhostTake->ClearSelection();
                GhostTake->RefreshOptions();
            }
        }

        void LoadGhost()
        {
            if (bRefreshing)
            {
                return;
            }
            GhostPose = FJJReviewPoseData();
            bGhostLoaded = false;
            const TSharedPtr<FString> Choice = SelectedGhostTake;
            if (bGhost && bLoaded && Choice.IsValid() && GhostTakes.Contains(Choice))
            {
                FString Error;
                if (JJAnimReviewService::ReadTakePose(Asset, *Choice, GhostPose, Error))
                {
                    bGhostLoaded = true;
                }
                else
                {
                    Status->SetText(FText::FromString(Error));
                }
            }
            else if (bGhost && bLoaded && GhostTakes.IsEmpty())
            {
                Status->SetText(FText::FromString(TEXT("No earlier take to compare. Change the animation and create another review bundle.")));
            }

            SyncDrawing();
        }

        void SyncDrawing()
        {
            StopDrawing();
            const bool bAnyNotes = bLoaded && bNotes && Bundle.Annotations.ContainsByPredicate([](const auto& Item)
            {
                return Item.Status == TEXT("open");
            });
            const bool bAny = bAnyNotes || (bLoaded && bGhost && bGhostLoaded);
            const auto Scene = GetScene();
            if (!bAny || !Scene)
            {
                return;
            }
            Lines.Reset(NewObject<ULineBatchComponent>());
            Lines->SetComponentTickEnabled(false);
            Scene->AddComponent(Lines.Get(), FTransform::Identity);
            DrawingScene = Scene;
            Scene->RegisterOnPostTick(FSimpleDelegate::CreateSP(this, &SAnnotations::Draw));
            Draw();
        }

        void Draw()
        {
            const auto Scene = DrawingScene.Pin();
            UDebugSkelMeshComponent* Mesh = Scene ? Scene->GetPreviewMeshComponent() : nullptr;
            UAnimSequence* Anim = Animation();
            if (!Lines.IsValid())
            {
                return;
            }
            Lines->Flush();
            if (!Mesh || !Anim || Anim->GetPathName() != Asset || !Mesh->GetSingleNodeInstance())
            {
                return;
            }
            const int32 Frame = FMath::RoundToInt(Mesh->GetSingleNodeInstance()->GetCurrentTime() * Anim->GetSamplingFrameRate().AsDecimal());

            if (bGhost && bGhostLoaded)
            {
                // R4 wire ghost: earlier take's component-space skeleton at the current preview time.
                // pose.json component space includes root motion, so it can drift from the preview
                // when Persona's root motion mode differs. Plugin-owned line component only (A6).
                const double Time = Mesh->GetSingleNodeInstance()->GetCurrentTime();
                const FTransform ToWorld = Mesh->GetComponentTransform();
                const FLinearColor GhostColor(1.0f, 0.0f, 1.0f);
                for (int32 Bone = 0; Bone < GhostPose.BoneNames.Num(); ++Bone)
                {
                    const int32 Parent = GhostPose.ParentIndices.IsValidIndex(Bone) ? GhostPose.ParentIndices[Bone] : INDEX_NONE;
                    if (!GhostPose.BoneNames.IsValidIndex(Parent))
                    {
                        continue;
                    }

                    const FVector ChildLocation = ToWorld.TransformPosition(
                        JJAnimReviewComparison::AtTime(GhostPose, Bone, Time).GetLocation());
                    const FVector ParentLocation = ToWorld.TransformPosition(
                        JJAnimReviewComparison::AtTime(GhostPose, Parent, Time).GetLocation());
                    Lines->DrawLine(ParentLocation, ChildLocation, GhostColor, 0, 1, -1);
                }
            }

            for (const auto& Item : Bundle.Annotations)
            {
                if (!bNotes || Item.Status != TEXT("open") || FMath::Abs(Item.Frame - Frame) > 2)
                {
                    continue;
                }
                const int32 Index = Mesh->GetBoneIndex(Item.Bone);
                const FVector P = Index == INDEX_NONE ? Mesh->Bounds.Origin : Mesh->GetBoneTransform(Index).GetLocation();
                const FLinearColor Color = Item.bFromPreviousVersion ? FLinearColor::Yellow : FLinearColor(0.0f, 1.0f, 1.0f);
                Lines->DrawPoint(P, Color, 12, 0, -1);
                if (!Item.Direction.IsEmpty())
                {
                    FVector Axis = FVector::ZeroVector;
                    const int32 K = Item.Direction.EndsWith(TEXT("X")) ? 0 : (Item.Direction.EndsWith(TEXT("Y")) ? 1 : 2);
                    Axis[K] = Item.Direction.StartsWith(TEXT("-")) ? -1 : 1;
                    const FVector End = P + Axis * (Item.Amount == TEXT("much") ? 30 : 12);
                    Lines->DrawLine(P, End, Color, 0, 2, -1);
                }
            }
        }

    private:
        TWeakPtr<IAnimationEditor> Editor;
        TWeakPtr<IPersonaPreviewScene> DrawingScene;
        TStrongObjectPtr<ULineBatchComponent> Lines;
        TSharedPtr<SEditableTextBox> Text;
        TSharedPtr<STextComboBox> Direction;
        TSharedPtr<STextComboBox> Amount;
        TArray<TSharedPtr<FString>> Directions;
        TArray<TSharedPtr<FString>> Amounts;
        TSharedPtr<STextBlock> Status;
        TSharedPtr<SScrollBox> Rows;
        FJJReviewBundle Bundle;
        TArray<FJJReviewBundle> Takes;
        FJJReviewAnnotation Selected;
        FString Asset;
        bool bLoaded = false;
        bool bNotes = true;
        TSharedPtr<STextComboBox> GhostTake;
        TSharedPtr<FString> SelectedGhostTake;
        bool bRefreshing = false;
        TArray<TSharedPtr<FString>> GhostTakes;
        FJJReviewPoseData GhostPose;
        bool bGhost = false;
        bool bGhostLoaded = false;
    };
}

void JJAnimReviewEngineCompat::OpenAnnotations(TWeakPtr<IAnimationEditor> Weak)
{
    using namespace JJAnimReviewAnnotationWidgetCompatPrivate;
    const auto Editor = Weak.Pin();
    const auto Manager = Editor ? Editor->GetTabManager() : nullptr;
    if (!Manager)
    {
        return;
    }
    // A12: Register a document-local dockable tab on explicit invocation.
    if (!Manager->HasTabSpawner(TabId))
    {
        Manager->RegisterTabSpawner(TabId, FOnSpawnTab::CreateLambda([Weak](const FSpawnTabArgs&)
        {
            TSharedRef<SDockTab> Tab = SNew(SDockTab).TabRole(ETabRole::PanelTab)
            [
                SNew(SAnnotations).Editor(Weak)
            ];
            Tabs.Add(Tab);
            return Tab;
        })).SetDisplayName(FText::FromString(TEXT("JJ 리뷰 주석")));
        Managers.Add(Manager);
    }
    Manager->TryInvokeTab(TabId);
}

void JJAnimReviewEngineCompat::CloseAnnotationTabs()
{
    using namespace JJAnimReviewAnnotationWidgetCompatPrivate;
    for (const auto& Weak : Tabs)
    {
        if (const auto Tab = Weak.Pin())
        {
            Tab->SetContent(SNullWidget::NullWidget);
            Tab->RequestCloseTab();
        }
    }
    Tabs.Reset();
    for (const auto& Weak : Managers)
    {
        if (const auto Manager = Weak.Pin())
        {
            Manager->UnregisterTabSpawner(TabId);
        }
    }
    Managers.Reset();
}
