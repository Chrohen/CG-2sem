#include "RenderingSystem.hpp"
#include "Gbuffer.hpp"
#include "RenderStructs.hpp"

namespace {

	ComPtr<ID3D12RootSignature> CreateRootSignature(
		ID3D12Device* device,
		const D3D12_ROOT_SIGNATURE_DESC& desc)
	{
		ComPtr<ID3DBlob> serialized;
		ComPtr<ID3DBlob> errors;

		HRESULT hr = D3D12SerializeRootSignature(
			&desc, D3D_ROOT_SIGNATURE_VERSION_1,
			serialized.GetAddressOf(), errors.GetAddressOf());

		if (errors)
			OutputDebugStringA(reinterpret_cast<const char*>(errors->GetBufferPointer()));
		ThrowIfFailed(hr);

		ComPtr<ID3D12RootSignature> rootSig;
		ThrowIfFailed(device->CreateRootSignature(
			0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
			IID_PPV_ARGS(&rootSig)));
		return rootSig;
	}

	D3D12_RASTERIZER_DESC DefaultRasterizer(D3D12_CULL_MODE cullMode)
	{
		D3D12_RASTERIZER_DESC d = {};
		d.FillMode = D3D12_FILL_MODE_SOLID;
		d.CullMode = cullMode;
		d.FrontCounterClockwise = FALSE;
		d.DepthBias = D3D12_DEFAULT_DEPTH_BIAS;
		d.DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP;
		d.SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS;
		d.DepthClipEnable = TRUE;
		d.MultisampleEnable = FALSE;
		d.AntialiasedLineEnable = FALSE;
		d.ForcedSampleCount = 0;
		d.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
		return d;
	}

}

void RenderingSystem::BuildShaders()
{
	m_geometryVsByteCode = CompileShader(L"shader\\DeferredGeometry.hlsl", nullptr, "VS_def", "vs_5_1");
	m_geometryPsByteCode = CompileShader(L"shader\\DeferredGeometry.hlsl", nullptr, "PS", "ps_5_1");

	m_lightingVsByteCode = CompileShader(L"shader\\DeferredLighting.hlsl", nullptr, "VSFullscreen", "vs_5_1");
	m_lightingPsByteCode = CompileShader(L"shader\\DeferredLighting.hlsl", nullptr, "PSLighting", "ps_5_1");

	m_wireframeVsByteCode = CompileShader(L"shader\\Wireframe.hlsl", nullptr, "VS", "vs_5_1");
	m_wireframePsByteCode = CompileShader(L"shader\\Wireframe.hlsl", nullptr, "PS", "ps_5_1");

	m_tessellationVsByteCode = CompileShader(L"shader\\DeferredGeometry.hlsl", nullptr, "VS", "vs_5_1");
	m_tessellationHsByteCode = CompileShader(L"shader\\DeferredGeometry.hlsl", nullptr, "HSMain", "hs_5_1");
	m_tessellationDsByteCode = CompileShader(L"shader\\DeferredGeometry.hlsl", nullptr, "DSMain", "ds_5_1");

	m_waterVsByteCode = CompileShader(L"shader\\WaterGeometry.hlsl", nullptr, "VS", "vs_5_1");
	m_waterHsByteCode = CompileShader(L"shader\\WaterGeometry.hlsl", nullptr, "HSMain", "hs_5_1");
	m_waterDsByteCode = CompileShader(L"shader\\WaterGeometry.hlsl", nullptr, "DSMain", "ds_5_1");
	m_waterPsByteCode = CompileShader(L"shader\\WaterGeometry.hlsl", nullptr, "PS", "ps_5_1");

	m_billboardVsByteCode = CompileShader(L"shader\\Billboard.hlsl", nullptr, "VS", "vs_5_1");
	m_billboardPsByteCode = CompileShader(L"shader\\Billboard.hlsl", nullptr, "PS", "ps_5_1");
}

