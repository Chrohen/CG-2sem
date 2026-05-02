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

	DirectX::XMFLOAT3 EyePosW = { .0f, .0f, .0f };
	float _pad0 = 0.0f;

	DirectX::XMFLOAT3 LightDirW = { .0f, .0f, .0f };
	//float _pad1 =0.f;
	float Time = .0f;

	DirectX::XMFLOAT4 Ambient = { 0.2f, 0.2f, 0.2f, 1.0f };
	DirectX::XMFLOAT4 Diffuse = { 1.0f, 1.0f, 1.0f, 1.0f };
	DirectX::XMFLOAT4 Specular = { 1.0f, 1.0f, 1.0f, 1.0f };

	float SpecPower = 32.0f;
	DirectX::XMFLOAT3 _pad2 = { 0.0f, 0.0f, 0.0f };
};

struct alignas(16) MaterialConstants {
	DirectX::XMFLOAT4 DiffuseAlbedo = { 1,1,1,1 };

	DirectX::XMFLOAT2 UVScale = { 1,1 };  
	DirectX::XMFLOAT2 UVOffset = { 0,0 };   

	DirectX::XMFLOAT2 UVSpeed = { 0,0 }; 
	int DiffuseTexIndex = 0;            
	float _pad = 0.0f;
};

static_assert(sizeof(ObjectConstants) % 16 == 0, "ObjectConstants must be 16-byte aligned sized.");
static_assert(sizeof(PassConstants) % 16 == 0, "PassConstants must be 16-byte aligned sized.");
static_assert(sizeof(MaterialConstants) % 16 == 0, "MaterialConstants must be 16-byte aligned sized.");
#endif // !RENDER_STRUCTS_HPP
