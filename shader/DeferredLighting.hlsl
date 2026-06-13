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
    PointLight gPointLights[1024];
    SpotLight gSpotLights[8];
    
    float3 gEyePosW;
    float _pad0;

    float4 gAmbient;

    float4x4 gInvViewProj;

    float gSpecPower;
    int gNumDirLights;
    int gNumPointLights;
    int gNumSpotLights;
    
    
    float4x4 gShadowViewProj[4];
    float4 gShadowCascadeSplits;
    
    float4 gVignetteParams;
    float4 gVCRParams;
    float4 gVCRTimeParams;
    float4 gOutlineColor;
    float4 gOutlineThresholds;
};

Texture2D gAlbedo : register(t0); 
Texture2D gNormal : register(t1); 
Texture2D gMR : register(t2);
Texture2D gDepth : register(t3);
Texture2DArray gShadowMap : register(t4);
TextureCube gIrradianceMap : register(t5);
TextureCube gPrefilteredEnvMap : register(t6);
Texture2D gBrdfLUT : register(t7);

SamplerState gSamPoint : register(s0);
SamplerComparisonState gShadowSampler : register(s1);
SamplerState gSamLinear : register(s2);

// PBR
float D_GGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (3.14159265f * denom * denom);
}

float G_SchlickGGX(float NdotV, float roughness)
{
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    return NdotV / (NdotV * (1.0f - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0 - roughness, 1.0 - roughness, 1.0 - roughness), F0) - F0) * pow(max(1.0 - cosTheta, 0.0), 5.0);
}

float3 F_Schlick(float3 F0, float VdotH)
{
    return F0 + (1.0f - F0) * pow(1.0f - VdotH, 5.0f);
}

float3 PBR_BSDF(float3 albedo, float metalness, float roughness, float3 N, float3 V, float3 L)
{
    float3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float3 F0 = lerp(0.04f, albedo, metalness);
    float3 F = F_Schlick(F0, VdotH);
    float D = D_GGX(NdotH, roughness);
    float G = G_Smith(NdotV, NdotL, roughness);

    float3 specular = D * F * G / (4.0f * NdotV * NdotL + 0.001f);

    float3 kD = (1.0f - F) * (1.0f - metalness);
    float3 diffuse = kD * albedo / 3.14159265f;

    return (diffuse + specular) * NdotL;
}

float3 WorldPosFromDepth(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = (1.0f - uv.y) * 2.0f - 1.0f;
    
    float4 ndcPos = float4(x, y, depth, 1.0f);
    float4 worldPos = mul(ndcPos, gInvViewProj);
    
    return worldPos.xyz / worldPos.w;
}

float CalcShadow(float3 worldPos)
{
    float depth = distance(worldPos, gEyePosW);
    uint cascadeIndex = 0;
    if (depth > gShadowCascadeSplits.x)
        cascadeIndex = 1;
    if (depth > gShadowCascadeSplits.y)
        cascadeIndex = 2;
    if (depth > gShadowCascadeSplits.z)
        cascadeIndex = 3;

    float4 shadowPos = mul(float4(worldPos, 1.0f), gShadowViewProj[cascadeIndex]);
    shadowPos.xyz /= shadowPos.w;
    float2 shadowTexC = shadowPos.xy * 0.5f + 0.5f;
    shadowTexC.y = 1.0f - shadowTexC.y;
    float currentDepth = shadowPos.z;

    if (currentDepth > 1.0f)
        return 1.0f;
    float shadow = 0.0f;
    float width, height, elements;
    gShadowMap.GetDimensions(width, height, elements);
    float2 texelSize = 1.0f / float2(width, height);
    currentDepth -= 0.001f;
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += gShadowMap.SampleCmpLevelZero(gShadowSampler,
                float3(shadowTexC + offset, (float) cascadeIndex), currentDepth).r;
        }
    }
    return shadow / 9.0f;
}

float Attenuate(float dist, float range)
{
    float ratio = saturate(dist / range);
    return saturate(1.0f - ratio * ratio) / (dist * dist + 0.0001f);
}

