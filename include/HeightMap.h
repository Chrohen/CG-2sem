#ifndef HEIGHTMAP_H
#define HEIGHTMAP_H

#include <string>
#include <vector>
#include <cstdint>
#include <wrl.h>
#include <d3d12.h>

class HeightMap
{
public:
    bool Init(ID3D12Device* device,
        ID3D12CommandQueue* queue,
        const std::wstring& path,
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpuHandle);

    float MinHeight01() const { return m_min01; }
    float MaxHeight01() const { return m_max01; }

    UINT Width()  const { return m_width; }
    UINT Height() const { return m_height; }

    float SampleCPU(float u, float v) const;

    ID3D12Resource* Texture() const { return m_texture.Get(); }

private:
    UINT m_width = 0;
    UINT m_height = 0;

    float m_min01 = 0.f;
    float m_max01 = 1.f;

    std::vector<uint16_t> m_data;

    Microsoft::WRL::ComPtr<ID3D12Resource> m_texture;
    Microsoft::WRL::ComPtr<ID3D12Resource> m_upload;
};

#endif // HEIGHTMAP_H