void RenderingSystem::BuildRootSignatures(ID3D12Device* device)
{
	{
		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = 64;
		srvRange.BaseShaderRegister = 0;
		srvRange.RegisterSpace = 0;
		srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER params[4] = {};

		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].Descriptor.RegisterSpace = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 1;
		params[1].Descriptor.RegisterSpace = 0;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[2].Descriptor.ShaderRegister = 2;
		params[2].Descriptor.RegisterSpace = 0;
		params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[3].DescriptorTable.NumDescriptorRanges = 1;
		params[3].DescriptorTable.pDescriptorRanges = &srvRange;
		params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC samp = {};
		samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.MipLODBias = 0.0f;
		samp.MaxAnisotropy = 1;
		samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		samp.MinLOD = 0.0f;
		samp.MaxLOD = D3D12_FLOAT32_MAX;
		samp.ShaderRegister = 0;
		samp.RegisterSpace = 0;
		samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(params);
		desc.pParameters = params;
		desc.NumStaticSamplers = 1;
		desc.pStaticSamplers = &samp;
		desc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		m_geometryRootSignature = CreateRootSignature(device, desc);
	}

	{
		const UINT kLightingTextureCount = 9;

		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = kLightingTextureCount;
		srvRange.BaseShaderRegister = 0;
		srvRange.RegisterSpace = 0;
		srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER params[2] = {};

		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].Descriptor.RegisterSpace = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[1].DescriptorTable.NumDescriptorRanges = 1;
		params[1].DescriptorTable.pDescriptorRanges = &srvRange;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_STATIC_SAMPLER_DESC samplers[3] = {};

		samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
		samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[0].MipLODBias = 0.0f;
		samplers[0].MaxAnisotropy = 1;
		samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplers[0].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		samplers[0].MinLOD = 0.0f;
		samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[0].ShaderRegister = 0;
		samplers[0].RegisterSpace = 0;
		samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
		samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
		samplers[1].MipLODBias = 0.0f;
		samplers[1].MaxAnisotropy = 1;
		samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
		samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		samplers[1].MinLOD = 0.0f;
		samplers[1].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[1].ShaderRegister = 1;
		samplers[1].RegisterSpace = 0;
		samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		samplers[2].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samplers[2].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplers[2].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplers[2].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samplers[2].MipLODBias = 0.0f;
		samplers[2].MaxAnisotropy = 1;
		samplers[2].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samplers[2].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		samplers[2].MinLOD = 0.0f;
		samplers[2].MaxLOD = D3D12_FLOAT32_MAX;
		samplers[2].ShaderRegister = 2;
		samplers[2].RegisterSpace = 0;
		samplers[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(params);
		desc.pParameters = params;
		desc.NumStaticSamplers = _countof(samplers);
		desc.pStaticSamplers = samplers;
		desc.Flags =
			D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
			D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

		m_lightingRootSignature = CreateRootSignature(device, desc);
	}

	{
		D3D12_ROOT_PARAMETER params[2] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 1;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(params);
		desc.pParameters = params;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		m_wireframeRootSignature = CreateRootSignature(device, desc);
	}

	{
		const UINT kTessTextureCount = 128;

		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = kTessTextureCount;
		srvRange.BaseShaderRegister = 0;
		srvRange.RegisterSpace = 0;
		srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER params[4] = {};

		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].Descriptor.RegisterSpace = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 1;
		params[1].Descriptor.RegisterSpace = 0;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[2].Descriptor.ShaderRegister = 2;
		params[2].Descriptor.RegisterSpace = 0;
		params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[3].DescriptorTable.NumDescriptorRanges = 1;
		params[3].DescriptorTable.pDescriptorRanges = &srvRange;
		params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_STATIC_SAMPLER_DESC samp = {};
		samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.MipLODBias = 0.0f;
		samp.MaxAnisotropy = 1;
		samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		samp.MinLOD = 0.0f;
		samp.MaxLOD = D3D12_FLOAT32_MAX;
		samp.ShaderRegister = 0;
		samp.RegisterSpace = 0;
		samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(params);
		desc.pParameters = params;
		desc.NumStaticSamplers = 1;
		desc.pStaticSamplers = &samp;

		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		m_tessellationRootSignature = CreateRootSignature(device, desc);
	}

	{
		const UINT kWaterTextureCount = 64;
		D3D12_DESCRIPTOR_RANGE srvRange = {};
		srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		srvRange.NumDescriptors = kWaterTextureCount;
		srvRange.BaseShaderRegister = 0;
		srvRange.RegisterSpace = 0;
		srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

		D3D12_ROOT_PARAMETER params[4] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].Descriptor.RegisterSpace = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 1;
		params[1].Descriptor.RegisterSpace = 0;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[2].Descriptor.ShaderRegister = 2;
		params[2].Descriptor.RegisterSpace = 0;
		params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[3].DescriptorTable.NumDescriptorRanges = 1;
		params[3].DescriptorTable.pDescriptorRanges = &srvRange;
		params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_STATIC_SAMPLER_DESC samp = {};
		samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
		samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
		samp.MipLODBias = 0.0f;
		samp.MaxAnisotropy = 1;
		samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		samp.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK;
		samp.MinLOD = 0.0f;
		samp.MaxLOD = D3D12_FLOAT32_MAX;
		samp.ShaderRegister = 0;
		samp.RegisterSpace = 0;
		samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(params);
		desc.pParameters = params;
		desc.NumStaticSamplers = 1;
		desc.pStaticSamplers = &samp;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		m_waterRootSignature = CreateRootSignature(device, desc);
	}

	{
		D3D12_ROOT_PARAMETER params[2] = {};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[0].Descriptor.ShaderRegister = 0;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
		params[1].Descriptor.ShaderRegister = 1;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

		D3D12_ROOT_SIGNATURE_DESC desc = {};
		desc.NumParameters = _countof(params);
		desc.pParameters = params;
		desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

		m_billboardRootSignature = CreateRootSignature(device, desc);
	}
}

