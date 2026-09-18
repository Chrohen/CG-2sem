#include "Terrain.h"
#include <DirectXTex.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

static bool LoadTexture2DSRV(ID3D12Device* device,
    ID3D12CommandQueue* queue,
    const std::wstring& path,
    DXGI_FORMAT resourceFormat,
    DXGI_FORMAT srvFormat,
    Microsoft::WRL::ComPtr<ID3D12Resource>& outTex,
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpu)
{
    using namespace DirectX;
    using Microsoft::WRL::ComPtr;

    ScratchImage img;
    HRESULT hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, nullptr, img);
    if (FAILED(hr)) {
        OutputDebugStringW((L"[Terrain] Load failed: " + path + L"\n").c_str());
        return false;
    }

    ScratchImage converted;
    if (img.GetMetadata().format != resourceFormat) {
        hr = Convert(img.GetImages(), img.GetImageCount(), img.GetMetadata(),
            resourceFormat, TEX_FILTER_DEFAULT, 0.0f, converted);
        if (FAILED(hr)) {
            OutputDebugStringW(L"[Terrain] Convert failed\n");
            return false;
        }
        img = std::move(converted);
    }

    const Image* image = img.GetImage(0, 0, 0);
    if (!image) return false;

    const UINT width = static_cast<UINT>(image->width);
    const UINT height = static_cast<UINT>(image->height);

    // --- GPU resource ---
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = resourceFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    ComPtr<ID3D12Resource> tex;
    hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex));
    if (FAILED(hr)) return false;

    // --- upload buffer ---
    const UINT bytesPerPixel = 4;   // R8G8B8A8
    const UINT srcRowPitch = width * bytesPerPixel;
    const UINT alignedPitch = (srcRowPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const UINT64 uploadSize = static_cast<UINT64>(alignedPitch) * height;

    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufDesc = {};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufDesc.Width = uploadSize;
    bufDesc.Height = 1;
    bufDesc.DepthOrArraySize = 1;
    bufDesc.MipLevels = 1;
    bufDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufDesc.SampleDesc.Count = 1;
    bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ComPtr<ID3D12Resource> upload;
    hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&upload));
    if (FAILED(hr)) return false;

    uint8_t* mapped = nullptr;
    hr = upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) return false;

    const uint8_t* srcRow = image->pixels;
    for (UINT y = 0; y < height; ++y) {
        memcpy(mapped + static_cast<UINT64>(y) * alignedPitch, srcRow, srcRowPitch);
        srcRow += image->rowPitch;
    }
    upload->Unmap(0, nullptr);

    // --- temp command list + fence ---
    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc));
    if (FAILED(hr)) return false;
    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list));
    if (FAILED(hr)) return false;

    D3D12_TEXTURE_COPY_LOCATION dst = {};
    dst.pResource = tex.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src = {};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint.Offset = 0;
    src.PlacedFootprint.Footprint.Format = resourceFormat;
    src.PlacedFootprint.Footprint.Width = width;
    src.PlacedFootprint.Footprint.Height = height;
    src.PlacedFootprint.Footprint.Depth = 1;
    src.PlacedFootprint.Footprint.RowPitch = alignedPitch;

    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = tex.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);

    list->Close();
    ID3D12CommandList* lists[] = { list.Get() };
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
    if (FAILED(hr)) return false;

    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!evt) return false;
    queue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, evt);
        WaitForSingleObject(evt, INFINITE);
    }
    CloseHandle(evt);

    // --- SRV ---
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = srvFormat;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.f;
    device->CreateShaderResourceView(tex.Get(), &srvDesc, srvCpu);

    outTex = tex;
    OutputDebugStringW((L"[Terrain] Loaded: " + path + L"\n").c_str());
    return true;
}

