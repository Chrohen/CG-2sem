//=================================================================================================
// PostProcess.hlsl
// Полноэкранный пост-процессинг: хроматическая аберрация, шум, scanlines, виньетка, контурная обводка
//=================================================================================================

//-----------------------------------------------------------
// Константный буфер (b0) – параметры всех эффектов
//-----------------------------------------------------------
cbuffer PostProcessCB : register(b0)
{
    float4 gVignetteParams; // x = intensity, y = power, z = inner radius, w = не используется
    float4 gVCRParams; // x = chromatic aberration, y = scanline intensity,
                                 // z = static noise intensity, w = jitter amount
    float4 gVCRTimeParams; // x = global time, y = scanline scroll speed,
                                 // z = jitter speed, w = не используется
    float4 gOutlineColor; // rgb – цвет обводки, a – множитель силы (пока не используется)
    float4 gOutlineThresholds; // x = depth threshold, y = normal threshold, z = outline strength
};

//-----------------------------------------------------------
// Входные текстуры
//-----------------------------------------------------------
Texture2D gLighting : register(t0); // результат работы шейдера освещения (HDR цвет)
Texture2D gDepth : register(t1); // буфер глубины (R24_UNORM_X8_TYPELESS)
Texture2D gNormal : register(t2); // буфер нормалей (R10G10B10A2_UNORM)

//-----------------------------------------------------------
// Сэмплеры
//-----------------------------------------------------------
SamplerState gSamPoint : register(s0); // точечный – для глубины/нормалей
SamplerState gSamLinear : register(s1); // линейный – для итогового цвета

//-----------------------------------------------------------
// Вершинный шейдер: полноэкранный треугольник
//-----------------------------------------------------------
struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 UV : TEXCOORD;
};

VSOut VSFullscreen(uint vertexID : SV_VertexID)
{
    // Генерация полноэкранного треугольника из 3 вершин (UV-координаты от 0 до 1)
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    VSOut output;
    output.PosH = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
    output.UV = uv;
    return output;
}

//-----------------------------------------------------------
// Вспомогательные функции эффектов
//-----------------------------------------------------------

// Случайное число для шума
float Random(float2 seed)
{
    return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
}

// Эффект scanline
float ScanlineEffect(float2 uv, float intensity)
{
    float time = gVCRTimeParams.x;
    float scanline = sin(uv.y * 1080.0 * 3.14159 + time * 20.0) * 0.5 + 0.5;
    scanline = pow(scanline, 2.0);
    float scroll = gVCRTimeParams.y;
    float blend = 0.5 + 0.5 * sin(time * scroll);
    return lerp(1.0, 1.0 - scanline * intensity, blend);
}

// Джиттер (дрожание) UV-координат
float2 JitterOffset(float2 uv, float amount)
{
    float time = gVCRTimeParams.x;
    float speed = gVCRTimeParams.z;
    float jitter = sin(time * speed) * amount;
    jitter += sin(time * 37.0) * amount * 0.3;
    uv.x += jitter;
    return uv;
}

// Хроматическая аберрация
float3 ChromaticAberration(float2 uv, float amount)
{
    float2 offset = float2(amount, 0.0);
    float r = gLighting.Sample(gSamLinear, uv + offset).r;
    float g = gLighting.Sample(gSamLinear, uv).g;
    float b = gLighting.Sample(gSamLinear, uv - offset).b;
    return float3(r, g, b);
}

// Виньетка
float Vignette(float2 uv)
{
    float2 centered = uv * 2.0 - 1.0;
    float dist = length(centered);
    float inner = gVignetteParams.z;
    float intensity = gVignetteParams.x;
    float power = gVignetteParams.y;
    float vignette = saturate((dist - inner) / (1.0 - inner));
    vignette = pow(vignette, power) * intensity;
    return 1.0 - vignette;
}

// Обнаружение краёв по глубине/нормали (для обводки)
bool IsEdge(float2 uv, float centerDepth, float3 centerNormal, float2 texelSize)
{
    float depthThresh = gOutlineThresholds.x;
    float normalThresh = gOutlineThresholds.y;

    const float2 offsets[4] =
    {
        float2(-texelSize.x, 0),
        float2(texelSize.x, 0),
        float2(0, -texelSize.y),
        float2(0, texelSize.y)
    };

    for (int i = 0; i < 4; ++i)
    {
        float2 neighborUV = uv + offsets[i];
        float neighborDepth = gDepth.Sample(gSamPoint, neighborUV).r;
        float3 neighborNormal = gNormal.Sample(gSamPoint, neighborUV).rgb * 2.0f - 1.0f;

        float depthDelta = abs(centerDepth - neighborDepth);
        float normalDelta = length(centerNormal - neighborNormal);

        if (depthDelta > depthThresh || normalDelta > normalThresh)
            return true;
    }
    return false;
}

//-----------------------------------------------------------
// Пиксельный шейдер пост-процессинга
//-----------------------------------------------------------
float4 PSPostProcess(VSOut pin) : SV_Target
{
    // --- 1. Базовый цвет (результат освещения) ---
    float3 color = gLighting.Sample(gSamLinear, pin.UV).rgb;

    // --- 2. Контурная обводка (outline) ---
    float depth = gDepth.Sample(gSamPoint, pin.UV).r;
    if (depth < 1.0f)   // только для видимых пикселей
    {
        float3 normal = gNormal.Sample(gSamPoint, pin.UV).rgb * 2.0f - 1.0f;
        float2 texelSize = 1.0f / float2(1920, 1080); // временно жестко, позже можно передать через константы
        bool edge = IsEdge(pin.UV, depth, normal, texelSize);
        if (edge)
        {
            float strength = gOutlineThresholds.z;
            color = lerp(color, gOutlineColor.rgb, strength);
        }
    }

    // --- 3. VCR-эффекты (джиттер, аберрация, шум, scanlines) ---
    float2 uv = pin.UV;

    // Jitter
    float jitterAmount = gVCRParams.w;
    uv = JitterOffset(uv, jitterAmount);

    // Chromatic aberration
    float abber = gVCRParams.x;
    float3 abberColor = ChromaticAberration(uv, abber);

    // Static noise
    float noiseIntensity = gVCRParams.z;
    float noise = Random(uv + floor(gVCRTimeParams.x * 10.0)) * noiseIntensity;

    // Scanlines
    float scanIntensity = gVCRParams.y;
    float scanline = ScanlineEffect(uv, scanIntensity);

    // Смешивание VCR-эффектов
    float3 finalColor = color;
    finalColor = lerp(finalColor, abberColor, 0.6);
    finalColor += noise;
    finalColor *= scanline;

    // --- 4. Виньетка ---
    float vig = Vignette(pin.UV);
    finalColor *= vig;

    return float4(finalColor, 1.0f);
}