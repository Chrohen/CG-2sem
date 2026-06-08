#include "Framework.hpp"
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <cmath>
#include <string>
#include <random>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

#include "directx/d3dx12.h"
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cfloat>

#if defined(_DEBUG)
#include <d3d12sdklayers.h>
#endif

using namespace DirectX;

Framework::Framework(int width, int height, const wchar_t* title)
	: m_initWidth(width)
	, m_initHeight(height)
	, m_title(title ? title : L"")
	, m_clientWidth(width)
	, m_clientHeight(height)
{
}

Framework::~Framework() {
	if (m_device)
		FlushCommandQueue();
	if (m_fenceEvent) {
		CloseHandle(m_fenceEvent);
		m_fenceEvent = nullptr;
	}
}

bool Framework::Init()
{
	m_window = std::make_unique<Window>(m_initWidth, m_initHeight, m_title, this);

	InitDxgi();
	InitD3D12Device();
	m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	CreateCommandObjects();
	CreateFence();
	CreateSwapChain();
	CreateRtvAndDsvDescriptorHeaps();

	m_renderingSystem.BuildShaders();
	m_renderingSystem.BuildRootSignatures(m_device.Get());
	m_renderingSystem.BuildPSOs(m_device.Get(), m_backBufferFormat, m_depthStencilFormat);

	BuildConstantBuffers();
	BuildBoxGeometry();
	GenerateCubes(5000);
	BuildOctree();
	m_octreeCB = std::make_unique<UploadBuffer<ObjectConstants>>(m_device.Get(), 1, true);
	BuildObjVB_Upload();
	InitFallingLights();
	BuildWaterPlane();

	OnResize();

	return MainWnd() != nullptr;
}

int Framework::Run()
{
	m_timer.Reset();

	while (m_window->ProcessMessages()) {
		m_timer.Tick();

		if (!m_appPaused) {
			const double dt = m_timer.DeltaTime();
			Update(dt);
			Draw();
		}
		else {
			Sleep(100);
		}
	}
	return 0;
}

LRESULT Framework::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_SIZE:
		m_clientWidth = LOWORD(lParam);
		m_clientHeight = HIWORD(lParam);
		if (wParam == SIZE_MINIMIZED) {
			m_appPaused = true; m_minimized = true; m_maximized = false;
			m_timer.Stop();
		}
		else if (wParam == SIZE_MAXIMIZED) {
			m_appPaused = false; m_minimized = false; m_maximized = true;
			m_timer.Start(); OnResize();
		}
		else if (wParam == SIZE_RESTORED) {
			if (m_minimized) { m_appPaused = false; m_minimized = false; m_timer.Start(); OnResize(); }
			else if (m_maximized) { m_appPaused = false; m_maximized = false; m_timer.Start(); OnResize(); }
			else if (!m_resizing) { OnResize(); }
		}
		return 0;

	case WM_ACTIVATEAPP:
		if (wParam == FALSE) { m_appPaused = true;  m_timer.Stop(); }
		else { m_appPaused = false; m_timer.Start(); }
		return 0;

	case WM_ENTERSIZEMOVE:
		m_appPaused = true; m_resizing = true; m_timer.Stop();
		return 0;

	case WM_EXITSIZEMOVE:
		m_appPaused = false; m_resizing = false; m_timer.Start(); OnResize();
		return 0;

	case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN:
		OnMouseDown(hwnd, wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_LBUTTONUP: case WM_MBUTTONUP: case WM_RBUTTONUP:
		OnMouseUp(hwnd, wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;
	case WM_MOUSEMOVE:
		OnMouseMove(hwnd, wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_KEYDOWN: case WM_SYSKEYDOWN:
		if (wParam == 'O')
		{
			m_wireframeMode = !m_wireframeMode;
			return 0;
		}
		if (wParam == 'T') {
			m_tessellationEnabled = !m_tessellationEnabled;
		}
		if (wParam == 'F') {
			m_frustumCullingEnabled = !m_frustumCullingEnabled;
			return 0;
		}
		if (wParam == 'G') {
			m_octreeCullingEnabled = !m_octreeCullingEnabled;
			return 0;
		}
		if (wParam == 'H') {
			m_showOctree = !m_showOctree;
			return 0;
		}
		if (wParam == 'B') {
			m_billboardEnabled = !m_billboardEnabled;
			return 0;
		}
		m_keyDown[static_cast<uint8_t>(wParam)] = true;
		return 0;
	case WM_KEYUP: case WM_SYSKEYUP:
		m_keyDown[static_cast<uint8_t>(wParam)] = false;
		return 0;
	case WM_KILLFOCUS:
		m_keyDown.fill(false);
		return 0;
	}
	return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void Framework::CreateRtvAndDsvDescriptorHeaps()
{
	m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	m_dsvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	m_cbvSrvUavDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.NumDescriptors = 1;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
}

void Framework::OnResize()
{
	if (!m_device || !m_swapChain || !m_commandQueue || !m_directCmdListAlloc || !m_commandList)
		return;

	FlushCommandQueue();

	ThrowIfFailed(m_directCmdListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_directCmdListAlloc.Get(), nullptr));

	for (UINT i = 0; i < SwapChainBufferCount; ++i)
		m_swapChainBuffer[i].Reset();
	m_depthStencilBuffer.Reset();

	ThrowIfFailed(m_swapChain->ResizeBuffers(
		SwapChainBufferCount, m_clientWidth, m_clientHeight, m_backBufferFormat, 0));
	m_currBackBuffer = static_cast<int>(m_swapChain->GetCurrentBackBufferIndex());

	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < SwapChainBufferCount; ++i) {
		ThrowIfFailed(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_swapChainBuffer[i])));
		m_device->CreateRenderTargetView(m_swapChainBuffer[i].Get(), nullptr, rtvHandle);
		rtvHandle.ptr += m_rtvDescriptorSize;
	}

	DXGI_FORMAT depthResourceFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
	DXGI_FORMAT dsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	D3D12_RESOURCE_DESC depthDesc = {};
	depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthDesc.Width = static_cast<UINT64>(m_clientWidth);
	depthDesc.Height = static_cast<UINT>(m_clientHeight);
	depthDesc.DepthOrArraySize = 1;
	depthDesc.MipLevels = 1;
	depthDesc.Format = depthResourceFormat;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.SampleDesc.Quality = 0;
	depthDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear = {};
	optClear.Format = dsvFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	D3D12_HEAP_PROPERTIES defaultHeap = {};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;

	ThrowIfFailed(m_device->CreateCommittedResource(
		&defaultHeap, D3D12_HEAP_FLAG_NONE, &depthDesc,
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		&optClear,
		IID_PPV_ARGS(&m_depthStencilBuffer)));

	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
	dsvDesc.Format = dsvFormat;
	dsvDesc.Texture2D.MipSlice = 0;
	m_device->CreateDepthStencilView(m_depthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

	m_gbuffer.Resize(m_device.Get(), m_clientWidth, m_clientHeight, m_rtvDescriptorSize);

	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
		heapDesc.NumDescriptors = 3;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_gbufferSrvHeap)));

		m_gbuffer.CreateSrvDescriptors(
			m_device.Get(),
			m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart(),
			m_cbvSrvUavDescriptorSize);

		D3D12_CPU_DESCRIPTOR_HANDLE depthSrvHandle = m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart();
		depthSrvHandle.ptr += 2 * m_cbvSrvUavDescriptorSize;

		D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
		depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		depthSrvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
		depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		depthSrvDesc.Texture2D.MipLevels = 1;
		depthSrvDesc.Texture2D.MostDetailedMip = 0;
		m_device->CreateShaderResourceView(m_depthStencilBuffer.Get(), &depthSrvDesc, depthSrvHandle);
	}

	m_screenViewport = { 0.f, 0.f,
		static_cast<float>(m_clientWidth), static_cast<float>(m_clientHeight),
		0.f, 1.f };
	m_scissorRect = { 0, 0, m_clientWidth, m_clientHeight };

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* cmds[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, cmds);
	FlushCommandQueue();
}

