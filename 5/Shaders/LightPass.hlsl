#ifndef NUM_DIR_LIGHTS
    #define NUM_DIR_LIGHTS 0
#endif

#ifndef NUM_POINT_LIGHTS
    #define NUM_POINT_LIGHTS 1
#endif

#ifndef NUM_SPOT_LIGHTS
    #define NUM_SPOT_LIGHTS 0
#endif

#include "LightingUtil.hlsl"

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);

SamplerState gsamPointClamp : register(s1);

cbuffer cbPerObject : register(b0)
{
    float4x4 gWorld;
    float4x4 gTexTransform;
};

cbuffer cbPass : register(b1)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    Light gLights[MaxLights];
};

cbuffer cbMaterial : register(b2)
{
    float4 gDiffuseAlbedo;
    float3 gFresnelR0;
    float gRoughness;
    float4x4 gMatTransform;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float3 ReconstructPosition(VSOut pin)
{
    float2 uv = pin.TexC;
    float depth = gDepth.Sample(gsamPointClamp, uv).r;
    
    float4 clip = float4(uv * 2.0f - 1.0f, depth, 1.0f);

    float4 view = mul(clip, gInvProj);
    view /= view.w;

    float4 world = mul(view, gInvView);
    return world.xyz;
}

VSOut VS(uint id : SV_VertexID)
{
    VSOut v;

    float2 pos[3] =
    {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0)
    };

    float2 uv[3] =
    {
        float2(0.0, 1.0),
        float2(0.0, -1.0),
        float2(2.0, 1.0)
    };

    v.PosH = float4(pos[id], 0.0, 1.0);
    v.TexC = uv[id];

    return v;
}

float4 PS(VSOut pin) : SV_Target
{
    float3 posW = ReconstructPosition(pin);
    
    float2 uv = pin.TexC;
    float4 albedo = gAlbedo.Sample(gsamPointClamp, uv);
    float3 normal = normalize(gNormal.Sample(gsamPointClamp, uv).xyz);

    float3 toEye = normalize(gEyePosW - posW);

    const float shininess = 1.0f - gRoughness;
    Material mat = { albedo, gFresnelR0, shininess };

    float4 lighting = ComputeLighting(gLights, mat, posW, normal, toEye, 1.0f);

    float3 finalColor = albedo.rgb * gAmbientLight.rgb + lighting.rgb;

    return float4(finalColor, 1.0f);
}