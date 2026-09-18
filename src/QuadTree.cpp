#include "QuadTree.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

void QuadTree::Init(const QuadTreeParams& params)
{
    m_params = params;

    m_root = std::make_unique<TerrainNode>();
    m_root->level = 0;
    m_root->center = { params.worldCenterX, 0.5f * (params.minHeight + params.maxHeight), params.worldCenterZ };
    m_root->halfExtents = {
        0.5f * params.worldSizeXZ,
        0.5f * (params.maxHeight - params.minHeight),
        0.5f * params.worldSizeXZ
    };
    m_root->uvMin = { 0.f, 0.f };
    m_root->uvMax = { 1.f, 1.f };
    m_root->parent = nullptr;

    m_currentMaxDepthReached = 0;
}

void QuadTree::Update(const XMFLOAT3& cameraPos,
    const XMVECTOR frustumPlanes[6],
    float screenHeight,
    float fovY)
{
    if (!m_root) return;

    m_currentMaxDepthReached = 0;

    UpdateNode(m_root.get(), cameraPos, frustumPlanes, screenHeight, fovY);
}

void QuadTree::UpdateNode(TerrainNode* node,
    const XMFLOAT3& camPos,
    const XMVECTOR frustumPlanes[6],
    float screenHeight,
    float fovY)
{
    if (!FrustumCull(node->center, node->halfExtents, frustumPlanes)) {
        node->visible = false;
        node->useAsLeaf = false;
        return;
    }
    node->visible = true;

    node->sse = ComputeScreenSpaceError(node, camPos, screenHeight, fovY);

    const bool canSubdivide = (node->level < m_params.maxLevel);
    const bool shouldSubdivide = canSubdivide && (node->sse > m_params.lodErrorThresholdPx);

    if (shouldSubdivide) {
        if (!node->children[0]) {
            Subdivide(node);
        }

        node->useAsLeaf = false;

        for (int i = 0; i < 4; ++i) {
            if (node->children[i]) {
                UpdateNode(node->children[i].get(), camPos, frustumPlanes, screenHeight, fovY);
            }
        }

        m_currentMaxDepthReached = std::max(m_currentMaxDepthReached, node->level + 1);
    }
    else {
        node->useAsLeaf = true;
    }
}

void QuadTree::Subdivide(TerrainNode* node)
{
    const float childHalfXZ = node->halfExtents.x * 0.5f;
    const float childHalfY = node->halfExtents.y;
    const float cx = node->center.x;
    const float cy = node->center.y;
    const float cz = node->center.z;

    const float uvMidX = 0.5f * (node->uvMin.x + node->uvMax.x);
    const float uvMidY = 0.5f * (node->uvMin.y + node->uvMax.y);

    struct ChildSpec {
        float x, z;
        float u0, v0, u1, v1;
    };

    const ChildSpec specs[4] = {
        { cx - childHalfXZ, cz - childHalfXZ, node->uvMin.x, node->uvMin.y, uvMidX,       uvMidY       },
        { cx + childHalfXZ, cz - childHalfXZ, uvMidX,       node->uvMin.y, node->uvMax.x, uvMidY       },
        { cx - childHalfXZ, cz + childHalfXZ, node->uvMin.x, uvMidY,       uvMidX,       node->uvMax.y },
        { cx + childHalfXZ, cz + childHalfXZ, uvMidX,       uvMidY,       node->uvMax.x, node->uvMax.y },
    };

    for (int i = 0; i < 4; ++i) {
        auto child = std::make_unique<TerrainNode>();
        child->level = node->level + 1;
        child->center = { specs[i].x, cy, specs[i].z };
        child->halfExtents = { childHalfXZ, childHalfY, childHalfXZ };
        child->uvMin = { specs[i].u0, specs[i].v0 };
        child->uvMax = { specs[i].u1, specs[i].v1 };
        child->parent = node;
        child->visible = false;
        child->useAsLeaf = true;
        child->sse = 0.f;

        node->children[i] = std::move(child);
    }
}

bool QuadTree::FrustumCull(const XMFLOAT3& center,
    const XMFLOAT3& halfExtents,
    const XMVECTOR    frustumPlanes[6])
{
    const XMVECTOR c = XMLoadFloat3(&center);

    for (int i = 0; i < 6; ++i) {
        const XMVECTOR plane = frustumPlanes[i];

        const float dist = XMVectorGetX(XMPlaneDotCoord(plane, c));

        const float nx = fabsf(XMVectorGetX(plane));
        const float ny = fabsf(XMVectorGetY(plane));
        const float nz = fabsf(XMVectorGetZ(plane));
        const float r = nx * halfExtents.x + ny * halfExtents.y + nz * halfExtents.z;

        if (dist + r < 0.0f) {
            return false;
        }
    }
    return true;
}

float QuadTree::ComputeScreenSpaceError(const TerrainNode* node,
    const XMFLOAT3& camPos,
    float screenHeight,
    float fovY) const
{
    const XMVECTOR c = XMLoadFloat3(&node->center);
    const XMVECTOR cam = XMLoadFloat3(&camPos);

    float dist = XMVectorGetX(XMVector3Length(c - cam));
    dist = (std::max)(dist, 0.0001f);

    const float nodeSizeXZ = 2.0f * node->halfExtents.x;

    const float tanHalf = tanf(fovY * 0.5f);
    const float projectedPx = (nodeSizeXZ / (2.0f * dist * tanHalf)) * screenHeight;

    return projectedPx;
}

void QuadTree::GetVisibleLeaves(std::vector<TerrainNode*>& outLeaves) const
{
    outLeaves.clear();
    if (!m_root) return;

    std::vector<const TerrainNode*> stack;
    stack.push_back(m_root.get());

    while (!stack.empty()) {
        const TerrainNode* n = stack.back();
        stack.pop_back();

        if (!n->visible) continue;

        if (n->useAsLeaf) {
            outLeaves.push_back(const_cast<TerrainNode*>(n));
        }
        else {
            for (int i = 0; i < 4; ++i) {
                if (n->children[i]) {
                    stack.push_back(n->children[i].get());
                }
            }
        }
    }
}

void QuadTree::GetAllLeaves(std::vector<TerrainNode*>& outLeaves) const
{
    outLeaves.clear();
    if (!m_root) return;

    std::vector<TerrainNode*> stack;
    stack.push_back(m_root.get());

    while (!stack.empty()) {
        TerrainNode* n = stack.back();
        stack.pop_back();

        if (!n->children[0]) {
            outLeaves.push_back(n);
        }
        else {
            for (int i = 0; i < 4; ++i) {
                if (n->children[i]) stack.push_back(n->children[i].get());
            }
        }
    }
}