void Framework::Update(const double& dt)
{
	static double timeElapsed = 0.0;
	static int frameCount = 0;
	frameCount++;
	timeElapsed += dt;

	if (timeElapsed >= 0.5) {
		m_currentFPS = static_cast<float>(frameCount) / static_cast<float>(timeElapsed);
		frameCount = 0;
		timeElapsed = 0.0;

		std::wostringstream woss;
		woss << m_title << L" - FPS: " << static_cast<int>(m_currentFPS);
		if (m_octreeCullingEnabled)
			woss << L" | Octree + Frustum | Visible: " << m_visibleCubeCount << L" / " << m_cubeInstances.size();
		else if (m_frustumCullingEnabled)
			woss << L" | Frustum | Visible: " << m_visibleCubeCount << L" / " << m_cubeInstances.size();
		else
			woss << L" | Cubes: " << m_cubeInstances.size();
		SetWindowText(MainWnd(), woss.str().c_str());
	}

	ObjectConstants obj = {};
	XMMATRIX world =
		XMMatrixTranslation(-m_modelCenter.x, -m_modelCenter.y, -m_modelCenter.z) *
		XMMatrixScaling(m_modelScale, m_modelScale, m_modelScale);
	XMMATRIX worldInvTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, world));
	XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
	XMStoreFloat4x4(&obj.WorldInvTranspose, worldInvTranspose);
	m_objectCB->CopyData(0, obj);

	XMVECTOR pos = XMLoadFloat3(&m_camPos);
	XMVECTOR target = XMLoadFloat3(&m_camTarget);
	XMVECTOR up = XMVector3Normalize(XMLoadFloat3(&m_camUp));
	XMVECTOR forward = XMVector3Normalize(target - pos);
	XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, forward));

	float speed = m_cameraMoveSpeed;
	if (m_keyDown[VK_SHIFT]) speed *= 3.0f;
	float step = speed * static_cast<float>(dt);

	XMVECTOR move = XMVectorZero();
	if (m_keyDown['W']) move += forward;
	if (m_keyDown['S']) move -= forward;
	if (m_keyDown['D']) move += right;
	if (m_keyDown['A']) move -= right;
	if (m_keyDown[VK_SPACE])   move += up;
	if (m_keyDown[VK_CONTROL]) move -= up;

	if (!XMVector3Equal(move, XMVectorZero()))
		move = XMVector3Normalize(move) * step;

	pos += move;
	target += move;

	XMStoreFloat3(&m_camPos, pos);
	XMStoreFloat3(&m_camTarget, target);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	float aspect = (float)m_clientWidth / (float)m_clientHeight;
	XMMATRIX proj = XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect, 0.1f, 1000.0f);
	XMMATRIX viewProj = view * proj;

	PassConstants pass;
	XMStoreFloat4x4(&pass.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat3(&pass.EyePosW, pos);
	pass.LightDirW = { 0.577f, -0.3f, 0.577f };
	pass.Time = static_cast<float>(m_timer.TotalTime());

	pass.Ambient = { 0.2f, 0.2f, 0.2f, 1.0f };
	pass.Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
	pass.Specular = { 0.3f, 0.3f, 0.3f, 0.3f };
	pass.SpecPower = 8.0f;

	pass.MinTessDistance = 1.0f;  
	pass.MaxTessDistance = 20.0f;
	pass.MinTessFactor = 0.001f;
	pass.MaxTessFactor = 8.0f;

	m_passCB->CopyData(0, pass);

	LightingConstants lc{};
	XMStoreFloat3(&lc.EyePosW, pos);
	lc.Ambient = { 0.15f, 0.15f, 0.15f, 1.0f };
	lc.SpecPower = 32.0f;

	XMVECTOR det;
	XMMATRIX invViewProj = XMMatrixInverse(&det, viewProj);
	XMStoreFloat4x4(&lc.InvViewProj, XMMatrixTranspose(invViewProj));

	// Освещение
	float deltat = static_cast<float>(m_timer.DeltaTime());
	for (auto& light : m_fallingLights)
	{
		light.position.y += light.velocityY * deltat;

		if (light.position.y <= light.groundLevel)
		{
			light.position.y = 20.0f;
		}
	}

	lc.NumDirLights = 1;
	lc.DirLights[0].Direction = { 0.577f, -0.577f, 0.577f };
	lc.DirLights[0].Color = { 1.0f, 0.95f, 0.85f };
	lc.DirLights[0].Intensity = 1.0f;

	lc.NumPointLights = (int)m_fallingLights.size();

	for (size_t i = 0; i < m_fallingLights.size(); ++i)
	{
		lc.PointLights[i].Position = m_fallingLights[i].position;
		lc.PointLights[i].Range = m_fallingLights[i].range;
		lc.PointLights[i].Color = m_fallingLights[i].color;
		lc.PointLights[i].Intensity = m_fallingLights[i].intensity;
	}

	lc.NumSpotLights = 1;
	lc.SpotLights[0].Position = { 0.0f, 3.0f, 0.0f };
	lc.SpotLights[0].Direction = { 0.0f, -1.0f, 0.0f };
	lc.SpotLights[0].Range = 6.0f;
	lc.SpotLights[0].InnerCosAngle = cosf(XMConvertToRadians(15.0f));
	lc.SpotLights[0].OuterCosAngle = cosf(XMConvertToRadians(30.0f));
	lc.SpotLights[0].Color = { 1.0f, 1.0f, 0.8f };
	lc.SpotLights[0].Intensity = 4.0f;

	m_lightingCB->CopyData(0, lc);
}

void Framework::Draw()
{
	ThrowIfFailed(m_directCmdListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_directCmdListAlloc.Get(), nullptr));

	m_commandList->RSSetViewports(1, &m_screenViewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	if (m_wireframeMode)
	{
		auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		m_commandList->ResourceBarrier(1, &barrier);

		D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentBackBufferView();
		auto dsv = DepthStencilView();
		m_commandList->OMSetRenderTargets(1, &rtv, TRUE, &dsv);
		m_commandList->ClearRenderTargetView(rtv, Colors::Black, 0, nullptr);
		m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		m_commandList->SetPipelineState(m_renderingSystem.WireframePSO());
		m_commandList->SetGraphicsRootSignature(m_renderingSystem.WireframeRootSignature());

		m_commandList->SetGraphicsRootConstantBufferView(0, m_objectCB->Resource()->GetGPUVirtualAddress());
		m_commandList->SetGraphicsRootConstantBufferView(1, m_passCB->Resource()->GetGPUVirtualAddress());

		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		if (m_modelVB && m_modelVertexCount > 0)
		{
			m_commandList->IASetVertexBuffers(0, 1, &m_modelVBV);
			for (const auto& range : m_drawRanges)
				m_commandList->DrawInstanced(range.vertexCount, 1, range.startVertex, 0);
		}
		else
		{
			m_commandList->IASetVertexBuffers(0, 1, &m_boxVBView);
			m_commandList->IASetIndexBuffer(&m_boxIBView);
			m_commandList->DrawIndexedInstanced(m_boxIndexCount, 1, 0, 0, 0);
		}

		barrier = CD3DX12_RESOURCE_BARRIER::Transition(
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT);
		m_commandList->ResourceBarrier(1, &barrier);

		ThrowIfFailed(m_commandList->Close());
		ID3D12CommandList* cmds[] = { m_commandList.Get() };
		m_commandQueue->ExecuteCommandLists(1, cmds);
		ThrowIfFailed(m_swapChain->Present(0, 0));
		m_currBackBuffer = (m_currBackBuffer + 1) % SwapChainBufferCount;

		FlushCommandQueue();
		return;
	}


	m_gbuffer.TransitionToRenderTargets(m_commandList.Get());
	m_gbuffer.Clear(m_commandList.Get());

	D3D12_CPU_DESCRIPTOR_HANDLE dsv = DepthStencilView();
	m_commandList->ClearDepthStencilView(
		dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	m_gbuffer.BindAsRenderTargets(m_commandList.Get(), dsv);

	if (m_tessellationEnabled)
	{
		m_commandList->SetPipelineState(m_renderingSystem.TessellationPSO());
		m_commandList->SetGraphicsRootSignature(m_renderingSystem.TessellationRootSignature());
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
	}
	else
	{
		m_commandList->SetPipelineState(m_renderingSystem.GeometryPSO());
		m_commandList->SetGraphicsRootSignature(m_renderingSystem.GeometryRootSignature());
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	}

	if (m_srvHeap) {
		ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
		m_commandList->SetDescriptorHeaps(1, heaps);
		m_commandList->SetGraphicsRootDescriptorTable(
			3, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
	}

	m_commandList->SetGraphicsRootConstantBufferView(
		0, m_objectCB->Resource()->GetGPUVirtualAddress());
	m_commandList->SetGraphicsRootConstantBufferView(
		1, m_passCB->Resource()->GetGPUVirtualAddress());

	if (m_modelVB && m_modelVertexCount > 0)
	{
		m_commandList->IASetVertexBuffers(0, 1, &m_modelVBV);
		for (const auto& range : m_drawRanges)
		{
			int safeId = range.materialId;
			if (safeId < 0 || safeId >= (int)m_materialCBs.size())
				safeId = 0;
			m_commandList->SetGraphicsRootConstantBufferView(
				2, m_materialCBs[safeId]->Resource()->GetGPUVirtualAddress());
			m_commandList->DrawInstanced(range.vertexCount, 1, range.startVertex, 0);
		}
	}
	else
	{
		m_commandList->IASetVertexBuffers(0, 1, &m_boxVBView);
		m_commandList->IASetIndexBuffer(&m_boxIBView);
		if (!m_materialCBs.empty())
			m_commandList->SetGraphicsRootConstantBufferView(
				2, m_materialCBs[0]->Resource()->GetGPUVirtualAddress());
		m_commandList->DrawIndexedInstanced(m_boxIndexCount, 1, 0, 0, 0);
	}

	DrawCubes();
	DrawOctree();
	DrawBillboards();

	// Отрисовка воды
	if (m_waterVB && m_waterVertexCount > 0)
	{
		m_commandList->SetPipelineState(m_renderingSystem.WaterPSO());
		m_commandList->SetGraphicsRootSignature(m_renderingSystem.WaterRootSignature());
		m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_4_CONTROL_POINT_PATCHLIST);
		m_commandList->IASetVertexBuffers(0, 1, &m_waterVBV);

		m_commandList->SetGraphicsRootConstantBufferView(0, m_waterObjectCB->Resource()->GetGPUVirtualAddress());
		m_commandList->SetGraphicsRootConstantBufferView(1, m_passCB->Resource()->GetGPUVirtualAddress());
		m_commandList->SetGraphicsRootConstantBufferView(2, m_waterMaterialCB->Resource()->GetGPUVirtualAddress());

		if (m_srvHeap)
		{
			ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
			m_commandList->SetDescriptorHeaps(1, heaps);
			m_commandList->SetGraphicsRootDescriptorTable(3, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
		}

		m_commandList->DrawInstanced(4, 1, 0, 0);
	}

	m_gbuffer.TransitionToShaderResources(m_commandList.Get());

	D3D12_RESOURCE_BARRIER toRT = {};
	toRT.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toRT.Transition.pResource = CurrentBackBuffer();
	toRT.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
	toRT.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toRT.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_commandList->ResourceBarrier(1, &toRT);

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = CurrentBackBufferView();
	m_commandList->OMSetRenderTargets(1, &rtv, TRUE, nullptr);
	m_commandList->ClearRenderTargetView(rtv, DirectX::Colors::Black, 0, nullptr);

	m_commandList->SetPipelineState(m_renderingSystem.LightingPSO());
	m_commandList->SetGraphicsRootSignature(m_renderingSystem.LightingRootSignature());

	{
		ID3D12DescriptorHeap* heaps[] = { m_gbufferSrvHeap.Get() };
		m_commandList->SetDescriptorHeaps(1, heaps);
	}

	m_commandList->SetGraphicsRootConstantBufferView(
		0, m_lightingCB->Resource()->GetGPUVirtualAddress());

	m_commandList->SetGraphicsRootDescriptorTable(
		1, m_gbufferSrvHeap->GetGPUDescriptorHandleForHeapStart());

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->DrawInstanced(3, 1, 0, 0);

	D3D12_RESOURCE_BARRIER toPresent = {};
	toPresent.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	toPresent.Transition.pResource = CurrentBackBuffer();
	toPresent.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
	toPresent.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
	toPresent.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_commandList->ResourceBarrier(1, &toPresent);

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* cmdsLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(m_swapChain->Present(0, 0));
	m_currBackBuffer = (m_currBackBuffer + 1) % SwapChainBufferCount;

	FlushCommandQueue();
}

void Framework::InitDxgi()
{
	UINT factoryFlags = 0;
#if defined(_DEBUG)
	factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
#endif
	ThrowIfFailed(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_dxgiFactory)));
#if defined(_DEBUG)
	LogAdapters();
#endif
	PickAdapter();
}

