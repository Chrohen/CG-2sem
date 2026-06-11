#ifndef RENDER_STRUCTS_HPP
#define RENDER_STRUCTS_HPP

#include <DirectXMath.h>
#include <cstdint>

namespace dx {
	inline DirectX::XMFLOAT4X4 Identity4x4() {
		DirectX::XMFLOAT4X4 m;
		DirectX::XMStoreFloat4x4(&m, DirectX::XMMatrixIdentity());
		return m;
	}
}

static constexpr UINT SHADOW_CASCADE_COUNT = 4;
static constexpr UINT SHADOW_MAP_SIZE = 4096;

struct Vertex {
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
	DirectX::XMFLOAT4 Color;
};

struct alignas(16) ObjectConstants {
	DirectX::XMFLOAT4X4 World = dx::Identity4x4();
	DirectX::XMFLOAT4X4 WorldInvTranspose = dx::Identity4x4();
};

struct alignas(16) PassConstants {
	DirectX::XMFLOAT4X4 ViewProj = dx::Identity4x4();

	DirectX::XMFLOAT3 EyePosW = { 0.f, 0.f, 0.f };
	float _pad0 = 0.0f;

	DirectX::XMFLOAT3 LightDirW = { 0.f, 0.f, 0.f };
	float Time = 0.f;

	DirectX::XMFLOAT4 Ambient = { 0.2f, 0.2f, 0.2f, 1.0f };
	DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT4 Specular = { 1.0f, 1.0f, 1.0f, 1.0f };

	float SpecPower = 32.0f;
	DirectX::XMFLOAT3 _pad2 = { 0.0f, 0.0f, 0.0f };

	float MinTessDistance = 5.0f;
	float MaxTessDistance = 30.0f;
	float MinTessFactor = 8.0f;
	float MaxTessFactor = 1.0f;

	DirectX::XMFLOAT4X4 ShadowViewProj[SHADOW_CASCADE_COUNT];
	DirectX::XMFLOAT4   ShadowCascadeSplits;
};

struct alignas(16) MaterialConstants {
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1,1,1,1 };

	DirectX::XMFLOAT2 UVScale = { 1,1 };
	DirectX::XMFLOAT2 UVOffset = { 0,0 };

	DirectX::XMFLOAT2 UVSpeed = { 0,0 };
	int  DiffuseTexIndex = 0;
	float _pad0 = 0.0f;

	int   DisplacementTexIndex = -1;
	float DisplacementScale = 0.0f;
	float DisplacementBias = 0.0f;

	int   NormalTexIndex = -1;
};


static constexpr int kMaxDirLights = 4;
static constexpr int kMaxPointLights = 1024;
static constexpr int kMaxSpotLights = 8;

struct alignas(16) ShadowPassConstants {
	DirectX::XMFLOAT4X4 LightViewProj;
};

struct alignas(16) DirectionalLight
{
	DirectX::XMFLOAT3 Direction = { 0.577f, -0.577f, 0.577f };
	float pad0 = 0.0f;
	DirectX::XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
	float Intensity = 1.0f;
};
static_assert(sizeof(DirectionalLight) == 32);

struct alignas(16) PointLight
{
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	float Range = 10.0f;
	DirectX::XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
	float Intensity = 1.0f;
};
static_assert(sizeof(PointLight) == 32);

struct alignas(16) SpotLight
{
	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	float Range = 10.0f;
	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
	float InnerCosAngle = 0.95f; 
	DirectX::XMFLOAT3 Color = { 1.0f, 1.0f, 1.0f };
	float OuterCosAngle = 0.85f; 
	float Intensity = 1.0f;
	DirectX::XMFLOAT3 pad = {};
};
static_assert(sizeof(SpotLight) == 64);

struct alignas(16) LightingConstants
{
	DirectionalLight DirLights[kMaxDirLights];
	PointLight PointLights[kMaxPointLights];
	SpotLight SpotLights[kMaxSpotLights];

	DirectX::XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
	float pad0 = 0.0f;

	DirectX::XMFLOAT4 Ambient = { 0.1f, 0.1f, 0.1f, 1.0f };
	DirectX::XMFLOAT4X4 InvViewProj = {};

	float SpecPower = 32.0f;
	int NumDirLights = 0;
	int NumPointLights = 0;
	int NumSpotLights = 0;

	DirectX::XMFLOAT4X4 ShadowViewProj[SHADOW_CASCADE_COUNT];
	DirectX::XMFLOAT4   ShadowCascadeSplits;
};

struct alignas(16) BillboardConstants {
	DirectX::XMFLOAT3 Position;
	float pad0;
	DirectX::XMFLOAT2 Size;
	float pad1[2];
	DirectX::XMFLOAT4 Color;
};

static_assert(sizeof(ObjectConstants) % 16 == 0, "ObjectConstants must be 16-byte aligned sized.");
static_assert(sizeof(PassConstants) % 16 == 0, "PassConstants must be 16-byte aligned sized.");
static_assert(sizeof(MaterialConstants) == 64, "MaterialConstants size must be 64 bytes (match HLSL cbuffer)");
static_assert(alignof(MaterialConstants) == 16, "MaterialConstants must be 16-byte aligned");
static_assert(sizeof(LightingConstants) % 16 == 0, "LightingConstants must be 16-byte aligned sized.");

#endif // RENDER_STRUCTS_HPP