// WaterGeometry.hlsl
// Шейдер для водной поверхности с тесселяцией (quad-патчи)
// Корректные нормали, волны, видимость сверху

// ------------------------------------------------------------
// Константные буферы
// ------------------------------------------------------------
cbuffer ObjectCB : register(b0)
{
    float4x4 gWorld;
    float4x4 gWorldInvTranspose;
};

cbuffer PassCB : register(b1)
{
    float4x4 gViewProj;
    
    float3 gEyePosW;
    float _pad0;
    
    float3 gLightDirW;
    float gTime;
    
    float4 gAmbient;
    float4 gDiffuse;
    float4 gSpecular;
    
    float gSpecPower;
    float3 _pad2;
    
    float gMinTessDistance;
    float gMaxTessDistance;
    float gMinTessFactor;
    float gMaxTessFactor;
};

cbuffer MaterialCB : register(b2)
{
    float4 gMatDiffuseAlbedo;
    
    float2 gUVScale;
    float2 gUVOffset;
    
    float2 gUVSpeed;
    int gDiffuseTexIndex;
    float _pad;
    
    int gDisplacementTexIndex;
    float gDisplacementScale;
    float gDisplacementBias;
    int gNormalTexIndex;
};

Texture2D gTextures[64] : register(t0);
SamplerState gSamLinearWrap : register(s0);

// ------------------------------------------------------------
// Входные / выходные структуры
// ------------------------------------------------------------
struct VertexIn
{
    float3 PosL : POSITION;
    float3 Normal : NORMAL;
    float2 TexC : TEXCOORD;
    float4 Color : COLOR;
};

struct HSInput
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float4 Color : COLOR;
};

struct DSInput
{
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float2 TexC : TEXCOORD;
    float4 Color : COLOR;
};

struct QuadPatchTess
{
    float EdgeTess[4] : SV_TessFactor;
    float InsideTess[2] : SV_InsideTessFactor;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 TexC : TEXCOORD2;
    float4 Color : COLOR;
    float3 TangentW : TEXCOORD3;
    float3 BitangentW : TEXCOORD4;
};

// ------------------------------------------------------------
// Вершинный шейдер
// ------------------------------------------------------------
HSInput VS(VertexIn vin)
{
    HSInput vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = mul(vin.Normal, (float3x3) gWorldInvTranspose);
    vout.TexC = vin.TexC;
    vout.Color = vin.Color;
    return vout;
}

// ------------------------------------------------------------
// Халл-шейдер
// ------------------------------------------------------------
[domain("quad")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(4)]
[patchconstantfunc("PatchConstant")]
HSInput HSMain(InputPatch<HSInput, 4> patch, uint id : SV_OutputControlPointID)
{
    return patch[id];
}

QuadPatchTess PatchConstant(InputPatch<HSInput, 4> patch, uint patchID : SV_PrimitiveID)
{
    QuadPatchTess tess;
    float3 center = (patch[0].PosW + patch[1].PosW + patch[2].PosW + patch[3].PosW) * 0.25;
    float dist = distance(gEyePosW, center);
    float t = saturate((dist - gMinTessDistance) / (gMaxTessDistance - gMinTessDistance));
    float tessFactor = lerp(gMaxTessFactor, gMinTessFactor, t);
    tessFactor = max(1.0, tessFactor * 2.0);
    tess.EdgeTess[0] = tessFactor;
    tess.EdgeTess[1] = tessFactor;
    tess.EdgeTess[2] = tessFactor;
    tess.EdgeTess[3] = tessFactor;
    tess.InsideTess[0] = tessFactor;
    tess.InsideTess[1] = tessFactor;
    return tess;
}