void Framework::PickAdapter()
{
	m_dxgiAdapter.Reset();
	m_adapterName.clear();

	ComPtr<IDXGIFactory6> factory6;
	if (SUCCEEDED(m_dxgiFactory.As(&factory6))) {
		for (UINT i = 0;; ++i) {
			ComPtr<IDXGIAdapter1> adapter;
			if (factory6->EnumAdapterByGpuPreference(
				i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
				IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND) break;
			DXGI_ADAPTER_DESC1 desc = {};
			ThrowIfFailed(adapter->GetDesc1(&desc));
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
			ComPtr<ID3D12Device> testDevice;
			if (SUCCEEDED(D3D12CreateDevice(
				adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&testDevice)))) {
				m_dxgiAdapter = adapter; m_adapterName = desc.Description; break;
			}
		}
	}
	if (!m_dxgiAdapter) {
		for (UINT i = 0;; ++i) {
			ComPtr<IDXGIAdapter1> adapter;
			if (m_dxgiFactory->EnumAdapters1(i, &adapter) == DXGI_ERROR_NOT_FOUND) break;
			DXGI_ADAPTER_DESC1 desc = {};
			ThrowIfFailed(adapter->GetDesc1(&desc));
			if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) continue;
			ComPtr<ID3D12Device> testDevice;
			if (SUCCEEDED(D3D12CreateDevice(
				adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&testDevice)))) {
				m_dxgiAdapter = adapter; m_adapterName = desc.Description; break;
			}
		}
	}
	if (!m_dxgiAdapter)
		throw std::runtime_error("No suitable DXGI adapter found (D3D12-capable).");
#if defined(_DEBUG)
	OutputDebugStringW((L"[DXGI] Using adapter: " + m_adapterName + L"\n").c_str());
#endif
}

void Framework::LogAdapters()
{
#if defined(_DEBUG)
	OutputDebugStringW(L"[DXGI] Adapters:\n");
	for (UINT i = 0;; ++i) {
		ComPtr<IDXGIAdapter1> adapter;
		HRESULT hr = m_dxgiFactory->EnumAdapters1(i, &adapter);
		if (hr == DXGI_ERROR_NOT_FOUND) break;
		ThrowIfFailed(hr);
		DXGI_ADAPTER_DESC1 desc = {};
		ThrowIfFailed(adapter->GetDesc1(&desc));
		std::wstring line = L"  -  ";
		line += desc.Description;
		line += (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? L" (SOFTWARE)\n" : L"\n";
		OutputDebugStringW(line.c_str());
		LogAdapterOutputs(adapter.Get());
	}
#endif
}

void Framework::LogAdapterOutputs(IDXGIAdapter1* adapter)
{
#if defined(_DEBUG)
	for (UINT j = 0;; ++j) {
		ComPtr<IDXGIOutput> output;
		if (adapter->EnumOutputs(j, &output) == DXGI_ERROR_NOT_FOUND) break;
		DXGI_OUTPUT_DESC outDesc = {};
		ThrowIfFailed(output->GetDesc(&outDesc));
		std::wstring line = L"\t\tOutput: ";
		line += outDesc.DeviceName;
		line += L"\n";
		OutputDebugStringW(line.c_str());
	}
#endif
}

void Framework::InitD3D12Device()
{
#if defined(_DEBUG)
	ComPtr<ID3D12Debug> debugController;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		debugController->EnableDebugLayer();
		OutputDebugStringW(L"[D3D12] Debug layer enabled\n");
	}
#endif
	HRESULT hr = D3D12CreateDevice(
		m_dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device));
	if (FAILED(hr)) {
		ThrowIfFailed(m_dxgiFactory->EnumWarpAdapter(IID_PPV_ARGS(&m_dxgiAdapter)));
		ThrowIfFailed(D3D12CreateDevice(
			m_dxgiAdapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));
	}
#if defined(_DEBUG)
	ComPtr<ID3D12InfoQueue> infoQueue;
	if (SUCCEEDED(m_device.As(&infoQueue))) {
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
	}
#endif
}

void Framework::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC qdesc = {};
	qdesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	qdesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	ThrowIfFailed(m_device->CreateCommandQueue(&qdesc, IID_PPV_ARGS(&m_commandQueue)));
	ThrowIfFailed(m_device->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_directCmdListAlloc)));
	ThrowIfFailed(m_device->CreateCommandList(
		0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		m_directCmdListAlloc.Get(), nullptr, IID_PPV_ARGS(&m_commandList)));
	ThrowIfFailed(m_commandList->Close());
}

void Framework::CreateFence()
{
	ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
	m_currentFence = 0;
	m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!m_fenceEvent)
		throw std::runtime_error("CreateEvent failed for fence event.");
}

