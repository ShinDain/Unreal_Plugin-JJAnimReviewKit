#include "JJAnimReviewEngineCompat.h"

#include "JJAnimReviewService.h"
#include "IAnimationEditorModule.h"
#include "IPersonaToolkit.h"
#include "IPersonaPreviewScene.h"
#include "Animation/AnimSequence.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Misc/MessageDialog.h"

namespace JJAnimReviewPersonaCompatPrivate
{
    FDelegateHandle ToolbarHandle;

    void CreateReview(TWeakPtr<IAnimationEditor> Weak)
    {
        const TSharedPtr<IAnimationEditor> Editor = Weak.Pin();
        if (!Editor)
        {
            return;
        }
        const UAnimSequence* Animation = Cast<UAnimSequence>(Editor->GetPersonaToolkit()->GetPreviewScene()->GetPreviewAnimationAsset());
        if (!Animation)
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TEXT("Open an AnimSequence first.")));
            return;
        }
        FJJAnimReviewRequest Request;
        Request.Asset = Animation->GetPathName();
        Request.bCapture = true;
        const FJJAnimReviewResponse Response = JJAnimReviewService::Run(Request);
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Response.ExitCode == 2 ? Response.Error : Response.TakeDirectory));
    }
}

void JJAnimReviewEngineCompat::RegisterPersona()
{
    // A12: Public per-animation-editor toolbar extender, no global polling.
    if (IsRunningCommandlet())
    {
        return;
    }
    auto& Module = FModuleManager::LoadModuleChecked<IAnimationEditorModule>(TEXT("AnimationEditor"));
    auto Delegate = IAnimationEditorModule::FAnimationEditorToolbarExtender::CreateLambda(
        [](const TSharedRef<FUICommandList>, TSharedRef<IAnimationEditor> Editor)
        {
            TSharedRef<FExtender> Extender = MakeShared<FExtender>();
            const TWeakPtr<IAnimationEditor> Weak = Editor;
            Extender->AddToolBarExtension(TEXT("Asset"), EExtensionHook::After, Editor->GetToolkitCommands(),
                FToolBarExtensionDelegate::CreateLambda([Weak](FToolBarBuilder& Builder)
                {
                    Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([Weak]()
                    {
                        JJAnimReviewPersonaCompatPrivate::CreateReview(Weak);
                    })), NAME_None, FText::FromString(TEXT("리뷰 묶음 만들기")),
                        FText::FromString(TEXT("Read-only checks and contact sheet")), FSlateIcon());
                    Builder.AddToolBarButton(FUIAction(FExecuteAction::CreateLambda([Weak]()
                    {
                        JJAnimReviewEngineCompat::OpenAnnotations(Weak);
                    })), NAME_None, FText::FromString(TEXT("리뷰 주석")),
                        FText::FromString(TEXT("Annotations, pose instructions and take comparison")), FSlateIcon());
                }));
            return Extender;
        });
    JJAnimReviewPersonaCompatPrivate::ToolbarHandle = Delegate.GetHandle();
    Module.GetAllAnimationEditorToolbarExtenders().Add(Delegate);
}

void JJAnimReviewEngineCompat::UnregisterPersona()
{
    CloseAnnotationTabs();
    if (auto* Module = FModuleManager::GetModulePtr<IAnimationEditorModule>(TEXT("AnimationEditor")))
    {
        Module->GetAllAnimationEditorToolbarExtenders().RemoveAll([](const auto& Delegate)
        {
            return Delegate.GetHandle() == JJAnimReviewPersonaCompatPrivate::ToolbarHandle;
        });
    }
}
