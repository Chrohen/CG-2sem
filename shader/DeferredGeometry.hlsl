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
    int gMetalnessTexIndex;
    int gRoughnessTexIndex;
    int gDisplacementTexIndex;

    float gDisplacementScale;
    float gDisplacementBias;
    int gNormalTexIndex;
    float gMetalness;
    float gRoughness;
    float gAO;
    float _padding[2];
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

struct PatchTess
{
    float EdgeTess[3] : SV_TessFactor;
    float InsideTess : SV_InsideTessFactor;
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

struct DSOutput
{
    float4 PosH : SV_POSITION;
    float3 PosW : TEXCOORD0;
    float3 NormalW : TEXCOORD1;
    float2 TexC : TEXCOORD2;
    float4 Color : COLOR;
    float3 TangentW : TEXCOORD3;
    float3 BitangentW : TEXCOORD4;
};

void ComputeTBNFromEdges(float3 p0, float3 p1, float3 p2,
                         float2 t0, float2 t1, float2 t2,
                         float3 N,
                         out float3 T, out float3 B)
{
    float3 dp1 = p1 - p0;
    float3 dp2 = p2 - p0;
    float2 duv1 = t1 - t0;
    float2 duv2 = t2 - t0;

    T = normalize(dp1 * duv2.y - dp2 * duv1.y);
    B = normalize(dp2 * duv1.x - dp1 * duv2.x);
    
    T = normalize(T - N * dot(N, T));
    float sign = dot(B, cross(N, T)) < 0.0 ? -1.0 : 1.0;
    B = cross(N, T) * sign;
}

// без тесселяции
VertexOut VS_def(VertexIn vin)
{
    VertexOut vout;

    float4 posW = mul(float4(vin.PosL, 1.0f), gWorld);
    vout.PosW = posW.xyz;
    vout.NormalW = mul(vin.Normal, (float3x3) gWorldInvTranspose);
    vout.PosH = mul(posW, gViewProj);
    vout.TexC = vin.TexC;
    vout.Color = vin.Color;
    
    vout.TangentW = float3(0, 0, 0);
    vout.BitangentW = float3(0, 0, 0);

    return vout;
}

// Для тесселяции
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

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchConstant")]
HSInput HSMain(InputPatch<HSInput, 3> patch, uint id : SV_OutputControlPointID)
{
    return patch[id];
}

PatchTess PatchConstant(InputPatch<HSInput, 3> patch)
{
    PatchTess tess;
    float3 cameraPos = gEyePosW;
    float3 patchCenter = (patch[0].PosW + patch[1].PosW + patch[2].PosW) / 3.0;
    float dist = distance(cameraPos, patchCenter);
    
    float tessFactor = lerp(gMaxTessFactor, gMinTessFactor,
                            saturate((dist - gMinTessDistance) / (gMaxTessDistance - gMinTessDistance)));

    tess.EdgeTess[0] = tessFactor;
    tess.EdgeTess[1] = tessFactor;
    tess.EdgeTess[2] = tessFactor;
    tess.InsideTess = tessFactor;
    return tess;
}

[domain("tri")]
DSOutput DSMain(PatchTess tessFactors, float3 bary : SV_DomainLocation,
                OutputPatch<DSInput, 3> patch)
{
    DSOutput dout;
    
    float3 posW = patch[0].PosW * bary.x + patch[1].PosW * bary.y + patch[2].PosW * bary.z;
    float3 normalW = normalize(patch[0].NormalW * bary.x + patch[1].NormalW * bary.y + patch[2].NormalW * bary.z);
    float2 texC = patch[0].TexC * bary.x + patch[1].TexC * bary.y + patch[2].TexC * bary.z;
    float4 color = patch[0].Color * bary.x + patch[1].Color * bary.y + patch[2].Color * bary.z;
    
    if (gDisplacementTexIndex >= 0)
    {
        float disp = gTextures[gDisplacementTexIndex].SampleLevel(gSamLinearWrap, texC, 0).r;
        posW += normalW * (disp * gDisplacementScale + gDisplacementBias);
    }
    
    float3 T, B;
    ComputeTBNFromEdges(patch[0].PosW, patch[1].PosW, patch[2].PosW,
                        patch[0].TexC, patch[1].TexC, patch[2].TexC,
                        normalW, T, B);

    dout.PosH = mul(float4(posW, 1.0f), gViewProj);
    dout.PosW = posW;
    dout.NormalW = normalW;
    dout.TexC = texC;
    dout.Color = color;
    dout.TangentW = T;
    dout.BitangentW = B;

    return dout;
}

struct PSOut
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float2 MR : SV_Target2;
};

PSOut PS(VertexOut pin)
{
    float2 uv = pin.TexC * gUVScale + gUVOffset + gUVSpeed * sin(gTime);
    float4 texSample = float4(1, 1, 1, 1);
    if (gDiffuseTexIndex >= 0)
        texSample = gTextures[gDiffuseTexIndex].Sample(gSamLinearWrap, uv);

    float3 albedo = pin.Color.rgb * gMatDiffuseAlbedo.rgb * texSample.rgb;
    float3 N = normalize(pin.NormalW);
    
    if (gNormalTexIndex >= 0)
    {
        float3 T, B;
        
        if (dot(pin.TangentW, pin.TangentW) > 0.0001f)
        {
            T = normalize(pin.TangentW);
            B = normalize(pin.BitangentW);
        }
        else
        {
            float3 dp1 = ddx(pin.PosW);
            float3 dp2 = ddy(pin.PosW);
            float2 duv1 = ddx(pin.TexC);
            float2 duv2 = ddy(pin.TexC);
            T = normalize(dp1 * duv2.y - dp2 * duv1.y);
            B = normalize(dp2 * duv1.x - dp1 * duv2.x);
        }
        
        T = normalize(T - N * dot(N, T));
        B = cross(N, T) * (dot(B, cross(N, T)) < 0.0 ? -1.0 : 1.0);

        float3x3 TBN = float3x3(T, B, N);
        float3 sampledNormal = gTextures[gNormalTexIndex].Sample(gSamLinearWrap, uv).rgb;
        sampledNormal = sampledNormal * 2.0f - 1.0f;
        
        float normalStrength = 1.0f;
        sampledNormal.xy *= normalStrength;
        sampledNormal = normalize(sampledNormal);
        
        N = normalize(mul(sampledNormal, TBN));
    }
    
    float metalness = gMetalness;
    float roughness = gRoughness;
    if (gMetalnessTexIndex >= 0)
        metalness = gTextures[gMetalnessTexIndex].Sample(gSamLinearWrap, uv).r;
    if (gRoughnessTexIndex >= 0)
        roughness = gTextures[gRoughnessTexIndex].Sample(gSamLinearWrap, uv).r;


    PSOut pout;
    pout.Albedo = float4(albedo, gMatDiffuseAlbedo.a * texSample.a);
    pout.Normal = float4(N * 0.5f + 0.5f, 1.0f);
    pout.MR = float2(metalness, roughness);
    return pout;
}