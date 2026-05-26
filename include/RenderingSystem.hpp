#ifndef RENDERING_SYSTEM_HPP
#define RENDERING_SYSTEM_HPP

#include "Dx12Common.hpp"

class RenderingSystem
{
public:
	void BuildShaders();
	void BuildRootSignatures(ID3D12Device* device);
	void BuildPSOs(ID3D12Device* device,
		DXGI_FORMAT backBufferFormat,
		DXGI_FORMAT depthStencilFormat);

	ID3D12RootSignature* GeometryRootSignature() const { return m_geometryRootSignature.Get(); }
	ID3D12PipelineState* GeometryPSO() const { return m_geometryPso.Get(); }

	ID3D12RootSignature* LightingRootSignature() const { return m_lightingRootSignature.Get(); }
	ID3D12PipelineState* LightingPSO() const { return m_lightingPso.Get(); }

	ID3D12RootSignature* WireframeRootSignature() const { return m_wireframeRootSignature.Get(); }
	ID3D12PipelineState* WireframePSO() const { return m_wireframePso.Get(); }

	ID3D12RootSignature* TessellationRootSignature() const { return m_tessellationRootSignature.Get(); }
	ID3D12PipelineState* TessellationPSO() const { return m_tesselationPso.Get(); }

	ID3D12RootSignature* WaterRootSignature() const { return m_waterRootSignature.Get(); }
	ID3D12PipelineState* WaterPSO() const { return m_waterPso.Get(); }
private:
	ComPtr<ID3DBlob> m_geometryVsByteCode;
	ComPtr<ID3DBlob> m_geometryPsByteCode;
	ComPtr<ID3DBlob> m_lightingVsByteCode;
	ComPtr<ID3DBlob> m_lightingPsByteCode;

	ComPtr<ID3D12RootSignature> m_geometryRootSignature;
	ComPtr<ID3D12RootSignature> m_lightingRootSignature;

	ComPtr<ID3D12PipelineState> m_geometryPso;
	ComPtr<ID3D12PipelineState> m_lightingPso;

	ComPtr<ID3DBlob> m_wireframeVsByteCode;
	ComPtr<ID3DBlob> m_wireframePsByteCode;
	ComPtr<ID3D12RootSignature> m_wireframeRootSignature;
	ComPtr<ID3D12PipelineState> m_wireframePso;

	ComPtr<ID3D12RootSignature> m_tessellationRootSignature;
	ComPtr<ID3D12PipelineState> m_tesselationPso;

	ComPtr<ID3DBlob> m_tessellationVsByteCode;
	ComPtr<ID3DBlob> m_tessellationHsByteCode;
	ComPtr<ID3DBlob> m_tessellationDsByteCode;
	ComPtr<ID3DBlob> m_tessellationPsByteCode;

	ComPtr<ID3D12RootSignature> m_waterRootSignature;
	ComPtr<ID3D12PipelineState> m_waterPso;

	ComPtr<ID3DBlob> m_waterVsByteCode;
	ComPtr<ID3DBlob> m_waterHsByteCode;
	ComPtr<ID3DBlob> m_waterDsByteCode;
	ComPtr<ID3DBlob> m_waterPsByteCode;
};

#endif // RENDERING_SYSTEM_HPP