void Framework::FlushCommandQueue()
{
	if (!m_commandQueue || !m_fence || !m_fenceEvent) return;
	++m_currentFence;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), m_currentFence));
	if (m_fence->GetCompletedValue() < m_currentFence) {
		ThrowIfFailed(m_fence->SetEventOnCompletion(m_currentFence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}
}

void Framework::CreateSwapChain()
{
	m_swapChain.Reset();
	DXGI_SWAP_CHAIN_DESC1 sd = {};
	sd.Width = m_clientWidth;
	sd.Height = m_clientHeight;
	sd.Format = m_backBufferFormat;
	sd.SampleDesc = { 1, 0 };
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Scaling = DXGI_SCALING_STRETCH;
	sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	ComPtr<IDXGISwapChain1> swapChain1;
	ThrowIfFailed(m_dxgiFactory->CreateSwapChainForHwnd(
		m_commandQueue.Get(), MainWnd(), &sd, nullptr, nullptr, &swapChain1));
	ThrowIfFailed(m_dxgiFactory->MakeWindowAssociation(MainWnd(), DXGI_MWA_NO_ALT_ENTER));
	ThrowIfFailed(swapChain1.As(&m_swapChain));
	m_currBackBuffer = static_cast<int>(m_swapChain->GetCurrentBackBufferIndex());
}

void Framework::BuildConstantBuffers()
{
	m_objectCB = std::make_unique<UploadBuffer<ObjectConstants>>(m_device.Get(), 1, true);
	m_passCB = std::make_unique<UploadBuffer<PassConstants>>(m_device.Get(), 1, true);
	m_lightingCB = std::make_unique<UploadBuffer<LightingConstants>>(m_device.Get(), 1, true);
}

void Framework::BuildGBufferSrvHeap()
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = Gbuffer::kTargetCount;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_gbufferSrvHeap)));

	m_gbuffer.CreateSrvDescriptors(
		m_device.Get(),
		m_gbufferSrvHeap->GetCPUDescriptorHandleForHeapStart(),
		m_cbvSrvUavDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE Framework::CurrentBackBufferView() const
{
	D3D12_CPU_DESCRIPTOR_HANDLE h = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();
	h.ptr += static_cast<SIZE_T>(m_currBackBuffer) * m_rtvDescriptorSize;
	return h;
}

D3D12_CPU_DESCRIPTOR_HANDLE Framework::DepthStencilView() const
{
	return m_dsvHeap->GetCPUDescriptorHandleForHeapStart();
}

void Framework::BuildBoxGeometry()
{
	auto ColorFromPos = [](float x, float y, float z) {
		return XMFLOAT4((x + 1.f) * 0.5f, (y + 1.f) * 0.5f, (z + 1.f) * 0.5f, 1.f);
		};

	std::array<Vertex, 24> vertices = {
		Vertex{{-1,-1,-1},{0,0,-1},{0,0},ColorFromPos(-1,-1,-1)},
		Vertex{{-1, 1,-1},{0,0,-1},{0,0},ColorFromPos(-1, 1,-1)},
		Vertex{{ 1, 1,-1},{0,0,-1},{0,0},ColorFromPos(1, 1,-1)},
		Vertex{{ 1,-1,-1},{0,0,-1},{0,0},ColorFromPos(1,-1,-1)},
		Vertex{{ 1,-1, 1},{0,0, 1},{0,0},ColorFromPos(1,-1, 1)},
		Vertex{{ 1, 1, 1},{0,0, 1},{0,0},ColorFromPos(1, 1, 1)},
		Vertex{{-1, 1, 1},{0,0, 1},{0,0},ColorFromPos(-1, 1, 1)},
		Vertex{{-1,-1, 1},{0,0, 1},{0,0},ColorFromPos(-1,-1, 1)},
		Vertex{{-1,-1, 1},{-1,0,0},{0,0},ColorFromPos(-1,-1, 1)},
		Vertex{{-1, 1, 1},{-1,0,0},{0,0},ColorFromPos(-1, 1, 1)},
		Vertex{{-1, 1,-1},{-1,0,0},{0,0},ColorFromPos(-1, 1,-1)},
		Vertex{{-1,-1,-1},{-1,0,0},{0,0},ColorFromPos(-1,-1,-1)},
		Vertex{{ 1,-1,-1},{ 1,0,0},{0,0},ColorFromPos(1,-1,-1)},
		Vertex{{ 1, 1,-1},{ 1,0,0},{0,0},ColorFromPos(1, 1,-1)},
		Vertex{{ 1, 1, 1},{ 1,0,0},{0,0},ColorFromPos(1, 1, 1)},
		Vertex{{ 1,-1, 1},{ 1,0,0},{0,0},ColorFromPos(1,-1, 1)},
		Vertex{{-1, 1,-1},{0,1,0},{0,0},ColorFromPos(-1, 1,-1)},
		Vertex{{-1, 1, 1},{0,1,0},{0,0},ColorFromPos(-1, 1, 1)},
		Vertex{{ 1, 1, 1},{0,1,0},{0,0},ColorFromPos(1, 1, 1)},
		Vertex{{ 1, 1,-1},{0,1,0},{0,0},ColorFromPos(1, 1,-1)},
		Vertex{{ 1,-1,-1},{0,-1,0},{0,0},ColorFromPos(1,-1,-1)},
		Vertex{{ 1,-1, 1},{0,-1,0},{0,0},ColorFromPos(1,-1, 1)},
		Vertex{{-1,-1, 1},{0,-1,0},{0,0},ColorFromPos(-1,-1, 1)},
		Vertex{{-1,-1,-1},{0,-1,0},{0,0},ColorFromPos(-1,-1,-1)},
	};

	std::array<uint16_t, 36> indices = {
		0,1,2, 0,2,3,   4,5,6, 4,6,7,
		8,9,10, 8,10,11, 12,13,14, 12,14,15,
		16,17,18, 16,18,19, 20,21,22, 20,22,23
	};

	m_boxIndexCount = (UINT)indices.size();
	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(uint16_t);

	auto MakeBufDesc = [](UINT64 sz) {
		D3D12_RESOURCE_DESC d = {};
		d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; d.Width = sz;
		d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
		d.Format = DXGI_FORMAT_UNKNOWN; d.SampleDesc.Count = 1;
		d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		return d;
		};
	D3D12_HEAP_PROPERTIES defHeap = {}; defHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_HEAP_PROPERTIES upHeap = {}; upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	auto vbDesc = MakeBufDesc(vbByteSize);
	auto ibDesc = MakeBufDesc(ibByteSize);

	ThrowIfFailed(m_device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_boxVB.GetAddressOf())));
	ThrowIfFailed(m_device->CreateCommittedResource(&defHeap, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_boxIB.GetAddressOf())));
	ThrowIfFailed(m_device->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &vbDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_boxVBUpload.GetAddressOf())));
	ThrowIfFailed(m_device->CreateCommittedResource(&upHeap, D3D12_HEAP_FLAG_NONE, &ibDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_boxIBUpload.GetAddressOf())));

	{ void* m = nullptr; ThrowIfFailed(m_boxVBUpload->Map(0, nullptr, &m)); memcpy(m, vertices.data(), vbByteSize); m_boxVBUpload->Unmap(0, nullptr); }
	{ void* m = nullptr; ThrowIfFailed(m_boxIBUpload->Map(0, nullptr, &m)); memcpy(m, indices.data(), ibByteSize);  m_boxIBUpload->Unmap(0, nullptr); }

	ThrowIfFailed(m_directCmdListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_directCmdListAlloc.Get(), nullptr));
	m_commandList->CopyBufferRegion(m_boxVB.Get(), 0, m_boxVBUpload.Get(), 0, vbByteSize);
	m_commandList->CopyBufferRegion(m_boxIB.Get(), 0, m_boxIBUpload.Get(), 0, ibByteSize);

	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition = { m_boxVB.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER };
	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition = { m_boxIB.Get(), D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER };
	m_commandList->ResourceBarrier(2, barriers);

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* cmds[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, cmds);
	FlushCommandQueue();

	m_boxVBView.BufferLocation = m_boxVB->GetGPUVirtualAddress();
	m_boxVBView.StrideInBytes = sizeof(Vertex);
	m_boxVBView.SizeInBytes = vbByteSize;
	m_boxIBView.BufferLocation = m_boxIB->GetGPUVirtualAddress();
	m_boxIBView.Format = DXGI_FORMAT_R16_UINT;
	m_boxIBView.SizeInBytes = ibByteSize;

	m_boxVBUpload.Reset();
	m_boxIBUpload.Reset();
}

