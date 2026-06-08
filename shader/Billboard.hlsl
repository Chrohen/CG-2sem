cbuffer BillboardCB : register(b0)
{
    float3 gBillboardPos;
    float gPadding0;
    float2 gBillboardSize;
    float2 gPadding1;
    float4 gBillboardColor;
};

cbuffer PassCB : register(b1)
{
    float4x4 gViewProj;
    float3 gEyePosW;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float4 Color : COLOR;
};

VSOut VS(uint vertexID : SV_VertexID)
{
    float2 offsets[4] =
    {
        float2(-0.5, -0.5),
        float2(0.5, -0.5),
        float2(-0.5, 0.5),
        float2(0.5, 0.5)
    };
    float2 off = offsets[vertexID];
    
    float3 toEye = normalize(gEyePosW - gBillboardPos);
    float3 right = normalize(cross(float3(0, 1, 0), toEye));
    float3 up = cross(toEye, right);
    
    float3 worldPos = gBillboardPos + right * off.x * gBillboardSize.x + up * off.y * gBillboardSize.y;
    
    VSOut vout;
    vout.PosW = worldPos;
    vout.PosH = mul(float4(worldPos, 1.0), gViewProj);
    vout.NormalW = normalize(gEyePosW - worldPos);
    vout.Color = gBillboardColor;
    return vout;
}

struct PSOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
};

PSOut PS(VSOut pin)
{
    PSOut pout;
    pout.Albedo = pin.Color;
    pout.Normal = float4(pin.NormalW * 0.5f + 0.5f, 1.0f);
    return pout;
}