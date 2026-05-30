#pragma once
#include <vector>
#include <memory>
#include <DirectXMath.h>

struct AABB {
    DirectX::XMFLOAT3 center;
    DirectX::XMFLOAT3 halfExtents;
};

class Octree {
public:
    Octree(const AABB& bounds, int maxDepth = 8, int maxObjectsPerNode = 16);
    ~Octree();

    void Build(const std::vector<AABB>& objectBounds, const std::vector<int>& objectIndices = {});
    void QueryFrustum(const DirectX::XMVECTOR planes[6], std::vector<int>& outIndices) const;
    void GetAllLeafNodes(std::vector<AABB>& outBounds) const;

private:
    struct Node {
        AABB bounds;
        std::vector<int> objects;
        std::unique_ptr<Node> children[8];
        bool isLeaf;

        explicit Node(const AABB& bounds);
    };

    std::unique_ptr<Node> m_root;
    int m_maxDepth;
    int m_maxObjectsPerNode;

    void Subdivide(Node* node, int depth,
        const std::vector<AABB>& objectBounds,
        const std::vector<int>& objectIndices);
    void QueryNode(const Node* node, const DirectX::XMVECTOR planes[6],
        std::vector<int>& outIndices) const;
    static bool IntersectAABB(const AABB& aabb, const DirectX::XMVECTOR planes[6]);
};