void RenderingSystem::BuildPSOs(
	ID3D12Device* device,
	DXGI_FORMAT backBufferFormat,
	DXGI_FORMAT depthStencilFormat)
{
	{
		D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_BLEND_DESC blendDesc = {};
		blendDesc.IndependentBlendEnable = FALSE;
		{
			D3D12_RENDER_TARGET_BLEND_DESC rt = {};
			rt.BlendEnable = FALSE;
			rt.LogicOpEnable = FALSE;
			rt.SrcBlend = D3D12_BLEND_ONE;
			rt.DestBlend = D3D12_BLEND_ZERO;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt.LogicOp = D3D12_LOGIC_OP_NOOP;
			rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			for (UINT i = 0; i < Gbuffer::kTargetCount; ++i)
				blendDesc.RenderTarget[i] = rt;
		}

		D3D12_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		dsDesc.StencilEnable = FALSE;
		dsDesc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
		dsDesc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
		dsDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
		dsDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		dsDesc.BackFace = dsDesc.FrontFace;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_geometryRootSignature.Get();
		psoDesc.VS = { m_geometryVsByteCode->GetBufferPointer(), m_geometryVsByteCode->GetBufferSize() };
		psoDesc.PS = { m_geometryPsByteCode->GetBufferPointer(), m_geometryPsByteCode->GetBufferSize() };
		psoDesc.RasterizerState = DefaultRasterizer(D3D12_CULL_MODE_BACK);
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = dsDesc;
		psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = Gbuffer::kTargetCount;
		psoDesc.RTVFormats[0] = Gbuffer::kAlbedoFormat;
		psoDesc.RTVFormats[1] = Gbuffer::kNormalFormat;
		psoDesc.RTVFormats[2] = Gbuffer::kMRFormat;
		psoDesc.DSVFormat = depthStencilFormat;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_geometryPso)));
	}

	{
		D3D12_BLEND_DESC blendDesc = {};
		blendDesc.IndependentBlendEnable = FALSE;
		{
			D3D12_RENDER_TARGET_BLEND_DESC rt = {};
			rt.BlendEnable = FALSE;
			rt.LogicOpEnable = FALSE;
			rt.SrcBlend = D3D12_BLEND_ONE;
			rt.DestBlend = D3D12_BLEND_ZERO;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt.LogicOp = D3D12_LOGIC_OP_NOOP;
			rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			blendDesc.RenderTarget[0] = rt;
		}

		D3D12_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = FALSE;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		dsDesc.StencilEnable = FALSE;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.pRootSignature = m_lightingRootSignature.Get();
		psoDesc.VS = { m_lightingVsByteCode->GetBufferPointer(), m_lightingVsByteCode->GetBufferSize() };
		psoDesc.PS = { m_lightingPsByteCode->GetBufferPointer(), m_lightingPsByteCode->GetBufferSize() };
		psoDesc.RasterizerState = DefaultRasterizer(D3D12_CULL_MODE_NONE);
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = dsDesc;
		psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = backBufferFormat;
		psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_lightingPso)));
	}

	{
		D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_RASTERIZER_DESC rasterizer = DefaultRasterizer(D3D12_CULL_MODE_NONE);
		rasterizer.FillMode = D3D12_FILL_MODE_WIREFRAME;

		D3D12_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		blendDesc.RenderTarget[0].BlendEnable = FALSE;

		D3D12_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		dsDesc.StencilEnable = FALSE;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_wireframeRootSignature.Get();
		psoDesc.VS = { m_wireframeVsByteCode->GetBufferPointer(), m_wireframeVsByteCode->GetBufferSize() };
		psoDesc.PS = { m_wireframePsByteCode->GetBufferPointer(), m_wireframePsByteCode->GetBufferSize() };
		psoDesc.RasterizerState = rasterizer;
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = dsDesc;
		psoDesc.SampleMask = UINT_MAX;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = 1;
		psoDesc.RTVFormats[0] = backBufferFormat;
		psoDesc.DSVFormat = depthStencilFormat;
		psoDesc.SampleDesc.Count = 1;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_wireframePso)));
	}

	{
		D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
			 D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		blendDesc.RenderTarget[0].BlendEnable = FALSE;

		D3D12_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		dsDesc.StencilEnable = FALSE;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_tessellationRootSignature.Get();
		psoDesc.VS = { m_tessellationVsByteCode->GetBufferPointer(), m_tessellationVsByteCode->GetBufferSize() };
		psoDesc.HS = { m_tessellationHsByteCode->GetBufferPointer(), m_tessellationHsByteCode->GetBufferSize() };
		psoDesc.DS = { m_tessellationDsByteCode->GetBufferPointer(), m_tessellationDsByteCode->GetBufferSize() };
		psoDesc.PS = { m_geometryPsByteCode->GetBufferPointer(), m_geometryPsByteCode->GetBufferSize() };
		psoDesc.RasterizerState = DefaultRasterizer(D3D12_CULL_MODE_BACK);
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = dsDesc;
		psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		psoDesc.NumRenderTargets = Gbuffer::kTargetCount;
		psoDesc.RTVFormats[0] = Gbuffer::kAlbedoFormat;
		psoDesc.RTVFormats[1] = Gbuffer::kNormalFormat;
		psoDesc.RTVFormats[2] = Gbuffer::kMRFormat;
		psoDesc.DSVFormat = depthStencilFormat;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_tesselationPso)));
	}

	{
		D3D12_INPUT_ELEMENT_DESC inputLayout[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
			  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
			  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
			  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 32,
			  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		D3D12_BLEND_DESC blendDesc = {};
		blendDesc.IndependentBlendEnable = FALSE;
		{
			D3D12_RENDER_TARGET_BLEND_DESC rt = {};
			rt.BlendEnable = FALSE;
			rt.LogicOpEnable = FALSE;
			rt.SrcBlend = D3D12_BLEND_ONE;
			rt.DestBlend = D3D12_BLEND_ZERO;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.SrcBlendAlpha = D3D12_BLEND_ONE;
			rt.DestBlendAlpha = D3D12_BLEND_ZERO;
			rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			rt.LogicOp = D3D12_LOGIC_OP_NOOP;
			rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			for (UINT i = 0; i < Gbuffer::kTargetCount; ++i)
				blendDesc.RenderTarget[i] = rt;
		}

		D3D12_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		dsDesc.StencilEnable = FALSE;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { inputLayout, _countof(inputLayout) };
		psoDesc.pRootSignature = m_waterRootSignature.Get();
		psoDesc.VS = { m_waterVsByteCode->GetBufferPointer(), m_waterVsByteCode->GetBufferSize() };
		psoDesc.HS = { m_waterHsByteCode->GetBufferPointer(), m_waterHsByteCode->GetBufferSize() };
		psoDesc.DS = { m_waterDsByteCode->GetBufferPointer(), m_waterDsByteCode->GetBufferSize() };
		psoDesc.PS = { m_waterPsByteCode->GetBufferPointer(), m_waterPsByteCode->GetBufferSize() };
		psoDesc.RasterizerState = DefaultRasterizer(D3D12_CULL_MODE_FRONT);
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = dsDesc;
		psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
		psoDesc.NumRenderTargets = Gbuffer::kTargetCount;
		psoDesc.RTVFormats[0] = Gbuffer::kAlbedoFormat;
		psoDesc.RTVFormats[1] = Gbuffer::kNormalFormat;
		psoDesc.RTVFormats[2] = Gbuffer::kMRFormat;
		psoDesc.DSVFormat = depthStencilFormat;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_waterPso)));
	}

	{
		D3D12_BLEND_DESC blendDesc = {};
		blendDesc.IndependentBlendEnable = FALSE;
		for (UINT i = 0; i < Gbuffer::kTargetCount; ++i) {
			D3D12_RENDER_TARGET_BLEND_DESC rt = {};
			rt.BlendEnable = FALSE;
			rt.LogicOpEnable = FALSE;
			rt.SrcBlend = D3D12_BLEND_ONE;
			rt.DestBlend = D3D12_BLEND_ZERO;
			rt.BlendOp = D3D12_BLEND_OP_ADD;
			rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			blendDesc.RenderTarget[i] = rt;
		}

		D3D12_DEPTH_STENCIL_DESC dsDesc = {};
		dsDesc.DepthEnable = TRUE;
		dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
		dsDesc.StencilEnable = FALSE;

		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.InputLayout = { nullptr, 0 };
		psoDesc.pRootSignature = m_billboardRootSignature.Get();
		psoDesc.VS = { m_billboardVsByteCode->GetBufferPointer(), m_billboardVsByteCode->GetBufferSize() };
		psoDesc.PS = { m_billboardPsByteCode->GetBufferPointer(), m_billboardPsByteCode->GetBufferSize() };
		psoDesc.RasterizerState = DefaultRasterizer(D3D12_CULL_MODE_NONE);
		psoDesc.BlendState = blendDesc;
		psoDesc.DepthStencilState = dsDesc;
		psoDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;
		psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets = Gbuffer::kTargetCount;
		psoDesc.RTVFormats[0] = Gbuffer::kAlbedoFormat;
		psoDesc.RTVFormats[1] = Gbuffer::kNormalFormat;
		psoDesc.RTVFormats[2] = Gbuffer::kMRFormat;
		psoDesc.DSVFormat = depthStencilFormat;
		psoDesc.SampleDesc.Count = 1;
		psoDesc.SampleDesc.Quality = 0;

		ThrowIfFailed(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_billboardPso)));
	}
}