#include "JJAnimReviewEngineCompat.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Components/InstancedSkinnedMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "UObject/Package.h"
#include "RHI.h"

bool JJAnimReviewEngineCompat::WeightedBones(USkeletalMesh* Mesh, TSet<FName>& Bones)
{
    check(IsInGameThread());
    Bones.Reset();
    // A22: public LOD0 section bone maps are a conservative skinning-bone set.
    const FSkeletalMeshRenderData* Data = Mesh ? Mesh->GetResourceForRendering() : nullptr;
    if (!Data || Data->LODRenderData.IsEmpty())
    {
        return false;
    }
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    for (const auto& Section : Data->LODRenderData[0].RenderSections)
    {
        for (FBoneIndexType Bone : Section.BoneMap)
        {
            if (Skeleton.IsValidIndex(Bone))
            {
                Bones.Add(Skeleton.GetBoneName(Bone));
            }
        }
    }
    return true;
}

bool JJAnimReviewEngineCompat::VisualizationMaterialReady(UMaterial* Material, USkinnedMeshComponent* Mesh, FString& Reason)
{
    check(IsInGameThread());
    // A23: read-only preflight, never CheckMaterialUsage/SetMaterialUsage (which can dirty assets).
    if (!Material || !Mesh || Material->MaterialDomain != MD_Surface || Material->GetOutermost()->IsDirty())
    {
        Reason = TEXT("엔진 머티리얼의 Surface domain 또는 clean package 조건 불충족: 선/본 가중치 보기로 대체합니다.");
        return false;
    }
    const USkeletalMesh* Asset = Cast<USkeletalMesh>(Mesh->GetSkinnedAsset());
    const FSkeletalMeshRenderData* Data = Asset ? Asset->GetResourceForRendering() : nullptr;
    bool bClothing = false;
    bool bDeformer = Mesh->GetMeshDeformerInstance() != nullptr;
    if (Data)
    {
        for (int32 LODIndex = 0; LODIndex < Data->LODRenderData.Num(); ++LODIndex)
        {
            bDeformer |= Mesh->GetMeshDeformerInstanceForLOD(LODIndex) != nullptr;
            for (const auto& Section : Data->LODRenderData[LODIndex].RenderSections)
            {
                bClothing |= Section.HasClothingData();
            }
        }
    }
    const auto Require = [&](EMaterialUsage Usage, bool bRequired, const TCHAR* Name)
    {
        if (bRequired && !Material->GetUsageByFlag(Usage))
        {
            Reason += FString::Printf(TEXT("엔진 머티리얼 %s 플래그 없음. "), Name);
            return false;
        }
        return true;
    };
    // A23: mirror all conditional SkeletalMeshSceneProxy usage checks without mutating the material.
    bool bUsageValid = Require(MATUSAGE_SkeletalMesh, true, TEXT("SkeletalMesh"));
    bUsageValid &= Require(MATUSAGE_MorphTargets, Asset && !Asset->GetMorphTargets().IsEmpty(), TEXT("MorphTargets"));
    bUsageValid &= Require(MATUSAGE_Clothing, bClothing, TEXT("Clothing"));
    bUsageValid &= Require(MATUSAGE_MeshDeformer, bDeformer, TEXT("MeshDeformer"));
    bUsageValid &= Require(MATUSAGE_InstancedSkinnedMesh, Mesh->IsA<UInstancedSkinnedMeshComponent>(), TEXT("InstancedSkinnedMesh"));
    if (!bUsageValid)
    {
        Reason += TEXT("본 가중치·색 선으로 대체합니다.");
        return false;
    }
    const FMaterialResource* Resource = Material->GetMaterialResource(GMaxRHIShaderPlatform);
    if (!Resource || !Resource->IsCompilationFinished() || !Resource->GetGameThreadShaderMap()
        || !Resource->GetCompileErrors().IsEmpty())
    {
        Reason = TEXT("엔진 머티리얼 셰이더가 준비되지 않아 선/본 가중치 보기로 대체합니다. 컴파일 뒤 새로고침하세요.");
        return false;
    }
    return true;
}