bool Terrain::Init(ID3D12Device* device,
    ID3D12CommandQueue* queue,
    const std::wstring& heightmapPath,
    float worldSizeXZ,
    float heightScale,
    float worldCenterX,
    float worldCenterZ,
    float worldOffsetY,
    int   maxLevel)
{
    m_worldSizeXZ = worldSizeXZ;
    m_heightScale = heightScale;
    m_worldCenterX = worldCenterX;
    m_worldCenterZ = worldCenterZ;
    m_worldOffsetY = worldOffsetY;

    // --- SRV heap: 3 слота (heightmap, diffuse, normal) ---
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 3;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_srvHeap));
    if (FAILED(hr)) return false;

    UINT descSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE srvStart = m_srvHeap->GetCPUDescriptorHandleForHeapStart();

    D3D12_CPU_DESCRIPTOR_HANDLE hHeightmap = srvStart;
    D3D12_CPU_DESCRIPTOR_HANDLE hDiffuse = srvStart; hDiffuse.ptr += descSize;
    D3D12_CPU_DESCRIPTOR_HANDLE hNormal = srvStart; hNormal.ptr += 2 * descSize;

    // slot 0 — heightmap
    if (!m_heightmap.Init(device, queue, heightmapPath, hHeightmap)) {
        OutputDebugStringW(L"[Terrain] HeightMap init failed\n");
        return false;
    }

    // slot 1 — diffuse (SRGB-семпл → linear в шейдере)
    if (!LoadTexture2DSRV(device, queue,
        L"assets\\terrain\\diffuse.png",
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        m_diffuseTex, hDiffuse)) {
        OutputDebugStringA("[Terrain] diffuse.png not loaded, using white fallback\n");
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullSrv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &nullSrv, hDiffuse);
    }

    // slot 2 — normal (linear)
    if (!LoadTexture2DSRV(device, queue,
        L"assets\\terrain\\normal.png",
        DXGI_FORMAT_R8G8B8A8_UNORM,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        m_normalTex, hNormal)) {
        OutputDebugStringA("[Terrain] normal.png not loaded, using flat-normal fallback\n");
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv = {};
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullSrv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullSrv.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &nullSrv, hNormal);
    }

    // --- QuadTree ---
    QuadTreeParams qtParams = {};
    qtParams.worldCenterX = worldCenterX;
    qtParams.worldCenterZ = worldCenterZ;
    qtParams.worldSizeXZ = worldSizeXZ;
    qtParams.minHeight = worldOffsetY;
    qtParams.maxHeight = worldOffsetY + heightScale;
    qtParams.maxLevel = maxLevel;
    qtParams.lodErrorThresholdPx = 128.f;

    m_quadtree.Init(qtParams);

    // --- Mesh + CBs ---
    BuildMesh(device, queue);
    BuildLeafCBs(device);

    OutputDebugStringW(L"[Terrain] Initialized\n");
    return true;
}

void Terrain::Update(const XMFLOAT3& camPos,
    const XMFLOAT3& camTarget,
    const XMFLOAT3& camUp,
    float fovY,
    float aspect,
    float nearZ,
    float farZ,
    UINT  screenW,
    UINT  screenH)
{
    XMVECTOR planes[6];
    ComputeFrustumPlanes(planes, camPos, camTarget, camUp, fovY, aspect, nearZ, farZ);

    m_quadtree.Update(camPos, planes, static_cast<float>(screenH), fovY);
    m_quadtree.GetVisibleLeaves(m_visibleLeaves);
}

