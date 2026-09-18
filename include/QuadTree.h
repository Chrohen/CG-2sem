#ifndef QUAD_TREE_H
#define QUAD_TREE_H

#include <vector>
#include <memory>
#include <DirectXMath.h>

struct TerrainNode {
    DirectX::XMFLOAT3 center = { 0.f, 0.f, 0.f };
    DirectX::XMFLOAT3 halfExtents = { 0.f, 0.f, 0.f };

    int level = 0;

    DirectX::XMFLOAT2 uvMin = { 0.f, 0.f };
    DirectX::XMFLOAT2 uvMax = { 1.f, 1.f };

    bool  useAsLeaf = true;
    bool  visible = false;
    float sse = 0.f;

    std::unique_ptr<TerrainNode> children[4];

    TerrainNode* parent = nullptr;
};

struct QuadTreeParams {
    float worldCenterX = 0.f;
    float worldCenterZ = 0.f;
    float worldSizeXZ = 1000.f;
    float minHeight = 0.f;
    float maxHeight = 100.f;

    int   maxLevel = 8;
    float lodErrorThresholdPx = 256.f;
};

class QuadTree {
public:
    QuadTree() = default;

    void Init(const QuadTreeParams& params);

    void Update(const DirectX::XMFLOAT3& cameraPos,
        const DirectX::XMVECTOR    frustumPlanes[6],
        float                      screenHeight,
        float                      fovY);

    void GetVisibleLeaves(std::vector<TerrainNode*>& outLeaves) const;

    void GetAllLeaves(std::vector<TerrainNode*>& outLeaves) const;

    const QuadTreeParams& Params() const { return m_params; }
    TerrainNode* Root() { return m_root.get(); }
    int MaxDepthReached() const { return m_currentMaxDepthReached; }

private:
    void UpdateNode(TerrainNode* node,
        const DirectX::XMFLOAT3& camPos,
        const DirectX::XMVECTOR    frustumPlanes[6],
        float screenHeight,
        float fovY);

    void Subdivide(TerrainNode* node);

    static bool FrustumCull(const DirectX::XMFLOAT3& center,
        const DirectX::XMFLOAT3& halfExtents,
        const DirectX::XMVECTOR    frustumPlanes[6]);

    float ComputeScreenSpaceError(const TerrainNode* node,
        const DirectX::XMFLOAT3& camPos,
        float screenHeight,
        float fovY) const;

    QuadTreeParams m_params;
    std::unique_ptr<TerrainNode> m_root;
    int m_currentMaxDepthReached = 0;
};

#endif // QUAD_TREE_H