float SpotFactor(float3 L, float3 lightDir, float innerCos, float outerCos)
{
    float cosAngle = dot(-L, normalize(lightDir));
    return smoothstep(outerCos, innerCos, cosAngle);
}

float3 CalcPointLightPBR(PointLight light, float3 P, float3 N, float3 V, float3 albedo, float metalness, float roughness)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
        return float3(0, 0, 0);
    float3 L = toLight / dist;
    float att = Attenuate(dist, light.Range);
    float3 radiance = light.Color * light.Intensity * att;
    return PBR_BSDF(albedo, metalness, roughness, N, V, L) * radiance;
}

float3 CalcSpotLightPBR(SpotLight light, float3 P, float3 N, float3 V, float3 albedo, float metalness, float roughness)
{
    float3 toLight = light.Position - P;
    float dist = length(toLight);
    if (dist > light.Range)
        return float3(0, 0, 0);
    float3 L = toLight / dist;
    float spot = SpotFactor(L, light.Direction, light.InnerCosAngle, light.OuterCosAngle);
    if (spot <= 0)
        return float3(0, 0, 0);
    float att = Attenuate(dist, light.Range) * spot;
    float3 radiance = light.Color * light.Intensity * att;
    return PBR_BSDF(albedo, metalness, roughness, N, V, L) * radiance;
}