void Terrain::BuildMesh(ID3D12Device* device, ID3D12CommandQueue* queue)
{
    using namespace DirectX;

    struct GridVertex { XMFLOAT2 UV; };

    std::vector<GridVertex> verts;
    verts.reserve(kGridVerts);

    const UINT N = kGridSegments;
    for (UINT y = 0; y <= N; ++y) {
        for (UINT x = 0; x <= N; ++x) {
            verts.push_back({ { static_cast<float>(x) / N, static_cast<float>(y) / N } });
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(kGridIndices);

    for (UINT y = 0; y < N; ++y) {
        for (UINT x = 0; x < N; ++x) {
            uint32_t i0 = y * (N + 1) + x;
            uint32_t i1 = i0 + 1;
            uint32_t i2 = i0 + (N + 1);
            uint32_t i3 = i2 + 1;

            indices.push_back(i0); indices.push_back(i2); indices.push_back(i1);
            indices.push_back(i2); indices.push_back(i3); indices.push_back(i1);
        }
    }
    m_indexCount = static_cast<UINT>(indices.size());

    const UINT vbBytes = static_cast<UINT>(verts.size()) * sizeof(GridVertex);
    const UINT ibBytes = static_cast<UINT>(indices.size()) * sizeof(uint32_t);

    auto makeBufDesc = [](UINT64 sz) {
        D3D12_RESOURCE_DESC d = {};
        d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = sz;
        d.Height = 1;
        d.DepthOrArraySize = 1;
        d.MipLevels = 1;
        d.Format = DXGI_FORMAT_UNKNOWN;
        d.SampleDesc.Count = 1;
        d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        return d;
        };

    D3D12_HEAP_PROPERTIES defHeap = {}; defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_HEAP_PROPERTIES upHeap = {}; upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    auto vbDesc = makeBufDesc(vbBytes);
    auto ibDesc = makeBufDesc(ibBytes);

    ComPtr<ID3D12Resource> vbUp, ibUp;
    ThrowIfFailed(device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_vb)));
    ThrowIfFailed(device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&m_ib)));
    ThrowIfFailed(device->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE,
        &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&vbUp)));
    ThrowIfFailed(device->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE,
        &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&ibUp)));

    void* mp = nullptr;
    ThrowIfFailed(vbUp->Map(0, nullptr, &mp)); memcpy(mp, verts.data(), vbBytes); vbUp->Unmap(0, nullptr);
    ThrowIfFailed(ibUp->Map(0, nullptr, &mp)); memcpy(mp, indices.data(), ibBytes); ibUp->Unmap(0, nullptr);

    ComPtr<ID3D12CommandAllocator>    alloc;
    ComPtr<ID3D12GraphicsCommandList> list;
    ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&alloc)));
    ThrowIfFailed(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(&list)));

    list->CopyBufferRegion(m_vb.Get(), 0, vbUp.Get(), 0, vbBytes);
    list->CopyBufferRegion(m_ib.Get(), 0, ibUp.Get(), 0, ibBytes);

    D3D12_RESOURCE_BARRIER barriers[2] = {};
    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition = { m_vb.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER };
    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition = { m_ib.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
        D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER };
    list->ResourceBarrier(2, barriers);

    list->Close();
    ID3D12CommandList* lists[] = { list.Get() };
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    HANDLE evt = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    queue->Signal(fence.Get(), 1);
    if (fence->GetCompletedValue() < 1) {
        fence->SetEventOnCompletion(1, evt);
        WaitForSingleObject(evt, INFINITE);
    }
    CloseHandle(evt);

    m_vbView.BufferLocation = m_vb->GetGPUVirtualAddress();
    m_vbView.StrideInBytes = sizeof(GridVertex);
    m_vbView.SizeInBytes = vbBytes;

    m_ibView.BufferLocation = m_ib->GetGPUVirtualAddress();
    m_ibView.Format = DXGI_FORMAT_R32_UINT;
    m_ibView.SizeInBytes = ibBytes;
}

void Terrain::BuildLeafCBs(ID3D12Device* device)
{
    m_leafCBs = std::make_unique<UploadBuffer<TerrainConstants>>(device, kMaxLeaves, true);
}

