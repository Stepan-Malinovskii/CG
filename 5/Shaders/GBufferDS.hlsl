#include "GBufferCommon.hlsl"

Texture2D gDisplacementMap : register(t2);
SamplerState gsamAnisotropicWrap : register(s4);

[domain("tri")]
DS_OUTPUT DS(HS_CONSTANT_DATA_OUTPUT input, float3 Barycentric : SV_DomainLocation, const OutputPatch<HS_CONTROL_POINT_OUTPUT, 3> TrianglePatch)
{
    DS_OUTPUT Out;

    Out.PosW = 
            Barycentric.x * TrianglePatch[0].PosW +
            Barycentric.y * TrianglePatch[1].PosW +
            Barycentric.z * TrianglePatch[2].PosW;

    Out.NormalW = normalize(
            Barycentric.x * TrianglePatch[0].NormalW +
            Barycentric.y * TrianglePatch[1].NormalW +
            Barycentric.z * TrianglePatch[2].NormalW);

    Out.TangentW = float4(normalize(
            Barycentric.x * TrianglePatch[0].TangentW.xyz +
            Barycentric.y * TrianglePatch[1].TangentW.xyz +
            Barycentric.z * TrianglePatch[2].TangentW.xyz),
            TrianglePatch[0].TangentW.w);

    Out.TexC = 
            Barycentric.x * TrianglePatch[0].TexC +
            Barycentric.y * TrianglePatch[1].TexC +
            Barycentric.z * TrianglePatch[2].TexC;

    float3 N = normalize(
    Barycentric.x * TrianglePatch[0].NormalW +
    Barycentric.y * TrianglePatch[1].NormalW +
    Barycentric.z * TrianglePatch[2].NormalW);

    float3 T = normalize(
    Barycentric.x * TrianglePatch[0].TangentW.xyz +
    Barycentric.y * TrianglePatch[1].TangentW.xyz +
    Barycentric.z * TrianglePatch[2].TangentW.xyz);

    float3 B = normalize(cross(N, T) * TrianglePatch[0].TangentW.w);
    float3x3 TBN = float3x3(T, B, N);
    
    float h = gDisplacementMap.SampleLevel(gsamAnisotropicWrap, Out.TexC, 0).r;
    h = h * gDisplacementScale + gDisplacementBias;

    float3 offsetTS = float3(0, 0, h);
    float3 offsetWS = mul(offsetTS, TBN);

    Out.PosW += offsetWS;

    Out.PosH = mul(float4(Out.PosW, 1.0f), gViewProj);

    return Out;
}