void Framework::BuildObjVB_Upload()
{
	using namespace DirectX;

	const std::wstring objPathW = L"assets\\sponza\\sponza.obj";

	auto WideToUtf8 = [](const std::wstring& w) -> std::string {
		if (w.empty()) return {};
		int sz = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
		std::string s((sz > 0) ? (sz - 1) : 0, '\0');
		if (sz > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), sz, nullptr, nullptr);
		return s;
		};

	std::string objPath = WideToUtf8(objPathW);
	std::string baseDir;
	{
		size_t pos = objPath.find_last_of("\\/");
		baseDir = (pos != std::string::npos) ? objPath.substr(0, pos + 1) : "";
		for (char& c : baseDir) if (c == '/') c = '\\';
	}

	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t>    shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	bool ok = tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
		objPath.c_str(), baseDir.empty() ? nullptr : baseDir.c_str(), true);
	if (!warn.empty()) OutputDebugStringA(("[tinyobj warn] " + warn + "\n").c_str());
	if (!ok)           throw std::runtime_error("tinyobj::LoadObj failed: " + err);

	std::unordered_map<std::string, int> texNameToIndex;
	m_matTexIndex.resize(materials.size(), -1);
	std::vector<std::wstring> texPaths;

	for (size_t mi = 0; mi < materials.size(); ++mi) {
		const std::string& diffMap = materials[mi].diffuse_texname;
		if (diffMap.empty()) continue;
		std::string normName = diffMap;
		for (char& c : normName) if (c == '/') c = '\\';
		auto it = texNameToIndex.find(normName);
		if (it != texNameToIndex.end()) {
			m_matTexIndex[mi] = it->second;
		}
		else {
			int idx = (int)texPaths.size();
			texNameToIndex[normName] = idx;
			m_matTexIndex[mi] = idx;
			std::string fullPath = baseDir + normName;
			int wsz = MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0);
			std::wstring wpath(wsz > 0 ? wsz - 1 : 0, L'\0');
			MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, wpath.data(), wsz);
			texPaths.push_back(wpath);
		}
	}

	std::unordered_map<std::string, int> bumpTexNameToIndex;
	std::vector<std::wstring> bumpTexPaths;
	const UINT diffuseCount = (UINT)texPaths.size();

	for (size_t mi = 0; mi < materials.size(); ++mi) {
		const std::string& bumpMap = materials[mi].bump_texname;
		if (bumpMap.empty()) continue;
		std::string normName = bumpMap;
		for (char& c : normName) if (c == '/') c = '\\';
		auto it = bumpTexNameToIndex.find(normName);
		if (it != bumpTexNameToIndex.end()) {
			continue;
		}
		int idx = (int)bumpTexPaths.size();
		bumpTexNameToIndex[normName] = idx;
		std::string fullPath = baseDir + normName;
		int wsz = MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0);
		std::wstring wpath(wsz > 0 ? wsz - 1 : 0, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, wpath.data(), wsz);
		bumpTexPaths.push_back(wpath);
	}

	ThrowIfFailed(m_directCmdListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_directCmdListAlloc.Get(), nullptr));

	UINT texCount = (UINT)texPaths.size();
	m_textures.resize(texCount);
	m_textureUploads.resize(texCount);
	for (UINT i = 0; i < texCount; ++i)
		m_textures[i] = LoadTextureFromFile(texPaths[i], m_textureUploads[i]);

	if (texCount == 0) {
		texCount = 1;
		m_textures.resize(1);
		m_textureUploads.resize(1);
		D3D12_HEAP_PROPERTIES defH = {}; defH.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC   td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		td.Width = 1; td.Height = 1; td.DepthOrArraySize = 1; td.MipLevels = 1;
		td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
		ThrowIfFailed(m_device->CreateCommittedResource(&defH, D3D12_HEAP_FLAG_NONE, &td,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(m_textures[0].GetAddressOf())));
		D3D12_HEAP_PROPERTIES upH = {}; upH.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC   ud = {}; ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		ud.Width = 256; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
		ud.Format = DXGI_FORMAT_UNKNOWN; ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		ThrowIfFailed(m_device->CreateCommittedResource(&upH, D3D12_HEAP_FLAG_NONE, &ud,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_textureUploads[0].GetAddressOf())));
		uint8_t white[4] = { 255,255,255,255 }; void* mp = nullptr;
		m_textureUploads[0]->Map(0, nullptr, &mp); memcpy(mp, white, 4); m_textureUploads[0]->Unmap(0, nullptr);
		D3D12_TEXTURE_COPY_LOCATION dst = { m_textures[0].Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{} };
		D3D12_TEXTURE_COPY_LOCATION src = {};
		src.pResource = m_textureUploads[0].Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		src.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM,1,1,1,256 };
		m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		b.Transition = { m_textures[0].Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
		m_commandList->ResourceBarrier(1, &b);
	}

	UINT oldTexCount = (UINT)m_textures.size();
	m_textures.resize(oldTexCount + bumpTexPaths.size());
	m_textureUploads.resize(m_textures.size());
	for (UINT i = 0; i < bumpTexPaths.size(); ++i)
		m_textures[oldTexCount + i] = LoadTextureFromFile(bumpTexPaths[i], m_textureUploads[oldTexCount + i]);

	BuildSrvHeap((UINT)m_textures.size());

	if (materials.empty()) {
		m_materialCBs.push_back(std::make_unique<UploadBuffer<MaterialConstants>>(m_device.Get(), 1, true));
		MaterialConstants mc{};
		mc.DiffuseAlbedo = { 1, 1, 1, 1 };
		mc.UVScale = { 1, 1 };
		mc.DiffuseTexIndex = 0;
		mc.DisplacementTexIndex = -1;
		mc.DisplacementScale = 0.0f;
		mc.DisplacementBias = 0.0f;
		m_materialCBs.back()->CopyData(0, mc);
	}
	else {
		for (size_t mi = 0; mi < materials.size(); ++mi) {
			m_materialCBs.push_back(std::make_unique<UploadBuffer<MaterialConstants>>(m_device.Get(), 1, true));
			MaterialConstants mc{};
			mc.DiffuseAlbedo = {
				materials[mi].diffuse[0], materials[mi].diffuse[1],
				materials[mi].diffuse[2], 1.0f };
			mc.UVScale = { 1,1 };
			mc.UVOffset = { 0,0 };
			mc.UVSpeed = { 0,0 };
			mc.DiffuseTexIndex = (m_matTexIndex[mi] >= 0) ? m_matTexIndex[mi] : 0;

			const std::string& bumpMap = materials[mi].bump_texname;
			if (!bumpMap.empty()) {
				std::string normName = bumpMap;
				for (char& c : normName) if (c == '/') c = '\\';
				auto it = bumpTexNameToIndex.find(normName);
				if (it != bumpTexNameToIndex.end()) {
					mc.DisplacementTexIndex = diffuseCount + it->second;
					mc.DisplacementScale = 0.05f;
					mc.DisplacementBias = 0.0f;
				}
				else {
					mc.DisplacementTexIndex = -1;
					mc.DisplacementScale = 0.0f;
					mc.DisplacementBias = 0.0f;
				}
			}
			else {
				mc.DisplacementTexIndex = -1;
				mc.DisplacementScale = 0.0f;
				mc.DisplacementBias = 0.0f;
			}

			if (materials[mi].name == "fabric_a" ||
				materials[mi].name == "fabric_b" ||
				materials[mi].name == "fabric_c" ||
				materials[mi].name == "fabric_d" ||
				materials[mi].name == "fabric_e" ||
				materials[mi].name == "fabric_f" ||
				materials[mi].name == "fabric_g")
			{
				mc.UVSpeed = { 0.05f, 0.f };
			}

			if (materials[mi].name == "bricks") {
				mc.UVScale = { 0.2f, 0.2f };
			}

			std::unordered_map<std::string, int> normalTexNameToIndex;
			std::vector<std::wstring> normalTexPaths;
			const UINT bumpOffset = (UINT)m_textures.size();

			for (size_t mi = 0; mi < materials.size(); ++mi) {
				const std::string& normMap = materials[mi].normal_texname;
				if (normMap.empty()) continue;
				std::string normName = normMap;
				for (char& c : normName) if (c == '/') c = '\\';
				auto it = normalTexNameToIndex.find(normName);
				if (it != normalTexNameToIndex.end()) continue;
				int idx = (int)normalTexPaths.size();
				normalTexNameToIndex[normName] = idx;
				std::string fullPath = baseDir + normName;
				int wsz = MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0);
				std::wstring wpath(wsz > 0 ? wsz - 1 : 0, L'\0');
				MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, wpath.data(), wsz);
				normalTexPaths.push_back(wpath);
			}

			UINT oldTexCount3 = (UINT)m_textures.size();
			m_textures.resize(oldTexCount3 + normalTexPaths.size());
			m_textureUploads.resize(m_textures.size());
			for (UINT i = 0; i < normalTexPaths.size(); ++i)
				m_textures[oldTexCount3 + i] = LoadTextureFromFile(normalTexPaths[i], m_textureUploads[oldTexCount3 + i]);
			
			const std::string& normMap = materials[mi].normal_texname;
			if (!normMap.empty()) {
				std::string normName = normMap;
				for (char& c : normName) if (c == '/') c = '\\';
				auto it = normalTexNameToIndex.find(normName);
				if (it != normalTexNameToIndex.end())
					mc.NormalTexIndex = diffuseCount + bumpTexPaths.size() + it->second;
				else
					mc.NormalTexIndex = -1;
			}
			else {
				mc.NormalTexIndex = -1;
			}

			BuildSrvHeap((UINT)m_textures.size());

			m_materialCBs.back()->CopyData(0, mc);
		}
	}

	const bool hasNormals = !attrib.normals.empty();
	auto ReadPos = [&](int v) -> XMFLOAT3 { return { attrib.vertices[3 * v], attrib.vertices[3 * v + 1], attrib.vertices[3 * v + 2] }; };
	auto ReadNrm = [&](int n) -> XMFLOAT3 {
		if (hasNormals && n >= 0) return { attrib.normals[3 * n], attrib.normals[3 * n + 1], attrib.normals[3 * n + 2] };
		return { 0,1,0 };
		};
	auto ReadTex = [&](int t) -> XMFLOAT2 {
		if (!attrib.texcoords.empty() && t >= 0)
			return { attrib.texcoords[2 * t], 1.0f - attrib.texcoords[2 * t + 1] };
		return { 0,0 };
		};

	struct FaceVerts { Vertex v[3]; };
	std::unordered_map<int, std::vector<FaceVerts>> facesByMat;
	XMFLOAT3 minP = { +FLT_MAX,+FLT_MAX,+FLT_MAX }, maxP = { -FLT_MAX,-FLT_MAX,-FLT_MAX };

	for (const auto& sh : shapes) {
		size_t off = 0;
		for (size_t f = 0; f < sh.mesh.num_face_vertices.size(); ++f) {
			int fv = sh.mesh.num_face_vertices[f];
			if (fv != 3) { off += fv; continue; }
			int matId = sh.mesh.material_ids.empty() ? -1 : sh.mesh.material_ids[f];
			auto i0 = sh.mesh.indices[off], i1 = sh.mesh.indices[off + 1], i2 = sh.mesh.indices[off + 2];
			XMFLOAT3 p0 = ReadPos(i0.vertex_index), p1 = ReadPos(i1.vertex_index), p2 = ReadPos(i2.vertex_index);
			XMFLOAT3 n0 = ReadNrm(i0.normal_index), n1 = ReadNrm(i1.normal_index), n2 = ReadNrm(i2.normal_index);
			if (!hasNormals || i0.normal_index < 0 || i1.normal_index < 0 || i2.normal_index < 0) {
				XMVECTOR fn = XMVector3Normalize(XMVector3Cross(
					XMLoadFloat3(&p1) - XMLoadFloat3(&p0),
					XMLoadFloat3(&p2) - XMLoadFloat3(&p0)));
				XMStoreFloat3(&n0, fn); n1 = n0; n2 = n0;
			}
			auto Exp = [&](const XMFLOAT3& p) {
				if (p.x < minP.x)minP.x = p.x; if (p.x > maxP.x)maxP.x = p.x;
				if (p.y < minP.y)minP.y = p.y; if (p.y > maxP.y)maxP.y = p.y;
				if (p.z < minP.z)minP.z = p.z; if (p.z > maxP.z)maxP.z = p.z;
				};
			Exp(p0); Exp(p1); Exp(p2);
			FaceVerts fv3;
			fv3.v[0] = { p0, n0, ReadTex(i0.texcoord_index), {1,1,1,1} };
			fv3.v[1] = { p1, n1, ReadTex(i1.texcoord_index), {1,1,1,1} };
			fv3.v[2] = { p2, n2, ReadTex(i2.texcoord_index), {1,1,1,1} };
			facesByMat[matId].push_back(fv3);
			off += 3;
		}
	}

	std::vector<Vertex> vertices; vertices.reserve(500000);
	m_drawRanges.clear();
	for (auto& kv : facesByMat) {
		UINT start = (UINT)vertices.size();
		for (auto& f : kv.second) { vertices.push_back(f.v[0]); vertices.push_back(f.v[1]); vertices.push_back(f.v[2]); }
		m_drawRanges.push_back({ start, (UINT)(kv.second.size() * 3), kv.first });
	}
	if (vertices.empty()) throw std::runtime_error("OBJ loaded but produced 0 vertices.");

	m_modelCenter = { 0.5f * (minP.x + maxP.x), 0.5f * (minP.y + maxP.y), 0.5f * (minP.z + maxP.z) };
	float dx = maxP.x - minP.x, dy = maxP.y - minP.y, dz = maxP.z - minP.z;
	float maxDim = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
	m_modelScale = (maxDim > 1e-6f) ? (2.0f / maxDim) : 1.0f;

	m_modelVertexCount = (UINT)vertices.size();
	const UINT vbBytes = m_modelVertexCount * sizeof(Vertex);
	D3D12_HEAP_PROPERTIES upH = {}; upH.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC   vd = {}; vd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vd.Width = vbBytes; vd.Height = 1; vd.DepthOrArraySize = 1; vd.MipLevels = 1;
	vd.Format = DXGI_FORMAT_UNKNOWN; vd.SampleDesc.Count = 1; vd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ThrowIfFailed(m_device->CreateCommittedResource(&upH, D3D12_HEAP_FLAG_NONE, &vd,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(m_modelVB.GetAddressOf())));
	void* mp = nullptr;
	ThrowIfFailed(m_modelVB->Map(0, nullptr, &mp));
	memcpy(mp, vertices.data(), vbBytes);
	m_modelVB->Unmap(0, nullptr);
	m_modelVBV.BufferLocation = m_modelVB->GetGPUVirtualAddress();
	m_modelVBV.StrideInBytes = sizeof(Vertex);
	m_modelVBV.SizeInBytes = vbBytes;

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* cmds[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, cmds);
	FlushCommandQueue();
	m_textureUploads.clear();
}