void Terrain::ComputeFrustumPlanes(XMVECTOR planes[6],
    const XMFLOAT3& camPos,
    const XMFLOAT3& camTarget,
    const XMFLOAT3& camUp,
    float fovY, float aspect,
    float nearZ, float farZ)
{
    const float tanHalfY = tanf(fovY * 0.5f);
    const float tanHalfX = tanHalfY * aspect;

    XMFLOAT3 nearC[4] = {
        { -tanHalfX * nearZ,  tanHalfY * nearZ, nearZ },
        {  tanHalfX * nearZ,  tanHalfY * nearZ, nearZ },
        {  tanHalfX * nearZ, -tanHalfY * nearZ, nearZ },
        { -tanHalfX * nearZ, -tanHalfY * nearZ, nearZ }
    };
    XMFLOAT3 farC[4] = {
        { -tanHalfX * farZ,  tanHalfY * farZ, farZ },
        {  tanHalfX * farZ,  tanHalfY * farZ, farZ },
        {  tanHalfX * farZ, -tanHalfY * farZ, farZ },
        { -tanHalfX * farZ, -tanHalfY * farZ, farZ }
    };

    XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&camPos),
        XMLoadFloat3(&camTarget),
        XMLoadFloat3(&camUp));
    XMMATRIX invView = XMMatrixInverse(nullptr, view);

    XMFLOAT3 wc[8];
    for (int i = 0; i < 4; ++i) {
        XMStoreFloat3(&wc[i], XMVector3TransformCoord(XMLoadFloat3(&nearC[i]), invView));
        XMStoreFloat3(&wc[i + 4], XMVector3TransformCoord(XMLoadFloat3(&farC[i]), invView));
    }

    planes[0] = XMPlaneFromPoints(XMLoadFloat3(&wc[0]), XMLoadFloat3(&wc[3]), XMLoadFloat3(&wc[4]));
    planes[1] = XMPlaneFromPoints(XMLoadFloat3(&wc[1]), XMLoadFloat3(&wc[2]), XMLoadFloat3(&wc[5]));
    planes[2] = XMPlaneFromPoints(XMLoadFloat3(&wc[3]), XMLoadFloat3(&wc[2]), XMLoadFloat3(&wc[7]));
    planes[3] = XMPlaneFromPoints(XMLoadFloat3(&wc[0]), XMLoadFloat3(&wc[1]), XMLoadFloat3(&wc[4]));
    planes[4] = XMPlaneFromPoints(XMLoadFloat3(&wc[0]), XMLoadFloat3(&wc[1]), XMLoadFloat3(&wc[2]));
    planes[5] = XMPlaneFromPoints(XMLoadFloat3(&wc[4]), XMLoadFloat3(&wc[5]), XMLoadFloat3(&wc[7]));

    const XMVECTOR eye = XMLoadFloat3(&camPos);
    const XMVECTOR lookDir = XMVector3Normalize(XMLoadFloat3(&camTarget) - eye);
    const XMVECTOR insidePt = eye + lookDir * 5.0f;

    for (int i = 0; i < 6; ++i) {
        planes[i] = XMPlaneNormalize(planes[i]);
        const float d = XMVectorGetX(XMPlaneDotCoord(planes[i], insidePt));
        if (d < 0.f) planes[i] = XMVectorNegate(planes[i]);
    }
}

void Terrain::Draw(ID3D12GraphicsCommandList* cmdList,
    ID3D12PipelineState* pso,
    ID3D12RootSignature* rootSig,
    D3D12_GPU_VIRTUAL_ADDRESS passCB)
{
    if (!m_leafCBs || m_visibleLeaves.empty() || !m_vb || !m_ib) return;
    if (!m_srvHeap) return;

    const float invH = (m_heightmap.Width() > 0)
        ? 1.0f / static_cast<float>(m_heightmap.Width())
        : 1.0f;

    cmdList->SetPipelineState(pso);
    cmdList->SetGraphicsRootSignature(rootSig);

    ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);
    cmdList->SetGraphicsRootDescriptorTable(
        2, m_srvHeap->GetGPUDescriptorHandleForHeapStart());

    cmdList->SetGraphicsRootConstantBufferView(1, passCB);

    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->IASetVertexBuffers(0, 1, &m_vbView);
    cmdList->IASetIndexBuffer(&m_ibView);

    const UINT cbStride = CalcConstantBufferByteSize(sizeof(TerrainConstants));
    const D3D12_GPU_VIRTUAL_ADDRESS cbBase =
        m_leafCBs->Resource()->GetGPUVirtualAddress();

    UINT count = static_cast<UINT>(m_visibleLeaves.size());
    if (count > kMaxLeaves) {
        static bool warned = false;
        if (!warned) {
            OutputDebugStringA("[Terrain] Leaf count exceeds CB pool, clamping\n");
            warned = true;
        }
        count = kMaxLeaves;
    }

    for (UINT i = 0; i < count; ++i) {
        const TerrainNode* n = m_visibleLeaves[i];

        TerrainConstants c = {};
        c.UVMin = n->uvMin;
        c.UVMax = n->uvMax;
        c.WorldCenterXZ = { n->center.x, n->center.z };
        c.WorldSizeXZ = 2.0f * n->halfExtents.x;
        c.HeightScale = m_heightScale;
        c.InvHeightmapSize = invH;
        c.Level = n->level;
        c.WorldOffsetY = m_worldOffsetY;
        c.Metalness = m_materialMetalness;
        c.Roughness = m_materialRoughness;

        m_leafCBs->CopyData(i, c);

        D3D12_GPU_VIRTUAL_ADDRESS cbAddr = cbBase + static_cast<UINT64>(i) * cbStride;
        cmdList->SetGraphicsRootConstantBufferView(0, cbAddr);
        cmdList->DrawIndexedInstanced(m_indexCount, 1, 0, 0, 0);
    }
}