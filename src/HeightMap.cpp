#include "HeightMap.h"
#include "Dx12Common.hpp"

#include <DirectXTex.h>
#include <algorithm>
#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

bool HeightMap::Init(ID3D12Device* device,
    ID3D12CommandQueue* queue,
    const std::wstring& path,
    D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle)
{
    ScratchImage img;
    HRESULT hr = LoadFromWICFile(path.c_str(), WIC_FLAGS_NONE, nullptr, img);
    if (FAILED(hr)) {
        OutputDebugStringW((L"[HeightMap] LoadFromWICFile failed: " + path + L"\n").c_str());
        return false;
    }

    ScratchImage converted;
    if (img.GetMetadata().format != DXGI_FORMAT_R16_UNORM) {
        hr = Convert(img.GetImages(), img.GetImageCount(), img.GetMetadata(),
            DXGI_FORMAT_R16_UNORM, TEX_FILTER_DEFAULT, 0.0f, converted);
        if (FAILED(hr)) {
            OutputDebugStringW(L"[HeightMap] Convert to R16_UNORM failed\n");
            return false;
        }
        img = std::move(converted);
    }

    const Image* image = img.GetImage(0, 0, 0);
    if (!image) return false;

    m_width = static_cast<UINT>(image->width);
    m_height = static_cast<UINT>(image->height);

    m_data.resize(static_cast<size_t>(m_width) * m_height);

    const uint8_t* srcRow = image->pixels;
    uint16_t mn = 0xFFFF, mx = 0;
    for (UINT y = 0; y < m_height; ++y) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(srcRow);
        uint16_t* dst = m_data.data() + static_cast<size_t>(y) * m_width;
        for (UINT x = 0; x < m_width; ++x) {
            dst[x] = src[x];
            mn = (std::min)(mn, src[x]);
            mx = (std::max)(mx, src[x]);
        }
        srcRow += image->rowPitch;
    }

    m_min01 = mn / 65535.0f;
    m_max01 = mx / 65535.0f;

    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = m_width;
    texDesc.Height = m_height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R16_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

    hr = device->CreateCommittedResource(
        &defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
        IID_PPV_ARGS(&m_texture));
    if (FAILED(hr)) return false;

    const UINT srcRowPitch = m_width * static_cast<UINT>(sizeof(uint16_t));
    const UINT alignedRowPitch =
        (srcRowPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
        & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const UINT64 uploadSize = static_cast<UINT64>(alignedRowPitch) * m_height;

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

    hr = device->CreateCommittedResource(
        &uploadHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_upload));
    if (FAILED(hr)) return false;

    uint8_t* mapped = nullptr;
    hr = m_upload->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
    if (FAILED(hr)) return false;

    for (UINT y = 0; y < m_height; ++y) {
        memcpy(mapped + static_cast<UINT64>(y) * alignedRowPitch,
            m_data.data() + static_cast<size_t>(y) * m_width,
            srcRowPitch);
    }
    m_upload->Unmap(0, nullptr);

    ComPtr<ID3D12CommandAllocator>    tmpAlloc;
    ComPtr<ID3D12GraphicsCommandList> tmpList;

    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&tmpAlloc));
    if (FAILED(hr)) return false;

    hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        tmpAlloc.Get(), nullptr,
        IID_PPV_ARGS(&tmpList));
    if (FAILED(hr)) return false;

    D3D12_TEXTURE_COPY_LOCATION dstLoc = {};
    dstLoc.pResource = m_texture.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc = {};
    srcLoc.pResource = m_upload.Get();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint.Offset = 0;
    srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R16_UNORM;
    srcLoc.PlacedFootprint.Footprint.Width = m_width;
    srcLoc.PlacedFootprint.Footprint.Height = m_height;
    srcLoc.PlacedFootprint.Footprint.Depth = 1;
    srcLoc.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

    tmpList->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_texture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    tmpList->ResourceBarrier(1, &barrier);

    tmpList->Close();

    ID3D12CommandList* lists[] = { tmpList.Get() };
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

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Format = DXGI_FORMAT_R16_UNORM;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.f;

    device->CreateShaderResourceView(m_texture.Get(), &srvDesc, srvCpuHandle);

    OutputDebugStringW((L"[HeightMap] Loaded: " + path +
        L"  " + std::to_wstring(m_width) + L"x" + std::to_wstring(m_height) + L"\n").c_str());
    return true;
}

float HeightMap::SampleCPU(float u, float v) const
{
    if (m_data.empty()) return 0.f;

    u = std::clamp(u, 0.f, 1.f);
    v = std::clamp(v, 0.f, 1.f);

    const int x = static_cast<int>(u * (m_width - 1) + 0.5f);
    const int y = static_cast<int>(v * (m_height - 1) + 0.5f);
    const uint16_t raw = m_data[static_cast<size_t>(y) * m_width + x];
    return raw / 65535.0f;
}