// ------------------------------------------------------------
// Функции волн
// ------------------------------------------------------------
float WaveHeight(float3 pos, float time)
{
    float h = 0.0;
    float amplitudeScale = 0.3f;
    h += 0.3 * sin(pos.x * 0.5 + time * 2.0) * cos(pos.z * 0.4 + time * 1.5);
    h += 0.2 * sin(pos.x * 1.2 - time * 2.5) * cos(pos.z * 0.9 + time * 1.8);
    h += 0.1 * sin(pos.x * 2.5 + time * 3.0) * sin(pos.z * 2.5 + time * 2.2);
    float radius = length(pos.xz);
    h += 0.15 * sin(radius * 1.5 - time * 4.0);
    h = h * amplitudeScale;
    return h;
}

// Вычисление нормали с правильным порядком cross
float3 ComputeWaveNormal(float3 posW, float time, float eps)
{
    float3 p = posW;
    float3 px = posW + float3(eps, 0, 0);
    float3 pz = posW + float3(0, 0, eps);
    
    float y0 = WaveHeight(p, time);
    float yx = WaveHeight(px, time);
    float yz = WaveHeight(pz, time);
    
    float3 p_displaced = float3(p.x, y0, p.z);
    float3 px_displaced = float3(px.x, yx, px.z);
    float3 pz_displaced = float3(pz.x, yz, pz.z);
    
    float3 tangent = normalize(px_displaced - p_displaced);
    float3 bitangent = normalize(pz_displaced - p_displaced);
    
    // Ключевое исправление: cross(bitangent, tangent) даёт нормаль вверх (0,1,0) для плоскости
    float3 normal = normalize(cross(bitangent, tangent));
    return normal;
}

// ------------------------------------------------------------
// Домейн-шейдер
// ------------------------------------------------------------
[domain("quad")]
VertexOut DSMain(QuadPatchTess tessFactors, float2 uv : SV_DomainLocation,
                 OutputPatch<DSInput, 4> patch)
{
    VertexOut dout;
    
    // Билинейная интерполяция
    float3 posW = lerp(lerp(patch[0].PosW, patch[1].PosW, uv.x),
                       lerp(patch[2].PosW, patch[3].PosW, uv.x), uv.y);
    float3 normalW = normalize(lerp(lerp(patch[0].NormalW, patch[1].NormalW, uv.x),
                                    lerp(patch[2].NormalW, patch[3].NormalW, uv.x), uv.y));
    float2 texC = lerp(lerp(patch[0].TexC, patch[1].TexC, uv.x),
                       lerp(patch[2].TexC, patch[3].TexC, uv.x), uv.y);
    float4 color = lerp(lerp(patch[0].Color, patch[1].Color, uv.x),
                        lerp(patch[2].Color, patch[3].Color, uv.x), uv.y);
    
    // Сохраняем исходную позицию (без смещения)
    float3 originalPos = posW;
    
    // Применяем смещение по Y
    float height = WaveHeight(originalPos, gTime);
    posW.y = height;
    
    // Вычисляем нормаль, используя исходную позицию
    const float eps = 0.01;
    normalW = ComputeWaveNormal(originalPos, gTime, eps);
    
    // Проекция
    dout.PosH = mul(float4(posW, 1.0f), gViewProj);
    dout.PosW = posW;
    dout.NormalW = normalW; // не инвертируем!
    dout.TexC = texC;
    dout.Color = color;
    dout.TangentW = float3(1, 0, 0);
    dout.BitangentW = float3(0, 0, 1);
    
    return dout;
}

// ------------------------------------------------------------
// Пиксельный шейдер
// ------------------------------------------------------------
struct PSOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

PSOut PS(VertexOut pin)
{
    float2 uv = pin.TexC * gUVScale + gUVOffset + gUVSpeed * sin(gTime);
    float4 texSample = float4(1, 1, 1, 1);
    if (gDiffuseTexIndex >= 0)
        texSample = gTextures[gDiffuseTexIndex].Sample(gSamLinearWrap, uv);
    
    float3 albedo = pin.Color.rgb * gMatDiffuseAlbedo.rgb * texSample.rgb;
    float3 N = normalize(pin.NormalW);
    
    PSOut pout;
    pout.Albedo = float4(albedo, gMatDiffuseAlbedo.a * texSample.a);
    pout.Normal = float4(N * 0.5f + 0.5f, 1.0f);
    return pout;
}