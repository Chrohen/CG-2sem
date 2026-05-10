#ifndef FRAMEWORK_HPP
#define FRAMEWORK_HPP

#include <array>
#include <string>
#include <memory>
#include <Windows.h>
#include <windowsx.h>
#include "Window.hpp"
#include "Timer.hpp"
#include "Dx12Common.hpp"
#include "UploadBuffer.hpp"
#include "RenderStructs.hpp"
#include "Gbuffer.hpp"
#include "RenderingSystem.hpp"
#include <wincodec.h>
#include <wrl.h>
#include <vector>
#pragma comment(lib, "windowscodecs.lib")

struct AnimatedPointLight {
	DirectX::XMFLOAT3 position;
	float range;
	DirectX::XMFLOAT3 color;
	float intensity;
	float velocityY;
	float groundLevel;
};

class Framework : public IWindowMessageHandler {
public:
	explicit Framework(int width, int height, const wchar_t* title);
	virtual ~Framework();

	bool Init();
	int  Run();

	LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) override;

protected:
	virtual void CreateRtvAndDsvDescriptorHeaps();
	virtual void OnResize();
	virtual void Update(const double& dt);
	virtual void Draw();

	virtual void OnMouseDown(HWND hwnd, WPARAM btnState, int x, int y);
	virtual void OnMouseUp(HWND hwnd, WPARAM btnState, int x, int y);
	virtual void OnMouseMove(HWND hwnd, WPARAM btnState, int x, int y);

	HWND MainWnd()    const { return m_window ? m_window->GetHWND() : nullptr; }
	int  ClientWidth()  const { return m_clientWidth; }
	int  ClientHeight() const { return m_clientHeight; }

	Timer m_timer;

private:
	int            m_initWidth = 0;
	int            m_initHeight = 0;
	const wchar_t* m_title = nullptr;

	std::unique_ptr<Window> m_window;

	int  m_clientWidth = 0;
	int  m_clientHeight = 0;

	bool m_appPaused = false;
	bool m_minimized = false;
	bool m_maximized = false;
	bool m_resizing = false;

	HINSTANCE m_hInstance = nullptr;
	POINT     m_lastMousePos = { 0, 0 };

	// D3D12 core
	ComPtr<IDXGIFactory4>  m_dxgiFactory;
	ComPtr<IDXGIAdapter1>  m_dxgiAdapter;
	ComPtr<ID3D12Device>   m_device;
	std::wstring           m_adapterName;

	ComPtr<ID3D12CommandQueue>         m_commandQueue;
	ComPtr<ID3D12CommandAllocator>     m_directCmdListAlloc;
	ComPtr<ID3D12GraphicsCommandList>  m_commandList;

	ComPtr<ID3D12Fence> m_fence;
	UINT64              m_currentFence = 0;
	HANDLE              m_fenceEvent = nullptr;

	static const int SwapChainBufferCount = 2;

	ComPtr<IDXGISwapChain4> m_swapChain;
	int                     m_currBackBuffer = 0;

	DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT m_depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12DescriptorHeap> m_dsvHeap;

	UINT m_rtvDescriptorSize = 0;
	UINT m_dsvDescriptorSize = 0;
	UINT m_cbvSrvUavDescriptorSize = 0;

	ComPtr<ID3D12Resource> m_swapChainBuffer[SwapChainBufferCount];
	ComPtr<ID3D12Resource> m_depthStencilBuffer;

	D3D12_VIEWPORT m_screenViewport = {};
	D3D12_RECT     m_scissorRect = {};

	// Deferred rendering
	Gbuffer         m_gbuffer;
	RenderingSystem m_renderingSystem;

	// Constant buffers
	std::unique_ptr<UploadBuffer<ObjectConstants>>  m_objectCB;
	std::unique_ptr<UploadBuffer<PassConstants>>    m_passCB;
	std::unique_ptr<UploadBuffer<LightingConstants>> m_lightingCB;


	ComPtr<ID3D12DescriptorHeap> m_gbufferSrvHeap;


	// Model resources
	std::vector<ComPtr<ID3D12Resource>> m_textures;
	std::vector<ComPtr<ID3D12Resource>> m_textureUploads;
	ComPtr<ID3D12DescriptorHeap>        m_srvHeap;

	std::vector<std::unique_ptr<UploadBuffer<MaterialConstants>>> m_materialCBs;
	std::vector<int> m_matTexIndex;

	struct DrawRange {
		UINT startVertex;
		UINT vertexCount;
		int  materialId;
	};
	std::vector<DrawRange> m_drawRanges;

	ComPtr<ID3D12Resource>   m_modelVB;
	D3D12_VERTEX_BUFFER_VIEW m_modelVBV{};
	UINT                     m_modelVertexCount = 0;

	DirectX::XMFLOAT3 m_modelCenter = { 0.0f, 0.0f, 0.0f };
	float             m_modelScale = 1.0f;

	// Box
	ComPtr<ID3D12Resource>   m_boxVB;
	ComPtr<ID3D12Resource>   m_boxIB;
	ComPtr<ID3D12Resource>   m_boxVBUpload;
	ComPtr<ID3D12Resource>   m_boxIBUpload;
	D3D12_VERTEX_BUFFER_VIEW m_boxVBView = {};
	D3D12_INDEX_BUFFER_VIEW  m_boxIBView = {};
	UINT                     m_boxIndexCount = 0;


	bool m_tessellationEnabled = false;

	// Camera & input
	std::array<bool, 256> m_keyDown{};
	float             m_cameraMoveSpeed = 3.0f;
	DirectX::XMFLOAT3 m_camPos = { 2.0f, 2.0f, -5.0f };
	DirectX::XMFLOAT3 m_camTarget = { 0.0f, 0.0f,  0.0f };
	DirectX::XMFLOAT3 m_camUp = { 0.0f, 1.0f,  0.0f };
	bool  m_rmbDown = false;
	float m_yaw = 0.0f;
	float m_pitch = 0.0f;
	float m_mouseSensitivity = 0.0025f;

	void InitDxgi();
	void PickAdapter();
	void LogAdapters();
	void LogAdapterOutputs(IDXGIAdapter1* adapter);
	void InitD3D12Device();
	void CreateCommandObjects();
	void CreateFence();
	void FlushCommandQueue();
	void CreateSwapChain();

	void BuildConstantBuffers();
	void BuildGBufferSrvHeap();
	void BuildObjVB_Upload();
	void BuildBoxGeometry();
	void BuildSrvHeap(UINT textureCount);

	void InitFallingLights();

	ComPtr<ID3D12Resource> LoadTextureFromFile(
		const std::wstring& path,
		ComPtr<ID3D12Resource>& outUpload);

	ID3D12Resource* CurrentBackBuffer()     const { return m_swapChainBuffer[m_currBackBuffer].Get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView()      const;

	bool m_wireframeMode = false;

	std::vector<AnimatedPointLight> m_fallingLights;
};

#endif // FRAMEWORK_HPP