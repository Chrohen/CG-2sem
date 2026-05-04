cbuffer ObjectCB : register(b0)
{
    float4x4 gWorld;
};

cbuffer PassCB : register(b1)
{
    float4x4 gViewProj;
};

struct VSIn
{
    float3 PosL : POSITION;
    float3 Normal : NORMAL;
    float2 TexC : TEXCOORD;
    float4 Color : COLOR;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
};

VSOut VS(VSIn vin)
{
    VSOut vout;
    vout.PosH = mul(mul(float4(vin.PosL, 1.0f), gWorld), gViewProj);
    return vout;
}

float4 PS(VSOut pin) : SV_Target
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}