static std::vector<uint8_t> LoadTGA(const std::wstring& path, UINT& outW, UINT& outH)
{
	std::wstring normPath = path;
	for (auto& c : normPath) if (c == L'/') c = L'\\';
	std::string narrow(normPath.begin(), normPath.end());
	FILE* f = nullptr;
	fopen_s(&f, narrow.c_str(), "rb");
	if (!f) return {};

	uint8_t hdr[18] = {};
	fread(hdr, 1, 18, f);
	const uint8_t idLen = hdr[0], colorMap = hdr[1], imgType = hdr[2];
	const UINT width = hdr[12] | (hdr[13] << 8), height = hdr[14] | (hdr[15] << 8);
	const uint8_t bpp = hdr[16];

	if (idLen > 0) fseek(f, idLen, SEEK_CUR);
	if (colorMap) {
		int cnt = hdr[5] | (hdr[6] << 8), sz = hdr[7];
		fseek(f, cnt * ((sz + 7) / 8), SEEK_CUR);
	}
	if ((imgType != 2 && imgType != 3) || width == 0 || height == 0) { fclose(f); return {}; }

	const UINT bpp8 = bpp / 8;
	std::vector<uint8_t> raw(width * height * bpp8);
	fread(raw.data(), 1, raw.size(), f);
	fclose(f);

	std::vector<uint8_t> rgba(width * height * 4);
	for (UINT y = 0; y < height; ++y) {
		UINT sr = height - 1 - y;
		for (UINT x = 0; x < width; ++x) {
			const uint8_t* s = raw.data() + (sr * width + x) * bpp8;
			uint8_t* d = rgba.data() + (y * width + x) * 4;
			if (imgType == 3) { d[0] = d[1] = d[2] = s[0]; d[3] = 255; }
			else if (bpp8 == 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255; }
			else { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
		}
	}
	outW = width; outH = height;
	return rgba;
}

ComPtr<ID3D12Resource> Framework::LoadTextureFromFile(
	const std::wstring& path, ComPtr<ID3D12Resource>& outUpload)
{
	UINT width = 0, height = 0;
	std::vector<uint8_t> pixels;

	std::wstring ext = path.size() > 4 ? path.substr(path.size() - 4) : L"";
	for (auto& c : ext) c = towlower(c);

	if (ext == L".tga") {
		pixels = LoadTGA(path, width, height);
	}
	else {
		ComPtr<IWICImagingFactory> wicFactory;
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
			CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wicFactory)))) return nullptr;
		ComPtr<IWICBitmapDecoder> decoder;
		if (FAILED(wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr,
			GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder))) return nullptr;
		ComPtr<IWICBitmapFrameDecode> frame;
		if (FAILED(decoder->GetFrame(0, &frame))) return nullptr;
		ComPtr<IWICFormatConverter> conv;
		wicFactory->CreateFormatConverter(&conv);
		conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
			WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut);
		conv->GetSize(&width, &height);
		const UINT rp = width * 4; pixels.resize(rp * height);
		conv->CopyPixels(nullptr, rp, (UINT)pixels.size(), pixels.data());
	}

	if (pixels.empty()) {
		OutputDebugStringW((L"[Tex] Not found: " + path + L"\n").c_str());
		return nullptr;
	}

	const UINT rp = width * 4;
	const UINT arp = (rp + 255) & ~255;
	const UINT usz = arp * height;

	D3D12_HEAP_PROPERTIES defH = {}; defH.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC td = {}; td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	td.Width = width; td.Height = height; td.DepthOrArraySize = 1; td.MipLevels = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
	ComPtr<ID3D12Resource> texture;
	ThrowIfFailed(m_device->CreateCommittedResource(&defH, D3D12_HEAP_FLAG_NONE, &td,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture)));

	D3D12_HEAP_PROPERTIES upH = {}; upH.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC ud = {}; ud.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	ud.Width = usz; ud.Height = 1; ud.DepthOrArraySize = 1; ud.MipLevels = 1;
	ud.Format = DXGI_FORMAT_UNKNOWN; ud.SampleDesc.Count = 1; ud.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	ThrowIfFailed(m_device->CreateCommittedResource(&upH, D3D12_HEAP_FLAG_NONE, &ud,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&outUpload)));

	uint8_t* mp = nullptr;
	ThrowIfFailed(outUpload->Map(0, nullptr, reinterpret_cast<void**>(&mp)));
	for (UINT row = 0; row < height; ++row)
		memcpy(mp + row * arp, pixels.data() + row * rp, rp);
	outUpload->Unmap(0, nullptr);

	D3D12_TEXTURE_COPY_LOCATION dst = { texture.Get(),D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX,{} };
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = outUpload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Footprint = { DXGI_FORMAT_R8G8B8A8_UNORM,width,height,1,arp };
	m_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	D3D12_RESOURCE_BARRIER b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	b.Transition = { texture.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
		D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE };
	m_commandList->ResourceBarrier(1, &b);

	return texture;
}

void Framework::BuildSrvHeap(UINT textureCount)
{
	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.NumDescriptors = textureCount;
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(m_device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap)));

	D3D12_CPU_DESCRIPTOR_HANDLE handle = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
	for (UINT i = 0; i < textureCount; ++i) {
		D3D12_SHADER_RESOURCE_VIEW_DESC sv = {};
		sv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		sv.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		sv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		sv.Texture2D.MipLevels = 1;
		if (m_textures[i])
			m_device->CreateShaderResourceView(m_textures[i].Get(), &sv, handle);
		handle.ptr += m_cbvSrvUavDescriptorSize;
	}
}

void Framework::OnMouseDown(HWND hwnd, WPARAM btnState, int x, int y)
{
	if (btnState & MK_RBUTTON) {
		m_rmbDown = true; m_lastMousePos = { x,y };
		SetCapture(hwnd); ShowCursor(FALSE);
	}
}

void Framework::OnMouseUp(HWND hwnd, WPARAM btnState, int x, int y)
{
	(void)btnState; (void)x; (void)y;
	if (m_rmbDown) { m_rmbDown = false; ReleaseCapture(); ShowCursor(TRUE); }
}

void Framework::OnMouseMove(HWND hwnd, WPARAM btnState, int x, int y)
{
	(void)btnState;
	if (!m_rmbDown) { m_lastMousePos = { x,y }; return; }

	int dx = x - m_lastMousePos.x, dy = y - m_lastMousePos.y;
	m_lastMousePos = { x,y };
	m_yaw += dx * m_mouseSensitivity;
	m_pitch -= dy * m_mouseSensitivity;
	const float limit = XM_PIDIV2 - 0.1f;
	m_pitch = max(-limit, min(limit, m_pitch));

	XMVECTOR forward = XMVectorSet(
		cosf(m_pitch) * sinf(m_yaw), sinf(m_pitch), cosf(m_pitch) * cosf(m_yaw), 0.f);
	forward = XMVector3Normalize(forward);
	XMVECTOR pos = XMLoadFloat3(&m_camPos);
	XMStoreFloat3(&m_camTarget, pos + forward);
}

void Framework::InitFallingLights()
{
	const int numLights = 1000;
	m_fallingLights.resize(numLights);
	for (int i = 0; i < numLights; ++i)
	{
		float x = (rand() % 4000) / 100.0f - 20.0f;
		float z = (rand() % 4000) / 100.0f - 20.0f;
		float y = (rand() % 300) / 10.0f + 5.0f;

		m_fallingLights[i].position = { x, y, z };
		m_fallingLights[i].range = 3.0f + (rand() % 50) / 10.0f;
		m_fallingLights[i].color = {
			(rand() % 100) / 100.0f,
			(rand() % 100) / 100.0f,
			(rand() % 100) / 100.0f
		};
		m_fallingLights[i].intensity = 1.0f + (rand() % 100) / 50.0f;
		m_fallingLights[i].velocityY = -2.0f - (rand() % 30) / 30.0f;
		m_fallingLights[i].groundLevel = -2.0f;
	}
}

