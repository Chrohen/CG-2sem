#include "Octree.hpp"
#include <algorithm>
#include <functional>

using namespace DirectX;

static bool IntersectAABB(const AABB& aabb, const XMVECTOR planes[6]) {
    XMVECTOR center = XMLoadFloat3(&aabb.center);
    XMVECTOR half = XMLoadFloat3(&aabb.halfExtents);

    for (int i = 0; i < 6; ++i) {
        XMVECTOR plane = planes[i];
        float d = XMVectorGetX(XMPlaneDotCoord(plane, center));
        XMVECTOR normal = XMVectorSet(XMVectorGetX(plane), XMVectorGetY(plane), XMVectorGetZ(plane), 0.0f);
        XMVECTOR absN = XMVectorAbs(normal);
        float r = XMVectorGetX(absN) * aabb.halfExtents.x +
            XMVectorGetY(absN) * aabb.halfExtents.y +
            XMVectorGetZ(absN) * aabb.halfExtents.z;
        if (d + r < 0.0f)
            return false;
    }
    return true;
}

Octree::Node::Node(const AABB& bounds)
    : bounds(bounds), isLeaf(true) {
    for (auto& child : children) child = nullptr;
}

Octree::Octree(const AABB& bounds, int maxDepth, int maxObjectsPerNode)
    : m_maxDepth(maxDepth), m_maxObjectsPerNode(maxObjectsPerNode) {
    m_root = std::make_unique<Node>(bounds);
}

Octree::~Octree() = default;

void Octree::Build(const std::vector<AABB>& objectBounds, const std::vector<int>& objectIndices) {
    if (objectBounds.empty()) return;
    std::vector<int> indices = objectIndices;
    if (indices.empty()) {
        indices.resize(objectBounds.size());
        for (size_t i = 0; i < objectBounds.size(); ++i) indices[i] = static_cast<int>(i);
    }
    Subdivide(m_root.get(), 0, objectBounds, indices);
}

void Octree::Subdivide(Node* node, int depth,
    const std::vector<AABB>& objectBounds,
    const std::vector<int>& objectIndices) {
    if (static_cast<int>(objectIndices.size()) <= m_maxObjectsPerNode || depth >= m_maxDepth) {
        node->objects = objectIndices;
        node->isLeaf = true;
        return;
    }

    node->isLeaf = false;
    XMVECTOR center = XMLoadFloat3(&node->bounds.center);
    XMVECTOR half = XMLoadFloat3(&node->bounds.halfExtents);
    float newHalf = XMVectorGetX(half) * 0.5f;
    XMFLOAT3 newHalfExtents = { newHalf, newHalf, newHalf };

    XMVECTOR offsets[8] = {
        XMVectorSet(-newHalf, -newHalf, -newHalf, 0),
        XMVectorSet(newHalf, -newHalf, -newHalf, 0),
        XMVectorSet(-newHalf,  newHalf, -newHalf, 0),
        XMVectorSet(newHalf,  newHalf, -newHalf, 0),
        XMVectorSet(-newHalf, -newHalf,  newHalf, 0),
        XMVectorSet(newHalf, -newHalf,  newHalf, 0),
        XMVectorSet(-newHalf,  newHalf,  newHalf, 0),
        XMVectorSet(newHalf,  newHalf,  newHalf, 0)
    };

    for (int i = 0; i < 8; ++i) {
        XMVECTOR childCenter = center + offsets[i];
        AABB childBounds;
        XMStoreFloat3(&childBounds.center, childCenter);
        childBounds.halfExtents = newHalfExtents;
        node->children[i] = std::make_unique<Node>(childBounds);
    }

    std::vector<int> childObjects[8];
    for (int idx : objectIndices) {
        const AABB& objBounds = objectBounds[idx];
        XMVECTOR objCenter = XMLoadFloat3(&objBounds.center);
        for (int i = 0; i < 8; ++i) {
            XMVECTOR childC = XMLoadFloat3(&node->children[i]->bounds.center);
            XMVECTOR childH = XMLoadFloat3(&node->children[i]->bounds.halfExtents);
            XMVECTOR minC = childC - childH;
            XMVECTOR maxC = childC + childH;
            if (XMVectorGetX(objCenter) >= XMVectorGetX(minC) && XMVectorGetX(objCenter) <= XMVectorGetX(maxC) &&
                XMVectorGetY(objCenter) >= XMVectorGetY(minC) && XMVectorGetY(objCenter) <= XMVectorGetY(maxC) &&
                XMVectorGetZ(objCenter) >= XMVectorGetZ(minC) && XMVectorGetZ(objCenter) <= XMVectorGetZ(maxC)) {
                childObjects[i].push_back(idx);
                break;
            }
        }
    }

    for (int i = 0; i < 8; ++i) {
        if (!childObjects[i].empty()) {
            Subdivide(node->children[i].get(), depth + 1, objectBounds, childObjects[i]);
        }
    }
}

void Octree::QueryFrustum(const XMVECTOR planes[6], std::vector<int>& outIndices) const {
    outIndices.clear();
    QueryNode(m_root.get(), planes, outIndices);
}

void Octree::QueryNode(const Node* node, const XMVECTOR planes[6], std::vector<int>& outIndices) const {
    if (!node) return;
    if (!IntersectAABB(node->bounds, planes)) return;

    if (node->isLeaf) {
        outIndices.insert(outIndices.end(), node->objects.begin(), node->objects.end());
    }
    else {
        for (int i = 0; i < 8; ++i) {
            if (node->children[i]) QueryNode(node->children[i].get(), planes, outIndices);
        }
    }
}

bool Octree::IntersectAABB(const AABB& aabb, const XMVECTOR planes[6]) {
    return ::IntersectAABB(aabb, planes);
}

void Octree::GetAllLeafNodes(std::vector<AABB>& outBounds) const
{
    outBounds.clear();
    std::function<void(const Node*)> traverse = [&](const Node* node) {
        if (!node) return;
        if (node->isLeaf) {
            outBounds.push_back(node->bounds);
        }
        else {
            for (int i = 0; i < 8; ++i)
                traverse(node->children[i].get());
        }
    };
    traverse(m_root.get());
}