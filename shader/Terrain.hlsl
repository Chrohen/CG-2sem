cbuffer TerrainCB : register(b0)
{
    float2 gUVMin;
    float2 gUVMax;
    float2 gWorldCenterXZ;
    float gWorldSizeXZ;
    float gHeightScale;
    float gInvHeightmapSize;
    int gLevel;
    float gWorldOffsetY;
    float gMetalness;
    float gRoughness;
    float _pad[3];
};

cbuffer PassCB : register(b1)
{
    float4x4 gViewProj;

    float3 gEyePosW;
    float _p0;
    float3 gLightDirW;
    float gTime;

    float4 gAmbient;
    float4 gDiffuse;
    float4 gSpecular;

    float gSpecPower;
    float3 _p1;

    float gMinTessDistance;
    float gMaxTessDistance;
    float gMinTessFactor;
    float gMaxTessFactor;
};

Texture2D gHeightMap : register(t0);
Texture2D gDiffuseMap : register(t1);
Texture2D gNormalMap : register(t2);
SamplerState gSamLinearWrap : register(s0);

struct VertexIn
{
    float2 UV : TEXCOORD0;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 UV : TEXCOORD2;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    float2 worldXZ = gWorldCenterXZ + (vin.UV - 0.5) * gWorldSizeXZ;
    
    float2 uv = gUVMin + vin.UV * (gUVMax - gUVMin);

    float h = gHeightMap.SampleLevel(gSamLinearWrap, uv, 0).r;
    float3 posW = float3(worldXZ.x, h * gHeightScale + gWorldOffsetY, worldXZ.y);

    vout.PosW = posW;
    vout.PosH = mul(float4(posW, 1.0f), gViewProj);
    vout.UV = uv;

    float e = gInvHeightmapSize;

    float hL = gHeightMap.SampleLevel(gSamLinearWrap, uv + float2(-e, 0), 0).r;
    float hR = gHeightMap.SampleLevel(gSamLinearWrap, uv + float2(e, 0), 0).r;
    float hD = gHeightMap.SampleLevel(gSamLinearWrap, uv + float2(0, -e), 0).r;
    float hU = gHeightMap.SampleLevel(gSamLinearWrap, uv + float2(0, e), 0).r;

    float dxWorld = 2.0 * e * gWorldSizeXZ;
    float dHdx = (hR - hL) * gHeightScale / dxWorld;
    float dHdz = (hU - hD) * gHeightScale / dxWorld;

    vout.NormalW = normalize(float3(-dHdx, 1.0, -dHdz));

    return vout;
}

struct PSOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float2 MR : SV_Target2;
};

PSOut PS(VertexOut pin)
{
    PSOut pout;

    float3 albedo = gDiffuseMap.Sample(gSamLinearWrap, pin.UV).rgb;
    
    float3 N = normalize(pin.NormalW);
    
    float3 dp1 = ddx(pin.PosW);
    float3 dp2 = ddy(pin.PosW);
    float2 duv1 = ddx(pin.UV);
    float2 duv2 = ddy(pin.UV);

    float3 T = dp1 * duv2.y - dp2 * duv1.y;
    float3 B = dp2 * duv1.x - dp1 * duv2.x;

    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) < 1e-8)
    {
        T = float3(1, 0, 0);
        B = float3(0, 0, 1);
    }
    
    T = normalize(T - N * dot(N, T));
    
    float signB = (dot(B, cross(N, T)) < 0.0) ? -1.0 : 1.0;
    B = cross(N, T) * signB;
    
    float3 nTs = gNormalMap.Sample(gSamLinearWrap, pin.UV).rgb * 2.0 - 1.0;
    nTs = normalize(nTs);

    float3 Nw = normalize(T * nTs.x + B * nTs.y + N * nTs.z);
    
    pout.Albedo = float4(albedo, 1.0);
    pout.Normal = float4(Nw * 0.5 + 0.5, 1.0);
    pout.MR = float2(gMetalness, gRoughness);
    return pout;
}