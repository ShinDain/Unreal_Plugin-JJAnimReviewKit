#include "Modules/ModuleManager.h"

#include "HAL/IConsoleManager.h"
#include "JJAnimReviewService.h"
#include "JJAnimReviewEngineCompat.h"
#include "Materials/Material.h"

DEFINE_LOG_CATEGORY_STATIC(LogJJAnimReviewConsole, Log, All);

class FJJAnimReviewKitEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        Command = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("JJAnimReview.Create"),
            TEXT("JJAnimReview.Create <AnimSequence> [Profile or -] [Output subfolder]"),
            FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
            {
                CreateReview(Args, false);
            }), ECVF_Default);
        SheetCommand = IConsoleManager::Get().RegisterConsoleCommand(
            TEXT("JJAnimReview.Sheet"), TEXT("JJAnimReview.Sheet <AnimSequence> [Profile or -] [Output]"),
            FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
            {
                CreateReview(Args, true);
            }), ECVF_Default);
        JJAnimReviewEngineCompat::RegisterPersona();
    }

    virtual void ShutdownModule() override
    {
        JJAnimReviewEngineCompat::UnregisterPersona();
        GhostMaterial.Material.Reset();
        if (SheetCommand)
        {
            IConsoleManager::Get().UnregisterConsoleObject(SheetCommand);
            SheetCommand = nullptr;
        }
        if (Command)
        {
            IConsoleManager::Get().UnregisterConsoleObject(Command);
            Command = nullptr;
        }
    }

    FJJReviewGhostMaterialState GhostMaterial;

private:
    void CreateReview(const TArray<FString>& Args, bool bCapture)
    {
        if (Args.IsEmpty() || Args.Num() > 3)
        {
            UE_LOG(LogJJAnimReviewConsole, Error, TEXT("Usage: JJAnimReview.Create <AnimSequence> [Profile or -] [Output subfolder]"));
            return;
        }
        FJJAnimReviewRequest Request;
        Request.bCapture = bCapture;
        Request.Asset = Args[0];
        if (Args.Num() >= 2 && Args[1] != TEXT("-"))
        {
            Request.Profile = Args[1];
        }
        if (Args.Num() >= 3)
        {
            Request.Output = Args[2];
        }
        const FJJAnimReviewResponse Response = JJAnimReviewService::Run(Request);
        if (Response.ExitCode == 2)
        {
            UE_LOG(LogJJAnimReviewConsole, Error, TEXT("%s"), *Response.Error);
        }
        else
        {
            UE_LOG(LogJJAnimReviewConsole, Display, TEXT("Review bundle: %s (exit %d)"), *Response.TakeDirectory, Response.ExitCode);
        }
        for (const FString& Warning : Response.Warnings)
        {
            UE_LOG(LogJJAnimReviewConsole, Warning, TEXT("%s"), *Warning);
        }
    }

    IConsoleObject* Command = nullptr;
    IConsoleObject* SheetCommand = nullptr;
};

IMPLEMENT_MODULE(FJJAnimReviewKitEditorModule, JJAnimReviewKitEditor)

FJJReviewGhostMaterialState& JJAnimReviewEngineCompat::GhostMaterialState()
{
    return FModuleManager::GetModuleChecked<FJJAnimReviewKitEditorModule>(TEXT("JJAnimReviewKitEditor")).GhostMaterial;
}