void Framework::BuildWaterPlane()
{
	const float halfSize = 10.0f;
	const float yLevel = 0.025f;

	Vertex vertices[4] = {
		{ {-halfSize, yLevel, -halfSize}, {0,1,0}, {0,0}, {1,1,1,1} },
		{ { halfSize, yLevel, -halfSize}, {0,1,0}, {1,0}, {1,1,1,1} },
		{ {-halfSize, yLevel,  halfSize}, {0,1,0}, {0,1}, {1,1,1,1} },
		{ { halfSize, yLevel,  halfSize}, {0,1,0}, {1,1}, {1,1,1,1} }
	};

	m_waterVertexCount = 4;
	const UINT vbByteSize = sizeof(vertices);

	D3D12_HEAP_PROPERTIES uploadHeap = {};
	uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = vbByteSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> uploadBuffer;
	ThrowIfFailed(m_device->CreateCommittedResource(
		&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&uploadBuffer)));

	void* mapped = nullptr;
	ThrowIfFailed(uploadBuffer->Map(0, nullptr, &mapped));
	memcpy(mapped, vertices, vbByteSize);
	uploadBuffer->Unmap(0, nullptr);

	D3D12_HEAP_PROPERTIES defaultHeap = {};
	defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
	ThrowIfFailed(m_device->CreateCommittedResource(
		&defaultHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(&m_waterVB)));

	ThrowIfFailed(m_directCmdListAlloc->Reset());
	ThrowIfFailed(m_commandList->Reset(m_directCmdListAlloc.Get(), nullptr));
	m_commandList->CopyBufferRegion(m_waterVB.Get(), 0, uploadBuffer.Get(), 0, vbByteSize);

	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = m_waterVB.Get();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	m_commandList->ResourceBarrier(1, &barrier);

	ThrowIfFailed(m_commandList->Close());
	ID3D12CommandList* cmdLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(1, cmdLists);
	FlushCommandQueue();

	m_waterVBV.BufferLocation = m_waterVB->GetGPUVirtualAddress();
	m_waterVBV.StrideInBytes = sizeof(Vertex);
	m_waterVBV.SizeInBytes = vbByteSize;

	m_waterObjectCB = std::make_unique<UploadBuffer<ObjectConstants>>(m_device.Get(), 1, true);
	m_waterMaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(m_device.Get(), 1, true);

	ObjectConstants waterObj;
	XMStoreFloat4x4(&waterObj.World, XMMatrixIdentity());
	XMStoreFloat4x4(&waterObj.WorldInvTranspose, XMMatrixIdentity());
	m_waterObjectCB->CopyData(0, waterObj);

	MaterialConstants waterMat;
	waterMat.DiffuseAlbedo = { 0.2f, 0.5f, 0.8f, 0.9f }; 
	waterMat.UVScale = { 4.0f, 4.0f };
	waterMat.UVOffset = { 0.0f, 0.0f };
	waterMat.UVSpeed = { 0.0f, 0.0f };
	waterMat.DiffuseTexIndex = -1;
	waterMat.DisplacementTexIndex = -1;
	waterMat.NormalTexIndex = -1;
	m_waterMaterialCB->CopyData(0, waterMat);
}

void Framework::GenerateCubes(int count)
{
	m_cubeInstances.clear();
	m_cubeObjectCBs.clear();
	m_cubeMaterialCBs.clear();

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_real_distribution<float> posDist(-10.0f, 10.0f);
	std::uniform_real_distribution<float> yPosDist(0.0f, 15.0f);
	std::uniform_real_distribution<float> scaleDist(0.3f, 0.3f);
	std::uniform_real_distribution<float> colorDist(0.5f, 0.5f);

	for (int i = 0; i < count; ++i) {
		CubeInstance cube;
		cube.position.x = posDist(gen);
		cube.position.z = posDist(gen);
		cube.position.y = yPosDist(gen);
		cube.scale.x = scaleDist(gen);
		cube.scale.y = scaleDist(gen);
		cube.scale.z = scaleDist(gen);
		cube.color = { 0.5f, 0.5f, 0.5f, 1.0f};
		m_cubeInstances.push_back(cube);

		auto objCB = std::make_unique<UploadBuffer<ObjectConstants>>(m_device.Get(), 1, true);
		auto matCB = std::make_unique<UploadBuffer<MaterialConstants>>(m_device.Get(), 1, true);

		XMMATRIX scaleMat = XMMatrixScaling(cube.scale.x, cube.scale.y, cube.scale.z);
		XMMATRIX transMat = XMMatrixTranslation(cube.position.x, cube.position.y, cube.position.z);
		XMMATRIX world = scaleMat * transMat;
		XMMATRIX worldInvTranspose = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

		ObjectConstants objData;
		XMStoreFloat4x4(&objData.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&objData.WorldInvTranspose, worldInvTranspose);
		objCB->CopyData(0, objData);

		MaterialConstants matData;
		matData.DiffuseAlbedo = cube.color;
		matData.UVScale = { 1.0f, 1.0f };
		matData.UVOffset = { 0.0f, 0.0f };
		matData.UVSpeed = { 0.0f, 0.0f };
		matData.DiffuseTexIndex = -1;
		matData.DisplacementTexIndex = -1;
		matData.DisplacementScale = 0.0f;
		matData.DisplacementBias = 0.0f;
		matData.NormalTexIndex = -1;
		matCB->CopyData(0, matData);

		m_cubeObjectCBs.push_back(std::move(objCB));
		m_cubeMaterialCBs.push_back(std::move(matCB));

		AABB bounds;
		bounds.center = cube.position;
		bounds.halfExtents.x = cube.scale.x * 0.5f;
		bounds.halfExtents.y = cube.scale.y * 0.5f;
		bounds.halfExtents.z = cube.scale.z * 0.5f;
		m_cubeAABBs.push_back(bounds);
	}
}

void Framework::DrawCubes()
{
	if (!m_showCubes || m_cubeInstances.empty())
		return;

	std::vector<int> visibleIndices;

	if (m_octreeCullingEnabled && m_octree) {
		FrustumPlanes frustum = ComputeFrustumPlanes();
		XMVECTOR planes[6];
		for (int i = 0; i < 6; ++i) planes[i] = frustum.planes[i];
		m_octree->QueryFrustum(planes, visibleIndices);
	}
	else if (m_frustumCullingEnabled) {
		FrustumPlanes frustum = ComputeFrustumPlanes();
		for (size_t i = 0; i < m_cubeInstances.size(); ++i) {
			if (IsAABBInFrustum(m_cubeAABBs[i].center, m_cubeAABBs[i].halfExtents, frustum))
				visibleIndices.push_back(static_cast<int>(i));
		}
	}
	else {
		visibleIndices.resize(m_cubeInstances.size());
		for (size_t i = 0; i < m_cubeInstances.size(); ++i) visibleIndices[i] = static_cast<int>(i);
	}

	m_visibleCubeCount = static_cast<int>(visibleIndices.size());
	m_billboardInstances.clear();

	m_commandList->SetPipelineState(m_renderingSystem.GeometryPSO());
	m_commandList->SetGraphicsRootSignature(m_renderingSystem.GeometryRootSignature());
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_boxVBView);
	m_commandList->IASetIndexBuffer(&m_boxIBView);
	m_commandList->SetGraphicsRootConstantBufferView(1, m_passCB->Resource()->GetGPUVirtualAddress());

	if (m_srvHeap) {
		ID3D12DescriptorHeap* heaps[] = { m_srvHeap.Get() };
		m_commandList->SetDescriptorHeaps(1, heaps);
		m_commandList->SetGraphicsRootDescriptorTable(3, m_srvHeap->GetGPUDescriptorHandleForHeapStart());
	}

	for (int idx : visibleIndices) {
		XMVECTOR center = XMLoadFloat3(&m_cubeInstances[idx].position);
		XMVECTOR camPos = XMLoadFloat3(&m_camPos);
		float dist = XMVectorGetX(XMVector3Length(center - camPos));

		if (m_billboardEnabled && dist > m_billboardDistance) {
			BillboardInstance bb;
			bb.Position = m_cubeInstances[idx].position;
			float scale = m_cubeInstances[idx].scale.x;
			bb.Size = { scale * 2.0f, scale * 2.0f };
			bb.Color = m_cubeInstances[idx].color;
			m_billboardInstances.push_back(bb);
		}
		else {
			// рисуем куб
			m_commandList->SetGraphicsRootConstantBufferView(0, m_cubeObjectCBs[idx]->Resource()->GetGPUVirtualAddress());
			m_commandList->SetGraphicsRootConstantBufferView(2, m_cubeMaterialCBs[idx]->Resource()->GetGPUVirtualAddress());
			m_commandList->DrawIndexedInstanced(m_boxIndexCount, 1, 0, 0, 0);
		}
	}
}