UMaterial* JJAnimReviewEngineCompat::VertexColorMaterial(USkinnedMeshComponent* Mesh, FString& Reason)
{
    check(IsInGameThread());
    // A23: UE5.8 read-only commandlet probe confirmed used_with_skeletal_mesh=True.
    UMaterial* Material = LoadObject<UMaterial>(nullptr,
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    return VisualizationMaterialReady(Material, Mesh, Reason) ? Material : nullptr;
}

bool JJAnimReviewEngineCompat::BoneWeightMaterialReady(USkinnedMeshComponent* Mesh, FString& Reason)
{
    // A23: SetShowBoneWeight uses this exact engine material; fallback must also be read-only safe.
    return VisualizationMaterialReady(GEngine ? GEngine->BoneWeightMaterial.Get() : nullptr, Mesh, Reason);
}

bool JJAnimReviewEngineCompat::BuildVertexColors(USkeletalMesh* Mesh,
    const TMap<FName, FJJReviewBoneColor>& Colors, TMap<int32, TArray<FLinearColor>>& LODColors,
    FString& Reason, bool* bNoTargets)
{
    check(IsInGameThread());
    LODColors.Reset();
    if (bNoTargets)
    {
        *bNoTargets = false;
    }
    const FSkeletalMeshRenderData* Data = Mesh ? Mesh->GetResourceForRendering() : nullptr;
    if (!Data)
    {
        Reason = TEXT("렌더 메시 데이터 없음: 선/본 가중치 보기로 대체합니다.");
        return false;
    }
    const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
    // A24: use only CPU-retained public render skin weights and section-local BoneMap indices.
    bool bAnyTargetInfluence = false;
    for (int32 LODIndex = 0; LODIndex < Data->LODRenderData.Num(); ++LODIndex)
    {
        const auto& LOD = Data->LODRenderData[LODIndex];
        const auto* Weights = LOD.GetSkinWeightVertexBuffer();
        if (!Weights || !Weights->GetNeedsCPUAccess() || !Weights->GetDataVertexBuffer()->GetWeightData()
            || (Weights->GetVariableBonesPerVertex() && !Weights->GetLookupVertexBuffer()->GetNeedsCPUAccess())
            || Weights->GetNumVertices() != LOD.GetNumVertices() || LOD.GetNumVertices() == 0)
        {
            Reason += FString::Printf(TEXT("LOD %d: CPU 가중치 없음 또는 렌더 정점 수 불일치, 칠하기 생략. "), LODIndex);
            continue;
        }
        TArray<FLinearColor> Vertices;
        Vertices.Init(FLinearColor(0.35f, 0.35f, 0.35f), LOD.GetNumVertices());
        bool bValid = true;
        bool bLODTargetInfluence = false;
        TArray<FJJReviewVertexInfluence> Influences;
        for (const auto& Section : LOD.RenderSections)
        {
            if (static_cast<uint64>(Section.BaseVertexIndex) + Section.NumVertices > LOD.GetNumVertices())
            {
                bValid = false;
                break;
            }
            for (uint32 Vertex = Section.BaseVertexIndex; Vertex < Section.BaseVertexIndex + Section.NumVertices; ++Vertex)
            {
                Influences.Reset();
                uint32 Offset = 0;
                uint32 Count = 0;
                Weights->GetVertexInfluenceOffsetCount(Vertex, Offset, Count);
                for (uint32 Influence = 0; Influence < Count; ++Influence)
                {
                    const uint16 Weight = Weights->GetBoneWeight(Vertex, Influence);
                    if (Weight == 0)
                    {
                        continue;
                    }
                    const uint32 LocalBone = Weights->GetBoneIndex(Vertex, Influence);
                    if (!Section.BoneMap.IsValidIndex(LocalBone) || !Skeleton.IsValidIndex(Section.BoneMap[LocalBone]))
                    {
                        bValid = false;
                        break;
                    }
                    Influences.Add({Skeleton.GetBoneName(Section.BoneMap[LocalBone]), static_cast<float>(Weight)});
                    bLODTargetInfluence |= Colors.Contains(Influences.Last().Bone);
                }
                if (!bValid)
                {
                    break;
                }
                Vertices[Vertex] = JJAnimReviewNotes::VertexColor(Influences, Colors);
            }
            if (!bValid)
            {
                break;
            }
        }
        if (bValid && Vertices.Num() == static_cast<int32>(LOD.GetNumVertices()))
        {
            bAnyTargetInfluence |= bLODTargetInfluence;
            LODColors.Add(LODIndex, MoveTemp(Vertices));
        }
        else
        {
            Reason += FString::Printf(TEXT("LOD %d: 정점/본 맵 불일치, 칠하기 생략. "), LODIndex);
        }
    }
    if (!bAnyTargetInfluence && !LODColors.IsEmpty())
    {
        LODColors.Reset();
        if (bNoTargets)
        {
            *bNoTargets = true;
        }
        Reason = TEXT("선택 대상에 스킨 가중치가 없어 칠할 정점이 없습니다.");
    }
    return !LODColors.IsEmpty();
}
