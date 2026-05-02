struct DirectionalLight
{
    float3 Direction;
    float pad0;
    float3 Color;
    float Intensity;
};

struct PointLight
{
    float3 Position;
    float Range;
    float3 Color;
    float Intensity;
};

struct SpotLight
{
    float3 Position;
    float Range;
    float3 Direction;
    float InnerCosAngle;
    float3 Color;
    float OuterCosAngle;
    float Intensity;
    float3 pad;
};

cbuffer LightingCB : register(b0)
{
    DirectionalLight gDirLights[4];
    PointLight gPointLights[16];
    SpotLight gSpotLights[8];
    
    float3 gEyePosW;
    float _pad0;

    float4 gAmbient;

    float4x4 gInvViewProj;

    float gSpecPower;
    int gNumDirLights;
    int gNumPointLights;
    int gNumSpotLights;
};

Texture2D gAlbedo : register(t0); 
Texture2D gNormal : register(t1); 
Texture2D gDepth : register(t2);

SamplerState gSamPoint : register(s0);

float3 WorldPosFromDepth(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = (1.0f - uv.y) * 2.0f - 1.0f;
    
    float4 ndcPos = float4(x, y, depth, 1.0f);
    float4 worldPos = mul(ndcPos, gInvViewProj);
    
    return worldPos.xyz / worldPos.w;
}

float3 CalcSpecular(float3 N, float3 L, float3 V, float3 color)
{
    float3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0f), gSpecPower);
    return color * spec;
}

float3 CalcDirectional(DirectionalLight light, float3 N, float3 V, float3 albedo)
{
    float3 L = normalize(-light.Direction);
    float NdotL = max(dot(N, L), 0.0f);
    float3 diff = light.Color * light.Intensity * albedo * NdotL;
    float3 spec = CalcSpecular(N, L, V, light.Color * light.Intensity) * NdotL;
    return diff + spec;
}

float Attenuate(float dist, float range)
{
    float ratio = saturate(dist / range);
    return saturate(1.0f - ratio * ratio) * (1.0f / max(dist * dist, 0.0001f));
}

float3 CalcPoint(PointLight light, float3 P, float3 N, float3 V, float3 albedo)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
        return float3(0, 0, 0);

    float3 L = toLight / dist;
    float NdotL = max(dot(N, L), 0.0f);
    float att = Attenuate(dist, light.Range);

    float3 diff = light.Color * light.Intensity * albedo * NdotL * att;
    float3 spec = CalcSpecular(N, L, V, light.Color * light.Intensity) * NdotL * att;
    return diff + spec;
}

float3 CalcSpot(SpotLight light, float3 P, float3 N, float3 V, float3 albedo)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
        return float3(0, 0, 0);

    float3 L = toLight / dist;
    float cosAngle = dot(-L, normalize(light.Direction));
    
    if (cosAngle < light.OuterCosAngle)
        return float3(0, 0, 0);
    
    float spotFactor = smoothstep(light.OuterCosAngle, light.InnerCosAngle, cosAngle);
    float NdotL = max(dot(N, L), 0.0f);
    float att = Attenuate(dist, light.Range) * spotFactor;

    float3 diff = light.Color * light.Intensity * albedo * NdotL * att;
    float3 spec = CalcSpecular(N, L, V, light.Color * light.Intensity) * NdotL * att;
    return diff + spec;
}


struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD;
};

VSOut VSFullscreen(uint id : SV_VertexID)
{
    float2 uv = float2((id & 1) * 2.0f, (id >> 1) * 2.0f);
    VSOut vout;
    vout.PosH = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    vout.UV = uv;
    return vout;
}

float4 PSLighting(VSOut pin) : SV_Target
{
    float depth = gDepth.Sample(gSamPoint, pin.UV).r;
    
    if (depth >= 1.0f)
        discard;

    float3 worldPos = WorldPosFromDepth(pin.UV, depth);
    float3 albedo = gAlbedo.Sample(gSamPoint, pin.UV).rgb;
    float3 N = gNormal.Sample(gSamPoint, pin.UV).rgb * 2.0f - 1.0f;
    N = normalize(N);
    
    float3 V = normalize(gEyePosW - worldPos);
    
    float3 color = gAmbient.rgb * albedo;
    
    for (int d = 0; d < gNumDirLights; ++d)
        color += CalcDirectional(gDirLights[d], N, V, albedo);
    
    for (int p = 0; p < gNumPointLights; ++p)
        color += CalcPoint(gPointLights[p], worldPos, N, V, albedo);
    
    for (int s = 0; s < gNumSpotLights; ++s)
        color += CalcSpot(gSpotLights[s], worldPos, N, V, albedo);

    return float4(color, 1.0f);
}
