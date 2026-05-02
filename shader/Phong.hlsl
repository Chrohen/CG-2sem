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
    //float _pad1;
    float gTime;

    float4 gAmbient;
    float4 gDiffuse;
    float4 gSpecular;

    float gSpecPower;
    float3 _pad2;
};

cbuffer MaterialCB : register(b2)
{
    float4 gMatDiffuseAlbedo;

    float2 gUVScale;
    float2 gUVOffset;

    float2 gUVSpeed;
    int gDiffuseTexIndex;
    float _pad;
};

Texture2D gTextures[64] : register(t0);
SamplerState gSamLinearWrap : register(s0);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 Normal : NORMAL;
    float2 TexC : TEXCOORD;
    float4 Color : COLOR;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 TexC : TEXCOORD2;
    float4 Color : COLOR;
};

VertexOut VS(VertexIn vin)
{
    VertexOut vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;

    vout.NormalW = mul(vin.Normal, (float3x3) gWorldInvTranspose);
    vout.PosH = mul(posW, gViewProj);

    vout.TexC = vin.TexC;
    vout.Color = vin.Color;

    return vout;
}

float4 PS(VertexOut pin) : SV_Target
{
    float3 N = normalize(pin.NormalW);
    float3 L = normalize(-gLightDirW);
    float3 V = normalize(gEyePosW - pin.PosW);

    //float2 uv = pin.TexC * gUVScale + gUVOffset;
    float2 uv = pin.TexC * gUVScale + gUVOffset + gUVSpeed * sin(gTime);

    float4 tex = gTextures[gDiffuseTexIndex].Sample(gSamLinearWrap, uv);
 
    float3 base = (pin.Color.rgb * gMatDiffuseAlbedo.rgb) * tex.rgb;

    float ndotl = saturate(dot(N, L));

    float3 ambient = gAmbient.rgb * base;
    float3 diffuse = gDiffuse.rgb * base * ndotl;

    float3 R = reflect(-L, N);
    float spec = pow(saturate(dot(R, V)), gSpecPower);
    float3 specular = gSpecular.rgb * spec;

    return float4(ambient + diffuse + specular, 1.0f);
}