Framework::FrustumPlanes Framework::ComputeFrustumPlanes() const
{
	FrustumPlanes frustum;

	float aspect = (float)m_clientWidth / (float)m_clientHeight;
	float fovY = 0.25f * XM_PI;
	float nearZ = 0.1f;
	float farZ = 1000.0f;

	float tanHalfFovY = tanf(fovY * 0.5f);
	float tanHalfFovX = tanHalfFovY * aspect;

	XMFLOAT3 nearCorners[4] = {
		{ -tanHalfFovX * nearZ,  tanHalfFovY * nearZ, nearZ },
		{  tanHalfFovX * nearZ,  tanHalfFovY * nearZ, nearZ },
		{  tanHalfFovX * nearZ, -tanHalfFovY * nearZ, nearZ },
		{ -tanHalfFovX * nearZ, -tanHalfFovY * nearZ, nearZ }
	};
	XMFLOAT3 farCorners[4] = {
		{ -tanHalfFovX * farZ,  tanHalfFovY * farZ, farZ },
		{  tanHalfFovX * farZ,  tanHalfFovY * farZ, farZ },
		{  tanHalfFovX * farZ, -tanHalfFovY * farZ, farZ },
		{ -tanHalfFovX * farZ, -tanHalfFovY * farZ, farZ }
	};

	XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_camPos), XMLoadFloat3(&m_camTarget), XMLoadFloat3(&m_camUp));
	XMMATRIX invView = XMMatrixInverse(nullptr, view);

	XMFLOAT3 worldCorners[8];
	for (int i = 0; i < 4; ++i) {
		XMStoreFloat3(&worldCorners[i], XMVector3TransformCoord(XMLoadFloat3(&nearCorners[i]), invView));
		XMStoreFloat3(&worldCorners[i + 4], XMVector3TransformCoord(XMLoadFloat3(&farCorners[i]), invView));
	}

	frustum.planes[0] = XMPlaneFromPoints(XMLoadFloat3(&worldCorners[0]), XMLoadFloat3(&worldCorners[3]), XMLoadFloat3(&worldCorners[4]));
	frustum.planes[1] = XMPlaneFromPoints(XMLoadFloat3(&worldCorners[1]), XMLoadFloat3(&worldCorners[2]), XMLoadFloat3(&worldCorners[5]));
	frustum.planes[2] = XMPlaneFromPoints(XMLoadFloat3(&worldCorners[3]), XMLoadFloat3(&worldCorners[2]), XMLoadFloat3(&worldCorners[7]));
	frustum.planes[3] = XMPlaneFromPoints(XMLoadFloat3(&worldCorners[0]), XMLoadFloat3(&worldCorners[1]), XMLoadFloat3(&worldCorners[4]));
	frustum.planes[4] = XMPlaneFromPoints(XMLoadFloat3(&worldCorners[0]), XMLoadFloat3(&worldCorners[1]), XMLoadFloat3(&worldCorners[2]));
	frustum.planes[5] = XMPlaneFromPoints(XMLoadFloat3(&worldCorners[4]), XMLoadFloat3(&worldCorners[5]), XMLoadFloat3(&worldCorners[7]));

	XMVECTOR eyePos = XMLoadFloat3(&m_camPos);
	XMVECTOR lookDir = XMVector3Normalize(XMLoadFloat3(&m_camTarget) - eyePos);
	XMVECTOR insidePoint = eyePos + lookDir * 5.0f;

	for (int i = 0; i < 6; ++i) {
		frustum.planes[i] = XMPlaneNormalize(frustum.planes[i]);
		float dot = XMVectorGetX(XMPlaneDotCoord(frustum.planes[i], insidePoint));
		if (dot < 0.0f) {
			frustum.planes[i] = XMVectorNegate(frustum.planes[i]);
		}
	}

	return frustum;
}

bool Framework::IsAABBInFrustum(const XMFLOAT3& center, const XMFLOAT3& halfExtents, const FrustumPlanes& frustum) const
{
	XMVECTOR c = XMLoadFloat3(&center);
	XMVECTOR he = XMLoadFloat3(&halfExtents);

	for (int i = 0; i < 6; ++i) {
		XMVECTOR plane = frustum.planes[i];
		float d = XMVectorGetX(XMPlaneDotCoord(plane, c));

		XMVECTOR absN = XMVectorAbs(XMVector3Normalize(plane));
		float r = XMVectorGetX(absN) * halfExtents.x +
			XMVectorGetY(absN) * halfExtents.y +
			XMVectorGetZ(absN) * halfExtents.z;

		if (d + r < 0.0f)
			return false;
	}
	return true;
}

void Framework::BuildOctree()
{
	AABB totalBounds;
	if (!m_cubeAABBs.empty()) {
		XMVECTOR minV = XMLoadFloat3(&m_cubeAABBs[0].center) - XMLoadFloat3(&m_cubeAABBs[0].halfExtents);
		XMVECTOR maxV = XMLoadFloat3(&m_cubeAABBs[0].center) + XMLoadFloat3(&m_cubeAABBs[0].halfExtents);
		for (size_t i = 1; i < m_cubeAABBs.size(); ++i) {
			XMVECTOR c = XMLoadFloat3(&m_cubeAABBs[i].center);
			XMVECTOR h = XMLoadFloat3(&m_cubeAABBs[i].halfExtents);
			XMVECTOR minC = c - h;
			XMVECTOR maxC = c + h;
			minV = XMVectorMin(minV, minC);
			maxV = XMVectorMax(maxV, maxC);
		}
		XMVECTOR center = (minV + maxV) * 0.5f;
		XMVECTOR half = (maxV - minV) * 0.5f;
		XMStoreFloat3(&totalBounds.center, center);
		XMStoreFloat3(&totalBounds.halfExtents, half);

		m_octree = std::make_unique<Octree>(totalBounds, 16, 32);
		m_octree->Build(m_cubeAABBs);

		OutputDebugStringA(("Total bounds center: " + std::to_string(totalBounds.center.x) + ", " + std::to_string(totalBounds.center.y) + ", " + std::to_string(totalBounds.center.z) + "\n").c_str());
		OutputDebugStringA(("Total bounds half: " + std::to_string(totalBounds.halfExtents.x) + ", " + std::to_string(totalBounds.halfExtents.y) + ", " + std::to_string(totalBounds.halfExtents.z) + "\n").c_str());
	}
}

void Framework::DrawOctree()
{
	if (!m_showOctree || !m_octree) return;

	std::vector<AABB> leafBounds;
	m_octree->GetAllLeafNodes(leafBounds);
	if (leafBounds.empty()) return;

	static std::vector<std::unique_ptr<UploadBuffer<ObjectConstants>>> leafBuffers;
	static size_t lastLeafCount = 0;

	if (leafBounds.size() != lastLeafCount) {
		leafBuffers.clear();
		leafBuffers.reserve(leafBounds.size());
		for (size_t i = 0; i < leafBounds.size(); ++i) {
			leafBuffers.push_back(std::make_unique<UploadBuffer<ObjectConstants>>(m_device.Get(), 1, true));
		}
		lastLeafCount = leafBounds.size();
	}

	m_commandList->SetPipelineState(m_renderingSystem.WireframePSO());
	m_commandList->SetGraphicsRootSignature(m_renderingSystem.WireframeRootSignature());
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_commandList->IASetVertexBuffers(0, 1, &m_boxVBView);
	m_commandList->IASetIndexBuffer(&m_boxIBView);
	m_commandList->SetGraphicsRootConstantBufferView(1, m_passCB->Resource()->GetGPUVirtualAddress());

	for (size_t i = 0; i < leafBounds.size(); ++i) {
		const auto& aabb = leafBounds[i];

		XMMATRIX scale = XMMatrixScaling(aabb.halfExtents.x, aabb.halfExtents.y, aabb.halfExtents.z);
		XMMATRIX trans = XMMatrixTranslation(aabb.center.x, aabb.center.y, aabb.center.z);
		XMMATRIX world = scale * trans;

		ObjectConstants obj;
		XMStoreFloat4x4(&obj.World, XMMatrixTranspose(world));
		XMStoreFloat4x4(&obj.WorldInvTranspose, XMMatrixIdentity());

		leafBuffers[i]->CopyData(0, obj);
		m_commandList->SetGraphicsRootConstantBufferView(0, leafBuffers[i]->Resource()->GetGPUVirtualAddress());
		m_commandList->DrawIndexedInstanced(m_boxIndexCount, 1, 0, 0, 0);
	}
}

void Framework::DrawBillboards()
{
	if (!m_billboardEnabled || m_billboardInstances.empty())
		return;

	if (m_billboardCBs.size() < m_billboardInstances.size()) {
		size_t oldSize = m_billboardCBs.size();
		m_billboardCBs.resize(m_billboardInstances.size());
		for (size_t i = oldSize; i < m_billboardInstances.size(); ++i) {
			m_billboardCBs[i] = std::make_unique<UploadBuffer<BillboardConstants>>(m_device.Get(), 1, true);
		}
	}

	m_commandList->SetPipelineState(m_renderingSystem.BillboardPSO());
	m_commandList->SetGraphicsRootSignature(m_renderingSystem.BillboardRootSignature());
	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	for (size_t i = 0; i < m_billboardInstances.size(); ++i) {
		BillboardConstants bb;
		bb.Position = m_billboardInstances[i].Position;
		bb.Size = m_billboardInstances[i].Size;
		bb.Color = m_billboardInstances[i].Color;
		m_billboardCBs[i]->CopyData(0, bb);
		m_commandList->SetGraphicsRootConstantBufferView(0, m_billboardCBs[i]->Resource()->GetGPUVirtualAddress());
		m_commandList->SetGraphicsRootConstantBufferView(1, m_passCB->Resource()->GetGPUVirtualAddress());
		m_commandList->DrawInstanced(4, 1, 0, 0);
	}
}