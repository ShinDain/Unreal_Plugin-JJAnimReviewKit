#include "JJAnimReviewEngineCompat.h"

#include "Materials/Material.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "MaterialShared.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "UObject/Package.h"

UMaterial* JJAnimReviewEngineCompat::GhostMaterial(bool& bPending, FString& Reason)
{
    check(IsInGameThread());
    bPending = false;
    FJJReviewGhostMaterialState& State = GhostMaterialState();
    if (!State.bAttempted)
    {
        State.bAttempted = true;
        State.bResubmitted = false;
        // A27: only this plugin-owned transient material receives usage flags.
        State.Material.Reset(NewObject<UMaterial>(GetTransientPackage(),
            MakeUniqueObjectName(GetTransientPackage(), UMaterial::StaticClass(), TEXT("JJReviewGhost")), RF_Transient));
        UMaterial* Material = State.Material.Get();
        if (Material)
        {
            Material->MaterialDomain = MD_Surface;
            Material->BlendMode = BLEND_Translucent;
            Material->SetShadingModel(MSM_Unlit);
            Material->bAutomaticallySetUsageInEditor = false;
            auto* Color = NewObject<UMaterialExpressionVectorParameter>(Material, NAME_None, RF_Transient);
            Color->ParameterName = TEXT("GhostColor");
            Color->Material = Material;
            Color->DefaultValue = FLinearColor(1.0f, 0.05f, 0.65f);
            auto* Opacity = NewObject<UMaterialExpressionScalarParameter>(Material, NAME_None, RF_Transient);
            Opacity->ParameterName = TEXT("GhostOpacity");
            Opacity->Material = Material;
            Opacity->DefaultValue = 0.22f;
            Material->GetExpressionCollection().AddExpression(Color);
            Material->GetExpressionCollection().AddExpression(Opacity);
            Material->GetEditorOnlyData()->EmissiveColor.Expression = Color;
            Material->GetEditorOnlyData()->Opacity.Expression = Opacity;
            Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
            Material->SetUsageByFlag(MATUSAGE_MorphTargets, true);
            Material->SetUsageByFlag(MATUSAGE_Clothing, true);
            Material->SetUsageByFlag(MATUSAGE_MeshDeformer, true);
            // A28: PoseableMesh is not InstancedSkinnedMesh. No instanced usage required.
            // A29: hidden Substrate conversion compiles legacy inputs without graph conversion.
            if (Substrate::IsHiddenMaterialAssetConversionEnabled())
            {
                // A29: None never submits jobs without a render request; the ghost stays hidden.
                Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
            }
            else
            {
                Material->PostEditChange();
                Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
            }
        }
    }

    UMaterial* Material = State.Material.Get();
    if (!Material)
    {
        Reason = TEXT("선 고스트 사용: 임시 고스트 머티리얼 생성 실패.");
        return nullptr;
    }
    FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
    if (!Resource)
    {
        Reason = TEXT("선 고스트 사용: 고스트 머티리얼 셰이더 리소스 없음.");
        return nullptr;
    }
    if (!Resource->IsCompilationFinished())
    {
        bPending = true;
        Reason = TEXT("고스트 머티리얼 준비 중");
        return nullptr;
    }
    if (!Resource->GetCompileErrors().IsEmpty())
    {
        Reason = TEXT("선 고스트 사용: 고스트 머티리얼 셰이더 컴파일 오류. ")
            + Resource->GetCompileErrors()[0];
        return nullptr;
    }
    if (!Resource->GetGameThreadShaderMap())
    {
        Reason = TEXT("선 고스트 사용: 고스트 머티리얼 게임 스레드 셰이더 맵 없음.");
        return nullptr;
    }
    // A29: finalized can stay false with editor job-cache DDC; require completeness instead.
    if (!Resource->IsGameThreadShaderMapComplete())
    {
        if (!State.bResubmitted)
        {
            State.bResubmitted = true;
            Resource->SubmitCompileJobs_GameThread(EShaderCompileJobPriority::High);
            bPending = true;
            Reason = TEXT("고스트 머티리얼 준비 중 (셰이더 한 번 재제출)");
            return nullptr;
        }
        Reason = TEXT("선 고스트 사용: 한 번 재제출한 뒤에도 고스트 머티리얼 셰이더 맵 불완전.");
        return nullptr;
    }
    Reason.Reset();
    return Material;
}
