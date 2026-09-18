#ifndef TERRAIN_H
#define TERRAIN_H

#include <memory>
#include <string>
#include <vector>

#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>

#include "QuadTree.h"
#include "HeightMap.h"
#include "UploadBuffer.hpp"
#include "RenderStructs.hpp"

class Terrain
{
public:
    bool Init(ID3D12Device* device,
        ID3D12CommandQueue* queue,
        const std::wstring& heightmapPath,
        float worldSizeXZ,
        float heightScale,
        float worldCenterX,
        float worldCenterZ,
        float worldOffsetY,
        int   maxLevel);

    void Update(const DirectX::XMFLOAT3& camPos,
        const DirectX::XMFLOAT3& camTarget,
        const DirectX::XMFLOAT3& camUp,
        float fovY,
        float aspect,
        float nearZ,
        float farZ,
        UINT  screenW,
        UINT  screenH);

    void Draw(ID3D12GraphicsCommandList* cmdList,
        ID3D12PipelineState* pso,
        ID3D12RootSignature* rootSig,
        D3D12_GPU_VIRTUAL_ADDRESS passCB);

    const std::vector<TerrainNode*>& VisibleLeaves() const { return m_visibleLeaves; }
    const QuadTree& GetQuadTree()  const { return m_quadtree; }
    const HeightMap& GetHeightMap() const { return m_heightmap; }

    float WorldSizeXZ()  const { return m_worldSizeXZ; }
    float HeightScale()  const { return m_heightScale; }
    float WorldCenterX() const { return m_worldCenterX; }
    float WorldCenterZ() const { return m_worldCenterZ; }
    float WorldOffsetY() const { return m_worldOffsetY; }

private:
    QuadTree  m_quadtree;
    HeightMap m_heightmap;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_diffuseTex;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_normalTex;

    float m_materialMetalness = 0.0f;
    float m_materialRoughness = 0.85f;

    float m_worldSizeXZ = 1000.f;
    float m_heightScale = 100.f;
    float m_worldCenterX = 0.f;
    float m_worldCenterZ = 0.f;
    float m_worldOffsetY = 0.f;

    std::vector<TerrainNode*> m_visibleLeaves;

    static constexpr UINT kGridSegments = 32;
    static constexpr UINT kGridVerts = (kGridSegments + 1) * (kGridSegments + 1);
    static constexpr UINT kGridIndices = kGridSegments * kGridSegments * 6;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_vb;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_ib;
    D3D12_VERTEX_BUFFER_VIEW m_vbView = {};
    D3D12_INDEX_BUFFER_VIEW  m_ibView = {};
    UINT                     m_indexCount = 0;

    void BuildMesh(ID3D12Device* device, ID3D12CommandQueue* queue);

    static constexpr UINT kMaxLeaves = 4096;
    std::unique_ptr<UploadBuffer<TerrainConstants>> m_leafCBs;

    void BuildLeafCBs(ID3D12Device* device);

    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_srvHeap;

    static void ComputeFrustumPlanes(DirectX::XMVECTOR planes[6],
        const DirectX::XMFLOAT3& camPos,
        const DirectX::XMFLOAT3& camTarget,
        const DirectX::XMFLOAT3& camUp,
        float fovY, float aspect,
        float nearZ, float farZ);
};

#endif // TERRAIN_H