float3 CalcDirLightPBR(DirectionalLight light, float3 N, float3 V, float3 albedo, float metalness, float roughness, float3 worldPos)
{
    float3 L = normalize(-light.Direction);
    float shadow = CalcShadow(worldPos);
    float3 radiance = light.Color * light.Intensity * shadow;
    return PBR_BSDF(albedo, metalness, roughness, N, V, L) * radiance;
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

float3 evaluateDirectionalLight(DirectionalLight light, float3 N, float3 V, float3 albedo, float3 worldPos)
{
    float3 L = normalize(-light.Direction);
    float NdotL = max(dot(N, L), 0.0f);
    float shadowFactor = CalcShadow(worldPos);
    float3 diff = light.Color * light.Intensity * albedo * NdotL * shadowFactor;
    float3 spec = CalcSpecular(N, L, V, light.Color * light.Intensity) * NdotL * shadowFactor;
    return diff + spec;
}

float Random(float2 seed)
{
    return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
}

float ScanlineEffect(float2 uv, float intensity)
{
    float time = gVCRTimeParams.x;
    float scanline = sin(uv.y * 1080.0 * 3.14159 + time * 20.0) * 0.5 + 0.5;
    scanline = pow(scanline, 2.0);
    return lerp(1.0, 1.0 - scanline * intensity, 0.5 + 0.5 * sin(time * gVCRTimeParams.y));
}

float2 JitterOffset(float2 uv, float amount)
{
    float time = gVCRTimeParams.x;
    float jitter = sin(time * gVCRTimeParams.z) * amount;
    jitter += sin(time * 37.0) * amount * 0.3;
    uv.x += jitter;
    return uv;
}

bool IsEdge(float2 uv, float centerDepth, float3 centerNormal, float2 texelSize)
{
    float2 offsets[4] =
    {
        float2(-texelSize.x, 0),
        float2(texelSize.x, 0),
        float2(0, -texelSize.y),
        float2(0, texelSize.y)
    };
    
    float depthThreshold = gOutlineThresholds.x;
    float normalThreshold = gOutlineThresholds.y;
    
    for (int i = 0; i < 4; ++i)
    {
        float2 neighborUV = uv + offsets[i];
        float neighborDepth = gDepth.Sample(gSamPoint, neighborUV).r;
        float3 neighborNormal = gNormal.Sample(gSamPoint, neighborUV).rgb * 2.0f - 1.0f;
        
        float depthDelta = abs(centerDepth - neighborDepth);
        float normalDelta = length(centerNormal - neighborNormal);
        
        if (depthDelta > depthThreshold || normalDelta > normalThreshold)
            return true;
    }
    return false;
}

float4 PSLighting(VSOut pin) : SV_Target
{
    float depth = gDepth.Sample(gSamPoint, pin.UV).r;
    
    if (depth >= 1.0f)
        discard;
    
    float2 mr = gMR.Sample(gSamPoint, pin.UV).rg;
    float metalness = mr.r;
    float roughness = mr.g;
    float3 worldPos = WorldPosFromDepth(pin.UV, depth);
    float3 albedo = gAlbedo.Sample(gSamPoint, pin.UV).rgb;
    float3 N = gNormal.Sample(gSamPoint, pin.UV).rgb * 2.0f - 1.0f;
    N = normalize(N);
    
    float3 V = normalize(gEyePosW - worldPos);
    
    float NdotV = max(dot(N, V), 0.0);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metalness);
    float3 F_IBL = FresnelSchlickRoughness(NdotV, F0, roughness);
    
    float3 kS = F_IBL;
    float3 kD = 1.0 - kS;
    kD *= 1.0 - metalness;
    
    // Diffuse IBL
    float3 irradiance = gIrradianceMap.Sample(gSamLinear, N).rgb;
    float3 diffuseIBL = irradiance * albedo;
    
    // Specular IBL
    const float MAX_REFLECTION_LOD = 4.0;
    float3 R = reflect(-V, N);
    float3 prefilteredColor = gPrefilteredEnvMap.SampleLevel(gSamLinear, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = gBrdfLUT.Sample(gSamLinear, float2(NdotV, roughness)).rg;
    float3 specularIBL = prefilteredColor * (F_IBL * brdf.x + brdf.y);
    
    float3 ambient = (kD * diffuseIBL + specularIBL);
    
    float3 color = ambient;
    
    // Directional lights
    for (int d = 0; d < gNumDirLights; ++d)
    {
        color += CalcDirLightPBR(gDirLights[d], N, V, albedo, metalness, roughness, worldPos);
    }
    
    // Point lights
    for (int p = 0; p < gNumPointLights; ++p)
    {
        color += CalcPointLightPBR(gPointLights[p], worldPos, N, V, albedo, metalness, roughness);
    }
    
    // Spot lights
    for (int s = 0; s < gNumSpotLights; ++s)
    {
        color += CalcSpotLightPBR(gSpotLights[s], worldPos, N, V, albedo, metalness, roughness);
    }
    
    // обводка
    float2 texelSize = 1.0f / float2(1920, 1080);
    
    float centerDepth = depth;
    float3 centerNormal = N;
    
    bool isEdge = IsEdge(pin.UV, centerDepth, centerNormal, texelSize);
    
    float outlineStrength = gOutlineThresholds.z;
    float3 outlineColor = gOutlineColor.rgb;
    
    if (isEdge)
    {
        color = lerp(color, outlineColor, outlineStrength);
    }
    
    // VCR
    float2 uv = pin.UV;
    float time = gVCRTimeParams.x;
    
    float jitterAmount = gVCRParams.w;
    uv = JitterOffset(uv, jitterAmount);
    
    float abber = gVCRParams.x;
    float r = gAlbedo.Sample(gSamPoint, uv + float2(abber, 0)).r;
    float g_channel = gAlbedo.Sample(gSamPoint, uv).g;
    float b = gAlbedo.Sample(gSamPoint, uv - float2(abber, 0)).b;
    float3 abberColor = float3(r, g_channel, b);
    
    float noiseIntensity = gVCRParams.z;
    float noise = Random(uv + floor(time * 10.0)) * noiseIntensity;
    
    float scanIntensity = gVCRParams.y;
    float scanline = ScanlineEffect(uv, scanIntensity);
    
    float3 finalColor = color;
    finalColor = lerp(finalColor, abberColor, 0.6);
    finalColor += noise;
    finalColor *= scanline;
    
    // виньетка
    float2 centered = pin.UV * 2.0 - 1.0;
    float vignetteDist = length(centered);
    float inner = gVignetteParams.z;
    float vigIntensity = gVignetteParams.x;
    float vigPower = gVignetteParams.y;
    float vignette = saturate((vignetteDist - inner) / (1.0 - inner));
    vignette = pow(vignette, vigPower) * vigIntensity;
    vignette = 1.0 - vignette;
    finalColor *= vignette;

    return float4(finalColor, 1.0f);
}