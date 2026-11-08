#include "JJAnimReviewEngineCompat.h"

#include "JJAnimReviewComparison.h"
#include "JJAnimReviewEditing.h"
#include "JJAnimReviewService.h"
#include "JJAnimReviewStore.h"
#include "JJAnimReviewNotes.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "HAL/PlatformProcess.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "IAnimationEditor.h"
#include "IPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "Animation/AnimSequence.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Engine/SkinnedAsset.h"
#include "Components/LineBatchComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "LODSyncInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
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
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("새로고침"))).ToolTipText(FText::FromString(TEXT("기본 출력 폴더의 최신 리뷰를 편집합니다. 추가는 현재 프레임·선택 본을 사용합니다."))).OnClicked_Lambda([this]()
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
                            if (!bGhost)
                            {
                                LastGhostMessage.Reset();
                            }
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
                                if (Choice != SelectedGhostTake)
                                {
                                    LastGhostMessage.Reset();
                                }
                                SelectedGhostTake = Choice;
                                LoadGhost();
                            }
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6, 0)
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("고스트 옆 간격(cm)")))
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SSpinBox<double>)
                        .MinValue(-500.0).MaxValue(500.0)
                        .MinSliderValue(-500.0).MaxSliderValue(500.0).Delta(10.0)
                        .Value_Lambda([this]()
                        {
                            return GhostSpacingCm;
                        })
                        .OnValueChanged_Lambda([this](double Value)
                        {
                            GhostSpacingCm = FMath::Clamp(Value, -500.0, 500.0);
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SSeparator)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    BuildNoteTools()
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SSeparator)
                ]
                + SVerticalBox::Slot().FillHeight(1)
                [
                    SAssignNew(Rows, SScrollBox)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SSeparator)
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
                    BuildRangeTools()
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
                    SNew(SButton).Text(FText::FromString(TEXT("본 수정 초기화"))).OnClicked_Lambda([this]()
                    {
                        const auto Scene = GetScene();
                        FString Message;
                        if (JJAnimReviewEngineCompat::ResetPreviewBones(
                            Scene ? Scene->GetPreviewMeshComponent() : nullptr, Message))
                        {
                            Scene->InvalidateViews();
                        }
                        Status->SetText(FText::FromString(Message));
                        return FReply::Handled();
                    })
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SButton).Text(FText::FromString(TEXT("선택 주석에 PNG/JPG 첨부"))).OnClicked_Lambda([this]()
                    {
                        AttachImage();
                        return FReply::Handled();
                    })
                ]
                + SVerticalBox::Slot().AutoHeight().MaxHeight(120)
                [
                    SAssignNew(Attachments, SScrollBox)
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(Status, STextBlock).AutoWrapText(true)
                ]
            ];
            if (const auto Scene = GetScene())
            {
                // A18: public selection event broadcasts the complete selected-name array.
                BoundScene = Scene;
                SelectionHandle = Scene->RegisterOnSelectedBonesChanged(FOnSelectedBonesChanged::CreateSP(this, &SAnnotations::BonesChanged));
                Scene->RegisterOnAnimChanged(FOnAnimChanged::CreateSP(this, &SAnnotations::AnimationChanged));
                Scene->RegisterOnPreviewMeshChanged(FOnPreviewMeshChanged::CreateSP(this, &SAnnotations::MeshChanged));
                if (const auto* Mesh = Scene->GetPreviewMeshComponent())
                {
                    const int32 Index = Scene->GetSelectedBoneIndex();
                    if (Index >= 0 && Index < Mesh->GetNumBones())
                    {
                        SelectedBones.Add(Mesh->GetBoneName(Index));
                    }
                }
            }
            Refresh();
        }

        virtual ~SAnnotations() override
        {
            RestoreWeight();
            StopDrawing();
            if (const auto Scene = BoundScene.Pin())
            {
                Scene->UnregisterOnSelectedBonesChanged(SelectionHandle);
                Scene->UnregisterOnAnimChanged(this);
                Scene->UnregisterOnPreviewMeshChanged(this);
            }
        }

        TSharedRef<SWidget> BuildRangeTools()
        {
            return SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("시작=현재 프레임"))).OnClicked_Lambda([this]()
                        {
                            StartFrame = CurrentFrame();
                            FinishFrame = FMath::Max(StartFrame, FinishFrame);
                            bRangeFromSelection = false;
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("끝=현재 프레임"))).OnClicked_Lambda([this]()
                        {
                            FinishFrame = CurrentFrame();
                            if (StartFrame == INDEX_NONE)
                            {
                                StartFrame = FinishFrame;
                            }
                            bRangeFromSelection = false;
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("단일 프레임"))).OnClicked_Lambda([this]()
                        {
                            StartFrame = INDEX_NONE;
                            FinishFrame = INDEX_NONE;
                            bRangeFromSelection = false;
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(STextBlock).Text_Lambda([this]()
                        {
                            if (StartFrame == INDEX_NONE)
                            {
                                return FText::FromString(TEXT("현재 프레임"));
                            }
                            const FString Range = FString::Printf(TEXT("%d–%df"), StartFrame, FinishFrame);
                            return FText::FromString(bRangeFromSelection
                                ? TEXT("수정: ") + Range + TEXT(" / 추가: 현재 프레임") : Range);
                        })
                    ]
                ]
                ;
        }

        TSharedRef<SWidget> BuildNoteTools()
        {
            Sides =
            {
                MakeShared<FString>(TEXT("가운데")), MakeShared<FString>(TEXT("왼쪽")), MakeShared<FString>(TEXT("오른쪽"))
            };
            return SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()
                [
                    SAssignNew(GroupStatus, STextBlock).AutoWrapText(true)
                    .Visibility_Lambda([this]()
                    {
                        return GroupStatus->GetText().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
                    })
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState Value)
                    {
                        bGroups = Value == ECheckBoxState::Checked;
                        SyncDrawing();
                    })
                    [
                        SNew(STextBlock).Text(FText::FromString(TEXT("그룹 표시")))
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SAssignNew(GroupChoice, STextComboBox).OptionsSource(&GroupChoices)
                        .OnSelectionChanged_Lambda([this](TSharedPtr<FString> Choice, ESelectInfo::Type)
                        {
                            ChosenGroup = Choice;
                            if (const auto* Group = FindGroup())
                            {
                                GroupName->SetText(FText::FromString(Group->Name));
                                int32 SideIndex = 0;
                                if (Group->Side == TEXT("left"))
                                {
                                    SideIndex = 1;
                                }
                                else if (Group->Side == TEXT("right"))
                                {
                                    SideIndex = 2;
                                }
                                GroupSide->SetSelectedItem(Sides[SideIndex]);
                            }
                            if (!bRefreshing)
                            {
                                ShowWeight();
                                SyncDrawing();
                            }
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("그룹 삭제"))).OnClicked_Lambda([this]()
                        {
                            SaveGroup(TEXT("delete"));
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("기본 그룹 복원"))).OnClicked_Lambda([this]()
                        {
                            RestoreGroups();
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SAssignNew(GroupName, SEditableTextBox).HintText(FText::FromString(TEXT("그룹 이름")))
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SAssignNew(GroupSide, STextComboBox).OptionsSource(&Sides).InitiallySelectedItem(Sides[0])
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("선택 본으로 그룹 저장"))).OnClicked_Lambda([this]()
                        {
                            SaveGroup(TEXT("save"));
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("이름 변경"))).OnClicked_Lambda([this]()
                        {
                            SaveGroup(TEXT("rename"));
                            return FReply::Handled();
                        })
                    ]
                ]
                + SVerticalBox::Slot().AutoHeight()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SCheckBox).OnCheckStateChanged_Lambda([this](ECheckBoxState Value)
                        {
                            bChildren = Value == ECheckBoxState::Checked;
                            ShowWeight();
                            SyncDrawing();
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("자식 본 포함")))
                        ]
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SCheckBox).IsChecked_Lambda([this]()
                        {
                            return bPaintSelection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                        }).OnCheckStateChanged_Lambda([this](ECheckBoxState Value)
                        {
                            bPaintSelection = Value == ECheckBoxState::Checked;
                            ShowWeight();
                            SyncDrawing();
                        })
                        [
                            SNew(STextBlock).Text(FText::FromString(TEXT("선택 그룹 메시 칠하기")))
                        ]
                    ]
                ];
        }

        int32 CurrentFrame() const
        {
            const auto Scene = GetScene();
            const auto* Anim = Animation();
            const auto* Mesh = Scene ? Scene->GetPreviewMeshComponent() : nullptr;
            return Anim && Mesh && Mesh->GetSingleNodeInstance()
                ? FMath::RoundToInt(Mesh->GetSingleNodeInstance()->GetCurrentTime() * Anim->GetSamplingFrameRate().AsDecimal()) : 0;
        }

        const FJJReviewBoneGroup* FindGroup() const
        {
            return ChosenGroup.IsValid() ? Groups.FindByPredicate([this](const auto& Group)
            {
                return Group.Name == *ChosenGroup;
            }) : nullptr;
        }

        void BonesChanged(const TArray<FName>& Names, ESelectInfo::Type)
        {
            if (SelectedBones == Names)
            {
                return;
            }
            SelectedBones = Names;
            SelectedBones.Remove(NAME_None);
            const FString Side = JJAnimReviewNotes::InferSide(SelectedBones);
            if (Side == TEXT("left"))
            {
                GroupSide->SetSelectedItem(Sides[1]);
            }
            else if (Side == TEXT("right"))
            {
                GroupSide->SetSelectedItem(Sides[2]);
            }
            else
            {
                GroupSide->SetSelectedItem(Sides[0]);
            }
            ShowWeight();
            SyncDrawing();
        }

        void AnimationChanged(UAnimationAsset*)
        {
            bPaintDirty = true;
            bPaintSelection = false;
            RestoreWeight();
            StopDrawing();
            SelectedBones.Reset();
            StartFrame = INDEX_NONE;
            FinishFrame = INDEX_NONE;
            Refresh();
        }

        void MeshChanged(USkeletalMesh*, USkeletalMesh*)
        {
            bPaintDirty = true;
            bPaintSelection = false;
            RestoreWeight();
            StopDrawing();
            SelectedBones.Reset();
            Refresh();
        }

        TArray<FName> ExpandedSelection() const
        {
            TArray<FName> Names;
            TArray<int32> Parents;
            JJAnimReviewEngineCompat::SkeletonBones(Animation(), Names, Parents);
            TArray<FName> Result = SelectedBones;
            if (bChildren)
            {
                for (int32 Index = 0; Index < Names.Num(); ++Index)
                {
                    int32 Parent = Parents[Index];
                    while (Names.IsValidIndex(Parent))
                    {
                        if (SelectedBones.Contains(Names[Parent]))
                        {
                            Result.AddUnique(Names[Index]);
                            break;
                        }
                        Parent = Parents[Parent];
                    }
                }
            }
            return Result;
        }

        void LoadGroups()
        {
            const FString PreviousGroup = ChosenGroup.IsValid() ? *ChosenGroup : FString();
            ChosenGroup.Reset();
            GroupChoices.Reset();
            GroupChoices.Add(MakeShared<FString>(TEXT("선택 본 (단일/여러 본)")));
            FString Error;
            bool bMatched = false;
            bGroupsValid = JJAnimReviewEngineCompat::LoadBoneGroups(Animation(), UserGroups, Groups, bMatched, Error);
            for (const auto& Group : Groups)
            {
                if (!Group.Bones.IsEmpty())
                {
                    GroupChoices.Add(MakeShared<FString>(Group.Name));
                }
            }
            GroupChoice->RefreshOptions();
            GroupChoice->SetSelectedItem(GroupChoices[0]);
            for (const auto& Choice : GroupChoices)
            {
                if (*Choice == PreviousGroup)
                {
                    GroupChoice->SetSelectedItem(Choice);
                    break;
                }
            }
            if (!bGroupsValid || !bMatched)
            {
                GroupStatus->SetText(FText::FromString(bGroupsValid ? TEXT("마네킹 기준 본 12/16 미만: 기본 템플릿 미적용. 사용자 그룹을 만드세요.") : Error));
            }
            else
            {
                GroupStatus->SetText(FText::GetEmpty());
            }
        }

        void SaveGroup(const FString& Operation)
        {
            if (!bGroupsValid)
            {
                Status->SetText(FText::FromString(TEXT("그룹 파일 오류를 해결한 뒤 새로고침하세요.")));
                return;
            }
            const auto* Existing = FindGroup();
            if (Operation != TEXT("save") && !Existing)
            {
                Status->SetText(FText::FromString(TEXT("그룹을 먼저 선택하세요.")));
                return;
            }
            FJJReviewBoneGroup Group;
            Group.Name = GroupName->GetText().ToString().TrimStartAndEnd();
            Group.Side = TEXT("center");
            if (GroupSide->GetSelectedItem() == Sides[1])
            {
                Group.Side = TEXT("left");
            }
            else if (GroupSide->GetSelectedItem() == Sides[2])
            {
                Group.Side = TEXT("right");
            }
            if (Operation == TEXT("save"))
            {
                Group.Bones = ExpandedSelection();
            }
            const FString RequestedName = Group.Name;
            TArray<FName> Names;
            TArray<int32> Parents;
            JJAnimReviewEngineCompat::SkeletonBones(Animation(), Names, Parents);
            bool bMatched = false;
            const auto Template = JJAnimReviewNotes::MannequinTemplate(Names, Parents, bMatched);
            EJJReviewGroupOperation ChangeOperation = EJJReviewGroupOperation::Save;
            if (Operation == TEXT("rename"))
            {
                ChangeOperation = EJJReviewGroupOperation::Rename;
            }
            else if (Operation == TEXT("delete"))
            {
                ChangeOperation = EJJReviewGroupOperation::Delete;
            }
            const auto Change = JJAnimReviewNotes::ChangeGroup(UserGroups, Template, Groups,
                Existing ? Existing->Name : FString(), Group, ChangeOperation);
            if (Change.bNoOp)
            {
                return;
            }
            if (!Change.Error.IsEmpty())
            {
                Status->SetText(FText::FromString(Change.Error));
                return;
            }
            FString Error;
            if (!JJAnimReviewEngineCompat::SaveBoneGroups(Animation(), Change.UserGroups, Error))
            {
                Status->SetText(FText::FromString(Error));
                return;
            }
            LoadGroups();
            for (const auto& Choice : GroupChoices)
            {
                if (*Choice == Change.FinalName && Operation != TEXT("delete"))
                {
                    GroupChoice->SetSelectedItem(Choice);
                    break;
                }
            }
            ShowWeight();
            SyncDrawing();
            Status->SetText(FText::FromString(Change.FinalName != RequestedName
                ? TEXT("이름 중복으로 새 그룹 저장: ") + Change.FinalName : TEXT("그룹 변경 저장됨")));
        }

        void RestoreGroups()
        {
            if (!bGroupsValid)
            {
                Status->SetText(FText::FromString(TEXT("그룹 파일 오류를 해결한 뒤 새로고침하세요.")));
                return;
            }
            TArray<FName> Names;
            TArray<int32> Parents;
            JJAnimReviewEngineCompat::SkeletonBones(Animation(), Names, Parents);
            bool bMatched = false;
            const auto Template = JJAnimReviewNotes::MannequinTemplate(Names, Parents, bMatched);
            const auto Changed = JJAnimReviewNotes::RestoreDefaultGroups(UserGroups, Template);
            if (Changed.Num() == UserGroups.Num())
            {
                Status->SetText(FText::FromString(TEXT("숨긴 기본 그룹이 없습니다.")));
                return;
            }
            FString Error;
            if (!JJAnimReviewEngineCompat::SaveBoneGroups(Animation(), Changed, Error))
            {
                Status->SetText(FText::FromString(Error));
                return;
            }
            LoadGroups();
            ShowWeight();
            SyncDrawing();
            Status->SetText(FText::FromString(TEXT("숨긴 기본 그룹을 복원했습니다.")));
        }

        void AttachImage()
        {
            if (Selected.AnnotationId.IsEmpty())
            {
                Status->SetText(FText::FromString(TEXT("이미지를 붙일 주석을 선택하세요.")));
                return;
            }
            TArray<FString> Files;
            auto* Desktop = FDesktopPlatformModule::Get();
            if (!Desktop || !Desktop->OpenFileDialog(nullptr, TEXT("주석 이미지"), TEXT(""), TEXT(""),
                TEXT("PNG/JPG|*.png;*.jpg;*.jpeg"), EFileDialogFlags::None, Files) || Files.IsEmpty())
            {
                return;
            }
            FString Error;
            if (!JJAnimReviewService::EditAnnotationWithImage(Asset, Bundle.ContentId, Selected, TEXT("update"), Files[0], Error))
            {
                Status->SetText(FText::FromString(Error));
                return;
            }
            Refresh();
        }

        void ShowAttachments()
        {
            Attachments->ClearChildren();
            for (const FString& Image : Selected.Images)
            {
                Attachments->AddSlot()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton).Text(FText::FromString(Image)).OnClicked_Lambda([this, Image]()
                        {
                            FString Root;
                            FString Error;
                            FJJAnimReviewStore Store;
                            if (FJJAnimReviewStore::ResolveOutputRoot(TEXT(""), Root, Error) && Store.OpenReadOnly(Root, Asset, Error))
                            {
                                const FString Path = Store.GetImagePath(Image);
                                if (!Path.IsEmpty())
                                {
                                    FPlatformProcess::LaunchFileInDefaultExternalApplication(*Path);
                                }
                            }
                            return FReply::Handled();
                        })
                    ]
                    + SHorizontalBox::Slot().AutoWidth()
                    [
                        SNew(SButton).Text(FText::FromString(TEXT("첨부 해제"))).OnClicked_Lambda([this, Image]()
                        {
                            auto Note = Selected;
                            Note.Images.Remove(Image);
                            FString Error;
                            if (JJAnimReviewService::EditAnnotation(Asset, Bundle.ContentId, Note, TEXT("update"), Error))
                            {
                                Refresh();
                            }
                            else
                            {
                                Status->SetText(FText::FromString(Error));
                            }
                            return FReply::Handled();
                        })
                    ]
                ];
            }
        }

        void ShowWeight()
        {
            bDrawingInputsDirty = true;
        }

        void UpdatePaint(const TMap<FName, FJJReviewBoneColor>& Colors)
        {
            const auto Scene = GetScene();
            auto* Mesh = Scene ? Scene->GetPreviewMeshComponent() : nullptr;
            bool bSame = !bPaintDirty && Colors.Num() == PaintColors.Num();
            for (const auto& Pair : Colors)
            {
                const auto* Old = PaintColors.Find(Pair.Key);
                if (!Old || Old->Color != Pair.Value.Color)
                {
                    bSame = false;
                    break;
                }
            }
            const bool bPersonaView = Mesh && (Mesh->bDrawMorphTargetVerts
                || (Mesh->bDrawBoneInfluences && !bFallbackWeight));
            FString Reason;
            bool bUnsafeMaterial = bPaintApplied
                && !JJAnimReviewEngineCompat::VisualizationMaterialReady(PaintMaterial.Get(), Mesh, Reason);
            if (bForcedPaintLOD && Mesh)
            {
                const auto* PreviewAsset = Mesh->GetSkeletalMeshAsset();
                const auto* Data = PreviewAsset ? PreviewAsset->GetResourceForRendering() : nullptr;
                if (!Data || Mesh->GetForcedLOD() != ForcedPaintLOD + 1 || Mesh->ComputeMinLOD() > ForcedPaintLOD
                    || Data->CurrentFirstLODIdx > ForcedPaintLOD || Data->PendingFirstLODIdx > ForcedPaintLOD)
                {
                    bUnsafeMaterial = true;
                    Reason = TEXT("LOD 설정이 바뀌어 칠한 LOD를 보장할 수 없어 본 가중치·색 선으로 대체합니다.");
                }
            }
            if (bSame && !bUnsafeMaterial && bPersonaView == bPaintBlockedPersona)
            {
                return;
            }
            RestoreWeight();
            PaintColors = Colors;
            bPaintDirty = false;
            bPaintBlockedPersona = bPersonaView;
            if (!Mesh || Colors.IsEmpty())
            {
                if (bPaintSelection || bGroups)
                {
                    Status->SetText(FText::FromString(TEXT("칠할 대상이 없습니다. 본·그룹을 선택하거나 현재 프레임의 주석 표시 구간을 확인하세요.")));
                }
                return;
            }
            if (bPersonaView)
            {
                Status->SetText(FText::FromString(TEXT("Persona의 본 가중치·모프 타깃 보기가 켜져 있어 칠하지 않습니다.")));
                return;
            }
            TMap<int32, TArray<FLinearColor>> LODColors;
            UMaterial* Material = nullptr;
            if (!bUnsafeMaterial)
            {
                Material = JJAnimReviewEngineCompat::VertexColorMaterial(Mesh, Reason);
            }
            bool bNoTargets = false;
            bool bCanPaint = Material && JJAnimReviewEngineCompat::BuildVertexColors(
                Mesh->GetSkeletalMeshAsset(), Colors, LODColors, Reason, &bNoTargets);
            if (bNoTargets)
            {
                Status->SetText(FText::FromString(Reason));
                return;
            }
            // A24: a forced LOD is still clamped by minimum LOD and streaming residency.
            const auto* SkeletalAsset = Mesh->GetSkeletalMeshAsset();
            const auto* RenderData = SkeletalAsset ? SkeletalAsset->GetResourceForRendering() : nullptr;
            const IConsoleVariable* GlobalForcedLOD = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SkeletalMeshForceLOD"));
            if (bCanPaint && RenderData && LODColors.Num() < RenderData->LODRenderData.Num()
                && GlobalForcedLOD && GlobalForcedLOD->GetInt() >= 0)
            {
                bCanPaint = false;
                Reason += TEXT("전역 LOD 강제가 켜져 있어 본 가중치·색 선으로 대체합니다.");
            }
            const bool bPartialLOD = bCanPaint && RenderData && LODColors.Num() < RenderData->LODRenderData.Num();
            if (bPartialLOD)
            {
                const int32 MinimumLOD = FMath::Max3(Mesh->ComputeMinLOD(),
                    static_cast<int32>(RenderData->CurrentFirstLODIdx), static_cast<int32>(RenderData->PendingFirstLODIdx));
                int32 PaintLOD = MAX_int32;
                for (const auto& Pair : LODColors)
                {
                    if (Pair.Key >= MinimumLOD)
                    {
                        PaintLOD = FMath::Min(PaintLOD, Pair.Key);
                    }
                }
                if (PaintLOD != MAX_int32)
                {
                    ILODSyncInterface& LODSync = *Mesh;
                    SavedRenderedLOD = LODSync.GetForceRenderedLOD();
                    SavedStreamedLOD = LODSync.GetForceStreamedLOD();
                    Mesh->SetForcedLOD(PaintLOD + 1);
                    if (Mesh->GetForcedLOD() == PaintLOD + 1)
                    {
                        bForcedPaintLOD = true;
                        ForcedPaintLOD = PaintLOD;
                        Reason += FString::Printf(TEXT("칠하기 중 LOD %d로 고정합니다. 해제 시 원래 LOD 설정을 복원합니다."), PaintLOD);
                    }
                    else
                    {
                        LODSync.SetForceRenderedLOD(SavedRenderedLOD);
                        LODSync.SetForceStreamedLOD(SavedStreamedLOD);
                        bCanPaint = false;
                        Reason += TEXT("외부 LOD 강제로 칠한 LOD를 보장할 수 없어 본 가중치·색 선으로 대체합니다.");
                    }
                }
                else
                {
                    bCanPaint = false;
                    Reason += TEXT("현재 표시 가능한 LOD에 칠할 데이터가 없어 본 가중치·색 선으로 대체합니다.");
                }
            }
            if (!bCanPaint)
            {
                FString WeightReason;
                if (!JJAnimReviewEngineCompat::BoneWeightMaterialReady(Mesh, WeightReason))
                {
                    Status->SetText(FText::FromString(Reason + TEXT(" 본 가중치 머티리얼도 조건 불충족: 색 선만 표시합니다. ") + WeightReason));
                    return;
                }
            }
            WeightMesh = Mesh;
            WeightAsset = Mesh->GetSkinnedAsset();
            SavedMaterialCount = Mesh->GetNumMaterials();
            for (int32 Slot = 0; Slot < SavedMaterialCount; ++Slot)
            {
                // A25: Slate members are invisible to GC; keep original materials strongly referenced.
                SavedMaterials.Emplace(Mesh->GetMaterial(Slot));
            }
            if (bCanPaint)
            {
                PaintMaterial.Reset(Material);
                for (const auto& Pair : LODColors)
                {
                    // A24: exact render-vertex count checked in BuildVertexColors, no engine resize fallback.
                    Mesh->SetVertexColorOverride_LinearColor(Pair.Key, Pair.Value);
                    PaintedLODs.Add(Pair.Key);
                }
                for (int32 Slot = 0; Slot < SavedMaterialCount; ++Slot)
                {
                    Mesh->SetMaterial(Slot, Material);
                }
                bPaintApplied = true;
                if (!JJAnimReviewEngineCompat::VisualizationMaterialReady(Material, Mesh, Reason))
                {
                    RestoreWeight();
                    Status->SetText(FText::FromString(Reason));
                    return;
                }
            }
            else
            {
                Mesh->BonesOfInterest.Reset();
                for (const auto& Pair : Colors)
                {
                    const int32 Index = Mesh->GetBoneIndex(Pair.Key);
                    if (Index != INDEX_NONE)
                    {
                        Mesh->BonesOfInterest.Add(Index);
                    }
                }
                Mesh->SetShowBoneWeight(true);
                bFallbackWeight = true;
                if (Reason.IsEmpty())
                {
                    Reason = TEXT("정점 색을 적용할 수 없어 본 가중치·색 선으로 대체합니다.");
                }
            }
            if (!Reason.IsEmpty())
            {
                Status->SetText(FText::FromString(Reason));
            }
            Mesh->MarkRenderStateDirty();
            Scene->InvalidateViews();
        }

        void RestoreWeight()
        {
            if (auto* Mesh = WeightMesh.Get())
            {
                if (bForcedPaintLOD)
                {
                    ILODSyncInterface& LODSync = *Mesh;
                    LODSync.SetForceRenderedLOD(SavedRenderedLOD);
                    LODSync.SetForceStreamedLOD(SavedStreamedLOD);
                }
                for (int32 LOD : PaintedLODs)
                {
                    Mesh->ClearVertexColorOverride(LOD);
                }
                if (Mesh->GetSkinnedAsset() == WeightAsset.Get() && Mesh->GetNumMaterials() == SavedMaterialCount)
                {
                    if (bFallbackWeight && Mesh->bDrawBoneInfluences)
                    {
                        Mesh->SetShowBoneWeight(false);
                    }
                    if (Mesh->bDrawBoneInfluences || Mesh->bDrawMorphTargetVerts)
                    {
                        // Persona enabled its own view while ours was active. Fix its restore buffer
                        // without disabling that view or leaving our vertex material in it.
                        Mesh->SkelMaterials.Reset();
                        for (const auto& Material : SavedMaterials)
                        {
                            Mesh->SkelMaterials.Add(Material.Get());
                        }
                    }
                    else
                    {
                        for (int32 Slot = 0; Slot < SavedMaterials.Num(); ++Slot)
                        {
                            Mesh->SetMaterial(Slot, SavedMaterials[Slot].Get());
                        }
                    }
                    Mesh->MarkRenderStateDirty();
                }
                else if (bFallbackWeight && Mesh->bDrawBoneInfluences)
                {
                    // Persona replaced the asset before broadcasting: never restore old materials.
                    Mesh->SkelMaterials = Mesh->GetMaterials();
                    Mesh->SetShowBoneWeight(false);
                    Mesh->MarkRenderStateDirty();
                }
                if (bFallbackWeight)
                {
                    Mesh->BonesOfInterest.Reset();
                    const auto Scene = GetScene();
                    if (Scene && Scene->GetPreviewMeshComponent() == Mesh)
                    {
                        const int32 Bone = Scene->GetSelectedBoneIndex();
                        if (Bone != INDEX_NONE)
                        {
                            Mesh->BonesOfInterest.Add(Bone);
                        }
                    }
                }
            }
            WeightMesh.Reset();
            WeightAsset.Reset();
            SavedMaterials.Reset();
            PaintMaterial.Reset();
            PaintedLODs.Reset();
            PaintColors.Reset();
            bForcedPaintLOD = false;
            bPaintApplied = false;
            bFallbackWeight = false;
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
            const FString WeightGroup = bPaintSelection && ChosenGroup.IsValid() ? *ChosenGroup : FString();
            const FString PreviousAsset = Asset;
            bRefreshing = true;
            StartFrame = INDEX_NONE;
            FinishFrame = INDEX_NONE;
            bRangeFromSelection = false;
            RestoreWeight();
            StopDrawing();
            Rows->ClearChildren();
            Attachments->ClearChildren();
            Selected = FJJReviewAnnotation();
            LoadGroups();
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
                const FString Label = FString::Printf(TEXT("%s [%s]%s"), *JJAnimReviewNotes::AnnotationLine(Item), *Item.Status,
                    Item.bFromPreviousVersion ? TEXT(" 이전 버전에서 작성") : TEXT(""));
                Rows->AddSlot()
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot().AutoWidth().Padding(3)
                    [
                        SNew(SColorBlock).Color(JJAnimReviewNotes::GroupColor(Item.GroupName)).Size(FVector2D(12, 12))
                    ]
                    + SHorizontalBox::Slot().FillWidth(1)
                    [
                        SNew(SButton).Text(FText::FromString(Label))
                        .ButtonColorAndOpacity_Lambda([this, Item]()
                        {
                            return Item.AnnotationId == Selected.AnnotationId
                                ? FLinearColor(0.25f, 0.5f, 0.8f) : FLinearColor::White;
                        }).OnClicked_Lambda([this, Item]()
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
            if (!WeightGroup.IsEmpty() && Asset == PreviousAsset)
            {
                for (const auto& Choice : GroupChoices)
                {
                    if (*Choice == WeightGroup)
                    {
                        GroupChoice->SetSelectedItem(Choice);
                        ShowWeight();
                        break;
                    }
                }
            }
            ShowWeight();
        }

        void Select(const FJJReviewAnnotation& Item)
        {
            Selected = Item;
            StartFrame = Item.Frame;
            FinishFrame = JJAnimReviewNotes::EndFrame(Item);
            bRangeFromSelection = true;
            ShowAttachments();
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
            SyncDrawing();
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
            const bool bPoseRange = bPose && StartFrame != INDEX_NONE;
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
                }
                const bool bUseRange = !bPose && StartFrame != INDEX_NONE && (Operation == TEXT("update") || !bRangeFromSelection);
                Item.Frame = bUseRange ? StartFrame : CurrentFrame();
                Item.EndFrame = bUseRange ? FinishFrame : Item.Frame;
                if (Operation == TEXT("add"))
                {
                    if (const auto* Group = FindGroup())
                    {
                        Item.GroupName = Group->Name;
                        Item.Bones = Group->Bones;
                    }
                    else
                    {
                        Item.Bones = ExpandedSelection();
                        if (Item.Bones.Num() == 1)
                        {
                            Item.Bone = Item.Bones[0];
                        }
                    }
                }
                if (bPose)
                {
                    // A14/A30: manual controller names gate both structured and text instructions.
                    const TSet<FName> ModifiedBones = JJAnimReviewEngineCompat::ModifiedPreviewBones(Mesh);
                    if (ModifiedBones.IsEmpty())
                    {
                        Status->SetText(FText::FromString(TEXT("No preview changes above 0.1cm / 0.5deg.")));
                        return;
                    }
                    const double Time = Mesh->GetSingleNodeInstance()->GetCurrentTime();
                    FJJReviewPoseData Original;
                    if (!JJAnimReviewEngineCompat::Sample(Anim, TArray<double>{Time}, Original, Error))
                    {
                        Status->SetText(FText::FromString(Error));
                        return;
                    }
                    const int32 Root = Original.ParentIndices.IndexOfByKey(INDEX_NONE);
                    if (Original.BoneNames.IsValidIndex(Root) && ModifiedBones.Contains(Original.BoneNames[Root])
                        && !JJAnimReviewEngineCompat::NormalizeInstructionRoot(Anim, Mesh, Original, Error))
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
                    Item.PoseInstructions = JJAnimReviewEditing::PoseInstructions(
                        Original, Preview, Signed, 0.1, 0.5, &ModifiedBones);
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
                const FString Message = bPoseRange ? TEXT("포즈 지시는 현재 프레임에 기록됨. ") : TEXT("포즈 지시 저장됨. ");
                Status->SetText(FText::FromString(Message + TEXT("미리보기 수정은 유지됩니다. 아래 '본 수정 초기화'로 지울 수 있습니다.")));
            }
        }

        void StopDrawing()
        {
            ReleaseGhost();
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
            DrawingMesh.Reset();
        }

        void ReleaseGhost()
        {
            // A28: unregister/destroy before releasing GC references, including on pending/failed requests.
            if (GhostMesh.IsValid())
            {
                GhostMesh->SetVisibility(false);
                if (const auto Scene = DrawingScene.Pin())
                {
                    Scene->RemoveComponent(GhostMesh.Get());
                }
                GhostMesh->DestroyComponent();
            }
            GhostMesh.Reset();
            GhostMID.Reset();
            GhostBoneSamples.Reset();
            GhostParents.Reset();
            GhostComponentPose.Reset();
            bGhostMeshFailed = false;
            GhostMessage.Reset();
        }

        void UpdateGhostStatus()
        {
            if (GhostMessage == LastGhostMessage)
            {
                return;
            }
            LastGhostMessage = GhostMessage;
            if (!GhostMessage.IsEmpty())
            {
                Status->SetText(FText::FromString(GhostMessage));
            }
        }

        FTransform SpacedGhostBasis(UDebugSkelMeshComponent* Mesh) const
        {
            // A33: same front as sheet yaw zero, including an explicit review profile.
            const double ForwardYaw = Bundle.bEstimatedProfile
                ? FJJReviewCaptureSettings().ForwardYawOffset : Bundle.ProfileSettings.ForwardYawOffset;
            return JJAnimReviewNotes::OffsetGhostBasis(
                JJAnimReviewEngineCompat::PreviewGhostBasis(Mesh), ForwardYaw, GhostSpacingCm);
        }

        bool UpdateGhost(UDebugSkelMeshComponent* Mesh, double Time)
        {
            check(IsInGameThread());
            if (bGhostMeshFailed)
            {
                UpdateGhostStatus();
                return false;
            }
            if (!GhostMesh.IsValid())
            {
                bool bPending = false;
                UMaterial* Material = JJAnimReviewEngineCompat::GhostMaterial(bPending, GhostMessage);
                if (!Material)
                {
                    bGhostMeshFailed = !bPending;
                    UpdateGhostStatus();
                    // Pending means draw nothing, never the opaque fallback or line ghost.
                    return bPending;
                }
                const auto Scene = DrawingScene.Pin();
                USkeletalMesh* PreviewAsset = Mesh->GetSkeletalMeshAsset();
                if (!Scene || !Scene->GetActor() || !PreviewAsset)
                {
                    GhostMessage = TEXT("선 고스트 사용: 미리보기 액터 또는 메시 없음.");
                    bGhostMeshFailed = true;
                    UpdateGhostStatus();
                    return false;
                }
                const FReferenceSkeleton& Skeleton = PreviewAsset->GetRefSkeleton();
                TArray<FName> MeshBones;
                GhostParents.Reset();
                for (int32 Bone = 0; Bone < Skeleton.GetNum(); ++Bone)
                {
                    MeshBones.Add(Skeleton.GetBoneName(Bone));
                    GhostParents.Add(Skeleton.GetParentIndex(Bone));
                }
                GhostBoneSamples = JJAnimReviewNotes::MapGhostBones(MeshBones, GhostPose.BoneNames);
                if (GhostBoneSamples.Num() != MeshBones.Num())
                {
                    GhostMessage = TEXT("선 고스트 사용: 이전 take에 현재 메시의 본이 없습니다.");
                    bGhostMeshFailed = true;
                    UpdateGhostStatus();
                    return false;
                }
                GhostComponentPose.SetNum(GhostBoneSamples.Num());
                GhostMesh.Reset(NewObject<UPoseableMeshComponent>(Scene->GetActor(), NAME_None, RF_Transient));
                GhostMID.Reset(UMaterialInstanceDynamic::Create(Material, GhostMesh.Get()));
                if (!GhostMesh.IsValid() || !GhostMID.IsValid())
                {
                    ReleaseGhost();
                    GhostMessage = TEXT("선 고스트 사용: 고스트 컴포넌트 또는 MID 생성 실패.");
                    bGhostMeshFailed = true;
                    UpdateGhostStatus();
                    return false;
                }
                GhostMID->SetFlags(RF_Transient);
                GhostMID->SetVectorParameterValue(TEXT("GhostColor"), FLinearColor(1.0f, 0.05f, 0.65f));
                GhostMID->SetScalarParameterValue(TEXT("GhostOpacity"), 0.22f);
                GhostMesh->SetVisibility(false);
                GhostMesh->PrimaryComponentTick.bCanEverTick = false;
                GhostMesh->SetComponentTickEnabled(false);
                GhostMesh->SetCastShadow(false);
                GhostMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
                GhostMesh->SetGenerateOverlapEvents(false);
                GhostMesh->bSelectable = false;
                GhostMesh->SetSkinnedAssetAndUpdate(PreviewAsset);
                for (int32 Slot = 0; Slot < GhostMesh->GetNumMaterials(); ++Slot)
                {
                    GhostMesh->SetMaterial(Slot, GhostMID.Get());
                }
                Scene->AddComponent(GhostMesh.Get(), Mesh->GetComponentTransform());
                GhostMessage = TEXT("이전 테이크 메시 고스트 표시 중 (Loop 누적·루프 경계에서는 위치 오차가 있을 수 있습니다).");
            }

            // A28: public local-pose array avoids repeated parent-chain reconstruction in setters.
            if (GhostMesh->BoneSpaceTransforms.Num() != GhostBoneSamples.Num() || !GhostMesh->IsRegistered())
            {
                ReleaseGhost();
                GhostMessage = TEXT("선 고스트 사용: 고스트 등록 또는 본 변환 배열 불일치.");
                bGhostMeshFailed = true;
                UpdateGhostStatus();
                return false;
            }
            GhostMesh->SetWorldTransform(SpacedGhostBasis(Mesh));
            FTransform RawRoot;
            FTransform LockedRoot;
            const bool bLockRoot = JJAnimReviewEngineCompat::GhostRootLock(Mesh, GhostPose, Time, RawRoot, LockedRoot);
            for (int32 Bone = 0; Bone < GhostBoneSamples.Num(); ++Bone)
            {
                const FTransform PoseTransform = JJAnimReviewComparison::AtTime(GhostPose, GhostBoneSamples[Bone], Time);
                GhostComponentPose[Bone] = bLockRoot
                    ? JJAnimReviewNotes::RebaseRoot(PoseTransform, RawRoot, LockedRoot) : PoseTransform;
            }
            if (!JJAnimReviewNotes::ComponentToLocalPose(GhostParents, GhostComponentPose, GhostMesh->BoneSpaceTransforms))
            {
                ReleaseGhost();
                GhostMessage = TEXT("선 고스트 사용: 고스트 등록 또는 본 변환 배열 불일치.");
                bGhostMeshFailed = true;
                UpdateGhostStatus();
                return false;
            }
            GhostMesh->RefreshBoneTransforms();
            GhostMesh->SetVisibility(true);
            UpdateGhostStatus();
            return true;
        }

        void ResetGhostTakes()
        {
            LastGhostMessage.Reset();
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
                    Status->SetText(FText::FromString(TEXT("이전 테이크 포즈를 읽었습니다.")));
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
            bDrawingInputsDirty = true;
            StopDrawing();
            const bool bAnyNotes = bLoaded && bNotes && Bundle.Annotations.ContainsByPredicate([this](const auto& Item)
            {
                return Item.Status == TEXT("open") || Item.AnnotationId == Selected.AnnotationId;
            });
            const bool bGroupTargets = bLoaded && bGroups && Groups.ContainsByPredicate([](const auto& Group)
            {
                return !Group.Bones.IsEmpty();
            });
            const bool bAny = bAnyNotes || (bLoaded && bGhost && bGhostLoaded) || bGroupTargets || bPaintSelection;
            const auto Scene = GetScene();
            if (!bAny || !Scene || !Scene->GetActor())
            {
                RestoreWeight();
                PaintColors.Reset();
                if (!bAny && (bPaintSelection || bGroups))
                {
                    Status->SetText(FText::FromString(TEXT("칠할 대상이 없습니다. 본·그룹 또는 표시할 주석을 확인하세요.")));
                }
                return;
            }
            auto* PreviewMesh = Scene->GetPreviewMeshComponent();
            TArray<int32> Parents;
            DrawBoneNames.Reset();
            if (PreviewMesh)
            {
                for (int32 Index = 0; Index < PreviewMesh->GetNumBones(); ++Index)
                {
                    const FName Name = PreviewMesh->GetBoneName(Index);
                    DrawBoneNames.Add(Name);
                    Parents.Add(PreviewMesh->GetBoneIndex(PreviewMesh->GetParentBone(Name)));
                }
            }
            TSet<FName> SkinBones;
            const bool bAvailable = JJAnimReviewEngineCompat::WeightedBones(
                PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr, SkinBones);
            DrawSegments = JJAnimReviewNotes::WeightedSegments(DrawBoneNames, Parents, bAvailable ? &SkinBones : nullptr);
            GhostSegments = JJAnimReviewNotes::WeightedSegments(GhostPose.BoneNames, GhostPose.ParentIndices,
                bAvailable ? &SkinBones : nullptr);
            DrawJointMarkers = JJAnimReviewNotes::JointMarkers(DrawBoneNames);
            GhostJointMarkers = JJAnimReviewNotes::JointMarkers(GhostPose.BoneNames);
            // A15: LineBatch scene proxies require an actor owner in UE5.8.
            Lines.Reset(NewObject<ULineBatchComponent>(Scene->GetActor()));
            Lines->SetComponentTickEnabled(false);
            Scene->AddComponent(Lines.Get(), FTransform::Identity);
            DrawingScene = Scene;
            DrawingMesh = PreviewMesh ? PreviewMesh->GetSkeletalMeshAsset() : nullptr;
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
            if (!Mesh || !Anim || Anim->GetPathName() != Asset || !Mesh->GetSingleNodeInstance()
                || Mesh->GetSkeletalMeshAsset() != DrawingMesh.Get())
            {
                RestoreWeight();
                StopDrawing();
                return;
            }
            const int32 Frame = FMath::RoundToInt(Mesh->GetSingleNodeInstance()->GetCurrentTime() * Anim->GetSamplingFrameRate().AsDecimal());
            // Cached membership is valid between adjacent enter/leave boundaries, including reverse scrubs.
            const bool bVisibleChanged = Frame < VisibleFromFrame || Frame >= VisibleUntilFrame;
            if (bDrawingInputsDirty || bVisibleChanged)
            {
                VisibleNotes.Reset();
                VisibleFromFrame = MIN_int64;
                VisibleUntilFrame = MAX_int64;
                if (bNotes && bLoaded)
                {
                    VisibleNotes = JJAnimReviewNotes::VisibleNotes(Bundle.Annotations, Frame, Selected.AnnotationId);
                    const auto Bounds = JJAnimReviewNotes::VisibilityBounds(Bundle.Annotations, Frame, Selected.AnnotationId);
                    VisibleFromFrame = Bounds.FromFrame;
                    VisibleUntilFrame = Bounds.UntilFrame;
                }
                if (bDrawingInputsDirty)
                {
                    DrawSelection = ExpandedSelection();
                }
                const auto* Group = FindGroup();
                DrawColors = JJAnimReviewNotes::BoneColors(Groups, Bundle.Annotations, VisibleNotes,
                    bGroups, bPaintSelection, Group ? Group->Bones : DrawSelection, Group ? Group->Name : FString());
                if (DrawColors.IsEmpty() && (bPaintSelection || bGroups))
                {
                    Status->SetText(FText::FromString(TEXT("칠할 대상이 없습니다. 본·그룹을 선택하거나 현재 프레임의 주석 표시 구간을 확인하세요.")));
                }
                bDrawingInputsDirty = false;
            }
            UpdatePaint(DrawColors);
            if (!bPaintApplied)
            {
                // D16 fallback shares the exact same bone colors/priority; only rendering changes.
                for (const auto& Segment : DrawSegments)
                {
                    const auto* BoneColor = DrawColors.Find(DrawBoneNames[Segment.Bone]);
                    if (!BoneColor)
                    {
                        continue;
                    }
                    const FVector Position = Mesh->GetBoneTransform(Segment.Bone).GetLocation();
                    Lines->DrawLine(Mesh->GetBoneTransform(Segment.Parent).GetLocation(), Position, BoneColor->Color, 0, 2, -1);
                    if (DrawJointMarkers[Segment.Bone])
                    {
                        Lines->DrawPoint(Position, FLinearColor(1.0f, 0.55f, 0.05f), 5, 0, -1);
                    }
                }
            }

            if (bGhost && bGhostLoaded && !UpdateGhost(Mesh, Mesh->GetSingleNodeInstance()->GetCurrentTime()))
            {
                // A28/D16: filtered wire ghost only after material/component failure.
                // A31: mesh and wire share both the basis and any stationary root-lock rebase.
                const double Time = Mesh->GetSingleNodeInstance()->GetCurrentTime();
                const FTransform ToWorld = SpacedGhostBasis(Mesh);
                FTransform RawRoot;
                FTransform LockedRoot;
                const bool bLockRoot = JJAnimReviewEngineCompat::GhostRootLock(Mesh, GhostPose, Time, RawRoot, LockedRoot);
                const FLinearColor GhostColor(1.0f, 0.0f, 1.0f);
                for (const auto& Segment : GhostSegments)
                {
                    const int32 Bone = Segment.Bone;
                    const int32 Parent = Segment.Parent;

                    FTransform Child = JJAnimReviewComparison::AtTime(GhostPose, Bone, Time);
                    FTransform ParentPose = JJAnimReviewComparison::AtTime(GhostPose, Parent, Time);
                    if (bLockRoot)
                    {
                        Child = JJAnimReviewNotes::RebaseRoot(Child, RawRoot, LockedRoot);
                        ParentPose = JJAnimReviewNotes::RebaseRoot(ParentPose, RawRoot, LockedRoot);
                    }
                    const FVector ChildLocation = ToWorld.TransformPosition(Child.GetLocation());
                    const FVector ParentLocation = ToWorld.TransformPosition(ParentPose.GetLocation());
                    Lines->DrawLine(ParentLocation, ChildLocation, GhostColor, 0, 1, -1);
                    if (GhostJointMarkers[Bone])
                    {
                        Lines->DrawPoint(ChildLocation, FLinearColor(1.0f, 0.55f, 0.05f), 5, 0, -1);
                    }
                }
            }

            for (const auto& Entry : VisibleNotes)
            {
                if (!bNotes)
                {
                    continue;
                }
                const auto& Item = Bundle.Annotations[Entry.Index];
                FVector P = FVector::ZeroVector;
                int32 TargetCount = 0;
                for (FName Bone : Entry.Bones)
                {
                    const int32 Index = Mesh->GetBoneIndex(Bone);
                    if (Index != INDEX_NONE)
                    {
                        P += Mesh->GetBoneTransform(Index).GetLocation();
                        ++TargetCount;
                    }
                }
                P = TargetCount > 0 ? P / TargetCount : Mesh->Bounds.Origin;
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
        TWeakPtr<IPersonaPreviewScene> BoundScene;
        FDelegateHandle SelectionHandle;
        TArray<FName> SelectedBones;
        TArray<FJJReviewBoneGroup> UserGroups;
        TArray<FJJReviewBoneGroup> Groups;
        TArray<TSharedPtr<FString>> GroupChoices;
        TSharedPtr<FString> ChosenGroup;
        TSharedPtr<STextComboBox> GroupChoice;
        TSharedPtr<SEditableTextBox> GroupName;
        TArray<TSharedPtr<FString>> Sides;
        TSharedPtr<STextComboBox> GroupSide;
        TSharedPtr<STextBlock> GroupStatus;
        TSharedPtr<SScrollBox> Attachments;
        bool bChildren = false;
        bool bPaintSelection = false;
        bool bGroups = false;
        bool bGroupsValid = false;
        int32 StartFrame = INDEX_NONE;
        int32 FinishFrame = INDEX_NONE;
        bool bRangeFromSelection = false;
        TWeakObjectPtr<UDebugSkelMeshComponent> WeightMesh;
        TWeakObjectPtr<USkinnedAsset> WeightAsset;
        int32 SavedMaterialCount = 0;
        int32 SavedRenderedLOD = INDEX_NONE;
        int32 SavedStreamedLOD = INDEX_NONE;
        int32 ForcedPaintLOD = INDEX_NONE;
        bool bForcedPaintLOD = false;
        TArray<TStrongObjectPtr<UMaterialInterface>> SavedMaterials;
        TStrongObjectPtr<UMaterial> PaintMaterial;
        TArray<int32> PaintedLODs;
        TMap<FName, FJJReviewBoneColor> PaintColors;
        TMap<FName, FJJReviewBoneColor> DrawColors;
        TArray<FJJReviewVisibleNote> VisibleNotes;
        int64 VisibleFromFrame = MIN_int64;
        int64 VisibleUntilFrame = MAX_int64;
        TArray<FName> DrawSelection;
        bool bDrawingInputsDirty = true;
        bool bPaintDirty = true;
        bool bPaintApplied = false;
        bool bFallbackWeight = false;
        bool bPaintBlockedPersona = false;
        TWeakPtr<IAnimationEditor> Editor;
        TWeakPtr<IPersonaPreviewScene> DrawingScene;
        TStrongObjectPtr<ULineBatchComponent> Lines;
        TWeakObjectPtr<USkeletalMesh> DrawingMesh;
        TStrongObjectPtr<UPoseableMeshComponent> GhostMesh;
        TStrongObjectPtr<UMaterialInstanceDynamic> GhostMID;
        TArray<int32> GhostBoneSamples;
        TArray<int32> GhostParents;
        TArray<FTransform> GhostComponentPose;
        bool bGhostMeshFailed = false;
        FString GhostMessage;
        FString LastGhostMessage;
        TArray<FName> DrawBoneNames;
        TArray<bool> DrawJointMarkers;
        TArray<bool> GhostJointMarkers;
        TArray<FJJReviewBoneSegment> DrawSegments;
        TArray<FJJReviewBoneSegment> GhostSegments;
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
        double GhostSpacingCm = 0;
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
