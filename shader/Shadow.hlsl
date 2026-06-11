cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};
cbuffer cbShadowPass : register(b1)
{
    float4x4 gLightViewProj;
};
Texture2D gDiffuseMap : register(t0);
SamplerState gSampler : register(s0);

struct VSIn
{
    float3 PosL : POSITION;
    float2 TexC : TEXCOORD;
};
struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};
VSOut ShadowVS(VSIn vin)
{
    VSOut vout;
    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosH = mul(posW, gLightViewProj);
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), gTexTransform);
    vout.TexC = texC.xy;
    return vout;
}
void ShadowPS(VSOut pin)
{
    float alpha = gDiffuseMap.Sample(gSampler, pin.TexC).a;
    clip(alpha - 0.01f);
}